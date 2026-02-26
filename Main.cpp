//
// Main.cpp
//

#include "pch.h"

#include <d3dcompiler.h>

#ifdef __clang__
#pragma clang diagnostic ignored "-Wcovered-switch-default"
#pragma clang diagnostic ignored "-Wswitch-enum"
#endif

#pragma warning(disable : 4061)

#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

extern "C"
{
    // Used to enable the "Agility SDK" components.
    __declspec(dllexport) extern const UINT D3D12SDKVersion = D3D12_SDK_VERSION;
    __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\";
}

namespace
{
    constexpr UINT g_frameCount = 2;

    HWND g_hwnd = nullptr;
    UINT g_width = 1280;
    UINT g_height = 720;

    ComPtr<ID3D12Device> g_device;
    ComPtr<ID3D12CommandQueue> g_commandQueue;
    ComPtr<IDXGISwapChain3> g_swapChain;
    ComPtr<ID3D12DescriptorHeap> g_rtvHeap;
    ComPtr<ID3D12Resource> g_renderTargets[g_frameCount];
    ComPtr<ID3D12CommandAllocator> g_commandAllocators[g_frameCount];
    ComPtr<ID3D12GraphicsCommandList> g_commandList;
    ComPtr<ID3D12Fence> g_fence;
    ComPtr<ID3D12RootSignature> g_rootSignature;
    ComPtr<ID3D12PipelineState> g_pipelineState;

    UINT g_rtvDescriptorSize = 0;
    UINT g_frameIndex = 0;
    UINT64 g_fenceValues[g_frameCount] = {};
    HANDLE g_fenceEvent = nullptr;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);

    if (message == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

void WaitForGpu()
{
    const UINT64 fenceToWaitFor = g_fenceValues[g_frameIndex];
    DX::ThrowIfFailed(g_commandQueue->Signal(g_fence.Get(), fenceToWaitFor));
    DX::ThrowIfFailed(g_fence->SetEventOnCompletion(fenceToWaitFor, g_fenceEvent));
    WaitForSingleObject(g_fenceEvent, INFINITE);
    ++g_fenceValues[g_frameIndex];
}

void MoveToNextFrame()
{
    const UINT64 currentFenceValue = g_fenceValues[g_frameIndex];
    DX::ThrowIfFailed(g_commandQueue->Signal(g_fence.Get(), currentFenceValue));

    g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();

    if (g_fence->GetCompletedValue() < g_fenceValues[g_frameIndex])
    {
        DX::ThrowIfFailed(g_fence->SetEventOnCompletion(g_fenceValues[g_frameIndex], g_fenceEvent));
        WaitForSingleObject(g_fenceEvent, INFINITE);
    }

    g_fenceValues[g_frameIndex] = currentFenceValue + 1;
}

void InitWindow(HINSTANCE hInstance, int nCmdShow)
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"HlslTriangleClass";

    if (!RegisterClassExW(&wc))
    {
        throw std::runtime_error("RegisterClassExW failed.");
    }

    RECT rc = {0, 0, static_cast<LONG>(g_width), static_cast<LONG>(g_height)};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    g_hwnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"ShaderEditor Triangle",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rc.right - rc.left,
        rc.bottom - rc.top,
        nullptr,
        nullptr,
        hInstance,
        nullptr);

    if (!g_hwnd)
    {
        throw std::runtime_error("CreateWindowExW failed.");
    }

    ShowWindow(g_hwnd, nCmdShow);
}

void CreatePipeline()
{
    constexpr char vertexShaderSrc[] =
        "struct VSOut {"
        "    float4 pos   : SV_Position;"
        "    float3 color : COLOR0;"
        "};"
        "VSOut main(uint id : SV_VertexID) {"
        "    float2 p[3] = {"
        "        float2( 0.0,  0.6),"
        "        float2( 0.6, -0.6),"
        "        float2(-0.6, -0.6)"
        "    };"
        "    float3 c[3] = {"
        "        float3(1,0,0),"
        "        float3(0,1,0),"
        "        float3(0,0,1)"
        "    };"
        "    VSOut o;"
        "    o.pos = float4(p[id], 0.0, 1.0);"
        "    o.color = c[id];"
        "    return o;"
        "}";

    constexpr char pixelShaderSrc[] =
        "struct VSOut {"
        "    float4 pos   : SV_Position;"
        "    float3 color : COLOR0;"
        "};"
        "float4 main(VSOut input) : SV_Target {"
        "    return float4(input.color, 1.0);"
        "}";

    ComPtr<ID3DBlob> vertexShaderBlob;
    ComPtr<ID3DBlob> pixelShaderBlob;
    ComPtr<ID3DBlob> errorBlob;

    UINT compileFlags = 0;
#if defined(_DEBUG)
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    HRESULT hr = D3DCompile(
        vertexShaderSrc,
        sizeof(vertexShaderSrc) - 1,
        nullptr,
        nullptr,
        nullptr,
        "main",
        "vs_5_0",
        compileFlags,
        0,
        &vertexShaderBlob,
        &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob)
        {
            OutputDebugStringA(static_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        DX::ThrowIfFailed(hr);
    }

    errorBlob.Reset();

    hr = D3DCompile(
        pixelShaderSrc,
        sizeof(pixelShaderSrc) - 1,
        nullptr,
        nullptr,
        nullptr,
        "main",
        "ps_5_0",
        compileFlags,
        0,
        &pixelShaderBlob,
        &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob)
        {
            OutputDebugStringA(static_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        DX::ThrowIfFailed(hr);
    }

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> rootSignatureBlob;
    ComPtr<ID3DBlob> rootSignatureErrorBlob;
    DX::ThrowIfFailed(D3D12SerializeRootSignature(
        &rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &rootSignatureBlob,
        &rootSignatureErrorBlob));

    DX::ThrowIfFailed(g_device->CreateRootSignature(
        0,
        rootSignatureBlob->GetBufferPointer(),
        rootSignatureBlob->GetBufferSize(),
        IID_PPV_ARGS(&g_rootSignature)));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = {nullptr, 0};
    psoDesc.pRootSignature = g_rootSignature.Get();
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShaderBlob.Get());
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShaderBlob.Get());
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;

    DX::ThrowIfFailed(g_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&g_pipelineState)));
}

void InitD3D()
{
    UINT factoryFlags = 0;
#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();
        factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
#endif

    ComPtr<IDXGIFactory4> factory;
    DX::ThrowIfFailed(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory)));

    DX::ThrowIfFailed(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_device)));

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    DX::ThrowIfFailed(g_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&g_commandQueue)));

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = g_frameCount;
    swapChainDesc.Width = g_width;
    swapChainDesc.Height = g_height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    DX::ThrowIfFailed(factory->CreateSwapChainForHwnd(
        g_commandQueue.Get(),
        g_hwnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain));
    DX::ThrowIfFailed(factory->MakeWindowAssociation(g_hwnd, DXGI_MWA_NO_ALT_ENTER));
    DX::ThrowIfFailed(swapChain.As(&g_swapChain));
    g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = g_frameCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    DX::ThrowIfFailed(g_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&g_rtvHeap)));
    g_rtvDescriptorSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < g_frameCount; ++i)
    {
        DX::ThrowIfFailed(g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_renderTargets[i])));
        g_device->CreateRenderTargetView(g_renderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += g_rtvDescriptorSize;

        DX::ThrowIfFailed(g_device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&g_commandAllocators[i])));
    }

    CreatePipeline();

    DX::ThrowIfFailed(g_device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        g_commandAllocators[g_frameIndex].Get(),
        g_pipelineState.Get(),
        IID_PPV_ARGS(&g_commandList)));
    DX::ThrowIfFailed(g_commandList->Close());

    DX::ThrowIfFailed(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)));
    g_fenceValues[g_frameIndex] = 1;

    g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!g_fenceEvent)
    {
        DX::ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }
}

void PopulateCommandList()
{
    DX::ThrowIfFailed(g_commandAllocators[g_frameIndex]->Reset());
    DX::ThrowIfFailed(g_commandList->Reset(g_commandAllocators[g_frameIndex].Get(), g_pipelineState.Get()));

    D3D12_VIEWPORT viewport = {0.0f, 0.0f, static_cast<float>(g_width), static_cast<float>(g_height), 0.0f, 1.0f};
    D3D12_RECT scissorRect = {0, 0, static_cast<LONG>(g_width), static_cast<LONG>(g_height)};

    g_commandList->SetGraphicsRootSignature(g_rootSignature.Get());
    g_commandList->RSSetViewports(1, &viewport);
    g_commandList->RSSetScissorRects(1, &scissorRect);

    D3D12_RESOURCE_BARRIER toRenderTarget = {};
    toRenderTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRenderTarget.Transition.pResource = g_renderTargets[g_frameIndex].Get();
    toRenderTarget.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toRenderTarget.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    toRenderTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    g_commandList->ResourceBarrier(1, &toRenderTarget);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += static_cast<SIZE_T>(g_frameIndex) * g_rtvDescriptorSize;

    constexpr float clearColor[] = {0.08f, 0.10f, 0.14f, 1.0f};
    g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
    g_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_commandList->DrawInstanced(3, 1, 0, 0);

    D3D12_RESOURCE_BARRIER toPresent = toRenderTarget;
    toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    g_commandList->ResourceBarrier(1, &toPresent);

    DX::ThrowIfFailed(g_commandList->Close());
}

void Render()
{
    PopulateCommandList();

    ID3D12CommandList* commandLists[] = {g_commandList.Get()};
    g_commandQueue->ExecuteCommandLists(1, commandLists);

    DX::ThrowIfFailed(g_swapChain->Present(1, 0));
    MoveToNextFrame();
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ PWSTR pCmdLine, _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(pCmdLine);

    if (!DirectX::XMVerifyCPUSupport())
    {
        return 1;
    }

#ifdef __MINGW32__
    if (FAILED(CoInitializeEx(nullptr, COINITBASE_MULTITHREADED)))
    {
        return 1;
    }
#else
    if (FAILED(RoInitialize(RO_INIT_MULTITHREADED)))
    {
        return 1;
    }
#endif

    int result = 0;

    try
    {
        InitWindow(hInstance, nCmdShow);
        InitD3D();

        MSG msg = {};
        while (msg.message != WM_QUIT)
        {
            if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            else
            {
                Render();
            }
        }

        WaitForGpu();
        result = static_cast<int>(msg.wParam);
    }
    catch (const std::exception&)
    {
        MessageBoxA(g_hwnd, "Failed to initialize or run the D3D12 triangle sample.", "Error", MB_OK | MB_ICONERROR);
        result = 1;
    }

    if (g_fenceEvent)
    {
        CloseHandle(g_fenceEvent);
        g_fenceEvent = nullptr;
    }

#ifdef __MINGW32__
    CoUninitialize();
#else
    RoUninitialize();
#endif

    return result;
}
