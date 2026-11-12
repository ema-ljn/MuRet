#pragma once

#include "Core/Expected.h"
#include "Render/ITextureStore.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextBox.h"
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
// tiempo que no se dibujan, y sus slots se sueltan, salvo uno que ya está en el lote donde se
// dibuja: el lote guarda el slot de cada quad y Gl::GlSpriteRenderer lo busca recién al dibujar,
// así que una caja soltada o pisada antes cambiaría lo que se ve. No es la caché de 0x466000 (un
// árbol por hash con los slots de 0x466560, que en las escenas medidas no corre: pregunta 11 del
// plan de la CL5).
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
    // vacío no dibuja nada, como 0x467060. Error si la tabla no sube una caja, si el texto pide
    // más cajas que los slots de la caché, o si para hacerle lugar habría que sacar un texto que ya
    // está en `batch`. Un lote por frame: un texto de otro lote que todavía no se dibujó puede
    // salir.
    [[nodiscard]] Core::Expected<void, std::string> Draw(SpriteBatch& batch, int x, int y, std::string_view text,
                                                         const TextStyle& style);

    // 0x5542f0(x, y, texto, 0, 6, 0), el modo 6 de 0x467060 (el cartel de Slide.bmd): con x negativa, TextOutA en
    // (x, 0) del DIB, así que se ven las columnas del texto desde −x (las de 640 × 480 tomadas como píxeles, Q-C519),
    // cx + x de ancho, desde la x 0; sin ninguna, nada. Cada corte es otra entrada de la caché, como el original, que
    // lo rasteriza en cada frame. Con x de 0 o más, Draw.
    [[nodiscard]] Core::Expected<void, std::string> DrawClippedAtLeft(SpriteBatch& batch, int x, int y,
                                                                      std::string_view text, const TextStyle& style);

    // 0x467060 con un ancho (los modos 1 y 2 de 0x554180): una caja de `columns` columnas en (x, y) de 640 × 480, con
    // el fondo entero y, en su columna 0, la columna `firstColumn` del texto: con más, lo de la izquierda queda
    // afuera (el modo 1 con el ancho menor que cx); con menos de 0, la caja empieza con fondo (el modo 2, centrado).
    // Lo que pasa del texto es fondo, aunque esté vacío. Cada caja es otra entrada de la caché. Sin columnas, nada.
    // Los errores, los de Draw.
    [[nodiscard]] Core::Expected<void, std::string> DrawColumns(SpriteBatch& batch, int x, int y, std::string_view text,
                                                                const TextStyle& style, int firstColumn, int columns);

    // 0x553e50 y 0x554000 de un renglón de un globo (0x555b00): la caja de `box` con el texto en (0, 0), aunque esté
    // vacío (el fondo solo), en (x, y) de la pantalla en píxeles, como la corre BoxedQuad. Cada caja es otra entrada
    // de la caché, de un slot. Los errores, los de Draw.
    [[nodiscard]] Core::Expected<void, std::string> DrawBoxed(SpriteBatch& batch, int x, int y, std::string_view text,
                                                              const TextStyle& style, const BoxShape& box, bool clampY);

    // 0x554180(x, y, texto, caja, 1, ·), el modo 2 de 0x467060 (el título del inventario): el texto centrado en una
    // caja de `boxWidth` columnas (CenterInBox) que empieza en (x, y) de 640 × 480, con el fondo en toda la caja y las
    // cajas de 256 y sus quads de TextPieces con el ancho de la caja. Un texto vacío con caja mide "0" (0x6c7ae4,
    // 0x4670ba..0x4670d0) y da el fondo solo; sin caja, nada. Cada caja es otra entrada de la caché. Error si la
    // caja da menos de 0 columnas (una caja negativa: el rep stos de 0x4671cf limpiaría fuera del DIB), y los de Draw.
    [[nodiscard]] Core::Expected<void, std::string>
    DrawCenteredInBox(SpriteBatch& batch, int x, int y, std::string_view text, const TextStyle& style, int boxWidth);

    // GetTextExtentPointA, para ubicar el texto antes de dibujarlo (RightAlignedAt).
    [[nodiscard]] TextExtent Measure(FontKind font, std::string_view text) const;

    // Otro rasterizador, el de otro ancho de pantalla (las fuentes cambian de alto, FontSet.h):
    // vacía la caché y suelta sus slots. Entre frames, como Clear.
    void ChangeRasterizer(const IGlyphRasterizer& rasterizer);

    // Vacía la caché y suelta sus slots, aunque estén en un lote: entre frames.
    void Clear();

    // Cuántos textos guarda y cuántos slots ocupan.
    [[nodiscard]] std::size_t Size() const;
    [[nodiscard]] std::size_t UsedSlots() const;

private:
    // La clave: la fuente, los colores, el texto, que en el índice apunta al de su entrada, y las columnas que el modo
    // 6 deja afuera a la izquierda (negativas, las que el modo 2 deja antes del texto).
    struct Key
    {
        FontKind font = FontKind::Normal;
        TextColors colors;
        std::string_view text;
        int skipped = 0;
        // La caja de un globo; con ancho 0, la de 0x467060.
        BoxShape box{};
        // Las columnas de la caja de DrawColumns; con 0, las del texto desde `skipped`.
        int columns = 0;
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
        int skipped = 0;
        BoxShape box{};
        int columns = 0;
        // El ancho es el que se ve: el del texto menos `skipped`, el de la caja de DrawColumns o el de la del globo.
        TextExtent extent;
        // Una por cada 256 columnas del texto, en orden.
        std::vector<TextureSlot> boxes;
    };
    using Entries = std::list<Entry>;

    // La entrada de `key`, rasterizada y subida si no estaba, al frente de la lista. Para hacerle
    // lugar no saca una entrada que ya está en `batch`.
    [[nodiscard]] Core::Expected<Entries::iterator, std::string> Find(const Key& key, const SpriteBatch& batch);
    // Los quads de `entry` en (x, y), como TextPieces.
    static void DrawPieces(SpriteBatch& batch, const Entry& entry, int x, int y);
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
