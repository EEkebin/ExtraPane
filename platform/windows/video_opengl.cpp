#include <dm/video_opengl.hpp>
#include <dm/video_layout.hpp>
#include <GL/gl.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace dm::video {
using Microsoft::WRL::ComPtr;
namespace {
constexpr GLenum framebuffer=0x8D40,read_framebuffer=0x8CA8,draw_framebuffer=0x8CA9;
constexpr GLenum renderbuffer=0x8D41,color_attachment=0x8CE0,framebuffer_complete=0x8CD5;
constexpr GLenum rgba8=0x8058,bgra=0x80E1;
void require(bool ok,const char* stage){if(!ok)throw std::runtime_error(std::string(stage)+" (Windows error "+std::to_string(GetLastError())+")");}
void dxcheck(HRESULT hr,const char* stage){if(FAILED(hr))throw std::runtime_error(std::string(stage)+" (HRESULT "+std::to_string(static_cast<unsigned long>(hr))+")");}
void glcheck(const char* stage){const auto error=glGetError();if(error!=GL_NO_ERROR)throw std::runtime_error(std::string(stage)+" (OpenGL error "+std::to_string(error)+")");}
template<class T>T load(const char* name){auto p=wglGetProcAddress(name);const auto value=reinterpret_cast<std::intptr_t>(p);if(!p||value==1||value==2||value==3||value==-1)throw std::runtime_error(std::string("OpenGL driver is missing ")+name);return reinterpret_cast<T>(p);}
bool extension(const char* list,std::string_view name){std::string_view text=list?list:"";for(size_t start=0;start<text.size();){auto end=text.find(' ',start);if(end==text.npos)end=text.size();if(text.substr(start,end-start)==name)return true;start=end+1;}return false;}
// Initialization and diagnostics restore the caller's context. Presentation
// retains its context on the render thread to avoid expensive driver rebinding.
struct Current {
    HDC previous_dc=wglGetCurrentDC();HGLRC previous=wglGetCurrentContext();
    Current(HDC dc,HGLRC context){require(wglMakeCurrent(dc,context)!=FALSE,"Activate OpenGL context on this window");}
    ~Current(){wglMakeCurrent(previous_dc,previous);}
};
}
struct OpenGLRenderer::State {
    using Clock=std::chrono::steady_clock;
    bool profile=GetEnvironmentVariableW(L"EXTRAPANE_GL_PROFILE",nullptr,0)>0;
    unsigned profile_frames=0;double timings[6]{};
    void elapsed(unsigned index,Clock::time_point& start){const auto now=Clock::now();if(profile)timings[index]+=std::chrono::duration<double,std::milli>(now-start).count();start=now;}
    HWND attached_window=nullptr;HDC attached_dc=nullptr;HDC saved_dc=nullptr;HGLRC saved_context=nullptr;bool saved_current=false;
    HWND probe=nullptr;HDC probe_dc=nullptr;HGLRC context=nullptr;int pixel_format=0;
    ComPtr<ID3D11Device> decoder;ComPtr<ID3D11Texture2D> texture;
    std::unique_ptr<GpuProcessor> processor;std::string name;
    HANDLE interop=nullptr,object=nullptr;bool locked=false;
    GLuint source_buffer=0,source_fbo=0;unsigned texture_width=0,texture_height=0;
    using OpenDevice=HANDLE(WINAPI*)(void*);using CloseDevice=BOOL(WINAPI*)(HANDLE);
    using RegisterObject=HANDLE(WINAPI*)(HANDLE,void*,GLuint,GLenum,GLenum);
    using UnregisterObject=BOOL(WINAPI*)(HANDLE,HANDLE);using LockObjects=BOOL(WINAPI*)(HANDLE,GLint,HANDLE*);
    OpenDevice open_device=nullptr;CloseDevice close_device=nullptr;RegisterObject register_object=nullptr;
    UnregisterObject unregister_object=nullptr;LockObjects lock_objects=nullptr,unlock_objects=nullptr;
    using Gen=void(APIENTRY*)(GLsizei,GLuint*);using Delete=void(APIENTRY*)(GLsizei,const GLuint*);using Bind=void(APIENTRY*)(GLenum,GLuint);
    Gen gen_buffers=nullptr,gen_fbos=nullptr;Delete delete_buffers=nullptr,delete_fbos=nullptr;Bind bind_buffer=nullptr,bind_fbo=nullptr;
    void(APIENTRY* attach)(GLenum,GLenum,GLenum,GLuint)=nullptr;
    GLenum(APIENTRY* status)(GLenum)=nullptr;
    void(APIENTRY* storage)(GLenum,GLenum,GLsizei,GLsizei)=nullptr;
    void(APIENTRY* blit)(GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum)=nullptr;
    BOOL(WINAPI* swap_interval)(int)=nullptr;

    ~State(){
        if(profile&&profile_frames){std::cout<<"OPENGL_PROFILE frames="<<profile_frames;const char* names[]={"convert","lock","draw","unlock","context","swap"};for(unsigned i=0;i<6;++i)std::cout<<' '<<names[i]<<"_ms="<<timings[i]/profile_frames;std::cout<<std::endl;}
        if(context&&probe_dc){
            auto previous=wglGetCurrentContext();auto previous_dc=wglGetCurrentDC();if(saved_current&&(!previous||previous==context)){previous=saved_context;previous_dc=saved_dc;}
            if(wglMakeCurrent(probe_dc,context)){clear_texture();if(source_fbo&&delete_fbos)delete_fbos(1,&source_fbo);if(interop&&close_device)close_device(interop);}
            wglMakeCurrent(nullptr,nullptr);wglDeleteContext(context);
            if(previous&&previous!=context)wglMakeCurrent(previous_dc,previous);
        }
        if(attached_dc)ReleaseDC(attached_window,attached_dc);
        if(probe_dc)ReleaseDC(probe,probe_dc);if(probe)DestroyWindow(probe);
    }
    void attach_window(HWND window){
        if(!saved_current){saved_context=wglGetCurrentContext();saved_dc=wglGetCurrentDC();saved_current=true;}
        if(attached_window!=window){
            HDC next=GetDC(window);require(next!=nullptr,"Get OpenGL presentation DC");
            try{format(next);require(wglMakeCurrent(next,context)!=FALSE,"Activate OpenGL on this window");require(swap_interval(1)!=FALSE,"Set OpenGL presentation interval");}
            catch(...){wglMakeCurrent(probe_dc,context);ReleaseDC(window,next);throw;}
            if(attached_dc)ReleaseDC(attached_window,attached_dc);attached_dc=next;attached_window=window;
        }else if(wglGetCurrentContext()!=context||wglGetCurrentDC()!=attached_dc){require(wglMakeCurrent(attached_dc,context)!=FALSE,"Restore OpenGL presentation context");}
    }
    void format(HDC dc){
        const auto existing=GetPixelFormat(dc);if(existing){if(existing!=pixel_format)throw std::runtime_error("OpenGL window pixel format is incompatible with the renderer");return;}
        PIXELFORMATDESCRIPTOR desc{};require(DescribePixelFormat(dc,pixel_format,sizeof(desc),&desc)!=0,"Read OpenGL pixel format");require(SetPixelFormat(dc,pixel_format,&desc)!=FALSE,"Set OpenGL window pixel format");
    }
    void initialize(ID3D11Device* device,unsigned width,unsigned height){
        decoder=device;processor=std::make_unique<GpuProcessor>(device);
        probe=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"ExtraPane OpenGL support check",WS_POPUP,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(probe!=nullptr,"Create hidden OpenGL support window");probe_dc=GetDC(probe);require(probe_dc!=nullptr,"Get OpenGL support DC");
        PIXELFORMATDESCRIPTOR desc{};desc.nSize=sizeof(desc);desc.nVersion=1;desc.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;desc.iPixelType=PFD_TYPE_RGBA;desc.cColorBits=32;desc.cAlphaBits=8;
        pixel_format=ChoosePixelFormat(probe_dc,&desc);require(pixel_format!=0,"No OpenGL window pixel format available");format(probe_dc);
        PIXELFORMATDESCRIPTOR actual{};require(DescribePixelFormat(probe_dc,pixel_format,sizeof(actual),&actual)!=0,"Inspect OpenGL pixel format");
        if(actual.dwFlags&PFD_GENERIC_FORMAT)throw std::runtime_error("Only software OpenGL is available. Install the GPU manufacturer's graphics driver.");
        context=wglCreateContext(probe_dc);require(context!=nullptr,"Create OpenGL driver context");Current current(probe_dc,context);
        const auto version=reinterpret_cast<const char*>(glGetString(GL_VERSION));int major=0,minor=0;if(!version||sscanf_s(version,"%d.%d",&major,&minor)!=2||major<3)throw std::runtime_error("OpenGL 3.0 or newer is required for GPU framebuffer scaling");
        const auto renderer=reinterpret_cast<const char*>(glGetString(GL_RENDERER));name=renderer?renderer:"Unknown OpenGL GPU";
        auto extensions=load<const char*(WINAPI*)(HDC)>("wglGetExtensionsStringARB")(probe_dc);
        if(!extension(extensions,"WGL_NV_DX_interop2"))throw std::runtime_error("The OpenGL driver lacks WGL_NV_DX_interop2 (D3D11 GPU texture sharing). Use D3D11, D3D12 or Vulkan.");
        if(!extension(extensions,"WGL_EXT_swap_control"))throw std::runtime_error("The OpenGL driver lacks WGL_EXT_swap_control for paced presentation");
        open_device=load<OpenDevice>("wglDXOpenDeviceNV");close_device=load<CloseDevice>("wglDXCloseDeviceNV");register_object=load<RegisterObject>("wglDXRegisterObjectNV");unregister_object=load<UnregisterObject>("wglDXUnregisterObjectNV");lock_objects=load<LockObjects>("wglDXLockObjectsNV");unlock_objects=load<LockObjects>("wglDXUnlockObjectsNV");
        gen_buffers=load<Gen>("glGenRenderbuffers");delete_buffers=load<Delete>("glDeleteRenderbuffers");bind_buffer=load<Bind>("glBindRenderbuffer");
        gen_fbos=load<Gen>("glGenFramebuffers");delete_fbos=load<Delete>("glDeleteFramebuffers");bind_fbo=load<Bind>("glBindFramebuffer");
        attach=load<decltype(attach)>("glFramebufferRenderbuffer");status=load<decltype(status)>("glCheckFramebufferStatus");storage=load<decltype(storage)>("glRenderbufferStorage");blit=load<decltype(blit)>("glBlitFramebuffer");swap_interval=load<decltype(swap_interval)>("wglSwapIntervalEXT");
        interop=open_device(decoder.Get());require(interop!=nullptr,"OpenGL cannot share the active decoder GPU; check the graphics driver and GPU assignment");
        gen_fbos(1,&source_fbo);resize_texture(width,height);
        lock();bind_fbo(read_framebuffer,source_fbo);attach(read_framebuffer,color_attachment,renderbuffer,source_buffer);
        const auto complete=status(read_framebuffer);unlock();if(complete!=framebuffer_complete)throw std::runtime_error("OpenGL GPU "+name+" cannot read the active decoder's shared framebuffer (status "+std::to_string(complete)+"). Check Windows GPU assignment or use D3D11.");
        bind_fbo(framebuffer,0);require(swap_interval(1)!=FALSE,"Set OpenGL presentation interval");require(SwapBuffers(probe_dc)!=FALSE,"Probe OpenGL Windows presentation");glcheck("Probe OpenGL texture sharing");
    }
    void clear_texture(){
        if(locked&&unlock_objects){unlock_objects(interop,1,&object);locked=false;}
        if(object&&unregister_object)unregister_object(interop,object);object=nullptr;
        if(source_buffer&&delete_buffers)delete_buffers(1,&source_buffer);source_buffer=0;texture.Reset();texture_width=texture_height=0;
    }
    void resize_texture(unsigned width,unsigned height){
        if(texture&&texture_width==width&&texture_height==height)return;clear_texture();
        D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=1;desc.ArraySize=1;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        dxcheck(decoder->CreateTexture2D(&desc,nullptr,&texture),"Create OpenGL interop texture");gen_buffers(1,&source_buffer);
        object=register_object(interop,texture.Get(),source_buffer,renderbuffer,0);require(object!=nullptr,"OpenGL cannot register the decoder's BGRA texture");texture_width=width;texture_height=height;
    }
    void lock(){require(lock_objects(interop,1,&object)!=FALSE,"Acquire decoder texture for OpenGL");locked=true;}
    void unlock(){require(unlock_objects(interop,1,&object)!=FALSE,"Release decoder texture from OpenGL");locked=false;}
    void draw(const Frame& frame,GLuint output,unsigned width,unsigned height){
        if(!frame.gpu||frame.gpu->device.Get()!=decoder.Get()||!frame.bgra.empty())throw std::runtime_error("OpenGL requires GPU-decoded frames on the interop adapter");
        auto started=Clock::now();resize_texture(frame.width,frame.height);processor->blit(*frame.gpu,frame.width,frame.height,texture.Get());processor->submit();elapsed(0,started);
        // WGL lock/unlock synchronizes the two APIs. Neither API accesses the
        // registered resource while the other owns it; no CPU pixel bridge.
        lock();elapsed(1,started);
        try{
            bind_fbo(read_framebuffer,source_fbo);attach(read_framebuffer,color_attachment,renderbuffer,source_buffer);glReadBuffer(color_attachment);
            if(status(read_framebuffer)!=framebuffer_complete)throw std::runtime_error("OpenGL shared framebuffer is incomplete");
            bind_fbo(draw_framebuffer,output);glDrawBuffer(output?color_attachment:GL_BACK);glDisable(GL_SCISSOR_TEST);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
            const auto rect=content_rect(frame.width,frame.height,width,height);
            // D3D rows start at the top. Reverse destination Y for GL's bottom origin.
            blit(0,0,frame.width,frame.height,rect.x,height-rect.y,rect.x+rect.width,height-rect.y-rect.height,GL_COLOR_BUFFER_BIT,GL_LINEAR);
            glcheck("Scale OpenGL frame");elapsed(2,started);unlock();elapsed(3,started);
        }catch(...){if(locked){unlock_objects(interop,1,&object);locked=false;}throw;}
    }
};
OpenGLRenderer::OpenGLRenderer(ID3D11Device* decoder,unsigned width,unsigned height):state_(std::make_unique<State>()){state_->initialize(decoder,width,height);}
OpenGLRenderer::~OpenGLRenderer()=default;
std::string OpenGLRenderer::adapter_name()const{return state_->name;}
bool OpenGLRenderer::present(HWND window,const Frame& frame){
    RECT rect{};GetClientRect(window,&rect);if(rect.right<2||rect.bottom<2)return false;
    auto& s=*state_;auto started=State::Clock::now();
    s.attach_window(window);s.elapsed(4,started);
    s.draw(frame,0,rect.right,rect.bottom);started=State::Clock::now();require(SwapBuffers(s.attached_dc)!=FALSE,"Present OpenGL frame");s.elapsed(5,started);
    ++s.profile_frames;return true;
}
Frame OpenGLRenderer::diagnostic(const Frame& frame,unsigned width,unsigned height){
    auto& s=*state_;Current current(s.probe_dc,s.context);GLuint target=0,buffer=0;
    auto cleanup=[&]{s.bind_fbo(framebuffer,0);if(target)s.delete_fbos(1,&target);if(buffer)s.delete_buffers(1,&buffer);};
    try{
        s.gen_buffers(1,&buffer);s.bind_buffer(renderbuffer,buffer);s.storage(renderbuffer,rgba8,width,height);s.gen_fbos(1,&target);s.bind_fbo(framebuffer,target);s.attach(framebuffer,color_attachment,renderbuffer,buffer);
        if(s.status(framebuffer)!=framebuffer_complete)throw std::runtime_error("OpenGL diagnostic framebuffer is incomplete");s.draw(frame,target,width,height);
        s.bind_fbo(read_framebuffer,target);glReadBuffer(color_attachment);glPixelStorei(GL_PACK_ALIGNMENT,1);
        Frame result{width,height,std::vector<std::uint8_t>(static_cast<size_t>(width)*height*4)};std::vector<std::uint8_t> pixels(result.bgra.size());glReadPixels(0,0,width,height,bgra,GL_UNSIGNED_BYTE,pixels.data());glcheck("Read OpenGL diagnostic pixels");
        for(unsigned y=0;y<height;++y)std::memcpy(result.bgra.data()+static_cast<size_t>(y)*width*4,pixels.data()+static_cast<size_t>(height-1-y)*width*4,static_cast<size_t>(width)*4);cleanup();return result;
    }catch(...){cleanup();throw;}
}
}
