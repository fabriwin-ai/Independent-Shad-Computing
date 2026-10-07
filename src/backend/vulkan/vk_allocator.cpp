#include "backend/vulkan/vk_allocator.hpp"

#include <algorithm>

namespace isc::vk {
namespace {

VkDeviceSize align_up(VkDeviceSize v, VkDeviceSize a) { return a > 1 ? (v + a - 1) / a * a : v; }

std::string mib(VkDeviceSize b) { return std::to_string(b >> 20) + " MiB"; }

}  // namespace

void Allocator::configure(VkDeviceSize block_size, VkDeviceSize budget) {
    block_size_ = std::max<VkDeviceSize>(block_size, 1ull << 20);
    budget_ = budget;
    if (reserved_ > budget_) trim();
}

std::uint32_t Allocator::block_count() const {
    std::uint32_t n = 0;
    for (const auto& b : blocks_)
        if (b.memory) ++n;
    return n;
}

int Allocator::find_type(std::uint32_t bits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred) const {
    const auto& mp = ctx_->memory;
    for (int pass = 0; pass < 2; ++pass) {
        const VkMemoryPropertyFlags want = pass == 0 ? (required | preferred) : required;
        for (std::uint32_t t = 0; t < mp.memoryTypeCount; ++t) {
            if ((bits & (1u << t)) && (mp.memoryTypes[t].propertyFlags & want) == want) return static_cast<int>(t);
        }
    }
    return -1;
}

bool Allocator::new_block(VkDeviceSize size, std::uint32_t type, bool dedicated, std::uint32_t& index,
                          std::string* error) {
    if (reserved_ + size > budget_) {
        if (error) {
            *error = "memory budget exceeded: reserved " + mib(reserved_) + " + " + mib(size) + " > budget " +
                     mib(budget_) + " (raise memory_budget_mb or lower resolution_scale_max)";
        }
        return false;
    }
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = size;
    ai.memoryTypeIndex = type;
    Block b;
    VkResult r = vkAllocateMemory(ctx_->device, &ai, nullptr, &b.memory);
    if (r != VK_SUCCESS) {
        if (error) *error = std::string("vkAllocateMemory(") + mib(size) + ") failed: " + result_string(r);
        return false;
    }
    if (ctx_->memory.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        r = vkMapMemory(ctx_->device, b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped);  // persistently mapped
        if (r != VK_SUCCESS) {
            vkFreeMemory(ctx_->device, b.memory, nullptr);
            if (error) *error = std::string("vkMapMemory failed: ") + result_string(r);
            return false;
        }
    }
    b.size = size;
    b.type = type;
    b.dedicated = dedicated;
    b.free.push_back({0, size});
    reserved_ += size;

    for (std::uint32_t i = 0; i < blocks_.size(); ++i) {  // reuse an empty slot
        if (!blocks_[i].memory) {
            blocks_[i] = std::move(b);
            index = i;
            return true;
        }
    }
    blocks_.push_back(std::move(b));
    index = static_cast<std::uint32_t>(blocks_.size() - 1);
    return true;
}

bool Allocator::carve(Block& b, VkDeviceSize size, VkDeviceSize alignment, VkDeviceSize& offset) {
    for (std::size_t i = 0; i < b.free.size(); ++i) {
        const Range r = b.free[i];
        const VkDeviceSize start = align_up(r.offset, alignment);
        const VkDeviceSize end = r.offset + r.size;
        if (start + size > end) continue;
        b.free.erase(b.free.begin() + static_cast<std::ptrdiff_t>(i));
        std::size_t at = i;
        if (start > r.offset)
            b.free.insert(b.free.begin() + static_cast<std::ptrdiff_t>(at++), Range{r.offset, start - r.offset});
        if (start + size < end)
            b.free.insert(b.free.begin() + static_cast<std::ptrdiff_t>(at), Range{start + size, end - start - size});
        offset = start;
        return true;
    }
    return false;
}

bool Allocator::allocate(const VkMemoryRequirements& req, MemoryUsage usage, Allocation& out, std::string* error) {
    // Host-visible memory is always requested HOST_COHERENT so that no explicit
    // flush/invalidate is needed around the persistent mappings.
    constexpr VkMemoryPropertyFlags kHostCoherent =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    VkMemoryPropertyFlags required = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkMemoryPropertyFlags preferred = 0;
    switch (usage) {
        case MemoryUsage::DeviceLocal: break;
        case MemoryUsage::Upload:
            required = kHostCoherent;
            preferred = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            break;
        case MemoryUsage::Readback:
            required = kHostCoherent;
            preferred = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
            break;
    }
    int type = find_type(req.memoryTypeBits, required, preferred);
    if (type < 0 && usage == MemoryUsage::DeviceLocal) type = find_type(req.memoryTypeBits, 0, 0);
    if (type < 0) {
        if (error) *error = "no compatible Vulkan memory type";
        return false;
    }
    const auto t = static_cast<std::uint32_t>(type);

    std::uint32_t index = 0;
    VkDeviceSize offset = 0;
    if (req.size > block_size_ / 2) {  // dedicated allocation
        if (!new_block(req.size, t, true, index, error)) return false;
        Block& b = blocks_[index];
        b.free.clear();
        b.used = req.size;
    } else {
        bool found = false;
        for (std::uint32_t i = 0; i < blocks_.size() && !found; ++i) {
            Block& b = blocks_[i];
            if (!b.memory || b.dedicated || b.type != t) continue;
            if (carve(b, req.size, req.alignment, offset)) {
                index = i;
                found = true;
            }
        }
        if (!found) {
            if (!new_block(block_size_, t, false, index, error)) return false;
            carve(blocks_[index], req.size, req.alignment, offset);
        }
        blocks_[index].used += req.size;
    }

    Block& b = blocks_[index];
    out.memory = b.memory;
    out.offset = offset;
    out.size = req.size;
    out.block = index;
    out.mapped = b.mapped ? static_cast<char*>(b.mapped) + offset : nullptr;
    used_ += req.size;
    return true;
}

void Allocator::free(Allocation& a) {
    if (!a.valid() || a.block >= blocks_.size()) return;
    Block& b = blocks_[a.block];
    used_ -= a.size;
    if (b.dedicated) {
        if (b.mapped) vkUnmapMemory(ctx_->device, b.memory);
        vkFreeMemory(ctx_->device, b.memory, nullptr);
        reserved_ -= b.size;
        b = Block{};
    } else {
        b.used -= a.size;
        auto it = std::lower_bound(b.free.begin(), b.free.end(), a.offset,
                                   [](const Range& r, VkDeviceSize off) { return r.offset < off; });
        it = b.free.insert(it, Range{a.offset, a.size});
        // Coalesce with the next and previous ranges.
        auto next = it + 1;
        if (next != b.free.end() && it->offset + it->size == next->offset) {
            it->size += next->size;
            b.free.erase(next);
        }
        if (it != b.free.begin()) {
            auto prev = it - 1;
            if (prev->offset + prev->size == it->offset) {
                prev->size += it->size;
                b.free.erase(it);
            }
        }
    }
    a = Allocation{};
}

void Allocator::trim() {
    for (auto& b : blocks_) {
        if (b.memory && !b.dedicated && b.used == 0) {
            if (b.mapped) vkUnmapMemory(ctx_->device, b.memory);
            vkFreeMemory(ctx_->device, b.memory, nullptr);
            reserved_ -= b.size;
            b = Block{};
        }
    }
}

void Allocator::destroy() {
    if (!ctx_ || !ctx_->device) return;
    for (auto& b : blocks_) {
        if (!b.memory) continue;
        if (b.mapped) vkUnmapMemory(ctx_->device, b.memory);
        vkFreeMemory(ctx_->device, b.memory, nullptr);
    }
    blocks_.clear();
    reserved_ = used_ = 0;
}

}  // namespace isc::vk
