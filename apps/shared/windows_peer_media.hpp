#pragma once
#include <extrapane/peer_rpc.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace extrapane::windows_peer {
struct Command {
    std::string executable;
    std::vector<std::string> arguments;
    bool elevated = false, graceful = true, parent_watch = true;
};
std::vector<std::string> native_arguments(const Command&, std::uint32_t parent_pid);
// All methods run on an owned worker thread, never on the UI thread. Native stop
// can wait for USB recovery; its existing exact-device cleanup must finish.
class Process {
public:
    virtual ~Process() = default;
    virtual void start(const Command&) = 0;
    virtual void write(const std::string&) = 0;
    virtual std::string drain() = 0;
    virtual bool running() const = 0;
    virtual void stop() noexcept = 0;
};
using Factory = std::function<std::unique_ptr<Process>()>;
#ifdef _WIN32
Factory native_process_factory();
#endif
struct Source {
    peers::Offer offer;
    // Local-only UTF-8 device identifier; never accepted from a remote request.
    std::string device;
    // Empty uses the saved per-user renderer preference at session start.
    std::string renderer;
    bool mirror = false;
};
struct Event {
    std::uint64_t grant = 0;
    bool active = false, ended = false;
    std::string detail;
};
class WindowsPeerMediaAdapter {
    struct Impl;
    std::unique_ptr<Impl> impl_;

public:
    explicit WindowsPeerMediaAdapter(Factory);
    ~WindowsPeerMediaAdapter();
    // Replacing offers stops grants whose source is removed or changed.
    void sources(std::vector<Source>);
    bool start(const peers::Grant&, const peers::rpc::Open&, std::uint16_t bridge_port, std::string credential,
               std::string& reason);
    void stop(std::uint64_t grant) noexcept;
    void stop_all() noexcept;
    std::vector<Event> poll();
};
struct Admission {
    std::uint64_t lease = 0;
    std::string credential;
};
class WindowsPeerReceiver {
    struct Impl;
    std::unique_ptr<Impl> impl_;

public:
    explicit WindowsPeerReceiver(Factory);
    ~WindowsPeerReceiver();
    // Explicit user demand only. Starts installed components; never installs them.
    // Must run on worker; deadline bounds status waits (UAC itself is interactive).
    void start(std::uint16_t local_port, std::chrono::steady_clock::time_point deadline);
    // Secret return value goes only over authenticated encrypted peer RPC. Never
    // send the local six-digit broker code or expose it in UI/logs.
    Admission issue(peers::Kind, bool input, std::chrono::steady_clock::time_point deadline);
    void release(std::uint64_t lease) noexcept;
    // Drain native status regularly; returned leases lost admission control and
    // their media bridges must be closed by the owner.
    std::vector<std::uint64_t> poll();
    std::string failure(std::uint64_t lease) const;
    bool running() const;
    std::uint16_t port() const;
    void stop() noexcept;
};
}
