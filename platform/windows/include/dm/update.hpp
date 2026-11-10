#pragma once
#include <atomic>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
namespace dm::update {
enum class Channel { stable, prerelease };
struct Release {
    std::string version, tag, asset_id, digest;
    std::uint64_t size = 0;
    bool prerelease = false;
};
int compare_versions(const std::string& left, const std::string& right);
std::optional<Release> select_release(const std::string& json, Channel channel);
std::optional<Release> check(Channel channel, const std::atomic<bool>& cancel);
std::filesystem::path download(const Release& release, const std::atomic<bool>& cancel,
                               std::atomic<unsigned>& progress);
std::string sha256(const std::vector<unsigned char>& bytes);
bool allowed_download_url(const std::wstring& url);
void launch_installer_after_exit(unsigned parent, const std::filesystem::path& installer, const std::string& digest);
}
