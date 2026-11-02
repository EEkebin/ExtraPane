#include <windows.h>
#include <dm/video_wait.hpp>
#include <d3d10_1.h>
#include <d3dcompiler.h>
#include <dm/video_gpu.hpp>
#include <dm/video_layout.hpp>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace dm::video {
using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT result,const char* stage) {
    if(FAILED(result)) throw std::runtime_error(std::string(stage)+" failed (HRESULT "+std::to_string(static_cast<unsigned long>(result))+")");
}
}
ComPtr<ID3D11Texture2D> make_video_texture(ID3D11Device* device,unsigned width,unsigned height,DXGI_FORMAT format) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width=width; desc.Height=height; desc.MipLevels=1; desc.ArraySize=1;
    desc.Format=format; desc.SampleDesc.Count=1; desc.Usage=D3D11_USAGE_DEFAULT;
    desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> result;
    check(device->CreateTexture2D(&desc,nullptr,&result),"Create GPU video texture");
    return result;
}
struct GpuProcessor::State {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VideoDevice> video;
    ComPtr<ID3D11VideoContext1> video_context;
    ComPtr<ID3D11VideoProcessorEnumerator> enumerator;
    ComPtr<ID3D11VideoProcessor> processor;
    ComPtr<ID3D11Query> completion;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11RasterizerState> rasterizer;
    ComPtr<ID3D11DepthStencilState> depth;
    ComPtr<ID3D10Multithread> threading;
    unsigned input_width=0,input_height=0,output_width=0,output_height=0;
    DXGI_FORMAT input_format=DXGI_FORMAT_UNKNOWN,output_format=DXGI_FORMAT_UNKNOWN;
    explicit State(ID3D11Device* value):device(value) {
        device->GetImmediateContext(&context);
        check(context.As(&threading),"GPU context synchronization");
        check(device.As(&video),"GPU video processor device");
        check(context.As(&video_context),"GPU video context 1");
        D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};
        check(device->CreateQuery(&query,&completion),"GPU completion query");
    }
    void nv12_to_bgra(const GpuFrame& source,unsigned width,unsigned height,ID3D11Texture2D* target) {
        if(!vertex) {
            constexpr char shader[]=R"(
Texture2D<float> YPlane:register(t0);
Texture2D<float2> UVPlane:register(t1);
SamplerState LinearSampler:register(s0);
cbuffer Geometry:register(b0){float2 SourceScale;float2 Padding;};
struct VertexOutput{float4 pos:SV_Position;float2 uv:TEXCOORD0;};
VertexOutput vs(uint id:SV_VertexID){VertexOutput o;float2 uv=float2((id<<1)&2,id&2);o.pos=float4(uv*float2(2,-2)+float2(-1,1),0,1);o.uv=uv;return o;}
float4 ps(VertexOutput input):SV_Target{
 float2 uv=input.uv*SourceScale;
 float y=(YPlane.Sample(LinearSampler,uv)-16.0/255.0)*1.16438356;
 float2 c=UVPlane.Sample(LinearSampler,uv)-float2(128.0/255.0,128.0/255.0);
 return float4(saturate(float3(y+1.79274107*c.y,y-0.21324861*c.x-0.53290933*c.y,y+2.11240179*c.x)),1);
})";
            ComPtr<ID3DBlob> code,errors;
            auto compile=[&](const char* entry,const char* profile) {
                code.Reset(); errors.Reset();
                const auto result=D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
                if(FAILED(result)) throw std::runtime_error(errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()) : "GPU color shader compilation failed");
            };
            compile("vs","vs_5_0"); check(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&vertex),"GPU color vertex shader");
            compile("ps","ps_5_0"); check(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&pixel),"GPU color pixel shader");
            D3D11_SAMPLER_DESC sample{}; sample.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
            sample.AddressU=sample.AddressV=sample.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; sample.MaxLOD=D3D11_FLOAT32_MAX;
            check(device->CreateSamplerState(&sample,&sampler),"GPU color sampler");
            D3D11_BUFFER_DESC buffer{}; buffer.ByteWidth=16; buffer.Usage=D3D11_USAGE_DEFAULT; buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            check(device->CreateBuffer(&buffer,nullptr,&constants),"GPU color constants");
            D3D11_RASTERIZER_DESC raster{}; raster.FillMode=D3D11_FILL_SOLID; raster.CullMode=D3D11_CULL_NONE; raster.DepthClipEnable=TRUE;
            check(device->CreateRasterizerState(&raster,&rasterizer),"GPU color rasterizer");
            D3D11_DEPTH_STENCIL_DESC depth_desc{}; depth_desc.DepthEnable=FALSE;
            check(device->CreateDepthStencilState(&depth_desc,&depth),"GPU color depth state");
        }
        if(source.subresource!=0) throw std::runtime_error("GPU color shader requires owned single-slice texture");
        D3D11_TEXTURE2D_DESC desc{},out{}; source.texture->GetDesc(&desc); target->GetDesc(&out);
        D3D11_SHADER_RESOURCE_VIEW_DESC view{}; view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; view.Texture2D.MipLevels=1;
        ComPtr<ID3D11ShaderResourceView> y,uv;
        view.Format=DXGI_FORMAT_R8_UNORM; check(device->CreateShaderResourceView(source.texture.Get(),&view,&y),"GPU NV12 luma view");
        view.Format=DXGI_FORMAT_R8G8_UNORM; check(device->CreateShaderResourceView(source.texture.Get(),&view,&uv),"GPU NV12 chroma view");
        ComPtr<ID3D11RenderTargetView> output;
        check(device->CreateRenderTargetView(target,nullptr,&output),"GPU color output target");
        const float scale[4]={static_cast<float>(width)/static_cast<float>(desc.Width),static_cast<float>(height)/static_cast<float>(desc.Height),0,0};
        context->UpdateSubresource(constants.Get(),0,nullptr,scale,0,0);
        ID3D11ShaderResourceView* inputs[]={y.Get(),uv.Get()};
        ID3D11SamplerState* samplers[]={sampler.Get()}; ID3D11Buffer* buffers[]={constants.Get()};
        ID3D11RenderTargetView* outputs[]={output.Get()};
        const auto rect=content_rect(width,height,out.Width,out.Height);
        const float black[]{0,0,0,1};context->ClearRenderTargetView(output.Get(),black);
        D3D11_VIEWPORT viewport{static_cast<float>(rect.x),static_cast<float>(rect.y),static_cast<float>(rect.width),static_cast<float>(rect.height),0,1};
        context->IASetInputLayout(nullptr); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex.Get(),nullptr,0); context->PSSetShader(pixel.Get(),nullptr,0);
        context->PSSetShaderResources(0,2,inputs); context->PSSetSamplers(0,1,samplers); context->PSSetConstantBuffers(0,1,buffers);
        context->RSSetState(rasterizer.Get()); context->RSSetViewports(1,&viewport);
        context->OMSetDepthStencilState(depth.Get(),0); context->OMSetBlendState(nullptr,nullptr,0xffffffff);
        context->OMSetRenderTargets(1,outputs,nullptr); context->Draw(3,0);
        ID3D11ShaderResourceView* empty[]={nullptr,nullptr}; context->PSSetShaderResources(0,2,empty);
        context->OMSetRenderTargets(0,nullptr,nullptr);
    }
    void configure(unsigned sw,unsigned sh,const D3D11_TEXTURE2D_DESC& source,const D3D11_TEXTURE2D_DESC& target) {
        if(processor && input_width==sw && input_height==sh && output_width==target.Width && output_height==target.Height && input_format==source.Format && output_format==target.Format) return;
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
        content.InputFrameFormat=D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        content.InputFrameRate={30,1}; content.OutputFrameRate={30,1};
        content.InputWidth=sw; content.InputHeight=sh; content.OutputWidth=target.Width; content.OutputHeight=target.Height;
        content.Usage=D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
        enumerator.Reset(); processor.Reset();
        check(video->CreateVideoProcessorEnumerator(&content,&enumerator),"Create GPU video processor enumerator");
        UINT support=0;
        check(enumerator->CheckVideoProcessorFormat(source.Format,&support),"Check GPU input format");
        if(!(support&D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT)) throw std::runtime_error("GPU does not support video input format");
        check(enumerator->CheckVideoProcessorFormat(target.Format,&support),"Check GPU output format");
        if(!(support&D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT)) throw std::runtime_error("GPU does not support video output format");
        check(video->CreateVideoProcessor(enumerator.Get(),0,&processor),"Create GPU video processor");
        input_width=sw; input_height=sh; output_width=target.Width; output_height=target.Height;
        input_format=source.Format; output_format=target.Format;
    }
};
GpuProcessor::GpuProcessor(ID3D11Device* device):state_(std::make_unique<State>(device)){}
GpuProcessor::~GpuProcessor()=default;
void GpuProcessor::submit(){state_->context->Flush();}
void GpuProcessor::blit(const GpuFrame& source,unsigned width,unsigned height,ID3D11Texture2D* target) {
    auto& s=*state_;
    if(source.device.Get()!=s.device.Get() || !source.texture || !target) throw std::runtime_error("GPU video conversion requires same-device textures");
    D3D11_TEXTURE2D_DESC src{},dst{}; source.texture->GetDesc(&src); target->GetDesc(&dst);
    if(!width || !height || width>src.Width || height>src.Height || source.subresource>=src.MipLevels*src.ArraySize) throw std::runtime_error("Invalid GPU video source rectangle or slice");
    struct Lock{ID3D10Multithread* threading;explicit Lock(ID3D10Multithread* t):threading(t){threading->Enter();}~Lock(){threading->Leave();}} lock{s.threading.Get()};
    if(src.Format==DXGI_FORMAT_NV12 && dst.Format==DXGI_FORMAT_B8G8R8A8_UNORM) {
        s.nv12_to_bgra(source,width,height,target);
        return;
    }
    s.configure(width,height,src,dst);
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_desc{};
    input_desc.ViewDimension=D3D11_VPIV_DIMENSION_TEXTURE2D;
    input_desc.Texture2D.MipSlice=source.subresource%src.MipLevels;
    input_desc.Texture2D.ArraySlice=source.subresource/src.MipLevels;
    ComPtr<ID3D11VideoProcessorInputView> input;
    check(s.video->CreateVideoProcessorInputView(source.texture.Get(),s.enumerator.Get(),&input_desc,&input),"Create GPU video input view");
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output_desc{};
    output_desc.ViewDimension=D3D11_VPOV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11VideoProcessorOutputView> output;
    check(s.video->CreateVideoProcessorOutputView(target,s.enumerator.Get(),&output_desc,&output),"Create GPU video output view");
    RECT source_rect{0,0,static_cast<LONG>(width),static_cast<LONG>(height)};
    RECT dest_rect{0,0,static_cast<LONG>(dst.Width),static_cast<LONG>(dst.Height)};
    s.video_context->VideoProcessorSetStreamFrameFormat(s.processor.Get(),0,D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
    s.video_context->VideoProcessorSetStreamAutoProcessingMode(s.processor.Get(),0,FALSE);
    s.video_context->VideoProcessorSetStreamSourceRect(s.processor.Get(),0,TRUE,&source_rect);
    s.video_context->VideoProcessorSetStreamDestRect(s.processor.Get(),0,TRUE,&dest_rect);
    s.video_context->VideoProcessorSetOutputTargetRect(s.processor.Get(),TRUE,&dest_rect);
    s.video_context->VideoProcessorSetOutputAlphaFillMode(s.processor.Get(),D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE_OPAQUE,0);
    s.video_context->VideoProcessorSetStreamColorSpace1(s.processor.Get(),0,src.Format==DXGI_FORMAT_NV12 ? DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    s.video_context->VideoProcessorSetOutputColorSpace1(s.processor.Get(),dst.Format==DXGI_FORMAT_NV12 ? DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    D3D11_VIDEO_PROCESSOR_STREAM stream{}; stream.Enable=TRUE; stream.pInputSurface=input.Get();
    check(s.video_context->VideoProcessorBlt(s.processor.Get(),output.Get(),0,1,&stream),"GPU scale/color conversion");
}
void GpuProcessor::wait_idle() {
    auto& s=*state_;
    s.context->End(s.completion.Get()); s.context->Flush();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    for(;;) {
        const auto result=s.context->GetData(s.completion.Get(),nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if(result==S_OK) return;
        check(result,"Wait for GPU video operation");
        check(s.device->GetDeviceRemovedReason(),"GPU video device removed");
        if(std::chrono::steady_clock::now()>=deadline) throw std::runtime_error("GPU video operation timed out");
        precise_wait(std::chrono::microseconds(250));
    }
}
Frame readback_gpu_bgra(const Frame& frame) {
    if(!frame.gpu) throw std::runtime_error("Diagnostic readback needs GPU frame");
    auto texture=make_video_texture(frame.gpu->device.Get(),frame.width,frame.height,DXGI_FORMAT_B8G8R8A8_UNORM);
    GpuProcessor converter(frame.gpu->device.Get());
    converter.blit(*frame.gpu,frame.width,frame.height,texture.Get());
    converter.wait_idle();
    D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
    desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    check(frame.gpu->device->CreateTexture2D(&desc,nullptr,&staging),"Diagnostic GPU staging texture");
    ComPtr<ID3D11DeviceContext> context; frame.gpu->device->GetImmediateContext(&context);
    context->CopyResource(staging.Get(),texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{}; check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Diagnostic GPU readback");
    struct Unmap{ID3D11DeviceContext* context;ID3D11Texture2D* texture;~Unmap(){context->Unmap(texture,0);}} unmap{context.Get(),staging.Get()};
    Frame result{frame.width,frame.height,{}}; result.bgra.resize(static_cast<std::size_t>(result.width)*result.height*4);
    for(unsigned y=0;y<result.height;++y) std::memcpy(result.bgra.data()+static_cast<std::size_t>(y)*result.width*4,static_cast<const std::uint8_t*>(mapped.pData)+static_cast<std::size_t>(y)*mapped.RowPitch,result.width*4);
    return result;
}
}
