#include <dm/usb_recovery.hpp>
#include <dm/usb_runner.hpp>
#include <dm/usb_generic_lease.hpp>
#include <algorithm>
#include <cwctype>
#include <sstream>
#include <stdexcept>
namespace dm::usb {
bool valid_generic_lease(const GenericLease& lease) {
    if (lease.parent.empty() != (!lease.port) || lease.port > 255 || lease.parent.size() > 4096 ||
        std::any_of(lease.parent.begin(), lease.parent.end(), [](auto c) { return c < 32 || c == 127; }))
        return false;
    if (lease.guid.size() != 38 || lease.guid.front() != L'{' || lease.guid.back() != L'}') return false;
    bool nonzero = false;
    for (std::size_t i = 1; i < 37; ++i) {
        const auto c = lease.guid[i];
        if (i == 9 || i == 14 || i == 19 || i == 24) {
            if (c != L'-') return false;
        } else {
            if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F'))) return false;
            nonzero |= c != L'0';
        }
    }
    if (!nonzero || !lease.instance.starts_with(L"USB\\") || lease.instance.size() < 9 ||
        lease.instance.size() > 4096 ||
        std::any_of(lease.instance.begin(), lease.instance.end(), [](auto c) { return c < 32 || c == 127; }))
        return false;
    const auto dash = lease.bus.find('-');
    if (dash == lease.bus.npos || !dash || dash + 1 == lease.bus.size() || lease.bus.size() >= 16 ||
        lease.bus.find('-', dash + 1) != lease.bus.npos)
        return false;
    if (lease.bus[0] == '0' || lease.bus[dash + 1] == '0') return false;
    return std::all_of(lease.bus.begin(), lease.bus.end(), [](char c) { return (c >= '0' && c <= '9') || c == '-'; });
}
bool generic_stub_matches(const GenericLease& lease, const std::wstring& instance, const std::wstring& parent,
                          unsigned port, const std::wstring& service) {
    if (!valid_generic_lease(lease) || lease.parent.empty()) return false;
    const auto same = [](const std::wstring& a, const std::wstring& b) {
        return a.size() == b.size() &&
               std::equal(a.begin(), a.end(), b.begin(), [](auto x, auto y) { return towupper(x) == towupper(y); });
    };
    const auto split = lease.instance.rfind(L'\\');
    if (split == lease.instance.npos) return false;
    return same(instance, L"USB\\VID_80EE&PID_CAFE" + lease.instance.substr(split)) && same(parent, lease.parent) &&
           port == lease.port && same(service, L"VBoxUSB");
}
GenericLeaseAction generic_lease_action(const GenericLease& lease, const std::wstring& actual, bool exists, bool active,
                                        bool forced, bool foreign) {
    if (!valid_generic_lease(lease) || active) return GenericLeaseAction::refuse;
    if (!exists) return GenericLeaseAction::retire_only;
    const bool same =
        actual.size() == lease.instance.size() && std::equal(actual.begin(), actual.end(), lease.instance.begin(),
                                                             [](auto a, auto b) { return towupper(a) == towupper(b); });
    if (!actual.empty() && !same) return GenericLeaseAction::retire_only;
    if (forced || foreign) return GenericLeaseAction::refuse;
    return GenericLeaseAction::release_binding;
}
bool import_port_absent(unsigned exit_code, std::string_view output) {
    while (!output.empty() && (output.back() == '\r' || output.back() == '\n')) output.remove_suffix(1);
    return exit_code == 1 && output == "error: requested port(s) not found";
}
bool matches_generic_import_port(std::string_view output, unsigned port, std::uint16_t local, const std::string& bus,
                                 bool active) {
    if (!port || port > 255 || !local || bus.empty() || output.size() > 1024 * 1024) return false;
    const auto heading = "Port " + (port < 10 ? std::string("0") : std::string{}) + std::to_string(port) + ":";
    const auto uri = "-> usbip://127.0.0.1:" + std::to_string(local) + "/" + bus;
    std::istringstream lines{std::string(output)};
    std::string line;
    unsigned headers = 0, uris = 0;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto begin = line.find_first_not_of(' ');
        if (begin != line.npos) line.erase(0, begin);
        if (line.starts_with("Port ")) {
            if (!line.starts_with(heading + (active ? " device in use at " : " ")) || line.size() > 256) return false;
            ++headers;
        }
        if (line.starts_with("-> usbip://")) {
            if (line != uri) return false;
            ++uris;
        }
    }
    return headers == 1 && uris == 1;
}
bool matches_import_port(std::string_view output, unsigned port, std::uint16_t local, const std::string& bus,
                         const std::string& serial, bool active) {
    if (!port || port > 255 || !local || bus.empty() || serial.empty() || output.size() > 1024 * 1024) return false;
    const auto heading = "Port " + (port < 10 ? std::string("0") : std::string{}) + std::to_string(port) + ":";
    const auto uri = "-> usbip://127.0.0.1:" + std::to_string(local) + "/" + bus;
    const auto identity = "-> serial: " + serial;
    std::istringstream lines{std::string(output)};
    std::string line;
    unsigned headers = 0, uris = 0, serials = 0;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto begin = line.find_first_not_of(' ');
        if (begin != line.npos) line.erase(0, begin);
        if (line.starts_with("Port ")) {
            if (!line.starts_with(heading + (active ? " device in use at " : " ")) || line.size() > 256) return false;
            ++headers;
        }
        if (line.starts_with("-> usbip://")) {
            if (line != uri) return false;
            ++uris;
        }
        if (line.starts_with("-> serial:")) {
            if (line != identity) return false;
            ++serials;
        }
    }
    return headers == 1 && uris == 1 && serials == 1;
}
extrapane::Availability live_interrupted_return_policy(const RecoveryRecord& record) {
    if (record.version != 2 || (record.state != RecoveryState::prepared && record.state != RecoveryState::offline &&
                                record.state != RecoveryState::binding_intent && record.state != RecoveryState::bound))
        return {false, "Live interrupted return requires this version 2 pre-export or bound generation"};
    try {
        (void)encode_recovery(record);
    } catch (const std::exception& e) {
        return {false, e.what()};
    }
    return {true, {}};
}
extrapane::Availability absent_release_policy(const RecoveryRecord& record) {
    if (record.version != 2 || record.state != RecoveryState::bound || record.binding_id.empty() ||
        record.bus_id.empty())
        return {false, "Absent release requires an exact version 2 bound journal"};
    try {
        (void)encode_recovery(record);
    } catch (const std::exception& e) {
        return {false, e.what()};
    }
    return {true, {}};
}
extrapane::Availability interrupted_return_policy(const RecoveryRecord& record) {
    if (record.version != 2 || record.state != RecoveryState::bound || (record.prior_attributes & 1) ||
        record.bus_id.empty() || record.binding_id.empty())
        return {false, "Interrupted return requires a version 2 bound record for an originally online disk"};
    // Exercise the same bounded codec validation used for protected records.
    try {
        (void)encode_recovery(record);
    } catch (const std::exception& e) {
        return {false, e.what()};
    }
    return {true, {}};
}
extrapane::Availability ejected_binding_adoption_policy(const PendingRecovery& selected, const PendingRecovery& current,
                                                        const HostDevice& device, const std::wstring& reserved) {
    auto same = [](const std::wstring& a, const std::wstring& b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
                                                  [](auto x, auto y) { return std::towupper(x) == std::towupper(y); });
    };
    if (!selected.record || !current.record || !selected.error.empty() || !current.error.empty() ||
        selected.journal_id.empty() || selected.journal_id != current.journal_id ||
        *selected.record != *current.record || selected.record->state != RecoveryState::ejected)
        return {false, "Binding adoption requires the unchanged protected destination-eject journal"};
    const auto& record = *current.record;
    std::wstring binding(record.binding_id.begin(), record.binding_id.end());
    if (reserved.empty() || !same(device.instance_id, reserved) || device.attached || device.forced ||
        record.bus_id.empty() || record.binding_id.empty() || device.bus_id != record.bus_id ||
        !same(device.binding_id, binding))
        return {false, "The reserved source and current unoccupied binding do not match the eject journal"};
    return {true, {}};
}
extrapane::Availability interrupted_binding_adoption_policy(const PendingRecovery& selected,
                                                            const PendingRecovery& current, const HostDevice& device,
                                                            const std::wstring& reserved, bool acknowledged) {
    if (!acknowledged || !selected.record || !current.record || !selected.error.empty() || !current.error.empty() ||
        selected.journal_id.empty() || selected.journal_id != current.journal_id || *selected.record != *current.record)
        return {
            false,
            "Interrupted binding adoption requires the unchanged protected record and explicit durable acknowledgment"};
    const auto policy = interrupted_return_policy(*current.record);
    if (!policy.available) return policy;
    const auto& record = *current.record;
    const std::wstring binding(record.binding_id.begin(), record.binding_id.end());
    auto same = [](const std::wstring& a, const std::wstring& b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
                                                  [](auto x, auto y) { return std::towupper(x) == std::towupper(y); });
    };
    if (reserved.empty() || !same(device.instance_id, reserved) || device.attached || device.forced ||
        device.bus_id != record.bus_id || !same(device.binding_id, binding))
        return {false, "Interrupted source binding is attached, ambiguous or differs from the acknowledged record"};
    return {true, {}};
}
namespace detail {
std::vector<std::uint8_t> partition_metadata(const SectorReader& read, std::uint32_t sector, std::uint64_t size) {
    auto need = [](bool ok) {
        if (!ok) throw std::invalid_argument("USB partition metadata is unsupported, corrupt or changed");
    };
    need(read && sector >= 512 && sector <= 4096 && (sector & (sector - 1)) == 0 && size >= sector * 4ull &&
         size % sector == 0);
    auto le = [&](std::span<const std::uint8_t> b, std::size_t off, unsigned n) {
        need(off <= b.size() && n <= b.size() - off);
        std::uint64_t value = 0;
        for (unsigned i = 0; i < n; ++i) value |= std::uint64_t(b[off + i]) << (8 * i);
        return value;
    };
    auto crc = [](std::span<const std::uint8_t> b) {
        std::uint32_t value = ~0u;
        for (auto c : b) {
            value ^= c;
            for (unsigned i = 0; i < 8; ++i)
                value = (value >> 1) ^ (0xedb88320u & static_cast<std::uint32_t>(-static_cast<int>(value & 1)));
        }
        return ~value;
    };
    auto fetch = [&](std::uint64_t offset, std::uint32_t count) {
        need(count && count <= 1024 * 1024 && offset <= size && count <= size - offset && offset % sector == 0 &&
             count % sector == 0);
        auto b = read(offset, count);
        need(b.size() == count);
        return b;
    };
    const auto mbr = fetch(0, sector);
    need(mbr[510] == 0x55 && mbr[511] == 0xaa);
    unsigned count = 0, protective = 0;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
    for (unsigned i = 0; i < 4; ++i) {
        const auto p = 446 + 16 * i;
        const auto type = mbr[p + 4];
        const auto start = le(mbr, p + 8, 4), length = le(mbr, p + 12, 4);
        if (!type) {
            need(!start && !length);
            continue;
        }
        ++count;
        need(start && length && start < size / sector && length <= size / sector - start);
        need(type != 0x05 && type != 0x0f && type != 0x85); // Extended chains need their own bounded parser.
        if (type == 0xee) ++protective;
        ranges.emplace_back(start, start + length);
    }
    need(count > 0); // Never fingerprint writable superfloppy filesystem bytes.
    std::sort(ranges.begin(), ranges.end());
    for (std::size_t i = 1; i < ranges.size(); ++i) need(ranges[i - 1].second <= ranges[i].first);
    std::vector<std::uint8_t> result(mbr.begin() + 440, mbr.begin() + 512);
    if (!protective) return result;
    need(protective == 1 && count == 1);
    auto primary = fetch(sector, sector);
    need(std::equal(primary.begin(), primary.begin() + 8, "EFI PART"));
    const auto headerSize = le(primary, 12, 4);
    need(headerSize >= 92 && headerSize <= sector && le(primary, 24, 8) == 1 &&
         le(primary, 32, 8) == size / sector - 1);
    auto validateHeader = [&](std::vector<std::uint8_t> b) {
        const auto length = le(b, 12, 4);
        need(length >= 92 && length <= sector);
        const auto expected = le(b, 16, 4);
        std::fill_n(b.begin() + 16, 4, 0);
        need(crc(std::span(b).first(static_cast<std::size_t>(length))) == expected);
    };
    validateHeader(primary);
    const auto entries = le(primary, 80, 4), entrySize = le(primary, 84, 4), lba = le(primary, 72, 8);
    need(entries > 0 && entries <= 4096 && entrySize >= 128 && entrySize <= 1024 && entrySize % 128 == 0 &&
         entries * entrySize <= 1024 * 1024 - 8192);
    const auto bytes = entries * entrySize, rounded = ((bytes + sector - 1) / sector) * sector;
    need(lba >= 2 && lba < size / sector && rounded <= size - lba * sector);
    auto table = fetch(lba * sector, static_cast<std::uint32_t>(rounded));
    need(crc(std::span(table).first(static_cast<std::size_t>(bytes))) == le(primary, 88, 4));
    const auto first = le(primary, 40, 8), last = le(primary, 48, 8);
    need(first >= lba + rounded / sector && first <= last && last < size / sector - 1);
    ranges.clear();
    for (std::uint64_t i = 0; i < entries; ++i) {
        const auto offset = static_cast<std::size_t>(i * entrySize);
        if (std::all_of(table.begin() + offset, table.begin() + offset + 16, [](auto b) { return b == 0; })) continue;
        const auto start = le(table, offset + 32, 8), end = le(table, offset + 40, 8);
        need(start >= first && end >= start && end <= last);
        ranges.emplace_back(start, end + 1);
    }
    need(!ranges.empty());
    std::sort(ranges.begin(), ranges.end());
    for (std::size_t i = 1; i < ranges.size(); ++i) need(ranges[i - 1].second <= ranges[i].first);
    auto backup = fetch(size - sector, sector);
    validateHeader(backup);
    need(std::equal(backup.begin(), backup.begin() + 8, "EFI PART") && le(backup, 24, 8) == size / sector - 1 &&
         le(backup, 32, 8) == 1);
    need(le(backup, 40, 8) == first && le(backup, 48, 8) == last &&
         std::equal(primary.begin() + 56, primary.begin() + 72, backup.begin() + 56) && le(backup, 80, 4) == entries &&
         le(backup, 84, 4) == entrySize && le(backup, 88, 4) == le(primary, 88, 4));
    const auto backupLba = le(backup, 72, 8);
    need(backupLba > last && backupLba < size / sector && rounded <= size - sector - backupLba * sector);
    auto backupTable = fetch(backupLba * sector, static_cast<std::uint32_t>(rounded));
    need(std::equal(table.begin(), table.begin() + static_cast<std::size_t>(bytes), backupTable.begin()));
    result.insert(result.end(), primary.begin(), primary.begin() + static_cast<std::size_t>(headerSize));
    result.insert(result.end(), table.begin(), table.begin() + static_cast<std::size_t>(bytes));
    return result;
}
}
namespace {
void valid(const RecoveryRecord& r) {
    const auto state = static_cast<unsigned>(r.state);
    if ((r.version != 1 && r.version != 2) || state < 1 || state > 7 || r.disk_number == 0xffffffffu ||
        r.bus_id.size() > 64 || r.binding_id.size() > 38 ||
        std::all_of(r.fingerprint.begin(), r.fingerprint.end(), [](auto b) { return b == 0; }) ||
        std::all_of(r.owner.begin(), r.owner.end(), [](auto b) { return b == 0; }))
        throw std::invalid_argument("Invalid USB recovery journal");
    if (!r.bus_id.empty()) {
        bool dash = false;
        for (char c : r.bus_id) {
            if (c == '-' && !dash) {
                dash = true;
                continue;
            }
            if (c < '0' || c > '9') throw std::invalid_argument("Invalid journal bus ID");
        }
        if (!dash || r.bus_id.front() == '-' || r.bus_id.back() == '-')
            throw std::invalid_argument("Invalid journal bus ID");
    }
    if (!r.binding_id.empty()) {
        std::string_view guid = r.binding_id;
        if (guid.size() == 38 && guid.front() == '{' && guid.back() == '}') guid = guid.substr(1, 36);
        if (guid.size() != 36) throw std::invalid_argument("Invalid journal binding ID");
        for (std::size_t i = 0; i < 36; ++i) {
            const char c = guid[i];
            if (i == 8 || i == 13 || i == 18 || i == 23) {
                if (c != '-') throw std::invalid_argument("Invalid journal binding ID");
            } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
                throw std::invalid_argument("Invalid journal binding ID");
        }
    }
    if ((r.state == RecoveryState::prepared || r.state == RecoveryState::offline) &&
        (!r.bus_id.empty() || !r.binding_id.empty()))
        throw std::invalid_argument("Unexpected USB journal binding");
    if (r.state == RecoveryState::binding_intent && (r.bus_id.empty() || !r.binding_id.empty()))
        throw std::invalid_argument("Incomplete USB binding intent");
    if ((r.state == RecoveryState::bound || r.state == RecoveryState::released || r.state == RecoveryState::ejected) &&
        (r.bus_id.empty() || r.binding_id.empty()))
        throw std::invalid_argument("Incomplete USB journal binding");
}
void put(std::span<std::uint8_t> b, unsigned o, std::uint64_t v, unsigned n) {
    for (unsigned i = 0; i < n; ++i) b[o + n - 1 - i] = static_cast<std::uint8_t>(v >> (8 * i));
}
std::uint64_t get(std::span<const std::uint8_t> b, unsigned o, unsigned n) {
    std::uint64_t v = 0;
    for (unsigned i = 0; i < n; ++i) v = (v << 8) | b[o + i];
    return v;
}
}
bool valid_recovery_transition(RecoveryState from, RecoveryState to) noexcept {
    return (from == RecoveryState::prepared && to == RecoveryState::offline) ||
           (from == RecoveryState::offline && to == RecoveryState::binding_intent) ||
           (from == RecoveryState::binding_intent && to == RecoveryState::bound) ||
           (from == RecoveryState::bound && to == RecoveryState::ejected) ||
           (from == RecoveryState::ejected && to == RecoveryState::released) ||
           ((from == RecoveryState::prepared || from == RecoveryState::offline || from == RecoveryState::released ||
             from == RecoveryState::restored) &&
            to == RecoveryState::restored);
}
std::vector<std::uint8_t> encode_recovery(const RecoveryRecord& r) {
    valid(r);
    std::vector<std::uint8_t> b(72 + r.bus_id.size() + r.binding_id.size());
    b[0] = 'E';
    b[1] = 'P';
    b[2] = 'U';
    b[3] = 'R';
    b[5] = r.version;
    b[6] = static_cast<std::uint8_t>(r.state);
    put(b, 8, r.disk_number, 4);
    put(b, 12, r.prior_attributes, 8);
    std::copy(r.fingerprint.begin(), r.fingerprint.end(), b.begin() + 20);
    std::copy(r.owner.begin(), r.owner.end(), b.begin() + 52);
    b[68] = static_cast<std::uint8_t>(r.bus_id.size());
    b[69] = static_cast<std::uint8_t>(r.binding_id.size());
    std::copy(r.bus_id.begin(), r.bus_id.end(), b.begin() + 72);
    std::copy(r.binding_id.begin(), r.binding_id.end(), b.begin() + 72 + r.bus_id.size());
    return b;
}
RecoveryRecord decode_recovery(std::span<const std::uint8_t> b) {
    if (b.size() < 72 || b.size() > 174 || b[0] != 'E' || b[1] != 'P' || b[2] != 'U' || b[3] != 'R' || b[4] ||
        (b[5] != 1 && b[5] != 2) || b[7] || b[70] || b[71] || b.size() != 72u + b[68] + b[69])
        throw std::invalid_argument("Malformed USB recovery journal");
    RecoveryRecord r;
    r.version = b[5];
    r.state = static_cast<RecoveryState>(b[6]);
    r.disk_number = static_cast<std::uint32_t>(get(b, 8, 4));
    r.prior_attributes = get(b, 12, 8);
    std::copy_n(b.begin() + 20, 32, r.fingerprint.begin());
    std::copy_n(b.begin() + 52, 16, r.owner.begin());
    r.bus_id.assign(b.begin() + 72, b.begin() + 72 + b[68]);
    r.binding_id.assign(b.begin() + 72 + b[68], b.end());
    valid(r);
    return r;
}
}
