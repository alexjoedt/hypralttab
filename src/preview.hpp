#pragma once

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/math/Box.hpp>

namespace alttab {

// No dynamic symbols required (technique B).
bool initPreviewSymbols();

// Technique B (Hyprspace): SRenderModif scale+translate over real geometry.
// Full frame for multi-surface clients (Firefox/Zed); no temp size / isBeingAnimated.
void renderLivePreview(Render::CRenderContext& ctx, PHLMONITOR monitor, PHLWINDOW window, const Hyprutils::Math::CBox& boxPx, const Time::steady_tp& now);

// Dim fullscreen behind tiles.
void renderBackdrop(Render::CRenderContext& ctx, PHLMONITOR monitor, float alpha = 0.55F);

// Selection ring around preview box (px, monitor-local).
void renderSelectionRing(Render::CRenderContext& ctx, PHLMONITOR monitor, const Hyprutils::Math::CBox& boxPx, float thickness = 3.F);

// Title above tile + workspace label below. boxPx = preview box.
void renderEntryLabels(Render::CRenderContext& ctx, PHLMONITOR monitor, PHLWINDOW window, const Hyprutils::Math::CBox& boxPx, bool selected);

} // namespace alttab
