#include <dm/usb_device.hpp>
#include <dm/usb_descriptors.hpp>
#include <windows.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <sstream>
#include <iomanip>
namespace dm::usb {
namespace {
void need(bool ok, const char* why) {
    if (!ok) throw std::runtime_error(why);
}
std::wstring upper(std::wstring s) {
    for (auto& c : s) c = towupper(c);
    return s;
}
std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    auto n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0, nullptr,
                                 nullptr);
    need(n > 0, "USB device name is invalid");
    std::string result(n, 0);
    need(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), int(value.size()), result.data(), n, nullptr,
                             nullptr) == n,
         "USB device name is invalid");
    return result;
}
std::vector<std::wstring> prop(DEVINST node, ULONG which) {
    ULONG type = 0, size = 0;
    const auto first = CM_Get_DevNode_Registry_PropertyW(node, which, &type, nullptr, &size, 0);
    if (first == CR_NO_SUCH_VALUE) return {};
    need(first == CR_BUFFER_SMALL && size >= 2 && size <= 65536 && size % 2 == 0, "Cannot read USB device properties");
    std::vector<wchar_t> data(size / 2 + 2, 0);
    need(CM_Get_DevNode_Registry_PropertyW(node, which, &type, data.data(), &size, 0) == CR_SUCCESS &&
             (type == REG_SZ || type == REG_MULTI_SZ),
         "USB device properties changed");
    std::vector<std::wstring> result;
    for (std::size_t i = 0; i < data.size() && data[i];) {
        std::wstring value(data.data() + i);
        need(i + value.size() < data.size(), "USB device property is truncated");
        result.push_back(value);
        i += value.size() + 1;
        if (type == REG_SZ) break;
    }
    return result;
}
std::wstring bus_reported_name(DEVINST node) {
    DEVPROPTYPE type = 0;
    ULONG size = 0;
    if (CM_Get_DevNode_PropertyW(node, &DEVPKEY_Device_BusReportedDeviceDesc, &type, nullptr, &size, 0) !=
            CR_BUFFER_SMALL ||
        type != DEVPROP_TYPE_STRING || size < 2 || size > 4096)
        return {};
    std::vector<wchar_t> data(size / 2 + 1, 0);
    if (CM_Get_DevNode_PropertyW(node, &DEVPKEY_Device_BusReportedDeviceDesc, &type,
                                 reinterpret_cast<PBYTE>(data.data()), &size, 0) != CR_SUCCESS ||
        type != DEVPROP_TYPE_STRING)
        return {};
    return data.data();
}
std::string first(const std::vector<std::wstring>& values) {
    return values.empty() ? std::string{} : utf8(values.front());
}
std::wstring node_id(DEVINST node) {
    wchar_t id[MAX_DEVICE_ID_LEN + 1]{};
    return CM_Get_Device_IDW(node, id, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS ? upper(id) : std::wstring{};
}
// Collect what the device exposes (keyboard, mouse, audio...) from its own
// present child nodes: interfaces (USB\...&MI_xx) and the function devices below
// them. Separate USB devices behind an internal hub are not this device's
// functions and are skipped with their subtrees. Best effort only: naming never
// affects availability.
void functions(DEVINST parent, std::vector<std::string>& out, int depth) {
    if (depth > 4 || out.size() > 16) return;
    DEVINST child = 0;
    if (CM_Get_Child(&child, parent, 0) != CR_SUCCESS) return;
    do {
        const auto id = node_id(child);
        if (id.empty() || (id.starts_with(L"USB\\") && id.find(L"&MI_") == std::wstring::npos)) continue;
        try {
            auto description = prop(child, CM_DRP_FRIENDLYNAME);
            if (description.empty()) description = prop(child, CM_DRP_DEVICEDESC);
            auto name = usb_function_name(first(prop(child, CM_DRP_CLASS)), first(description));
            if (!name.empty()) out.push_back(std::move(name));
        } catch (const std::exception&) {}
        functions(child, out, depth + 1);
    } while (CM_Get_Sibling(&child, child, 0) == CR_SUCCESS);
}
std::string digest(const std::string& text) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    need(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0,
         "Cannot fingerprint USB descriptors");
    struct A {
        BCRYPT_ALG_HANDLE h;
        ~A() { BCryptCloseAlgorithmProvider(h, 0); }
    } a{algorithm};
    std::array<unsigned char, 32> hash{};
    need(BCryptHash(algorithm, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(text.data())), ULONG(text.size()),
                    hash.data(), ULONG(hash.size())) >= 0,
         "Cannot fingerprint USB descriptors");
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto byte : hash) out << std::setw(2) << unsigned(byte);
    return out.str();
}
}
std::vector<GenericDevice> generic_inventory(HostRunner& runner) {
    std::vector<GenericDevice> result;
    for (const auto& host : runner.devices()) {
        GenericDevice item;
        item.instance_id = upper(host.instance_id);
        item.bus_id = host.bus_id;
        item.id = "usb-generic-" + digest(utf8(item.instance_id));
        item.label = "USB device";
        GenericFacts facts;
        facts.owned = host.forced || host.attached || !host.binding_id.empty();
        std::string reported, manufacturer, driver;
        std::vector<std::string> parts;
        try {
            DEVINST root = 0;
            need(CM_Locate_DevNodeW(&root, item.instance_id.data(), CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS,
                 "USB device is no longer present");
            auto names = prop(root, CM_DRP_FRIENDLYNAME);
            if (names.empty()) names = prop(root, CM_DRP_DEVICEDESC);
            driver = first(names);
            // Each naming source is independent and best effort.
            try {
                reported = utf8(bus_reported_name(root));
            } catch (const std::exception&) {}
            try {
                manufacturer = first(prop(root, CM_DRP_MFG));
            } catch (const std::exception&) {}
            try {
                functions(root, parts, 0);
            } catch (const std::exception&) {}
            bool bluetooth = false;
            for (auto service : prop(root, CM_DRP_SERVICE)) bluetooth |= upper(service) == L"BTHUSB";
            for (auto compatible : prop(root, CM_DRP_COMPATIBLEIDS))
                bluetooth |= upper(compatible).starts_with(L"USB\\CLASS_E0");
            need(!bluetooth, "Bluetooth USB adapters are not supported for forwarding");
            need(!item.bus_id.empty(), "USB device has no available bus address");
            ULONG status = 0, problem = 0;
            need(CM_Get_DevNode_Status(&status, &problem, root, 0) == CR_SUCCESS,
                 "USB device was removed during inspection");
            need(!(status & DN_HAS_PROBLEM), "Windows reports a problem with this USB device or its driver");
            need(!facts.owned, "USB device is already bound or attached elsewhere");
            const auto descriptors = read_device_descriptors(item.instance_id);
            item.vendor = std::uint16_t(descriptors.device[8] | (unsigned(descriptors.device[9]) << 8));
            item.product = std::uint16_t(descriptors.device[10] | (unsigned(descriptors.device[11]) << 8));
            facts.hid = descriptors.classification.hid;
            facts.audio = descriptors.classification.audio;
            facts.composite = descriptors.classification.composite;
            facts.descriptors_verified = true;
            facts.complete = true;
            std::string snapshot = utf8(item.instance_id);
            auto append = [&](const auto& blob) {
                snapshot += std::to_string(blob.size()) + ":";
                snapshot.append(reinterpret_cast<const char*>(blob.data()), blob.size());
            };
            append(descriptors.device);
            for (const auto& config : descriptors.configurations) append(config);
            item.fingerprint = digest(snapshot);
            const auto eligible = generic_eligibility(facts);
            item.available = eligible.available;
            item.reason = eligible.reason;
        } catch (const std::exception& e) {
            item.reason = e.what();
        }
        if (!item.vendor && !item.product) { // descriptors unavailable: strict fallback to the instance ID
            const auto vid = usb_id_word(item.instance_id, L"VID_"), pid = usb_id_word(item.instance_id, L"PID_");
            if (vid && pid) {
                item.vendor = *vid;
                item.product = *pid;
            }
        }
        // `name` excludes child functions, which disappear while a device is
        // bound, so duplicate detection stays stable during sharing.
        item.name = generic_label(reported, manufacturer, driver, {}, item.vendor, item.product);
        item.label = generic_label(reported, manufacturer, driver, parts, item.vendor, item.product);
        result.push_back(std::move(item));
    }
    return result;
}
}
