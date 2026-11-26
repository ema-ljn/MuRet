#pragma once

#include "Core/Expected.h"
#include "Core/Log.h"
#include "EngineApi.h"
#include "Render/ITextureStore.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextCache.h"
#include "Render/Frame/ITextDrawer.h"
#include "Render/Frame/TextCacheDrawer.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

// The original creates the fonts once, with the window width (FontSet.h, FontHeightFor); in MuRet the drawing area can
// change (another screen density), and another width opens other fonts.
namespace Client::Render::Frame
{
using RasterizerFactory =
    std::function<Core::Expected<std::unique_ptr<Render::Text::IGlyphRasterizer>, std::string>(int screenWidth)>;

class ENGINE_API SceneText
{
public:
    // The store, the batch and the log must outlive it.
    [[nodiscard]] static Core::Expected<std::unique_ptr<SceneText>, std::string>
    Create(Render::ITextureStore& store, Render::SpriteBatch& batch, RasterizerFactory fonts, int screenWidth,
           Core::Log& log);

    SceneText(const SceneText&) = delete;
    SceneText& operator=(const SceneText&) = delete;
    SceneText(SceneText&&) = delete;
    SceneText& operator=(SceneText&&) = delete;
    ~SceneText() = default;

    // Between frames, with the new frame's width: if it changed, that width's fonts and an empty cache. If they fail,
    // the old ones stay and the error is logged once per width.
    void FollowScreenWidth(int screenWidth);

    ITextDrawer& Drawer();

private:
    SceneText(Render::ITextureStore& store, Render::SpriteBatch& batch, RasterizerFactory fonts,
              std::unique_ptr<Render::Text::IGlyphRasterizer> rasterizer, int screenWidth, Core::Log& log);

    RasterizerFactory m_fonts;
    Core::Log& m_log;
    std::unique_ptr<Render::Text::IGlyphRasterizer> m_rasterizer;
    int m_screenWidth;
    // The last width that failed, so its error is not repeated.
    std::optional<int> m_failedWidth;
    Render::Text::TextCache m_cache;
    TextCacheDrawer m_drawer;
};
} // namespace Client::Render::Frame
