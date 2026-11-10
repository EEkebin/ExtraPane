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
    bool hid=false,audio=false,owned=false,descriptors_verified=false;
};
extrapane::Availability generic_eligibility(const GenericFacts&);
// Short user-facing function name ("Keyboard", "Mouse", "Audio"...) for a child
// device node's setup class and description, or empty when it is not meaningful.
std::string usb_function_name(const std::string& device_class,const std::string& description);
// Distinguishing label: device-reported product string (falling back to the
// driver description), a non-generic manufacturer, the child functions, and
// VID:PID only when the name is still generic.
std::string generic_label(const std::string& product,const std::string& manufacturer,const std::string& driver_name,const std::vector<std::string>& functions,std::uint16_t vendor,std::uint16_t product_id);
// Appends the USB port (" - port 3-4", middle-dot separator) to labels shared by several devices.
void disambiguate_labels(std::vector<GenericDevice>&);
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
