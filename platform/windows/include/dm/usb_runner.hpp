#pragma once
#include <extrapane/capabilities.hpp>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
namespace dm::usb {
struct Disk;
struct PendingRecovery;
struct HostDevice {std::wstring instance_id;std::string bus_id;std::wstring binding_id;bool forced=false,attached=false;};
// Pure prerequisite policy. Native adoption additionally re-reads the protected
// journal, proves the physical disk identity and checks the binding registry.
extrapane::Availability ejected_binding_adoption_policy(const PendingRecovery& selected,
    const PendingRecovery& current,const HostDevice& device,const std::wstring& reserved_instance);
extrapane::Availability interrupted_binding_adoption_policy(const PendingRecovery& selected,
    const PendingRecovery& current,const HostDevice& device,const std::wstring& reserved_instance,bool protected_acknowledgment);
class HostRunner {
public:
    virtual ~HostRunner()=default;
    virtual extrapane::Availability ready()=0;
    virtual std::vector<HostDevice> devices()=0;
    virtual extrapane::Availability reserve(const std::wstring& physicalInstance)=0;
    virtual extrapane::Availability bind(const std::string& bus)=0;
    virtual extrapane::Availability unbind(const std::wstring& ownedGuid)=0;
    virtual extrapane::Availability adopt_ejected_binding(const Disk&,const PendingRecovery&){return {false,"This backend cannot adopt a verified ejected binding"};}
    virtual extrapane::Availability adopt_interrupted_binding(const Disk&,const PendingRecovery&){return {false,"This backend cannot adopt an explicitly acknowledged interrupted binding"};}
    virtual extrapane::Availability verify_idle_source(const Disk&){return {false,"This backend cannot prove an idle unbound source"};}
    virtual extrapane::Availability start(const std::wstring& instance,const std::string& bus,std::uint16_t loopbackPort)=0;
    virtual void stop()noexcept=0;
    virtual void release()noexcept=0;
    virtual bool running()const noexcept=0;
};
std::unique_ptr<HostRunner> native_host_runner();
// Fixed managed USBip 0.9.8.1 client. No arbitrary executable or command surface.
class ImportRunner {
public:
    virtual ~ImportRunner()=default;
    virtual extrapane::Availability ready()=0;
    virtual unsigned attach(std::uint16_t local_port,const std::string& bus,const std::string& serial)=0;
    virtual bool verify_owned(unsigned port,std::uint16_t local_port,const std::string& bus,const std::string& serial)=0;
    // Caller must have completed a device-specific successful PnP eject first.
    virtual void detach_ejected(unsigned port,std::uint16_t local_port,const std::string& bus,const std::string& serial)=0;
};
std::unique_ptr<ImportRunner> native_import_runner();
}
