#pragma once
#include <dm/usb_runner.hpp>
#include <functional>
#include <memory>
#include <optional>
namespace dm::usb {
struct GenericDevice {
    std::string id, label, fingerprint, bus_id, reason;
    std::wstring instance_id;
    std::uint16_t vendor = 0, product = 0;
    bool available = false;
    std::string name; // label without child functions; used for duplicate detection
};
// Explicit read-only enumeration. No constructor or build enumerates devices.
std::vector<GenericDevice> generic_inventory(HostRunner&);
struct GenericFacts {
    bool complete = false, storage = false, hub = false, controller = false, unknown = false, composite = false;
    bool hid = false, audio = false, owned = false, descriptors_verified = false;
};
extrapane::Availability generic_eligibility(const GenericFacts&);
// User-facing USB labels are at most this many UTF-8 bytes (peer catalogs cap at 128).
inline constexpr std::size_t usb_label_limit = 120;
// Short user-facing function name ("Keyboard", "Mouse", "Audio"...) for a child
// device node's setup class and description, or empty when it is not meaningful.
std::string usb_function_name(const std::string& device_class, const std::string& description);
// Distinguishing label: device-reported product string (falling back to the
// driver description), the INF manufacturer when it names a real vendor not
// already in the name, the child functions, and VID:PID when the device/driver
// name is generic. Always at most usb_label_limit bytes.
std::string generic_label(const std::string& product, const std::string& manufacturer, const std::string& driver_name,
                          const std::vector<std::string>& functions, std::uint16_t vendor, std::uint16_t product_id);
// Appends the USB port (middle dot, "port 3-4") to the label of each device whose
// name (or label when name is empty) is shared with another listed device or with
// one of other_labels. Labels stay within usb_label_limit. Call after filtering
// to the rows that are actually shown.
void disambiguate_labels(std::vector<GenericDevice>&, const std::vector<std::string>& other_labels = {});
// Strict 4-hex-digit VID_/PID_ value from a USB instance ID.
std::optional<std::uint16_t> usb_id_word(const std::wstring& instance, const wchar_t* key);
class GenericExport {
public:
    using Inspect = std::function<std::vector<GenericDevice>()>;
    GenericExport(std::unique_ptr<HostRunner>, Inspect);
    ~GenericExport();
    GenericExport(const GenericExport&) = delete;
    extrapane::Availability start(const GenericDevice&, std::uint16_t);
    extrapane::Availability stop() noexcept;
    std::string bus() const { return bus_; }

private:
    std::unique_ptr<HostRunner> runner_;
    Inspect inspect_;
    bool reserved_ = false, bound_ = false;
    std::string bus_;
};
}
