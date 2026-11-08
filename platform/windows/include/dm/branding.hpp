#pragma once

// Visible native Windows branding only. Persisted IDs, protocol magic and
// driver package identities deliberately do not follow display-name changes.
namespace dm::branding {
inline constexpr wchar_t product[] = L"ExtraPane";
inline constexpr wchar_t display_title[] = L"ExtraPane Display - F8 pauses input - F11 fullscreen";
inline constexpr wchar_t display_view_title[] = L"ExtraPane Display - View only - F11 fullscreen";
inline constexpr wchar_t display_paused_title[] = L"ExtraPane Display - Input paused - F8 resumes input";
inline constexpr wchar_t virtual_display[] = L"ExtraPane LAN Virtual Display";
}
