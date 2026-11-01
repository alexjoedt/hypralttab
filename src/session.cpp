#include "session.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>

#include <xkbcommon/xkbcommon.h>

#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/types/Geometric.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "config.hpp"
#include "globals.hpp"

namespace alttab {
namespace {

template <typename T>
using HyprSignalRefArg = std::conditional_t<std::is_trivially_copyable_v<T>, T, const T&>;

template <typename EventType, typename Signal>
CHyprSignalListener listenCancellable(Signal& signal, std::function<void(const EventType&, Event::SCallbackInfo&)> handler) {
    struct Hack : Hyprutils::Signal::CSignalBase {
        using CSignalBase::registerListenerInternal;
    };
    return reinterpret_cast<Hack&>(signal).registerListenerInternal([handler](void* args) {
        using Tuple = std::tuple<HyprSignalRefArg<EventType>, HyprSignalRefArg<Event::SCallbackInfo&>>;
        auto* tup   = static_cast<Tuple*>(args);
        handler(std::get<0>(*tup), std::get<1>(*tup));
    });
}

std::string asciiLower(std::string value) {
    for (char& ch : value)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}

std::optional<Input::ModifierMask> releaseModifierMask(const std::string& value) {
    const auto lowered = asciiLower(value);
    if (lowered == "shift" || lowered == "shift_l" || lowered == "shift_r")
        return Input::HL_MODIFIER_SHIFT;
    if (lowered == "ctrl" || lowered == "control" || lowered == "control_l" || lowered == "control_r" || lowered == "ctrl_l" ||
        lowered == "ctrl_r")
        return Input::HL_MODIFIER_CTRL;
    if (lowered == "alt" || lowered == "alt_l" || lowered == "alt_r")
        return Input::HL_MODIFIER_ALT;
    if (lowered == "super" || lowered == "super_l" || lowered == "super_r" || lowered == "meta" || lowered == "meta_l" ||
        lowered == "meta_r")
        return Input::HL_MODIFIER_META;
    return std::nullopt;
}

constexpr const char* kReleaseKey = "Alt_L";

int64_t readIntConfig(const char* name, int64_t fallback) {
    if (!::Config::mgr())
        return fallback;

    const auto value = ::Config::mgr()->getConfigValue(name);
    if (!value.dataptr || !value.type)
        return fallback;

    if (*value.type == typeid(bool))
        return **reinterpret_cast<bool* const*>(value.dataptr) ? 1 : 0;

    if (*value.type == typeid(::Config::INTEGER))
        return static_cast<int64_t>(**reinterpret_cast<::Config::INTEGER* const*>(value.dataptr));

    if (*value.type == typeid(Hyprlang::INT))
        return static_cast<int64_t>(**reinterpret_cast<Hyprlang::INT* const*>(value.dataptr));

    return fallback;
}

std::string setConfigKeyword(const std::string& name, const std::string& value) {
    // Lua config rejects hyprctl keyword; use eval + hl.config instead.
    if (::Config::mgr() && ::Config::mgr()->type() == ::Config::CONFIG_LUA) {
        if (name == "input:follow_mouse") {
            const auto code   = "hl.config({ input = { follow_mouse = " + value + " } })";
            const auto result = HyprlandAPI::invokeHyprctlCommand("eval", code);
            return result == "ok" ? std::string{} : result;
        }
    }

    const auto result = HyprlandAPI::invokeHyprctlCommand("keyword", name + " " + value);
    return result == "ok" ? std::string{} : result;
}

SP<IKeyboard> keyboardWithState() {
    if (!g_pInputManager)
        return {};

    for (const auto& kb : g_pInputManager->m_keyboards) {
        if (kb && kb->m_active && kb->m_xkbState)
            return kb;
    }

    for (const auto& kb : g_pInputManager->m_keyboards) {
        if (kb && kb->m_xkbState)
            return kb;
    }

    return {};
}

bool anyKeyboardMatches(const std::function<bool(const SP<IKeyboard>&)>& pred) {
    if (!g_pInputManager)
        return false;

    for (const auto& kb : g_pInputManager->m_keyboards) {
        if (kb && pred(kb))
            return true;
    }

    return false;
}

bool isAltKeysym(xkb_keysym_t sym) {
    return sym == XKB_KEY_Alt_L || sym == XKB_KEY_Alt_R || sym == XKB_KEY_Meta_L || sym == XKB_KEY_Meta_R;
}

void notify(const std::string& msg, const CHyprColor& color = CHyprColor(0.2, 0.8, 1.0, 1.0), int ms = 1500) {
    if (g_pluginHandle)
        HyprlandAPI::addNotification(g_pluginHandle, msg, color, ms);
}

} // namespace

SwitchSession::SwitchSession(MruTracker& mru) : m_mru(mru) {}

SwitchSession::~SwitchSession() {
    unwire();
    restoreCompositorTweaks();
}

void SwitchSession::wire() {
    m_keyListener = listenCancellable<IKeyboard::SKeyEvent>(Event::bus()->m_events.input.keyboard.key,
                                                            [this](const IKeyboard::SKeyEvent& event, Event::SCallbackInfo& info) {
                                                                onKey(event, info);
                                                            });
}

void SwitchSession::unwire() {
    m_keyListener.reset();
}

bool SwitchSession::visible() const {
    return m_phase == SessionPhase::Opening || m_phase == SessionPhase::Active;
}

SessionPhase SwitchSession::phase() const {
    return m_phase;
}

std::size_t SwitchSession::selectedIndex() const {
    return m_selectedIndex;
}

PHLWINDOW SwitchSession::selectedWindow() const {
    if (m_windows.empty() || m_selectedIndex >= m_windows.size())
        return {};
    return m_windows[m_selectedIndex];
}

const std::vector<PHLWINDOW>& SwitchSession::windows() const {
    return m_windows;
}

const std::vector<LayoutEntry>& SwitchSession::layout() const {
    return m_layout;
}

void SwitchSession::setOnOpened(SessionCallback cb) {
    m_onOpened = std::move(cb);
}

void SwitchSession::setOnClosed(SessionCallback cb) {
    m_onClosed = std::move(cb);
}

void SwitchSession::setOnCycled(SessionCallback cb) {
    m_onCycled = std::move(cb);
}

PHLMONITOR SwitchSession::ownerMonitor() const {
    return m_ownerMonitor.lock();
}

std::optional<std::size_t> SwitchSession::hitTestLocal(double px, double py) const {
    std::vector<Box> boxes;
    boxes.reserve(m_layout.size());
    for (const auto& entry : m_layout)
        boxes.push_back(entry.box);
    return hitTest(boxes, px, py);
}

void SwitchSession::rebuildLayout() {
    m_layout.clear();

    const auto monitor = m_ownerMonitor.lock();
    if (!monitor || m_windows.empty())
        return;

    std::vector<Size> naturals;
    naturals.reserve(m_windows.size());

    for (const auto& window : m_windows) {
        Size nat{320, 200};
        if (window) {
            const auto cur = window->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
            if (cur.x > 1.0 && cur.y > 1.0)
                nat = Size{cur.x, cur.y};
            else {
                const auto goal = window->size(Desktop::View::IGeometric::GEOMETRIC_GOAL);
                if (goal.x > 1.0 && goal.y > 1.0)
                    nat = Size{goal.x, goal.y};
            }
        }
        naturals.push_back(nat);
    }

    LayoutConfig cfg{
        .monitorW = monitor->m_size.x,
        .monitorH = monitor->m_size.y,
    };

    const auto boxes = layoutStrip(naturals, cfg);
    m_layout.reserve(m_windows.size());
    for (std::size_t i = 0; i < m_windows.size(); ++i) {
        m_layout.push_back(LayoutEntry{
            .window  = m_windows[i],
            .box     = i < boxes.size() ? boxes[i] : Box{},
            .natural = naturals[i],
        });
    }
}

SDispatchResult SwitchSession::next() {
    if (m_phase == SessionPhase::Closing)
        return {};

    if (m_phase == SessionPhase::Inactive) {
        open(true);
        return {};
    }

    cycle(1);
    return {};
}

SDispatchResult SwitchSession::prev() {
    if (m_phase == SessionPhase::Closing)
        return {};

    if (m_phase == SessionPhase::Inactive) {
        open(false);
        if (m_windows.size() > 1)
            cycle(-1);
        return {};
    }

    cycle(-1);
    return {};
}

void SwitchSession::open(bool advanceOnOpen) {
    m_phase = SessionPhase::Opening;

    m_focusBeforeOpen = Desktop::focusState() ? Desktop::focusState()->window() : PHLWINDOW{};
    m_ownerMonitor    = Desktop::focusState() ? Desktop::focusState()->monitor() : PHLMONITOR{};
    m_windows         = m_mru.collectCandidates(cfg::includeSpecial());

    if (m_windows.empty()) {
        m_phase = SessionPhase::Inactive;
        m_ownerMonitor.reset();
        notify("[hypr-alt-tab] no windows", CHyprColor(1.0, 0.6, 0.2, 1.0));
        return;
    }

    // Index 0 = most recent (usually focused). First Tab selects next.
    if (advanceOnOpen && m_windows.size() > 1)
        m_selectedIndex = 1;
    else
        m_selectedIndex = 0;

    rebuildLayout();

    m_pendingExitFocus = selectedWindow();
    m_closeMode        = CloseMode::ActivateSelection;
    m_releaseArmed     = releaseKeyHeld();

    m_mru.setPaused(true);
    setFollowMouseOverride(true);

    m_phase = SessionPhase::Active;

    if (m_onOpened)
        m_onOpened();
}

void SwitchSession::cycle(int delta) {
    if (m_phase != SessionPhase::Active || m_windows.empty())
        return;

    const auto n   = static_cast<int>(m_windows.size());
    auto       idx = static_cast<int>(m_selectedIndex) + delta;
    idx %= n;
    if (idx < 0)
        idx += n;

    m_selectedIndex    = static_cast<std::size_t>(idx);
    m_pendingExitFocus = selectedWindow();

    if (m_onCycled)
        m_onCycled();
}

void SwitchSession::beginClose(CloseMode mode) {
    if (m_phase == SessionPhase::Inactive || m_phase == SessionPhase::Closing)
        return;

    m_phase     = SessionPhase::Closing;
    m_closeMode = mode;

    if (mode == CloseMode::ActivateSelection) {
        auto target = selectedWindow();
        if (!target || !target->mapped())
            target = m_focusBeforeOpen;
        m_pendingExitFocus = target;
    } else {
        m_pendingExitFocus = m_focusBeforeOpen;
    }

    finishClose();
}

void SwitchSession::finishClose() {
    const auto target = m_pendingExitFocus;

    m_windows.clear();
    m_layout.clear();
    m_ownerMonitor.reset();
    m_selectedIndex    = 0;
    m_pendingExitFocus = {};
    m_focusBeforeOpen  = {};
    m_releaseArmed     = false;

    restoreCompositorTweaks();
    m_mru.setPaused(false);

    m_phase = SessionPhase::Inactive;

    if (m_onClosed)
        m_onClosed();

    if (m_closeMode == CloseMode::ActivateSelection) {
        applyFocus(target);
        return;
    }

    if (target && Desktop::focusState() && Desktop::focusState()->window() != target)
        applyFocus(target);
}

void SwitchSession::applyFocus(const PHLWINDOW& window) {
    if (!window || !window->mapped() || !Desktop::focusState())
        return;

    // Cross-workspace: move monitor to window's workspace first when needed.
    if (window->m_workspace) {
        const auto mon = window->m_workspace->m_monitor.lock();
        if (mon && mon->m_activeWorkspace != window->m_workspace)
            mon->changeWorkspace(window->m_workspace, false, false, true);
    }

    // HARD reason: scrolling layout only centers the tape on hard focus
    // (FOCUS_REASON_OTHER → SOFT → off-screen / multi-FS columns stay put).
    // forceFSCycle: non-layout FS workspaces replace covering FS with target.
    Desktop::focusState()->fullWindowFocus(window, Desktop::FOCUS_REASON_SWITCH_TO_WINDOW_HARD, nullptr, true);
}

void SwitchSession::setFollowMouseOverride(bool disable) {
    if (disable) {
        if (m_followMouseOverridden)
            return;

        m_followMouseBackup = readIntConfig("input:follow_mouse", 1);
        const auto err      = setConfigKeyword("input:follow_mouse", "0");
        if (!err.empty()) {
            notify("[hypr-alt-tab] failed to set input:follow_mouse=0", CHyprColor(1.0, 0.2, 0.2, 1.0), 3000);
            return;
        }

        m_followMouseOverridden = true;
        return;
    }

    if (!m_followMouseOverridden)
        return;

    const auto err = setConfigKeyword("input:follow_mouse", std::to_string(m_followMouseBackup));
    if (!err.empty())
        notify("[hypr-alt-tab] failed to restore input:follow_mouse", CHyprColor(1.0, 0.2, 0.2, 1.0), 3000);

    m_followMouseOverridden = false;
}

void SwitchSession::restoreCompositorTweaks() {
    setFollowMouseOverride(false);
}

bool SwitchSession::releaseKeyHeld() const {
    const auto key = cfg::releaseKey();
    if (const auto mask = releaseModifierMask(key))
        return anyKeyboardMatches([&](const SP<IKeyboard>& kb) { return kb && (static_cast<uint32_t>(kb->getModifiers()) & static_cast<uint32_t>(*mask)) != 0; });

    return anyKeyboardMatches([](const SP<IKeyboard>& kb) { return kb && (static_cast<uint32_t>(kb->getModifiers()) & static_cast<uint32_t>(Input::HL_MODIFIER_ALT)) != 0; });
}

bool SwitchSession::isReleaseKeyEvent(const IKeyboard::SKeyEvent& event, const SP<IKeyboard>& keyboard) const {
    if (!keyboard || !keyboard->m_xkbState)
        return false;

    if (event.state != WL_KEYBOARD_KEY_STATE_RELEASED)
        return false;

    const auto         key      = cfg::releaseKey();
    const xkb_keysym_t eventSym = xkb_state_key_get_one_sym(keyboard->m_xkbState, event.keycode + 8);

    if (const auto mask = releaseModifierMask(key)) {
        if (mask == Input::HL_MODIFIER_ALT && isAltKeysym(eventSym))
            return true;
        if (mask == Input::HL_MODIFIER_META && (eventSym == XKB_KEY_Super_L || eventSym == XKB_KEY_Super_R || eventSym == XKB_KEY_Meta_L || eventSym == XKB_KEY_Meta_R))
            return true;
        if (mask == Input::HL_MODIFIER_CTRL && (eventSym == XKB_KEY_Control_L || eventSym == XKB_KEY_Control_R))
            return true;
        if (mask == Input::HL_MODIFIER_SHIFT && (eventSym == XKB_KEY_Shift_L || eventSym == XKB_KEY_Shift_R))
            return true;
    }

    const xkb_keysym_t configured = xkb_keysym_from_name(key.c_str(), XKB_KEYSYM_CASE_INSENSITIVE);
    return configured != XKB_KEY_NoSymbol && eventSym == configured;
}

bool SwitchSession::isEscapePress(const IKeyboard::SKeyEvent& event, const SP<IKeyboard>& keyboard) const {
    if (!keyboard || !keyboard->m_xkbState)
        return false;

    if (event.state != WL_KEYBOARD_KEY_STATE_PRESSED)
        return false;

    const xkb_keysym_t sym = xkb_state_key_get_one_sym(keyboard->m_xkbState, event.keycode + 8);
    return sym == XKB_KEY_Escape;
}

void SwitchSession::onKey(const IKeyboard::SKeyEvent& event, Event::SCallbackInfo& info) {
    if (m_phase != SessionPhase::Active && m_phase != SessionPhase::Opening)
        return;

    const auto keyboard = keyboardWithState();
    if (!keyboard)
        return;

    if (isEscapePress(event, keyboard)) {
        info.cancelled = true;
        beginClose(CloseMode::Abort);
        return;
    }

    if (event.state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        if (releaseKeyHeld())
            m_releaseArmed = true;
        return;
    }

    if (event.state != WL_KEYBOARD_KEY_STATE_RELEASED)
        return;

    if (!m_releaseArmed) {
        if (releaseKeyHeld())
            m_releaseArmed = true;
        return;
    }

    if (isReleaseKeyEvent(event, keyboard))
        beginClose(CloseMode::ActivateSelection);
}

} // namespace alttab
