#pragma once
#include <dm/network.hpp>
#include <atomic>
#include <string>
namespace dm::host {
struct Control {
    std::atomic_bool stop=false; // Forced process/parent shutdown; cancellation preserves USB quarantine.
    std::atomic_bool quit=false; // Normal UI stop waits for safe USB eject.
    std::atomic_bool eject_usb=false;
};
// One interface-scoped listener. Per-resource failures never stop other resources.
void serve(const net::Options&,const std::string& pair,bool allow_input,unsigned frames,Control&);
}
