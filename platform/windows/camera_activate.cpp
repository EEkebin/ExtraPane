#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <wrl.h>
#include <dm/camera_activate.hpp>
#include <dm/camera_endpoint.hpp>
#include <dm/camera_media_source.hpp>
#include <mutex>

namespace dm::camera {
using namespace Microsoft::WRL;
namespace {
class Activate final:public RuntimeClass<RuntimeClassFlags<ClassicCom>,ChainInterfaces<IMFActivate,IMFAttributes>> {
public:
    HRESULT initialize(){return MFCreateAttributes(&attributes_,4);}
    STDMETHODIMP ActivateObject(REFIID id,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;std::lock_guard lock(mutex_);
        try{
            if(!source_){
                UINT32 width=0,height=0,num=0,den=0,length=0;
                auto hr=MFGetAttributeSize(attributes_.Get(),MF_MT_FRAME_SIZE,&width,&height);if(FAILED(hr))return hr;
                hr=MFGetAttributeRatio(attributes_.Get(),MF_MT_FRAME_RATE,&num,&den);if(FAILED(hr))return hr;
                if(den!=1)return MF_E_INVALIDMEDIATYPE;
                hr=attributes_->GetStringLength(pipe_attribute,&length);if(FAILED(hr))return hr;
                if(length>128)return E_INVALIDARG;
                std::wstring pipe(length+1,L'\0');hr=attributes_->GetString(pipe_attribute,pipe.data(),length+1,&length);if(FAILED(hr))return hr;pipe.resize(length);
                MediaSource feed(Format{width,height,num},std::move(pipe));
                source_=feed.detach();
            }
            return source_->QueryInterface(id,out);
        }catch(const std::invalid_argument&){return E_INVALIDARG;}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}catch(...){return E_FAIL;}
    }
    STDMETHODIMP ShutdownObject()override{
        std::lock_guard lock(mutex_);
        const auto hr=source_?source_->Shutdown():S_OK;source_.Reset();return hr;
    }
    STDMETHODIMP DetachObject()override{
        std::lock_guard lock(mutex_);source_.Reset();return S_OK;
    }
    // IMFAttributes is delegated to MF's thread-safe attribute store. The
    // activation snapshot is populated by Frame Server before ActivateObject.
#define FORWARD(name,signature,args) STDMETHODIMP name signature override{return attributes_->name args;}
    FORWARD(GetItem,(REFGUID k,PROPVARIANT* v),(k,v))
    FORWARD(GetItemType,(REFGUID k,MF_ATTRIBUTE_TYPE* v),(k,v))
    FORWARD(CompareItem,(REFGUID k,REFPROPVARIANT v,BOOL* r),(k,v,r))
    FORWARD(Compare,(IMFAttributes* a,MF_ATTRIBUTES_MATCH_TYPE t,BOOL* r),(a,t,r))
    FORWARD(GetUINT32,(REFGUID k,UINT32* v),(k,v))
    FORWARD(GetUINT64,(REFGUID k,UINT64* v),(k,v))
    FORWARD(GetDouble,(REFGUID k,double* v),(k,v))
    FORWARD(GetGUID,(REFGUID k,GUID* v),(k,v))
    FORWARD(GetStringLength,(REFGUID k,UINT32* n),(k,n))
    FORWARD(GetString,(REFGUID k,LPWSTR v,UINT32 n,UINT32* used),(k,v,n,used))
    FORWARD(GetAllocatedString,(REFGUID k,LPWSTR* v,UINT32* n),(k,v,n))
    FORWARD(GetBlobSize,(REFGUID k,UINT32* n),(k,n))
    FORWARD(GetBlob,(REFGUID k,UINT8* v,UINT32 n,UINT32* used),(k,v,n,used))
    FORWARD(GetAllocatedBlob,(REFGUID k,UINT8** v,UINT32* n),(k,v,n))
    FORWARD(GetUnknown,(REFGUID k,REFIID id,void** v),(k,id,v))
    FORWARD(SetItem,(REFGUID k,REFPROPVARIANT v),(k,v))
    FORWARD(DeleteItem,(REFGUID k),(k))
    FORWARD(DeleteAllItems,(),())
    FORWARD(SetUINT32,(REFGUID k,UINT32 v),(k,v))
    FORWARD(SetUINT64,(REFGUID k,UINT64 v),(k,v))
    FORWARD(SetDouble,(REFGUID k,double v),(k,v))
    FORWARD(SetGUID,(REFGUID k,REFGUID v),(k,v))
    FORWARD(SetString,(REFGUID k,LPCWSTR v),(k,v))
    FORWARD(SetBlob,(REFGUID k,const UINT8* v,UINT32 n),(k,v,n))
    FORWARD(SetUnknown,(REFGUID k,IUnknown* v),(k,v))
    FORWARD(LockStore,(),())
    FORWARD(UnlockStore,(),())
    FORWARD(GetCount,(UINT32* n),(n))
    FORWARD(GetItemByIndex,(UINT32 i,GUID* k,PROPVARIANT* v),(i,k,v))
    FORWARD(CopyAllItems,(IMFAttributes* a),(a))
#undef FORWARD
private:
    // Releasing an activation object only releases its reference. The caller
    // owning the activated source is responsible for explicit Shutdown.
    ComPtr<IMFAttributes> attributes_;std::mutex mutex_;ComPtr<IMFMediaSourceEx> source_;
};
}
HRESULT create_camera_activation(IMFActivate** out)noexcept{
    if(!out)return E_POINTER;*out=nullptr;
    try{auto object=Make<Activate>();if(!object)return E_OUTOFMEMORY;auto hr=object->initialize();if(FAILED(hr))return hr;return object.CopyTo(out);}
    catch(...){return E_OUTOFMEMORY;}
}
}
