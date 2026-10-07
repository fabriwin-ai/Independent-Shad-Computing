// Linked when the library is built without the Vulkan backend.
#include "isc/backend/backend.hpp"

namespace isc {

bool vulkan_backend_compiled() { return false; }

std::unique_ptr<Backend> make_vulkan_backend(const VulkanBackendOptions&, std::string* error) {
    if (error) *error = "built without the Vulkan backend (ISC_WITH_VULKAN=OFF or Vulkan SDK not found)";
    return nullptr;
}

}  // namespace isc
