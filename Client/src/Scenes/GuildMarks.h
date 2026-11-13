#pragma once

#include "Core/Expected.h"
#include "Render/ITextureStore.h"
#include "Render/TextureSlot.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

// La marca de un gremio como textura (CL9 hito 8, fase 2; 06 § El gremio en pantalla): 0x5f16a0(índice, fondo) pinta
// los 64 nibbles de la entrada `índice` de la tabla (0x787aa48 + 0x16) con su paleta en el buffer de la textura del
// slot 0x22 (Interface\guild.tga, que 0x61b33c sube con 0x63bd00 en GL_NEAREST y GL_CLAMP) y la vuelve a subir con
// glTexImage2D; lo que la llama dibuja enseguida un quad con el slot 0x22. MuRet dibuja los lotes después de armarlos:
// cada marca va a su slot (Render::FirstGuildMarkTextureSlot) y el quad usa ese.
namespace Client::Scenes
{
struct GuildState;

// Interface\guild.tga es de 8 × 8 y 32 bits (el encabezado del OZT, *estático*): 0x5f16a0 recorre su alto y su ancho
// (0x7d3a928 y 0x7d3a92c en float, con _ftol) y sube con el formato interno de sus componentes (4) en GL_RGBA.
inline constexpr std::size_t GuildMarkSide = 8;
inline constexpr std::size_t GuildMarkBytes = GuildMarkSide * GuildMarkSide * 4;
using GuildMarkImage = std::array<std::uint8_t, GuildMarkBytes>;

// Los píxeles de 0x5f16a0: la paleta de 0x77f3d84..0x77f3dc0 (cada DWORD 0xAABBGGRR, en memoria R, G, B y A), con el
// 0 negro y alfa 0x80, o 0 con `clearBackground` (el segundo argumento distinto de 0); los nibbles en orden, fila por
// fila. Corta con un índice fuera de la tabla (0x787aa5e + índice · 0x58 leería otra memoria) o con un nibble de más de
// 15 (leería después de la paleta); los que escribe el 66 (0x447b90) van de 0 a 15.
[[nodiscard]] Core::Expected<GuildMarkImage, std::string> PaintTheGuildMark(const GuildState& guild, std::int32_t index,
                                                                            bool clearBackground);

// 0x5f1bb0(x, y, w, h, color), el cuadrado de un color del editor de la marca: llena el buffer del 0x22 (los 8 × 8 que
// recorre con el alto y el ancho de la textura) con el color de la paleta (0x77f3d84, la que dejó el último 0x5f16a0)
// o, con el 0, de negro (0xff000000) con las dos diagonales grises (0xff808080: 0x5f1c83..0x5f1ca6, de a 9 y de a 7
// píxeles desde el 0 y el 7). Corta con un color de más de 15, que leería después de la paleta.
[[nodiscard]] Core::Expected<GuildMarkImage, std::string> PaintTheSwatch(std::int32_t color);

// 0x5f16a0 para los que dibujan: pinta la marca y da el slot donde quedó, para el quad que sigue.
class IGuildMarks
{
public:
    virtual ~IGuildMarks() = default;

    [[nodiscard]] virtual Core::Expected<Render::TextureSlot, std::string>
    Upload(const GuildState& guild, std::int32_t index, bool clearBackground) = 0;
    // 0x5f1bb0: pinta el cuadrado del color y da su slot (Render::FirstGuildSwatchTextureSlot + color).
    [[nodiscard]] virtual Core::Expected<Render::TextureSlot, std::string> UploadSwatch(std::int32_t color) = 0;
};

// Las marcas en la tabla de la escena: el slot de (índice, fondo) en Render::FirstGuildMarkTextureSlot, en
// GL_NEAREST y GL_CLAMP como el 0x22. Sube solo si los píxeles cambiaron desde la última vez (el original sube en cada
// llamada la misma imagen: lo que se ve es igual). Suelta sus slots al morir. Vive lo que la tabla.
class GuildMarkTextures final : public IGuildMarks
{
public:
    explicit GuildMarkTextures(Render::ITextureStore& store);
    ~GuildMarkTextures() override;

    GuildMarkTextures(const GuildMarkTextures&) = delete;
    GuildMarkTextures& operator=(const GuildMarkTextures&) = delete;
    GuildMarkTextures(GuildMarkTextures&&) = delete;
    GuildMarkTextures& operator=(GuildMarkTextures&&) = delete;

    [[nodiscard]] Core::Expected<Render::TextureSlot, std::string> Upload(const GuildState& guild, std::int32_t index,
                                                                          bool clearBackground) override;
    [[nodiscard]] Core::Expected<Render::TextureSlot, std::string> UploadSwatch(std::int32_t color) override;

    // El slot de (índice, fondo).
    [[nodiscard]] static Render::TextureSlot SlotOf(std::int32_t index, bool clearBackground);

private:
    // Sube `image` al slot si cambió desde la última vez.
    [[nodiscard]] Core::Expected<void, std::string> Load(Render::TextureSlot slot, const GuildMarkImage& image);

    Render::ITextureStore* m_store;
    // Lo subido a cada slot.
    std::map<std::uint16_t, GuildMarkImage> m_uploaded;
};
} // namespace Client::Scenes
