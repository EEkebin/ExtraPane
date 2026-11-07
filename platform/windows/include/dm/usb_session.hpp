#pragma once
#include <dm/network.hpp>
#include <atomic>
#include <functional>
#include <string>
namespace dm::usb {
using Report=std::function<void(const std::string&)>;
// Explicit local inventory only; constructor/config validation never calls this.
void list_storage(const Report&);
void send_storage(const net::Options&,const std::string& opaque_id,std::atomic_bool& stop,const Report&);
void receive_storage(const net::Options&,std::atomic_bool& stop,std::atomic_bool& eject,const Report&);
}
