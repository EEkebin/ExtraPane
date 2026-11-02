#include "backend.hpp"
#include "tray.hpp"
#include <dm/display.hpp>
#include "App.xaml.h"
#include <microsoft.ui.xaml.window.h>
#include <winrt/Windows.UI.Text.h>
#include <fstream>
#include <functional>
using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
namespace {
struct Shell {
    Window window{nullptr};FrameworkElement root{nullptr};DispatcherTimer timer;
    TextBox peer{nullptr},code{nullptr},details{nullptr};TextBlock status{nullptr},addresses{nullptr};
    std::unique_ptr<Tray> tray;bool quitting=false,firewall_owned=false;ULONGLONG last_addresses=0;
    CheckBox input{nullptr},debug{nullptr};Button start_button{nullptr},stop_button{nullptr};
    ULONGLONG started=GetTickCount64();unsigned test_seconds=0;bool test_report=wcsstr(GetCommandLineW(),L"--test-report")!=nullptr;
    Child engine,pattern;bool active=false,pattern_started=false;std::string pending;std::wstring log;
    template<class T>T find(const wchar_t* name){return root.FindName(name).as<T>();}
    void attempt(const std::function<void()>& action){try{action();}catch(const hresult_error& e){status.Text(e.message());}catch(const std::exception& e){status.Text(wide(e.what()));}}
    void enabled(bool running){active=running;peer.IsEnabled(!running);input.IsEnabled(!running);debug.IsEnabled(!running);start_button.IsEnabled(!running);stop_button.IsEnabled(running);find<Expander>(L"Pairing").IsEnabled(!running);if(tray)tray->update(running);}
    void paired(){attempt([&]{auto key=dm::security::load_pairing("dm-lan");SecureZeroMemory(key.data(),key.size());status.Text(L"Paired and ready.");});}
    void stop(){
        if(host && active){try{for(const auto& monitor:dm::display::active_monitors())if(monitor.dm_virtual)dm::display::remember_last_mode(monitor.source);}catch(...){}}
        engine.stop();pattern.stop();enabled(false);if(firewall_owned){firewall(false);firewall_owned=false;}status.Text(host?L"Driver stopped.":L"Disconnected. Ready when you are.");}
    void exit(){quitting=true;attempt([&]{stop();});timer.Stop();if(tray)tray->remove();window.Close();}
    void show(){window.AppWindow().Show();window.Activate();}
    void refresh_addresses(){
        std::wstring value;
        for(const auto& nic:dm::net::local_addresses()) {
            if(!value.empty())value+=L"\n";
            const auto name=winrt::to_hstring(nic.name);value+=name.c_str();value+=L"  -  "+wide(nic.address);
            value+=nic.ipv6?L"  (IPv6)":L"  (IPv4)";if(!nic.up)value+=L"  -  Disconnected";
        }
        if(value.empty())value=L"No network addresses found.";
        if(addresses.Text()!=value)addresses.Text(value);
        last_addresses=GetTickCount64();
    }
    void start(){
        const std::wstring remote(peer.Text());if(!host)ipv4(remote);
        auto key=dm::security::load_pairing("dm-lan");SecureZeroMemory(key.data(),key.size());
        if(!host)save_setting(L"Peer",remote);pending.clear();log.clear();details.Text(L"");pattern_started=false;
        std::vector<std::wstring> args{host?L"host":L"receive",L"--name",L"dm-lan",L"--bind",L"auto",L"--parent-pid",std::to_wstring(GetCurrentProcessId())};
        if(host){firewall(true);firewall_owned=true;args.insert(args.end(),{L"--allow-screen",L"--extend",L"--serve",L"--keep-layout",L"--client-mode",L"--frames",debug.IsChecked().Value()?L"3600":L"0",L"--timeout-ms",L"0"});if(input.IsChecked().Value())args.push_back(L"--allow-input");}
        else {args.insert(args.end(),{L"--peer",remote});if(input.IsChecked().Value())args.push_back(L"--control");}
        try{engine.start((directory()/L"dm-view.exe").wstring(),args);}catch(...){if(firewall_owned){firewall(false);firewall_owned=false;}throw;}
        enabled(true);status.Text(host?L"Starting driver...":L"Connecting securely to your server...");
    }
    void tick(){if(test_seconds && GetTickCount64()-started>test_seconds*1000ull){exit();return;}if(GetTickCount64()-last_addresses>5000)refresh_addresses();if(!engine.process)return;pending+=engine.drain();size_t end;
        while((end=pending.find('\n'))!=std::string::npos){auto line=pending.substr(0,end);pending.erase(0,end+1);if(!line.empty()&&line.back()=='\r')line.pop_back();log+=wide(line)+L"\r\n";if(log.size()>24000)log.erase(0,log.size()-18000);
            if(line.starts_with("DRIVER_STARTED")||line.starts_with("LISTENING")||line.starts_with("DRIVER_WAITING")){status.Text(L"Driver running. Waiting for a client connection.");pattern.stop();pattern_started=false;}
            if(line.starts_with("CLIENT_DISCONNECTED"))status.Text(L"Client disconnected. Driver remains running.");
            if(line.starts_with("CLIENT_MODE")){
                const auto requested=line.find("requested="),selected=line.find("selected="),fallback=line.find(" fallback=");
                if(requested!=std::string::npos&&selected!=std::string::npos&&fallback!=std::string::npos){
                    auto wanted=line.substr(requested+10,selected-requested-11),chosen=line.substr(selected+9,fallback-selected-9);
                    find<TextBlock>(L"Mode").Text(L"Client screen: "+wide(wanted)+L". Starting display: "+wide(chosen)+(line.find("fallback=1")!=std::string::npos?L" (limited by driver/codec support).":L". Saved Windows layout takes precedence."));
                }
            }
            if(line.starts_with("VIDEO_STARTED")||line.starts_with("VIDEO_RECEIVING")){status.Text(L"Connected. You can rearrange the display in Windows Settings.");if(host&&debug.IsChecked().Value()&&!pattern_started){pattern.start((directory()/L"dm-display.exe").wstring(),{L"pattern",L"60"});pattern_started=true;}}
            if(line.starts_with("DISPLAY_RECONFIGURING"))status.Text(L"Applying your display changes...");
            if(line.starts_with("DISPLAY_RECONFIGURED"))status.Text(L"Connected. Display changes applied.");
            if(line.starts_with("error:"))status.Text(wide(line.substr(6)));
        }
        if(pending.size()>16384)pending.clear();if(debug.IsChecked().Value())details.Text(log);if(test_report){std::ofstream out(directory()/L"session-check.log");out<<ascii(log);}
        if(WaitForSingleObject(engine.process,0)==WAIT_OBJECT_0){DWORD exit=0;GetExitCodeProcess(engine.process,&exit);engine.stop();pattern.stop();enabled(false);if(firewall_owned){firewall(false);firewall_owned=false;}if(!exit)status.Text(host?L"Driver stopped.":L"Session ended. Ready to connect again.");}
    }
    Shell(Window w):window(w){
        root=Markup::XamlReader::Load(LR"XAML(
<ScrollViewer xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Background="{ThemeResource ApplicationPageBackgroundThemeBrush}">
<StackPanel Padding="32" Spacing="20" MaxWidth="760" HorizontalAlignment="Stretch">
 <StackPanel Spacing="6"><TextBlock x:Name="Title" FontSize="32" FontWeight="SemiBold"/><TextBlock x:Name="Subtitle" TextWrapping="Wrap" Foreground="{ThemeResource TextFillColorSecondaryBrush}"/></StackPanel>
 <Border Padding="20" CornerRadius="12" Background="{ThemeResource CardBackgroundFillColorDefaultBrush}" BorderBrush="{ThemeResource CardStrokeColorDefaultBrush}" BorderThickness="1"><StackPanel Spacing="16">
 <StackPanel Spacing="8"><TextBlock Text="This PC's network addresses" FontWeight="SemiBold"/><ScrollViewer MaxHeight="165" HorizontalScrollBarVisibility="Auto"><TextBlock x:Name="Addresses" IsTextSelectionEnabled="True" FontSize="13"/></ScrollViewer><TextBlock Text="Connections use IPv4. Addresses are detected automatically." FontSize="12" TextWrapping="Wrap" Foreground="{ThemeResource TextFillColorSecondaryBrush}"/></StackPanel>
 <TextBox x:Name="Peer" Header="Server address" PlaceholderText="Enter the server's IPv4 address"/>
 <TextBlock x:Name="Mode" TextWrapping="Wrap" Foreground="{ThemeResource TextFillColorSecondaryBrush}"/>
 <CheckBox x:Name="Input" Content="Mouse, keyboard, touch and pen" IsChecked="True"/>
 <StackPanel Orientation="Horizontal" Spacing="12"><Button x:Name="Start" Style="{StaticResource AccentButtonStyle}"/><Button x:Name="Stop" Content="Disconnect" IsEnabled="False"/></StackPanel>
 <TextBlock x:Name="Status" Text="Pair these computers once to get started." TextWrapping="Wrap"/>
 </StackPanel></Border>
 <Expander x:Name="Pairing" Header="Pair computers" HorizontalAlignment="Stretch" HorizontalContentAlignment="Stretch"><StackPanel Spacing="12"><TextBlock Text="Generate a code, copy it to the other computer, then save on both. Existing pairing is retained until you forget it." TextWrapping="Wrap"/><TextBox x:Name="Code" PlaceholderText="Pairing code" MaxLength="64"/><StackPanel Orientation="Horizontal" Spacing="8"><Button x:Name="Generate" Content="Generate code"/><Button x:Name="Save" Content="Save pairing"/><Button x:Name="Forget" Content="Forget"/></StackPanel></StackPanel></Expander>
 <CheckBox x:Name="Debug" Content="Debug diagnostics"/>
 <TextBox x:Name="Details" Visibility="Collapsed" IsReadOnly="True" AcceptsReturn="True" Height="180" TextWrapping="Wrap"/>
 <TextBlock Text="Direct LAN · Encrypted connection · No audio yet" FontSize="12" Foreground="{ThemeResource TextFillColorSecondaryBrush}"/>
</StackPanel></ScrollViewer>)XAML").as<FrameworkElement>();
        if(const auto* option=wcsstr(GetCommandLineW(),L"--test-duration ")){test_seconds=wcstoul(option+16,nullptr,10);if(!test_seconds||test_seconds>300)throw std::runtime_error("Invalid test duration");}
        window.Content(root);window.Title(host?L"SideScreen Server":L"SideScreen Client");
        find<TextBlock>(L"Title").Text(host?L"SideScreen / Server":L"SideScreen / Client");find<TextBlock>(L"Subtitle").Text(host?L"Extend your desktop onto another computer.":L"Make this screen part of your server's desktop.");
        find<TextBlock>(L"Mode").Text(host?L"Resolution and frame-rate target come from the client's screen. Windows Display Settings remain in control after connecting.":L"Uses this screen's resolution and refresh, within server limits. Resize the display window freely; F11 toggles fullscreen.");
        addresses=find<TextBlock>(L"Addresses");peer=find<TextBox>(L"Peer");code=find<TextBox>(L"Code");details=find<TextBox>(L"Details");status=find<TextBlock>(L"Status");input=find<CheckBox>(L"Input");debug=find<CheckBox>(L"Debug");if(host)debug.Content(box_value(L"Debug: color test and 3,600-frame session"));start_button=find<Button>(L"Start");stop_button=find<Button>(L"Stop");
        refresh_addresses();peer.Text(host?L"":setting(L"Peer",L""));if(host)peer.Visibility(Visibility::Collapsed);
        start_button.Content(box_value(host?L"Start Driver":L"Connect"));stop_button.Content(box_value(host?L"Stop Driver":L"Disconnect"));
        start_button.Click([this](auto&&,auto&&){attempt([&]{start();});});stop_button.Click([this](auto&&,auto&&){attempt([&]{stop();});});
        find<Button>(L"Generate").Click([this](auto&&,auto&&){attempt([&]{dm::security::Key key{};if(BCryptGenRandom(nullptr,key.data(),static_cast<ULONG>(key.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("Cannot generate pairing code");std::wstring value;for(auto b:key){value+=L"0123456789abcdef"[b>>4];value+=L"0123456789abcdef"[b&15];}code.Text(value);SecureZeroMemory(key.data(),key.size());SecureZeroMemory(value.data(),value.size()*sizeof(wchar_t));});});
        find<Button>(L"Save").Click([this](auto&&,auto&&){attempt([&]{auto value=ascii(std::wstring(code.Text()));auto key=dm::security::parse_key(value);SecureZeroMemory(value.data(),value.size());try{dm::security::save_pairing("dm-lan",key);}catch(...){SecureZeroMemory(key.data(),key.size());throw;}SecureZeroMemory(key.data(),key.size());code.Text(L"");paired();});});
        find<Button>(L"Forget").Click([this](auto&&,auto&&){attempt([&]{dm::security::revoke_pairing("dm-lan");status.Text(L"Pairing removed on this computer.");});});
        debug.Checked([this](auto&&,auto&&){details.Visibility(Visibility::Visible);});debug.Unchecked([this](auto&&,auto&&){details.Visibility(Visibility::Collapsed);});
        tray=std::make_unique<Tray>(host,[this]{show();},[this]{attempt([&]{if(active)stop();else if(host)start();});},[this]{exit();});
        window.AppWindow().Closing([this](auto&&,winrt::Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args){if(!quitting && tray->ready){args.Cancel(true);window.AppWindow().Hide();}});
        window.Closed([this](auto&&,auto&&){attempt([&]{stop();});timer.Stop();});
        timer.Interval(std::chrono::milliseconds(200));timer.Tick([this](auto&&,auto&&){attempt([&]{tick();});});timer.Start();paired();
        window.AppWindow().Resize({760,850});
        window.Activate();
    }
};
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
        if(wcsstr(GetCommandLineW(),L"--smoke-test")){if(!shell->start_button.IsEnabled()||shell->stop_button.IsEnabled()||shell->debug.IsChecked().Value())throw std::runtime_error("Bad initial UI state");if(shell->addresses.Text().empty() || (host && shell->peer.Visibility()!=Visibility::Collapsed) || (!host && shell->peer.Visibility()!=Visibility::Visible))throw std::runtime_error("Bad network UI state");shell->timer.Interval(std::chrono::milliseconds(700));shell->timer.Tick([](auto&&,auto&&){shell->timer.Stop();capture_check(shell.get());});}
        else if(wcsstr(GetCommandLineW(),L"--start"))shell->attempt([&]{shell->start();});
    }catch(const hresult_error& e){MessageBoxW(nullptr,e.message().c_str(),L"DM startup",MB_OK|MB_ICONERROR);ExitProcess(1);}catch(const std::exception& e){MessageBoxW(nullptr,wide(e.what()).c_str(),L"DM startup",MB_OK|MB_ICONERROR);ExitProcess(1);}
}
}

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    const bool isolated=wcsstr(GetCommandLineW(),L"--smoke-test")||wcsstr(GetCommandLineW(),L"--test-report");
    HANDLE instance=nullptr;
    if(!isolated){
        instance=CreateMutexW(nullptr,FALSE,host?L"Local\\SideScreen.Server":L"Local\\SideScreen.Client");
        if(!instance || GetLastError()==ERROR_ALREADY_EXISTS){
            for(int i=0;i<40;++i){if(auto existing=FindWindowW(Tray::class_name(host),nullptr)){PostMessageW(existing,Tray::show_message,0,0);if(instance)CloseHandle(instance);return 0;}Sleep(50);}
            if(instance)CloseHandle(instance);return 1;
        }
    }
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    Application::Start([](auto&&){winrt::make<winrt::DM::implementation::App>();});
    shell.reset();if(instance)CloseHandle(instance);return 0;
}
