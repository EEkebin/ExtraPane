#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <bcrypt.h>

#include <dm/network.hpp>
#include <dm/probe.hpp>
#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <limits>

namespace dm::net {
namespace {
using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;

[[noreturn]] void fail(const char* operation, int code = WSAGetLastError()) {
    throw std::runtime_error(std::string(operation) + " failed (Windows error " + std::to_string(code) + ")");
}

struct Winsock {
    Winsock() {
        WSADATA data{};
        const int result = WSAStartup(MAKEWORD(2, 2), &data);
        if (result != 0) fail("WSAStartup", result);
    }
    ~Winsock() { WSACleanup(); }
    Winsock(const Winsock&) = delete;
    Winsock& operator=(const Winsock&) = delete;
};

class Socket {
public:
    explicit Socket(SOCKET handle) : handle_(handle) {
        if (handle_ == INVALID_SOCKET) fail("socket/accept");
    }
    ~Socket() { if (handle_ != INVALID_SOCKET) closesocket(handle_); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept : handle_(std::exchange(other.handle_, INVALID_SOCKET)) {}
    SOCKET get() const { return handle_; }
private:
    SOCKET handle_;
};

void nonblocking(const Socket& socket) {
    u_long enabled = 1;
    if (ioctlsocket(socket.get(), FIONBIO, &enabled) == SOCKET_ERROR) fail("nonblocking socket");
}

void wait_ready(const Socket& socket, bool writing, Deadline deadline, const char* stage) {
    const bool infinite=deadline==Deadline::max();
    const auto left = std::chrono::duration_cast<std::chrono::microseconds>((infinite?Clock::now()+std::chrono::seconds(1):deadline) - Clock::now());
    if (left.count() <= 0) throw std::runtime_error(std::string(stage) + " timed out");
    timeval timeout{static_cast<long>(left.count() / 1000000), static_cast<long>(left.count() % 1000000)};
    fd_set ready, errors;
    FD_ZERO(&ready);
    FD_ZERO(&errors);
    FD_SET(socket.get(), &ready);
    FD_SET(socket.get(), &errors);
    const int result = select(0, writing ? nullptr : &ready, writing ? &ready : nullptr, &errors, infinite?nullptr:&timeout);
    if (result == SOCKET_ERROR) fail(stage);
    if (result == 0) throw std::runtime_error(std::string(stage) + " timed out");
    if (FD_ISSET(socket.get(), &errors)) {
        int code = 0;
        int size = sizeof(code);
        if (getsockopt(socket.get(), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&code), &size) == SOCKET_ERROR) fail("SO_ERROR");
        fail(stage, code);
    }
}

sockaddr_in address(const std::string& text, std::uint16_t port) {
    sockaddr_in value{};
    value.sin_family = AF_INET;
    value.sin_port = htons(port);
    if (InetPtonA(AF_INET, _stricmp(text.c_str(),"localhost")==0?"127.0.0.1":text.c_str(), &value.sin_addr) != 1) {
        throw std::invalid_argument("Expected an IPv4 literal: " + text);
    }
    const auto host = ntohl(value.sin_addr.s_addr);
    if ((host >> 24) == 0 || (host >> 24) >= 224) {
        throw std::invalid_argument("A unicast IPv4 address is required: " + text);
    }
    return value;
}

std::string ip_text(const sockaddr_in& value) {
    std::array<char, INET_ADDRSTRLEN> text{};
    if (!InetNtopA(AF_INET, &value.sin_addr, text.data(), text.size())) fail("InetNtop");
    return text.data();
}

std::string endpoint(const Socket& socket, bool peer) {
    sockaddr_in value{};
    int length = sizeof(value);
    const int result = peer ? getpeername(socket.get(), reinterpret_cast<sockaddr*>(&value), &length)
                            : getsockname(socket.get(), reinterpret_cast<sockaddr*>(&value), &length);
    if (result == SOCKET_ERROR) fail("read socket endpoint");
    return ip_text(value) + ':' + std::to_string(ntohs(value.sin_port));
}

Interface find_interface(const std::string& text) {
    const auto target = address(text, 0);
    for (const auto& adapter : interfaces()) {
        if (address(adapter.address, 0).sin_addr.s_addr == target.sin_addr.s_addr && adapter.up) return adapter;
    }
    throw std::invalid_argument("Bind address is not assigned to an active local interface: " + text);
}

void constrain_outgoing_interface(const Socket& socket, const Interface& adapter) {
    const DWORD index = htonl(adapter.index);
    if (setsockopt(socket.get(), IPPROTO_IP, IP_UNICAST_IF, reinterpret_cast<const char*>(&index), sizeof(index)) == SOCKET_ERROR) fail("select outgoing interface");
}

Socket bound_socket(const Options& options, const Interface& adapter) {
    Socket socket(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    const BOOL exclusive = TRUE;
    if (setsockopt(socket.get(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) == SOCKET_ERROR) fail("exclusive bind");
    constrain_outgoing_interface(socket, adapter);
    const BOOL no_delay=TRUE;
    if (setsockopt(socket.get(),IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<const char*>(&no_delay),sizeof(no_delay))==SOCKET_ERROR) fail("disable TCP Nagle buffering");
    const auto local = address(options.bind_address, options.peer.empty() ? options.port : 0);
    if (bind(socket.get(), reinterpret_cast<const sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR) fail("bind");
    nonblocking(socket);
    return socket;
}

void send_packet(const Socket& socket, const probe::Packet& packet, Deadline deadline) {
    std::size_t sent = 0;
    while (sent < packet.size()) {
        wait_ready(socket, true, deadline, "diagnostic send");
        const int count = send(socket.get(), reinterpret_cast<const char*>(packet.data() + sent), static_cast<int>(packet.size() - sent), 0);
        if (count == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAEWOULDBLOCK) continue;
            fail("diagnostic send");
        }
        if (count == 0) throw std::runtime_error("Peer closed during diagnostic send");
        sent += static_cast<std::size_t>(count);
    }
}

probe::Message receive_packet(const Socket& socket, Deadline deadline) {
    probe::Packet packet{};
    std::size_t received = 0;
    while (received < packet.size()) {
        wait_ready(socket, false, deadline, "diagnostic receive");
        const int count = recv(socket.get(), reinterpret_cast<char*>(packet.data() + received), static_cast<int>(packet.size() - received), 0);
        if (count == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAEWOULDBLOCK) continue;
            fail("diagnostic receive");
        }
        if (count == 0) throw std::runtime_error("Peer closed before the full diagnostic packet arrived");
        received += static_cast<std::size_t>(count);
    }
    return probe::decode(packet);
}

std::string utf8(const wchar_t* value) {
    if (!value) return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
    if (count <= 0) fail("adapter name conversion", static_cast<int>(GetLastError()));
    std::string result(static_cast<std::size_t>(count), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, result.data(), count, nullptr, nullptr)) fail("adapter name conversion", static_cast<int>(GetLastError()));
    result.pop_back();
    return result;
}
}

std::vector<Interface> interfaces() {
    Winsock runtime;
    ULONG size = 15000;
    for (int attempt = 0; attempt < 4; ++attempt) {
        // Global operator new provides alignment suitable for the adapter structures.
        const auto buffer = std::make_unique<std::byte[]>(size);
        auto* first = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.get());
        const ULONG result = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, first, &size);
        if (result == ERROR_BUFFER_OVERFLOW) continue;
        if (result == ERROR_NO_DATA) return {};
        if (result != NO_ERROR) fail("enumerate adapters", static_cast<int>(result));
        std::vector<Interface> output;
        for (auto* adapter = first; adapter; adapter = adapter->Next) {
            for (auto* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
                if (!unicast->Address.lpSockaddr || unicast->Address.lpSockaddr->sa_family != AF_INET) continue;
                const auto* local = reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr);
                output.push_back({adapter->IfIndex, utf8(adapter->FriendlyName), ip_text(*local), unicast->OnLinkPrefixLength, adapter->OperStatus == IfOperStatusUp});
            }
        }
        return output;
    }
    throw std::runtime_error("Network adapters kept changing; retry enumeration");
}

std::vector<LocalAddress> local_addresses() {
    Winsock runtime;ULONG size=16384;std::vector<unsigned char> buffer(size);
    for(unsigned attempt=0;attempt<4;++attempt) {
        auto* first=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        const auto result=GetAdaptersAddresses(AF_UNSPEC,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,nullptr,first,&size);
        if(result==ERROR_BUFFER_OVERFLOW){buffer.resize(size);continue;}
        if(result!=NO_ERROR)throw std::runtime_error("Cannot enumerate network addresses");
        std::vector<LocalAddress> output;
        for(auto* adapter=first;adapter;adapter=adapter->Next) {
            if(adapter->OperStatus!=IfOperStatusUp || adapter->IfType==IF_TYPE_SOFTWARE_LOOPBACK || adapter->IfType==IF_TYPE_TUNNEL)continue;
            MIB_IF_ROW2 row{};row.InterfaceLuid=adapter->Luid;
            if(GetIfEntry2(&row)!=NO_ERROR || !row.InterfaceAndOperStatusFlags.HardwareInterface)continue;
            for(auto* entry=adapter->FirstUnicastAddress;entry;entry=entry->Next) {
            const auto* sock=entry->Address.lpSockaddr;if(!sock)continue;
            char text[INET6_ADDRSTRLEN]{};std::string address_text;
            if(sock->sa_family==AF_INET){const auto* v=reinterpret_cast<const sockaddr_in*>(sock);if(!InetNtopA(AF_INET,&v->sin_addr,text,sizeof(text)))continue;address_text=text;}
            else if(sock->sa_family==AF_INET6){const auto* v=reinterpret_cast<const sockaddr_in6*>(sock);if(!InetNtopA(AF_INET6,&v->sin6_addr,text,sizeof(text)))continue;address_text=text;if(v->sin6_scope_id)address_text+='%'+std::to_string(v->sin6_scope_id);}
            else continue;
            output.push_back({utf8(adapter->FriendlyName),address_text,adapter->OperStatus==IfOperStatusUp,sock->sa_family==AF_INET6});
            }
        }
        std::stable_sort(output.begin(),output.end(),[](const auto& a,const auto& b){
            if(a.up!=b.up)return a.up>b.up;
            if(a.ipv6!=b.ipv6)return !a.ipv6;
            const bool a_loop=a.address=="::1"||a.address.starts_with("127."),b_loop=b.address=="::1"||b.address.starts_with("127.");
            return a_loop!=b_loop && !a_loop;
        });return output;
    }
    throw std::runtime_error("Network addresses kept changing");
}
std::string route_source(const std::string& peer) {
    Winsock runtime;SOCKADDR_INET destination{},source{};destination.Ipv4=address(peer,0);MIB_IPFORWARD_ROW2 route{};
    if(GetBestRoute2(nullptr,0,nullptr,&destination,0,&route,&source)!=NO_ERROR || source.si_family!=AF_INET)throw std::runtime_error("No IPv4 route to the server");
    return ip_text(source.Ipv4);
}

void listen_once(const Options& options) {
    Winsock runtime;
    const auto adapter = find_interface(options.bind_address);
    auto listener = bound_socket(options, adapter);
    if (listen(listener.get(), 1) == SOCKET_ERROR) fail("listen");
    std::cout << "LISTENING " << endpoint(listener, false) << " interface=" << adapter.index
              << " diagnostic-only (not paired)" << std::endl;
    const auto deadline = Clock::now() + options.timeout;
    wait_ready(listener, false, deadline, "accept");
    Socket client(accept(listener.get(), nullptr, nullptr));
    nonblocking(client);
    constrain_outgoing_interface(client, adapter);
    const auto request = receive_packet(client, deadline);
    if (request.kind != probe::Kind::request) throw std::runtime_error("Expected a diagnostic request");
    send_packet(client, probe::encode({probe::Kind::response, request.token}), deadline);
    std::cout << "PROBE_OK local=" << endpoint(client, false) << " peer=" << endpoint(client, true)
              << " diagnostic-only (not paired)\n";
}

void probe_peer(const Options& options) {
    Winsock runtime;
    const auto remote = address(options.peer, options.port);
    const auto adapter = find_interface(options.bind_address);
    auto socket = bound_socket(options, adapter);
    const auto start = Clock::now();
    const auto deadline = start + options.timeout;
    if (connect(socket.get(), reinterpret_cast<const sockaddr*>(&remote), sizeof(remote)) == SOCKET_ERROR) {
        const int code = WSAGetLastError();
        if (code != WSAEWOULDBLOCK) fail("connect", code);
        wait_ready(socket, true, deadline, "connect");
        int error = 0;
        int size = sizeof(error);
        if (getsockopt(socket.get(), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &size) == SOCKET_ERROR) fail("connect status");
        if (error) fail("connect", error);
    }
    probe::Token token{};
    if (BCryptGenRandom(nullptr, token.data(), static_cast<ULONG>(token.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) throw std::runtime_error("System random generation failed");
    send_packet(socket, probe::encode({probe::Kind::request, token}), deadline);
    const auto response = receive_packet(socket, deadline);
    if (response.kind != probe::Kind::response || response.token != token) throw std::runtime_error("Diagnostic response did not match this probe");
    const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    std::cout << "PROBE_OK local=" << endpoint(socket, false) << " peer=" << endpoint(socket, true)
              << " interface=" << adapter.index << " connect_and_probe_ms=" << elapsed
              << " diagnostic-only (not paired)\n";
}

struct TcpStream::State {
    Winsock runtime;
    Socket socket;
    explicit State(Socket value) : socket(std::move(value)) {}
};
TcpStream::TcpStream(std::unique_ptr<State> state) : state_(std::move(state)) {}
TcpStream::~TcpStream() = default;
TcpStream::TcpStream(TcpStream&&) noexcept = default;
TcpStream& TcpStream::operator=(TcpStream&&) noexcept = default;
int TcpStream::read_some(std::span<std::uint8_t> bytes) {
    const auto size = static_cast<int>(std::min(bytes.size(), static_cast<std::size_t>(std::numeric_limits<int>::max())));
    const int count = recv(state_->socket.get(), reinterpret_cast<char*>(bytes.data()), size, 0);
    if (count == SOCKET_ERROR) {
        if (WSAGetLastError() == WSAEWOULDBLOCK) return -1;
        fail("stream receive");
    }
    return count;
}
int TcpStream::write_some(std::span<const std::uint8_t> bytes) {
    const auto size = static_cast<int>(std::min(bytes.size(), static_cast<std::size_t>(std::numeric_limits<int>::max())));
    const int count = send(state_->socket.get(), reinterpret_cast<const char*>(bytes.data()), size, 0);
    if (count == SOCKET_ERROR) {
        if (WSAGetLastError() == WSAEWOULDBLOCK) return -1;
        fail("stream send");
    }
    return count;
}
void TcpStream::wait(bool writing, Clock::time_point deadline) { wait_ready(state_->socket, writing, deadline, "secure stream"); }
void TcpStream::cancel() noexcept { if(state_)::shutdown(state_->socket.get(),SD_BOTH); }
std::string TcpStream::local_endpoint() const { return endpoint(state_->socket, false); }
std::string TcpStream::peer_endpoint() const { return endpoint(state_->socket, true); }

TcpStream connect_stream(const Options& requested) {
    auto options=requested;
    if(options.bind_address=="auto")options.bind_address=route_source(options.peer);
    Winsock runtime;
    if (options.peer.empty()) throw std::invalid_argument("A peer is required");
    const auto remote = address(options.peer, options.port);
    const auto adapter = find_interface(options.bind_address);
    auto socket = bound_socket(options, adapter);
    const auto deadline = Clock::now() + options.timeout;
    if (connect(socket.get(), reinterpret_cast<const sockaddr*>(&remote), sizeof(remote)) == SOCKET_ERROR) {
        const int code = WSAGetLastError();
        if (code != WSAEWOULDBLOCK) fail("connect", code);
        wait_ready(socket, true, deadline, "connect");
        int error = 0;
        int size = sizeof(error);
        if (getsockopt(socket.get(), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &size) == SOCKET_ERROR) fail("connect status");
        if (error) fail("connect", error);
    }
    return TcpStream(std::make_unique<TcpStream::State>(std::move(socket)));
}

struct TcpListener::State {
    Winsock runtime;
    std::vector<Socket> listeners;
    std::vector<Interface> adapters;
    std::string endpoints;
    std::chrono::milliseconds timeout;
};
TcpListener::TcpListener(const Options& options):state_(std::make_unique<State>()) {
    if(!options.peer.empty())throw std::invalid_argument("Listener cannot select a remote address");
    auto& s=*state_;s.timeout=options.timeout;
    const auto candidates=options.bind_address=="auto"?interfaces():std::vector<Interface>{find_interface(options.bind_address)};
    for(const auto& nic:candidates) {
        if(!nic.up || std::any_of(s.adapters.begin(),s.adapters.end(),[&](const auto& a){return a.address==nic.address;}))continue;
        if(s.listeners.size()>=FD_SETSIZE)throw std::runtime_error("Too many local IPv4 addresses");
        auto selected=options;selected.bind_address=nic.address;
        auto listener=bound_socket(selected,nic);
        if(listen(listener.get(),16)==SOCKET_ERROR)fail("listen");
        if(!s.endpoints.empty())s.endpoints+=", ";s.endpoints+=endpoint(listener,false);
        s.listeners.push_back(std::move(listener));s.adapters.push_back(nic);
    }
    if(s.listeners.empty())throw std::runtime_error("No active IPv4 interfaces");
}
TcpListener::~TcpListener()=default;
std::string TcpListener::endpoints() const{return state_->endpoints;}
TcpStream TcpListener::accept() {
    auto& s=*state_;fd_set set;FD_ZERO(&set);for(const auto& listener:s.listeners)FD_SET(listener.get(),&set);
    timeval timeout{static_cast<long>(s.timeout.count()/1000),static_cast<long>((s.timeout.count()%1000)*1000)};
    const auto count=select(0,&set,nullptr,nullptr,s.timeout.count()?&timeout:nullptr);
    if(count==SOCKET_ERROR)fail("accept");if(!count)throw std::runtime_error("Accept timed out");
    for(size_t i=0;i<s.listeners.size();++i)if(FD_ISSET(s.listeners[i].get(),&set)) {
        Socket client(::accept(s.listeners[i].get(),nullptr,nullptr));nonblocking(client);constrain_outgoing_interface(client,s.adapters[i]);
        return TcpStream(std::make_unique<TcpStream::State>(std::move(client)));
    }
    throw std::runtime_error("No ready listener");
}
TcpStream accept_stream(const Options& options,const std::function<void(const std::string&)>& ready) {
    TcpListener listener(options);ready(listener.endpoints());return listener.accept();
}
}
