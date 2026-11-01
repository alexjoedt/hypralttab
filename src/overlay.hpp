#pragma once

#include <hyprutils/math/Region.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include "session.hpp"

class CWLSurfaceResource;

namespace alttab {

// Compositor overlay while switch session active.
// Incompatible with other overview plugins that own renderWorkspace heavily
// (hymission / Hyprspace / hyprexpo / scrolloverview) when their overview is open.
class Overlay {
  public:
    Overlay() = default;
    ~Overlay();

    Overlay(const Overlay&)            = delete;
    Overlay& operator=(const Overlay&) = delete;

    void attach(SwitchSession* session);
    void detach();

    // Call when session opens/closes/cycles so monitor stays live.
    void onSessionOpened();
    void onSessionClosed();
    void damage();

  private:
    void onRenderStage(const Event::SRenderStageEvent& ev);
    void draw(Render::CRenderContext& ctx);
    void forceCandidatesVisible(bool enable);
    void restoreForcedVisibility();
    void forceWindowSurfaces(const PHLWINDOW& window);
    void forceSurfaceVisibility(SP<CWLSurfaceResource> surface);

    SwitchSession*      m_session = nullptr;
    CHyprSignalListener m_renderListener;
    bool                m_blockDamage = false;

    struct ForcedWindow {
        PHLWINDOWREF window;
        bool         wasHidden   = false;
        float        fullscreenA = 1.F;
    };

    struct ForcedWorkspace {
        PHLWORKSPACE workspace;
        bool         visible        = false;
        bool         forceRendering = false;
        float        alphaValue     = 1.F;
        float        alphaGoal      = 1.F;
        Vector2D     renderOffsetVal{};
        Vector2D     renderOffsetGoal{};
    };

    struct ForcedSurface {
        WP<CWLSurfaceResource> surface;
        CRegion                visibleRegion;
    };

    std::vector<ForcedWindow>    m_forcedWindows;
    std::vector<ForcedWorkspace> m_forcedWorkspaces;
    std::vector<ForcedSurface>   m_forcedSurfaces;
};

} // namespace alttab
