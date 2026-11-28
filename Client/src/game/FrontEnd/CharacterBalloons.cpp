#include "FrontEnd/CharacterBalloons.h"

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
#include "Ui/GameText.h"
#include "Ui/Widget.h"
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

namespace Client::FrontEnd
{
namespace
{
using Render::Text::FontKind;

// 0x63c9a0 → 0x647050(0x76, 0x36, 0x13, 0, 0, 0x3b, 0x36, 0, 0, 1, 1): character_ex.tga (slot 0x13 of 0x61ca30),
// 118 × 54, in pixels (fx = fy = 1), with the point of +0x58 and +0x5c at (59, 54): 0x6472e0(x, y, 3) leaves it
// centered on x with its bottom edge at y.
constexpr Render::TextureSlot BalloonSlot{0x13};
constexpr int BalloonWidth = 0x76;
constexpr int BalloonHeight = 0x36;
constexpr double AnchorX = 0x3b;
constexpr double AnchorY = 0x36;

// 0x63cc60: the name color (+0xac) by the CtlCode, in the order of its tests, in 0xAABBGGRR.
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
    // 0x63ccaf: with the 4, white before checking the 8 and the 0x20.
    if ((controlCode & 0x04) != 0)
        return WhiteName;
    if ((controlCode & 0x08) != 0 || (controlCode & 0x20) != 0)
        return RedName;
    return WhiteName;
}

// 0x63cd0d..0x63cd4e: the guild rank text (+0x368): 0x80 the 1300, 0x40 the 1301, 0x20 the 1302, 0 the 1330 and
// 0xff (no guild, the one of 0x511290) the 488; with another, the 0 (eax at 0 from 0x63ccfe).
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

// 0x63cd82..0x63cdac: the class text, 20 + (+0x1d4 >> 3) · 5 + (+0x1d4 & 7), like the character window.
constexpr std::size_t FirstClassText = 20;

// "(%s)" (0x6c6cbc) at +0xc0 and "%s %d" (0x6d6574) at +0xd0: 0x63c9a0 clears +0xac..+0xef, so the first has 16
// bytes and the second 32, up to the end of the balloon (+0xf0), where the next one starts.
constexpr std::size_t GuildBytes = 0x10;
constexpr std::size_t ClassBytes = 0x20;

// The rows of 0x63ca20: 0x5542f0(x, y, text, 118, 2, 0), centered in the 118-column box (0x467060 in mode 2), at
// y + 7, + 0x17 and + 0x27 from the top edge, with the font of DAT_05763560 (0x63cab2) and the background of
// 0x6c8e64 at 0 (0x63cad1).
constexpr int NameTop = 0x07;
constexpr int GuildTop = 0x17;
constexpr int ClassTop = 0x27;
constexpr std::uint32_t GuildColor = 0xffffffffu;
constexpr std::uint32_t ClassColor = 0xff27d9ffu;

// 0x63cbbc..0x63cc50: with +0x1d8 & 0x80, which it reads on each draw, texts 1241 to 1243 in 0xffb4c8ff, above the
// box (y − 46 + 16 · i): the first with the bold font (DAT_05763558) and the others with the normal one
// (DAT_05763554).
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
    return std::string(Ui::TextAt(texts, index).value_or(std::string_view{}));
}

// 0x5542f0(x, y, text, 118, 2, 0) → 0x465950 → 0x467060 in mode 2: the 118-column box from x, with the text at
// (118 − cx) / 2. The box is drawn even if the text is empty.
void DrawLine(Render::Frame::ITextDrawer& text, int x, int y, const std::string& line, FontKind font,
              std::uint32_t color)
{
    const Render::Text::TextStyle style{font, Render::Text::TextColors{color, 0}};
    const Render::Text::CenteredInBox placed = Render::Text::CenterInBox(text, font, line, BalloonWidth);
    text.DrawColumns(x, y, line, style, -placed.textColumn, placed.width);
}
} // namespace

CharacterBalloons::CharacterBalloons(Ui::TextureSizes sizes, const Tables::TextTable* texts)
    : m_sizes(std::move(sizes)), m_texts(texts)
{
}

void CharacterBalloons::Build()
{
    // 0x63ce70 builds the array the first time (operator new of 0x4b4 and 0x6a0210 with 0x63c940) and, always, 0x63c9a0
    // with each record: 0x647050 takes the UVs from the slot's buffer at that moment.
    const Assets::TextureBufferSize buffer = m_sizes ? m_sizes(BalloonSlot) : Assets::TextureBufferSize{};
    for (CharacterBalloon& balloon : m_balloons)
    {
        balloon = CharacterBalloon{};
        balloon.image = Ui::PixelImage(
            BalloonSlot, Ui::Rect{.x = 0, .y = 0, .width = BalloonWidth, .height = BalloonHeight}, buffer);
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
        // 0x63cc71: the record's +0 at 0 only clears +0xa0.
        if (!record.alive)
        {
            balloon.shown = false;
            continue;
        }
        balloon.shown = true;
        balloon.nameColor = NameColorOf(record.controlCode);
        // 0x63ccdb..0x63cd03: the strcpy of the name, up to 10 bytes and its 0, into the 16 of +0xb0.
        balloon.name = record.name;
        // 0x63cd70: sprintf("(%s)") at +0xc0, without a limit. What passes 16 bytes falls into +0xd0, which the next
        // sprintf overwrites; what passes +0xf0 falls into the next balloon (or after the array, in the last one).
        const std::string guild = "(" + TextOf(m_texts, GuildTextOf(record.guildStatus)) + ")";
        if (guild.size() + 1 > GuildBytes + ClassBytes)
        {
            return Core::Unexpected(std::format("0x63cd70: el rango del gremio del lugar {} ocupa {} bytes con su 0 y "
                                                "pasa de los {} que quedan hasta el final del globo",
                                                index, guild.size() + 1, GuildBytes + ClassBytes));
        }
        // 0x63cdba: sprintf("%s %d") at +0xd0 with the class and the level (the word, with movzx: unsigned).
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
        // Q-C1503: without its 0 in the 16 bytes, 0x5542f0 reads +0xc0 up to the 0 of the one at +0xd0.
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
    // rx and ry (0x5762ea8 and 0x5762eac): W · (1/640) and H · (1/480), as floats.
    const double ratioX = static_cast<double>(Render::ScaleX(1.0f, screen));
    const double ratioY = static_cast<double>(Render::ScaleY(1.0f, screen));
    // +0x08 of the image: H / fy as a float, with fy = 1.
    const double height = static_cast<double>(static_cast<float>(screen.height));
    for (std::size_t index = 0; index < BalloonCount; ++index)
    {
        CharacterBalloon& balloon = m_balloons[index];
        if (!balloon.shown)
            continue;
        // 0x63ca47: 0x647650 draws the box where the previous draw left it (at (0, 0) the first time), with its white.
        sprites.Image(balloon.image);
        const std::optional<BalloonHead>& head = heads[index];
        if (!head)
            return Core::Unexpected(std::format("0x63ca76: el globo {} se muestra sin su personaje proyectado", index));
        // 0x63ca7b..0x63caa0: fild of the point, fmul by ry or rx and _ftol; 0x6472e0(x, y, 3) subtracts the point of
        // +0x58 and adds the one of +0x5c to H − y, on the x87, and stores the edge as a float.
        const std::optional<std::int32_t> pixelX = Math::OriginalFtolLow(static_cast<double>(head->point.x) * ratioX);
        const std::optional<std::int32_t> pixelY = Math::OriginalFtolLow(static_cast<double>(head->point.y) * ratioY);
        if (!pixelX || !pixelY)
            return Core::Unexpected(std::format("0x63ca89: el punto del globo {} no entra en un entero", index));
        const auto left = static_cast<float>(static_cast<double>(*pixelX) - AnchorX);
        const auto topFromBottom = static_cast<float>(height - static_cast<double>(*pixelY) + AnchorY);
        // 0x63cab8..0x63cb15: _ftol of +0x18 and of +0x08 − +0x1c, the top edge with y from the top.
        const std::optional<std::int32_t> column = Math::OriginalFtolLow(static_cast<double>(left));
        const std::optional<std::int32_t> row = Math::OriginalFtolLow(height - static_cast<double>(topFromBottom));
        if (!column || !row)
            return Core::Unexpected(std::format("0x63cabb: el borde del globo {} no entra en un entero", index));
        balloon.image.x = *column;
        balloon.image.y = *row;
        // From pixels to 640 × 480: fild, fdiv by rx or ry and _ftol (Render::Text::DesignX and DesignY).
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
} // namespace Client::FrontEnd
