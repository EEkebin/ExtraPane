#pragma once
#include <dm/network.hpp>
#include <atomic>
#include <functional>
namespace dm::security {
class TlsStream;
}
namespace dm::speaker {
using Report = std::function<void(const std::string&)>;
void send_admitted(security::TlsStream&, std::atomic_bool& stop, const Report&, unsigned slot);
void receive(const net::Options&, const std::wstring& output, std::atomic_bool& stop, std::atomic_bool& mute,
             std::atomic_uint& volume, std::atomic_uint64_t& muteEpoch, const Report&);
}
