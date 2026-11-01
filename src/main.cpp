#include <memory>
#include <stdexcept>
#include <string>

#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include "config.hpp"
#include "globals.hpp"
#include "mru.hpp"
#include "overlay.hpp"
#include "preview.hpp"
#include "session.hpp"

namespace {

std::unique_ptr<alttab::MruTracker>    g_mru;
std::unique_ptr<alttab::SwitchSession> g_session;
std::unique_ptr<alttab::Overlay>       g_overlay;
CHyprSignalListener                    g_windowActiveListener;
CHyprSignalListener                    g_windowCloseListener;
CHyprSignalListener                    g_windowDestroyListener;
CHyprSignalListener                    g_configReloadListener;

SDispatchResult dispatchNext(const std::string&) {
    if (!g_session)
        return {.success = false, .error = "session unavailable"};
    return g_session->next();
}

SDispatchResult dispatchPrev(const std::string&) {
    if (!g_session)
        return {.success = false, .error = "session unavailable"};
    return g_session->prev();
}

// Lua config (hl.plugin.alttab.*) needs addLuaFunction — dispatchers alone
// stay on the C++ keybind map and never appear under hl.plugin.
//
// Pattern mirrors scrolloverview: next()/prev() run immediately when invoked
// from a live __lua keybind callback; at bind registration they return a
// zero-arg action that calls _run (always executes).

bool insideLuaKeybind(lua_State* L) {
    lua_Debug ar;
    for (int level = 0; lua_getstack(L, level, &ar); ++level) {
        if (lua_getinfo(L, "S", &ar) && ar.what && std::string_view{ar.what} == "main")
            return false;
    }
    return true;
}

SDispatchResult (*dispatcherByName(const char* name))(const std::string&) {
    if (name && std::string_view{name} == "next")
        return dispatchNext;
    if (name && std::string_view{name} == "prev")
        return dispatchPrev;
    return nullptr;
}

int runNamedDispatcher(lua_State* L, const char* name) {
    const auto fn = dispatcherByName(name);
    if (!fn)
        return luaL_error(L, "alttab._run: unknown dispatcher '%s'", name ? name : "");

    const auto result = fn("");
    if (!result.success)
        return luaL_error(L, "alttab.%s: %s", name, result.error.c_str());
    return 0;
}

int luaRun(lua_State* L) {
    if (lua_gettop(L) < 1 || !lua_isstring(L, 1))
        return luaL_error(L, "alttab._run: expected dispatcher name (next|prev)");
    return runNamedDispatcher(L, lua_tostring(L, 1));
}

int runOrBindDispatcher(lua_State* L, const char* name) {
    if (insideLuaKeybind(L))
        return runNamedDispatcher(L, name);

    // Bind-time: return action that always executes via _run (no re-entry trap).
    const std::string code =
        std::string("return function() return hl.plugin.alttab._run(\"") + name + "\") end";
    if (luaL_loadstring(L, code.c_str()) != LUA_OK)
        return lua_error(L);
    if (lua_pcall(L, 0, 1, 0) != LUA_OK)
        return lua_error(L);
    return 1;
}

int luaNext(lua_State* L) {
    return runOrBindDispatcher(L, "next");
}

int luaPrev(lua_State* L) {
    return runOrBindDispatcher(L, "prev");
}

void registerLuaApi(HANDLE handle) {
    if (!::Config::mgr() || ::Config::mgr()->type() != ::Config::CONFIG_LUA)
        return;

    const auto add = [&](const char* name, PLUGIN_LUA_FN fn) {
        if (!HyprlandAPI::addLuaFunction(handle, "alttab", name, fn)) {
            HyprlandAPI::addNotification(handle, std::string("[hypr-alt-tab] failed to register hl.plugin.alttab.") + name,
                                         CHyprColor(1.0, 0.2, 0.2, 1.0), 5000);
        }
    };

    add("_run", luaRun);
    add("next", luaNext);
    add("prev", luaPrev);
}

void wireMruListeners() {
    auto& events = Event::bus()->m_events;

    g_windowActiveListener = events.window.active.listen([](PHLWINDOW window, auto) {
        if (g_mru)
            g_mru->recordActivation(window);
    });

    g_windowCloseListener = events.window.close.listen([](PHLWINDOW window) {
        if (g_mru)
            g_mru->prune(window);
    });

    g_windowDestroyListener = events.window.destroy.listen([](PHLWINDOWREF window) {
        if (g_mru)
            g_mru->prune(window.lock());
    });
}

void clearListeners() {
    g_windowActiveListener.reset();
    g_windowCloseListener.reset();
    g_windowDestroyListener.reset();
    g_configReloadListener.reset();
}

} // namespace

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    g_pluginHandle = handle;

    const std::string HASH        = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();

    if (HASH != CLIENT_HASH) {
        HyprlandAPI::addNotification(g_pluginHandle, "[hypr-alt-tab] version mismatch (headers != running hyprland)",
                                     CHyprColor(1.0, 0.2, 0.2, 1.0), 5000);
        throw std::runtime_error("[hypr-alt-tab] version mismatch");
    }

    (void)alttab::initPreviewSymbols();

    alttab::cfg::registerConfig(handle);
    g_configReloadListener = Event::bus()->m_events.config.reloaded.listen([] {
        // Skip mid-session to avoid corrupting open overlay state.
        if (g_session && g_session->visible())
            return;
        alttab::cfg::reload();
    });

    g_mru = std::make_unique<alttab::MruTracker>();
    wireMruListeners();

    g_session = std::make_unique<alttab::SwitchSession>(*g_mru);
    g_session->wire();

    g_overlay = std::make_unique<alttab::Overlay>();
    g_overlay->attach(g_session.get());
    g_session->setOnOpened([] { if (g_overlay) g_overlay->onSessionOpened(); });
    g_session->setOnClosed([] { if (g_overlay) g_overlay->onSessionClosed(); });
    g_session->setOnCycled([] { if (g_overlay) g_overlay->damage(); });

    const auto registerDispatcher = [](const char* name, auto fn) {
        if (!HyprlandAPI::addDispatcherV2(g_pluginHandle, name, fn)) {
            HyprlandAPI::addNotification(g_pluginHandle, std::string("[hypr-alt-tab] failed to register ") + name,
                                         CHyprColor(1.0, 0.2, 0.2, 1.0), 5000);
        }
    };

    registerDispatcher("alttab:next", dispatchNext);
    registerDispatcher("alttab:prev", dispatchPrev);
    registerLuaApi(handle);

    HyprlandAPI::addNotification(g_pluginHandle, "[hypr-alt-tab] loaded", CHyprColor(0.2, 0.9, 0.4, 1.0), 3000);

    return {
        .name        = "hypr-alt-tab",
        .description = "Windows-style Alt-Tab with live preview",
        .author      = "alex",
        .version     = "0.1.0",
    };
}

APICALL EXPORT void PLUGIN_EXIT() {
    if (g_overlay) {
        g_overlay->detach();
        g_overlay.reset();
    }

    if (g_session) {
        g_session->unwire();
        g_session.reset();
    }

    clearListeners();
    g_mru.reset();
    g_pluginHandle = nullptr;
}
