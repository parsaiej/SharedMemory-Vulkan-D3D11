#include <climits>
#include <cstdint>
#include <dxgi.h>
#include <d3d11.h>

#include <volk.h>
#include <vulkan/vulkan_core.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <spdlog/spdlog.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#ifdef USE_RENDERDOC
    #include <renderdoc_app.h>
    RENDERDOC_API_1_1_2* pRenderDocAPI = NULL;
#endif

#include <format>
#include <wrl.h>
#include <codecvt>

using namespace Microsoft::WRL;

const UINT kTestImageWidth  = 1920;
const UINT kTestImageHeight = 1080;

bool SelectVulkanPhysicalDevice(VkInstance vkInstance, IDXGIAdapter* pDXGIAdapter, VkPhysicalDevice& vkPhysicalDevice)
{    
    uint32_t deviceCount = 0u;
    vkEnumeratePhysicalDevices(vkInstance, &deviceCount, nullptr);

    std::vector<VkPhysicalDevice> vkPhysicalDevices(deviceCount);
    vkEnumeratePhysicalDevices(vkInstance, &deviceCount, vkPhysicalDevices.data());

    DXGI_ADAPTER_DESC selectedAdapterDesc;
    pDXGIAdapter->GetDesc(&selectedAdapterDesc);

    std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> wideStringConvert;
    const auto dxgiAdapterName = wideStringConvert.to_bytes(selectedAdapterDesc.Description);

    vkPhysicalDevice = VK_NULL_HANDLE;

    for (const auto& physicalDevice : vkPhysicalDevices)
    {
        VkPhysicalDeviceProperties physicalDeviceProperties;
        vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);

        if (strcmp(dxgiAdapterName.c_str(), physicalDeviceProperties.deviceName))
            continue;

        // Found the matching Vulkan Physical Device for the existing DXGI Adapter.
        vkPhysicalDevice = physicalDevice;

        break;
    }

    return vkPhysicalDevice != VK_NULL_HANDLE;
}

bool GetVulkanGraphicsQueueIndexFromDevice(VkPhysicalDevice vkPhysicalDevice, uint32_t& graphicsQueueIndex)
{
    graphicsQueueIndex = UINT_MAX;

    uint32_t queueFamilyCount = 0u;
    vkGetPhysicalDeviceQueueFamilyProperties(vkPhysicalDevice, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(vkPhysicalDevice, &queueFamilyCount, queueFamilyProperties.data());

    for (uint32_t queueFamilyIndex = 0; queueFamilyIndex < queueFamilyCount; queueFamilyIndex++)
    {
        if (!(queueFamilyProperties[queueFamilyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            continue;

        // Choose the first graphics queue we find.
        graphicsQueueIndex = queueFamilyIndex;

        break;
    }

    return graphicsQueueIndex != UINT_MAX;
}

bool CreateVulkanLogicalDevice(VkPhysicalDevice vkPhysicalDevice, uint32_t vkGraphicsQueueIndex, VkDevice& vkLogicalDevice)
{
    float graphicsQueuePriority = 1.0;

    VkDeviceQueueCreateInfo vkGraphicsQueueCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    vkGraphicsQueueCreateInfo.queueFamilyIndex = vkGraphicsQueueIndex;
    vkGraphicsQueueCreateInfo.queueCount       = 1u;
    vkGraphicsQueueCreateInfo.pQueuePriorities = &graphicsQueuePriority;

    VkDeviceCreateInfo vkLogicalDeviceCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    vkLogicalDeviceCreateInfo.pQueueCreateInfos    = &vkGraphicsQueueCreateInfo;
    vkLogicalDeviceCreateInfo.queueCreateInfoCount = 1u;

    return vkCreateDevice(vkPhysicalDevice, &vkLogicalDeviceCreateInfo, nullptr, &vkLogicalDevice) == VK_SUCCESS;
}

bool SelectDXGIAdapter(IDXGIAdapter** ppSelectedAdapter)
{
    ComPtr<IDXGIFactory> pFactory;
    if (!SUCCEEDED(CreateDXGIFactory(IID_PPV_ARGS(&pFactory))))
        return false;

    UINT selectedAdapterIndex = UINT_MAX;

    // Select DXGI adapter based on the one that has the most dedicated video memory.
    SIZE_T dedicatedVideoMemory = 0u;

    ComPtr<IDXGIAdapter> pAdapter;
    for (UINT i = 0; pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC adapterDesc;
        pAdapter->GetDesc(&adapterDesc);

        spdlog::info(L"Found DXGI Adapter: {}", adapterDesc.Description);

        if (adapterDesc.DedicatedVideoMemory < dedicatedVideoMemory)
            continue;

        // Update largest video memory found so far.
        dedicatedVideoMemory = adapterDesc.DedicatedVideoMemory;

        // Update the selected adapter index for the current most dedicated VRAM found.
        selectedAdapterIndex = i;
    }

    if (pFactory->EnumAdapters(selectedAdapterIndex, ppSelectedAdapter) == DXGI_ERROR_NOT_FOUND)
        return false;

    DXGI_ADAPTER_DESC selectedAdapterDesc;
    (*ppSelectedAdapter)->GetDesc(&selectedAdapterDesc);

    spdlog::info(std::format(L"Selected DXGI Adapter: {}", selectedAdapterDesc.Description));

    return true;
}

int main()
{
    spdlog::info("Initializing...");
    
#ifdef USE_RENDERDOC
    if(HMODULE mod = GetModuleHandleA("renderdoc.dll"))
    {
        pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
        if (RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void **)&pRenderDocAPI) != 1)
        {
            spdlog::critical("Failed to load the RenderDoc API.");
            return 1;
        }

        spdlog::info("Loaded the RenderDoc API.");
    }
#endif

    ComPtr<IDXGIAdapter> pAdapter;
    if (!SelectDXGIAdapter(pAdapter.GetAddressOf()))
    {
        spdlog::critical("Failed to load a DXGI Adapter.");
        return 1;
    }

    D3D_FEATURE_LEVEL selectedFeatureLevel;

    UINT deviceCreationFlags = 0u;
#if defined(_DEBUG) && !defined(USE_RENDERDOC)
    deviceCreationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    ComPtr<ID3D11Device>        pDeviceDX;
    ComPtr<ID3D11DeviceContext> pImmediateContextDX;
    if (!SUCCEEDED(D3D11CreateDevice(pAdapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, deviceCreationFlags, nullptr, 0u, D3D11_SDK_VERSION, pDeviceDX.GetAddressOf(), &selectedFeatureLevel, pImmediateContextDX.GetAddressOf())))
    {
        spdlog::critical("Failed to create the D3D11 Device and Immediate Context.");
        return 1;
    }

    spdlog::info("Created D3D11 Device and Immediate Mode Context.");

#ifdef USE_RENDERDOC
    if (pRenderDocAPI) pRenderDocAPI->StartFrameCapture(pDeviceDX.Get(), nullptr);
#endif

    // Initialize Vulkan
    // ------------------------------------------------

    if (volkInitialize() != VK_SUCCESS)
    {
        spdlog::critical("Failed to initialize Volk.");
        return 1;
    }

    VkApplicationInfo vkApplicationInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    vkApplicationInfo.pApplicationName   = "SharedMemory-Vulkan-D3D11";
    vkApplicationInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    vkApplicationInfo.pEngineName        = "No Engine";
    vkApplicationInfo.engineVersion      = VK_MAKE_VERSION(0, 0, 0);
    vkApplicationInfo.apiVersion         = VK_API_VERSION_1_3;

    VkInstanceCreateInfo vkInstanceCreateInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    vkInstanceCreateInfo.pApplicationInfo = &vkApplicationInfo;

    VkInstance vkInstance;
    if (vkCreateInstance(&vkInstanceCreateInfo, nullptr, &vkInstance) != VK_SUCCESS)
    {
        spdlog::critical("Failed to create the Vulkan Instance.");
        return 1;
    }

    volkLoadInstanceOnly(vkInstance);

    VkPhysicalDevice vkPhysicalDevice;
    if (!SelectVulkanPhysicalDevice(vkInstance, pAdapter.Get(), vkPhysicalDevice))
    {
        spdlog::critical("Failed to select a Vulkan Physical Device.");
        return 1;
    }

    uint32_t vkGraphicsQueueIndex;
    if (!GetVulkanGraphicsQueueIndexFromDevice(vkPhysicalDevice, vkGraphicsQueueIndex))
    {
        spdlog::critical("Failed to get the graphics queue from the selected Vulkan Physical Device.");
        return 1;
    }

    VkDevice vkLogicalDevice;
    if (!CreateVulkanLogicalDevice(vkPhysicalDevice, vkGraphicsQueueIndex, vkLogicalDevice))
    {
        spdlog::critical("Failed to create the Vulkan Logical Device");
        return 1;
    }

    volkLoadDevice(vkLogicalDevice);

    VmaAllocator vkMemoryAllocator;
    {
        VmaVulkanFunctions vkMemoryAllocatorFunctions = {};
        vkMemoryAllocatorFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        vkMemoryAllocatorFunctions.vkGetDeviceProcAddr   = vkGetDeviceProcAddr;
        
        VmaAllocatorCreateInfo vkMemoryAllocatorCreateInfo = {};
        vkMemoryAllocatorCreateInfo.flags            = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
        vkMemoryAllocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_2;
        vkMemoryAllocatorCreateInfo.physicalDevice   = vkPhysicalDevice;
        vkMemoryAllocatorCreateInfo.device           = vkLogicalDevice;
        vkMemoryAllocatorCreateInfo.instance         = vkInstance;
        vkMemoryAllocatorCreateInfo.pVulkanFunctions = &vkMemoryAllocatorFunctions;
        
        if (vmaCreateAllocator(&vkMemoryAllocatorCreateInfo, &vkMemoryAllocator) != VK_SUCCESS)
        {
            spdlog::critical("Failed to create the Vulkan Memory Allocator.");
            return 1;
        }
    }

    // Create GPU-native Image Resource.
    // ------------------------------------------------

    D3D11_TEXTURE2D_DESC imageDesc = {};
    imageDesc.Width     = kTestImageWidth;
    imageDesc.Height    = kTestImageHeight;
    imageDesc.MipLevels = 1;
    imageDesc.ArraySize = 1;
    imageDesc.Format    = DXGI_FORMAT_R8G8B8A8_UNORM;
    imageDesc.Usage     = D3D11_USAGE_DEFAULT;
    imageDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    imageDesc.SampleDesc.Count = 1;
    imageDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

    ComPtr<ID3D11Texture2D> pImageDX;
    if (!SUCCEEDED(pDeviceDX->CreateTexture2D(&imageDesc, nullptr, pImageDX.GetAddressOf())))
    {
        spdlog::critical("Failed to create the D3D11 Image resource.");
        return 1;
    }

    // Open a shareable handle to D3D11 Image Resource.
    // ------------------------------------------------

    ComPtr<IDXGIResource> pSharedResource;
    if (!SUCCEEDED(pImageDX->QueryInterface(IID_PPV_ARGS(pSharedResource.GetAddressOf()))))
    {
        spdlog::critical("Failed.");
        return 1;
    }

    HANDLE sharedHandle;
    pSharedResource->GetSharedHandle(&sharedHandle);

    // Create CPU-Accessible Staging Image Resource.
    // ------------------------------------------------

    D3D11_TEXTURE2D_DESC stagingImageDesc = imageDesc;
    stagingImageDesc.Usage          = D3D11_USAGE_STAGING;
    stagingImageDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingImageDesc.BindFlags      = 0u;
    stagingImageDesc.MiscFlags      = 0u;
    
    ComPtr<ID3D11Texture2D> pStagingImageDX;
    if (!SUCCEEDED(pDeviceDX->CreateTexture2D(&stagingImageDesc, nullptr, pStagingImageDX.GetAddressOf())))
    {
        spdlog::critical("Failed to create the D3D11 Staging Image resource");
        return 1;
    }

    // Create Render Target View.
    // ------------------------------------------------

    ComPtr<ID3D11RenderTargetView> pRenderTargetViewDX;
    if (!SUCCEEDED(pDeviceDX->CreateRenderTargetView(pImageDX.Get(), nullptr, pRenderTargetViewDX.GetAddressOf())))
    {
        spdlog::critical("Failed to create the D3D11 Render Target View for Image resource.");
        return 1;
    }

    FLOAT clearColor[4] = { 1.0, 0.0, 0.0, 1.0 };
    pImmediateContextDX->ClearRenderTargetView(pRenderTargetViewDX.Get(), clearColor);

    // Transfer the cleared native image to staging memory.
    pImmediateContextDX->CopyResource(pStagingImageDX.Get(), pImageDX.Get());

#ifdef USE_RENDERDOC
    if (pRenderDocAPI) pRenderDocAPI->EndFrameCapture(pDeviceDX.Get(), nullptr);
#endif

    // Map to CPU for copy.
    D3D11_MAPPED_SUBRESOURCE mappedStagingMemory;
    if (!SUCCEEDED(pImmediateContextDX->Map(pStagingImageDX.Get(), 0u, D3D11_MAP_READ, 0u, &mappedStagingMemory)))
    {
        spdlog::critical("Failed to map a pointer to the staging image memory.");
        return 1;
    }
    
    // Write out the result to disk.
    stbi_write_jpg("Output.jpg", 1920, 1080, 4, mappedStagingMemory.pData, 100);

    // Release Vulkan Primitives.
    vmaDestroyAllocator(vkMemoryAllocator);
    vkDestroyDevice(vkLogicalDevice, nullptr);
    vkDestroyInstance(vkInstance, nullptr);

    return 0;
}