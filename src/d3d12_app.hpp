#pragma once

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>
#include <DirectXMath.h>

#include "d3dx12.hpp"

using Microsoft::WRL::ComPtr;

class D3D12App
{
public:
	D3D12App();
	virtual ~D3D12App();

	static LRESULT CALLBACK WindowProc(HWND handle, UINT msg, WPARAM w_param, LPARAM l_param);

	void SetupWindow(HINSTANCE inst, int show_cmd);
	void SetupD3D12();
	void InitDebugLayer();
	void SetupSwapchain();

	void StartLoop();

	virtual void Init() = 0;
	virtual void Render() = 0;
	virtual void Update() = 0;

	DirectX::XMINT2 GetWindowSize();

	static const std::string name;
	static const bool allow_fullscreen;
	static const bool allow_resizing;
	static const std::uint16_t initial_width;
	static const std::uint16_t initial_height;
	static const std::uint8_t num_backbuffers;
	static const D3D_FEATURE_LEVEL feature_level;
	static const D3D_ROOT_SIGNATURE_VERSION root_signature_version;

	static std::uint32_t rtv_increment_size;
	static std::uint32_t dsv_increment_size;
	static std::uint32_t cbv_srv_uav_increment_size;
	static std::uint32_t sampler_increment_size;

protected:
	unsigned int frame_idx;

	ComPtr<IDXGIFactory5> factory;
	ComPtr<ID3D12Device> device;
	ComPtr<IDXGIAdapter1> adapter;
	ComPtr<IDXGISwapChain4> swap_chain;
	ComPtr<ID3D12CommandQueue> cmd_queue;
	ComPtr<ID3D12Debug1> debug_controller;

	HWND window_handle;
};

[[nodiscard]] ComPtr<ID3D12DescriptorHeap> CreateDepthStencilHeap(ComPtr<ID3D12Device> device, std::uint16_t num_buffers);
[[nodiscard]] ComPtr<ID3D12Resource> CreateDepthStencilBuffer(ComPtr<ID3D12Device> device, CD3DX12_CPU_DESCRIPTOR_HANDLE desc_handle, DirectX::XMINT2 size);
[[nodiscard]] ComPtr<ID3D12Resource> CreateDepthStencilBuffer(ComPtr<ID3D12Device> device, CD3DX12_CPU_DESCRIPTOR_HANDLE desc_handle, std::uint16_t width, std::uint16_t height);
[[nodiscard]] ComPtr<ID3D12DescriptorHeap> CreateRenderTargetViewHeap(ComPtr<ID3D12Device> device, std::uint16_t num_buffers);
[[nodiscard]] std::pair<D3D12_VIEWPORT, D3D12_RECT> CreateViewportAndScissor(DirectX::XMINT2 size);

template<const std::uint16_t NUM = D3D12App::num_backbuffers>
[[nodiscard]] std::array<ComPtr<ID3D12Resource>, NUM> GetRenderTargetsFromSwapChain(ComPtr<ID3D12Device> device, ComPtr<IDXGISwapChain4> swap_chain) {
	std::array<ComPtr<ID3D12Resource>, NUM> render_targets;

	for (std::uint16_t i = 0; i < NUM; i++)
	{
		HRESULT hr = swap_chain->GetBuffer(i, IID_PPV_ARGS(&render_targets[i]));
		if (FAILED(hr))
		{
			throw "Failed to get swap chain buffer.";
		}
	}

	return render_targets;
}

template<const std::uint16_t NUM>
void CreateRTVsFromResourceArray(ComPtr<ID3D12Device> device, std::array<ComPtr<ID3D12Resource>, NUM> render_targets, CD3DX12_CPU_DESCRIPTOR_HANDLE desc_handle)
{
	for (std::uint16_t i = 0; i < NUM; i++)
	{
		device->CreateRenderTargetView(render_targets[i].Get(), nullptr, desc_handle);
		desc_handle.Offset(1, D3D12App::rtv_increment_size);
	}
}

template<std::uint8_t NUM = D3D12App::num_backbuffers>
[[nodiscard]] std::array<ComPtr<ID3D12CommandAllocator>, NUM> CreateVersionedCommandAllocators(ComPtr<ID3D12Device> device, D3D12_COMMAND_LIST_TYPE type, std::wstring name = L"Versioned Command Allocator")
{
	std::array<ComPtr<ID3D12CommandAllocator>, NUM> cmd_allocators;

	for (int i = 0; i < NUM; i++)
	{
		HRESULT hr = device->CreateCommandAllocator(type, IID_PPV_ARGS(&cmd_allocators[i]));
		if (FAILED(hr))
		{
			throw "Failed to create command allocator";
		}

		cmd_allocators[i]->SetName(name.c_str());
	}

	return cmd_allocators;
}

template<std::uint8_t NUM = D3D12App::num_backbuffers>
[[nodiscard]] std::pair<ComPtr<ID3D12GraphicsCommandList2>, std::array<ComPtr<ID3D12CommandAllocator>, NUM>> CreateVersionedCommandListAndAllocators(ComPtr<ID3D12Device> device, D3D12_COMMAND_LIST_TYPE type, std::wstring name = L"Versioned Command List")
{
	auto versioned_allocators = CreateVersionedCommandAllocators(device, type, (name + L" Allocator").c_str());

	ComPtr<ID3D12GraphicsCommandList2> cmd_list;
	HRESULT hr = device->CreateCommandList(
		0,
		type,
		versioned_allocators[0].Get(),
		NULL,
		IID_PPV_ARGS(&cmd_list)
	);
	if (FAILED(hr))
	{
		throw "Failed to create command list";
	}
	cmd_list->SetName(name.c_str());

	return std::make_pair(cmd_list, versioned_allocators);
}

template<std::uint8_t NUM = D3D12App::num_backbuffers>
void ResetVersionedCommandListAndAllocator(ComPtr<ID3D12GraphicsCommandList2> cmd_list, std::array<ComPtr<ID3D12CommandAllocator>, NUM> cmd_allocators, std::uint8_t frame_idx, ComPtr<ID3D12PipelineState> pso = nullptr)
{
	// Reset command allocators and buffers
	HRESULT hr = cmd_allocators[frame_idx]->Reset();
	if (FAILED(hr))
	{
		throw "Failed to reset cmd allocators";
	}

	hr = cmd_list->Reset(cmd_allocators[frame_idx].Get(), pso.Get());
	if (FAILED(hr))
	{
		throw "Failed to reset command list";
	}
}

[[nodiscard]] std::wstring GetUTF16(std::string_view const str, int codepage);
[[nodiscard]] std::pair<ID3DBlob*, D3D12_SHADER_BYTECODE> LoadShader(std::string_view path, std::string_view entry, std::string_view type);