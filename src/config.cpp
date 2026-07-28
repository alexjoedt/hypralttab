#include "config.hpp"

#include <string>
#include <unordered_map>

#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "globals.hpp"

namespace alttab::cfg {
namespace {

std::string g_releaseKey     = "Alt_L";
bool        g_includeSpecial = false;
bool        g_animations     = true;

template <typename T>
CConfigValue<T>& valueRef(const std::string& name) {
    static std::unordered_map<std::string, UP<CConfigValue<T>>> values;
    const auto [it, inserted] = values.try_emplace(name);
    if (inserted)
        it->second = makeUnique<CConfigValue<T>>(name);
    return *it->second;
}

} // namespace

void registerConfig(HANDLE handle) {
    using namespace ::Config::Values;

    HyprlandAPI::addConfigValueV2(handle, makeShared<CStringValue>("plugin:alttab:release_key", "hold-mod key released to commit switch (e.g. Alt_L)", "Alt_L"));
    HyprlandAPI::addConfigValueV2(handle, makeShared<CIntValue>("plugin:alttab:include_special", "include special workspaces in switch list", 0,
                                                                SIntValueOptions{.min = 0, .max = 1}));
    HyprlandAPI::addConfigValueV2(handle, makeShared<CIntValue>("plugin:alttab:animations", "reserved: overlay animations on/off", 1,
                                                                SIntValueOptions{.min = 0, .max = 1}));

    if (!HyprlandAPI::reloadConfig()) {
        HyprlandAPI::addNotification(handle, "[hypr-alt-tab] reloadConfig failed", CHyprColor(1.0, 0.2, 0.2, 1.0), 4000);
    }

    reload();
}

void reload() {
    auto& releaseKey = valueRef<std::string>("plugin:alttab:release_key");
    auto& special    = valueRef<::Config::INTEGER>("plugin:alttab:include_special");
    auto& anims      = valueRef<::Config::INTEGER>("plugin:alttab:animations");

    if (releaseKey.good()) {
        const auto key = *releaseKey;
        g_releaseKey   = key.empty() ? "Alt_L" : key;
    } else {
        g_releaseKey = "Alt_L";
    }

    g_includeSpecial = special.good() && *special != 0;
    g_animations     = !anims.good() || *anims != 0;
}

std::string releaseKey() {
    return g_releaseKey;
}

bool includeSpecial() {
    return g_includeSpecial;
}

bool animations() {
    return g_animations;
}

} // namespace alttab::cfg
