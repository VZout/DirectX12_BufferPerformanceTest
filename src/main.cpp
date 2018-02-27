#include "main.hpp"

#include "profiler.hpp"

BufferPerfApp::BufferPerfApp()
	: clear_color{ 0.568f, 0.733f, 1.0f, 1.0f },
	frames(0),
	framerate(0)
{
	auto viewport_and_scissor = CreateViewportAndScissor( { initial_width, initial_height } );
	viewport = viewport_and_scissor.first;
	scissor_rect = viewport_and_scissor.second;
}

BufferPerfApp::~BufferPerfApp()
{
	profiler::PrintResult("drawing");
	profiler::PrintResult("update");
	profiler::PrintResult("full_frame");
	PerfOutput_Framerate();

	// Wait for all command lists to be finished
	for (auto i = 0; i < fences.size(); i++) {
		if (fences[i]->GetCompletedValue() < fence_values[i])
		{
			HRESULT hr = fences[i]->SetEventOnCompletion(fence_values[i], fence_event);
			WaitForSingleObject(fence_event, INFINITE);
		}

		fence_values[i]++;
	}

}

void BufferPerfApp::Init()
{
	{
		//device->SetStablePowerState(TRUE);
	}

	render_targets = GetRenderTargetsFromSwapChain<num_backbuffers>(device, swap_chain);
	render_target_view_heap = CreateRenderTargetViewHeap(device, num_backbuffers);
	CreateRTVsFromResourceArray(device, render_targets, render_target_view_heap->GetCPUDescriptorHandleForHeapStart());

	depth_stencil_view_heap = CreateDepthStencilHeap(device, 1);
	depth_stencil_buffer = CreateDepthStencilBuffer(device, depth_stencil_view_heap->GetCPUDescriptorHandleForHeapStart(), GetWindowSize());

	CreateCommandList();
	CreateFences();
	CreateRootSignature();
	CreatePipelineStateObject();

#ifdef CB_BIG_BUFFER
	unsigned int mul_size = (sizeof(CBPerObject) + 255) & ~255;
	CreateBigConstantBuffer(mul_size * NUM_RENDER_OBJECTS);
#endif

	// Start recording
	CreateVertexBuffer();

	// Now we execute the command list to upload the initial assets (triangle data)
	cmd_list->Close();
	std::array<ID3D12CommandList*, 1> cmd_lists = { cmd_list.Get() };
	cmd_queue->ExecuteCommandLists(cmd_lists.size(), cmd_lists.data());

	// increment the fence value now, otherwise the buffer might not be uploaded by the time we start drawing
	fence_values[frame_idx]++;
	HRESULT hr = cmd_queue->Signal(fences[frame_idx].Get(), fence_values[frame_idx]);
	if (FAILED(hr)) {
		throw "Failed to signal command queue.";
	}

	// Initialize the scene
	for (auto i = 0; i < NUM_RENDER_OBJECTS; i++)
	{
		draw_list[i].vb_view = vertex_buffer_view;
		draw_list[i].ib_view = index_buffer_view;
		CreateConstantBuffer(&draw_list[i].const_buffer, sizeof(CBPerObject));
		draw_list[i].pos = { 0, 0, 0, 1 };
		draw_list[i].color = { 1, 0, 0, 1};
	}

	// Start counting the framerate.
	prev = std::chrono::high_resolution_clock::now();
}

void BufferPerfApp::Update()
{
	PROFILER_BEGIN_CPU("update")
	for (auto& obj : draw_list)
	{
		/* COLLECT DATA */
		CBPerObject data;
		data.pos = obj.pos;
		data.color = obj.color;

		/* UPDATE CONSTANT BUFFERS */
#ifdef CB_MAP_ON_UPDATE
		void* adress;
		CD3DX12_RANGE readRange(0, 0);
#ifdef CB_BIG_BUFFER
		big_cb_buffers[frame_idx]->Map(0, &readRange, &adress);
#else // CB_BIG_BUFFER
		obj.const_buffer->buffers[frame_idx]->Map(0, &readRange, &adress);
#endif // CB_BIG_BUFFER
#endif

		unsigned int mul_size = (sizeof(CBPerObject) + 255) & ~255;
#ifdef CB_MAP_ON_UPDATE
#ifdef CB_BIG_BUFFER
		std::memcpy((UINT8*)adress + obj.const_buffer->offset, &data, mul_size);
#else
		std::memcpy(adress, &data, mul_size);
#endif
#endif // CB_MAP_ON_UPDATE
#ifdef CB_MAP_ON_CREATION
#ifdef CB_BIG_BUFFER
		std::memcpy((UINT8*)big_cb_addresses[frame_idx] + obj.const_buffer->offset, &data, mul_size);
#else // CB_BIG_BUFFER
		std::memcpy(obj.const_buffer->addresses[frame_idx], &data, mul_size);
#endif // CB_BIG_BUFFER
#endif // CB_MAP_ON_CREATION

#if defined CB_MAP_ON_UPDATE && defined CB_UNMAP
#ifdef CB_BIG_BUFFER
		big_cb_buffers[frame_idx]->Unmap(0, &readRange);
#else // CB_BIG_BUFFER
		obj.const_buffer->buffers[frame_idx]->Unmap(0, &readRange);
#endif // CB_BIG_BUFFER
#endif // CB_MAP_ON_UPDATE && CB_UNMAP
	}
	PROFILER_END_CPU("update")
}

void BufferPerfApp::Render()
{
	WaitForPrevFrame();

	UpdateFramerate();

	ResetVersionedCommandListAndAllocator(cmd_list, cmd_allocators, frame_idx, pipeline);

	// ### BEGIN RECORDING ###
	auto begin_transition = CD3DX12_RESOURCE_BARRIER::Transition(
		render_targets[frame_idx].Get(),
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET);
	cmd_list->ResourceBarrier(1, &begin_transition);

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(render_target_view_heap->GetCPUDescriptorHandleForHeapStart(), frame_idx, rtv_increment_size);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsv_handle(depth_stencil_view_heap->GetCPUDescriptorHandleForHeapStart());
	cmd_list->OMSetRenderTargets(1, &rtv_handle, false, &dsv_handle);
	cmd_list->ClearRenderTargetView(rtv_handle, clear_color, 0, nullptr);
	cmd_list->ClearDepthStencilView(dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	cmd_list->SetPipelineState(pipeline.Get());
	cmd_list->SetGraphicsRootSignature(root_signature.Get());

	cmd_list->RSSetViewports(1, &viewport);
	cmd_list->RSSetScissorRects(1, &scissor_rect);

	cmd_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	cmd_list->IASetVertexBuffers(0, 1, &vertex_buffer_view);

	PROFILER_BEGIN_CPU("drawing");
	for (auto& obj : draw_list)
	{
		cmd_list->SetGraphicsRootConstantBufferView(0, obj.const_buffer->gpu_addresses[frame_idx]);
		//cmd_list->DrawIndexedInstanced(indices.size(), 1, 0, 0, 0);
		cmd_list->DrawInstanced(vertices.size(), 1, 0, 0);
	}
	PROFILER_END_CPU("drawing");

	auto end_transition = CD3DX12_RESOURCE_BARRIER::Transition(
		render_targets[frame_idx].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT
	);
	cmd_list->ResourceBarrier(1, &end_transition);
	cmd_list->Close();
	// ### STOPPED RECORDING ###

	std::array<ID3D12CommandList*, 1> cmd_lists = { cmd_list.Get() };
	cmd_queue->ExecuteCommandLists(cmd_lists.size(), cmd_lists.data());

	// GPU Signal
	HRESULT hr = cmd_queue->Signal(fences[frame_idx].Get(), fence_values[frame_idx]);
	if (FAILED(hr)) {
		throw "Failed to set fence signal.";
	}
}

void BufferPerfApp::CreateCommandList()
{
	auto cmd_list_and_allocators = CreateVersionedCommandListAndAllocators(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	cmd_list = cmd_list_and_allocators.first;
	cmd_allocators = cmd_list_and_allocators.second;
}

void BufferPerfApp::CreateFences()
{
	HRESULT hr;

	// create the fences
	for (int i = 0; i < num_backbuffers; i++)
	{
		hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fences[i]));
		if (FAILED(hr))
		{
			throw "Failed to create fence.";
		}
		fence_values[i] = 0; // set the initial fence value to 0
	}

	// create a handle to a fence event
	fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (fence_event == nullptr)
	{
		throw "Failed to create fence event.";
	}
}

void BufferPerfApp::CreateRootSignature()
{
	std::array<D3D12_STATIC_SAMPLER_DESC, 0> samplers;
	std::array<CD3DX12_ROOT_PARAMETER, 1> parameters_1_0;
	parameters_1_0[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
	std::array<CD3DX12_ROOT_PARAMETER1, 1> parameters_1_1;
	parameters_1_1[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_PIXEL);

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
	// Root signature version 1.0
	root_signature_desc.Init_1_0(
		parameters_1_0.size(),
		parameters_1_0.data(),
		samplers.size(),
		samplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	// Root signature version 1.1
	root_signature_desc.Init_1_1(
		parameters_1_1.size(),
		parameters_1_1.data(),
		samplers.size(),
		samplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ID3DBlob* signature;
	ID3DBlob* error = nullptr;
	HRESULT hr = D3D12SerializeVersionedRootSignature(&root_signature_desc, &signature, &error); //TODO: FIX error parameter
	if (FAILED(hr))
	{
		throw "Failed to create a serialized root signature";
	}

	hr = device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&root_signature));
	if (FAILED(hr))
	{
		throw "Failed to create root signature";
	}
	root_signature->SetName(L"Generic Root Signature");
}

void BufferPerfApp::CreatePipelineStateObject()
{
	D3D12_BLEND_DESC blend_desc = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	D3D12_DEPTH_STENCIL_DESC depth_stencil_state = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	D3D12_RASTERIZER_DESC rasterize_desc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	rasterize_desc.CullMode = D3D12_CULL_MODE_NONE;
	DXGI_SAMPLE_DESC sampleDesc = { 1, 0 };

	input_layout = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	D3D12_INPUT_LAYOUT_DESC input_layout_desc = {};
	input_layout_desc.NumElements = input_layout.size();
	input_layout_desc.pInputElementDescs = input_layout.data();

	auto vertex_shader = LoadShader("cb_vertex.hlsl", "main", "vs_5_0");
	auto pixel_shader = LoadShader("cb_pixel.hlsl", "main", "ps_5_0");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
	pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pso_desc.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
	pso_desc.SampleDesc = sampleDesc;
	pso_desc.SampleMask = 0xffffffff;
	pso_desc.RasterizerState = rasterize_desc;
	pso_desc.BlendState = blend_desc;
	pso_desc.NumRenderTargets = 1;
	pso_desc.pRootSignature = root_signature.Get();
	pso_desc.VS = vertex_shader.second;
	pso_desc.PS = pixel_shader.second;
	pso_desc.InputLayout = input_layout_desc;

	HRESULT hr = device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline));
	if (FAILED(hr))
	{
		throw "Failed to create graphics pipeline";
	}
	pipeline->SetName(L"Generic pipeline object");
}

void BufferPerfApp::CreateVertexBuffer()
{
	vertex_buffer_size = sizeof(vertices);

	device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(vertex_buffer_size),
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&vertex_buffer));

	vertex_buffer->SetName(L"Vertex Buffer Resource Heap");

	device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(vertex_buffer_size),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&vb_upload_heap));

	vb_upload_heap->SetName(L"Vertex Buffer Upload Resource Heap");

	// store vertex buffer in upload heap
	D3D12_SUBRESOURCE_DATA vertex_data = {};
	vertex_data.pData = vertices.data();
	vertex_data.RowPitch = vertex_buffer_size;
	vertex_data.SlicePitch = vertex_buffer_size;

	UpdateSubresources(cmd_list.Get(), vertex_buffer.Get(), vb_upload_heap.Get(), 0, 0, 1, &vertex_data);

	// transition the vertex buffer data from copy destination state to vertex buffer state
	cmd_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(vertex_buffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));

	// create a vertex buffer view for the rectangle. We get the GPU memory address to the vertex buffer using the GetGPUVirtualAddress() method
	vertex_buffer_view.BufferLocation = vertex_buffer->GetGPUVirtualAddress();
	vertex_buffer_view.StrideInBytes = sizeof(Vertex);
	vertex_buffer_view.SizeInBytes = vertex_buffer_size;
}

void BufferPerfApp::WaitForPrevFrame()
{
	if (fences[frame_idx]->GetCompletedValue() < fence_values[frame_idx])
	{
		HRESULT hr = fences[frame_idx]->SetEventOnCompletion(fence_values[frame_idx], fence_event);
		if (FAILED(hr))
		{
			throw "Failed to set fence event.";
		}

		WaitForSingleObject(fence_event, INFINITE);
	}

	fence_values[frame_idx]++;
}

void BufferPerfApp::UpdateFramerate()
{
	frames++;
	auto now = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double> diff = now - prev;

	if (diff.count() >= 1) {
		prev = std::chrono::high_resolution_clock::now();
		framerate = frames;
		captured_framerates.push_back(framerate);
		frames = 0;
	}
}

#ifdef CB_BIG_BUFFER
void BufferPerfApp::CreateBigConstantBuffer(std::uint32_t size)
{
	for (unsigned int i = 0; i < D3D12App::num_backbuffers; ++i) {
		HRESULT hr = device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(size),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&big_cb_buffers[i]));
		if (FAILED(hr)) {
			throw "Failed to create constant buffer resource";
		}
		big_cb_buffers[i]->SetName(L"Constant Buffer Upload Resource Heap");

#ifdef CB_MAP_ON_CREATION
		CD3DX12_RANGE readRange(0, 0);
		hr = big_cb_buffers[i]->Map(0, &readRange, &big_cb_addresses[i]);
		if (FAILED(hr)) {
			throw "Failed to map constant buffer";
		}
#endif
	}
}
#endif

void BufferPerfApp::CreateConstantBuffer(ConstantBuffer** cb, std::uint32_t size)
{
	ConstantBuffer* new_cb = new ConstantBuffer();
	unsigned int mul_size = (size + 255) & ~255;

#ifdef CB_BIG_BUFFER
	for (unsigned int i = 0; i < D3D12App::num_backbuffers; ++i) {
		new_cb->gpu_addresses[i] = big_cb_buffers[i]->GetGPUVirtualAddress() + current_offset;
	}
	new_cb->offset = current_offset;
	current_offset += mul_size;
#else // CB_BIG_BUFFER
	for (unsigned int i = 0; i < D3D12App::num_backbuffers; ++i) {
		HRESULT hr = device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(mul_size),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&new_cb->buffers[i]));
		if (FAILED(hr)) {
			throw "Failed to create constant buffer resource";
		}
		new_cb->buffers[i]->SetName(L"Constant Buffer Upload Resource Heap");

#ifdef CB_MAP_ON_CREATION
		CD3DX12_RANGE readRange(0, 0);
		hr = new_cb->buffers[i]->Map(0, &readRange, &new_cb->addresses[i]);
		if (FAILED(hr)) {
			throw "Failed to map constant buffer";
		}
#endif // CB_MAP_ON_CREATION
		new_cb->gpu_addresses[i] = new_cb->buffers[i]->GetGPUVirtualAddress();
}
#endif // CB_BIG_BUFFER

	(*cb) = new_cb;
}

void BufferPerfApp::PerfOutput_Framerate()
{
	std::ofstream file;
	file.open("perf_framerate.txt");

	// Calc average fps
	int fps_sum = 0;
	for (auto fps : captured_framerates)
		fps_sum += fps;
	int average_fps = fps_sum / captured_framerates.size();

	// Calc average frame time
	/*int frametime_sum = 0;
	for (auto frametime : captured_frametimes)
		frametime_sum += frametime;
	int average_frametime = frametime_sum / captured_frametimes.size();*/

	//file << "Average frametime over " << captured_frametimes.size() << ": " << average_frametime << '\n';
	file << "Average framerate over " << captured_framerates.size() << ": " << average_fps << '\n';

	for (auto fps : captured_framerates)
	{
		file << fps << '\n';
	}

	file.close();
}

#ifdef temp
int main()
#else
int WINAPI WinMain(HINSTANCE instance, HINSTANCE prev_instance, PSTR cmd_line, INT show_cmd)
#endif
{
	BufferPerfApp* app = new BufferPerfApp();
	app->InitDebugLayer();
	app->SetupD3D12();
#ifdef temp
	app->SetupWindow(GetModuleHandle(0), 1);
#else
	app->SetupWindow(instance, show_cmd);
#endif
	app->SetupSwapchain();
	app->StartLoop();

	delete app;

	return 0;
}
