#include "backend.hpp"
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
    TextBox bind{nullptr},peer{nullptr},code{nullptr},details{nullptr};TextBlock status{nullptr};
    CheckBox input{nullptr},debug{nullptr};Button start_button{nullptr},stop_button{nullptr};
    ULONGLONG started=GetTickCount64();unsigned test_seconds=0;bool test_report=wcsstr(GetCommandLineW(),L"--test-report")!=nullptr;
    Child engine,pattern;bool active=false,pattern_started=false;std::string pending;std::wstring log;
    template<class T>T find(const wchar_t* name){return root.FindName(name).as<T>();}
    void attempt(const std::function<void()>& action){try{action();}catch(const hresult_error& e){status.Text(e.message());}catch(const std::exception& e){status.Text(wide(e.what()));}}
    void enabled(bool running){active=running;bind.IsEnabled(!running);peer.IsEnabled(!running);input.IsEnabled(!running);debug.IsEnabled(!running);start_button.IsEnabled(!running);stop_button.IsEnabled(running);find<Expander>(L"Pairing").IsEnabled(!running);}
    void paired(){attempt([&]{auto key=dm::security::load_pairing("dm-lan");SecureZeroMemory(key.data(),key.size());status.Text(L"Paired and ready.");});}
    void stop(){engine.stop();pattern.stop();enabled(false);firewall(false);status.Text(L"Disconnected. Ready when you are.");}
    void start(){
        const std::wstring local(bind.Text()),remote(peer.Text());ipv4(local);ipv4(remote);
        auto key=dm::security::load_pairing("dm-lan");SecureZeroMemory(key.data(),key.size());
        save_setting(L"Bind",local);save_setting(L"Peer",remote);pending.clear();log.clear();details.Text(L"");pattern_started=false;
        std::vector<std::wstring> args{host?L"host":L"receive",L"--name",L"dm-lan",L"--bind",local,L"--parent-pid",std::to_wstring(GetCurrentProcessId())};
        if(host){firewall(true,local,remote);args.insert(args.end(),{L"--allow-screen",L"--extend",L"--keep-layout",L"--client-mode",L"--frames",debug.IsChecked().Value()?L"3600":L"0",L"--timeout-ms",L"0"});if(input.IsChecked().Value())args.push_back(L"--allow-input");}
        else {args.insert(args.end(),{L"--peer",remote});if(input.IsChecked().Value())args.push_back(L"--control");}
        try{engine.start((directory()/L"dm-view.exe").wstring(),args);}catch(...){firewall(false);throw;}
        enabled(true);status.Text(host?L"Waiting for the client. Its screen chooses the starting resolution and refresh.":L"Connecting securely to your server...");
    }
    void tick(){if(test_seconds && GetTickCount64()-started>test_seconds*1000ull){window.Close();return;}if(!engine.process)return;pending+=engine.drain();size_t end;
        while((end=pending.find('\n'))!=std::string::npos){auto line=pending.substr(0,end);pending.erase(0,end+1);log+=wide(line)+L"\r\n";if(log.size()>24000)log.erase(0,log.size()-18000);
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
        if(WaitForSingleObject(engine.process,0)==WAIT_OBJECT_0){DWORD exit=0;GetExitCodeProcess(engine.process,&exit);engine.stop();pattern.stop();enabled(false);firewall(false);if(!exit)status.Text(L"Session ended. Ready to connect again.");}
    }
    Shell(Window w):window(w){
        root=Markup::XamlReader::Load(LR"XAML(
<ScrollViewer xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Background="{ThemeResource ApplicationPageBackgroundThemeBrush}">
<StackPanel Padding="32" Spacing="20" MaxWidth="760" HorizontalAlignment="Stretch">
 <StackPanel Spacing="6"><TextBlock x:Name="Title" FontSize="32" FontWeight="SemiBold"/><TextBlock x:Name="Subtitle" TextWrapping="Wrap" Foreground="{ThemeResource TextFillColorSecondaryBrush}"/></StackPanel>
 <Border Padding="20" CornerRadius="12" Background="{ThemeResource CardBackgroundFillColorDefaultBrush}" BorderBrush="{ThemeResource CardStrokeColorDefaultBrush}" BorderThickness="1"><StackPanel Spacing="16">
 <TextBox x:Name="Bind" Header="This computer's LAN address" PlaceholderText="192.168.1.10"/>
 <TextBox x:Name="Peer" Header="Other computer's LAN address" PlaceholderText="192.168.1.20"/>
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
        window.Content(root);window.Title(host?L"DM Server":L"DM Client");
        find<TextBlock>(L"Title").Text(host?L"DM / Server":L"DM / Client");find<TextBlock>(L"Subtitle").Text(host?L"Extend your desktop onto another computer.":L"Make this screen part of your server's desktop.");
        find<TextBlock>(L"Mode").Text(host?L"Resolution and frame-rate target come from the client's screen. Windows Display Settings remain in control after connecting.":L"Uses this screen's resolution and refresh, within server limits. Resize the display window freely; F11 toggles fullscreen.");
        bind=find<TextBox>(L"Bind");peer=find<TextBox>(L"Peer");code=find<TextBox>(L"Code");details=find<TextBox>(L"Details");status=find<TextBlock>(L"Status");input=find<CheckBox>(L"Input");debug=find<CheckBox>(L"Debug");if(host)debug.Content(box_value(L"Debug: color test and 3,600-frame session"));start_button=find<Button>(L"Start");stop_button=find<Button>(L"Stop");
        std::wstring local;for(const auto& nic:dm::net::interfaces())if(nic.up&&nic.address!="127.0.0.1"){local=wide(nic.address);break;}
        bind.Text(setting(L"Bind",local));peer.Text(setting(L"Peer",L""));start_button.Content(box_value(host?L"Start display":L"Connect"));
        start_button.Click([this](auto&&,auto&&){attempt([&]{start();});});stop_button.Click([this](auto&&,auto&&){attempt([&]{stop();});});
        find<Button>(L"Generate").Click([this](auto&&,auto&&){attempt([&]{dm::security::Key key{};if(BCryptGenRandom(nullptr,key.data(),static_cast<ULONG>(key.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("Cannot generate pairing code");std::wstring value;for(auto b:key){value+=L"0123456789abcdef"[b>>4];value+=L"0123456789abcdef"[b&15];}code.Text(value);SecureZeroMemory(key.data(),key.size());SecureZeroMemory(value.data(),value.size()*sizeof(wchar_t));});});
        find<Button>(L"Save").Click([this](auto&&,auto&&){attempt([&]{auto value=ascii(std::wstring(code.Text()));auto key=dm::security::parse_key(value);SecureZeroMemory(value.data(),value.size());try{dm::security::save_pairing("dm-lan",key);}catch(...){SecureZeroMemory(key.data(),key.size());throw;}SecureZeroMemory(key.data(),key.size());code.Text(L"");paired();});});
        find<Button>(L"Forget").Click([this](auto&&,auto&&){attempt([&]{dm::security::revoke_pairing("dm-lan");status.Text(L"Pairing removed on this computer.");});});
        debug.Checked([this](auto&&,auto&&){details.Visibility(Visibility::Visible);});debug.Unchecked([this](auto&&,auto&&){details.Visibility(Visibility::Collapsed);});
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
        ui->timer.Stop();ui->window.Close();
    } catch(...) {ExitProcess(3);}
}
std::unique_ptr<Shell> shell;
}
namespace winrt::DM::implementation {
App::App(){}
void App::OnLaunched(LaunchActivatedEventArgs const&){
    try{Resources().MergedDictionaries().Append(XamlControlsResources());window=Window();shell=std::make_unique<Shell>(window);
        if(wcsstr(GetCommandLineW(),L"--smoke-test")){if(!shell->start_button.IsEnabled()||shell->stop_button.IsEnabled()||shell->debug.IsChecked().Value())throw std::runtime_error("Bad initial UI state");shell->timer.Interval(std::chrono::milliseconds(700));shell->timer.Tick([](auto&&,auto&&){shell->timer.Stop();capture_check(shell.get());});}
        else if(wcsstr(GetCommandLineW(),L"--start"))shell->attempt([&]{shell->start();});
    }catch(const hresult_error& e){MessageBoxW(nullptr,e.message().c_str(),L"DM startup",MB_OK|MB_ICONERROR);ExitProcess(1);}catch(const std::exception& e){MessageBoxW(nullptr,wide(e.what()).c_str(),L"DM startup",MB_OK|MB_ICONERROR);ExitProcess(1);}
}
}

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    Application::Start([](auto&&){winrt::make<winrt::DM::implementation::App>();});
    shell.reset();return 0;
}
