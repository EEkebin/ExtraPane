#include <extrapane/session.hpp>
#include <extrapane/server.hpp>
#include <extrapane/capabilities.hpp>
#include <extrapane/wire.hpp>
#include <stdexcept>
#include <iostream>

using namespace extrapane;
struct Counts { int prepared=0, started=0, stopped=0; };
class Fake final : public Resource {
public:
    Fake(Counts& counts, bool fails=false, bool throws=false): counts_(counts), fails_(fails), throws_(throws) {}
    Availability prepare() override { ++counts_.prepared; return {true,{}}; }
    Availability start() override {
        ++counts_.started;
        if (throws_) throw std::runtime_error("private adapter detail");
        return {!fails_, fails_ ? "Device busy" : ""};
    }
    void stop() noexcept override { ++counts_.stopped; }
private:
    Counts& counts_; bool fails_, throws_;
};
void require(bool value) { if (!value) throw std::runtime_error("Lifecycle check failed"); }
class FakeListener final : public Listener {
public:
    explicit FakeListener(Counts& c): counts(c) {}
    Availability start() override { ++counts.started; return {true,{}}; }
    void stop() noexcept override { ++counts.stopped; }
    Counts& counts;
};
int main() {
    try {
        using namespace extrapane::wire;
        Header header{Operation::request,ResourceKind::camera,0x0102030405060708ULL,64};
        auto encoded=encode(header);auto decoded=decode(encoded);
        require(decoded.resource_id==header.resource_id&&decoded.payload_bytes==64&&decoded.kind==ResourceKind::camera);
        require(encoded[8]==1&&encoded[15]==8&&encoded[19]==64);
        auto rejected=[](auto action){try{action();return false;}catch(const std::invalid_argument&){return true;}};
        for(std::size_t length=0;length<24;++length)require(rejected([&]{decode(std::span(encoded).first(length));}));
        auto corrupt=encoded;corrupt[5]=99;require(rejected([&]{decode(corrupt);}));
        corrupt=encoded;corrupt[20]=1;require(rejected([&]{decode(corrupt);}));
        corrupt=encoded;corrupt[7]=255;require(rejected([&]{decode(corrupt);}));
        corrupt=encoded;corrupt[6]=255;require(rejected([&]{decode(corrupt);}));
        require(rejected([&]{encode({Operation::request,ResourceKind::camera,1,max_control_payload+1});}));
        require(rejected([&]{encode({Operation::stop,ResourceKind::camera,1,1});}));
        require(rejected([&]{encode({Operation::request,ResourceKind::camera,0,0});}));
        auto client = unavailable_capabilities(), host = unavailable_capabilities();
        require(!can_connect(ResourceKind::camera,client,host).available);
        client[0].destination = {true,{}}; host[0].source = {true,{}};
        require(can_connect(ResourceKind::display,client,host).available);
        require(!can_connect(ResourceKind::display,host,client).available);
        client[1].source = {true,{}}; host[1].destination = {true,{}};
        require(can_connect(ResourceKind::camera,client,host).available);
        require(!can_connect(ResourceKind::camera,host,client).available);
        client[4].destination={true,{}};host[4].source={true,{}};
        require(can_connect(ResourceKind::speaker,client,host).available);
        require(!can_connect(ResourceKind::speaker,host,client).available);
        Header speakerHeader{Operation::request,ResourceKind::speaker,99,0};
        require(decode(encode(speakerHeader)).kind==ResourceKind::speaker);
        Counts camera, mic, broken, throwing, duplicate, late;
        Session session;
        require(session.add({1,ResourceKind::camera,"camera"},std::make_unique<Fake>(camera)).available);
        require(session.add({2,ResourceKind::microphone,"mic"},std::make_unique<Fake>(mic)).available);
        require(!session.add({3,ResourceKind::camera,"other"},std::make_unique<Fake>(duplicate)).available);
        require(duplicate.prepared == 0);
        require(!session.add({4,ResourceKind::usb,"usb"},std::make_unique<Fake>(broken,true)).available);
        require(broken.stopped == 1 && camera.stopped == 0 && mic.stopped == 0);
        require(!session.add({5,ResourceKind::usb,"usb2"},std::make_unique<Fake>(throwing,false,true)).available);
        require(throwing.stopped == 1);
        require(session.stop_resource(1));
        require(camera.stopped == 1 && mic.stopped == 0);
        require(session.fail_resource(2,"Permission withdrawn"));
        require(!session.fail_resource(2,"Repeated failure"));
        session.close(); session.close();
        require(session.closed() && session.snapshot().empty());
        require(mic.stopped == 1 && broken.stopped == 1 && throwing.stopped == 1);
        require(!session.add({6,ResourceKind::camera,"camera"},std::make_unique<Fake>(late)).available);
        require(late.prepared == 0);
        Counts owned;
        {
            Session scoped;
            require(scoped.add({1,ResourceKind::microphone,"mic"},std::make_unique<Fake>(owned)).available);
        }
        require(owned.stopped == 1);
        Counts listening, first, second, denied;
        auto claims = std::make_shared<DeviceClaims>();
        Server server(2);
        require(!server.admit(1).available);
        require(server.start(std::make_unique<FakeListener>(listening)).available);
        require(server.client_count() == 0);
        require(server.admit(1).available && server.admit(2).available);
        require(!server.admit(3).available);
        require(server.add(1,{1,ResourceKind::camera,"local-camera"},
            with_device_claim(claims,"local-camera",std::make_unique<Fake>(first))).available);
        require(!server.add(2,{1,ResourceKind::camera,"local-camera"},
            with_device_claim(claims,"local-camera",std::make_unique<Fake>(denied))).available);
        require(denied.prepared == 0 && denied.stopped == 0);
        require(server.disconnect(1));
        require(server.listening() && server.client_count() == 1 && first.stopped == 1);
        require(server.add(2,{2,ResourceKind::camera,"local-camera"},
            with_device_claim(claims,"local-camera",std::make_unique<Fake>(second))).available);
        server.stop(); server.stop();
        require(!server.listening() && server.client_count() == 0);
        require(second.stopped == 1 && listening.stopped == 1);
        require(claims->acquire("local-camera"));
        claims->release("local-camera");
        require(server.start(std::make_unique<FakeListener>(listening)).available);
        require(server.admit(1).available);
        std::cout << "Resource lifecycle checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
