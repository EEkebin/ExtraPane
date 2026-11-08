#include <dm/network.hpp>
#include <charconv>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string_view>

namespace {
constexpr std::string_view usage =
    "ExtraPane network diagnostic (no media, authentication, or remote control)\n"
    "  dm-net interfaces\n"
    "  dm-net listen --bind <local-ipv4> [--port 43821] [--timeout-ms 5000]\n"
    "  dm-net probe --bind <local-ipv4> --peer <peer-ipv4> [--port 43821] [--timeout-ms 5000]\n"
    "Listen serves one diagnostic request and exits. IP literals only.\n";

unsigned number(std::string_view value, unsigned max) {
    unsigned parsed = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size() || parsed == 0 || parsed > max) {
        throw std::invalid_argument("Invalid numeric option: " + std::string(value));
    }
    return parsed;
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--help")) {
            std::cout << usage;
            return 0;
        }
        const std::string_view command(argv[1]);
        if (command == "interfaces") {
            if (argc != 2) throw std::invalid_argument("interfaces takes no options");
            for (const auto& adapter : dm::net::interfaces()) {
                std::cout << adapter.index << '\t' << (adapter.up ? "up" : "down") << '\t'
                          << adapter.address << '/' << adapter.prefix << '\t' << adapter.name << '\n';
            }
            return 0;
        }
        if (command != "listen" && command != "probe") throw std::invalid_argument("Unknown command");
        std::map<std::string, std::string> args;
        for (int i = 2; i < argc; i += 2) {
            const std::string key(argv[i]);
            if (key != "--bind" && key != "--peer" && key != "--port" && key != "--timeout-ms") {
                throw std::invalid_argument("Unknown option: " + key);
            }
            if (i + 1 >= argc) throw std::invalid_argument("Missing value for " + key);
            if (!args.emplace(key, argv[i + 1]).second) throw std::invalid_argument("Duplicate option: " + key);
        }
        if (!args.contains("--bind")) throw std::invalid_argument("--bind is required; run dm-net interfaces");
        dm::net::Options options;
        options.bind_address = args.at("--bind");
        if (args.contains("--port")) options.port = static_cast<std::uint16_t>(number(args.at("--port"), 65535));
        if (args.contains("--timeout-ms")) options.timeout = std::chrono::milliseconds(number(args.at("--timeout-ms"), 60000));
        if (command == "probe") {
            if (!args.contains("--peer")) throw std::invalid_argument("probe requires --peer");
            options.peer = args.at("--peer");
            dm::net::probe_peer(options);
        } else {
            if (args.contains("--peer")) throw std::invalid_argument("listen does not take --peer");
            dm::net::listen_once(options);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
