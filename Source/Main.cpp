#include <climits>
#include <cstdint>

#include <dxgi.h>
#include <dxgidebug.h>
#include <dxgi1_6.h>

#include <d3d11.h>

#include <spdlog/spdlog.h>

// Compile Vulkan for usage of extension:
// VK_NV_external_memory_win32
#define VK_USE_PLATFORM_WIN32_KHR

#define VOLK_IMPLEMENTATION
#include <volk.h>

// This experiment just writes images to disk, no swapchain or OS window.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

constexpr const char* kOutputFileName = "Output.jpg";

#include <format>
#include <wrl.h>
#include <codecvt>
#include <filesystem>

using namespace Microsoft::WRL;

const UINT kTestImageWidth  = 1920;
const UINT kTestImageHeight = 1080;

bool SelectVulkanPhysicalDevice(const VkInstance& vkInstance, const std::vector<const char*> requiredExtensions, IDXGIAdapter* pDXGIAdapter, VkPhysicalDevice& vkPhysicalDevice)
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

    if (vkPhysicalDevice == VK_NULL_HANDLE)
        return false;

    // Confirm that the selected physical device supports the required extensions.
    uint32_t supportedDeviceExtensionCount; 
    vkEnumerateDeviceExtensionProperties(vkPhysicalDevice, nullptr, &supportedDeviceExtensionCount, nullptr);

    std::vector<VkExtensionProperties> supportedDeviceExtensions(supportedDeviceExtensionCount);
    vkEnumerateDeviceExtensionProperties(vkPhysicalDevice, nullptr, &supportedDeviceExtensionCount, supportedDeviceExtensions.data());

    auto CheckExtension = [&](const char* extensionName)
    {
        for (const auto& deviceExtension : supportedDeviceExtensions)
        {
            if (!strcmp(deviceExtension.extensionName, extensionName))
                return true;
        }

        return false;
    };

    for (const auto& requiredExtension : requiredExtensions)
    {
        if (CheckExtension(requiredExtension))
            continue;
        
        spdlog::error("The selected Vulkan physical device does not support required Vulkan Extension: {}", requiredExtension);
        return false;
    }

    return true;
}

bool GetVulkanGraphicsQueueIndexFromDevice(const VkPhysicalDevice& vkPhysicalDevice, uint32_t& graphicsQueueIndex)
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

bool CreateVulkanLogicalDevice(const VkPhysicalDevice& vkPhysicalDevice, const std::vector<const char*>& requiredExtensions, uint32_t vkGraphicsQueueIndex, VkDevice& vkLogicalDevice)
{
    float graphicsQueuePriority = 1.0;

    VkDeviceQueueCreateInfo vkGraphicsQueueCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    vkGraphicsQueueCreateInfo.queueFamilyIndex = vkGraphicsQueueIndex;
    vkGraphicsQueueCreateInfo.queueCount       = 1u;
    vkGraphicsQueueCreateInfo.pQueuePriorities = &graphicsQueuePriority;

    VkDeviceCreateInfo vkLogicalDeviceCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    vkLogicalDeviceCreateInfo.pQueueCreateInfos       = &vkGraphicsQueueCreateInfo;
    vkLogicalDeviceCreateInfo.queueCreateInfoCount    = 1u;
    vkLogicalDeviceCreateInfo.enabledExtensionCount   = (uint32_t)requiredExtensions.size();
    vkLogicalDeviceCreateInfo.ppEnabledExtensionNames = requiredExtensions.data();

    return vkCreateDevice(vkPhysicalDevice, &vkLogicalDeviceCreateInfo, nullptr, &vkLogicalDevice) == VK_SUCCESS;
}

bool SelectDXGIAdapter(IDXGIAdapter1** ppSelectedAdapter)
{
    ComPtr<IDXGIFactory1> pFactory;
    if (!SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&pFactory))))
        return false;

    UINT selectedAdapterIndex = UINT_MAX;

    // Select DXGI adapter based on the one that has the most dedicated video memory.
    SIZE_T dedicatedVideoMemory = 0u;

    ComPtr<IDXGIAdapter1> pAdapter;
    for (UINT i = 0; pFactory->EnumAdapters1(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC adapterDesc;
        pAdapter->GetDesc(&adapterDesc);

        if (adapterDesc.DedicatedVideoMemory < dedicatedVideoMemory)
            continue;

        // Update largest video memory found so far.
        dedicatedVideoMemory = adapterDesc.DedicatedVideoMemory;

        // Update the selected adapter index for the current most dedicated VRAM found.
        selectedAdapterIndex = i;
    }

    if (pFactory->EnumAdapters1(selectedAdapterIndex, ppSelectedAdapter) == DXGI_ERROR_NOT_FOUND)
        return false;

    DXGI_ADAPTER_DESC selectedAdapterDesc;
    (*ppSelectedAdapter)->GetDesc(&selectedAdapterDesc);

    spdlog::info(L"Selected DXGI Adapter: {}", selectedAdapterDesc.Description);

    return true;
}

bool BindD3D11ImageToVulkanImage(const VkDevice& vkLogicalDevice, ID3D11Texture2D* pImageDX, VkDeviceMemory& vkImageMemory, VkImage& vkImage)
{
    VkExternalMemoryImageCreateInfo vkExternalMemoryImageCreateInfo = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };
    vkExternalMemoryImageCreateInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;

    VkImageCreateInfo vkImageCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    {
        vkImageCreateInfo.pNext       = &vkExternalMemoryImageCreateInfo;
        vkImageCreateInfo.format      = VK_FORMAT_R8G8B8A8_UNORM;
        vkImageCreateInfo.imageType   = VK_IMAGE_TYPE_2D;
        vkImageCreateInfo.arrayLayers = 1u;
        vkImageCreateInfo.mipLevels   = 1u;
        vkImageCreateInfo.extent      = { kTestImageWidth, kTestImageHeight, 1};
        vkImageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkImageCreateInfo.samples     = VK_SAMPLE_COUNT_1_BIT;
        vkImageCreateInfo.usage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    if (vkCreateImage(vkLogicalDevice, &vkImageCreateInfo, nullptr, &vkImage) != VK_SUCCESS)
        return false;

    // Open a shareable handle to D3D11 Image Resource.
    ComPtr<IDXGIResource1> pSharedResource;
    if (!SUCCEEDED(pImageDX->QueryInterface(IID_PPV_ARGS(pSharedResource.GetAddressOf()))))
        return false;

    HANDLE sharedHandle;
    if (!SUCCEEDED(pSharedResource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, L"Shared-D3D11-Image", &sharedHandle)))
        return false;
    
    // Bind the Vulkan Memory Allocation to the exported D3D11 Image Resource Handle.
    VkMemoryWin32HandlePropertiesKHR vkImportedHandleProperties = { VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR };
    if (vkGetMemoryWin32HandlePropertiesKHR(vkLogicalDevice, VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT, sharedHandle, &vkImportedHandleProperties) != VK_SUCCESS)
        return false;

    // Specify that the provided Vulkan Image is the only one that can be used with the D3D11 Image memory. 
    VkMemoryDedicatedAllocateInfo vkDedicatedAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
    vkDedicatedAllocateInfo.image = vkImage;

    VkImportMemoryWin32HandleInfoKHR vkImportedHandleInfo = { VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR };
    vkImportedHandleInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
    vkImportedHandleInfo.handle     = sharedHandle;
    vkImportedHandleInfo.name       = L"Shared-D3D11-Image";
    vkImportedHandleInfo.pNext      = &vkDedicatedAllocateInfo;

    VkMemoryAllocateInfo vkImportAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    vkImportAllocateInfo.pNext           = &vkImportedHandleInfo;
    vkImportAllocateInfo.memoryTypeIndex = VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT; 

    if (vkAllocateMemory(vkLogicalDevice, &vkImportAllocateInfo, nullptr, &vkImageMemory) != VK_SUCCESS)
        return false;

    // Bind the Vulkan Image to the Memory.
    vkBindImageMemory(vkLogicalDevice, vkImage, vkImageMemory, 0u);

    return true;
}

int main()
{
    ComPtr<IDXGIAdapter1> pAdapter;
    if (!SelectDXGIAdapter(pAdapter.GetAddressOf()))
    {
        spdlog::critical("Failed to load a DXGI Adapter.");
        return 1;
    }

    D3D_FEATURE_LEVEL desiredFeatureLevel = D3D_FEATURE_LEVEL_11_1;

    UINT deviceCreationFlags = 0u;
    
#if defined(_DEBUG)
    deviceCreationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    ComPtr<ID3D11Device>        pDeviceDX;
    ComPtr<ID3D11DeviceContext> pImmediateContextDX;
    D3D_FEATURE_LEVEL           selectedFeatureLevel;
    if (!SUCCEEDED(D3D11CreateDevice (pAdapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, deviceCreationFlags, &desiredFeatureLevel, 1u, D3D11_SDK_VERSION, pDeviceDX.GetAddressOf(), &selectedFeatureLevel, pImmediateContextDX.GetAddressOf())))
    {
        spdlog::critical("Failed to create the D3D11 Device and Immediate Context.");
        return 1;
    }

    spdlog::info("Initialized D3D11.");

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

    std::vector<const char*> requiredInstanceExtensions;
#ifdef _DEBUG
    requiredInstanceExtensions.push_back("VK_LAYER_KHRONOS_validation");
#endif

    VkInstanceCreateInfo vkInstanceCreateInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    vkInstanceCreateInfo.pApplicationInfo     = &vkApplicationInfo;
    vkInstanceCreateInfo.enabledLayerCount    = (uint32_t)requiredInstanceExtensions.size();
    vkInstanceCreateInfo.ppEnabledLayerNames  = requiredInstanceExtensions.data();

    VkInstance vkInstance;
    if (vkCreateInstance(&vkInstanceCreateInfo, nullptr, &vkInstance) != VK_SUCCESS)
    {
        spdlog::critical("Failed to create the Vulkan Instance.");
        return 1;
    }

    volkLoadInstanceOnly(vkInstance);

    std::vector<const char*> requiredDeviceExtensions;
    {
        requiredDeviceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
    }

    VkPhysicalDevice vkPhysicalDevice;
    if (!SelectVulkanPhysicalDevice(vkInstance, requiredDeviceExtensions, pAdapter.Get(), vkPhysicalDevice))
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
    if (!CreateVulkanLogicalDevice(vkPhysicalDevice, requiredDeviceExtensions, vkGraphicsQueueIndex, vkLogicalDevice))
    {
        spdlog::critical("Failed to create the Vulkan Logical Device");
        return 1;
    }

    volkLoadDevice(vkLogicalDevice);
    
    spdlog::info("Initialized Vulkan.");

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
    imageDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    imageDesc.SampleDesc.Count = 1;

    ComPtr<ID3D11Texture2D> pImageDX;
    if (!SUCCEEDED(pDeviceDX->CreateTexture2D(&imageDesc, nullptr, pImageDX.GetAddressOf())))
    {
        spdlog::critical("Failed to create the D3D11 Image resource.");
        return 1;
    }

    spdlog::info("Successfully created a D3D11 Image Resource with external memory support.");
    
    // Bind the D3D11 Image To Vulkan Image (backed by the same memory on GPU).
    // ------------------------------------------------

    VkImage        vkImage;
    VkDeviceMemory vkImageMemory;
    if (!BindD3D11ImageToVulkanImage(vkLogicalDevice, pImageDX.Get(), vkImageMemory, vkImage))
    {
        spdlog::critical("Failed to bind the ID3D11 Image resource to a Vulkan Image.");
        return 1;
    }

    spdlog::info("Successfully created a Vulkan Image backed by the D3D11 Image memory allocation.");

    // Create CPU-Accessible Staging D3D11 Image Resource.
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

#if 0
    // Clear the Image Resource from D3D11.
    // ------------------------------------------------
    {
        ComPtr<ID3D11RenderTargetView> pRenderTargetViewDX;
        if (!SUCCEEDED(pDeviceDX->CreateRenderTargetView(pImageDX.Get(), nullptr, pRenderTargetViewDX.GetAddressOf())))
        {
            spdlog::critical("Failed to create the D3D11 Render Target View for Image resource.");
            return 1;
        }

        FLOAT clearColor[4] = { 1.0, 0.0, 0.0, 1.0 };
        pImmediateContextDX->ClearRenderTargetView(pRenderTargetViewDX.Get(), clearColor);
    }
#else
    // Clear the Image Resource from Vulkan
    // -----------------------------------------------
    {
        VkQueue vkGraphicsQueue;
        vkGetDeviceQueue(vkLogicalDevice, vkGraphicsQueueIndex, 0u, &vkGraphicsQueue);

        VkCommandPoolCreateInfo vkGraphicsCommandPoolCreateInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        {
            vkGraphicsCommandPoolCreateInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            vkGraphicsCommandPoolCreateInfo.queueFamilyIndex = vkGraphicsQueueIndex;
        }

        VkCommandPool vkGraphicsCommandPool;
        if (vkCreateCommandPool(vkLogicalDevice, &vkGraphicsCommandPoolCreateInfo, nullptr, &vkGraphicsCommandPool) != VK_SUCCESS)
        {
            spdlog::critical("Failed to create a Vulkan Command Pool.");
            return 1;
        }

        VkCommandBufferAllocateInfo vkGraphicsCommandAllocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        {
            vkGraphicsCommandAllocateInfo.commandBufferCount = 1u;
            vkGraphicsCommandAllocateInfo.commandPool        = vkGraphicsCommandPool;
            vkGraphicsCommandAllocateInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        }

        VkCommandBuffer vkGraphicsCommandBuffer;
        if (vkAllocateCommandBuffers(vkLogicalDevice, &vkGraphicsCommandAllocateInfo, &vkGraphicsCommandBuffer) != VK_SUCCESS)
        {
            spdlog::critical("Failed to create a Vulkan Command Buffer.");
            return 1;
        }

        VkCommandBufferBeginInfo vkGraphicsCommandBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        vkGraphicsCommandBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(vkGraphicsCommandBuffer, &vkGraphicsCommandBeginInfo);

        VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = vkImage;
        
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;
        
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        
        vkCmdPipelineBarrier(vkGraphicsCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkImageSubresourceRange vkImageClearRange;
        vkImageClearRange.layerCount     = 1u;
        vkImageClearRange.baseMipLevel   = 0u;
        vkImageClearRange.baseArrayLayer = 0u;
        vkImageClearRange.levelCount     = 1u;
        vkImageClearRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;

        VkClearColorValue clearColor = { {0.25f, 0.5f, 1.0f, 1.0f} };
        vkCmdClearColorImage(vkGraphicsCommandBuffer, vkImage, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1u, &vkImageClearRange);

        vkEndCommandBuffer(vkGraphicsCommandBuffer);

        VkSubmitInfo vkGraphicsQueueSubmitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        {
            vkGraphicsQueueSubmitInfo.commandBufferCount = 1u;
            vkGraphicsQueueSubmitInfo.pCommandBuffers    = &vkGraphicsCommandBuffer;
        }

        if (vkQueueSubmit(vkGraphicsQueue, 1u, &vkGraphicsQueueSubmitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
        {
            spdlog::critical("Failed to submit commands to the Vulkan Graphics Queue.");
            return 1;
        }
        
        // Pause execution until the graphics queue has finished work.
        vkDeviceWaitIdle(vkLogicalDevice);

        spdlog::info("Successfully cleared the Vulkan Image with color: [{},{},{},{}]", 
            clearColor.float32[0], 
            clearColor.float32[1], 
            clearColor.float32[2], 
            clearColor.float32[3]
        );

        // Destroy command primitives.
        vkDestroyCommandPool(vkLogicalDevice, vkGraphicsCommandPool, nullptr);
    }
#endif
    
    // Transfer the cleared native image to staging memory.
    pImmediateContextDX->CopyResource(pStagingImageDX.Get(), pImageDX.Get());

    // Map to CPU for copy.
    D3D11_MAPPED_SUBRESOURCE mappedStagingMemory;
    if (!SUCCEEDED(pImmediateContextDX->Map(pStagingImageDX.Get(), 0u, D3D11_MAP_READ, 0u, &mappedStagingMemory)))
    {
        spdlog::critical("Failed to map a pointer to the staging image memory.");
        return 1;
    }

    spdlog::info("Successfully copied the D3D11 Image to staging mapped memory.");
    
    // Write out the result to disk.
    stbi_write_jpg(kOutputFileName, kTestImageWidth, kTestImageHeight, 4, mappedStagingMemory.pData, 100);

    char workingDirectory[MAX_PATH];
    GetModuleFileNameA(NULL, workingDirectory, MAX_PATH);
    std::filesystem::path workingDirectoryPath(workingDirectory);

    spdlog::info("Successfully wrote image result to: {}\\{}", workingDirectoryPath.parent_path().string(), kOutputFileName);

    // Release Vulkan Primitives.
    vkDestroyImage    (vkLogicalDevice, vkImage,       nullptr);
    vkFreeMemory      (vkLogicalDevice, vkImageMemory, nullptr);
    vkDestroyDevice   (vkLogicalDevice,                nullptr);
    vkDestroyInstance (vkInstance,                     nullptr);

    return 0;
}