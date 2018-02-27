#include "d3d12_app.hpp"

#include <assert.h>
#include "profiler.hpp"

std::uint32_t D3D12App::rtv_increment_size = 0;
std::uint32_t D3D12App::dsv_increment_size = 0;
std::uint32_t D3D12App::cbv_srv_uav_increment_size = 0;
std::uint32_t D3D12App::sampler_increment_size = 0;

[[nodiscard]] IDXGIFactory5* CreateFactory(HWND window_handle, bool allow_fullscreen)
{
	IDXGIFactory5* factory = nullptr;

	HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
	if (FAILED(hr))
	{
		throw "Failed to create DXGIFactory.";
	}

	factory->MakeWindowAssociation(window_handle, allow_fullscreen ? 0 : DXGI_MWA_NO_ALT_ENTER);

	return factory;
}

[[nodiscard]] ComPtr<IDXGIAdapter1> FindCompatibleAdapter(ComPtr<IDXGIFactory5> factory, D3D_FEATURE_LEVEL feature_level)
{
	IDXGIAdapter1* adapter = nullptr;
	std::uint8_t adapter_idx = 0;

	// Find a compatible adapter.
	while (factory->EnumAdapters1(adapter_idx, &adapter) != DXGI_ERROR_NOT_FOUND)
	{
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);

		// Skip software adapters.
		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			adapter_idx++;
			continue;
		}

		// Create a device to test if the adapter supports the specified feature level.
		HRESULT hr = D3D12CreateDevice(adapter, feature_level, _uuidof(ID3D12Device), nullptr);
		if (SUCCEEDED(hr))
		{
			break;
		}

		adapter_idx++;
	}

	if (adapter == nullptr)
	{
		throw("No comaptible adapter found.");
	}

	return adapter;
}


[[nodiscard]] ComPtr<ID3D12Device> CreateDevice(ComPtr<IDXGIFactory5> factory, ComPtr<IDXGIAdapter1> adapter, D3D_FEATURE_LEVEL feature_level)
{
	assert(adapter);

	ComPtr<ID3D12Device> device;
	HRESULT hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
	if (FAILED(hr))
	{
		throw("Failed to create device.");
	}

	return device;
}

D3D12App::D3D12App() : frame_idx(0)
{
}

D3D12App::~D3D12App()
{
}

LRESULT CALLBACK D3D12App::WindowProc(HWND window_handle, UINT msg, WPARAM w_param, LPARAM l_param)
{
	switch (msg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_KEYDOWN:
		if (w_param == VK_ESCAPE)
		{
			DestroyWindow(window_handle);
		}
		return 0;
	}

	return DefWindowProc(window_handle, msg, w_param, l_param);
}

void D3D12App::SetupWindow(HINSTANCE inst, int show_cmd)
{
	WNDCLASSEX wc;
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = &D3D12App::WindowProc;
	wc.cbClsExtra = NULL;
	wc.cbWndExtra = NULL;
	wc.hInstance = inst;
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = name.c_str();
	wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

	if (!RegisterClassEx(&wc))
	{
		throw("Failed to register class with error: " + GetLastError());
	}

	DWORD window_style;
	window_style = WS_OVERLAPPEDWINDOW;

	if (!allow_resizing)
	{
		window_style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
	}

	window_handle = CreateWindowEx(NULL,
		name.c_str(), name.c_str(),
		window_style,
		CW_USEDEFAULT, CW_USEDEFAULT,
		initial_width, initial_height,
		NULL,
		NULL,
		inst,
		NULL);

	if (!window_handle)
	{
		throw("Failed to create window with error: " + GetLastError());
	}

	UpdateWindow(window_handle);
	ShowWindow(window_handle, show_cmd);
}

void D3D12App::SetupD3D12() {
	factory = CreateFactory(window_handle, allow_fullscreen);
	adapter = FindCompatibleAdapter(factory, feature_level);
	device = CreateDevice(factory, adapter, feature_level);

	// Init increment sizes
	rtv_increment_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	dsv_increment_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	cbv_srv_uav_increment_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	sampler_increment_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

	D3D12_COMMAND_QUEUE_DESC cmd_queue_desc = { D3D12_COMMAND_LIST_TYPE_DIRECT , 0, D3D12_COMMAND_QUEUE_FLAG_NONE };
	HRESULT hr = device->CreateCommandQueue(&cmd_queue_desc, IID_PPV_ARGS(&cmd_queue));
	if (FAILED(hr))
	{
		throw "Failed to create direct command queue.";
	}
}

void D3D12App::InitDebugLayer()
{
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)))) {
		debug_controller->EnableDebugLayer();
		debug_controller->SetEnableGPUBasedValidation(TRUE);
		debug_controller->SetEnableSynchronizedCommandQueueValidation(TRUE);
	}
}

void D3D12App::SetupSwapchain()
{
	IDXGISwapChain1* temp_swap_chain;

	// Describe multisampling capabilities.
	DXGI_SAMPLE_DESC sample_desc = {};
	sample_desc.Count = 1;
	sample_desc.Quality = 0;

	// Describe the swap chain
	DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
	swap_chain_desc.Width = initial_width;
	swap_chain_desc.Height = initial_height;
	swap_chain_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	swap_chain_desc.SampleDesc = sample_desc;
	swap_chain_desc.BufferCount = num_backbuffers;
	swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swap_chain_desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	swap_chain_desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	HRESULT hr = factory->CreateSwapChainForHwnd(
		cmd_queue.Get(),
		window_handle,
		&swap_chain_desc,
		NULL,
		NULL,
		&temp_swap_chain
	);
	if (FAILED(hr)) {
		throw "Failed to create swap chain.";
	}

	swap_chain = static_cast<IDXGISwapChain4*>(temp_swap_chain);
}

void D3D12App::StartLoop()
{
	MSG msg;
	ZeroMemory(&msg, sizeof(MSG));

	Init();

	while (true)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
				break;

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else {
			PROFILER_BEGIN_CPU("full_frame");
			Update();
			Render();

			swap_chain->Present(0, 0);

			frame_idx = swap_chain->GetCurrentBackBufferIndex();
			PROFILER_END_CPU("full_frame");
		}
	}
}

DirectX::XMINT2 D3D12App::GetWindowSize()
{
	if (allow_resizing)
	{
		RECT rect;
		if (!GetWindowRect(window_handle, &rect))
		{
			throw "Failed to get the window's client rectangle.";
			return DirectX::XMINT2(initial_width, initial_height);
		}

		return DirectX::XMINT2(rect.right - rect.left, rect.bottom - rect.top);
	}
	else
	{
		return DirectX::XMINT2(initial_width, initial_height);
	}
}

ComPtr<ID3D12DescriptorHeap> CreateDepthStencilHeap(ComPtr<ID3D12Device> device, std::uint16_t num_buffers)
{
	ID3D12DescriptorHeap* heap;

	D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
	heap_desc.NumDescriptors = num_buffers;
	heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	HRESULT hr = device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap));

	if (FAILED(hr))
	{
		throw "Failed to create descriptor heap for depth stencil buffers";
	}

	return heap;
}

ComPtr<ID3D12Resource> CreateDepthStencilBuffer(ComPtr<ID3D12Device> device, CD3DX12_CPU_DESCRIPTOR_HANDLE desc_handle, DirectX::XMINT2 size)
{
	return CreateDepthStencilBuffer(device, desc_handle, size.x, size.y);
}

ComPtr<ID3D12Resource> CreateDepthStencilBuffer(ComPtr<ID3D12Device> device, CD3DX12_CPU_DESCRIPTOR_HANDLE desc_handle, std::uint16_t width, std::uint16_t height)
{
	ID3D12Resource* buffer;

	D3D12_CLEAR_VALUE optimized_clear_value = {};
	optimized_clear_value.Format = DXGI_FORMAT_D32_FLOAT;
	optimized_clear_value.DepthStencil.Depth = 1.0f;
	optimized_clear_value.DepthStencil.Stencil = 0;

	HRESULT hr = device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, width, height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&optimized_clear_value,
		IID_PPV_ARGS(&buffer)
	);
	if (FAILED(hr))
	{
		throw "Failed to create commited resource.";
	}
	buffer->SetName(L"Depth/Stencil Buffer");

	D3D12_DEPTH_STENCIL_VIEW_DESC view_desc = {};
	view_desc.Format = DXGI_FORMAT_D32_FLOAT;
	view_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	view_desc.Flags = D3D12_DSV_FLAG_NONE;

	device->CreateDepthStencilView(buffer, &view_desc, desc_handle);

	return buffer;
}

ComPtr<ID3D12DescriptorHeap> CreateRenderTargetViewHeap(ComPtr<ID3D12Device> device, std::uint16_t num_buffers)
{
	ID3D12DescriptorHeap* heap;

	D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
	heap_desc.NumDescriptors = num_buffers;
	heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	HRESULT hr = device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap));

	if (FAILED(hr))
	{
		throw "Failed to create descriptor heap for render target views";
	}

	return heap;
}

std::pair<D3D12_VIEWPORT, D3D12_RECT> CreateViewportAndScissor(DirectX::XMINT2 size)
{
	D3D12_VIEWPORT viewport;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.Width = size.x;
	viewport.Height = size.y;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	D3D12_RECT rect;
	rect.left = 0;
	rect.top = 0;
	rect.right = size.x;
	rect.bottom = size.y;

	return std::make_pair(viewport, rect);
}

std::wstring GetUTF16(std::string_view const str, int codepage)
{
	if (str.empty()) return std::wstring();
	int sz = MultiByteToWideChar(codepage, 0, &str[0], (int)str.size(), 0, 0);
	std::wstring retval(sz, 0);
	MultiByteToWideChar(codepage, 0, &str[0], (int)str.size(), &retval[0], sz);
	return retval;
}

std::pair<ID3DBlob*, D3D12_SHADER_BYTECODE> LoadShader(std::string_view path, std::string_view entry, std::string_view type)
{
	ID3DBlob* shader;
	ID3DBlob* error;
	HRESULT hr = D3DCompileFromFile(GetUTF16(path, CP_UTF8).c_str(),
		nullptr,
		nullptr,
		entry.data(),
		type.data(),
		D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_VALIDATION,
		0,
		&shader,
		&error);
	if (FAILED(hr)) {
		throw((char*)error->GetBufferPointer());
	}

	D3D12_SHADER_BYTECODE bytecode = {};
	bytecode.BytecodeLength = shader->GetBufferSize();
	bytecode.pShaderBytecode = shader->GetBufferPointer();

	return std::make_pair(shader, bytecode);
}
