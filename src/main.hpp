#pragma once

// root signature version 1.1
// dxgi_6.h
// GraphicsCommandList2
// ID3D12Debug1

// GetVirtualAddress every frame. What is the performance impact?

#include "d3d12_app.hpp"

#include <vector>
#include <array>
#include <chrono>

//#define CB_MAP_ON_UPDATE
//#define CB_UNMAP
#define CB_MAP_ON_CREATION

#define CB_BIG_BUFFER

#define NUM_RENDER_OBJECTS 100

const std::string D3D12App::name = "Constant Buffer Performance Test";
const bool D3D12App::allow_fullscreen = false;
const bool D3D12App::allow_resizing = false;
const std::uint16_t D3D12App::initial_width = 640;
const std::uint16_t D3D12App::initial_height = 360;
const std::uint8_t D3D12App::num_backbuffers = 3;
const D3D_FEATURE_LEVEL D3D12App::feature_level = D3D_FEATURE_LEVEL_12_1;
const D3D_ROOT_SIGNATURE_VERSION D3D12App::root_signature_version = D3D_ROOT_SIGNATURE_VERSION_1_1;

struct Vertex
{
	Vertex(DirectX::XMFLOAT3 pos) : pos(pos) { }

	DirectX::XMFLOAT3 pos;
};

struct CBPerObject
{
	DirectX::XMFLOAT4 pos;
	DirectX::XMFLOAT4 color;
};

static const std::array<Vertex, 4> vertices
{
	(DirectX::XMFLOAT3(0.5f, -0.5f, 0.5f)),
	(DirectX::XMFLOAT3(0.5f, 0.5f, 0.5f)),
	(DirectX::XMFLOAT3(-0.5f, -0.5f, 0.5f)),
	(DirectX::XMFLOAT3(-0.5f, 0.5f, 0.5f)),
};

struct ConstantBuffer
{
	std::array<D3D12_GPU_VIRTUAL_ADDRESS, D3D12App::num_backbuffers> gpu_addresses;

#ifdef CB_BIG_BUFFER
	size_t offset;
#else // CB_BIG_BUFFER
	std::array<ComPtr<ID3D12Resource>, D3D12App::num_backbuffers> buffers;
#ifdef CB_MAP_ON_CREATION
	std::array<void*, D3D12App::num_backbuffers> addresses;
#endif // CB_MAP_ON_CREATION
#endif // CB_BIG_BUFFER
};

struct RenderObject
{
	DirectX::XMFLOAT4 pos;
	DirectX::XMFLOAT4 color;

	D3D12_VERTEX_BUFFER_VIEW vb_view;
	D3D12_INDEX_BUFFER_VIEW ib_view;
	ConstantBuffer* const_buffer;
};

class BufferPerfApp : public D3D12App
{
public:
	BufferPerfApp();
	~BufferPerfApp();

	void Init() override;
	void Update() override;
	void Render() override;

private:
	void CreateCommandList();
	void CreateFences();
	void CreateRootSignature();
	void CreatePipelineStateObject();
	void CreateVertexBuffer();
	void WaitForPrevFrame();
	void UpdateFramerate();

#ifdef CB_BIG_BUFFER
	void CreateBigConstantBuffer(std::uint32_t size);
#endif
	void CreateConstantBuffer(ConstantBuffer** cb, std::uint32_t size);

	void PerfOutput_Framerate();

	ComPtr<ID3D12Resource> depth_stencil_buffer;
	ComPtr<ID3D12DescriptorHeap> depth_stencil_view_heap;

	std::array<ComPtr<ID3D12Resource>, num_backbuffers> render_targets;
	ComPtr<ID3D12DescriptorHeap> render_target_view_heap;

	std::array<ComPtr<ID3D12CommandAllocator>, num_backbuffers> cmd_allocators;
	ComPtr<ID3D12GraphicsCommandList2> cmd_list;

	std::array<ComPtr<ID3D12Fence>, num_backbuffers> fences;
	std::array<UINT64, num_backbuffers> fence_values;
	HANDLE fence_event;

	ComPtr<ID3D12RootSignature> root_signature;

	ComPtr<ID3D12Resource> vertex_buffer;
	ComPtr<ID3D12Resource> vb_upload_heap;
	int vertex_buffer_size;
	D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view;

	ComPtr<ID3D12Resource> index_buffer;
	ComPtr<ID3D12Resource> ib_upload_heap;
	int index_buffer_size;
	D3D12_INDEX_BUFFER_VIEW index_buffer_view;

	ComPtr<ID3D12PipelineState> pipeline;
	std::vector<D3D12_INPUT_ELEMENT_DESC> input_layout;

	D3D12_VIEWPORT viewport;
	D3D12_RECT scissor_rect;

#ifdef CB_BIG_BUFFER
	std::array<ComPtr<ID3D12Resource>, D3D12App::num_backbuffers> big_cb_buffers;
#ifdef CB_MAP_ON_CREATION
	std::array<void*, D3D12App::num_backbuffers> big_cb_addresses;
#endif // CB_MAP_ON_CREATION
	size_t current_offset = 0;
#endif

	const float clear_color[4];
	std::array<RenderObject, NUM_RENDER_OBJECTS> draw_list;

	// profiling
	std::uint32_t frames;
	std::uint32_t framerate;
	std::chrono::time_point<std::chrono::high_resolution_clock> prev;

	std::vector<std::uint32_t> captured_framerates;
};