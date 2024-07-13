
// D3D11 Includes 
// -------------------------

#include <dxgi.h>
#include <d3d11.h>

// Vulkan Includes
// -------------------------

#include <vulkan/vulkan.h>

// Windows Includes
// -------------------------

#include <wrl.h>

using namespace Microsoft::WRL;

bool SelectDXGIAdapter(IDXGIAdapter** ppAdapter)
{
    // Create a DXGI factory
    ComPtr<IDXGIFactory> pFactory;
    if (!SUCCEEDED(CreateDXGIFactory(IID_PPV_ARGS(&pFactory))))
        return false;

    for (UINT i = 0; pFactory->EnumAdapters(i, ppAdapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC adapterDesc;
        (*ppAdapter)->GetDesc(&adapterDesc);


    }

    return true;
}

int main()
{
    ComPtr<IDXGIAdapter> pAdapter;
    if (!SelectDXGIAdapter(pAdapter.GetAddressOf()))
        return 1;

    

    return 0;
}