#include <windows.h>
#include <dm/tls.hpp>
#include <charconv>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string_view>

namespace {
unsigned number(const std::string& text, unsigned maximum) {
    unsigned value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0 || value > maximum) throw std::invalid_argument("Invalid numeric option");
    return value;
}
}
int main(int argc, char** argv) {
    try {
        if (argc < 2 || std::string_view(argv[1]) == "--help") {
            std::cout << "DM authenticated session diagnostic (no video or input)\n"
                         "  dm-session pair --name <pair> --secret-stdin\n"
                         "  dm-session revoke --name <pair>\n"
                         "  dm-session peers\n"
                         "  dm-session listen --name <pair> --bind <local-ipv4> [--port 43823] [--timeout-ms 5000]\n"
                         "  dm-session connect --name <pair> --bind <local-ipv4> --peer <peer-ipv4> [--port 43823] [--timeout-ms 5000]\n"
                         "Provision the same random 32-byte hex key and pair name at both endpoints over a trusted channel.\n";
            return 0;
        }
        const std::string_view command(argv[1]);
        std::map<std::string, std::string> args;
        bool stdin_secret = false;
        for (int i = 2; i < argc; ++i) {
            const std::string key(argv[i]);
            if (key == "--secret-stdin") {
                if (stdin_secret) throw std::invalid_argument("Duplicate --secret-stdin");
                stdin_secret = true;
                continue;
            }
            if (key != "--name" && key != "--bind" && key != "--peer" && key != "--port" && key != "--timeout-ms") throw std::invalid_argument("Unknown option: " + key);
            if (++i >= argc) throw std::invalid_argument("Missing option value");
            if (!args.emplace(key, argv[i]).second) throw std::invalid_argument("Duplicate option: " + key);
        }
        if (command == "peers") {
            if (!args.empty() || stdin_secret) throw std::invalid_argument("peers takes no options");
            dm::security::list_pairings();
            return 0;
        }
        if (!args.contains("--name")) throw std::invalid_argument("--name is required");
        const auto& name = args.at("--name");
        dm::security::validate_name(name);
        if (command == "pair" || command == "revoke") {
            if (args.size() != 1 || (command == "revoke" && stdin_secret)) throw std::invalid_argument("Unexpected pairing option");
            if (command == "revoke") {
                dm::security::revoke_pairing(name);
                std::cout << "PAIRING_REVOKED\n";
            } else {
                if (!stdin_secret) throw std::invalid_argument("pair requires --secret-stdin; secrets must not appear in process arguments");
                // Bounded input: never read an arbitrary-length line containing a secret.
                std::array<char, 66> buffer{};
                std::cin.getline(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                if (std::cin.fail()) { SecureZeroMemory(buffer.data(), buffer.size()); throw std::invalid_argument("Invalid pairing key length"); }
                std::string hex(buffer.data());
                SecureZeroMemory(buffer.data(), buffer.size());
                dm::security::Key key{};
                try {
                    key = dm::security::parse_key(hex);
                    SecureZeroMemory(hex.data(), hex.size());
                    dm::security::save_pairing(name, key);
                } catch (...) {
                    SecureZeroMemory(hex.data(), hex.size());
                    SecureZeroMemory(key.data(), key.size());
                    throw;
                }
                SecureZeroMemory(key.data(), key.size());
                std::cout << "PAIRING_SAVED Windows-Credential-Manager\n";
            }
            return 0;
        }
        if (command != "listen" && command != "connect") throw std::invalid_argument("Unknown command");
        if (stdin_secret) throw std::invalid_argument("Session commands cannot accept pairing secrets");
        if (!args.contains("--bind")) throw std::invalid_argument("--bind is required");
        dm::net::Options options;
        options.port = 43823;
        options.bind_address = args.at("--bind");
        if (args.contains("--port")) options.port = static_cast<std::uint16_t>(number(args.at("--port"), 65535));
        if (args.contains("--timeout-ms")) options.timeout = std::chrono::milliseconds(number(args.at("--timeout-ms"), 60000));
        if (command == "connect") {
            if (!args.contains("--peer")) throw std::invalid_argument("connect requires --peer");
            options.peer = args.at("--peer");
        } else if (args.contains("--peer")) throw std::invalid_argument("listen does not take --peer");
        dm::security::session_check(options, name, command == "listen");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
