#include <dm/peer_auth.hpp>
#include <condition_variable>
#include <deque>
#include <thread>
#include <iostream>
#include <stdexcept>
using namespace dm::peer_auth;
void check(bool ok) {
    if (!ok) throw std::runtime_error("Peer auth check failed");
}
template <class F>
void rejects(F f) {
    bool failed = false;
    try {
        f();
    } catch (...) {
        failed = true;
    }
    check(failed);
}
struct Pipe {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<uint8_t> bytes;
    bool closed = false, tamper = false;
    std::vector<uint8_t> captured;
    std::function<void(std::span<const uint8_t>)> observe;
    void write(std::span<const uint8_t> b) {
        if (observe) observe(b);
        std::lock_guard lock(mutex);
        if (closed) throw std::runtime_error("closed");
        for (auto c : b) {
            bytes.push_back(c);
            captured.push_back(c);
        }
        if (tamper && !b.empty()) bytes.back() ^= 1;
        changed.notify_all();
    }
    void read(std::span<uint8_t> b) {
        std::unique_lock lock(mutex);
        for (auto& c : b) {
            if (!changed.wait_for(lock, std::chrono::seconds(2), [&] { return closed || !bytes.empty(); }) ||
                bytes.empty())
                throw std::runtime_error("timeout");
            c = bytes.front();
            bytes.pop_front();
        }
    }
    void close() {
        std::lock_guard lock(mutex);
        closed = true;
        changed.notify_all();
    }
};
using Store = std::map<DeviceId, Secret>;
struct Pair {
    Pipe cs, sc;
    Channel c{[&](auto b) { sc.read(b); }, [&](auto b) { cs.write(b); }},
        s{[&](auto b) { cs.read(b); }, [&](auto b) { sc.write(b); }};
    Identity ci, si;
    std::string client_error, server_error;
    bool run(Policy& p, ClientOptions o, Store& server, Store& client, DeviceId binding, Lookup custom = {}) {
        bool a = false, b = false;
        auto lookup = custom ? custom : Lookup([&](auto id) -> std::optional<Secret> {
            auto i = server.find(id);
            if (i == server.end()) return {};
            return i->second;
        });
        std::thread t([&] {
            try {
                si = s.server(
                    p, lookup,
                    [&](auto id, auto key) {
                        check(!server.contains(id));
                        server.emplace(id, key);
                    },
                    true, binding);
                b = true;
            } catch (const std::exception& e) {
                server_error = e.what();
                cs.close();
                sc.close();
            }
        });
        try {
            ci = c.client(o, [&](auto id, auto key) {
                check(!client.contains(id));
                client.emplace(id, key);
            });
            a = true;
        } catch (const std::exception& e) {
            client_error = e.what();
            cs.close();
            sc.close();
        }
        t.join();
        return a && b;
    }
};
int main() {
    try {
        auto host = random_identity(), device = random_identity(), binding = random_identity();
        Policy policy(host);
        Store server, client;
        ClientOptions o;
        o.local = device;
        o.binding = binding;
        o.outer_encrypted = true;
        o.code = policy.code();
        auto code = o.code;
        policy.set_required(false);
        check(!policy.required() && policy.code() == code);
        policy.set_required(true);
        check(policy.required() && policy.code() == code);
        Pair pair;
        check(pair.run(policy, o, server, client, binding));
        check(pair.ci.verified_peer == host && pair.si.verified_peer == device && pair.ci.newly_paired &&
              server.at(device).bytes == client.at(host).bytes);
        check(policy.code() == code);
        std::vector<uint8_t> data(150000, 42), out(data.size());
        pair.c.write(data);
        pair.s.read(out);
        check(out == data);
        pair.s.write(data);
        pair.c.read(out);
        check(out == data);
        pair.cs.captured.clear();
        std::array<uint8_t, 1> x{7}, y{};
        pair.c.write(x);
        auto replay = pair.cs.captured;
        pair.s.read(y);
        check(y == x);
        pair.cs.write(replay);
        rejects([&] { pair.s.read(y); });
        rejects([&] { pair.s.read(y); });
        ClientOptions remembered = o;
        remembered.mode = Mode::remembered;
        remembered.expected_peer = host;
        remembered.remembered = client.at(host);
        Pair again;
        check(again.run(policy, remembered, server, client, binding));
        check(!again.ci.newly_paired);
        again.cs.tamper = true;
        again.c.write(x);
        rejects([&] { again.s.read(y); });
        {
            Pair bad;
            auto wrong = remembered;
            wrong.remembered->bytes[0] ^= 1;
            check(!bad.run(policy, wrong, server, client, binding));
        }
        {
            Pair bad;
            auto wrong = remembered;
            wrong.expected_peer = random_identity();
            check(!bad.run(policy, wrong, server, client, binding));
        }
        {
            Pair bad;
            check(!bad.run(policy, remembered, server, client, random_identity()));
        }
        {
            Pair bad;
            check(!bad.run(policy, o, server, client, binding));
            check(server.at(device).bytes == client.at(host).bytes);
        }
        {
            Pair bad;
            unsigned lookups = 0;
            check(!bad.run(policy, remembered, server, client, binding, [&](auto) -> std::optional<Secret> {
                if (++lookups > 1) return {};
                return server.at(device);
            }));
        }
        // New clients reuse the current timed code; successful pairing does not consume it.
        {
            Policy p(host);
            Store a, b;
            auto opt = o;
            opt.code = p.code();
            Pair first;
            check(first.run(p, opt, a, b, binding));
            Store other;
            opt.local = random_identity();
            Pair second;
            check(second.run(p, opt, a, other, binding));
            check(p.code() == opt.code);
        }
        {
            Policy p(host);
            Store a, b;
            auto opt = o;
            opt.code = p.code();
            opt.code[0] = opt.code[0] == '0' ? '1' : '0';
            Pair bad;
            check(!bad.run(p, opt, a, b, binding));
            check(a.empty() && b.empty());
            check(bad.client_error == "Incorrect PIN or password, or code expired");
        }
        {
            Policy p(host);
            Store a, b;
            auto opt = o;
            opt.local = host;
            opt.code = p.code();
            Pair self;
            check(self.run(p, opt, a, b, binding));
            check(a.empty() && b.empty() && self.ci.verified_peer == host && !self.ci.newly_paired);
        }
        {
            Policy p(host, false);
            Store a, b;
            auto opt = o;
            opt.mode = Mode::open;
            Pair open;
            check(open.run(p, opt, a, b, binding));
            check(!open.ci.verified_peer && !open.si.verified_peer && a.empty() && b.empty());
        }
        {
            Policy p(host);
            Store a, b;
            auto opt = o;
            opt.mode = Mode::open;
            Pair bad;
            check(!bad.run(p, opt, a, b, binding));
        }
        {
            Policy p(host, false);
            Store a, b;
            auto opt = o;
            opt.mode = Mode::open;
            opt.outer_encrypted = false;
            Pair bad;
            check(!bad.run(p, opt, a, b, binding));
        }
        {
            Policy p(host);
            Store a, b;
            Pair missing;
            check(!missing.run(p, remembered, a, b, binding));
        }
        {
            Policy p(host);
            Store a, b;
            auto opt = o;
            opt.binding = {};
            Pair bad;
            check(!bad.run(p, opt, a, b, binding));
        }
        {
            Policy p(host);
            std::vector<DeviceId> pending;
            for (unsigned i = 0; i < 5; ++i) {
                pending.push_back(random_identity());
                p.begin(pending.back(), Mode::pair);
            }
            rejects([&] { p.begin(random_identity(), Mode::pair); });
            for (auto id : pending) p.finish(id, Mode::pair, true);
            p.begin(device, Mode::pair);
            p.finish(device, Mode::pair, true);
        }
        auto now = dm::access::Clock::now();
        Policy limited(host, true, [&] { return now; });
        for (unsigned i = 0; i < 5; ++i) {
            limited.begin(device, Mode::pair);
            limited.finish(device, Mode::pair, false);
        }
        check(!limited.pairing_available());
        limited.set_password("Custom!");
        check(!limited.pairing_available());
        limited.set_password("");
        check(!limited.pairing_available());
        now += std::chrono::minutes(2);
        check(limited.pairing_available());
        limited.begin(device, Mode::pair);
        limited.finish(device, Mode::pair, true);
        limited.begin(device, Mode::remembered);
        limited.finish(device, Mode::remembered, true);
        limited.stop();
        rejects([&] { limited.begin(device, Mode::remembered); });
        {
            auto tick = dm::access::Clock::now();
            Policy rotating(host, true, [&] { return tick; });
            const auto first = rotating.status();
            check(first.remaining_seconds == 20 && !first.custom && first.code.size() == 6);
            tick += std::chrono::milliseconds(19999);
            check(rotating.status().code == first.code && rotating.status().remaining_seconds == 1);
            tick += std::chrono::milliseconds(1);
            const auto next = rotating.status();
            check(next.remaining_seconds == 20 && next.code != first.code);
            rejects([&] { rotating.confirm_password(first.code); });
            rotating.confirm_password(next.code);
            auto old = o;
            old.code = first.code;
            Store a, b;
            Pair expired;
            check(!expired.run(rotating, old, a, b, binding));
            check(expired.client_error == "Incorrect PIN or password, or code expired");
            for (const auto& secret : std::vector<std::string>{" x ", "words!# no six digits",
                                                               std::string("caf") + "\xc3\xa9" + " \xf0\x9f\x94\x92",
                                                               std::string(1024, 'a')}) {
                rotating.set_password(secret);
                check(rotating.status().custom && rotating.status().code.empty() &&
                      !rotating.status().remaining_seconds);
                tick += std::chrono::seconds(120);
                check(rotating.code() == secret);
                rotating.confirm_password(secret);
                Store sa, ca;
                auto custom = o;
                custom.local = random_identity();
                custom.code = secret;
                Pair good;
                check(good.run(rotating, custom, sa, ca, binding));
                custom.local = random_identity();
                custom.code = secret + "!";
                if (custom.code.size() <= 1024) {
                    Store wrongStore;
                    Pair wrong;
                    check(!wrong.run(rotating, custom, sa, wrongStore, binding));
                    check(wrong.client_error == "Incorrect PIN or password, or code expired");
                }
            }
            rejects([&] { rotating.set_password(std::string(1025, 'a')); });
            rejects([&] { rotating.set_password("\xc0\xaf"); });
            check(!valid_pairing_password(""));
            check(!valid_pairing_password(std::string("a\0b", 3)));
            check(!valid_pairing_password("\xed\xa0\x80"));
            rotating.set_password("");
            check(!rotating.status().custom && rotating.status().remaining_seconds == 20);
        }
        {
            auto tick = dm::access::Clock::now();
            Policy expiring(host, true, [&] { return tick; });
            Store a, b;
            auto attempt = o;
            attempt.code = expiring.code();
            Pair crossing;
            unsigned proofs = 0;
            crossing.cs.observe = [&](auto bytes) {
                if (bytes.size() == 32 && ++proofs == 2) tick += std::chrono::seconds(20);
            };
            check(!crossing.run(expiring, attempt, a, b, binding));
            check(crossing.client_error == "Incorrect PIN or password, or code expired" && a.empty() && b.empty());
        }
        {
            auto tick = dm::access::Clock::now();
            Policy budget(host, true, [&] { return tick; });
            auto old = budget.code();
            for (unsigned i = 0; i < 5; ++i) {
                budget.begin(device, Mode::pair);
                budget.finish(device, Mode::pair, false);
            }
            check(!budget.pairing_available());
            for (unsigned i = 0; i < 2; ++i) {
                tick += std::chrono::seconds(20);
                const auto fresh = budget.status();
                check(fresh.code != old && fresh.remaining_seconds == 20);
                old = fresh.code;
                check(!budget.pairing_available());
                rejects([&] { budget.begin(random_identity(), Mode::pair); });
            }
            tick += std::chrono::milliseconds(19999);
            check(!budget.pairing_available());
            tick += std::chrono::milliseconds(1);
            check(budget.pairing_available());
            budget.begin(device, Mode::pair);
            budget.finish(device, Mode::pair, true);
        }
        {
            Channel broken([](auto) { throw std::runtime_error("transport timeout"); }, [](auto) {});
            auto attempt = o;
            attempt.code = "custom password";
            try {
                broken.client(attempt, {});
                check(false);
            } catch (const std::exception& e) {
                check(std::string(e.what()) == "transport timeout");
            }
        }
        std::cout << "Peer auth checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
