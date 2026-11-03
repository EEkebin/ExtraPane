struct SettingsPage {
    RolePage& server;RolePage& client;FrameworkElement root{nullptr};
    ComboBox renderer{nullptr};ComboBoxItem d3d11{nullptr},d3d12{nullptr},vulkan{nullptr};TextBlock support{nullptr},vulkan_support{nullptr},timing{nullptr};Button refresh{nullptr};
    Child probe;bool checked=false,complete=false,syncing=false,saw_d3d12=false,saw_vulkan=false;ULONGLONG started=0;std::string pending;
    template<class T>T find(const wchar_t* name){return root.FindName(name).as<T>();}
    void message(const std::wstring& text,bool warning,bool for_vulkan=false){auto target=for_vulkan?vulkan_support:support;target.Text(text);if(warning)target.Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Color{255,210,155,30}));else target.ClearValue(TextBlock::ForegroundProperty());}
    void update(){
        if(!client.renderer_warning.empty()){const bool vk=client.renderer_failed==L"Vulkan";(vk?vulkan:d3d12).IsEnabled(false);message(client.renderer_failed+L" unavailable for this session: "+client.renderer_warning+L". Using D3D11.",true,vk);if(renderer.SelectedIndex()==(vk?2:1)){syncing=true;renderer.SelectedIndex(0);syncing=false;save_setting(L"Renderer",L"d3d11");}}
        renderer.IsEnabled(complete&&!client.active);refresh.IsEnabled(!probe.process&&!client.active);timing.Text(client.active?L"Disconnect the Client before changing its renderer. Your current session keeps its renderer.":L"Saved on this PC. Applies when the Client connects; D3D11 is the default.");
    }
    void start_probe(){
        if(probe.process)return;checked=true;complete=false;saw_d3d12=saw_vulkan=false;pending.clear();client.renderer_warning.clear();client.renderer_failed.clear();started=GetTickCount64();d3d11.IsEnabled(true);d3d12.IsEnabled(false);vulkan.IsEnabled(false);message(L"Checking renderer support on this PC...",false);message(L"Checking Vulkan support...",false,true);
        try{probe.start((directory()/L"dm-view.exe").wstring(),{L"renderer-info"});}
        catch(const std::exception& e){complete=true;message(L"Renderer check failed: "+wide(e.what()),true);message(L"Vulkan check failed: "+wide(e.what()),true,true);}
        update();
    }
    void enter(){if(!checked)start_probe();update();}
    void drain(){
        pending+=probe.drain();size_t end;
        while((end=pending.find('\n'))!=std::string::npos){auto line=pending.substr(0,end);pending.erase(0,end+1);if(!line.empty()&&line.back()=='\r')line.pop_back();
            constexpr std::string_view yes="RENDERER_INFO d3d12 available ",no="RENDERER_INFO d3d12 unavailable ";
            if(line.starts_with(yes)){saw_d3d12=true;d3d12.IsEnabled(true);const auto adapter=line.substr(yes.size());message(L"D3D12 is available on "+wide(adapter.substr(0,adapter.find(';')))+L".",false);}
            if(line.starts_with(no)){saw_d3d12=true;d3d12.IsEnabled(false);message(L"D3D12 unavailable: "+wide(line.substr(no.size())),true);}
            constexpr std::string_view vk_yes="RENDERER_INFO vulkan available ",vk_no="RENDERER_INFO vulkan unavailable ";
            if(line.starts_with(vk_yes)){saw_vulkan=true;vulkan.IsEnabled(true);const auto adapter=line.substr(vk_yes.size());message(L"Vulkan is available on "+wide(adapter.substr(0,adapter.find(';')))+L".",false,true);}
            if(line.starts_with(vk_no)){saw_vulkan=true;vulkan.IsEnabled(false);message(L"Vulkan unavailable: "+wide(line.substr(vk_no.size())),true,true);}
            if(line.starts_with("RENDERER_INFO d3d11 unavailable "))d3d11.IsEnabled(false);
        }
        if(pending.size()>16384)pending.clear();
    }
    void tick(){if(!probe.process)return;drain();
        const bool done=WaitForSingleObject(probe.process,0)==WAIT_OBJECT_0;
        if(done||GetTickCount64()-started>15000){if(done)drain();probe.stop();complete=true;
            if(!saw_d3d12){d3d12.IsEnabled(false);message(done?L"D3D12 check did not return a result. Retry or update the graphics driver.":L"D3D12 support check timed out. Retry after checking the graphics driver.",true);}
            if(!saw_vulkan){vulkan.IsEnabled(false);message(done?L"Vulkan check did not return a result. Retry or update the graphics driver.":L"Vulkan support check timed out. Retry after checking the graphics driver.",true,true);}
            if((!d3d12.IsEnabled()&&renderer.SelectedIndex()==1)||(!vulkan.IsEnabled()&&renderer.SelectedIndex()==2)){syncing=true;renderer.SelectedIndex(0);syncing=false;save_setting(L"Renderer",L"d3d11");}
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
   <ComboBox x:Name="Renderer" Header="Renderer" HorizontalAlignment="Stretch"><ComboBoxItem x:Name="D3D11" Content="D3D11 (default)"/><ComboBoxItem x:Name="D3D12" Content="D3D12" IsEnabled="False"/><ComboBoxItem x:Name="Vulkan" Content="Vulkan" IsEnabled="False"/></ComboBox>
   <TextBlock x:Name="Timing" TextWrapping="Wrap" FontSize="12" Foreground="{ThemeResource TextFillColorSecondaryBrush}"/>
   <TextBlock x:Name="Support" TextWrapping="Wrap" IsTextSelectionEnabled="True"/>
   <TextBlock x:Name="VulkanSupport" TextWrapping="Wrap" IsTextSelectionEnabled="True"/>
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
        renderer=find<ComboBox>(L"Renderer");d3d11=find<ComboBoxItem>(L"D3D11");d3d12=find<ComboBoxItem>(L"D3D12");vulkan=find<ComboBoxItem>(L"Vulkan");support=find<TextBlock>(L"Support");vulkan_support=find<TextBlock>(L"VulkanSupport");timing=find<TextBlock>(L"Timing");refresh=find<Button>(L"Refresh");
        const auto saved_renderer=renderer_setting();renderer.SelectedIndex(saved_renderer==L"vulkan"?2:saved_renderer==L"d3d12"?1:0);
        renderer.SelectionChanged([this](auto&&,auto&&){if(syncing)return;client.attempt([&]{save_setting(L"Renderer",renderer.SelectedIndex()==2?L"vulkan":renderer.SelectedIndex()==1?L"d3d12":L"d3d11");});});
        refresh.Click([this](auto&&,auto&&){client.attempt([&]{start_probe();});});
        auto move=[&](RolePage& role,CheckBox control,const wchar_t* source,const wchar_t* target){auto parent=role.find<StackPanel>(source);uint32_t index=0;if(!parent.Children().IndexOf(control,index))throw std::runtime_error("Settings option parent missing");parent.Children().RemoveAt(index);find<StackPanel>(target).Children().Append(control);};
        client.input.Content(box_value(L"Send mouse, keyboard, touch and pen"));server.input.Content(box_value(L"Allow clients to control mouse, keyboard, touch and pen"));
        move(client,client.input,L"ConnectionOptions",L"ClientOptions");move(client,client.debug,L"RoleOptions",L"ClientOptions");move(server,server.input,L"ConnectionOptions",L"ServerOptions");move(server,server.debug,L"RoleOptions",L"ServerOptions");update();
    }
};
