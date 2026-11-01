#include "preview.hpp"

#include <algorithm>
#include <cctype>
#include <string>

#include <hyprutils/utils/ScopeGuard.hpp>

#include <hyprland/src/config/shared/complex/ComplexDataTypes.hpp>
#include <hyprland/src/workspace/HLWorkspace.hpp>
#include <hyprland/src/desktop/view/WLSurface.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/pass/BorderPassElement.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/render/pass/RendererHintsPassElement.hpp>
#include <hyprland/src/render/pass/SurfacePassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
#include <hyprland/src/render/types.hpp>

#include "globals.hpp"

namespace alttab {
namespace {

using CBox = Hyprutils::Math::CBox;

} // namespace

bool initPreviewSymbols() {
    // Technique B (Hyprspace): no renderWindow symbol needed.
    return true;
}

void renderBackdrop(Render::CRenderContext& ctx, PHLMONITOR monitor, float alpha) {
    if (!monitor || !g_pHyprRenderer)
        return;

    CRectPassElement::SRectData data;
    data.color = CHyprColor(0.F, 0.F, 0.F, alpha);
    data.box   = CBox{{0, 0}, monitor->m_size * monitor->m_scale};
    Render::IHyprRenderer::addPassElement(ctx, makeUnique<CRectPassElement>(data));
}

void renderSelectionRing(Render::CRenderContext& ctx, PHLMONITOR monitor, const Hyprutils::Math::CBox& boxPx, float thickness) {
    if (!monitor || !g_pHyprRenderer || boxPx.w < 1 || boxPx.h < 1)
        return;

    CBorderPassElement::SBorderData data;
    data.box        = boxPx;
    data.borderSize = static_cast<int>(thickness);
    data.a          = 1.F;
    data.round      = 0;
    data.grad1      = Config::CGradientValueData{CHyprColor(0.24F, 0.78F, 1.F, 1.F)};
    Render::IHyprRenderer::addPassElement(ctx, makeUnique<CBorderPassElement>(data));
}

void renderLivePreview(Render::CRenderContext& ctx, PHLMONITOR monitor, PHLWINDOW window, const Hyprutils::Math::CBox& boxPx, const Time::steady_tp& now) {
    if (!monitor || !window || !g_pHyprRenderer)
        return;

    if (!window->mapped() || window->isHidden())
        return;

    if (!window->wlSurface() || !window->wlSurface()->resource())
        return;

    if (boxPx.w < 2 || boxPx.h < 2)
        return;

    const float monScale = monitor->m_scale > 0.F ? monitor->m_scale : 1.F;

    const Vector2D oPos  = window->positionAnimation()->value();
    const Vector2D oSize = window->sizeAnimation()->value();
    const float    logicalW = std::max(static_cast<float>(oSize.x), 5.F);
    const float    logicalH = std::max(static_cast<float>(oSize.y), 5.F);

    // Fit full window into tile (letterbox). Scale is uniform in px space.
    const float scaleMod = std::min(static_cast<float>(boxPx.w) / std::max(logicalW * monScale, 5.F),
                                    static_cast<float>(boxPx.h) / std::max(logicalH * monScale, 5.F));
    if (!(scaleMod > 0.F) || !std::isfinite(scaleMod))
        return;

    const float    outW      = logicalW * monScale * scaleMod;
    const float    outH      = logicalH * monScale * scaleMod;
    const Vector2D targetTL  = {boxPx.x + (boxPx.w - outW) * 0.5, boxPx.y + (boxPx.h - outH) * 0.5};

    const Vector2D logicalTL = oPos + window->presentation().floatingOffset();
    const Vector2D scaledTL  = (logicalTL - monitor->m_position) * monScale;
    // apply order: translate then scale → (p + T) * S. Want scaledTL*S + T*S = targetTL.
    const Vector2D translate = targetTL / scaleMod - scaledTL;

    // Tile fill so letterbox bars aren't transparent garbage.
    {
        CRectPassElement::SRectData bg;
        bg.color = CHyprColor(0.08F, 0.09F, 0.11F, 0.95F);
        bg.box   = boxPx;
        Render::IHyprRenderer::addPassElement(ctx, makeUnique<CRectPassElement>(bg));
    }

    Render::SRenderModifData renderModif;
    renderModif.modifs.emplace_back(Render::SRenderModifData::RMOD_TYPE_TRANSLATE, std::any(translate));
    renderModif.modifs.emplace_back(Render::SRenderModifData::RMOD_TYPE_SCALE, std::any(scaleMod));
    renderModif.enabled = true;

    Render::IHyprRenderer::addPassElement(ctx, makeUnique<CRendererHintsPassElement>(CRendererHintsPassElement::SData{.renderModif = renderModif}));
    auto clearHints = Hyprutils::Utils::CScopeGuard([&ctx] {
        Render::IHyprRenderer::addPassElement(ctx, makeUnique<CRendererHintsPassElement>(CRendererHintsPassElement::SData{.renderModif = Render::SRenderModifData{}}));
    });

    CSurfacePassElement::SRenderData renderdata = {monitor, now};
    renderdata.pos                = logicalTL;
    renderdata.w                  = logicalW;
    renderdata.h                  = logicalH;
    renderdata.surface            = window->wlSurface()->resource();
    renderdata.dontRound          = true;
    renderdata.fadeAlpha          = 1.F;
    renderdata.alpha              = 1.F;
    renderdata.decorate           = false;
    renderdata.rounding           = 0;
    renderdata.roundingPower      = 2.F;
    renderdata.blur               = false;
    renderdata.pWindow            = window;
    renderdata.clipBox            = boxPx;
    renderdata.useNearestNeighbor = false;
    renderdata.squishOversized    = true;
    renderdata.surfaceCounter     = 0;

    window->wlSurface()->resource()->breadthfirst(
        [&renderdata, &window, &ctx](SP<CWLSurfaceResource> s, const Vector2D& offset, void*) {
            if (!s || !s->m_current.texture)
                return;
            if (s->m_current.size.x < 1 || s->m_current.size.y < 1)
                return;

            renderdata.localPos    = offset;
            renderdata.texture     = s->m_current.texture;
            renderdata.surface     = s;
            renderdata.mainSurface = s == window->wlSurface()->resource();
            Render::IHyprRenderer::addPassElement(ctx, makeUnique<CSurfacePassElement>(renderdata));
            renderdata.surfaceCounter++;
        },
        nullptr);
}

void renderEntryLabels(Render::CRenderContext& ctx, PHLMONITOR monitor, PHLWINDOW window, const Hyprutils::Math::CBox& boxPx, bool selected) {
    if (!monitor || !window || !g_pHyprRenderer || boxPx.w < 2)
        return;

    const float scale   = monitor->m_scale > 0.F ? monitor->m_scale : 1.F;
    const int   titlePt = static_cast<int>(std::round((selected ? 15.0 : 13.0) * scale));
    const int   wsPt    = static_cast<int>(std::round(11.0 * scale));
    const int   maxW    = std::max(1, static_cast<int>(std::floor(boxPx.w)));

    // v1 icon fallback: class initial letter badge (top-left of tile).
    {
        std::string cls = std::string{window->metadata().appID()};
        if (cls.empty())
            cls = "?";
        char        letter = static_cast<char>(std::toupper(static_cast<unsigned char>(cls.front())));
        const float badge  = 22.0 * scale;
        CRectPassElement::SRectData badgeBg;
        badgeBg.color = CHyprColor(0.12F, 0.14F, 0.18F, 0.85F);
        badgeBg.box   = CBox{boxPx.x + 4 * scale, boxPx.y + 4 * scale, badge, badge};
        badgeBg.round = static_cast<int>(4 * scale);
        Render::IHyprRenderer::addPassElement(ctx, makeUnique<CRectPassElement>(badgeBg));

        if (auto tex = g_pHyprRenderer->renderText(std::string(1, letter), CHyprColor(1.F, 1.F, 1.F, 1.F),
                                                   static_cast<int>(std::round(14.0 * scale)), false, "", static_cast<int>(badge))) {
            if (tex->ok()) {
                CBox textBox;
                textBox.width  = tex->m_size.x;
                textBox.height = tex->m_size.y;
                textBox.x      = badgeBg.box.x + (badge - textBox.width) * 0.5;
                textBox.y      = badgeBg.box.y + (badge - textBox.height) * 0.5;
                textBox.round();
                CTexPassElement::SRenderData data;
                data.tex = tex;
                data.box = textBox;
                data.a   = 1.F;
                Render::IHyprRenderer::addPassElement(ctx, makeUnique<CTexPassElement>(data));
            }
        }
    }

    std::string title = std::string{window->metadata().title()};
    if (title.empty())
        title = std::string{window->metadata().appID()};
    if (title.empty())
        title = "?";

    const CHyprColor titleColor = selected ? CHyprColor(1.F, 1.F, 1.F, 1.F) : CHyprColor(0.9F, 0.9F, 0.9F, 0.9F);
    if (auto tex = g_pHyprRenderer->renderText(title, titleColor, titlePt, false, "", maxW)) {
        if (tex->ok()) {
            CBox textBox;
            textBox.width  = tex->m_size.x;
            textBox.height = tex->m_size.y;
            textBox.x      = boxPx.x + (boxPx.w - textBox.width) * 0.5;
            textBox.y      = boxPx.y - textBox.height - 6.0 * scale;
            textBox.round();

            CTexPassElement::SRenderData data;
            data.tex = tex;
            data.box = textBox;
            data.a   = 1.F;
            Render::IHyprRenderer::addPassElement(ctx, makeUnique<CTexPassElement>(data));
        }
    }

    std::string wsLabel = "ws ?";
    if (window->m_workspace) {
        if (!window->m_workspace->displayName().empty())
            wsLabel = window->m_workspace->displayName();
        else
            wsLabel = "ws " + window->m_workspace->addressableName();
    }

    const CHyprColor wsColor = CHyprColor(0.75F, 0.8F, 0.85F, 0.85F);
    if (auto tex = g_pHyprRenderer->renderText(wsLabel, wsColor, wsPt, false, "", maxW)) {
        if (tex->ok()) {
            CBox textBox;
            textBox.width  = tex->m_size.x;
            textBox.height = tex->m_size.y;
            textBox.x      = boxPx.x + (boxPx.w - textBox.width) * 0.5;
            textBox.y      = boxPx.y + boxPx.h + 6.0 * scale;
            textBox.round();

            CTexPassElement::SRenderData data;
            data.tex = tex;
            data.box = textBox;
            data.a   = 1.F;
            Render::IHyprRenderer::addPassElement(ctx, makeUnique<CTexPassElement>(data));
        }
    }
}

} // namespace alttab
