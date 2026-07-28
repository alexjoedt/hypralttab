#pragma once

#include <cstdint>
#include <string>

#include <hyprland/src/plugins/PluginAPI.hpp>

namespace alttab::cfg {

void registerConfig(HANDLE handle);
void reload();

[[nodiscard]] std::string releaseKey();
[[nodiscard]] bool        includeSpecial();
[[nodiscard]] bool        animations();

} // namespace alttab::cfg
