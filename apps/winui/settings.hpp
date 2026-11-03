struct SettingsPage {
    RolePage& server;RolePage& client;FrameworkElement root{nullptr};
    ComboBox renderer{nullptr};ComboBoxItem d3d11{nullptr},d3d12{nullptr};TextBlock support{nullptr},timing{nullptr};Button refresh{nullptr};
    Child probe;bool checked=false,complete=false,syncing=false,saw_d3d12=false;ULONGLONG started=0;std::string pending;
    template<class T>T find(const wchar_t* name){return root.FindName(name).as<T>();}
    void message(const std::wstring& text,bool warning){support.Text(text);if(warning)support.Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Color{255,210,155,30}));else support.ClearValue(TextBlock::ForegroundProperty());}
    void update(){
        if(!client.renderer_warning.empty()){d3d12.IsEnabled(false);message(L"D3D12 unavailable for this session: "+client.renderer_warning+L". Using D3D11.",true);if(renderer.SelectedIndex()==1){syncing=true;renderer.SelectedIndex(0);syncing=false;save_setting(L"Renderer",L"d3d11");}}
        renderer.IsEnabled(complete&&!client.active);refresh.IsEnabled(!probe.process&&!client.active);timing.Text(client.active?L"Disconnect the Client before changing its renderer. Your current session keeps its renderer.":L"Saved on this PC. Applies when the Client connects; D3D11 is the default.");
    }
    void start_probe(){
        if(probe.process)return;checked=true;complete=false;saw_d3d12=false;pending.clear();client.renderer_warning.clear();started=GetTickCount64();d3d11.IsEnabled(true);d3d12.IsEnabled(false);message(L"Checking renderer support on this PC...",false);
        try{probe.start((directory()/L"dm-view.exe").wstring(),{L"renderer-info"});}
        catch(const std::exception& e){complete=true;message(L"Renderer check failed: "+wide(e.what()),true);}
        update();
    }
    void enter(){if(!checked)start_probe();update();}
    void drain(){
        pending+=probe.drain();size_t end;
        while((end=pending.find('\n'))!=std::string::npos){auto line=pending.substr(0,end);pending.erase(0,end+1);if(!line.empty()&&line.back()=='\r')line.pop_back();
            constexpr std::string_view yes="RENDERER_INFO d3d12 available ",no="RENDERER_INFO d3d12 unavailable ";
            if(line.starts_with(yes)){saw_d3d12=true;d3d12.IsEnabled(true);const auto adapter=line.substr(yes.size());message(L"D3D12 is available on "+wide(adapter.substr(0,adapter.find(';')))+L".",false);}
            if(line.starts_with(no)){saw_d3d12=true;d3d12.IsEnabled(false);message(L"D3D12 unavailable: "+wide(line.substr(no.size())),true);}
            if(line.starts_with("RENDERER_INFO d3d11 unavailable "))d3d11.IsEnabled(false);
        }
        if(pending.size()>16384)pending.clear();
    }
    void tick(){if(!probe.process)return;drain();
        const bool done=WaitForSingleObject(probe.process,0)==WAIT_OBJECT_0;
        if(done||GetTickCount64()-started>15000){if(done)drain();probe.stop();complete=true;
            if(!saw_d3d12){d3d12.IsEnabled(false);message(done?L"D3D12 check did not return a result. Retry or update the graphics driver.":L"D3D12 support check timed out. Retry after checking the graphics driver.",true);}
            if(!d3d12.IsEnabled()&&renderer.SelectedIndex()==1){syncing=true;renderer.SelectedIndex(0);syncing=false;save_setting(L"Renderer",L"d3d11");}
            update();
        }
    }
    SettingsPage(RolePage& host,RolePage& receiver):server(host),client(receiver){
        root=Markup::XamlReader::Load(LR"XAML(
<ScrollViewer xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" HorizontalContentAlignment="Stretch" HorizontalScrollBarVisibility="Disabled" Background="{ThemeResource ApplicationPageBackgroundThemeBrush}">
 <StackPanel Padding="32" Spacing="24" MaxWidth="760" HorizontalAlignment="Stretch">
  <StackPanel Spacing="6"><TextBlock Text="Settings" FontSize="32" FontWeight="SemiBold"/><TextBlock Text="Set up either role on this PC before you connect." TextWrapping="Wrap" Foreground="{ThemeResource TextFillColorSecondaryBrush}"/></StackPanel>
  <Border Padding="20" CornerRadius="12" Background="{ThemeResource CardBackgroundFillColorDefaultBrush}" BorderBrush="{ThemeResource CardStrokeColorDefaultBrush}" BorderThickness="1"><StackPanel Spacing="14">
   <TextBlock Text="Client" FontSize="20" FontWeight="SemiBold"/><TextBlock Text="How the received desktop is displayed on this PC." TextWrapping="Wrap" Foreground="{ThemeResource TextFillColorSecondaryBrush}"/>
   <ComboBox x:Name="Renderer" Header="Renderer" HorizontalAlignment="Stretch"><ComboBoxItem x:Name="D3D11" Content="D3D11 (default)"/><ComboBoxItem x:Name="D3D12" Content="D3D12" IsEnabled="False"/></ComboBox>
   <TextBlock x:Name="Timing" TextWrapping="Wrap" FontSize="12" Foreground="{ThemeResource TextFillColorSecondaryBrush}"/>
   <TextBlock x:Name="Support" TextWrapping="Wrap" IsTextSelectionEnabled="True"/>
   <Button x:Name="Refresh" Content="Check renderer support"/>
   <StackPanel x:Name="ClientOptions" Spacing="8"/>
  </StackPanel></Border>
  <Border Padding="20" CornerRadius="12" Background="{ThemeResource CardBackgroundFillColorDefaultBrush}" BorderBrush="{ThemeResource CardStrokeColorDefaultBrush}" BorderThickness="1"><StackPanel Spacing="14">
   <TextBlock Text="Server" FontSize="20" FontWeight="SemiBold"/><TextBlock Text="Capture uses D3D11 with H.264 encoding. The client renderer does not change server capture." TextWrapping="Wrap" Foreground="{ThemeResource TextFillColorSecondaryBrush}"/>
   <StackPanel x:Name="ServerOptions" Spacing="8"/>
   <TextBlock Text="Stop Driver before changing server options. Debug options are off at app startup." FontSize="12" TextWrapping="Wrap" Foreground="{ThemeResource TextFillColorSecondaryBrush}"/>
  </StackPanel></Border>
 </StackPanel>
</ScrollViewer>)XAML").as<FrameworkElement>();
        renderer=find<ComboBox>(L"Renderer");d3d11=find<ComboBoxItem>(L"D3D11");d3d12=find<ComboBoxItem>(L"D3D12");support=find<TextBlock>(L"Support");timing=find<TextBlock>(L"Timing");refresh=find<Button>(L"Refresh");
        renderer.SelectedIndex(setting(L"Renderer",L"d3d11")==L"d3d12"?1:0);
        renderer.SelectionChanged([this](auto&&,auto&&){if(syncing)return;client.attempt([&]{save_setting(L"Renderer",renderer.SelectedIndex()==1?L"d3d12":L"d3d11");});});
        refresh.Click([this](auto&&,auto&&){client.attempt([&]{start_probe();});});
        auto move=[&](RolePage& role,CheckBox control,const wchar_t* source,const wchar_t* target){auto parent=role.find<StackPanel>(source);uint32_t index=0;if(!parent.Children().IndexOf(control,index))throw std::runtime_error("Settings option parent missing");parent.Children().RemoveAt(index);find<StackPanel>(target).Children().Append(control);};
        client.input.Content(box_value(L"Send mouse, keyboard, touch and pen"));server.input.Content(box_value(L"Allow clients to control mouse, keyboard, touch and pen"));
        move(client,client.input,L"ConnectionOptions",L"ClientOptions");move(client,client.debug,L"RoleOptions",L"ClientOptions");move(server,server.input,L"ConnectionOptions",L"ServerOptions");move(server,server.debug,L"RoleOptions",L"ServerOptions");update();
    }
};
