#pragma once

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Tables/TextTable.h"
#include "Render/SpriteBatch.h"
#include "Render/UiImage.h"
#include "Render/Frame/ITextDrawer.h"
#include "Ui/Widget.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

// The selection's balloons (06 § La selección de personaje, "Los globos"): the dark box of character_ex.tga with the
// name, the guild rank and the class with the level above each character of the list. It is the object at +0x8dc8 of
// the new UI, with an array of five 0xf0-byte balloons at +4, each with the 0xa8-byte image of 0x646fe0 in front:
// 0x63ce70 builds it from 0x65f4b0, 0x63cfb0 updates it and 0x63cf80 draws it from 0x65fe00, before the windows.
// 0x63ce50 releases it at login (0x65f300) and in the game (0x65f5f0): there are balloons only in mode 4. *Static*.
namespace Client::FrontEnd
{
// The balloons of 0x63ce70, one per selection slot, in the pool's order (0x743a16c + 0x438 · i).
inline constexpr std::size_t BalloonCount = 5;

// What 0x63ca20 adds to the character's z before projecting it (0x6b8c78).
inline constexpr float BalloonHeadHeight = 280.0f;

// What 0x63cc60 reads from the slot's record: +0 (alive), the class (+0x1d4), the level (+0x1d6), the CtlCode
// (+0x1d8), the name (+0x1d9, up to its 0) and the guild rank (+0x368).
struct BalloonRecord
{
    bool alive = false;
    std::uint8_t characterClass = 0;
    std::uint16_t level = 0;
    std::uint8_t controlCode = 0;
    std::string name;
    std::uint8_t guildStatus = 0;
};

// What 0x63ca20 reads from the record on each draw: the character at z + 280 projected with 0x6205b0 (640 × 480) and
// the current CtlCode (+0x1d8), for the three rows of 0x80.
struct BalloonHead
{
    Ui::Point point;
    std::uint8_t controlCode = 0;
};

// A balloon (0x63c9a0, 0x63cc60 and 0x63ca20): the image and the three texts.
struct CharacterBalloon
{
    // The image of 0x647050(118, 54, 0x13, 0, 0, 59, 54, 0, 0, 1, 1), in pixels: at (0, 0) until 0x63ca20 places it,
    // each time after drawing it.
    Render::UiImage image;
    // +0xa0: 0x63cc60 sets it with a live record and clears it with a dead one; 0x647050 leaves it at 0.
    bool shown = false;
    // +0xac, by the CtlCode.
    std::uint32_t nameColor = 0;
    // +0xb0, +0xc0 and +0xd0, as 0x5542f0 reads them (up to the first 0): the name, "(%s)" with the guild rank and
    // "%s %d" with the class and the level. The one at +0xc0 has 16 bytes: if it does not fit, it goes on into the one
    // at +0xd0 (Q-C1503).
    std::string name;
    std::string guild;
    std::string classAndLevel;
};

class CharacterBalloons
{
public:
    // `texts` can be nullptr (empty texts) and has to outlive the balloons.
    CharacterBalloons(Ui::TextureSizes sizes, const Tables::TextTable* texts);

    // 0x63ce70: the five, each with 0x63c9a0: the image built again at (0, 0), not shown, without color and without
    // texts.
    void Build();
    // 0x63ce50: without balloons, 0x63cf80 and 0x63cfb0 do nothing.
    void Release();
    [[nodiscard]] bool IsBuilt() const;

    // 0x63cfb0 → 0x63cc60 on the five: one with a dead record only hides, and keeps the texts; a live one takes the
    // color and the texts. Stops with its address if a sprintf passes the balloon's 0xf0 bytes (0x63cd70 and 0x63cdba).
    [[nodiscard]] Core::Expected<void, std::string> Update(std::span<const BalloonRecord, BalloonCount> records);

    [[nodiscard]] bool IsShown(std::size_t index) const;

    // 0x63cf80 → 0x63ca20 on the five, with whatever blend is set: the box where it was and, then, the new place and
    // the texts. Each shown balloon carries its BalloonHead; it stops without it (0x63ca76).
    [[nodiscard]] Core::Expected<void, std::string>
    Draw(Render::SpriteBatch& sprites, Render::Frame::ITextDrawer& text, Core::PixelSize screen,
         std::span<const std::optional<BalloonHead>, BalloonCount> heads);

    [[nodiscard]] const std::array<CharacterBalloon, BalloonCount>& Balloons() const;

private:
    Ui::TextureSizes m_sizes;
    const Tables::TextTable* m_texts = nullptr;
    bool m_built = false;
    std::array<CharacterBalloon, BalloonCount> m_balloons{};
};
} // namespace Client::FrontEnd
