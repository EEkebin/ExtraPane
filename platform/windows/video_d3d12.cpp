#include <dm/video_d3d12.hpp>
#include <dm/video_vulkan.hpp>
#include <dm/video_layout.hpp>
#include <d3d11_4.h>
#include <d3d10_1.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace dm::video {
using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT hr,const char* stage){if(FAILED(hr)){std::ostringstream text;text<<stage<<" (0x"<<std::hex<<static_cast<unsigned long>(hr)<<")";throw std::runtime_error(text.str());}}
struct Handle {HANDLE value=nullptr;~Handle(){if(value)CloseHandle(value);}};
struct ContextLock {ID3D10Multithread* threading;explicit ContextLock(ID3D10Multithread* t):threading(t){threading->Enter();}~ContextLock(){threading->Leave();}};
void transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&barrier);
}
constexpr char shader[]=R"(
Texture2D<float> YPlane:register(t0);
Texture2D<float2> UVPlane:register(t1);
SamplerState LinearSampler:register(s0);
cbuffer Geometry:register(b0){float2 SourceScale;};
struct VertexOutput{float4 pos:SV_Position;float2 uv:TEXCOORD0;};
VertexOutput vs(uint id:SV_VertexID){VertexOutput o;float2 uv=float2((id<<1)&2,id&2);o.pos=float4(uv*float2(2,-2)+float2(-1,1),0,1);o.uv=uv;return o;}
float4 ps(VertexOutput input):SV_Target{
 float2 uv=input.uv*SourceScale;
 float y=(YPlane.Sample(LinearSampler,uv)-16.0/255.0)*1.16438356;
 float2 c=UVPlane.Sample(LinearSampler,uv)-float2(128.0/255.0,128.0/255.0);
 return float4(saturate(float3(y+1.79274107*c.y,y-0.21324861*c.x-0.53290933*c.y,y+2.11240179*c.x)),1);
})";
}
struct D3D12Renderer::State {
    ComPtr<ID3D11Device> decoder;
    ComPtr<ID3D11Device5> decoder5;
    ComPtr<ID3D11DeviceContext4> context;
    ComPtr<ID3D10Multithread> threading;
    ComPtr<ID3D12Device> device;
    ComPtr<IDXGIFactory2> factory;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> copied,finished;
    ComPtr<ID3D11Fence> copied11;
    ComPtr<ID3D11Texture2D> shared11;
    ComPtr<ID3D12Resource> shared12;
    ComPtr<ID3D12DescriptorHeap> srv,rtv;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    ComPtr<IDXGISwapChain3> swap;
    std::array<ComPtr<ID3D12Resource>,2> backbuffers;
    Handle event;
    UINT rtv_step=0,texture_width=0,texture_height=0,window_width=0,window_height=0;
    UINT64 copy_value=0,finish_value=0;
    std::string name;

    State(ID3D11Device* source,unsigned width,unsigned height):decoder(source){
        check(decoder.As(&decoder5),"D3D12 requires D3D11 shared-fence support");
        ComPtr<ID3D11DeviceContext> original;decoder->GetImmediateContext(&original);
        check(original.As(&context),"D3D12 requires a fence-capable decoder context");
        check(original.As(&threading),"Decoder multithread synchronization unavailable");
        ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;
        check(decoder.As(&dxgi),"Find decoder GPU");check(dxgi->GetAdapter(&adapter),"Find decoder adapter");
        DXGI_ADAPTER_DESC description{};check(adapter->GetDesc(&description),"Read decoder GPU name");
        for(auto c:description.Description){if(!c)break;name.push_back(c<128?static_cast<char>(c):'?');}
        check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"D3D12 device unavailable on decoder GPU; update its graphics driver");
        check(adapter->GetParent(IID_PPV_ARGS(&factory)),"D3D12 DXGI factory unavailable");
        D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"D3D12 graphics queue unavailable");
        check(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&allocator)),"D3D12 command allocator");
        check(device->CreateCommandList(0,q.Type,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"D3D12 command list");check(list->Close(),"Close initial D3D12 command list");
        check(device->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&copied)),"D3D12 shared fence unavailable");
        Handle handle;check(device->CreateSharedHandle(copied.Get(),nullptr,GENERIC_ALL,nullptr,&handle.value),"Share D3D12 fence");
        check(decoder5->OpenSharedFence(handle.value,IID_PPV_ARGS(&copied11)),"Decoder cannot open D3D12 fence");
        check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&finished)),"D3D12 completion fence");
        event.value=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event.value)throw std::runtime_error("Cannot create D3D12 completion event");
        D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;heap.NumDescriptors=2;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&srv)),"D3D12 NV12 descriptor heap");
        heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        check(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&rtv)),"D3D12 target descriptor heap");rtv_step=device->GetDescriptorHandleIncrementSize(heap.Type);
        D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,2,0,0,D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};
        D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[0].DescriptorTable={1,&range};params[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[1].Constants={0,0,2};params[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;sampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC signature{2,params,1,&sampler,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
        ComPtr<ID3DBlob> signature_code,errors,vertex,pixel;
        check(D3D12SerializeRootSignature(&signature,D3D_ROOT_SIGNATURE_VERSION_1,&signature_code,&errors),"D3D12 root signature serialization");
        check(device->CreateRootSignature(0,signature_code->GetBufferPointer(),signature_code->GetBufferSize(),IID_PPV_ARGS(&root)),"D3D12 root signature");
        check(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"vs","vs_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vertex,&errors),"D3D12 vertex shader");
        check(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"ps","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&pixel,&errors),"D3D12 NV12 pixel shader");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};p.pRootSignature=root.Get();p.VS={vertex->GetBufferPointer(),vertex->GetBufferSize()};p.PS={pixel->GetBufferPointer(),pixel->GetBufferSize()};
        for(auto& blend:p.BlendState.RenderTarget){blend.SrcBlend=blend.SrcBlendAlpha=D3D12_BLEND_ONE;blend.DestBlend=blend.DestBlendAlpha=D3D12_BLEND_ZERO;blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;blend.LogicOp=D3D12_LOGIC_OP_NOOP;blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;}
        p.SampleMask=UINT_MAX;p.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;p.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;p.RasterizerState.DepthClipEnable=TRUE;
        p.DepthStencilState.DepthEnable=FALSE;p.DepthStencilState.StencilEnable=FALSE;p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;p.NumRenderTargets=1;p.RTVFormats[0]=DXGI_FORMAT_B8G8R8A8_UNORM;p.SampleDesc.Count=1;
        check(device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&pipeline)),"D3D12 video pipeline unavailable");
        texture(width,height);
        // Check actual cross-API fence execution, not only DLL/device presence.
        {ContextLock lock(threading.Get());check(context->Signal(copied11.Get(),++copy_value),"Decoder shared-fence signal");context->Flush();}
        check(queue->Wait(copied.Get(),copy_value),"D3D12 shared-fence wait");idle();
    }
    void idle(){
        check(queue->Signal(finished.Get(),++finish_value),"D3D12 completion signal");
        if(finished->GetCompletedValue()<finish_value){check(finished->SetEventOnCompletion(finish_value,event.value),"D3D12 completion event");if(WaitForSingleObject(event.value,5000)!=WAIT_OBJECT_0)throw std::runtime_error("D3D12 GPU timed out");}
        check(device->GetDeviceRemovedReason(),"D3D12 GPU removed");
    }
    void texture(unsigned width,unsigned height){
        if(shared12&&texture_width==width&&texture_height==height)return;
        if(shared12)idle();shared12.Reset();shared11.Reset();
        D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=1;desc.ArraySize=1;desc.Format=DXGI_FORMAT_NV12;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED_NTHANDLE|D3D11_RESOURCE_MISC_SHARED;
        check(decoder->CreateTexture2D(&desc,nullptr,&shared11),"Decoder GPU cannot share NV12 textures with D3D12");
        ComPtr<IDXGIResource1> resource;check(shared11.As(&resource),"NV12 shared-resource interface");Handle handle;
        check(resource->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&handle.value),"Create NV12 shared handle");
        check(device->OpenSharedHandle(handle.value,IID_PPV_ARGS(&shared12)),"D3D12 cannot open decoder NV12 texture");
        const auto step=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);auto cpu=srv->GetCPUDescriptorHandleForHeapStart();
        for(UINT plane=0;plane<2;++plane){D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=plane?DXGI_FORMAT_R8G8_UNORM:DXGI_FORMAT_R8_UNORM;view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.Texture2D.MipLevels=1;view.Texture2D.PlaneSlice=plane;device->CreateShaderResourceView(shared12.Get(),&view,cpu);cpu.ptr+=step;}
        texture_width=width;texture_height=height;
    }
    void begin(const Frame& frame){
        if(!frame.gpu||frame.gpu->device.Get()!=decoder.Get()||!frame.bgra.empty())throw std::runtime_error("D3D12 requires GPU frames on the decoder adapter");
        D3D11_TEXTURE2D_DESC desc{};frame.gpu->texture->GetDesc(&desc);
        if(desc.Format!=DXGI_FORMAT_NV12||!frame.width||!frame.height||frame.width>desc.Width||frame.height>desc.Height||frame.gpu->subresource>=desc.ArraySize*desc.MipLevels)throw std::runtime_error("D3D12 requires valid NV12 decoder surfaces");
        texture(desc.Width,desc.Height);
        {ContextLock lock(threading.Get());context->CopySubresourceRegion(shared11.Get(),0,0,0,0,frame.gpu->texture.Get(),frame.gpu->subresource,nullptr);check(context->Signal(copied11.Get(),++copy_value),"Signal decoded texture ready");context->Flush();}
        check(queue->Wait(copied.Get(),copy_value),"Wait for decoded GPU texture");
        check(allocator->Reset(),"Reset D3D12 allocator");check(list->Reset(allocator.Get(),pipeline.Get()),"Reset D3D12 command list");
    }
    void draw(const Frame& frame,ID3D12Resource* output,D3D12_CPU_DESCRIPTOR_HANDLE target,unsigned width,unsigned height,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
        transition(list.Get(),shared12.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        transition(list.Get(),output,before,D3D12_RESOURCE_STATE_RENDER_TARGET);
        const float black[]{0,0,0,1};list->ClearRenderTargetView(target,black,0,nullptr);list->OMSetRenderTargets(1,&target,FALSE,nullptr);
        const auto rect=content_rect(frame.width,frame.height,width,height);D3D12_VIEWPORT viewport{static_cast<float>(rect.x),static_cast<float>(rect.y),static_cast<float>(rect.width),static_cast<float>(rect.height),0,1};D3D12_RECT scissor{0,0,static_cast<LONG>(width),static_cast<LONG>(height)};
        list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&scissor);list->SetGraphicsRootSignature(root.Get());ID3D12DescriptorHeap* heaps[]={srv.Get()};list->SetDescriptorHeaps(1,heaps);list->SetGraphicsRootDescriptorTable(0,srv->GetGPUDescriptorHandleForHeapStart());
        const float scale[]={static_cast<float>(frame.width)/static_cast<float>(texture_width),static_cast<float>(frame.height)/static_cast<float>(texture_height)};list->SetGraphicsRoot32BitConstants(1,2,scale,0);list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);list->DrawInstanced(3,1,0,0);
        transition(list.Get(),output,D3D12_RESOURCE_STATE_RENDER_TARGET,after);transition(list.Get(),shared12.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
    }
    void submit(){check(list->Close(),"Close D3D12 draw commands");ID3D12CommandList* commands[]={list.Get()};queue->ExecuteCommandLists(1,commands);}
    void detach(){if(swap){idle();for(auto& buffer:backbuffers)buffer.Reset();swap.Reset();window_width=window_height=0;}}
};
D3D12Renderer::D3D12Renderer(ID3D11Device* decoder,unsigned width,unsigned height):state_(std::make_unique<State>(decoder,width,height)){}
D3D12Renderer::~D3D12Renderer(){try{state_->detach();}catch(...) {}}
std::string D3D12Renderer::adapter_name()const{return state_->name;}
void D3D12Renderer::detach(){state_->detach();}
void D3D12Renderer::present(HWND window,const Frame& frame){
    auto& s=*state_;RECT client{};GetClientRect(window,&client);if(client.right<2||client.bottom<2)return;
    const auto width=static_cast<UINT>(client.right),height=static_cast<UINT>(client.bottom);
    if(!s.swap){DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=width;desc.Height=height;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;desc.Scaling=DXGI_SCALING_STRETCH;
        ComPtr<IDXGISwapChain1> swap;check(s.factory->CreateSwapChainForHwnd(s.queue.Get(),window,&desc,nullptr,nullptr,&swap),"Create D3D12 preview swapchain");check(swap.As(&s.swap),"D3D12 swapchain interface");check(s.factory->MakeWindowAssociation(window,DXGI_MWA_NO_ALT_ENTER),"D3D12 window association");
    }else if(s.window_width!=width||s.window_height!=height){s.idle();for(auto& b:s.backbuffers)b.Reset();check(s.swap->ResizeBuffers(2,width,height,DXGI_FORMAT_B8G8R8A8_UNORM,0),"Resize D3D12 preview");}
    if(s.window_width!=width||s.window_height!=height){auto target=s.rtv->GetCPUDescriptorHandleForHeapStart();for(UINT i=0;i<2;++i){check(s.swap->GetBuffer(i,IID_PPV_ARGS(&s.backbuffers[i])),"D3D12 backbuffer");s.device->CreateRenderTargetView(s.backbuffers[i].Get(),nullptr,target);target.ptr+=s.rtv_step;}s.window_width=width;s.window_height=height;}
    const auto index=s.swap->GetCurrentBackBufferIndex();auto target=s.rtv->GetCPUDescriptorHandleForHeapStart();target.ptr+=index*s.rtv_step;
    s.begin(frame);s.draw(frame,s.backbuffers[index].Get(),target,width,height,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT);s.submit();
    check(s.swap->Present(1,0),"D3D12 presentation failed");s.idle(); // One GPU frame in flight; shared NV12 is safe to reuse.
}
Frame D3D12Renderer::diagnostic(const Frame& frame,unsigned width,unsigned height){
    auto& s=*state_;s.idle();D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=width;desc.Height=height;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ComPtr<ID3D12Resource> target,readback;check(s.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&target)),"D3D12 diagnostic target");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;s.device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&bytes);
    heap.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=bytes;buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    check(s.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)),"D3D12 diagnostic readback");
    auto view=s.rtv->GetCPUDescriptorHandleForHeapStart();s.device->CreateRenderTargetView(target.Get(),nullptr,view);s.begin(frame);s.draw(frame,target.Get(),view,width,height,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=target.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;s.list->CopyTextureRegion(&to,0,0,0,&from,nullptr);s.submit();s.idle();
    Frame result{width,height,std::vector<std::uint8_t>(static_cast<size_t>(width)*height*4)};void* data=nullptr;D3D12_RANGE range{0,static_cast<SIZE_T>(bytes)};check(readback->Map(0,&range,&data),"Map diagnostic result");for(unsigned row=0;row<height;++row)std::memcpy(result.bgra.data()+static_cast<size_t>(row)*width*4,static_cast<const std::uint8_t*>(data)+footprint.Offset+static_cast<size_t>(row)*footprint.Footprint.RowPitch,static_cast<size_t>(width)*4);D3D12_RANGE written{};readback->Unmap(0,&written);
    ComPtr<ID3D12InfoQueue> messages;
    if(SUCCEEDED(s.device.As(&messages)))for(UINT64 index=0;index<messages->GetNumStoredMessages();++index){SIZE_T size=0;check(messages->GetMessage(index,nullptr,&size),"Read D3D12 validation length");std::vector<unsigned char> storage(size);auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());check(messages->GetMessage(index,message,&size),"Read D3D12 validation message");if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR)throw std::runtime_error(std::string("D3D12 validation: ")+message->pDescription);}
    return result;
}
static void renderer_check(Renderer kind,unsigned adapter_index){
    // This command deliberately uploads test pixels and reads results back.
    // Neither operation is part of the production presentation path.
    ComPtr<ID3D12Debug> debug;if(kind==Renderer::d3d12&&SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
    ComPtr<IDXGIFactory1> factory;ComPtr<IDXGIAdapter1> adapter;ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"Test DXGI factory");check(factory->EnumAdapters1(adapter_index,&adapter),"Test adapter index");
    check(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"Test D3D11 device");
    ComPtr<ID3D10Multithread> threading;check(context.As(&threading),"Test context synchronization");threading->SetMultithreadProtected(TRUE);
    std::unique_ptr<D3D12Renderer> d3d12;std::unique_ptr<VulkanRenderer> vulkan;
    if(kind==Renderer::vulkan)vulkan=std::make_unique<VulkanRenderer>(device.Get(),256,128);else d3d12=std::make_unique<D3D12Renderer>(device.Get(),256,128);
    constexpr std::array<std::array<unsigned char,3>,4> colors{{{63,102,240},{173,42,26},{32,240,118},{235,128,128}}};
    unsigned checks=0;
    for(const auto size: {std::pair{256u,128u},std::pair{512u,256u},std::pair{256u,128u}}){
        const auto width=size.first,height=size.second,visible_width=width-16,visible_height=height-16;
        std::vector<std::uint8_t> pixels(static_cast<size_t>(width)*height*3/2,0);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){const auto quadrant=(x>=visible_width/2?1u:0u)+(y>=visible_height/2?2u:0u);pixels[static_cast<size_t>(y)*width+x]=colors[quadrant][0];}
        for(unsigned y=0;y<height/2;++y)for(unsigned x=0;x<width;x+=2){const auto quadrant=(x>=visible_width/2?1u:0u)+(y*2>=visible_height/2?2u:0u);const auto offset=static_cast<size_t>(width)*height+static_cast<size_t>(y)*width+x;pixels[offset]=colors[quadrant][1];pixels[offset+1]=colors[quadrant][2];}
        D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=1;desc.ArraySize=1;desc.Format=DXGI_FORMAT_NV12;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{pixels.data(),width,0};ComPtr<ID3D11Texture2D> texture;check(device->CreateTexture2D(&desc,&data,&texture),"Test NV12 upload");
        Frame input{visible_width,visible_height,{}};input.gpu=std::make_shared<GpuFrame>();input.gpu->device=device;input.gpu->texture=texture;
        for(const auto output:{std::pair{320u,200u},std::pair{200u,320u}}){
            const auto result=vulkan?vulkan->diagnostic(input,output.first,output.second):d3d12->diagnostic(input,output.first,output.second);const auto rect=content_rect(visible_width,visible_height,output.first,output.second);
            for(unsigned quadrant=0;quadrant<4;++quadrant){const unsigned x=rect.x+rect.width*(quadrant%2?3:1)/4,y=rect.y+rect.height*(quadrant/2?3:1)/4;const auto offset=(static_cast<size_t>(y)*result.width+x)*4;
                const auto c=colors[quadrant];const float luma=(c[0]/255.f-16.f/255.f)*1.16438356f,u=c[1]/255.f-128.f/255.f,v=c[2]/255.f-128.f/255.f;const float expected[]={luma+2.11240179f*u,luma-0.21324861f*u-0.53290933f*v,luma+1.79274107f*v};
                for(unsigned channel=0;channel<3;++channel)if(std::abs(static_cast<int>(result.bgra[offset+channel])-static_cast<int>(std::round(std::clamp(expected[channel],0.f,1.f)*255)))>3)throw std::runtime_error("Renderer color/orientation/crop mismatch");++checks;
            }
            if(rect.y>0||rect.x>0)for(unsigned channel=0;channel<3;++channel)if(result.bgra[channel]!=0)throw std::runtime_error("Renderer letterbox must be black");
        }
    }
    std::cout<<renderer_name(kind)<<"_CHECK passed adapter="<<(vulkan?vulkan->adapter_name():d3d12->adapter_name())<<" quadrant_checks="<<checks<<" padded-NV12 crop color orientation aspect-fit resize GPU-interop"<<std::endl;
}
void d3d12_renderer_check(unsigned adapter_index){renderer_check(Renderer::d3d12,adapter_index);}
void vulkan_renderer_check(unsigned adapter_index){renderer_check(Renderer::vulkan,adapter_index);}
void renderer_info(){
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    try{
        check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_VIDEO_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"D3D11 video device unavailable; update your graphics driver");
        ComPtr<ID3D10Multithread> threading;check(context.As(&threading),"Decoder context synchronization");threading->SetMultithreadProtected(TRUE);
        ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC desc{};check(device.As(&dxgi),"Decoder DXGI device");check(dxgi->GetAdapter(&adapter),"Decoder adapter");check(adapter->GetDesc(&desc),"Decoder adapter name");std::string name;for(auto c:desc.Description){if(!c)break;name.push_back(c<128?static_cast<char>(c):'?');}
        std::cout<<"RENDERER_INFO d3d11 available "<<name<<std::endl;
    }catch(const std::exception& e){std::cout<<"RENDERER_INFO d3d11 unavailable "<<e.what()<<"\nRENDERER_INFO d3d12 unavailable Requires a working D3D11 decoder device: "<<e.what()<<"\nRENDERER_INFO vulkan unavailable Requires a working D3D11 decoder device: "<<e.what()<<std::endl;return;}
    try{D3D12Renderer renderer(device.Get(),1280,720);std::cout<<"RENDERER_INFO d3d12 available "<<renderer.adapter_name()<<"; shared NV12 textures and GPU fences checked"<<std::endl;}
    catch(const std::exception& e){std::cout<<"RENDERER_INFO d3d12 unavailable "<<e.what()<<std::endl;}
    try{VulkanRenderer renderer(device.Get(),1280,720);std::cout<<"RENDERER_INFO vulkan available "<<renderer.adapter_name()<<"; GPU import, keyed mutex and Windows presentation checked"<<std::endl;}
    catch(const std::exception& e){std::cout<<"RENDERER_INFO vulkan unavailable "<<e.what()<<std::endl;}
}
}
