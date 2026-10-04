#include "Application/GraphicsDevice.hpp"

#include "Application/Window.hpp"

#include <nvrhi/nvrhi.h>

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#    include <nvrhi/d3d12.h>
#    include <directx/d3d12.h>
#    include <dxgi1_6.h>
#    include <wrl/client.h>
#else
#    include <GLFW/glfw3.h>
#    include <nvrhi/vulkan.h>
#    include <vulkan/vulkan.h>
#    define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#    include <vulkan/vulkan.hpp>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
#endif

struct GraphicsDevice::Impl
{
#if defined(_WIN32)
    Microsoft::WRL::ComPtr<IDXGIFactory4> Factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> Adapter;
    Microsoft::WRL::ComPtr<ID3D12Device> NativeDevice;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> GraphicsQueue;
    nvrhi::d3d12::DeviceHandle NvrhiDevice;

    explicit Impl(const Window& window)
    {
        if (window.NativeHandle() == nullptr)
        {
            throw std::invalid_argument("GraphicsDevice requires a live window.");
        }

        HRESULT result = CreateDXGIFactory2(0, IID_PPV_ARGS(&Factory));
        if (FAILED(result))
        {
            throw std::runtime_error("Failed to create the DXGI factory.");
        }

        for (UINT index = 0;; ++index)
        {
            result = Factory->EnumAdapters1(index, &Adapter);
            if (result == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }
            if (FAILED(result))
            {
                continue;
            }

            DXGI_ADAPTER_DESC1 description{};
            if (SUCCEEDED(Adapter->GetDesc1(&description)) &&
                (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
                SUCCEEDED(D3D12CreateDevice(
                    Adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&NativeDevice))))
            {
                break;
            }

            Adapter.Reset();
            NativeDevice.Reset();
        }

        if (!NativeDevice)
        {
            if (SUCCEEDED(Factory->EnumWarpAdapter(IID_PPV_ARGS(&Adapter))))
            {
                result = D3D12CreateDevice(
                    Adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&NativeDevice));
            }
            if (!NativeDevice)
            {
                throw std::runtime_error("No D3D12 adapter, including WARP, was found.");
            }
        }

        D3D12_COMMAND_QUEUE_DESC queueDescription{};
        queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        result = NativeDevice->CreateCommandQueue(
            &queueDescription, IID_PPV_ARGS(&GraphicsQueue));
        if (FAILED(result))
        {
            throw std::runtime_error("Failed to create the D3D12 graphics queue.");
        }

        nvrhi::d3d12::DeviceDesc description{};
        description.pDevice = NativeDevice.Get();
        description.pGraphicsCommandQueue = GraphicsQueue.Get();
        NvrhiDevice = nvrhi::d3d12::createDevice(description);
        if (!NvrhiDevice)
        {
            throw std::runtime_error("Failed to wrap the D3D12 device with NVRHI.");
        }
    }
#else
    VkInstance Instance = VK_NULL_HANDLE;
    VkSurfaceKHR Surface = VK_NULL_HANDLE;
    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    VkDevice NativeDevice = VK_NULL_HANDLE;
    VkQueue GraphicsQueue = VK_NULL_HANDLE;
    uint32_t GraphicsQueueFamily = 0;
    nvrhi::vulkan::DeviceHandle NvrhiDevice;

    static void Check(VkResult result, const char* operation)
    {
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error(
                std::string(operation) + " failed with VkResult " + std::to_string(result));
        }
    }

    explicit Impl(const Window& window)
    {
        try
        {
            auto* nativeWindow = static_cast<GLFWwindow*>(window.NativeHandle());
            if (nativeWindow == nullptr)
            {
                throw std::invalid_argument("GraphicsDevice requires a live window.");
            }

            uint32_t instanceExtensionCount = 0;
            const char** requiredExtensions = glfwGetRequiredInstanceExtensions(&instanceExtensionCount);
            if (requiredExtensions == nullptr || instanceExtensionCount == 0)
            {
                throw std::runtime_error("GLFW could not provide Vulkan surface extensions.");
            }

            VkApplicationInfo applicationInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
            applicationInfo.pApplicationName = "ArcadeEngine";
            applicationInfo.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
            applicationInfo.pEngineName = "ArcadeEngine";
            applicationInfo.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
            applicationInfo.apiVersion = VK_API_VERSION_1_2;

            VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
            instanceInfo.pApplicationInfo = &applicationInfo;
            instanceInfo.enabledExtensionCount = instanceExtensionCount;
            instanceInfo.ppEnabledExtensionNames = requiredExtensions;
            Check(vkCreateInstance(&instanceInfo, nullptr, &Instance), "vkCreateInstance");
            Check(glfwCreateWindowSurface(Instance, nativeWindow, nullptr, &Surface),
                  "glfwCreateWindowSurface");

            uint32_t physicalDeviceCount = 0;
            Check(vkEnumeratePhysicalDevices(Instance, &physicalDeviceCount, nullptr),
                  "vkEnumeratePhysicalDevices");
            if (physicalDeviceCount == 0)
            {
                throw std::runtime_error("No Vulkan physical device was found.");
            }

            std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
            Check(vkEnumeratePhysicalDevices(Instance, &physicalDeviceCount, physicalDevices.data()),
                  "vkEnumeratePhysicalDevices");

            for (const VkPhysicalDevice candidate : physicalDevices)
            {
                uint32_t queueFamilyCount = 0;
                vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, nullptr);
                std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
                vkGetPhysicalDeviceQueueFamilyProperties(
                    candidate, &queueFamilyCount, queueFamilies.data());

                for (uint32_t family = 0; family < queueFamilyCount; ++family)
                {
                    VkBool32 supportsPresent = VK_FALSE;
                    Check(vkGetPhysicalDeviceSurfaceSupportKHR(
                              candidate, family, Surface, &supportsPresent),
                          "vkGetPhysicalDeviceSurfaceSupportKHR");

                    if ((queueFamilies[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 &&
                        supportsPresent == VK_TRUE)
                    {
                        PhysicalDevice = candidate;
                        GraphicsQueueFamily = family;
                        break;
                    }
                }

                if (PhysicalDevice != VK_NULL_HANDLE)
                {
                    break;
                }
            }

            if (PhysicalDevice == VK_NULL_HANDLE)
            {
                throw std::runtime_error("No Vulkan graphics queue can present to this window.");
            }

            uint32_t deviceExtensionCount = 0;
            Check(vkEnumerateDeviceExtensionProperties(
                      PhysicalDevice, nullptr, &deviceExtensionCount, nullptr),
                  "vkEnumerateDeviceExtensionProperties");
            std::vector<VkExtensionProperties> availableExtensions(deviceExtensionCount);
            Check(vkEnumerateDeviceExtensionProperties(
                      PhysicalDevice, nullptr, &deviceExtensionCount, availableExtensions.data()),
                  "vkEnumerateDeviceExtensionProperties");
            const bool supportsSwapchain = std::any_of(
                availableExtensions.begin(), availableExtensions.end(), [](const auto& extension) {
                    return std::string_view(extension.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME;
                });
            if (!supportsSwapchain)
            {
                throw std::runtime_error("The Vulkan device does not support swapchain presentation.");
            }

            VkPhysicalDeviceVulkan12Features supportedFeatures{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
            VkPhysicalDeviceFeatures2 supportedFeatures2{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            supportedFeatures2.pNext = &supportedFeatures;
            vkGetPhysicalDeviceFeatures2(PhysicalDevice, &supportedFeatures2);
            if (supportedFeatures.timelineSemaphore != VK_TRUE)
            {
                throw std::runtime_error("The Vulkan device does not support timeline semaphores.");
            }

            const float queuePriority = 1.0f;
            VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            queueInfo.queueFamilyIndex = GraphicsQueueFamily;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &queuePriority;

            VkPhysicalDeviceVulkan12Features enabledFeatures{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
            enabledFeatures.timelineSemaphore = VK_TRUE;

            const char* enabledDeviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
            VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
            deviceInfo.pNext = &enabledFeatures;
            deviceInfo.queueCreateInfoCount = 1;
            deviceInfo.pQueueCreateInfos = &queueInfo;
            deviceInfo.enabledExtensionCount = 1;
            deviceInfo.ppEnabledExtensionNames = enabledDeviceExtensions;
            Check(vkCreateDevice(PhysicalDevice, &deviceInfo, nullptr, &NativeDevice), "vkCreateDevice");
            vkGetDeviceQueue(NativeDevice, GraphicsQueueFamily, 0, &GraphicsQueue);
            VULKAN_HPP_DEFAULT_DISPATCHER.init(Instance, vkGetInstanceProcAddr, NativeDevice);

            nvrhi::vulkan::DeviceDesc description{};
            description.instance = Instance;
            description.physicalDevice = PhysicalDevice;
            description.device = NativeDevice;
            description.graphicsQueue = GraphicsQueue;
            description.graphicsQueueIndex = static_cast<int>(GraphicsQueueFamily);
            description.instanceExtensions = requiredExtensions;
            description.numInstanceExtensions = instanceExtensionCount;
            description.deviceExtensions = enabledDeviceExtensions;
            description.numDeviceExtensions = std::size(enabledDeviceExtensions);
            NvrhiDevice = nvrhi::vulkan::createDevice(description);
            if (!NvrhiDevice)
            {
                throw std::runtime_error("Failed to wrap the Vulkan device with NVRHI.");
            }
        }
        catch (...)
        {
            Reset();
            throw;
        }
    }

    ~Impl() { Reset(); }

    void Reset() noexcept
    {
        NvrhiDevice = nullptr;
        if (NativeDevice != VK_NULL_HANDLE)
        {
            (void)vkDeviceWaitIdle(NativeDevice);
            vkDestroyDevice(NativeDevice, nullptr);
            NativeDevice = VK_NULL_HANDLE;
        }
        if (Surface != VK_NULL_HANDLE && Instance != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(Instance, Surface, nullptr);
            Surface = VK_NULL_HANDLE;
        }
        if (Instance != VK_NULL_HANDLE)
        {
            vkDestroyInstance(Instance, nullptr);
            Instance = VK_NULL_HANDLE;
        }
    }
#endif
};

GraphicsDevice::GraphicsDevice(const Window& window) : m_Impl(std::make_unique<Impl>(window)) {}

GraphicsDevice::~GraphicsDevice() = default;

bool GraphicsDevice::WaitForIdle() const
{
    auto* device = m_Impl->NvrhiDevice.Get();
    return device != nullptr && device->waitForIdle();
}
