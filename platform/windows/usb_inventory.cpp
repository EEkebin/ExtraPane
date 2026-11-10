#include <dm/usb_inventory.hpp>
#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <psapi.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <cwctype>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>

namespace dm::usb {
namespace {
constexpr GUID disk_interface = {0x53f56307, 0xb6bf, 0x11d0, {0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b}};
constexpr GUID basic_data = {0xebd0a0a2, 0xb9e5, 0x4433, {0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7}};
constexpr std::size_t max_query = 1024 * 1024;
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() {
        if (value != INVALID_HANDLE_VALUE) CloseHandle(value);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit Handle(HANDLE h) : value(h) {}
};
std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (!count) throw std::runtime_error("Invalid device identity encoding");
    std::string result(count, 0);
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(),
                        count, nullptr, nullptr);
    return result;
}
template <class T>
std::optional<T> value_at(std::span<const unsigned char> bytes, std::size_t offset = 0) {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) return {};
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof value);
    return value;
}
std::optional<std::vector<unsigned char>> query(HANDLE handle, DWORD control, void* input = nullptr, DWORD size = 0) {
    for (DWORD capacity = 4096; capacity <= max_query; capacity *= 2) {
        std::vector<unsigned char> result(capacity);
        DWORD returned = 0;
        if (DeviceIoControl(handle, control, input, size, result.data(), capacity, &returned, nullptr)) {
            if (returned > capacity) return {};
            result.resize(returned);
            return result;
        }
        const auto error = GetLastError();
        if (error != ERROR_MORE_DATA && error != ERROR_INSUFFICIENT_BUFFER) return {};
    }
    return {};
}
std::optional<std::set<DWORD>> volume_disks(std::wstring volume) {
    if (!volume.empty() && volume.back() == L'\\') volume.pop_back();
    Handle handle(CreateFileW(volume.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, 0, nullptr));
    if (handle.value == INVALID_HANDLE_VALUE) return {};
    const auto data = query(handle.value, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS);
    if (!data) return {};
    const auto count = value_at<DWORD>(*data);
    if (!count || !*count ||
        *count > (data->size() - std::min(data->size(), offsetof(VOLUME_DISK_EXTENTS, Extents))) / sizeof(DISK_EXTENT))
        return {};
    std::set<DWORD> disks;
    for (DWORD i = 0; i < *count; ++i) {
        const auto extent =
            value_at<DISK_EXTENT>(*data, offsetof(VOLUME_DISK_EXTENTS, Extents) + i * sizeof(DISK_EXTENT));
        if (!extent || extent->StartingOffset.QuadPart < 0 || extent->ExtentLength.QuadPart <= 0) return {};
        disks.insert(extent->DiskNumber);
    }
    return disks;
}
std::optional<std::set<DWORD>> path_disks(std::wstring path) {
    if (path.rfind(L"\\??\\", 0) == 0) path.erase(0, 4);
    wchar_t mount[32768]{}, volume[128]{};
    if (!GetVolumePathNameW(path.c_str(), mount, 32768) || !GetVolumeNameForVolumeMountPointW(mount, volume, 128))
        return {};
    return volume_disks(volume);
}
struct Paging {
    bool complete = true;
    std::set<DWORD> disks;
};
BOOL CALLBACK page_file(LPVOID context, PENUM_PAGE_FILE_INFORMATION, LPCWSTR name) {
    auto& paging = *static_cast<Paging*>(context);
    try {
        const auto disks = path_disks(name ? name : L"");
        if (!disks)
            paging.complete = false;
        else
            paging.disks.insert(disks->begin(), disks->end());
    } catch (...) {
        paging.complete = false;
    }
    return TRUE;
}
std::optional<std::wstring> property(DEVINST node, ULONG key) {
    ULONG type = 0, size = 0;
    auto status = CM_Get_DevNode_Registry_PropertyW(node, key, &type, nullptr, &size, 0);
    if (status != CR_BUFFER_SMALL || size < sizeof(wchar_t) || size > 65536 || size % sizeof(wchar_t)) return {};
    std::vector<wchar_t> value(size / sizeof(wchar_t) + 1, 0);
    if (CM_Get_DevNode_Registry_PropertyW(node, key, &type, value.data(), &size, 0) != CR_SUCCESS ||
        (type != REG_SZ && type != REG_MULTI_SZ))
        return {};
    std::wstring result(value.data(), size / sizeof(wchar_t));
    std::transform(result.begin(), result.end(), result.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towupper(ch)); });
    return result;
}
// Stop at the physical USB VID/PID node, not its hub, so sibling ports do not
// make a single-function device look composite. Any uncertain tree is rejected.
struct UsbParent {
    bool composite;
    std::wstring identity;
    bool unique;
};
std::optional<UsbParent> composite(DEVINST node) {
    bool multi = false;
    for (unsigned depth = 0; depth < 32; ++depth) {
        wchar_t id[MAX_DEVICE_ID_LEN]{};
        if (CM_Get_Device_IDW(node, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) return {};
        std::wstring identity(id);
        std::transform(identity.begin(), identity.end(), identity.begin(),
                       [](wchar_t ch) { return static_cast<wchar_t>(std::towupper(ch)); });
        if (identity.find(L"&MI_") != std::wstring::npos) multi = true;
        if (identity.rfind(L"USB\\VID_", 0) == 0 && identity.find(L"&MI_") == std::wstring::npos) {
            const auto compatible = property(node, CM_DRP_COMPATIBLEIDS);
            if (!compatible) return {};
            if (compatible->find(L"USB\\COMPOSITE") != std::wstring::npos) multi = true;
            DEVINST child = 0, sibling = 0;
            const auto children = CM_Get_Child(&child, node, 0);
            if (children == CR_SUCCESS) {
                const auto next = CM_Get_Sibling(&sibling, child, 0);
                if (next == CR_SUCCESS)
                    multi = true;
                else if (next != CR_NO_SUCH_DEVNODE)
                    return {};
            } else if (children != CR_NO_SUCH_DEVNODE)
                return {};
            ULONG caps = 0, kind = 0, bytes = sizeof(caps);
            if (CM_Get_DevNode_Registry_PropertyW(node, CM_DRP_CAPABILITIES, &kind, &caps, &bytes, 0) != CR_SUCCESS ||
                kind != REG_DWORD || bytes != sizeof(caps))
                return {};
            return UsbParent{multi, identity, (caps & CM_DEVCAP_UNIQUEID) != 0};
        }
        DEVINST parent = 0;
        if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS) return {};
        node = parent;
    }
    return {};
}
}

std::optional<detail::Descriptor> detail::descriptor(std::span<const unsigned char> bytes) {
    const auto value = value_at<STORAGE_DEVICE_DESCRIPTOR>(bytes);
    if (!value || value->Size < sizeof(STORAGE_DEVICE_DESCRIPTOR) || value->Size > bytes.size()) return {};
    Descriptor result{value->BusType == BusTypeUsb, value->RemovableMedia != FALSE, {}};
    for (const DWORD offset : {value->VendorIdOffset, value->ProductIdOffset}) {
        if (!offset) continue;
        if (offset < offsetof(STORAGE_DEVICE_DESCRIPTOR, RawDeviceProperties) || offset >= value->Size) return {};
        auto end = offset;
        while (end < value->Size && bytes[end]) ++end;
        if (end == value->Size) return {};
        std::string part(reinterpret_cast<const char*>(bytes.data() + offset), end - offset);
        for (auto& ch : part)
            if (static_cast<unsigned char>(ch) < 32 || static_cast<unsigned char>(ch) > 126) ch = '?';
        while (!part.empty() && part.back() == ' ') part.pop_back();
        if (!part.empty()) {
            if (!result.label.empty()) result.label += ' ';
            result.label += part;
        }
    }
    if (value->SerialNumberOffset) {
        const auto offset = value->SerialNumberOffset;
        if (offset < offsetof(STORAGE_DEVICE_DESCRIPTOR, RawDeviceProperties) || offset >= value->Size) return {};
        auto end = offset;
        while (end < value->Size && bytes[end]) ++end;
        if (end == value->Size || end - offset > 256) return {};
        bool nontrivial = false;
        for (auto i = offset; i < end; ++i) {
            if (bytes[i] < 32 || bytes[i] > 126) return {};
            if (bytes[i] != ' ' && bytes[i] != '0') nontrivial = true;
        }
        result.stable_serial = nontrivial;
    }
    return result;
}
std::optional<bool> detail::protected_layout(std::span<const unsigned char> bytes) {
    if (bytes.size() < offsetof(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry)) return {};
    const auto style = value_at<PARTITION_STYLE>(bytes);
    const auto count = value_at<DWORD>(bytes, offsetof(DRIVE_LAYOUT_INFORMATION_EX, PartitionCount));
    if (!style || !count ||
        *count >
            (bytes.size() - offsetof(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry)) / sizeof(PARTITION_INFORMATION_EX))
        return {};
    if (*style != PARTITION_STYLE_MBR && *style != PARTITION_STYLE_GPT) return true;
    for (DWORD i = 0; i < *count; ++i) {
        const auto part = value_at<PARTITION_INFORMATION_EX>(
            bytes, offsetof(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry) + i * sizeof(PARTITION_INFORMATION_EX));
        if (!part || part->PartitionStyle != *style) return {};
        if (part->PartitionLength.QuadPart == 0) continue;
        if (part->StartingOffset.QuadPart < 0 || part->PartitionLength.QuadPart < 0) return {};
        if (*style == PARTITION_STYLE_GPT) {
            // Only ordinary data partitions are admitted, excluding EFI, MSR,
            // recovery, dynamic storage, and platform-required partitions.
            if (!IsEqualGUID(part->Gpt.PartitionType, basic_data) || (part->Gpt.Attributes & 1)) return true;
        } else {
            const auto type = part->Mbr.PartitionType;
            if (part->Mbr.BootIndicator || !(type == 0x01 || type == 0x04 || type == 0x06 || type == 0x07 ||
                                             type == 0x0b || type == 0x0c || type == 0x0e))
                return true;
        }
    }
    return false;
}

Inventory inventory(const OwnershipProbe& owned_elsewhere) {
    Inventory result;
    std::map<std::wstring, std::string> fingerprints;
    wchar_t windows[32768]{};
    const auto windowsLength = GetWindowsDirectoryW(windows, 32768);
    const auto system = (windowsLength && windowsLength < 32768) ? path_disks(windows) : std::nullopt;
    Paging paging;
    if (!EnumPageFilesW(&page_file, &paging)) paging.complete = false;
    std::vector<std::pair<std::wstring, std::set<DWORD>>> volumes;
    bool volumesComplete = true;
    wchar_t volume[128]{};
    const HANDLE volumeSearch = FindFirstVolumeW(volume, 128);
    if (volumeSearch == INVALID_HANDLE_VALUE)
        volumesComplete = false;
    else {
        do {
            const auto disks = volume_disks(volume);
            if (disks)
                volumes.emplace_back(volume, *disks);
            else
                volumesComplete = false;
        } while (FindNextVolumeW(volumeSearch, volume, 128));
        if (GetLastError() != ERROR_NO_MORE_FILES) volumesComplete = false;
        FindVolumeClose(volumeSearch);
    }
    struct DeviceSet {
        HDEVINFO value;
        ~DeviceSet() {
            if (value != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(value);
        }
    } devices{SetupDiGetClassDevsW(&disk_interface, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)};
    if (devices.value == INVALID_HANDLE_VALUE) {
        result.errors.push_back("Cannot enumerate present disk interfaces");
        return result;
    }
    for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA entry{};
        entry.cbSize = sizeof entry;
        if (!SetupDiEnumDeviceInterfaces(devices.value, nullptr, &disk_interface, index, &entry)) {
            if (GetLastError() != ERROR_NO_MORE_ITEMS)
                result.errors.push_back("Disk interface enumeration was incomplete");
            break;
        }
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(devices.value, &entry, nullptr, 0, &size, nullptr);
        if (size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || size > 65536) {
            result.errors.push_back("Invalid disk interface detail size");
            continue;
        }
        std::vector<unsigned char> buffer(size);
        auto* interfaceDetail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
        interfaceDetail->cbSize = sizeof *interfaceDetail;
        SP_DEVINFO_DATA device{};
        device.cbSize = sizeof device;
        if (!SetupDiGetDeviceInterfaceDetailW(devices.value, &entry, interfaceDetail, size, nullptr, &device)) {
            result.errors.push_back("Cannot read disk interface identity");
            continue;
        }
        const auto pathCapacity =
            (buffer.size() - offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_W, DevicePath)) / sizeof(wchar_t);
        if (std::find(interfaceDetail->DevicePath, interfaceDetail->DevicePath + pathCapacity, L'\0') ==
            interfaceDetail->DevicePath + pathCapacity) {
            result.errors.push_back("Unterminated disk interface identity");
            continue;
        }
        Disk disk;
        disk.interface_path = interfaceDetail->DevicePath;
        wchar_t identity[MAX_DEVICE_ID_LEN]{};
        if (CM_Get_Device_IDW(device.DevInst, identity, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) {
            result.errors.push_back("Cannot read disk instance identity");
            continue;
        }
        disk.instance_id = identity;
        disk.facts.id = utf8(disk.instance_id);
        Handle handle(CreateFileW(disk.interface_path.c_str(), 0,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0,
                                  nullptr));
        if (handle.value == INVALID_HANDLE_VALUE) {
            disk.unresolved.push_back("Cannot inspect storage device");
            result.disks.push_back(std::move(disk));
            continue;
        }
        STORAGE_PROPERTY_QUERY request{};
        request.PropertyId = StorageDeviceProperty;
        request.QueryType = PropertyStandardQuery;
        const auto data = query(handle.value, IOCTL_STORAGE_QUERY_PROPERTY, &request, sizeof request);
        const auto description = data ? detail::descriptor(*data) : std::nullopt;
        if (!description)
            disk.unresolved.push_back("Storage descriptor unavailable or malformed");
        else {
            if (!description->usb) continue;
            disk.label = description->label;
            disk.facts.removable_storage = description->removable;
            disk.descriptor_bytes = *data;
        }
        const auto numberData = query(handle.value, IOCTL_STORAGE_GET_DEVICE_NUMBER);
        const auto number = numberData ? value_at<STORAGE_DEVICE_NUMBER>(*numberData) : std::nullopt;
        if (!number || number->DeviceType != FILE_DEVICE_DISK)
            disk.unresolved.push_back("Physical disk number unavailable");
        else
            disk.disk_number = number->DeviceNumber;
        const auto layout = query(handle.value, IOCTL_DISK_GET_DRIVE_LAYOUT_EX);
        if (layout) disk.layout_bytes = *layout;
        auto& fingerprint = fingerprints[disk.instance_id];
        for (const auto* response : {&data, &numberData, &layout}) {
            const auto length = response->has_value() ? response->value().size() : 0;
            fingerprint += std::to_string(length) + ":";
            if (length) fingerprint.append(reinterpret_cast<const char*>(response->value().data()), length);
        }
        const auto protection = layout ? detail::protected_layout(*layout) : std::nullopt;
        if (!protection)
            disk.unresolved.push_back("Partition protection facts unavailable");
        else
            disk.facts.system_disk = *protection;
        const auto compositeDevice = composite(device.DevInst);
        if (!compositeDevice)
            disk.unresolved.push_back("USB ancestry or composite status unavailable");
        else {
            disk.facts.composite = compositeDevice->composite;
            disk.usb_instance_id = compositeDevice->identity;
            disk.unique_identity = compositeDevice->unique && description && description->stable_serial;
        }
        if (!disk.unique_identity)
            disk.unresolved.push_back("USB device lacks a unique physical identity and usable storage serial");
        if (!system)
            disk.unresolved.push_back("Windows volume disk mapping unavailable");
        else
            disk.facts.boot_disk = system->contains(disk.disk_number);
        if (!paging.complete)
            disk.unresolved.push_back("Paging-file disk mapping unavailable");
        else
            disk.facts.paging_disk = paging.disks.contains(disk.disk_number);
        if (!volumesComplete) disk.unresolved.push_back("Volume extent inventory incomplete");
        for (const auto& [name, disks] : volumes)
            if (disks.contains(disk.disk_number)) disk.volumes.push_back(name);
        std::optional<bool> owned;
        try {
            if (owned_elsewhere && !disk.usb_instance_id.empty()) owned = owned_elsewhere(disk.usb_instance_id);
        } catch (...) {}
        if (!owned)
            disk.unresolved.push_back("Local USB export ownership has not been verified");
        else
            disk.facts.owned_elsewhere = *owned;
        disk.facts.inventory_complete = disk.unresolved.empty();
        result.disks.push_back(std::move(disk));
    }
    // A failed outer enumeration cannot produce a complete snapshot.
    if (!result.errors.empty())
        for (auto& disk : result.disks) {
            disk.facts.inventory_complete = false;
            disk.unresolved.push_back("Disk enumeration incomplete");
        }
    // Stable across matching reinspection, invalidated on any observed identity,
    // layout, descriptor, volume or eligibility change. This is not a hotplug
    // lease: callers must still hold/revalidate device handles before detach.
    struct Revision {
        std::string fingerprint;
        std::uint64_t generation = 0;
    };
    static std::mutex revisionMutex;
    static std::map<std::wstring, Revision> revisions;
    static std::uint64_t nextGeneration = 0;
    std::lock_guard lock(revisionMutex);
    std::set<std::wstring> present;
    for (auto& disk : result.disks) {
        present.insert(disk.instance_id);
        auto fingerprint = fingerprints[disk.instance_id];
        const auto append = [&](const std::string& value) {
            fingerprint += std::to_string(value.size()) + ":" + value;
        };
        append(utf8(disk.interface_path));
        append(std::to_string(disk.disk_number));
        append(utf8(disk.usb_instance_id));
        fingerprint += disk.unique_identity ? '1' : '0';
        for (const auto flag :
             {disk.facts.inventory_complete, disk.facts.removable_storage, disk.facts.composite, disk.facts.system_disk,
              disk.facts.boot_disk, disk.facts.paging_disk, disk.facts.owned_elsewhere})
            fingerprint += flag ? '1' : '0';
        std::sort(disk.volumes.begin(), disk.volumes.end());
        for (const auto& name : disk.volumes) append(utf8(name));
        for (const auto& reason : disk.unresolved) append(reason);
        auto& revision = revisions[disk.instance_id];
        if (!revision.generation || revision.fingerprint != fingerprint) {
            revision.fingerprint = std::move(fingerprint);
            revision.generation = ++nextGeneration;
        }
        disk.facts.generation = revision.generation;
    }
    std::erase_if(revisions, [&](const auto& entry) { return !present.contains(entry.first); });
    return result;
}
}
