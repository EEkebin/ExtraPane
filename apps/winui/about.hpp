#pragma once
#include "ExtraPaneVersion.h"
#include <dm/update.hpp>
#include <thread>
#include <mutex>

struct AboutPage {
    struct Job {std::atomic<bool> cancelled{false},done{false};std::atomic<unsigned> progress{0};std::optional<dm::update::Release> release;std::filesystem::path installer;std::wstring error;bool downloading=false;};
    FrameworkElement root{nullptr};std::shared_ptr<Job> job;std::optional<dm::update::Release> latest;
    std::function<void(const std::filesystem::path&,const std::string&)> install;
    template<class T>T find(const wchar_t* name){return root.FindName(name).as<T>();}
    ~AboutPage(){if(job)job->cancelled.store(true);}
    void status(const std::wstring& text){find<TextBlock>(L"UpdateStatus").Text(text);}
    void busy(bool value){find<Button>(L"Check").IsEnabled(!value);find<ComboBox>(L"Channel").IsEnabled(!value);find<Button>(L"Update").IsEnabled(!value&&latest&&latest->version!=EXTRAPANE_VERSION);find<ProgressBar>(L"Progress").Visibility(value?Visibility::Visible:Visibility::Collapsed);find<Button>(L"Cancel").Visibility(value?Visibility::Visible:Visibility::Collapsed);}
    void begin(bool download){
        if(job)return;if(download&&!latest)return;
        auto work=std::make_shared<Job>();work->downloading=download;job=work;const auto channel=find<ComboBox>(L"Channel").SelectedIndex()==0?dm::update::Channel::stable:dm::update::Channel::prerelease;const auto selected=latest;
        busy(true);find<ProgressBar>(L"Progress").IsIndeterminate(!download);find<ProgressBar>(L"Progress").Value(0);status(download?L"Downloading and verifying the installer...":L"Checking GitHub releases...");
        std::thread([work,channel,selected]{try{winrt::init_apartment(winrt::apartment_type::multi_threaded);if(work->downloading)work->installer=dm::update::download(*selected,work->cancelled,work->progress);else work->release=dm::update::check(channel,work->cancelled);winrt::uninit_apartment();}catch(const hresult_error& e){work->error=e.message();}catch(const std::exception& e){work->error=wide(e.what());}work->done.store(true);}).detach();
    }
    void tick(){
        if(!job)return;find<ProgressBar>(L"Progress").Value(job->progress.load());if(!job->done.load())return;auto finished=std::exchange(job,{});busy(false);
        if(finished->cancelled.load()){if(!finished->installer.empty()){std::error_code ignored;std::filesystem::remove(finished->installer,ignored);std::filesystem::remove(finished->installer.parent_path(),ignored);}status(L"Update cancelled.");return;}
        if(!finished->error.empty()){status(finished->error);return;}
        if(finished->downloading){status(L"Installer verified. Closing ExtraPane to install...");try{install(finished->installer,latest->digest);}catch(const std::exception& e){status(wide(e.what()));}return;}
        latest=finished->release;find<TextBlock>(L"CurrentVersion").Text(latest?wide(latest->version):L"Not published");busy(false);
        if(!latest){status(find<ComboBox>(L"Channel").SelectedIndex()==0?L"No stable release has been published yet. Choose Prerelease to try the current builds.":L"No prerelease installer is available.");return;}
        const auto order=dm::update::compare_versions(latest->version,EXTRAPANE_VERSION);status(order==0?L"You're up to date.":order>0?L"A new version is ready to install.":L"This channel's current version is older than your running version. Update will install that version.");
    }
    AboutPage(){
        root=Markup::XamlReader::Load(LR"XAML(
<ScrollViewer xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
 <StackPanel Padding="36" Spacing="24" MaxWidth="760" HorizontalAlignment="Stretch">
  <StackPanel Orientation="Horizontal" Spacing="20"><Image x:Name="Brand" Width="88" Height="88"/><StackPanel VerticalAlignment="Center" Spacing="4"><TextBlock Text="ExtraPane" FontSize="34" FontWeight="SemiBold"/><TextBlock Text="Your desktop. More room." FontSize="18" Foreground="{ThemeResource TextFillColorSecondaryBrush}"/></StackPanel></StackPanel>
  <TextBlock Text="Give your desktop another screen. Connect over your local network and make room for the way you work—with mouse, keyboard, touch and pen." FontSize="15" TextWrapping="Wrap"/>
  <Border Padding="22" CornerRadius="12" Background="{ThemeResource CardBackgroundFillColorDefaultBrush}" BorderBrush="{ThemeResource CardStrokeColorDefaultBrush}" BorderThickness="1"><StackPanel Spacing="16">
   <StackPanel Orientation="Horizontal" Spacing="10"><FontIcon Glyph="&#xE895;" FontSize="18"/><TextBlock Text="Updates" FontSize="20" FontWeight="SemiBold"/></StackPanel>
   <Grid ColumnSpacing="24" RowSpacing="8"><Grid.ColumnDefinitions><ColumnDefinition Width="Auto"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions><Grid.RowDefinitions><RowDefinition/><RowDefinition/></Grid.RowDefinitions><TextBlock Text="Running version" Foreground="{ThemeResource TextFillColorSecondaryBrush}"/><TextBlock x:Name="RunningVersion" Grid.Column="1" IsTextSelectionEnabled="True"/><TextBlock Grid.Row="1" Text="Current version" Foreground="{ThemeResource TextFillColorSecondaryBrush}"/><TextBlock x:Name="CurrentVersion" Grid.Row="1" Grid.Column="1" Text="Not checked" IsTextSelectionEnabled="True"/></Grid>
   <ComboBox x:Name="Channel" Header="Update channel" HorizontalAlignment="Stretch"><ComboBoxItem Content="Stable release"/><ComboBoxItem Content="Prerelease"/></ComboBox>
   <TextBlock x:Name="ChannelNote" FontSize="12" TextWrapping="Wrap" Foreground="{ThemeResource TextFillColorSecondaryBrush}"/>
   <StackPanel Orientation="Horizontal" Spacing="10"><Button x:Name="Check" Content="Check for updates"/><Button x:Name="Update" Content="Update" Style="{StaticResource AccentButtonStyle}" IsEnabled="False"/><Button x:Name="Cancel" Content="Cancel" Visibility="Collapsed"/></StackPanel>
   <ProgressBar x:Name="Progress" Visibility="Collapsed" Maximum="100"/><TextBlock x:Name="UpdateStatus" Text="Choose a channel, then check for updates." TextWrapping="Wrap"/>
   <TextBlock Text="Update downloads the installer, closes ExtraPane and starts setup. Your settings are kept. Portable copies use the installer too." FontSize="12" TextWrapping="Wrap" Foreground="{ThemeResource TextFillColorSecondaryBrush}"/>
  </StackPanel></Border>
  <Border Padding="12" CornerRadius="12" Background="{ThemeResource CardBackgroundFillColorDefaultBrush}" BorderBrush="{ThemeResource CardStrokeColorDefaultBrush}" BorderThickness="1"><Grid ColumnSpacing="12" RowSpacing="8"><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions><Grid.RowDefinitions><RowDefinition/><RowDefinition/></Grid.RowDefinitions>
   <HyperlinkButton NavigateUri="https://github.com/EEkebin/ExtraPane#readme" HorizontalAlignment="Stretch" HorizontalContentAlignment="Left"><StackPanel Orientation="Horizontal" Spacing="12"><FontIcon Glyph="&#xE82D;" FontSize="18"/><TextBlock Text="README"/></StackPanel></HyperlinkButton>
   <HyperlinkButton Grid.Column="1" NavigateUri="https://github.com/EEkebin/ExtraPane/releases" HorizontalAlignment="Stretch" HorizontalContentAlignment="Left"><StackPanel Orientation="Horizontal" Spacing="12"><FontIcon Glyph="&#xE81C;" FontSize="18"/><TextBlock Text="Changelog"/></StackPanel></HyperlinkButton>
   <HyperlinkButton Grid.Row="1" NavigateUri="https://github.com/EEkebin/ExtraPane" HorizontalAlignment="Stretch" HorizontalContentAlignment="Left"><StackPanel Orientation="Horizontal" Spacing="12"><FontIcon Glyph="&#xE943;" FontSize="18"/><TextBlock Text="GitHub repository"/></StackPanel></HyperlinkButton>
   <HyperlinkButton Grid.Row="1" Grid.Column="1" NavigateUri="https://github.com/EEkebin/ExtraPane/blob/main/docs/LICENSING.md" HorizontalAlignment="Stretch" HorizontalContentAlignment="Left"><StackPanel Orientation="Horizontal" Spacing="12"><FontIcon Glyph="&#xE8A5;" FontSize="18"/><TextBlock Text="License &amp; notices"/></StackPanel></HyperlinkButton>
  </Grid></Border>
  <TextBlock Text="Made by EEkebin" FontSize="13" Foreground="{ThemeResource TextFillColorSecondaryBrush}"/>
 </StackPanel>
</ScrollViewer>)XAML").as<FrameworkElement>();
        find<TextBlock>(L"RunningVersion").Text(EXTRAPANE_VERSION_W);
        find<Image>(L"Brand").Source(winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage(winrt::Windows::Foundation::Uri(L"file:///"+directory().generic_wstring()+L"/ExtraPane.png")));
        find<ComboBox>(L"Channel").SelectedIndex(setting(L"UpdateChannel",std::string_view(EXTRAPANE_VERSION).find("-pre.")!=std::string_view::npos?L"pre":L"stable")==L"stable"?0:1);
        auto changed=[this]{latest.reset();find<TextBlock>(L"CurrentVersion").Text(L"Not checked");const bool pre=find<ComboBox>(L"Channel").SelectedIndex()==1;save_setting(L"UpdateChannel",pre?L"pre":L"stable");find<TextBlock>(L"ChannelNote").Text(pre?L"Early builds with the newest changes. Updates stay on the prerelease channel.":L"Stable releases only. Prereleases are never installed from this channel.");status(L"Choose a channel, then check for updates.");busy(false);};changed();
        find<ComboBox>(L"Channel").SelectionChanged([changed](auto&&,auto&&){changed();});find<Button>(L"Check").Click([this](auto&&,auto&&){begin(false);});find<Button>(L"Update").Click([this](auto&&,auto&&){begin(true);});find<Button>(L"Cancel").Click([this](auto&&,auto&&){if(job){job->cancelled.store(true);status(L"Cancelling update...");}});
    }
};
