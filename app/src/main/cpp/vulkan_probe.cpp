#include "vulkan_probe.h"

#include <sstream>
#include <string>
#include <vector>

#if ASTRA_HAS_VULKAN
#include <vulkan/vulkan.h>
#endif

namespace astrans {

#if ASTRA_HAS_VULKAN
namespace {

const char* result_name(VkResult result) {
    switch (result) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        default: return "VkResult error";
    }
}

const char* device_type_name(VkPhysicalDeviceType type) {
    switch (type) {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated GPU";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete GPU";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual GPU";
        case VK_PHYSICAL_DEVICE_TYPE_CPU: return "CPU device";
        default: return "other device";
    }
}

}  // namespace

std::string vulkan_probe() {
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "AstraNS";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 5, 0);
    app_info.pEngineName = "AstraNS native probe";
    app_info.engineVersion = VK_MAKE_VERSION(0, 5, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;

    VkInstance instance = VK_NULL_HANDLE;
    const VkResult create_result = vkCreateInstance(&create_info, nullptr, &instance);
    if (create_result != VK_SUCCESS) {
        return std::string("Vulkan instance creation failed: ") + result_name(create_result);
    }

    uint32_t device_count = 0;
    VkResult result = vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
    if (result != VK_SUCCESS || device_count == 0) {
        vkDestroyInstance(instance, nullptr);
        return result == VK_SUCCESS ? "Vulkan loader found no physical GPU devices"
                                    : std::string("Vulkan device enumeration failed: ") + result_name(result);
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    result = vkEnumeratePhysicalDevices(instance, &device_count, devices.data());
    if (result != VK_SUCCESS) {
        vkDestroyInstance(instance, nullptr);
        return std::string("Vulkan device query failed: ") + result_name(result);
    }

    std::ostringstream output;
    output << "Vulkan: available; physical devices: " << device_count;
    for (uint32_t index = 0; index < device_count; ++index) {
        VkPhysicalDeviceProperties properties{};
        VkPhysicalDeviceMemoryProperties memory_properties{};
        vkGetPhysicalDeviceProperties(devices[index], &properties);
        vkGetPhysicalDeviceMemoryProperties(devices[index], &memory_properties);

        uint64_t device_local_bytes = 0;
        for (uint32_t heap = 0; heap < memory_properties.memoryHeapCount; ++heap) {
            if ((memory_properties.memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0)
                device_local_bytes += memory_properties.memoryHeaps[heap].size;
        }

        uint32_t queue_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[index], &queue_count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queue_count);
        if (queue_count != 0)
            vkGetPhysicalDeviceQueueFamilyProperties(devices[index], &queue_count, queues.data());

        bool has_graphics_queue = false;
        bool has_compute_queue = false;
        for (const VkQueueFamilyProperties& queue : queues) {
            has_graphics_queue = has_graphics_queue || (queue.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
            has_compute_queue = has_compute_queue || (queue.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
        }

        uint32_t extension_count = 0;
        const VkResult extension_result = vkEnumerateDeviceExtensionProperties(
            devices[index], nullptr, &extension_count, nullptr);
        bool has_swapchain = false;
        if (extension_result == VK_SUCCESS && extension_count != 0) {
            std::vector<VkExtensionProperties> extensions(extension_count);
            if (vkEnumerateDeviceExtensionProperties(devices[index], nullptr,
                    &extension_count, extensions.data()) == VK_SUCCESS) {
                for (const VkExtensionProperties& extension : extensions) {
                    if (std::string(extension.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME) {
                        has_swapchain = true;
                        break;
                    }
                }
            }
        }

        output << "\n\nGPU " << (index + 1) << ": " << properties.deviceName
               << "\nType: " << device_type_name(properties.deviceType)
               << "\nVulkan API: " << VK_VERSION_MAJOR(properties.apiVersion) << "."
               << VK_VERSION_MINOR(properties.apiVersion) << "."
               << VK_VERSION_PATCH(properties.apiVersion)
               << "\nDevice-local heap: " << (device_local_bytes / (1024ULL * 1024ULL)) << " MiB"
               << "\nQueue families: " << queue_count
               << " (graphics=" << (has_graphics_queue ? "yes" : "no")
               << ", compute=" << (has_compute_queue ? "yes" : "no") << ")"
               << "\nDevice extensions: " << extension_count
               << "\nVK_KHR_swapchain: " << (has_swapchain ? "yes" : "no");
    }

    vkDestroyInstance(instance, nullptr);
    return output.str();
}

#else

std::string vulkan_probe() {
    return "Vulkan support was not linked into this build.";
}

#endif

}  // namespace astrans
