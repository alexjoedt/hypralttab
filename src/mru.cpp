#include "mru.hpp"

#include <algorithm>

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/workspace/HLWorkspace.hpp>
#include <hyprland/src/desktop/state/ViewState.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/types/Geometric.hpp>

namespace alttab {
namespace {

bool hasUsableSize(const Vector2D& size) {
    return size.x > 1.0 && size.y > 1.0;
}

bool windowHasUsableGeometry(const PHLWINDOW& window) {
    if (!window)
        return false;

    if (hasUsableSize(window->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT)))
        return true;

    return hasUsableSize(window->size(Desktop::View::IGeometric::GEOMETRIC_GOAL));
}

} // namespace

bool isSwitchableWindow(const PHLWINDOW& window, bool includeSpecial) {
    if (!window || !window->mapped() || window->isHidden())
        return false;

    if (!windowHasUsableGeometry(window))
        return false;

    if (!includeSpecial && window->onSpecialWorkspace())
        return false;

    if (!includeSpecial && window->m_workspace && window->m_workspace->type() == Workspace::eWorkspaceType::SPECIAL)
        return false;

    return true;
}

void MruTracker::setPaused(bool paused) {
    m_paused = paused;
}

bool MruTracker::paused() const {
    return m_paused;
}

void MruTracker::recordActivation(const PHLWINDOW& window) {
    if (m_paused)
        return;

    if (!window || !window->mapped() || window->isHidden())
        return;

    prune();
    m_serials[window] = m_nextSerial++;
}

void MruTracker::prune(const PHLWINDOW& removed) {
    if (removed)
        m_serials.erase(removed);

    std::erase_if(m_serials, [](const auto& entry) { return !entry.first || !entry.first->mapped(); });
}

std::uint64_t MruTracker::serialOf(const PHLWINDOW& window) const {
    if (!window)
        return 0;

    const auto it = m_serials.find(window);
    return it == m_serials.end() ? 0 : it->second;
}

std::vector<PHLWINDOW> MruTracker::collectCandidates(bool includeSpecial) const {
    std::vector<PHLWINDOW> candidates;

    if (!Desktop::viewState())
        return candidates;

    for (const auto& window : Desktop::viewState()->windows()) {
        if (isSwitchableWindow(window, includeSpecial))
            candidates.push_back(window);
    }

    std::stable_sort(candidates.begin(), candidates.end(), [this](const PHLWINDOW& lhs, const PHLWINDOW& rhs) {
        const auto lhsSerial = serialOf(lhs);
        const auto rhsSerial = serialOf(rhs);

        if (lhsSerial != rhsSerial)
            return lhsSerial > rhsSerial;

        return false;
    });

    return candidates;
}

} // namespace alttab
