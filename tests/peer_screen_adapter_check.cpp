#include "../apps/shared/windows_peer_screen.hpp"
#include <iostream>
#include <stdexcept>
#include <deque>
using namespace extrapane;
using namespace windows_peer;
void check(bool ok) {
    if (!ok) throw std::runtime_error("Scoped screen check failed");
}
template <class F>
void rejects(F f) {
    bool no = false;
    try {
        f();
    } catch (...) {
        no = true;
    }
    check(no);
}
struct State {
    Command command;
    std::vector<std::string> writes;
    std::string pending;
    bool alive = false, stopped = false;
};
const std::string ticket = "EPCT1:0:" + std::string(32, 'a') + ":" + std::string(64, 'b');
struct Fake : Process {
    std::shared_ptr<State> s;
    explicit Fake(std::shared_ptr<State> value) : s(std::move(value)) {}
    void start(const Command& c) override {
        s->command = c;
        s->alive = true;
        if (c.executable == "dm-view.exe" && c.arguments[0] == "host")
            s->pending = "ACCESS_CODE 123456\nLISTENING 127.0.0.1\n";
    }
    void write(const std::string& text) override {
        s->writes.push_back(text);
        if (s->command.executable == "ExtraPane.Session.exe")
            s->pending +=
                text.starts_with("ADD 0 ") ? "SESSION_TICKET_V1\tdisplay\t" + ticket + "\n" : "SESSION_READY\n";
    }
    std::string drain() override {
        auto result = std::move(s->pending);
        s->pending.clear();
        return result;
    }
    bool running() const override { return s->alive; }
    void stop() noexcept override {
        s->alive = false;
        s->stopped = true;
    }
};
bool has(const Command& c, const std::string& value) {
    return std::find(c.arguments.begin(), c.arguments.end(), value) != c.arguments.end();
}
int main() {
    try {
        std::vector<std::shared_ptr<State>> children;
        Factory factory = [&] {
            auto state = std::make_shared<State>();
            children.push_back(state);
            return std::make_unique<Fake>(state);
        };
        Source source;
        source.offer.id = "screen";
        source.offer.kind = peers::Kind::existing_screen;
        source.offer.available = true;
        source.offer.input_allowed = true;
        source.device = "\\\\?\\DISPLAY#private-identity";
        peers::Grant grant;
        grant.id = 7;
        grant.resource = "screen";
        grant.kind = peers::Kind::existing_screen;
        ScopedScreenProvider provider(factory);
        check(provider.start(source, grant, 45001, std::chrono::steady_clock::now() + std::chrono::seconds(1)) ==
              ticket);
        check(provider.running() && provider.port() == 45001);
        const auto& host = children[0]->command;
        check(has(host, "--scoped-screen") && has(host, "--screen-identity-stdin") && has(host, "--require-code") &&
              !has(host, "--extend") && !has(host, "--allow-input") && !has(host, "--allow-screen-input") &&
              !host.elevated);
        check(children[0]->writes[0] == source.device + "\n");
        check(children[1]->writes.back() == "ADD 0 0\n");
        check(!has(host, source.device));
        provider.stop();
        check(children[0]->stopped && children[1]->stopped && !provider.running());
        ScopedScreenReceiver receiver(factory);
        receiver.start(grant, 45002, ticket);
        check(has(children.back()->command, "--existing-screen") && !has(children.back()->command, "--control") &&
              !has(children.back()->command, "--width") && !has(children.back()->command, ticket));
        receiver.stop();
        grant.input = true;
        provider.start(source, grant, 45001, std::chrono::steady_clock::now() + std::chrono::seconds(1));
        check(has(children[children.size() - 2]->command, "--allow-screen-input"));
        check(children.back()->writes.back() == "ADD 0 1\n");
        receiver.start(grant, 45002, ticket);
        check(has(children.back()->command, "--control"));
        provider.stop();
        receiver.stop();
        receiver.start(grant, 45002, ticket);
        children.back()->pending = "error: Selected decoder initialization failed\n";
        children.back()->alive = false;
        check(!receiver.running() && receiver.failure() == "Selected decoder initialization failed");
        receiver.stop();
        receiver.start(grant, 45002, ticket);
        children.back()->pending = "error: EPCT1:private-ticket\n";
        check(!receiver.running() && receiver.failure().find("private-ticket") == std::string::npos);
        receiver.stop();
        source.offer.input_allowed = false;
        rejects([&] { provider.start(source, grant, 45001, std::chrono::steady_clock::now()); });
        source.offer.input_allowed = true;
        source.device = "\\\\.\\DISPLAY1";
        rejects([&] { provider.start(source, grant, 45001, std::chrono::steady_clock::now()); });
        auto wrong = ticket;
        wrong[6] = '1';
        rejects([&] { receiver.start(grant, 45002, wrong); });
        grant.kind = peers::Kind::display_destination;
        rejects([&] { receiver.start(grant, 45002, ticket); });
        std::cout << "Scoped screen command, identity, ticket and input checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
