#include "renderer_settings.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
QVariantMap option(const RendererSettings& r,const QString& id){for(const auto& v:r.options()){const auto m=v.toMap();if(m["id"]==id)return m;}throw std::runtime_error("missing option");}
std::unique_ptr<QSettings> store(const QString& path){return std::make_unique<QSettings>(path,QSettings::IniFormat);}
QString saved(const QString& path){QSettings s(path,QSettings::IniFormat);return s.value("Renderer").toString();}
}

int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    try{
        QTemporaryDir dir;require(dir.isValid(),"temp dir");
        const auto path=dir.filePath("renderer.ini");
        {// Defaults: only D3D11 selectable before a check.
            RendererSettings r("missing-dm-view.exe",store(path));
            require(r.renderer()=="d3d11"&&!r.checked()&&!r.checking(),"default renderer");
            require(option(r,"d3d11")["available"].toBool()&&!option(r,"vulkan")["available"].toBool(),"unchecked availability");
            require(!r.setRenderer("vulkan")&&r.renderer()=="d3d11"&&!r.message().isEmpty(),"unchecked renderer accepted");
            require(!r.setRenderer("metal")&&r.renderer()=="d3d11","unknown renderer accepted");
            // Probe output, including noise.
            r.applyProbeLine("garbage");r.applyProbeLine("RENDERER_INFO metal available Apple");r.applyProbeLine("RENDERER_INFO d3d12");
            r.applyProbeLine("RENDERER_INFO d3d11 available NVIDIA RTX");
            r.applyProbeLine("RENDERER_INFO d3d12 available NVIDIA RTX; shared NV12 textures and GPU fences checked");
            r.applyProbeLine("RENDERER_INFO vulkan unavailable vkCreateInstance failed");
            r.finishProbe();
            require(r.checked()&&r.message().isEmpty(),"probe finish");
            require(option(r,"d3d12")["available"].toBool()&&option(r,"d3d12")["detail"]=="Available on NVIDIA RTX","d3d12 detail");
            require(!option(r,"vulkan")["available"].toBool()&&option(r,"vulkan")["detail"]=="vkCreateInstance failed","vulkan reason");
            require(!option(r,"opengl")["available"].toBool()&&option(r,"opengl")["detail"].toString().contains("Not reported"),"unreported renderer");
            require(r.setRenderer("d3d12")&&r.renderer()=="d3d12"&&saved(path)=="d3d12","renderer not saved");
            require(!r.setRenderer("vulkan")&&r.renderer()=="d3d12"&&saved(path)=="d3d12","unavailable renderer saved");
        }
        {// Saved choice is loaded, and replaced by D3D11 when the check says it no longer works.
            RendererSettings r("missing-dm-view.exe",store(path));
            require(r.renderer()=="d3d12","saved renderer not loaded");
            r.applyProbeLine("RENDERER_INFO d3d11 available Intel");r.applyProbeLine("RENDERER_INFO d3d12 unavailable Driver too old");
            r.finishProbe();
            require(r.renderer()=="d3d11"&&saved(path)=="d3d11"&&r.message().contains("D3D12")&&r.message().contains("Driver too old"),"unavailable saved renderer kept");
        }
        {// D3D11 stays selectable even when reported unavailable (it is the fallback).
            RendererSettings r("missing-dm-view.exe",store(path));
            r.applyProbeLine("RENDERER_INFO d3d11 unavailable No GPU");r.finishProbe();
            require(option(r,"d3d11")["available"].toBool()&&option(r,"d3d11")["detail"]=="No GPU"&&r.setRenderer("d3d11"),"default disabled");
        }
        {// Unknown stored values fall back to D3D11.
            QSettings(path,QSettings::IniFormat).setValue("Renderer","metal");
            RendererSettings r("missing-dm-view.exe",store(path));require(r.renderer()=="d3d11","invalid stored renderer");
        }
        {// A probe program that cannot start finishes cleanly without enabling anything.
            QSettings(path,QSettings::IniFormat).setValue("Renderer","opengl");
            RendererSettings r(dir.filePath("does-not-exist.exe"),store(path));
            r.check();require(r.checking()||r.checked(),"check did not run");// start failure may be reported synchronously
            QElapsedTimer t;t.start();while(!r.checked()&&t.elapsed()<10000){app.processEvents();QThread::msleep(5);}
            require(r.checked()&&!r.checking(),"failed probe did not finish");
            require(!option(r,"opengl")["available"].toBool()&&r.renderer()=="d3d11"&&saved(path)=="d3d11","failed probe enabled a renderer");
            require(!r.message().isEmpty(),"failed probe has no message");
        }
        std::cout<<"Renderer settings: defaults, probe parsing, persistence, fallback and failed checks passed; no GPU or registry used\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
