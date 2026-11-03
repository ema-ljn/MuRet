#pragma once

#include "Core/Log.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/TextCache.h"
#include "Scenes/ITextDrawer.h"

#include <set>
#include <string>
#include <string_view>

// El ITextDrawer de main.cpp (CL5.4): el TextCache de la CL5.3 sobre el lote del frame. Si un
// texto no se puede dibujar (la tabla no subió una caja, no entra en los slots, o para hacerle
// lugar habría que sacar uno que ya está en el lote), el texto no sale y el error va al log una
// vez: el mismo texto se pide en cada frame. Ningún mensaje lleva el texto.
namespace Client::Scenes
{
class TextCacheDrawer final : public ITextDrawer
{
public:
    // La caché, el lote y el log tienen que vivir lo que él.
    TextCacheDrawer(Render::Text::TextCache& cache, Render::SpriteBatch& batch, Core::Log& log);

    [[nodiscard]] Render::Text::TextExtent Measure(Render::Text::FontKind font, std::string_view text) const override;
    void Draw(int x, int y, std::string_view text, const Render::Text::TextStyle& style) override;

private:
    Render::Text::TextCache& m_cache;
    Render::SpriteBatch& m_batch;
    Core::Log& m_log;
    // Los errores que ya fueron al log.
    std::set<std::string> m_logged;
};
} // namespace Client::Scenes
