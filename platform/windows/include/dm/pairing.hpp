#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace dm::security {
using Key = std::array<std::uint8_t, 32>;
void validate_name(const std::string& name);
void save_pairing(const std::string& name, const Key& key);
Key load_pairing(const std::string& name);
void revoke_pairing(const std::string& name);
void list_pairings();
Key parse_key(const std::string& hex);
}
