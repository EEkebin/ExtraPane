#include "peer_discovery.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <unistd.h>
#include <fcntl.h>
#include <chrono>
#include <stdexcept>
#include <array>
#include <algorithm>
namespace dm::mac {
using namespace extrapane::peers;
namespace {
Milliseconds now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
in_addr group() {
    in_addr a{};
    inet_pton(AF_INET, "239.255.43.24", &a);
    return a;
}
}
struct PeerDiscovery::State {
    struct Interface {
        unsigned index;
        in_addr address, mask;
    };
    int socket = -1;
    std::vector<Interface> interfaces;
    DiscoveryTable table;
    std::optional<Announcement> announcement;
    Milliseconds sent = 0;
    void send(const Bytes& bytes) {
        if (socket < 0) return;
        sockaddr_in target{};
        target.sin_family = AF_INET;
        target.sin_port = htons(43825);
        target.sin_addr = group();
        for (const auto& nic : interfaces) {
            if (setsockopt(socket, IPPROTO_IP, IP_MULTICAST_IF, &nic.address, sizeof(nic.address))) continue;
            sendto(socket, bytes.data(), bytes.size(), 0, reinterpret_cast<sockaddr*>(&target), sizeof(target));
        }
    }
};
PeerDiscovery::PeerDiscovery() : state_(std::make_unique<State>()) {}
PeerDiscovery::~PeerDiscovery() {
    stop();
}
void PeerDiscovery::start(const std::string& local) {
    stop();
    auto& s = *state_;
    ifaddrs* entries = nullptr;
    if (getifaddrs(&entries)) throw std::runtime_error("Cannot list network interfaces");
    for (auto* entry = entries; entry; entry = entry->ifa_next) {
        if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET || !entry->ifa_netmask ||
            (entry->ifa_flags & IFF_LOOPBACK) || !(entry->ifa_flags & IFF_UP) || !(entry->ifa_flags & IFF_RUNNING) ||
            !(entry->ifa_flags & IFF_MULTICAST))
            continue;
        auto address = reinterpret_cast<sockaddr_in*>(entry->ifa_addr)->sin_addr;
        char text[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &address, text, sizeof(text));
        if (!local.empty() && local != text) continue;
        s.interfaces.push_back(
            {if_nametoindex(entry->ifa_name), address, reinterpret_cast<sockaddr_in*>(entry->ifa_netmask)->sin_addr});
    }
    freeifaddrs(entries);
    if (s.interfaces.empty()) return;
    s.socket = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (s.socket < 0) throw std::runtime_error("Cannot start LAN discovery");
    int yes = 1;
    setsockopt(s.socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    setsockopt(s.socket, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
    if (fcntl(s.socket, F_SETFL, O_NONBLOCK) < 0 || fcntl(s.socket, F_SETFD, FD_CLOEXEC) < 0) {
        stop();
        throw std::runtime_error("Cannot configure nonblocking LAN discovery");
    }
    unsigned char ttl = 1, loop = 0;
    setsockopt(s.socket, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    setsockopt(s.socket, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
    sockaddr_in bind{};
    bind.sin_family = AF_INET;
    bind.sin_port = htons(43825);
    bind.sin_addr.s_addr = INADDR_ANY;
    if (::bind(s.socket, reinterpret_cast<sockaddr*>(&bind), sizeof(bind))) {
        stop();
        throw std::runtime_error("LAN discovery port is unavailable; connect by address instead");
    }
    std::set<unsigned> joined;
    std::vector<State::Interface> active;
    for (const auto& nic : s.interfaces) {
        ip_mreq request{};
        request.imr_multiaddr = group();
        request.imr_interface = nic.address;
        if (!setsockopt(s.socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &request, sizeof(request))) {
            joined.insert(nic.index);
            active.push_back(nic);
        }
    }
    s.interfaces = std::move(active);
    s.table.interfaces(std::move(joined));
}
void PeerDiscovery::advertise(Announcement announcement) {
    auto& s = *state_;
    encode(announcement);
    if (s.announcement && s.announcement->device == announcement.device &&
        s.announcement->instance == announcement.instance) {
        if (s.announcement->sequence == UINT64_MAX)
            throw std::runtime_error("Discovery sequence exhausted; restart sharing");
        announcement.sequence = std::max(announcement.sequence, s.announcement->sequence + 1);
    } else
        withdraw();
    s.announcement = std::move(announcement);
    s.sent = 0;
}
void PeerDiscovery::withdraw() {
    auto& s = *state_;
    if (s.announcement) {
        const auto& a = *s.announcement;
        if (a.sequence < UINT64_MAX) s.send(encode(Withdrawal{a.device, a.instance, a.sequence + 1}));
        s.announcement.reset();
    }
}
void PeerDiscovery::stop() {
    withdraw();
    auto& s = *state_;
    if (s.socket >= 0) close(s.socket);
    s.socket = -1;
    s.interfaces.clear();
    s.table.clear();
}
std::vector<Route> PeerDiscovery::poll() {
    auto& s = *state_;
    const auto time = now();
    if (s.socket < 0) return {};
    if (s.announcement && time - s.sent >= 5000) {
        if (s.announcement->sequence == UINT64_MAX)
            withdraw();
        else {
            s.announcement->sequence++;
            s.send(encode(*s.announcement));
            s.sent = time;
        }
    }
    for (unsigned i = 0; i < 64; ++i) {
        std::array<std::uint8_t, 1200> bytes{};
        sockaddr_in peer{};
        socklen_t size = sizeof(peer);
        const auto count = recvfrom(s.socket, bytes.data(), bytes.size(), 0, reinterpret_cast<sockaddr*>(&peer), &size);
        if (count < 0) break;
        if (count < 8 || count > 512) continue;
        unsigned index = 0;
        bool ambiguous = false;
        for (const auto& nic : s.interfaces)
            if ((peer.sin_addr.s_addr & nic.mask.s_addr) == (nic.address.s_addr & nic.mask.s_addr)) {
                if (index && index != nic.index) ambiguous = true;
                index = nic.index;
            }
        if (!index || ambiguous) continue;
        char address[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &peer.sin_addr, address, sizeof(address));
        auto payload = std::span(bytes).first(static_cast<std::size_t>(count));
        try {
            auto announcement = decode_announcement(payload);
            if (!s.announcement || announcement.device != s.announcement->device)
                s.table.observe(announcement, address, index, time);
        } catch (...) {
            try {
                s.table.withdraw(decode_withdrawal(payload), address, index);
            } catch (...) {}
        }
    }
    return s.table.snapshot(time);
}
}
