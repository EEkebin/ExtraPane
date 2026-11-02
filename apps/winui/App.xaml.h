#pragma once
#include "pch.h"

namespace winrt::DM::implementation {
struct App : Microsoft::UI::Xaml::ApplicationT<App,Microsoft::UI::Xaml::Markup::IXamlMetadataProvider> {
    App();
    Microsoft::UI::Xaml::XamlTypeInfo::XamlControlsXamlMetaDataProvider provider{nullptr};
    void ensure_provider(){if(!provider)provider=Microsoft::UI::Xaml::XamlTypeInfo::XamlControlsXamlMetaDataProvider();}
    auto GetXamlType(Windows::UI::Xaml::Interop::TypeName const& type){ensure_provider();return provider.GetXamlType(type);}
    auto GetXamlType(hstring const& name){ensure_provider();return provider.GetXamlType(name);}
    auto GetXmlnsDefinitions(){ensure_provider();return provider.GetXmlnsDefinitions();}
    void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);
    Microsoft::UI::Xaml::Window window{nullptr};
};
}
