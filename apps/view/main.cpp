#include <windows.h>
#include <thread>
#include <dm/video.hpp>
#include <dm/h264.hpp>
#include <dm/input.hpp>
#include <charconv>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string_view>

namespace {
unsigned number(const std::string& text, unsigned maximum,bool allow_zero=false) {
    unsigned result = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (error != std::errc{} || end != text.data() + text.size() || (!result && !allow_zero) || result > maximum) throw std::invalid_argument("Invalid numeric option");
    return result;
}
}
int main(int argc, char** argv) {
    try {
        if (argc < 2 || std::string_view(argv[1]) == "--help") {
            std::cout << "DM encrypted display/input feasibility test (audio not implemented)\n"
                         "  dm-view info\n"
                         "  dm-view codec-check\n"
                         "  dm-view gpu-check\n"
                         "  dm-view host --name <pair> --bind <local-ipv4> --allow-screen [--extend | --display <GDI-name>] [--allow-input] [--frames 0] [--fps 30] [--width 1280 --height 720] [--codec h264|jpeg]\n"
                         "  dm-view receive --name <pair> --bind <local-ipv4> --peer <host-ipv4> [--headless | --control] [--fullscreen] [--decoder-adapter INDEX] [--quality auto|sharp|smooth]\n"
                         "  Common options: --port 43824 --timeout-ms 15000\n"
                         "Pairs: dm-session. Displays: dm-display list. Default source: primary output.\n"
                         "--extend requires separately installed signed DM driver and an administrator token.\n";
            return 0;
        }
        const std::string_view command(argv[1]);
        if(command=="input-broker") {
            if(argc!=4 || std::string_view(argv[2])!="--display")throw std::invalid_argument("Invalid input watchdog invocation");
            const std::string text=argv[3];return dm::input::broker_main(std::wstring(text.begin(),text.end()));
        }
        if(command=="gpu-check") {
            if(argc!=2) throw std::invalid_argument("gpu-check takes no options");
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
        bool allow = false, headless = false, extend=false,allow_input=false,control=false,fullscreen=false,keep_layout=false,client_mode=false,serve=false;
        std::map<std::string,std::string> args;
        for (int i = 2; i < argc; ++i) {
            const std::string key(argv[i]);
            if(key=="--serve") {serve=true;continue;}
            if(key=="--client-mode") {client_mode=true;continue;}
            if(key=="--keep-layout") {keep_layout=true;continue;}
            if(key=="--fullscreen") {if(fullscreen)throw std::invalid_argument("Duplicate fullscreen option");fullscreen=true;continue;}
            if (key == "--allow-screen") { if (allow) throw std::invalid_argument("Duplicate consent flag"); allow = true; continue; }
            if (key == "--headless") { if (headless) throw std::invalid_argument("Duplicate headless flag"); headless = true; continue; }
            if (key == "--extend") { if(extend) throw std::invalid_argument("Duplicate extend flag"); extend=true;continue; }
            if(key=="--allow-input") {if(allow_input)throw std::invalid_argument("Duplicate input grant");allow_input=true;continue;}
            if(key=="--control") {if(control)throw std::invalid_argument("Duplicate control request");control=true;continue;}
            if (key != "--quality" && key != "--name" && key != "--bind" && key != "--peer" && key != "--port" && key != "--timeout-ms" && key != "--frames" && key != "--fps" && key != "--codec" && key != "--decoder-adapter" && key != "--display" && key != "--width" && key != "--height" && key != "--parent-pid") throw std::invalid_argument("Unknown option: " + key);
            if (++i >= argc || !args.emplace(key, argv[i]).second) throw std::invalid_argument("Missing value or duplicate option");
        }
        if (!args.contains("--name") || !args.contains("--bind")) throw std::invalid_argument("--name and --bind are required");
        if(args.contains("--parent-pid")) {
            const auto pid=number(args.at("--parent-pid"),0xffffffffu);
            const auto parent=OpenProcess(SYNCHRONIZE,FALSE,pid);
            if(!parent)throw std::runtime_error("Launcher process is unavailable");
            std::thread([parent]{WaitForSingleObject(parent,INFINITE);CloseHandle(parent);TerminateProcess(GetCurrentProcess(),0);}).detach();
        }
        dm::net::Options options;
        options.bind_address = args.at("--bind"); options.port = 43824; options.timeout = std::chrono::milliseconds(15000);
        if (args.contains("--port")) options.port = static_cast<std::uint16_t>(number(args.at("--port"),65535));
        if (args.contains("--timeout-ms")) options.timeout = std::chrono::milliseconds(number(args.at("--timeout-ms"),60000,command=="host"));
        if (command == "host") {
            if (!allow) throw std::invalid_argument("Screen capture requires explicit --allow-screen consent");
            if (headless || control || fullscreen || args.contains("--quality") || args.contains("--peer") || args.contains("--decoder-adapter")) throw std::invalid_argument("Unsupported host option");
            auto codec=dm::video::wire::Codec::h264;
            if (args.contains("--codec")) {
                if(args.at("--codec")=="jpeg") codec=dm::video::wire::Codec::jpeg;
                else if(args.at("--codec")!="h264") throw std::invalid_argument("Unknown codec; choose h264 or jpeg");
            }
            const auto frames = args.contains("--frames") ? number(args.at("--frames"),36000,true) : 0;
            const auto fps = args.contains("--fps") ? number(args.at("--fps"),60) : 30;
            std::wstring source;
            if(args.contains("--display")) {
                const auto& text=args.at("--display");
                if(text.empty() || text.size()>32) throw std::invalid_argument("Invalid GDI display name");
                for(const unsigned char c:text) { if(c>127) throw std::invalid_argument("Display name must be ASCII");source.push_back(c); }
            }
            if(serve){if(!extend || !source.empty() || codec!=dm::video::wire::Codec::h264)throw std::invalid_argument("--serve requires H264 extension");dm::video::serve_video(options,args.at("--name"),allow_input,frames);}
            else dm::video::host_video(options,args.at("--name"),frames,fps,codec,source,extend,allow_input,args.contains("--width")?number(args.at("--width"),4096):1280,args.contains("--height")?number(args.at("--height"),4096):720,keep_layout,client_mode);
        } else {
            if (allow || allow_input || serve || extend || keep_layout || client_mode || ((control || fullscreen) && headless) || args.contains("--width") || args.contains("--height") || args.contains("--display") || args.contains("--frames") || args.contains("--fps") || args.contains("--codec") || !args.contains("--peer")) throw std::invalid_argument("Unsupported receiver options or missing --peer");
            options.peer = args.at("--peer");
            std::optional<unsigned> adapter;
            if (args.contains("--decoder-adapter")) adapter=number(args.at("--decoder-adapter"),255,true);
            dm::video::Quality quality=dm::video::Quality::automatic;
            if(args.contains("--quality")){const auto& value=args.at("--quality");if(value=="sharp")quality=dm::video::Quality::sharp;else if(value=="smooth")quality=dm::video::Quality::smooth;else if(value!="auto")throw std::invalid_argument("Quality must be auto, sharp or smooth");}
            if (headless) dm::video::receive_video(options,args.at("--name"),[](dm::video::Frame){},adapter,{},{},{},[&]{return dm::video::wire::Feedback{0,quality};});
            else dm::video::preview_video(options,args.at("--name"),adapter,control,fullscreen,quality);
        }
        return 0;
    } catch (const std::exception& error) { std::cerr << "error: " << error.what() << '\n'; return 1; }
}
