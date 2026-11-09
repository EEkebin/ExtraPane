#pragma once
#include <extrapane/capabilities.hpp>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <string_view>
namespace dm::usb {
struct Disk;
struct PendingRecovery;
struct HostDevice {std::wstring instance_id;std::string bus_id;std::wstring binding_id;bool forced=false,attached=false;};
bool import_port_absent(unsigned exit_code,std::string_view output);
bool matches_generic_import_port(std::string_view output,unsigned port,std::uint16_t local_port,const std::string& bus,bool require_active);
bool matches_import_port(std::string_view output,unsigned port,std::uint16_t local_port,
    const std::string& bus,const std::string& serial,bool require_active);
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
    // Generic nonstorage exports use a separate durable exact-GUID lease.
    virtual extrapane::Availability bind_generic(const std::string&){return {false,"Generic USB binding is unavailable"};}
    virtual extrapane::Availability release_generic(){return {false,"Generic USB release is unavailable"};}
    virtual extrapane::Availability cleanup_generic(){return {false,"Generic USB lease cleanup is unavailable"};}
    virtual extrapane::Availability adopt_ejected_binding(const Disk&,const PendingRecovery&){return {false,"This backend cannot adopt a verified ejected binding"};}
    virtual extrapane::Availability adopt_interrupted_binding(const Disk&,const PendingRecovery&){return {false,"This backend cannot adopt an explicitly acknowledged interrupted binding"};}
    virtual extrapane::Availability verify_idle_source(const Disk&){return {false,"This backend cannot prove an idle unbound source"};}
    virtual std::wstring absent_binding_source(const PendingRecovery&){return {};}
    virtual extrapane::Availability release_absent_binding(const PendingRecovery&,const std::wstring&){return {false,"Absent source release is unavailable"};}
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
    virtual unsigned attach_generic(std::uint16_t,const std::string&){throw std::runtime_error("Generic USB import unavailable");}
    virtual bool verify_owned_generic(unsigned,std::uint16_t,const std::string&,bool=true){return false;}
    virtual void detach_generic(unsigned,std::uint16_t,const std::string&){throw std::runtime_error("Generic USB detach unavailable");}
    virtual unsigned attach(std::uint16_t local_port,const std::string& bus,const std::string& serial)=0;
    virtual bool verify_owned(unsigned port,std::uint16_t local_port,const std::string& bus,const std::string& serial)=0;
    // Caller must have completed a device-specific successful PnP eject first.
    virtual void detach_ejected(unsigned port,std::uint16_t local_port,const std::string& bus,const std::string& serial)=0;
    // Interrupted teardown: exact process-owned port/URI/nonce only. This does
    // not claim destination flush or authorize a clean-eject receipt.
    virtual void detach_interrupted(unsigned,std::uint16_t,const std::string&,const std::string&){throw std::runtime_error("Interrupted import detach is unavailable");}
};
std::unique_ptr<ImportRunner> native_import_runner();
}
