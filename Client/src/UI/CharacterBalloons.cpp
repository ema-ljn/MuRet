#include "UI/CharacterBalloons.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Tables/TextTable.h"
#include "Render/ScreenScale.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"
#include "Render/Frame/ITextDrawer.h"
#include "Assets/SceneTextures.h"
#include "UI/GameText.h"
#include "UI/Widget.h"
#include "Math/Ftol.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::UI
{
namespace
{
using Render::Text::FontKind;

// 0x63c9a0 → 0x647050(0x76, 0x36, 0x13, 0, 0, 0x3b, 0x36, 0, 0, 1, 1): character_ex.tga (el slot 0x13 de 0x61ca30),
// de 118 × 54, en píxeles (fx = fy = 1), con el punto de +0x58 y +0x5c en (59, 54): 0x6472e0(x, y, 3) la deja
// centrada en x y con el borde de abajo en y.
constexpr Render::TextureSlot BalloonSlot{0x13};
constexpr int BalloonWidth = 0x76;
constexpr int BalloonHeight = 0x36;
constexpr double AnchorX = 0x3b;
constexpr double AnchorY = 0x36;

// 0x63cc60: el color del nombre (+0xac) por el CtlCode, en el orden de sus pruebas, en 0xAABBGGRR.
constexpr std::uint32_t CyanName = 0xffffff00u;
constexpr std::uint32_t GoldName = 0xff27d9ffu;
constexpr std::uint32_t RedName = 0xff0000ffu;
constexpr std::uint32_t WhiteName = 0xffffffffu;

std::uint32_t NameColorOf(std::uint8_t controlCode)
{
    if ((controlCode & 0x01) != 0)
        return CyanName;
    if ((controlCode & 0x12) != 0)
        return GoldName;
    // 0x63ccaf: con el 4, blanco antes de mirar el 8 y el 0x20.
    if ((controlCode & 0x04) != 0)
        return WhiteName;
    if ((controlCode & 0x08) != 0 || (controlCode & 0x20) != 0)
        return RedName;
    return WhiteName;
}

// 0x63cd0d..0x63cd4e: el texto del rango del gremio (+0x368): 0x80 el 1300, 0x40 el 1301, 0x20 el 1302, 0 el 1330 y
// 0xff (sin gremio, el de 0x511290) el 488; con otro, el 0 (eax en 0 desde 0x63ccfe).
std::size_t GuildTextOf(std::uint8_t status)
{
    switch (status)
    {
    case 0x80:
        return 0x514;
    case 0x40:
        return 0x515;
    case 0x20:
        return 0x516;
    case 0x00:
        return 0x532;
    case 0xff:
        return 0x1e8;
    default:
        return 0;
    }
}

// 0x63cd82..0x63cdac: el texto de la clase, 20 + (+0x1d4 >> 3) · 5 + (+0x1d4 & 7), como la ventana del personaje.
constexpr std::size_t FirstClassText = 20;

// "(%s)" (0x6c6cbc) en +0xc0 y "%s %d" (0x6d6574) en +0xd0: 0x63c9a0 limpia +0xac..+0xef, así que el primero tiene
// 16 bytes y el segundo 32, hasta el final del globo (+0xf0), donde empieza el que sigue.
constexpr std::size_t GuildBytes = 0x10;
constexpr std::size_t ClassBytes = 0x20;

// Los renglones de 0x63ca20: 0x5542f0(x, y, texto, 118, 2, 0), centrados en la caja de 118 columnas (0x467060 en el
// modo 2), en y + 7, + 0x17 y + 0x27 del borde de arriba, con la fuente de DAT_05763560 (0x63cab2) y el fondo de
// 0x6c8e64 en 0 (0x63cad1).
constexpr int NameTop = 0x07;
constexpr int GuildTop = 0x17;
constexpr int ClassTop = 0x27;
constexpr std::uint32_t GuildColor = 0xffffffffu;
constexpr std::uint32_t ClassColor = 0xff27d9ffu;

// 0x63cbbc..0x63cc50: con +0x1d8 & 0x80, que lee en cada dibujo, los textos 1241 a 1243 en 0xffb4c8ff, arriba de la
// caja (y − 46 + 16 · i): el primero con la negrita (DAT_05763558) y los otros con la normal (DAT_05763554).
constexpr std::uint8_t ExtraLinesFlag = 0x80;
constexpr std::size_t FirstExtraText = 1241;
constexpr int ExtraLines = 3;
constexpr int ExtraTop = -0x2e;
constexpr int ExtraStep = 0x10;
constexpr std::uint32_t ExtraColor = 0xffb4c8ffu;

std::int32_t WrappedAdd(std::int32_t a, std::int32_t b)
{
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(a) + std::bit_cast<std::uint32_t>(b));
}

std::string TextOf(const Tables::TextTable* texts, std::size_t index)
{
    return std::string(TextAt(texts, index).value_or(std::string_view{}));
}

// 0x5542f0(x, y, texto, 118, 2, 0) → 0x465950 → 0x467060 en el modo 2: la caja de 118 columnas desde x, con el texto
// en (118 − cx) / 2. La caja va aunque el texto esté vacío.
void DrawLine(Render::Frame::ITextDrawer& text, int x, int y, const std::string& line, FontKind font,
              std::uint32_t color)
{
    const Render::Text::TextStyle style{font, Render::Text::TextColors{color, 0}};
    const Render::Text::CenteredInBox placed = Render::Text::CenterInBox(text, font, line, BalloonWidth);
    text.DrawColumns(x, y, line, style, -placed.textColumn, placed.width);
}
} // namespace

CharacterBalloons::CharacterBalloons(TextureSizes sizes, const Tables::TextTable* texts)
    : m_sizes(std::move(sizes)), m_texts(texts)
{
}

void CharacterBalloons::Build()
{
    // 0x63ce70 arma el arreglo la primera vez (operator new de 0x4b4 y 0x6a0210 con 0x63c940) y, siempre, 0x63c9a0 con
    // cada registro: 0x647050 saca las UV del buffer del slot en ese momento.
    const Scenes::TextureBufferSize buffer = m_sizes ? m_sizes(BalloonSlot) : Scenes::TextureBufferSize{};
    for (CharacterBalloon& balloon : m_balloons)
    {
        balloon = CharacterBalloon{};
        balloon.image =
            PixelImage(BalloonSlot, Rect{.x = 0, .y = 0, .width = BalloonWidth, .height = BalloonHeight}, buffer);
    }
    m_built = true;
}

void CharacterBalloons::Release()
{
    m_built = false;
}

bool CharacterBalloons::IsBuilt() const
{
    return m_built;
}

Core::Expected<void, std::string> CharacterBalloons::Update(std::span<const BalloonRecord, BalloonCount> records)
{
    if (!m_built)
        return {};
    for (std::size_t index = 0; index < BalloonCount; ++index)
    {
        CharacterBalloon& balloon = m_balloons[index];
        const BalloonRecord& record = records[index];
        // 0x63cc71: el +0 del registro en 0 solo baja +0xa0.
        if (!record.alive)
        {
            balloon.shown = false;
            continue;
        }
        balloon.shown = true;
        balloon.nameColor = NameColorOf(record.controlCode);
        // 0x63ccdb..0x63cd03: el strcpy del nombre, de hasta 10 bytes y su 0, en los 16 de +0xb0.
        balloon.name = record.name;
        // 0x63cd70: sprintf("(%s)") en +0xc0, sin tope. Lo que pasa de 16 bytes cae en +0xd0, que el sprintf que sigue
        // pisa; lo que pasa de +0xf0 cae en el globo de al lado (o después del arreglo, en el último).
        const std::string guild = "(" + TextOf(m_texts, GuildTextOf(record.guildStatus)) + ")";
        if (guild.size() + 1 > GuildBytes + ClassBytes)
        {
            return Core::Unexpected(std::format("0x63cd70: el rango del gremio del lugar {} ocupa {} bytes con su 0 y "
                                                "pasa de los {} que quedan hasta el final del globo",
                                                index, guild.size() + 1, GuildBytes + ClassBytes));
        }
        // 0x63cdba: sprintf("%s %d") en +0xd0 con la clase y el nivel (el word, con movzx: sin signo).
        const std::uint8_t characterClass = record.characterClass;
        const std::size_t classText = FirstClassText + static_cast<std::size_t>(characterClass >> 3u) * 5u +
                                      static_cast<std::size_t>(characterClass & 7u);
        std::string classAndLevel = TextOf(m_texts, classText) + " " + std::to_string(record.level);
        if (classAndLevel.size() + 1 > ClassBytes)
        {
            return Core::Unexpected(std::format("0x63cdba: la clase y el nivel del lugar {} ocupan {} bytes con su 0 y "
                                                "pasan de los {} de +0xd0",
                                                index, classAndLevel.size() + 1, ClassBytes));
        }
        // Q-C1503: sin su 0 en los 16 bytes, 0x5542f0 lee +0xc0 hasta el 0 del de +0xd0.
        balloon.guild = guild.size() < GuildBytes ? guild : guild.substr(0, GuildBytes) + classAndLevel;
        balloon.classAndLevel = std::move(classAndLevel);
    }
    return {};
}

bool CharacterBalloons::IsShown(std::size_t index) const
{
    return m_built && index < BalloonCount && m_balloons[index].shown;
}

Core::Expected<void, std::string>
CharacterBalloons::Draw(Render::SpriteBatch& sprites, Render::Frame::ITextDrawer& text, Core::PixelSize screen,
                        std::span<const std::optional<BalloonHead>, BalloonCount> heads)
{
    if (!m_built)
        return {};
    // rx y ry (0x5762ea8 y 0x5762eac): W · (1/640) y H · (1/480), en float.
    const double ratioX = static_cast<double>(Render::ScaleX(1.0f, screen));
    const double ratioY = static_cast<double>(Render::ScaleY(1.0f, screen));
    // +0x08 de la imagen: H / fy en float, con fy = 1.
    const double height = static_cast<double>(static_cast<float>(screen.height));
    for (std::size_t index = 0; index < BalloonCount; ++index)
    {
        CharacterBalloon& balloon = m_balloons[index];
        if (!balloon.shown)
            continue;
        // 0x63ca47: 0x647650 dibuja la caja donde la dejó el dibujo anterior (en (0, 0) el primero), con su blanco.
        sprites.Image(balloon.image);
        const std::optional<BalloonHead>& head = heads[index];
        if (!head)
            return Core::Unexpected(std::format("0x63ca76: el globo {} se muestra sin su personaje proyectado", index));
        // 0x63ca7b..0x63caa0: fild del punto, fmul por ry o rx y _ftol; 0x6472e0(x, y, 3) resta el punto de +0x58 y
        // suma el de +0x5c a H − y, en la x87, y guarda el borde en float.
        const std::optional<std::int32_t> pixelX = Math::OriginalFtolLow(static_cast<double>(head->point.x) * ratioX);
        const std::optional<std::int32_t> pixelY = Math::OriginalFtolLow(static_cast<double>(head->point.y) * ratioY);
        if (!pixelX || !pixelY)
            return Core::Unexpected(std::format("0x63ca89: el punto del globo {} no entra en un entero", index));
        const auto left = static_cast<float>(static_cast<double>(*pixelX) - AnchorX);
        const auto topFromBottom = static_cast<float>(height - static_cast<double>(*pixelY) + AnchorY);
        // 0x63cab8..0x63cb15: _ftol de +0x18 y de +0x08 − +0x1c, el borde de arriba con la y desde arriba.
        const std::optional<std::int32_t> column = Math::OriginalFtolLow(static_cast<double>(left));
        const std::optional<std::int32_t> row = Math::OriginalFtolLow(height - static_cast<double>(topFromBottom));
        if (!column || !row)
            return Core::Unexpected(std::format("0x63cabb: el borde del globo {} no entra en un entero", index));
        balloon.image.x = *column;
        balloon.image.y = *row;
        // De los píxeles a 640 × 480: fild, fdiv por rx o ry y _ftol (Render::Text::DesignX y DesignY).
        const int x = Render::Text::DesignX(*column, screen);
        DrawLine(text, x, Render::Text::DesignY(WrappedAdd(*row, NameTop), screen), balloon.name, FontKind::OtherFace12,
                 balloon.nameColor);
        DrawLine(text, x, Render::Text::DesignY(WrappedAdd(*row, GuildTop), screen), balloon.guild,
                 FontKind::OtherFace12, GuildColor);
        DrawLine(text, x, Render::Text::DesignY(WrappedAdd(*row, ClassTop), screen), balloon.classAndLevel,
                 FontKind::OtherFace12, ClassColor);
        if ((head->controlCode & ExtraLinesFlag) == 0)
            continue;
        for (int line = 0; line < ExtraLines; ++line)
        {
            const FontKind font = line == 0 ? FontKind::Bold : FontKind::Normal;
            const int y = Render::Text::DesignY(WrappedAdd(*row, ExtraTop + line * ExtraStep), screen);
            DrawLine(text, x, y, TextOf(m_texts, FirstExtraText + static_cast<std::size_t>(line)), font, ExtraColor);
        }
    }
    return {};
}

const std::array<CharacterBalloon, BalloonCount>& CharacterBalloons::Balloons() const
{
    return m_balloons;
}
} // namespace Client::UI
