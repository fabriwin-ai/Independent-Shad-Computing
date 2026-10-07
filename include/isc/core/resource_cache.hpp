#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <optional>
#include <utility>

namespace isc {

// Identifies interchangeable resources (e.g. width/height, or byte size/usage).
struct CacheKey {
    std::uint64_t a = 0;
    std::uint64_t b = 0;
    friend bool operator==(const CacheKey& x, const CacheKey& y) { return x.a == y.a && x.b == y.b; }
};

struct CacheStats {
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    std::uint64_t evictions = 0;
    std::uint64_t bytes_cached = 0;
    std::uint64_t entries = 0;
};

// Byte-budgeted LRU cache of released resources. Resources are *taken* out
// when needed and *put* back when the frame is done with them; anything that
// does not fit the budget is evicted (oldest first) through `on_evict`.
//
// Used for CPU render targets and Vulkan buffers so that dynamic-resolution
// changes do not cause per-frame allocation churn: recently used sizes stay
// warm while the controller oscillates between neighbouring rungs.
template <class T>
class ResourceCache {
public:
    using EvictFn = std::function<void(T&)>;

    explicit ResourceCache(std::uint64_t budget_bytes = 0, EvictFn on_evict = {})
        : budget_(budget_bytes), on_evict_(std::move(on_evict)) {}

    ResourceCache(const ResourceCache&) = delete;
    ResourceCache& operator=(const ResourceCache&) = delete;
    ~ResourceCache() { clear(); }

    void set_evict_callback(EvictFn fn) { on_evict_ = std::move(fn); }

    void set_budget(std::uint64_t budget_bytes) {
        budget_ = budget_bytes;
        trim();
    }
    std::uint64_t budget() const { return budget_; }

    // Most-recently-released matching entry, or nullopt (counts a miss).
    std::optional<T> take(const CacheKey& key) {
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->key == key) {
                T value = std::move(it->value);
                stats_.bytes_cached -= it->bytes;
                entries_.erase(it);
                ++stats_.hits;
                stats_.entries = entries_.size();
                return value;
            }
        }
        ++stats_.misses;
        return std::nullopt;
    }

    void put(const CacheKey& key, T value, std::uint64_t bytes) {
        if (bytes > budget_) {  // never cacheable: release immediately
            if (on_evict_) on_evict_(value);
            ++stats_.evictions;
            return;
        }
        entries_.push_front(Entry{key, std::move(value), bytes});
        stats_.bytes_cached += bytes;
        trim();
    }

    void trim() {
        while (stats_.bytes_cached > budget_ && !entries_.empty()) {
            Entry& victim = entries_.back();
            stats_.bytes_cached -= victim.bytes;
            if (on_evict_) on_evict_(victim.value);
            entries_.pop_back();
            ++stats_.evictions;
        }
        stats_.entries = entries_.size();
    }

    void clear() {
        for (auto& e : entries_) {
            if (on_evict_) on_evict_(e.value);
        }
        entries_.clear();
        stats_.bytes_cached = 0;
        stats_.entries = 0;
    }

    const CacheStats& stats() const { return stats_; }

private:
    struct Entry {
        CacheKey key;
        T value;
        std::uint64_t bytes;
    };
    std::list<Entry> entries_;  // front = most recently released
    std::uint64_t budget_;
    EvictFn on_evict_;
    CacheStats stats_;
};

}  // namespace isc
