#include <windows.h>
#include <mfapi.h>
#include <mfvirtualcamera.h>
#include <dm/camera_endpoint.hpp>
#include <stdexcept>
#include <iomanip>
#include <sstream>

namespace dm::camera {
namespace {
class NativeRegistration final : public CameraRegistration {
public:
    ~NativeRegistration() override { stop(); }
    extrapane::Availability prepare(const std::wstring& pipe, Format format, unsigned slot) override {
        if (runtime_ || camera_) return {false, "Virtual camera already prepared"};
        auto hr = MFStartup(MF_VERSION);
        if (FAILED(hr)) return failure("Media Foundation initialization failed", hr);
        runtime_ = true;
        CLSID clsid{};
        hr = CLSIDFromString(source_class_id, &clsid);
        if (FAILED(hr)) {
            stop();
            return failure("Invalid camera component identity", hr);
        }
        // Fail before creating a device when setup has not installed the source.
        Microsoft::WRL::ComPtr<IClassFactory> factory;
        hr = CoGetClassObject(clsid, CLSCTX_INPROC_SERVER, nullptr, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) {
            stop();
            return failure("CoGetClassObject for ExtraPane camera source failed", hr);
        }
        const auto name = L"ExtraPane Camera " + std::to_wstring(slot);
        hr = MFCreateVirtualCamera(MFVirtualCameraType_SoftwareCameraSource, MFVirtualCameraLifetime_Session,
                                   MFVirtualCameraAccess_CurrentUser, name.c_str(), source_class_id, nullptr, 0,
                                   &camera_);
        if (FAILED(hr)) {
            stop();
            return failure("MFCreateVirtualCamera failed", hr);
        }
        hr = camera_->SetString(pipe_attribute, pipe.c_str());
        if (FAILED(hr)) {
            stop();
            return failure("Virtual camera pipe attribute failed", hr);
        }
        hr = MFSetAttributeSize(camera_.Get(), MF_MT_FRAME_SIZE, format.width, format.height);
        if (FAILED(hr)) {
            stop();
            return failure("Virtual camera frame size attribute failed", hr);
        }
        hr = MFSetAttributeRatio(camera_.Get(), MF_MT_FRAME_RATE, format.fps, 1);
        if (FAILED(hr)) {
            stop();
            return failure("Virtual camera frame rate attribute failed", hr);
        }
        return {true, {}};
    }
    extrapane::Availability start() override {
        if (!camera_) return {false, "Virtual camera is not prepared"};
        const auto hr = camera_->Start(nullptr);
        if (FAILED(hr)) {
            stop();
            return failure("IMFVirtualCamera::Start failed", hr);
        }
        return {true, {}};
    }
    void stop() noexcept override {
        // Session lifetime removes enumeration/activation when shutdown occurs.
        if (camera_) {
            camera_->Shutdown();
            camera_.Reset();
        }
        if (runtime_) {
            MFShutdown();
            runtime_ = false;
        }
    }

private:
    static extrapane::Availability failure(const char* message, HRESULT hr) {
        std::ostringstream text;
        text << message << " (HRESULT 0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
             << static_cast<unsigned long>(hr) << ")";
        return {false, text.str()};
    }
    bool runtime_ = false;
    Microsoft::WRL::ComPtr<IMFVirtualCamera> camera_;
};
}
std::unique_ptr<CameraRegistration> native_camera_registration() {
    return std::make_unique<NativeRegistration>();
}
CameraEndpoint::CameraEndpoint(Format format, unsigned slot, std::unique_ptr<CameraRegistration> registration)
    : format_(format), slot_(slot), registration_(std::move(registration)), publisher_(format) {
    if (slot < 1 || !registration_) throw std::invalid_argument("Invalid local camera slot or registration backend");
}
CameraEndpoint::~CameraEndpoint() {
    stop();
}
extrapane::Availability CameraEndpoint::prepare() {
    if (stopped_ || prepared_) return {false, "Camera endpoint already prepared or stopped"};
    try {
        publisher_.start();
        auto result = registration_->prepare(publisher_.name(), format_, slot_);
        if (!result.available) {
            stop();
            return result;
        }
        prepared_ = true;
        return {true, {}};
    } catch (const std::exception& e) {
        stop();
        return {false, std::string("Camera endpoint preparation: ") + e.what()};
    } catch (...) {
        stop();
        return {false, "Camera endpoint preparation: unknown exception"};
    }
}
extrapane::Availability CameraEndpoint::start() {
    if (!prepared_ || started_ || stopped_) return {false, "Camera endpoint is not ready"};
    try {
        auto result = registration_->start();
        if (!result.available) {
            stop();
            return result;
        }
        started_ = true;
        return result;
    } catch (const std::exception& e) {
        stop();
        return {false, std::string("Camera endpoint startup: ") + e.what()};
    } catch (...) {
        stop();
        return {false, "Camera endpoint startup: unknown exception"};
    }
}
void CameraEndpoint::stop() noexcept {
    if (stopped_) return;
    stopped_ = true;
    started_ = false;
    prepared_ = false;
    registration_->stop();
    publisher_.stop();
}
void CameraEndpoint::submit(std::span<const std::uint8_t> pixels, std::int64_t timestamp) {
    // Native decoder executor must be joined before owner calls stop/destruct.
    if (!started_ || stopped_) throw std::runtime_error("Camera endpoint is not running");
    publisher_.submit(pixels, timestamp);
}
}
