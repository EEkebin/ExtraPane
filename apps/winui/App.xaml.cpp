#include "backend.hpp"
#include "tray.hpp"
#include <dm/display.hpp>
#include "App.xaml.h"
#include <microsoft.ui.xaml.window.h>
#include <winrt/Windows.UI.Text.h>
#include <fstream>
#include <functional>
#include <set>
using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
namespace {
struct RolePage {
    const bool host;std::function<void()> changed;
    FrameworkElement root{nullptr};
    TextBox peer{nullptr},details{nullptr},port{nullptr};PasswordBox code{nullptr};CheckBox require_code{nullptr};TextBlock host_code{nullptr};Button generate_code{nullptr};ULONGLONG code_expires=0;TextBlock status{nullptr},addresses{nullptr};
    unsigned next_scale=0;std::wstring last_error,renderer_warning,renderer_failed;
    std::set<unsigned> connections;
    ULONGLONG last_addresses=0;
    CheckBox input{nullptr},debug{nullptr};Button start_button{nullptr},stop_button{nullptr};
    bool test_report=wcsstr(GetCommandLineW(),L"--test-report")!=nullptr;
    Child engine,pattern;bool active=false,pattern_started=false;std::string pending;std::wstring log;
    template<class T>T find(const wchar_t* name){return root.FindName(name).as<T>();}
    void attempt(const std::function<void()>& action){try{action();}catch(const hresult_error& e){status.Text(e.message());}catch(const std::exception& e){status.Text(wide(e.what()));}}
    void enabled(bool running){active=running;peer.IsEnabled(!running);port.IsEnabled(!running);code.IsEnabled(!running);require_code.IsEnabled(!running);generate_code.IsEnabled(host&&running&&require_code.IsChecked().Value());input.IsEnabled(!running);debug.IsEnabled(!running);start_button.IsEnabled(!running);stop_button.IsEnabled(running);if(changed)changed();}
    void ready(){status.Text(host?L"Ready. Start Driver to accept LAN clients.":L"Ready. Enter the server address to connect.");}
    void stop(){try{drain_output();}catch(...){}engine.stop();pattern.stop();connections.clear();host_code.Text(L"No active code");code_expires=0;enabled(false);status.Text(host?L"Driver stopped.":L"Disconnected. Ready when you are.");}
    void refresh_addresses(){
        std::wstring value;
        for(const auto& nic:dm::net::local_addresses()) {
            if(!value.empty())value+=L"\n";
            const auto name=winrt::to_hstring(nic.name);value+=name.c_str();value+=L"  -  "+wide(nic.address);
            value+=nic.ipv6?L"  (IPv6)":L"  (IPv4)";
        }
        if(value.empty())value=L"No connected LAN adapters found.";
        if(addresses.Text()!=value)addresses.Text(value);
        last_addresses=GetTickCount64();
    }
    void start(){
        const std::wstring portText(port.Text());if(portText.empty()||portText.size()>5||portText.find_first_not_of(L"0123456789")!=std::wstring::npos)throw std::runtime_error("Enter a port from 1 to 65535");const auto portValue=std::stoul(portText);if(!portValue||portValue>65535)throw std::runtime_error("Enter a port from 1 to 65535");
        std::string secret=ascii(std::wstring(code.Password()));if(!host&&!secret.empty()&&(secret.size()!=6||secret.find_first_not_of("0123456789")!=std::string::npos))throw std::runtime_error("Access code must contain six digits");
        const std::wstring remote(peer.Text());if(!host)ipv4(remote);
        if(!host)save_setting(L"Peer",remote);pending.clear();log.clear();last_error.clear();renderer_warning.clear();renderer_failed.clear();details.Text(L"");pattern_started=false;
        std::vector<std::wstring> args{host?L"host":L"receive",L"--lan",L"--bind",L"auto",L"--parent-pid",std::to_wstring(GetCurrentProcessId())};
        if(host)engine.start_driver(input.IsChecked().Value(),debug.IsChecked().Value(),static_cast<unsigned>(portValue),require_code.IsChecked().Value());
        else {const auto quality=std::exchange(next_scale,0u);find<TextBlock>(L"Mode").Text(quality==0?L"This connection: Native resolution.":quality==1?L"This connection: 75% stream resolution. The next connection defaults to Native.":L"This connection: 50% stream resolution. The next connection defaults to Native.");args.insert(args.end(),{L"--quality",quality==1?L"75":quality==2?L"50":L"native",L"--renderer",renderer_setting(),L"--peer",remote});args.insert(args.end(),{L"--port",std::to_wstring(portValue)});if(!secret.empty())args.push_back(L"--access-stdin");if(input.IsChecked().Value())args.push_back(L"--control");engine.start((directory()/L"dm-view.exe").wstring(),args);if(!secret.empty()){engine.send_control(secret+"\n");SecureZeroMemory(secret.data(),secret.size());code.Password(L"");}}
        enabled(true);status.Text(host?L"Starting driver...":L"Connecting securely to your server...");
    }
    void drain_output(){if(code_expires&&GetTickCount64()>=code_expires){host_code.Text(L"Code expired. Generate a new code.");code_expires=0;}if(!engine.process)return;pending+=engine.drain();size_t end;
        while((end=pending.find('\n'))!=std::string::npos){auto line=pending.substr(0,end);pending.erase(0,end+1);if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.starts_with("ACCESS_STATUS ")){host_code.Text(L"Code "+wide(line.substr(14))+L". Generate a new code.");code_expires=0;continue;}if(line.starts_with("ACCESS_CODE ")){auto digits=line.substr(12);if(digits.size()==6&&digits.find_first_not_of("0123456789")==std::string::npos){host_code.Text(wide(digits)+L" ? one use, expires in 5 minutes");code_expires=GetTickCount64()+300000;}SecureZeroMemory(line.data(),line.size());continue;}log+=wide(line)+L"\r\n";if(log.size()>24000)log.erase(0,log.size()-18000);
            if(line.starts_with("DRIVER_STARTED")||line.starts_with("LISTENING")||line.starts_with("DRIVER_WAITING")){status.Text(L"Driver running. Waiting for a client connection.");pattern.stop();pattern_started=false;}
            if(host && (line.starts_with("CLIENT_CONNECTED slot=")||line.starts_with("CLIENT_DISCONNECTED slot="))){
                const auto position=line.find("slot=");const auto slot=std::stoul(line.substr(position+5));
                if(line.starts_with("CLIENT_CONNECTED"))connections.insert(slot);else connections.erase(slot);
                status.Text(connections.empty()?L"Driver running. Waiting for clients.":std::to_wstring(connections.size())+L" client(s) connected. Driver running.");
            }
            if(line.starts_with("CLIENT_MODE")){
                const auto requested=line.find("requested="),selected=line.find("selected="),fallback=line.find(" fallback=");
                if(requested!=std::string::npos&&selected!=std::string::npos&&fallback!=std::string::npos){
                    auto wanted=line.substr(requested+10,selected-requested-11),chosen=line.substr(selected+9,fallback-selected-9);
                    find<TextBlock>(L"Mode").Text(L"Client screen: "+wide(wanted)+L". Starting display: "+wide(chosen)+(line.find("fallback=1")!=std::string::npos?L" (limited by driver/codec support).":L". Saved Windows layout takes precedence."));
                }
            }
            if(line.starts_with("VIDEO_STARTED")||line.starts_with("VIDEO_RECEIVING")){status.Text(host?std::to_wstring(connections.size())+L" client(s) connected. Arrange monitors in Windows Settings.":L"Connected. You can rearrange the display in Windows Settings.");if(host&&debug.IsChecked().Value()&&!pattern_started){pattern.start((directory()/L"dm-display.exe").wstring(),{L"pattern",L"60"});pattern_started=true;}}
            if(line.starts_with("DISPLAY_RECONFIGURING"))status.Text(L"Applying your display changes...");
            if(line.starts_with("DISPLAY_RECONFIGURED"))status.Text(L"Connected. Display changes applied.");
            if(!host && line.starts_with("NEXT_STREAM_SCALE ") && line.size()==19 && line.back()>='0' && line.back()<='2'){
                next_scale=static_cast<unsigned>(line.back()-'0');
                find<TextBlock>(L"Mode").Text(next_scale==0?L"Next connection: Native resolution.":next_scale==1?L"Next connection only: 75% stream resolution. Later connections return to Native.":L"Next connection only: 50% stream resolution. Later connections return to Native.");
            }
            if(line.starts_with("error:")){last_error=wide(line.substr(6));status.Text(last_error);}
            if(!host&&line.starts_with("RENDERER_UNAVAILABLE ")){const auto text=line.substr(std::string_view("RENDERER_UNAVAILABLE ").size());const auto split=text.find(' ');if(split!=std::string::npos){renderer_failed=wide(text.substr(0,split));renderer_warning=wide(text.substr(split+1));status.Text(renderer_failed+L" could not run; using D3D11. "+renderer_warning);if(changed)changed();}}
        }
        if(pending.size()>16384)pending.clear();if(debug.IsChecked().Value())details.Text(log);if(test_report){std::ofstream out(directory()/(host?L"server-session-check.log":L"client-session-check.log"));out<<ascii(log);}
    }
    void tick(){
        if(GetTickCount64()-last_addresses>5000)refresh_addresses();if(!engine.process)return;
        drain_output();
        if(WaitForSingleObject(engine.process,0)==WAIT_OBJECT_0){
            DWORD exit=0;GetExitCodeProcess(engine.process,&exit);drain_output();engine.stop();pattern.stop();enabled(false);
            if(!exit)status.Text(host?L"Driver stopped.":L"Session ended. Ready to connect again.");
            else if(!host){next_scale=0;status.Text((last_error.empty()?L"The display connection ended unexpectedly.":last_error)+std::wstring(L" Next connection will use Native resolution."));}
            else if(last_error.empty())status.Text(L"Driver stopped unexpectedly. Start Driver to try again.");
        }
    }
    RolePage(bool server):host(server){
        root=Markup::XamlReader::Load(LR"XAML(
<ScrollViewer xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Background="{ThemeResource ApplicationPageBackgroundThemeBrush}">
<StackPanel x:Name="RoleOptions" Padding="32" Spacing="20" MaxWidth="760" HorizontalAlignment="Stretch">
 <StackPanel Spacing="6"><TextBlock x:Name="Title" FontSize="32" FontWeight="SemiBold"/><TextBlock x:Name="Subtitle" TextWrapping="Wrap" Foreground="{ThemeResource TextFillColorSecondaryBrush}"/></StackPanel>
 <Border Padding="20" CornerRadius="12" Background="{ThemeResource CardBackgroundFillColorDefaultBrush}" BorderBrush="{ThemeResource CardStrokeColorDefaultBrush}" BorderThickness="1"><StackPanel x:Name="ConnectionOptions" Spacing="16">
 <StackPanel Spacing="8"><TextBlock Text="This PC's network addresses" FontWeight="SemiBold"/><ScrollViewer MaxHeight="165" HorizontalScrollBarVisibility="Auto"><TextBlock x:Name="Addresses" IsTextSelectionEnabled="True" FontSize="13"/></ScrollViewer><TextBlock Text="Connections use IPv4. Addresses are detected automatically." FontSize="12" TextWrapping="Wrap" Foreground="{ThemeResource TextFillColorSecondaryBrush}"/></StackPanel>
 <TextBox x:Name="Peer" Header="Server address" PlaceholderText="Enter the server's IPv4 address"/>
 <TextBlock x:Name="Mode" TextWrapping="Wrap" Foreground="{ThemeResource TextFillColorSecondaryBrush}"/>
 <CheckBox x:Name="Input" Content="Mouse, keyboard, touch and pen" IsChecked="True"/>
 <StackPanel Orientation="Horizontal" Spacing="12"><Button x:Name="Start" Style="{StaticResource AccentButtonStyle}"/><Button x:Name="Stop" Content="Disconnect" IsEnabled="False"/></StackPanel>
 <TextBlock x:Name="Status" Text="Ready to connect over LAN." TextWrapping="Wrap"/>
 </StackPanel></Border>
 <TextBox x:Name="Port" Header="TCP port" Text="43824" MaxLength="5"/>
 <CheckBox x:Name="RequireCode" Content="Require access code"/>
 <PasswordBox x:Name="AccessCode" Header="Access code (leave blank for trusted LAN)" MaxLength="6"/>
 <TextBlock x:Name="HostCode" Text="Start Driver to generate a code" IsTextSelectionEnabled="True" TextWrapping="Wrap"/>
 <Button x:Name="GenerateCode" Content="Generate new code" IsEnabled="False"/>
 <CheckBox x:Name="Debug" Content="Debug diagnostics"/>
 <TextBox x:Name="Details" Visibility="Collapsed" IsReadOnly="True" AcceptsReturn="True" Height="180" TextWrapping="Wrap"/>
 <TextBlock Text="Direct LAN · Encrypted connection · No audio yet" FontSize="12" Foreground="{ThemeResource TextFillColorSecondaryBrush}"/>
</StackPanel></ScrollViewer>)XAML").as<FrameworkElement>();
        find<TextBlock>(L"Title").Text(host?L"Server":L"Client");find<TextBlock>(L"Subtitle").Text(host?L"Extend your desktop onto another computer.":L"Make this screen part of your server's desktop.");
        find<TextBlock>(L"Mode").Text(host?L"Up to four clients, each with its own monitor. Clients connect automatically while the driver is running. Resolution and refresh come from each client.":L"Uses this screen's resolution and refresh, within server limits. Resize the display window freely; F11 toggles fullscreen.");
        addresses=find<TextBlock>(L"Addresses");peer=find<TextBox>(L"Peer");details=find<TextBox>(L"Details");status=find<TextBlock>(L"Status");input=find<CheckBox>(L"Input");debug=find<CheckBox>(L"Debug");if(host)debug.Content(box_value(L"Debug: color test and 3,600-frame session"));start_button=find<Button>(L"Start");stop_button=find<Button>(L"Stop");
        port=find<TextBox>(L"Port");code=find<PasswordBox>(L"AccessCode");require_code=find<CheckBox>(L"RequireCode");host_code=find<TextBlock>(L"HostCode");generate_code=find<Button>(L"GenerateCode");
        code.Visibility(host?Visibility::Collapsed:Visibility::Visible);require_code.Visibility(host?Visibility::Visible:Visibility::Collapsed);host_code.Visibility(host?Visibility::Visible:Visibility::Collapsed);generate_code.Visibility(host?Visibility::Visible:Visibility::Collapsed);
        generate_code.Click([this](auto&&,auto&&){attempt([&]{engine.send_control("G");});});
        refresh_addresses();peer.Text(host?L"":setting(L"Peer",L""));if(host)peer.Visibility(Visibility::Collapsed);
        start_button.Content(box_value(host?L"Start Driver":L"Connect"));stop_button.Content(box_value(host?L"Stop Driver":L"Disconnect"));
        start_button.Click([this](auto&&,auto&&){attempt([&]{start();});});stop_button.Click([this](auto&&,auto&&){attempt([&]{stop();});});
        debug.Checked([this](auto&&,auto&&){details.Visibility(Visibility::Visible);});debug.Unchecked([this](auto&&,auto&&){details.Visibility(Visibility::Collapsed);});
        ready();

    }
};
#include "settings.hpp"
#include "shell.hpp"
winrt::fire_and_forget capture_check(Shell* ui) {
    try {
        winrt::Microsoft::UI::Xaml::Media::Imaging::RenderTargetBitmap image;
        co_await image.RenderAsync(ui->root);
        const auto pixels=co_await image.GetPixelsAsync();
        BITMAPFILEHEADER file{};BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=image.PixelWidth();info.biHeight=-image.PixelHeight();info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
        file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info);file.bfSize=file.bfOffBits+pixels.Length();
        std::ofstream out(directory()/L"ui-check.bmp",std::ios::binary);out.write(reinterpret_cast<const char*>(&file),sizeof(file));out.write(reinterpret_cast<const char*>(&info),sizeof(info));out.write(reinterpret_cast<const char*>(pixels.data()),pixels.Length());
        if(!out || image.PixelWidth()<300 || image.PixelHeight()<300)ExitProcess(2);
        ui->exit();
    } catch(...) {ExitProcess(3);}
}
std::unique_ptr<Shell> shell;
}
namespace winrt::DM::implementation {
App::App(){}
void App::OnLaunched(LaunchActivatedEventArgs const&){
    try{Resources().MergedDictionaries().Append(XamlControlsResources());window=Window();shell=std::make_unique<Shell>(window);
        if(wcsstr(GetCommandLineW(),L"--smoke-test")){
            for(auto* page:{shell->server.get(),shell->client.get()})if(!page->start_button.IsEnabled()||page->stop_button.IsEnabled()||page->debug.IsChecked().Value()||page->addresses.Text().empty())throw std::runtime_error("Bad initial page state");
            shell->choose(false);shell->select(L"About");shell->choose(true);if(wcsstr(GetCommandLineW(),L"--settings"))shell->navigation.SelectedItem(shell->navigation.FooterMenuItems().GetAt(0));
            shell->timer.Interval(std::chrono::milliseconds(700));shell->timer.Tick([](auto&&,auto&&){shell->timer.Stop();capture_check(shell.get());});
        }else if(wcsstr(GetCommandLineW(),L"--start")){auto& page=wcsstr(GetCommandLineW(),L"--client")?shell->client:shell->server;page->attempt([&]{page->start();});}

    }catch(const hresult_error& e){MessageBoxW(nullptr,e.message().c_str(),L"ExtraPane startup",MB_OK|MB_ICONERROR);ExitProcess(1);}catch(const std::exception& e){MessageBoxW(nullptr,wide(e.what()).c_str(),L"ExtraPane startup",MB_OK|MB_ICONERROR);ExitProcess(1);}
}
}

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    const bool isolated=wcsstr(GetCommandLineW(),L"--smoke-test")||wcsstr(GetCommandLineW(),L"--test-report");
    HANDLE instance=nullptr;
    if(!isolated){
        instance=CreateMutexW(nullptr,FALSE,L"Local\\SideScreen.App");
        if(!instance || GetLastError()==ERROR_ALREADY_EXISTS){
            for(int i=0;i<40;++i){if(auto existing=FindWindowW(Tray::class_name(),nullptr)){PostMessageW(existing,Tray::show_message,0,0);if(instance)CloseHandle(instance);return 0;}Sleep(50);}
            if(instance)CloseHandle(instance);return 1;
        }
    }
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    Application::Start([](auto&&){winrt::make<winrt::DM::implementation::App>();});
    shell.reset();if(instance)CloseHandle(instance);return 0;
}
