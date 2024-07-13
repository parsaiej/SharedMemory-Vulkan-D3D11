#include <dxgi.h>
#include <d3d11.h>
#include <locale>
#include <spdlog/spdlog.h>
#include <thread>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#ifdef USE_RENDERDOC
    #include <renderdoc_app.h>
    RENDERDOC_API_1_1_2* pRenderDocAPI = NULL;
#endif

#include <format>
#include <wrl.h>

using namespace Microsoft::WRL;

const UINT kTestImageWidth  = 1920;
const UINT kTestImageHeight = 1080;

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

    ComPtr<ID3D11Texture2D> pImageDX;
    if (!SUCCEEDED(pDeviceDX->CreateTexture2D(&imageDesc, nullptr, pImageDX.GetAddressOf())))
    {
        spdlog::critical("Failed to create the D3D11 Image resource.");
        return 1;
    }

    // Create CPU-Accessible Staging Image Resource.
    // ------------------------------------------------

    D3D11_TEXTURE2D_DESC stagingImageDesc = imageDesc;
    stagingImageDesc.Usage          = D3D11_USAGE_STAGING;
    stagingImageDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingImageDesc.BindFlags      = 0u;
    
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

    return 0;
}