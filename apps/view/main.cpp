#include <dm/tls.hpp>
#include <dm/unified_host.hpp>
#include <syncstream>
#include <windows.h>
#include <thread>
#include <dm/video.hpp>
#include <dm/h264.hpp>
#include <dm/video_d3d12.hpp>
#include <dm/video_vulkan.hpp>
#include <dm/video_opengl.hpp>
#include <dm/input.hpp>
#include <charconv>
#include <algorithm>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string_view>

namespace {
dm::host::Control unified_control;
unsigned number(const std::string& text, unsigned maximum, bool allow_zero = false) {
    unsigned result = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (error != std::errc{} || end != text.data() + text.size() || (!result && !allow_zero) || result > maximum)
        throw std::invalid_argument("Invalid numeric option");
    return result;
}
}
int main(int argc, char** argv) {
    try {
        if (argc < 2 || std::string_view(argv[1]) == "--help") {
            std::cout << "ExtraPane encrypted display/input diagnostic (audio not implemented)\n"
                         "  dm-view info\n"
                         "  dm-view codec-check\n"
                         "  dm-view gpu-check\n"
                         "  dm-view d3d12-check [adapter-index]\n"
                         "  dm-view vulkan-check [adapter-index]\n"
                         "  dm-view opengl-check [adapter-index]\n"
                         "  dm-view renderer-info\n"
                         "  dm-view host --lan --bind <local-ipv4> --allow-screen [--extend | --display <GDI-name>] "
                         "[--allow-input] [--frames 0] [--fps 30] [--width 1280 --height 720] [--codec h264|jpeg]\n"
                         "  dm-view receive --lan --bind <local-ipv4> --peer <host-ipv4> [--headless | --control] "
                         "[--fullscreen] [--decoder-adapter INDEX] [--quality native|75|50] [--renderer "
                         "d3d11|d3d12|vulkan|opengl] [--monitor GDI-NAME] [--existing-screen ID] [--width N --height "
                         "N] [--fps 1..360]\n"
                         "  Existing screen: host --serve --extend --share-display GDI-NAME [--allow-screen-input]; "
                         "receiver --existing-screen 1 (view-only unless --control is also requested).\n"
                         "  Common options: --port 43824 --timeout-ms 15000\n"
                         "Pairs: dm-session. Displays: dm-display list. Default source: primary output.\n"
                         "--extend requires separately installed signed ExtraPane driver and an administrator token.\n";
            return 0;
        }
        const std::string_view command(argv[1]);
        if (command == "renderer-info") {
            if (argc != 2) throw std::invalid_argument("renderer-info takes no options");
            dm::video::renderer_info();
            return 0;
        }
        if (command == "d3d12-check") {
            if (argc > 3) throw std::invalid_argument("d3d12-check takes an optional adapter index");
            dm::video::d3d12_renderer_check(argc == 3 ? number(argv[2], 255, true) : 0);
            return 0;
        }
        if (command == "opengl-check") {
            if (argc > 3) throw std::invalid_argument("opengl-check takes an optional adapter index");
            dm::video::opengl_renderer_check(argc == 3 ? number(argv[2], 255, true) : 0);
            return 0;
        }
        if (command == "vulkan-check") {
            if (argc > 3) throw std::invalid_argument("vulkan-check takes an optional adapter index");
            dm::video::vulkan_renderer_check(argc == 3 ? number(argv[2], 255, true) : 0);
            return 0;
        }
        if (command == "input-broker") {
            if (argc != 4 || std::string_view(argv[2]) != "--display")
                throw std::invalid_argument("Invalid input watchdog invocation");
            const std::string text = argv[3];
            return dm::input::broker_main(std::wstring(text.begin(), text.end()));
        }
        if (command == "gpu-check") {
            if (argc != 2) throw std::invalid_argument("gpu-check takes no options");
            dm::video::gpu_h264_roundtrip_check();
            return 0;
        }
        if (command == "codec-check") {
            if (argc != 2) throw std::invalid_argument("codec-check takes no options");
            dm::video::h264_roundtrip_check();
            return 0;
        }
        if (command == "info") {
            if (argc != 2) throw std::invalid_argument("info takes no options");
            dm::video::hardware_info();
            return 0;
        }
        if (command != "host" && command != "receive") throw std::invalid_argument("Unknown command");
        bool scoped_screen = false, screen_identity_stdin = false;
        std::wstring scoped_identity;
        bool allow_screen_input = false, allow = false, headless = false, extend = false, allow_input = false,
             control = false, fullscreen = false, keep_layout = false, client_mode = false, serve = false;
        std::map<std::string, std::string> args;
        bool automatic_lan = false, require_code = false, access_stdin = false;
        for (int i = 2; i < argc; ++i) {
            const std::string key(argv[i]);
            if (key == "--scoped-screen") {
                scoped_screen = true;
                continue;
            }
            if (key == "--screen-identity-stdin") {
                screen_identity_stdin = true;
                continue;
            }
            if (key == "--require-code") {
                require_code = true;
                continue;
            }
            if (key == "--access-stdin") {
                access_stdin = true;
                continue;
            }
            if (key == "--lan") {
                automatic_lan = true;
                continue;
            }
            if (key == "--serve") {
                serve = true;
                continue;
            }
            if (key == "--client-mode") {
                client_mode = true;
                continue;
            }
            if (key == "--keep-layout") {
                keep_layout = true;
                continue;
            }
            if (key == "--fullscreen") {
                if (fullscreen) throw std::invalid_argument("Duplicate fullscreen option");
                fullscreen = true;
                continue;
            }
            if (key == "--allow-screen") {
                if (allow) throw std::invalid_argument("Duplicate consent flag");
                allow = true;
                continue;
            }
            if (key == "--headless") {
                if (headless) throw std::invalid_argument("Duplicate headless flag");
                headless = true;
                continue;
            }
            if (key == "--extend") {
                if (extend) throw std::invalid_argument("Duplicate extend flag");
                extend = true;
                continue;
            }
            if (key == "--allow-screen-input") {
                if (allow_screen_input) throw std::invalid_argument("Duplicate screen input grant");
                allow_screen_input = true;
                continue;
            }
            if (key == "--allow-input") {
                if (allow_input) throw std::invalid_argument("Duplicate input grant");
                allow_input = true;
                continue;
            }
            if (key == "--control") {
                if (control) throw std::invalid_argument("Duplicate control request");
                control = true;
                continue;
            }
            if (key != "--share-display" && key != "--existing-screen" && key != "--monitor" && key != "--renderer" &&
                key != "--quality" && key != "--name" && key != "--bind" && key != "--peer" && key != "--port" &&
                key != "--timeout-ms" && key != "--frames" && key != "--fps" && key != "--codec" &&
                key != "--decoder-adapter" && key != "--display" && key != "--width" && key != "--height" &&
                key != "--parent-pid")
                throw std::invalid_argument("Unknown option: " + key);
            if (++i >= argc || !args.emplace(key, argv[i]).second)
                throw std::invalid_argument("Missing value or duplicate option");
        }
        if (automatic_lan) {
            if (args.contains("--name")) throw std::invalid_argument("Choose --lan or --name");
            args["--name"] = "";
        }
        if (!args.contains("--name") || !args.contains("--bind"))
            throw std::invalid_argument("--lan (or diagnostic --name) and --bind are required");
        if (scoped_screen || screen_identity_stdin) {
            if (!scoped_screen || !screen_identity_stdin || command != "host" || !serve || !automatic_lan ||
                !require_code || args.at("--bind") != "127.0.0.1" || extend || allow_input ||
                args.contains("--display") || args.contains("--share-display"))
                throw std::invalid_argument("Invalid scoped screen host options");
            std::string identity;
            std::getline(std::cin, identity);
            if (identity.empty() || identity.size() > 4096 ||
                std::any_of(identity.begin(), identity.end(), [](unsigned char c) { return c < 32 || c == 127; }))
                throw std::invalid_argument("Invalid screen identity");
            const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, identity.data(),
                                                 static_cast<int>(identity.size()), nullptr, 0);
            if (!size) throw std::invalid_argument("Invalid UTF-8 screen identity");
            scoped_identity.resize(size);
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, identity.data(), static_cast<int>(identity.size()),
                                scoped_identity.data(), size);
        }
        if (args.contains("--parent-pid")) {
            const auto pid = number(args.at("--parent-pid"), 0xffffffffu);
            const auto parent = OpenProcess(SYNCHRONIZE, FALSE, pid);
            if (!parent) throw std::runtime_error("Launcher process is unavailable");
            std::thread([parent, unified = (command == "host" && serve)] {
                WaitForSingleObject(parent, INFINITE);
                CloseHandle(parent);
                if (unified) {
                    unified_control.stop = true;
                    Sleep(15000);
                }
                TerminateProcess(GetCurrentProcess(), 0);
            }).detach();
        }
        if ((require_code || access_stdin) && !automatic_lan)
            throw std::invalid_argument("Access codes require product LAN mode");
        std::shared_ptr<dm::access::Gate> server_gate;
        if (require_code) {
            if (command != "host") throw std::invalid_argument("Only hosts generate codes");
            auto gate = dm::security::enable_access_host();
            server_gate = gate;
            auto generate = [gate] { std::osyncstream(std::cout) << "ACCESS_CODE " << gate->generate() << std::endl; };
            generate();
            std::thread([gate] {
                auto previous = dm::access::Gate::Status::active;
                for (;;) {
                    Sleep(500);
                    auto current = gate->status();
                    if (current != previous) {
                        previous = current;
                        if (current != dm::access::Gate::Status::active)
                            std::osyncstream(std::cout)
                                << "ACCESS_STATUS "
                                << (current == dm::access::Gate::Status::used      ? "used"
                                    : current == dm::access::Gate::Status::expired ? "expired"
                                                                                   : "exhausted")
                                << std::endl;
                    }
                }
            }).detach();
            if (!serve)
                std::thread([generate] {
                    std::string line;
                    while (std::getline(std::cin, line)) {
                        if (line == "G") generate();
                    }
                }).detach();
        }
        if (command == "host" && serve) {
            std::thread([gate = server_gate] {
                std::string line;
                while (std::getline(std::cin, line)) {
                    if (line == "Q")
                        unified_control.quit = true;
                    else if (line == "E")
                        unified_control.eject_usb = true;
                    else if (line == "G" && gate)
                        std::osyncstream(std::cout) << "ACCESS_CODE " << gate->generate() << std::endl;
                }
                unified_control.stop = true;
            }).detach();
        }
        if (access_stdin) {
            if (command != "receive") throw std::invalid_argument("Client access input only");
            std::string code;
            std::getline(std::cin, code);
            dm::security::set_access_client(code);
            SecureZeroMemory(code.data(), code.size());
        }
        dm::net::Options options;
        options.bind_address = args.at("--bind");
        options.port = 43824;
        options.timeout = std::chrono::milliseconds(15000);
        if (args.contains("--port")) options.port = static_cast<std::uint16_t>(number(args.at("--port"), 65535));
        if (args.contains("--timeout-ms"))
            options.timeout = std::chrono::milliseconds(number(args.at("--timeout-ms"), 60000, command == "host"));
        if (command == "host") {
            if (!allow) throw std::invalid_argument("Screen capture requires explicit --allow-screen consent");
            if (args.contains("--existing-screen") || headless || control || fullscreen || args.contains("--monitor") ||
                args.contains("--renderer") || args.contains("--quality") || args.contains("--peer") ||
                args.contains("--decoder-adapter"))
                throw std::invalid_argument("Unsupported host option");
            auto codec = dm::video::wire::Codec::h264;
            if (args.contains("--codec")) {
                if (args.at("--codec") == "jpeg")
                    codec = dm::video::wire::Codec::jpeg;
                else if (args.at("--codec") != "h264")
                    throw std::invalid_argument("Unknown codec; choose h264 or jpeg");
            }
            const auto frames = args.contains("--frames") ? number(args.at("--frames"), 36000, true) : 0;
            const auto fps = args.contains("--fps") ? number(args.at("--fps"), 360) : 30;
            std::wstring source;
            if (args.contains("--display")) {
                const auto& text = args.at("--display");
                if (text.empty() || text.size() > 32) throw std::invalid_argument("Invalid GDI display name");
                for (const unsigned char c : text) {
                    if (c > 127) throw std::invalid_argument("Display name must be ASCII");
                    source.push_back(c);
                }
            }
            std::vector<dm::video::SharedScreen> shared_screens;
            if (args.contains("--share-display")) {
                const auto& name = args.at("--share-display");
                if (!serve || !name.starts_with("\\\\.\\DISPLAY") || name.size() > 32 || name.size() <= 11 ||
                    !std::all_of(name.begin() + 11, name.end(), [](char c) { return c >= '0' && c <= '9'; }))
                    throw std::invalid_argument("Share an attached Windows monitor on the unified server");
                shared_screens.push_back(
                    dm::video::share_screen(1, std::wstring(name.begin(), name.end()), allow_screen_input));
            } else if (scoped_screen)
                shared_screens.push_back({1, scoped_identity, allow_screen_input});
            else if (allow_screen_input)
                throw std::invalid_argument("Screen input requires an explicitly shared display");
            if (serve) {
                if ((!extend && !scoped_screen) || !source.empty() || codec != dm::video::wire::Codec::h264)
                    throw std::invalid_argument("--serve requires H264 extension");
                dm::host::serve(options, args.at("--name"), allow_input, frames, unified_control, shared_screens,
                                scoped_screen);
            } else
                dm::video::host_video(options, args.at("--name"), frames, fps, codec, source, extend, allow_input,
                                      args.contains("--width") ? number(args.at("--width"), 4096) : 1280,
                                      args.contains("--height") ? number(args.at("--height"), 4096) : 720, keep_layout,
                                      client_mode);
        } else {
            if (allow_screen_input || args.contains("--share-display") || allow || allow_input || serve || extend ||
                keep_layout || client_mode || ((control || fullscreen) && headless) || args.contains("--display") ||
                args.contains("--frames") || args.contains("--codec") || !args.contains("--peer"))
                throw std::invalid_argument("Unsupported receiver options or missing --peer");
            dm::video::PreviewOptions preview;
            if (args.contains("--existing-screen")) {
                preview.screen = {dm::video::wire::DisplayMode::existing_screen,
                                  number(args.at("--existing-screen"), 0xffffffffu)};
                if (args.contains("--width") || args.contains("--height"))
                    throw std::invalid_argument("Existing-screen capture does not configure a virtual monitor");
            }
            if (headless && (args.contains("--width") || args.contains("--height") || args.contains("--fps")))
                throw std::invalid_argument("Custom display mode requires a preview window");
            if (args.contains("--width") != args.contains("--height"))
                throw std::invalid_argument("Custom resolution requires both --width and --height");
            if (args.contains("--width")) {
                preview.width = number(args.at("--width"), 4096);
                preview.height = number(args.at("--height"), 4096);
                if (preview.width < 2 || preview.height < 2 || preview.width % 2 || preview.height % 2)
                    throw std::invalid_argument("Custom display dimensions must be even and between 2 and 4096");
            }
            if (args.contains("--fps")) preview.fps = number(args.at("--fps"), 360);
            if (args.contains("--monitor")) {
                const auto& name = args.at("--monitor");
                if (headless || !name.starts_with("\\\\.\\DISPLAY") || name.size() > 32 || name.size() <= 11 ||
                    !std::all_of(name.begin() + 11, name.end(), [](char c) { return c >= '0' && c <= '9'; }))
                    throw std::invalid_argument("Select an attached Windows monitor device name");
                preview.monitor.assign(name.begin(), name.end());
            }
            options.peer = args.at("--peer");
            std::optional<unsigned> adapter;
            if (args.contains("--decoder-adapter")) adapter = number(args.at("--decoder-adapter"), 255, true);
            dm::video::Quality quality = dm::video::Quality::native;
            auto renderer = dm::video::Renderer::d3d11;
            if (args.contains("--renderer")) {
                if (headless) throw std::invalid_argument("Headless receive has no renderer");
                const auto& value = args.at("--renderer");
                if (value == "d3d12")
                    renderer = dm::video::Renderer::d3d12;
                else if (value == "vulkan")
                    renderer = dm::video::Renderer::vulkan;
                else if (value == "opengl")
                    renderer = dm::video::Renderer::opengl;
                else if (value != "d3d11")
                    throw std::invalid_argument("Renderer must be d3d11, d3d12, vulkan or opengl");
            }
            if (args.contains("--quality")) {
                const auto& value = args.at("--quality");
                if (value == "75")
                    quality = dm::video::Quality::reduced;
                else if (value == "50")
                    quality = dm::video::Quality::low_bandwidth;
                else if (value != "native")
                    throw std::invalid_argument("Stream quality must be native, 75 or 50");
            }
            if (headless)
                dm::video::receive_video(
                    options, args.at("--name"), [](dm::video::Frame) {}, adapter, {}, {}, {}, quality, {}, {},
                    preview.screen);
            else
                dm::video::preview_video(options, args.at("--name"), adapter, control, fullscreen, quality, renderer,
                                         preview);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
