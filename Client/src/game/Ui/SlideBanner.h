#pragma once

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Client::Tables
{
struct SlideTable;
}

namespace Client::Render
{
class SpriteBatch;
}

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Render::Text
{
class ITextMeasure;
}

namespace Client::Render::Frame
{
class ITextDrawer;
}

// The banner that crosses the screen in the game (CL8 milestone 3; 06 § El cartel de Slide.bmd): the object
// [0x5763528] (0x46aea0, 0x121c bytes) with its two queues (0x46a020, 0x8d4 bytes: the normal one at +4 and the bold
// font one at +0x8d8) and the Slide.bmd ranges. The 0x3eb timer (0x46b0e0, Hud/Slides) and the 0D of 10 to 15
// (0x4c66b3) fill it, the UI step advances it (0x46b7b0) and HUD drawing 15 draws it (0x46b090). None of these
// functions draws a rand() except 0x46b680. Each byte is a letter (Q-C67): _mbclen returns 1.
namespace Client::Ui
{
inline constexpr std::size_t SlideGroups = 5;

// A queue entry: the value of the +0x84 multimap (the node + 0x10..+0x20).
struct SlideEntry
{
    // +0x10: what 0x46b720 asks for minus 1 or minus 4. With −1 it expires 60 seconds after its key (0x46ae07).
    std::int32_t kind = 0;
    // +0x14. 0x46abf0 makes one copy per call and the last repeat frees it (+0x20); MuRet keeps one per entry, because
    // the repeats always come out in order and the last one is the one furthest back.
    std::string text;
    // +0x18: 0xAABBGGRR, like 0x6c8e5c.
    std::uint32_t color = 0;
    // +0x1c (0x46abc0).
    float speed = 0.0f;
};

// 0x46a020: a queue. What the constructor does not write (+0x50, +0x54, +0x7c, +0x80 and the text) 0x46b010 sets on
// the game's first step.
struct SlideQueue
{
    // +0x50 and +0x54 (0x45a9d0): where it goes, in 640 × 480. The y rises once by the letter height (0x46a770).
    std::int32_t x = 0;
    std::int32_t y = 0;
    // +0x7c and +0x80: the time of the last counted second (timeGetTime) and the seconds so far (0x46ada0).
    std::uint32_t lastTick = 0;
    std::uint32_t seconds = 0;
    // +0x84: the multimap by the unsigned second they go out. With the same key, in the order they came in.
    std::multimap<std::uint32_t, SlideEntry> entries;
    // +0xa4 (0x5763554 or 0x5763558).
    Render::Text::FontKind font = Render::Text::FontKind::Normal;
    // +0xa8: what is scrolling, in the current 0x400-byte buffer (+0xac or +0x4ac).
    std::string text;
    // +0x8ac..+0x8b8: the scroll, the push (1, or −1 with the mouse over it), the speed and the maximum speed (2.5 from
    // the constructor). The scroll adds to the x; negative, it is text columns that are not seen (0x467060 with mode 6,
    // Q-C519).
    float scroll = 0.0f;
    float push = 1.0f;
    float velocity = 0.0f;
    float maxSpeed = 2.5f;
    // +0x8bc: the opacity, from 0 to 205 by 30.
    std::int32_t alpha = 0;
    // +0x8c0 and +0x8c4: the bytes and the pixel width of the front chunk, of up to 4 letters (0x46ab30).
    std::uint32_t chunkBytes = 0;
    std::int32_t chunkWidth = 0;
    // +0x8c8: the letter height in 640 × 480 (that of "Z"), 0 until the first drawing with text.
    std::int32_t lineHeight = 0;
    // +0x8cc: the color of the scrolling text.
    std::uint32_t color = 0;
    // +0x8d0: with 1, the text's alpha drops to half 5 of every 10 draws. 0x46b010 passes 0 and nobody else writes it.
    std::int32_t blink = 0;
};

struct SlideBanner
{
    // +4 and +0x8d8.
    SlideQueue normal{};
    SlideQueue bold{};
    // +0x11ac: the timer's period, in seconds (10 from the constructor).
    std::int32_t periodSeconds = 10;
    // +0x11b0, +0x11c4 and +0x11d8: the Slide.bmd ranges (Tables::SlideGroup).
    std::array<std::int32_t, SlideGroups> maxLevels{};
    std::array<std::int32_t, SlideGroups> counts{};
    std::array<std::vector<std::string>, SlideGroups> texts{};
    // +0x1218: the speed of the timer's banners (2.5 from the constructor).
    float speed = 2.5f;
    // The window's 0x3eb timer: when it fires (in timeGetTime ms) and how often, from the first 0x46b010. The binary
    // does not import KillTimer: it never stops.
    std::optional<std::uint32_t> timerDue{};
    std::uint32_t timerPeriod = 0;
    // 0x5762ee4 (.bss): the texts the banner drew, which the blink reads.
    std::int32_t drawn = 0;
};

// 0x46b3b0 after reading the file: the period, the speed and the ranges. If there were texts, it first empties the
// five ranges (0x46b5b0).
void LoadTheSlides(SlideBanner& banner, const Tables::SlideTable& table);

// 0x46a460(font, blink) and 0x46b010's two 0x45a9d0(0, 0x1ad): the two queues without text and at (0, 0x1ad), with
// second 10 from `now`. It does not touch their entries, their maximum speed or their color.
void ResetTheSlideQueues(SlideBanner& banner, std::uint32_t now);

// 0x46b680: the first range whose cap is at least the level and, if its count is not 0, a rand() modulo the count
// (IDIV) and the text at that place, if the list has it. Empty, the NULL that queues nothing.
[[nodiscard]] std::optional<std::string> PickASlide(SlideBanner& banner, std::uint16_t level, Crt::CrtRandom& random);

// 0x46b720 without the scene, which the caller checks: with a non-empty text, 1 to 30 repeats and the kind 0 to 5,
// 0x46abf0 queues `repeats` entries in the normal queue (0 to 2) or the bold one (3 to 5), with the kind minus 1 or
// minus 4: with 0, all with key 0, and otherwise the i-th at the queue's second plus i · `gap`.
void QueueTheSlide(SlideBanner& banner, std::int32_t repeats, std::uint16_t gap, std::string_view text,
                   std::int32_t kind, float speed, std::uint32_t color);

// What the queues' step measures and leaves: GetTextExtentPointA, 0x46a9c0's SelectObject and rx.
struct SlideStepContext
{
    // Without a measure, a queue that has to add a text bails out with an error.
    const Render::Text::ITextMeasure* measure = nullptr;
    Render::Text::FontKind& dcFont;
    Core::PixelSize screen{640, 480};
    // timeGetTime().
    std::uint32_t now = 0;
};

// 0x46b7b0: 0x46add0 on the bold one and then on the normal one. A queue with nothing to scroll counts its second
// and, if its first entry is due, appends it to the text with spaces in front (0x46a9c0; if it does not fit in 0x400
// bytes, it waits), sets its speed (0x46abc0) and removes it.
[[nodiscard]] Core::Expected<void, std::string> StepTheSlides(SlideBanner& banner, const SlideStepContext& context);

// 0x46a510: without text, or with fewer bytes than its front chunk.
[[nodiscard]] bool SlideQueueIsIdle(const SlideQueue& queue);

// What the drawing uses and leaves: the batch, the texts, the DC's font and 0x465950's colors, the mouse in
// 640 × 480 and 0x6c7a8c.
struct SlideDrawContext
{
    Render::SpriteBatch& sprites;
    Render::Frame::ITextDrawer& text;
    Render::Text::FontKind& dcFont;
    Render::Text::TextColors& textColors;
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    bool slidesOn = true;
};

// 0x46b090: with the bold one scrolling, the normal one fades out (0x46a540(1)); with the bold one empty and already
// faded, the normal one (0x46a540(0)); then, with the normal one faded, the bold one. Each queue raises or lowers its
// opacity, draws its three black strips and, if it has something, its text with 0x467060's mode 6, and scrolls it. It
// bails out in 0x46a850 if it would have to drop more bytes from the front than remain (Q-C518).
[[nodiscard]] Core::Expected<void, std::string> DrawTheSlides(SlideBanner& banner, const SlideDrawContext& context);
} // namespace Client::Ui
