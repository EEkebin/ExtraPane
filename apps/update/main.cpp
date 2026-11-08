#include <windows.h>
#include <shellapi.h>
#include <dm/update.hpp>
#include <stdexcept>
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    try{int count=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&count);if(count!=4){if(argv)LocalFree(argv);throw std::runtime_error("Invalid update handoff");}const auto parent=std::stoul(argv[1]);const std::filesystem::path installer(argv[2]);const std::wstring wide_digest(argv[3]);LocalFree(argv);const std::string digest(wide_digest.begin(),wide_digest.end());dm::update::launch_installer_after_exit(parent,installer,digest);CoUninitialize();return 0;}
    catch(const std::exception& error){const std::string text=error.what();const std::wstring message(text.begin(),text.end());MessageBoxW(nullptr,message.c_str(),L"ExtraPane update",MB_OK|MB_ICONERROR);CoUninitialize();return 1;}
}
