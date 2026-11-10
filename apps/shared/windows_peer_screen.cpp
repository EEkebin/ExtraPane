#include "windows_peer_screen.hpp"
#include <algorithm>
#include <thread>
#include <stdexcept>
namespace extrapane::windows_peer {
namespace {
using Clock = std::chrono::steady_clock;
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void wipe(std::string& s) {
    volatile char* p = s.data();
    for (size_t i = 0; i < s.size(); ++i) p[i] = 0;
    s.clear();
}
struct Erase {
    std::string& s;
    ~Erase() { wipe(s); }
};
bool ticket(const std::string& value) {
    if (value.size() != 105 || value.substr(0, 8) != "EPCT1:0:" || value[40] != ':') return false;
    for (size_t i = 8; i < value.size(); ++i)
        if (i != 40 && !((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f'))) return false;
    return true;
}
bool safe(const std::string& s) {
    return !s.empty() && s.size() <= 4096 &&
           std::none_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
std::string diagnostic(std::string value) {
    if (value.find("EPCT1:") != value.npos || value.find("ACCESS_CODE") != value.npos)
        return "Screen component failed (sensitive diagnostics omitted)";
    for (char& c : value)
        if (static_cast<unsigned char>(c) < 32 || c == 127) c = ' ';
    while (!value.empty() && value.front() == ' ') value.erase(value.begin());
    while (!value.empty() && value.back() == ' ') value.pop_back();
    if (value.size() > 512) value.resize(512);
    return value;
}
void consume(Process& process, std::string& pending, const std::function<void(const std::string&)>& line) {
    auto data = process.drain();
    Erase erase{data};
    require(data.size() <= 16384 && pending.size() + data.size() <= 16384, "Screen component status overflow");
    pending += data;
    while (true) {
        const auto n = pending.find('\n');
        if (n == pending.npos) break;
        auto value = pending.substr(0, n);
        Erase clean{value};
        pending.erase(0, n + 1);
        if (!value.empty() && value.back() == '\r') value.pop_back();
        if (value.starts_with("error:")) throw std::runtime_error(diagnostic(value.substr(6)));
        line(value);
    }
}
void wait(Process& p, std::string& pending, const std::function<bool()>& done,
          const std::function<void(const std::string&)>& line, Clock::time_point deadline) {
    while (!done()) {
        consume(p, pending, line);
        if (done()) break;
        require(p.running(), "Screen component exited before readiness");
        require(Clock::now() < deadline, "Screen component preparation timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
}
struct ScopedScreenProvider::Impl {
    Factory factory;
    std::unique_ptr<Process> host, broker;
    uint16_t port = 0;
    bool failed = false;
    std::string hostPending, brokerPending, failure;
    explicit Impl(Factory f) : factory(std::move(f)) { require(bool(factory), "Missing process factory"); }
};
ScopedScreenProvider::ScopedScreenProvider(Factory factory) : impl_(std::make_unique<Impl>(std::move(factory))) {}
ScopedScreenProvider::~ScopedScreenProvider() {
    stop();
}
std::string ScopedScreenProvider::start(const Source& source, const peers::Grant& grant, uint16_t port,
                                        Clock::time_point deadline) {
    require(!running(), "Screen grant is already running");
    stop();
    require(port && grant.id && grant.kind == peers::Kind::existing_screen && !grant.delegated &&
                source.offer.kind == grant.kind && source.offer.id == grant.resource && source.offer.available &&
                (!grant.input || source.offer.input_allowed),
            "Invalid existing-screen grant");
    require(safe(source.device) && source.device.starts_with("\\\\?\\"),
            "Screen offer must pin a local monitor identity");
    try {
        impl_->port = port;
        impl_->host = impl_->factory();
        Command host{"dm-view.exe",
                     {"host", "--lan", "--serve", "--scoped-screen", "--screen-identity-stdin", "--require-code",
                      "--allow-screen", "--bind", "127.0.0.1", "--port", std::to_string(port)}};
        if (grant.input) host.arguments.push_back("--allow-screen-input");
        impl_->host->start(host);
        impl_->host->write(source.device + "\n");
        std::string code;
        Erase clean{code};
        bool listening = false;
        wait(
            *impl_->host, impl_->hostPending, [&] { return listening && !code.empty(); },
            [&](const std::string& line) {
                if (line.starts_with("ACCESS_CODE ")) {
                    code = line.substr(12);
                    require(code.size() == 6 &&
                                std::all_of(code.begin(), code.end(), [](char c) { return c >= '0' && c <= '9'; }),
                            "Invalid capture admission code");
                }
                if (line.starts_with("LISTENING ")) listening = true;
            },
            deadline);
        impl_->broker = impl_->factory();
        impl_->broker->start({"ExtraPane.Session.exe",
                              {"--session", "--access-stdin", "--bind", "127.0.0.1", "--peer", "127.0.0.1", "--port",
                               std::to_string(port)}});
        impl_->broker->write(code + "\n");
        bool ready = false;
        wait(
            *impl_->broker, impl_->brokerPending, [&] { return ready; },
            [&](const auto& line) { ready |= line == "SESSION_READY"; }, deadline);
        impl_->broker->write(grant.input ? "ADD 0 1\n" : "ADD 0 0\n");
        std::string credential;
        Erase cleanCredential{credential};
        wait(
            *impl_->broker, impl_->brokerPending, [&] { return !credential.empty(); },
            [&](const auto& line) {
                const std::string prefix = "SESSION_TICKET_V1\tdisplay\t";
                if (line.starts_with(prefix)) credential = line.substr(prefix.size());
            },
            deadline);
        if (!ticket(credential)) {
            wipe(credential);
            throw std::runtime_error("Invalid capture admission ticket");
        }
        return std::string(credential);
    } catch (...) {
        stop();
        throw;
    }
}
uint16_t ScopedScreenProvider::port() const {
    return impl_->port;
}
bool ScopedScreenProvider::running() const {
    if (impl_->failed || !impl_->host || !impl_->broker) return false;
    try {
        consume(*impl_->host, impl_->hostPending, [](const auto& line) {
            if (line.starts_with("SERVER_SESSION_ERROR")) throw std::runtime_error(diagnostic(line));
        });
        consume(*impl_->broker, impl_->brokerPending, [](const auto&) {});
        if (!impl_->host->running() || !impl_->broker->running())
            throw std::runtime_error("Screen capture component exited unexpectedly");
        return true;
    } catch (const std::exception& error) {
        impl_->failure = diagnostic(error.what());
        impl_->failed = true;
        return false;
    }
}
std::string ScopedScreenProvider::failure() const {
    return impl_->failure;
}
void ScopedScreenProvider::stop() noexcept {
    if (impl_->broker) impl_->broker->stop();
    if (impl_->host) impl_->host->stop();
    impl_->broker.reset();
    impl_->host.reset();
    wipe(impl_->hostPending);
    wipe(impl_->brokerPending);
    impl_->port = 0;
    impl_->failed = false;
    impl_->failure.clear();
}
struct ScopedScreenReceiver::Impl {
    Factory factory;
    std::unique_ptr<Process> process;
    bool failed = false;
    std::string pending, failure;
    explicit Impl(Factory f) : factory(std::move(f)) { require(bool(factory), "Missing process factory"); }
};
ScopedScreenReceiver::ScopedScreenReceiver(Factory factory) : impl_(std::make_unique<Impl>(std::move(factory))) {}
ScopedScreenReceiver::~ScopedScreenReceiver() {
    stop();
}
void ScopedScreenReceiver::start(const peers::Grant& grant, uint16_t port, std::string credential, std::string renderer,
                                 std::string monitor) {
    Erase clean{credential};
    require(!running(), "Screen receiver is already running");
    stop();
    require(grant.id && grant.kind == peers::Kind::existing_screen && port && ticket(credential),
            "Invalid existing-screen receiver grant");
    require(renderer == "d3d11" || renderer == "d3d12" || renderer == "vulkan" || renderer == "opengl",
            "Invalid screen renderer");
    Command command{"dm-view.exe",
                    {"receive", "--lan", "--existing-screen", "1", "--bind", "127.0.0.1", "--peer", "127.0.0.1",
                     "--port", std::to_string(port), "--access-stdin", "--renderer", renderer},
                    false,
                    false};
    if (grant.input) command.arguments.push_back("--control");
    if (!monitor.empty()) {
        require(safe(monitor) && monitor.starts_with("\\\\.\\DISPLAY") && monitor.size() <= 32,
                "Invalid local fullscreen monitor");
        command.arguments.insert(command.arguments.end(), {"--monitor", monitor});
    }
    try {
        impl_->process = impl_->factory();
        impl_->process->start(command);
        impl_->process->write(credential + "\n");
    } catch (...) {
        stop();
        throw;
    }
}
bool ScopedScreenReceiver::running() const {
    if (impl_->failed || !impl_->process) return false;
    try {
        consume(*impl_->process, impl_->pending, [](const auto&) {});
        if (!impl_->process->running()) throw std::runtime_error("Screen viewer exited before the stream ended");
        return true;
    } catch (const std::exception& error) {
        impl_->failure = diagnostic(error.what());
        impl_->failed = true;
        return false;
    }
}
std::string ScopedScreenReceiver::failure() const {
    return impl_->failure;
}
void ScopedScreenReceiver::stop() noexcept {
    if (impl_->process) impl_->process->stop();
    impl_->process.reset();
    wipe(impl_->pending);
    impl_->failed = false;
    impl_->failure.clear();
}
}
