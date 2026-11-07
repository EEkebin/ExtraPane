#pragma once
#include <dm/network.hpp>
#include <dm/camera.hpp>
#include <atomic>
#include <functional>
namespace dm::resources {
using Report=std::function<void(const std::string&)>;
// Dedicated camera-only listener for the first resource-sharing slice. The
// selected port cannot simultaneously belong to the legacy display listener.
// No monitor or display driver is created. Configure access-code policy before
// calling; all peers complete existing TLS/access admission before allocation.
void serve_camera(const net::Options& options,std::atomic_bool& stop,const Report& report);
void send_camera(const net::Options& options,const std::wstring& local_device,
    camera::Format format,std::atomic_bool& stop,const Report& report);
}
