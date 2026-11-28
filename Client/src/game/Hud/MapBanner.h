#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

// [0x5763534], the map name banner (vtable 0x6baad4): the image Interface\<map>.tga in slot 0x66a that comes in,
// stays and goes out over the world on arriving at a map. WinMain builds it (0x4bb9b8, 0x65cd70 → 0x65d080); the first
// step of scene 5 resets it (0x63692b, 0x65dc00); the world entry of the F3 03 (0x4c4b28), the respawn of the F3 04
// (0x4c504e) and the 1C with a gate (0x4c831c) trigger it with 0x65dc30; and the HUD's 19th draws it (0x5a74ca,
// 0x65dd70), which runs the fade (0x65dcd0). The rest of its .text stretch is MSVC6's std::map templates of +8 (the
// red-black tree, its 0x24 nodes, operator[] and the destructors), which std::map covers, and the banner's
// destructors, which MapBanner's covers (triage of the reference).
namespace Client::Render
{
class ITextureStore;
}

namespace Client::Hud
{
// +0x18, an int in the original (0 to 3).
enum class MapBannerState : std::uint8_t
{
    Off = 0,
    FadingIn = 1,
    Holding = 2,
    FadingOut = 3,
};

struct MapBanner
{
    // +8: the map → the image, filled by 0x65d080. The tree's operator[] inserts an empty one for a missing one.
    std::map<std::int32_t, std::string> files;
    // +0x14 (a word): the map of the image in slot 0x66a; −1 from 0x65dc00.
    std::int16_t shownMap = -1;
    MapBannerState state = MapBannerState::Off;
    // +0x1c: the last step's timeGetTime(); +0x20: how long it has been still, in ms.
    std::uint32_t last = 0;
    std::uint32_t elapsed = 0;
    // +0x24: the alpha of the draw's glColor4f.
    float alpha = 0.0f;
    // MuRet: the image 0x65dc30 loads into slot 0x66a with 0x63bd00 (no GPU in the package, the scene uploads it before
    // the draw, UploadTheMapBanner). Empty, a map with no image: "File not exist" and the client closes.
    std::optional<std::string> pendingImage;
};

// 0x63bd00(image, 0x66a, GL_NEAREST, GL_CLAMP, 0, 1) with 0x65dc30's pending image: the OZT from Data\ into slot
// 0x66a. A missing one, or the empty name of a map with no image (no dot, Q-C74), builds "File not exist", a
// MessageBox and WM_DESTROY with the sixth argument at 1: it stops (Q-C23). One that exists and does not read is not
// uploaded and gives no warning (Q-C14): the slot keeps the previous one. With none pending, nothing.
[[nodiscard]] Core::Expected<void, std::string>
UploadTheMapBanner(MapBanner& banner, const std::filesystem::path& dataRoot, Render::ITextureStore& textures);

// 0x65cd70 → 0x65d080: the table, without the state (+0x14..+0x24 it leaves unwritten, and nobody reads them before
// 0x65dc00).
[[nodiscard]] MapBanner MakeTheMapBanner();

// 0x65dc00: off, +0x14 at −1, the clock (`now`, timeGetTime), the count at 0 and the alpha at 1.
void ResetTheMapBanner(MapBanner& banner, std::uint32_t now);

// 0x65dc30: comes in (1) with the alpha at 0.2 (0x3e4ccccd) and the count at 0. With `map` (the map of 0x6c9dd0)
// other than +0x14 (the signed word against the int), operator[] gives its image (with no string, the empty one at
// 0x6b85ec), which goes to slot 0x66a with 0x63bd00(image, 0x66a, GL_NEAREST, GL_CLAMP, 0, 1), and +0x14 keeps the map.
void ShowTheMapBanner(MapBanner& banner, std::int32_t map);

// 0x65dcd0, on each draw of 0x65dd70: comes in by 0.015 (0x6b8a24) until the x87 sum reaches 1 (0x6b87d4), where it
// stays (2) with the alpha at 1; it stays until 5000 ms of timeGetTime (unsigned) pass and goes out (3) by 0.015 until
// the sum reaches 0 (0x6b86a0), where it turns off with the alpha at 0. Saves the clock on each step.
void StepTheMapBanner(MapBanner& banner, std::uint32_t now);

// 0x65dd70: the 166 × 90 banner with UVs 0.6484375 × 0.703125 (0x621b60 without scaling the size or the position), at
// x = (the world's width (0x5bb8d0) · rx − 166) · 0.5 and y = ry · 220, in screen pixels.
struct MapBannerQuad
{
    float x = 0.0f;
    float y = 0.0f;
};
[[nodiscard]] MapBannerQuad MapBannerQuadOf(std::int32_t worldWidth, float rx, float ry);
inline constexpr std::uint16_t MapBannerSlot = 0x66a;
inline constexpr float MapBannerWidth = 166.0f;
inline constexpr float MapBannerHeight = 90.0f;
inline constexpr float MapBannerDu = 0.6484375f;
inline constexpr float MapBannerDv = 0.703125f;
} // namespace Client::Hud
