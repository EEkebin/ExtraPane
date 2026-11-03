#include "SideScreenVersion.h"
struct Shell {
    Window window{nullptr};FrameworkElement root{nullptr},about{nullptr};NavigationView navigation{nullptr};
    std::unique_ptr<RolePage> server,client;std::unique_ptr<Tray> tray;DispatcherTimer timer;
    std::unique_ptr<SettingsPage> settings_page;
    bool quitting=false;ULONGLONG started=GetTickCount64();unsigned test_seconds=0;
    void show(){window.AppWindow().Show();window.Activate();}
    void update(){if(settings_page)settings_page->update();if(tray)tray->update(server->active,client->active);if(!quitting&&!server->active&&tray&&!window.AppWindow().IsVisible())exit();}
    void select(const std::wstring& page){
        auto content=root.FindName(L"PageContent").as<ContentControl>();
        if(page==L"About")content.Content(about);else if(page==L"Settings"){content.Content(settings_page->root);settings_page->enter();}else {auto& role=page==L"Server"?server:client;content.Content(role->root);if(!role->active)role->ready();}
        save_setting(L"Page",page);show();
    }
    void choose(bool host){navigation.SelectedItem(navigation.MenuItems().GetAt(host?0:1));show();}
    void exit(){if(quitting)return;quitting=true;if(settings_page)settings_page->probe.stop();server->attempt([&]{server->stop();});client->attempt([&]{client->stop();});timer.Stop();if(tray)tray->remove();window.Close();}
    Shell(Window w):window(w){
        root=Markup::XamlReader::Load(LR"XAML(
<NavigationView xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" x:Name="Navigation" PaneDisplayMode="Left" OpenPaneLength="210" IsBackButtonVisible="Collapsed" IsSettingsVisible="False" IsPaneToggleButtonVisible="False" IsPaneOpen="True" Background="{ThemeResource ApplicationPageBackgroundThemeBrush}">
 <NavigationView.PaneHeader><TextBlock Text="SideScreen" FontSize="24" FontWeight="SemiBold" Margin="20,24,0,24"/></NavigationView.PaneHeader>
 <NavigationView.MenuItems><NavigationViewItem Content="Server" Tag="Server" Icon="AllApps"/><NavigationViewItem Content="Client" Tag="Client" Icon="Remote"/></NavigationView.MenuItems>
 <NavigationView.FooterMenuItems><NavigationViewItem Content="Settings" Tag="Settings" Icon="Setting"/><NavigationViewItem Content="About" Tag="About" Icon="Help"/></NavigationView.FooterMenuItems>
 <ContentControl x:Name="PageContent" HorizontalContentAlignment="Stretch" VerticalContentAlignment="Stretch"/>
</NavigationView>)XAML").as<FrameworkElement>();
        navigation=root.as<NavigationView>();server=std::make_unique<RolePage>(true);client=std::make_unique<RolePage>(false);
        settings_page=std::make_unique<SettingsPage>(*server,*client);
        about=Markup::XamlReader::Load(LR"XAML(
<ScrollViewer xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"><StackPanel Padding="40" Spacing="20" MaxWidth="650" HorizontalAlignment="Left"><TextBlock Text="About SideScreen" FontSize="32" FontWeight="SemiBold"/><TextBlock Text="Your desktop. More room." FontSize="22"/><TextBlock x:Name="VersionLabel"/><TextBlock Text="One app, either role. Use Server on the PC whose desktop you want to extend, and Client on the second screen. Switching pages keeps both roles running." TextWrapping="Wrap"/><TextBlock Text="The app runs without elevation. Start Driver requests administrator approval for its separate helper. The development display driver must already be installed on the server PC." TextWrapping="Wrap"/><TextBlock Text="Ctrl+Shift+Q disconnects - F10 reveals controls - F11 toggles fullscreen - F8 pauses input" TextWrapping="Wrap"/><HyperlinkButton Content="Source and downloads" NavigateUri="https://github.com/EEkebin/SideScreen"/><TextBlock Text="Created by EEkebin. Audio, microphone passthrough and Windows+K are not implemented yet." TextWrapping="Wrap"/></StackPanel></ScrollViewer>)XAML").as<FrameworkElement>();
        about.FindName(L"VersionLabel").as<TextBlock>().Text(L"Version " SIDESCREEN_VERSION_W L" - Windows x64");
        server->changed=client->changed=[this]{update();};
        navigation.SelectionChanged([this](auto&&,NavigationViewSelectionChangedEventArgs const& args){if(auto item=args.SelectedItem().try_as<NavigationViewItem>())select(std::wstring(unbox_value<hstring>(item.Tag())));});
        window.Content(root);window.Title(L"SideScreen");
        tray=std::make_unique<Tray>([this](bool host){choose(host);},[this]{server->attempt([&]{if(server->active)server->stop();else server->start();});},[this]{client->attempt([&]{client->stop();});},[this]{exit();},[this]{show();});
        window.AppWindow().Closing([this](auto&&,winrt::Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args){if(!quitting&&server->active&&tray->ready){args.Cancel(true);window.AppWindow().Hide();}});
        window.Closed([this](auto&&,auto&&){quitting=true;if(settings_page)settings_page->probe.stop();server->stop();client->stop();timer.Stop();if(tray)tray->remove();});
        if(const auto* option=wcsstr(GetCommandLineW(),L"--test-duration ")){test_seconds=wcstoul(option+16,nullptr,10);if(!test_seconds||test_seconds>300)throw std::runtime_error("Invalid test duration");}
        timer.Interval(std::chrono::milliseconds(200));timer.Tick([this](auto&&,auto&&){if(test_seconds&&GetTickCount64()-started>test_seconds*1000ull){exit();return;}server->attempt([&]{server->tick();});client->attempt([&]{client->tick();settings_page->tick();});});timer.Start();
        window.AppWindow().SetIcon((directory()/L"SideScreen.ico").wstring());window.AppWindow().Resize({1000,850});window.Activate();
        const auto page=setting(L"Page",L"Server");navigation.SelectedItem(navigation.MenuItems().GetAt(wcsstr(GetCommandLineW(),L"--client")?1:wcsstr(GetCommandLineW(),L"--server")?0:page==L"Client"?1:0));update();
    }
};
