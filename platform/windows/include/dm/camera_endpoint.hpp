#pragma once
#include <dm/camera_pipe.hpp>
#include <extrapane/session.hpp>

namespace dm::camera {
// Stable installer/COM identity; this is not a physical-camera device identifier.
inline constexpr wchar_t source_class_id[]=L"{5A053AD3-02CD-4281-8367-CEF990182004}";
inline constexpr GUID pipe_attribute={0x6bb6af07,0x2814,0x4cb9,{0xb7,0xdd,0xd9,0x56,0xbc,0x5c,0x37,0x27}};

// Narrow backend allows failure/cleanup verification without registering a camera.
class CameraRegistration {
public:
    virtual ~CameraRegistration()=default;
    virtual extrapane::Availability prepare(const std::wstring& pipe,Format format,unsigned slot)=0;
    virtual extrapane::Availability start()=0;
    virtual void stop() noexcept=0;
};
std::unique_ptr<CameraRegistration> native_camera_registration();

// Receiving-side resource. Only create after admission and a server-local slot
// lease. Caller initializes COM; owner lifecycle calls are serialized.
class CameraEndpoint final:public extrapane::Resource {
public:
    CameraEndpoint(Format format,unsigned local_slot,std::unique_ptr<CameraRegistration> registration=native_camera_registration());
    ~CameraEndpoint() override;
    extrapane::Availability prepare() override;
    extrapane::Availability start() override;
    void stop() noexcept override;
    void submit(std::span<const std::uint8_t> nv12,std::int64_t timestamp);
private:
    Format format_;unsigned slot_;std::unique_ptr<CameraRegistration> registration_;
    FramePublisher publisher_;bool prepared_=false,started_=false,stopped_=false;
};
}
