#pragma once
#include <string>
#include <vector>
namespace dm::usb {
struct GenericLease {
    std::wstring guid, instance;
    std::string bus;
    std::wstring parent;
    unsigned port = 0;
    friend bool operator==(const GenericLease&, const GenericLease&) = default;
};
bool valid_generic_lease(const GenericLease&);
bool generic_stub_matches(const GenericLease&, const std::wstring& instance, const std::wstring& parent, unsigned port,
                          const std::wstring& service);
enum class GenericLeaseAction { retire_only, release_binding, refuse };
GenericLeaseAction generic_lease_action(const GenericLease&, const std::wstring& current_instance, bool key_exists,
                                        bool active, bool forced, bool foreign);
std::wstring generic_lease_guid();
void save_generic_lease(const GenericLease&);
std::vector<GenericLease> generic_leases();
void retire_generic_lease(const GenericLease&);
}
