#pragma once

#include "Core/Expected.h"
#include "Render/ITextureStore.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"

#include <cstddef>
#include <list>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// Los textos ya rasterizados (CL5.3). El original rasteriza y sube cada texto en cada frame:
// 0x467060 lo dibuja con TextOutA en el DIB, 0x465a00 lo pasa a la caja de 256 × 32 y 0x554000 la
// sube al slot 0 con glTexImage2D antes de cada quad (06 § Texto). MuRet rasteriza un texto una vez
// por fuente y colores, guarda sus cajas en slots del texto (TextureSlot.h) y las reusa mientras no
// cambie. El contenido de una caja no depende de dónde va el texto: lo que cambia con la x y la
// pantalla (el corte en el borde, la posición) lo pone TextPieces en el quad.
//
// Tiene un tope: los slots que le dan. Cuando un texto nuevo no entra, salen los que hace más
// tiempo que no se dibujan, y sus slots se sueltan. No es la caché de 0x466000 (un árbol por hash
// con los slots de 0x466560, que en las escenas medidas no corre: pregunta 11 del plan de la CL5).
// Vive lo que la tabla donde sube, y no es para varios hilos.
namespace Client::Render::Text
{
class TextCache
{
public:
    // Sube las cajas a `store`, en los `slotCount` slots desde `firstSlot`. El rasterizador y la
    // tabla tienen que vivir lo que la caché.
    TextCache(const IGlyphRasterizer& rasterizer, ITextureStore& store, TextureSlot firstSlot = FirstTextTextureSlot,
              std::size_t slotCount = TextTextureSlotCount);
    // Suelta los slots que usa.
    ~TextCache();

    TextCache(const TextCache&) = delete;
    TextCache& operator=(const TextCache&) = delete;
    TextCache(TextCache&&) = delete;
    TextCache& operator=(TextCache&&) = delete;

    // 0x554180(x, y, texto, 0, 0, 0) con TextOut distinto de 1, el del login: el texto en (x, y)
    // de 640 × 480, con la fuente y los colores de `style`, como quads de TextPieces en `batch`
    // (con su pantalla, su color y su mezcla). Un texto que no está se rasteriza y se sube; uno
    // vacío no dibuja nada, como 0x467060. Error si la tabla no sube una caja, o si el texto pide
    // más cajas que los slots de la caché.
    [[nodiscard]] Core::Expected<void, std::string> Draw(SpriteBatch& batch, int x, int y, std::string_view text,
                                                         const TextStyle& style);

    // GetTextExtentPointA, para ubicar el texto antes de dibujarlo (RightAlignedAt).
    [[nodiscard]] TextExtent Measure(FontKind font, std::string_view text) const;

    // Otro rasterizador, el de otro ancho de pantalla (las fuentes cambian de alto, FontSet.h):
    // vacía la caché y suelta sus slots.
    void ChangeRasterizer(const IGlyphRasterizer& rasterizer);

    // Vacía la caché y suelta sus slots.
    void Clear();

    // Cuántos textos guarda y cuántos slots ocupan.
    [[nodiscard]] std::size_t Size() const;
    [[nodiscard]] std::size_t UsedSlots() const;

private:
    // La clave: la fuente, los colores y el texto, que en el índice apunta al de su entrada.
    struct Key
    {
        FontKind font = FontKind::Normal;
        TextColors colors;
        std::string_view text;
    };
    struct KeyLess
    {
        [[nodiscard]] bool operator()(const Key& a, const Key& b) const;
    };
    struct Entry
    {
        FontKind font = FontKind::Normal;
        TextColors colors;
        std::string text;
        TextExtent extent;
        // Una por cada 256 columnas del texto, en orden.
        std::vector<TextureSlot> boxes;
    };
    using Entries = std::list<Entry>;

    // La entrada de `key`, rasterizada y subida si no estaba, al frente de la lista.
    [[nodiscard]] Core::Expected<Entries::iterator, std::string> Find(const Key& key);
    // Saca la entrada del fondo de la lista (la que hace más tiempo que no se dibuja).
    void EvictOldest();

    const IGlyphRasterizer* m_rasterizer;
    ITextureStore* m_store;
    std::size_t m_slotCount;
    // La más reciente adelante. Los nodos de una lista no se mueven: el índice apunta a sus textos.
    Entries m_entries;
    std::map<Key, Entries::iterator, KeyLess> m_index;
    std::vector<TextureSlot> m_freeSlots;
};
} // namespace Client::Render::Text
