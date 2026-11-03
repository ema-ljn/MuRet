#pragma once

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Render/ITextureStore.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextCache.h"
#include "Scenes/ITextDrawer.h"
#include "Scenes/TextCacheDrawer.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

// El texto de las escenas en main.cpp (CL5.4): el TextCache de la CL5.3 con el rasterizador de las
// fuentes del ancho de la pantalla, y su ITextDrawer sobre el lote del frame. El original crea las
// fuentes una vez, con el ancho de la ventana (FontSet.h, FontHeightFor); en MuRet el área de
// dibujo puede cambiar (otra densidad de pantalla), y con otro ancho se abren otras.
namespace Client::Scenes
{
// Abre las fuentes para una pantalla de `screenWidth` de ancho: en main.cpp, el FreeTypeRasterizer
// con los archivos de App::ChooseFontFiles; en los tests, uno falso.
using RasterizerFactory =
    std::function<Core::Expected<std::unique_ptr<Render::Text::IGlyphRasterizer>, std::string>(int screenWidth)>;

class SceneText
{
public:
    // Las fuentes de `screenWidth`, o su error. La tabla, el lote y el log tienen que vivir más.
    [[nodiscard]] static Core::Expected<std::unique_ptr<SceneText>, std::string>
    Create(Render::ITextureStore& store, Render::SpriteBatch& batch, RasterizerFactory fonts, int screenWidth,
           Core::Log& log);

    SceneText(const SceneText&) = delete;
    SceneText& operator=(const SceneText&) = delete;
    SceneText(SceneText&&) = delete;
    SceneText& operator=(SceneText&&) = delete;
    ~SceneText() = default;

    // Entre frames, con el ancho del que empieza: si cambió, las fuentes de ese ancho y la caché
    // vacía. Si no abren, siguen las de antes y el error va al log una vez por ancho.
    void FollowScreenWidth(int screenWidth);

    ITextDrawer& Drawer();

private:
    SceneText(Render::ITextureStore& store, Render::SpriteBatch& batch, RasterizerFactory fonts,
              std::unique_ptr<Render::Text::IGlyphRasterizer> rasterizer, int screenWidth, Core::Log& log);

    RasterizerFactory m_fonts;
    Core::Log& m_log;
    std::unique_ptr<Render::Text::IGlyphRasterizer> m_rasterizer;
    int m_screenWidth;
    // El último ancho que no abrió, para no repetir su error.
    std::optional<int> m_failedWidth;
    Render::Text::TextCache m_cache;
    TextCacheDrawer m_drawer;
};
} // namespace Client::Scenes
