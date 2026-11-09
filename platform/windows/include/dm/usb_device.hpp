#pragma once
#include <dm/usb_runner.hpp>
#include <functional>
#include <memory>
namespace dm::usb {
struct GenericDevice {
    std::string id,label,fingerprint,bus_id,reason;
    std::wstring instance_id;
    std::uint16_t vendor=0,product=0;
    bool available=false;
};
// Explicit read-only enumeration. No constructor or build enumerates devices.
std::vector<GenericDevice> generic_inventory(HostRunner&);
struct GenericFacts {
    bool complete=false,storage=false,hub=false,controller=false,unknown=false,composite=false;
    bool hid=false,audio=false,owned=false;
};
extrapane::Availability generic_eligibility(const GenericFacts&);
class GenericExport {
public:
    using Inspect=std::function<std::vector<GenericDevice>()>;
    GenericExport(std::unique_ptr<HostRunner>,Inspect);
    ~GenericExport();
    GenericExport(const GenericExport&)=delete;
    extrapane::Availability start(const GenericDevice&,std::uint16_t);
    extrapane::Availability stop()noexcept;
    std::string bus()const{return bus_;}
private:
    std::unique_ptr<HostRunner> runner_;Inspect inspect_;
    bool reserved_=false,bound_=false;
    std::string bus_;
};
}
