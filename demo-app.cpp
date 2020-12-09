#include "demo-app.h"
#include "gpu-mem.h"

DemoApp::DemoApp(HWND hWnd, UINT Width, UINT Height)
{
	CreateDevice();
	CreateQueues();
	CreateFence();

	CreateSwapchain(hWnd, Width, Height);
	CreateRenderTargets();

	//CreateRootSignature();
	//CreatePipeline();
}

void DemoApp::CreateDevice()
{
    CreateDXGIFactory1(IID_PPV_ARGS(&Factory));

    ComPtr<IDXGIAdapter1> Adapter;
    bool bAdapterFound = false;

    for (UINT AdapterIndex = 0;
        !bAdapterFound && Factory->EnumAdapters1(AdapterIndex, &Adapter) != DXGI_ERROR_NOT_FOUND;
        ++AdapterIndex)
    {
        DXGI_ADAPTER_DESC1 AdapterDesc;
        Adapter->GetDesc1(&AdapterDesc);

        if (AdapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            continue;
        }

        HRESULT hr;
        hr = D3D12CreateDevice(Adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr);
        if (SUCCEEDED(hr))
        {
            bAdapterFound = true;
        }
    }

    D3D12CreateDevice(Adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device));
}

void DemoApp::CreateQueues()
{
    // CommandQueue

    D3D12_COMMAND_QUEUE_DESC CommandQueueDesc{};

    CommandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    CommandQueueDesc.NodeMask = 0;
    CommandQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    CommandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    Device->CreateCommandQueue(&CommandQueueDesc, IID_PPV_ARGS(&CommandQueue));

    // Command Allocator
    Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&CommandAllocator));

    // Command List
    ID3D12PipelineState* InitialState = nullptr;
    Device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        CommandAllocator.Get(),
        InitialState,
        IID_PPV_ARGS(&CommandList)
    );
    CommandList->Close();
}

void DemoApp::CreateFence()
{
    FenceValue = 0;

    Device->CreateFence(FenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence));

    FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

void DemoApp::FlushAndWait()
{
    const UINT64 FenceValueToSignal = FenceValue;
    CommandQueue->Signal(Fence.Get(), FenceValueToSignal);

    ++FenceValue;

    if (Fence->GetCompletedValue() < FenceValueToSignal)
    {
        Fence->SetEventOnCompletion(FenceValueToSignal, FenceEvent);
        WaitForSingleObject(FenceEvent, INFINITE);
    }
}

void DemoApp::CreateRenderTargets()
{
    /* RenderTargetHeap */
    D3D12_DESCRIPTOR_HEAP_DESC DescriptorHeapDesc{};
    DescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    DescriptorHeapDesc.NodeMask = 0;
    DescriptorHeapDesc.NumDescriptors = kFrameCount;
    DescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

    Device->CreateDescriptorHeap(&DescriptorHeapDesc, IID_PPV_ARGS(&RenderTargetViewHeap));

    for (UINT FrameIndex = 0; FrameIndex < kFrameCount; ++FrameIndex)
    {
        Swapchain->GetBuffer(FrameIndex, IID_PPV_ARGS(&RenderTargets[FrameIndex]));

        D3D12_RENDER_TARGET_VIEW_DESC RTDesc{};
        RTDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        RTDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        RTDesc.Texture2D.MipSlice = 0;
        RTDesc.Texture2D.PlaneSlice = 0;

        D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor = RenderTargetViewHeap->GetCPUDescriptorHandleForHeapStart();
        DestDescriptor.ptr += ((SIZE_T)FrameIndex) * Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        Device->CreateRenderTargetView(RenderTargets[FrameIndex].Get(), &RTDesc, DestDescriptor);
    }
}

void DemoApp::CreateSwapchain(HWND hWnd, UINT Width, UINT Height)
{
    /* Swapchain */
    DXGI_SWAP_CHAIN_DESC1 SwapchainDesc{};

    SwapchainDesc.BufferCount = 2;
    SwapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;

    SwapchainDesc.Width = Width;
    SwapchainDesc.Height = Height;
    SwapchainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

    SwapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    SwapchainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> SwapchainTemp;
    Factory->CreateSwapChainForHwnd(
        CommandQueue.Get(), // swap chain forces flush when does flip
        hWnd,
        &SwapchainDesc,
        nullptr,
        nullptr,
        &SwapchainTemp
    );

    SwapchainTemp.As(&Swapchain);
}

void DemoApp::RecordCommandList()
{
    const UINT BackFrameIndex = Swapchain->GetCurrentBackBufferIndex();

    CommandAllocator->Reset();
    CommandList->Reset(CommandAllocator.Get(), nullptr); // para borrar la pantalla no necesitamos un pipeline

    D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetDescriptor = RenderTargetViewHeap->GetCPUDescriptorHandleForHeapStart();
    RenderTargetDescriptor.ptr += ((SIZE_T)BackFrameIndex) * Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    GPUMem::ResourceBarrier(CommandList.Get(), RenderTargets[BackFrameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

    const FLOAT ClearValue[] = { 1.0f, 0.0f, 0.0f, 1.0f };
    CommandList->ClearRenderTargetView(RenderTargetDescriptor, ClearValue, 0, nullptr);

    GPUMem::ResourceBarrier(CommandList.Get(), RenderTargets[BackFrameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    CommandList->Close();
}

void DemoApp::Tick()
{
    RecordCommandList();
    ID3D12CommandList* ppCommandLists[] = { CommandList.Get() };
    CommandQueue->ExecuteCommandLists(1, ppCommandLists);
    Swapchain->Present(1, 0);
    FlushAndWait();
}