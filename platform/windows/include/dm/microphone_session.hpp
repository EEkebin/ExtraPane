#pragma once
#include <dm/network.hpp>
#include <atomic>
#include <functional>
namespace dm::security { class TlsStream; }
namespace dm::microphone {
using Report=std::function<void(const std::string&)>;
// Dedicated PCM microphone-only mode. Configure TLS access-code policy first.
// This is not the legacy display listener; do not reuse its occupied port.
void receive_admitted(security::TlsStream&,std::atomic_bool& stop,const Report& report);
void serve(const net::Options& options,std::atomic_bool& stop,const Report& report);
void send(const net::Options& options,const std::wstring& local_device,
    std::atomic_bool& stop,std::atomic_bool& mute,const Report& report);
}
