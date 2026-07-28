#include "overlay.hpp"

#include <cmath>

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/state/GlobalWindowController.hpp>
#include <hyprland/src/desktop/view/WLSurface.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include "preview.hpp"

namespace alttab {

Overlay::~Overlay() {
    detach();
}

void Overlay::attach(SwitchSession* session) {
    m_session = session;
    m_renderListener = Event::bus()->m_events.render.stage.listen([this](eRenderStage stage) { onRenderStage(stage); });
}

void Overlay::detach() {
    onSessionClosed();
    m_renderListener.reset();
    m_session = nullptr;
}

void Overlay::onSessionOpened() {
    forceCandidatesVisible(true);
    damage();
}

void Overlay::onSessionClosed() {
    restoreForcedVisibility();
    if (g_pHyprRenderer)
        g_pHyprRenderer->m_renderPass.removeAllOfType("CRectPassElement"); // best-effort; elements are per-frame
    damage();
}

void Overlay::damage() {
    if (!m_session || !g_pHyprRenderer)
        return;

    const auto monitor = m_session->ownerMonitor();
    if (!monitor)
        return;

    m_blockDamage = true;
    g_pHyprRenderer->damageMonitor(monitor);
    m_blockDamage = false;
}

void Overlay::onRenderStage(eRenderStage stage) {
    if (stage != RENDER_POST_WINDOWS)
        return;

    if (!m_session || !m_session->visible())
        return;

    draw();
}

void Overlay::draw() {
    if (!m_session || !g_pHyprRenderer)
        return;

    const auto monitor = m_session->ownerMonitor();
    if (!monitor)
        return;

    // Only draw on the session owner monitor's current pass.
    if (g_pHyprRenderer->m_renderData.pMonitor != monitor)
        return;

    const auto now = Time::steadyNow();
    const auto& layout = m_session->layout();
    const auto  selected = m_session->selectedIndex();
    const double scale = monitor->m_scale > 0.F ? monitor->m_scale : 1.F;

    // Known-good clip for chrome; never leave default CBox() junk in renderData.
    g_pHyprRenderer->m_renderData.clipBox = CBox{{0, 0}, monitor->m_transformedSize};

    renderBackdrop(monitor, 0.55F);

    for (std::size_t i = 0; i < layout.size(); ++i) {
        const auto& entry = layout[i];
        const auto  window = entry.window.lock();
        if (!window)
            continue;

        // layout boxes are monitor-local logical; convert to px for pass elements / preview.
        const CBox boxPx{
            entry.box.x * scale,
            entry.box.y * scale,
            entry.box.w * scale,
            entry.box.h * scale,
        };

        renderLivePreview(monitor, window, boxPx, now);
        renderEntryLabels(monitor, window, boxPx, i == selected);

        if (i == selected) {
            const CBox ring{
                boxPx.x - 4,
                boxPx.y - 4,
                boxPx.w + 8,
                boxPx.h + 8,
            };
            renderSelectionRing(monitor, ring, 3.F);
        }
    }

    g_pHyprRenderer->m_renderData.clipBox = CBox{{0, 0}, monitor->m_transformedSize};
}

void Overlay::forceCandidatesVisible(bool enable) {
    if (!enable) {
        restoreForcedVisibility();
        return;
    }

    restoreForcedVisibility();
    if (!m_session)
        return;

    constexpr auto kFsAlpha = Desktop::View::WINDOW_ALPHA_FULLSCREEN;

    for (const auto& entry : m_session->layout()) {
        const auto window = entry.window.lock();
        if (!window)
            continue;

        ForcedWindow fw;
        fw.window      = window;
        fw.wasHidden   = window->isHidden();
        fw.fullscreenA = window->alphaValue(kFsAlpha);

        if (fw.wasHidden)
            window->setHidden(false);
        // Keep last buffer alive for off-workspace tiles; compositor re-suspends on close.
        window->setSuspended(false);
        window->alpha(kFsAlpha)->setValueAndWarp(1.F);

        forceWindowSurfaces(window);
        m_forcedWindows.push_back(std::move(fw));

        const auto workspace = window->m_workspace;
        if (!workspace)
            continue;

        bool known = false;
        for (const auto& existing : m_forcedWorkspaces) {
            if (existing.workspace == workspace) {
                known = true;
                break;
            }
        }
        if (known)
            continue;

        ForcedWorkspace fws;
        fws.workspace        = workspace;
        fws.visible          = workspace->m_visible;
        fws.forceRendering   = workspace->m_forceRendering;
        fws.alphaValue       = workspace->m_alpha ? workspace->m_alpha->value() : 1.F;
        fws.alphaGoal        = workspace->m_alpha ? workspace->m_alpha->goal() : 1.F;
        fws.renderOffsetVal  = workspace->m_renderOffset ? workspace->m_renderOffset->value() : Vector2D{};
        fws.renderOffsetGoal = workspace->m_renderOffset ? workspace->m_renderOffset->goal() : Vector2D{};

        workspace->m_visible        = true;
        workspace->m_forceRendering = true;
        if (workspace->m_renderOffset) {
            workspace->m_renderOffset->setValueAndWarp(Vector2D{});
            *workspace->m_renderOffset = Vector2D{};
        }
        if (workspace->m_alpha) {
            workspace->m_alpha->setValueAndWarp(1.F);
            *workspace->m_alpha = 1.F;
        }

        m_forcedWorkspaces.push_back(std::move(fws));
    }
}

void Overlay::restoreForcedVisibility() {
    constexpr auto kFsAlpha = Desktop::View::WINDOW_ALPHA_FULLSCREEN;

    for (auto& entry : m_forcedSurfaces) {
        const auto surface = entry.surface.lock();
        if (!surface)
            continue;
        if (const auto hl = Desktop::View::CWLSurface::fromResource(surface))
            hl->m_visibleRegion = entry.visibleRegion;
    }
    m_forcedSurfaces.clear();

    for (auto& fws : m_forcedWorkspaces) {
        if (!fws.workspace)
            continue;

        fws.workspace->m_visible        = fws.visible;
        fws.workspace->m_forceRendering = fws.forceRendering;
        if (fws.workspace->m_renderOffset) {
            fws.workspace->m_renderOffset->setValueAndWarp(fws.renderOffsetVal);
            if (fws.renderOffsetGoal != fws.renderOffsetVal)
                *fws.workspace->m_renderOffset = fws.renderOffsetGoal;
        }
        if (fws.workspace->m_alpha) {
            fws.workspace->m_alpha->setValueAndWarp(fws.alphaValue);
            if (std::abs(fws.alphaGoal - fws.alphaValue) > 0.0001F)
                *fws.workspace->m_alpha = fws.alphaGoal;
        }
    }
    m_forcedWorkspaces.clear();

    for (auto& fw : m_forcedWindows) {
        const auto window = fw.window.lock();
        if (!window)
            continue;

        window->alpha(kFsAlpha)->setValueAndWarp(fw.fullscreenA);
        if (fw.wasHidden)
            window->setHidden(true);
    }
    m_forcedWindows.clear();

    if (Desktop::globalWindowController())
        Desktop::globalWindowController()->updateSuspendedStates();
}

void Overlay::forceWindowSurfaces(const PHLWINDOW& window) {
    if (!window || !window->wlSurface() || !window->wlSurface()->resource())
        return;

    window->wlSurface()->resource()->breadthfirst(
        [this](SP<CWLSurfaceResource> surface, const Vector2D&, void*) { forceSurfaceVisibility(surface); }, nullptr);
}

void Overlay::forceSurfaceVisibility(SP<CWLSurfaceResource> surface) {
    if (!surface)
        return;

    const auto hl = Desktop::View::CWLSurface::fromResource(surface);
    if (!hl)
        return;

    for (auto& entry : m_forcedSurfaces) {
        if (entry.surface.lock() == surface) {
            hl->m_visibleRegion = {};
            return;
        }
    }

    m_forcedSurfaces.push_back({surface, hl->m_visibleRegion});
    hl->m_visibleRegion = {};
}

} // namespace alttab
