#include <windows.h>
#include <wrl.h>
#include <wrl/module.h>
#include <dm/camera_activate.hpp>
#include <dm/camera_endpoint.hpp>
#include <atomic>
using namespace Microsoft::WRL;
namespace {
std::atomic_ulong locks=0;
class Factory final:public RuntimeClass<RuntimeClassFlags<ClassicCom>,IClassFactory> {
public:
    STDMETHODIMP CreateInstance(IUnknown* outer,REFIID id,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;if(outer)return CLASS_E_NOAGGREGATION;
        ComPtr<IMFActivate> activation;auto hr=dm::camera::create_camera_activation(&activation);
        return FAILED(hr)?hr:activation->QueryInterface(id,out);
    }
    STDMETHODIMP LockServer(BOOL lock)override{
        if(lock){++locks;return S_OK;}
        auto value=locks.load();while(value){if(locks.compare_exchange_weak(value,value-1))return S_OK;}
        return E_UNEXPECTED;
    }
};
}
STDAPI DllGetClassObject(REFCLSID clsid,REFIID id,void** out){
    if(!out)return E_POINTER;*out=nullptr;
    CLSID expected{};CLSIDFromString(dm::camera::source_class_id,&expected);
    if(clsid!=expected)return CLASS_E_CLASSNOTAVAILABLE;
    (void)Module<InProc>::GetModule();
    auto factory=Make<Factory>();return factory?factory->QueryInterface(id,out):E_OUTOFMEMORY;
}
STDAPI DllCanUnloadNow(){
    return !locks.load()&&Module<InProc>::GetModule().GetObjectCount()==0?S_OK:S_FALSE;
}
