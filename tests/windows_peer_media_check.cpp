#include "windows_peer_media.hpp"
#include <iostream>
#include <stdexcept>
using namespace extrapane;
using namespace extrapane::windows_peer;
using namespace std::chrono_literals;
static void require(bool b, const char* why) {
    if (!b) throw std::runtime_error(why);
}
static std::string ticket(unsigned kind) {
    return "EPCT1:" + std::to_string(kind) + ":" + std::string(32, 'a') + ":" + std::string(64, 'b');
}
struct Record {
    Command command;
    std::vector<std::string> writes;
    std::string output;
    bool live = false;
    unsigned stops = 0;
};
struct Fake final : Process {
    std::shared_ptr<Record> r;
    explicit Fake(std::shared_ptr<Record> record) : r(std::move(record)) {}
    void start(const Command& c) override {
        r->command = c;
        r->live = true;
        if (c.executable == "ExtraPane.Service.exe") r->output = "ACCESS_CODE 123456\nLISTENING 127.0.0.1:12345\n";
    }
    void write(const std::string& text) override {
        r->writes.push_back(text);
        if (r->command.executable == "ExtraPane.Service.exe" && text == "G") r->output += "ACCESS_CODE 654321\n";
        if (r->command.executable == "ExtraPane.Session.exe") {
            if (text.starts_with("ADD ")) {
                const auto kind = text[4] - '0';
                r->output += "SESSION_TICKET_V1\t" +
                             std::string(std::array{"display", "camera", "microphone", "usb", "speaker"}[kind]) + "\t" +
                             ticket(kind) + "\n";
            } else if (text.size() == 7)
                r->output += "SESSION_READY\n";
        }
        if (text.starts_with("ADD\t")) {
            const auto at = text.find('\t', 4);
            r->output += "RESOURCE_EVENT\t" + text.substr(4, at - 4) + "\tSTARTED\tok\n";
        }
    }
    std::string drain() override {
        auto out = std::move(r->output);
        r->output.clear();
        return out;
    }
    bool running() const override { return r->live; }
    void stop() noexcept override {
        r->live = false;
        ++r->stops;
    }
};
int main() {
    try {
        std::vector<std::shared_ptr<Record>> records;
        Factory factory = [&] {
            auto r = std::make_shared<Record>();
            records.push_back(r);
            return std::make_unique<Fake>(r);
        };
        WindowsPeerMediaAdapter provider(factory);
        peers::DeviceId owner{};
        owner[0] = 1;
        Source camera{{"opaque-camera", "Camera", peers::Kind::camera, true, false, ""}, "private-device"};
        Source display{{"opaque-display", "Display", peers::Kind::display_destination, true, true, "", 1920, 1080, 60},
                       "\\\\.\\DISPLAY1"};
        provider.sources({camera, display});
        peers::rpc::Open request{owner, "opaque-camera", false};
        peers::Grant grant{1, {}, owner, "opaque-camera", peers::Kind::camera, false, false, true};
        std::string reason;
        require(!provider.start(grant, request, 12345, ticket(0), reason) && records.empty(),
                "wrong-kind ticket launched helper");
        require(provider.start(grant, request, 12345, ticket(1), reason), "camera helper not prepared");
        grant.id = 2;
        require(provider.start(grant, request, 12346, ticket(1), reason) && records.size() == 1,
                "camera did not reuse capture broker");
        require(records[0]->writes[0] == "private-device\n", "source not private stdin");
        for (const auto& arg : records[0]->command.arguments)
            require(arg.find("EPCT1:") == arg.npos && arg != "private-device", "secret/source leaked to arguments");
        auto events = provider.poll();
        require(events.size() == 2 && events[0].active && events[1].active, "broker status not routed");
        provider.stop(1);
        require(records[0]->live && records[0]->writes.back() == "REMOVE\t1\n",
                "stopping one grant stopped shared capture");
        provider.stop(2);
        require(!records[0]->live, "last grant did not stop capture");
        grant = {3, {}, owner, "opaque-display", peers::Kind::display_destination, true, false, true};
        request = {owner, "opaque-display", true, 2560, 1440, 120};
        require(provider.start(grant, request, 12347, ticket(0), reason), "display helper not prepared");
        const auto& args = records.back()->command.arguments;
        require(std::find(args.begin(), args.end(), "--control") != args.end() &&
                    std::find(args.begin(), args.end(), "2560") != args.end(),
                "display input/mode missing");
        display.offer.label = "Renamed display";
        display.offer.width = 3840;
        provider.sources({camera, display});
        require(records.back()->live, "Metadata refresh stopped active grant");
        provider.sources({camera});
        require(!records.back()->live, "removed source retained helper");
        WindowsPeerReceiver receiver(factory);
        receiver.start(12345, std::chrono::steady_clock::now() + 1s);
        auto host = records.back();
        require(host->command.elevated && std::find(host->command.arguments.begin(), host->command.arguments.end(),
                                                    "127.0.0.1") != host->command.arguments.end(),
                "receiver not loopback scoped");
        const auto one = receiver.issue(peers::Kind::camera, false, std::chrono::steady_clock::now() + 1s);
        auto brokerOne = records.back();
        const auto two = receiver.issue(peers::Kind::camera, false, std::chrono::steady_clock::now() + 1s);
        auto brokerTwo = records.back();
        require(one.lease != two.lease && one.credential == ticket(1) && brokerOne != brokerTwo &&
                    host->writes.back() == "G",
                "same-kind tickets do not have distinct controls");
        receiver.release(one.lease);
        require(!brokerOne->live && brokerTwo->live && host->live, "lease release stopped unrelated receiver");
        const auto view =
            receiver.issue(peers::Kind::display_destination, false, std::chrono::steady_clock::now() + 1s);
        auto viewBroker = records.back();
        const auto control =
            receiver.issue(peers::Kind::display_destination, true, std::chrono::steady_clock::now() + 1s);
        auto controlBroker = records.back();
        require(viewBroker->writes.back() == "ADD 0 0\n" && controlBroker->writes.back() == "ADD 0 1\n",
                "display input scope not issued per ticket");
        receiver.release(view.lease);
        receiver.release(control.lease);
        receiver.stop();
        require(!host->live && !brokerTwo->live, "receiver teardown leaked children");
        std::cout << "Windows peer media: isolated fake-process source privacy, grants, shared capture, loopback "
                     "admission, distinct tickets and cleanup passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
