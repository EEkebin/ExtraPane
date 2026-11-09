#pragma once
#include <dm/network.hpp>
#include <dm/camera.hpp>
#include <atomic>
#include <functional>
namespace dm::security { class TlsStream; }
namespace dm::resources {
using Report=std::function<void(const std::string&)>;
// Diagnostic camera-only listener. Product hosting dispatches admitted TLS below. The
// selected port cannot simultaneously belong to the legacy display listener.
// No monitor or display driver is created. Configure access-code policy before
// calling; all peers complete existing TLS/access admission before allocation.
void receive_camera_admitted(security::TlsStream&,unsigned slot,std::atomic_bool& stop,const Report& report);
void serve_camera(const net::Options& options,std::atomic_bool& stop,const Report& report);
void send_camera(const net::Options& options,const std::wstring& local_device,
    std::optional<camera::Format> format,std::atomic_bool& stop,const Report& report,const std::atomic_bool* mirrored=nullptr);
struct CameraRecipientCommand {
    enum class Kind {add,remove};
    Kind kind=Kind::remove;std::string row;
    net::Options options;std::string credential;
};
// One process/capture/encoder for one local source. Mirror is source-wide.
void broker_camera(const std::wstring& local_device,std::optional<camera::Format> format,
    std::atomic_bool& stop,const Report& report,const std::atomic_bool* mirrored,
    const std::function<std::optional<CameraRecipientCommand>()>& next_command);
}
