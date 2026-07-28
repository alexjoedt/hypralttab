#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/devices/IKeyboard.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "layout.hpp"
#include "mru.hpp"

namespace alttab {

enum class SessionPhase {
    Inactive,
    Opening,
    Active,
    Closing,
};

enum class CloseMode {
    ActivateSelection,
    Abort,
};

struct LayoutEntry {
    PHLWINDOWREF window;
    Box          box; // monitor-local preview
    Size         natural;
};

class SwitchSession {
  public:
    explicit SwitchSession(MruTracker& mru);
    ~SwitchSession();

    SwitchSession(const SwitchSession&)            = delete;
    SwitchSession& operator=(const SwitchSession&) = delete;

    void wire();
    void unwire();

    SDispatchResult next();
    SDispatchResult prev();

    [[nodiscard]] bool         visible() const;
    [[nodiscard]] SessionPhase phase() const;
    [[nodiscard]] std::size_t  selectedIndex() const;
    [[nodiscard]] PHLWINDOW    selectedWindow() const;
    [[nodiscard]] const std::vector<PHLWINDOW>& windows() const;
    [[nodiscard]] const std::vector<LayoutEntry>& layout() const;
    [[nodiscard]] PHLMONITOR   ownerMonitor() const;

    // Monitor-local coords. Ready for mouse; unused until input wired.
    [[nodiscard]] std::optional<std::size_t> hitTestLocal(double px, double py) const;

    // Overlay hooks these for damage / force-visible.
    using SessionCallback = std::function<void()>;
    void setOnOpened(SessionCallback cb);
    void setOnClosed(SessionCallback cb);
    void setOnCycled(SessionCallback cb);

  private:
    void open(bool advanceOnOpen);
    void cycle(int delta);
    void beginClose(CloseMode mode);
    void finishClose();
    void applyFocus(const PHLWINDOW& window);
    void rebuildLayout();

    void setFollowMouseOverride(bool disable);
    void restoreCompositorTweaks();

    void onKey(const IKeyboard::SKeyEvent& event, Event::SCallbackInfo& info);

    [[nodiscard]] bool releaseKeyHeld() const;
    [[nodiscard]] bool isReleaseKeyEvent(const IKeyboard::SKeyEvent& event, const SP<IKeyboard>& keyboard) const;
    [[nodiscard]] bool isEscapePress(const IKeyboard::SKeyEvent& event, const SP<IKeyboard>& keyboard) const;

    MruTracker&                m_mru;
    SessionPhase               m_phase = SessionPhase::Inactive;
    std::vector<PHLWINDOW>     m_windows;
    std::vector<LayoutEntry>   m_layout;
    PHLMONITORREF              m_ownerMonitor;
    std::size_t                m_selectedIndex = 0;
    PHLWINDOW                  m_focusBeforeOpen;
    PHLWINDOW                  m_pendingExitFocus;
    CloseMode                  m_closeMode = CloseMode::ActivateSelection;

    bool    m_followMouseOverridden = false;
    int64_t m_followMouseBackup     = 0;
    bool    m_releaseArmed          = false;

    CHyprSignalListener m_keyListener;

    SessionCallback m_onOpened;
    SessionCallback m_onClosed;
    SessionCallback m_onCycled;
};

} // namespace alttab
