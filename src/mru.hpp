#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <hyprland/src/desktop/DesktopTypes.hpp>

namespace alttab {

class MruTracker {
  public:
    void setPaused(bool paused);
    bool paused() const;

    void recordActivation(const PHLWINDOW& window);
    void prune(const PHLWINDOW& removed = {});

    // Mapped, not hidden, not special (unless includeSpecial), usable size. All workspaces.
    std::vector<PHLWINDOW> collectCandidates(bool includeSpecial = false) const;

    std::uint64_t serialOf(const PHLWINDOW& window) const;

  private:
    bool                                        m_paused = false;
    std::uint64_t                               m_nextSerial = 1;
    std::unordered_map<PHLWINDOW, std::uint64_t> m_serials;
};

bool isSwitchableWindow(const PHLWINDOW& window, bool includeSpecial = false);

} // namespace alttab
