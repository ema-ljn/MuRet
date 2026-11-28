#include "Hud/MapBanner.h"

#include "Core/Expected.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/TextureSet.h"
#include "Math/FloatConstants.h"
#include "Render/ITextureStore.h"

#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Hud
{
namespace
{
// 0x65d080, en su orden: las cadenas de 0x6d6808..0x6d6990.
constexpr std::string_view Lorencia = "Interface\\lorencia.tga";
constexpr std::string_view BloodCastle = "Interface\\bloodcastle.tga";
constexpr std::string_view ChaosCastle = "Interface\\chaoscastle.tga";
constexpr std::string_view Kalima = "Interface\\Kalima.tga";
constexpr auto Files = std::to_array<std::pair<std::int32_t, std::string_view>>({
    {0, Lorencia},
    {1, "Interface\\dungeun.tga"},
    {2, "Interface\\devias.tga"},
    {3, "Interface\\noria.tga"},
    {4, "Interface\\losttower.tga"},
    {6, "Interface\\stadium.tga"},
    {7, "Interface\\atlans.tga"},
    {8, "Interface\\tarcan.tga"},
    {9, "Interface\\devilsquare.tga"},
    {10, "Interface\\Icarus.tga"},
    {11, BloodCastle},
    {12, BloodCastle},
    {13, BloodCastle},
    {14, BloodCastle},
    {15, BloodCastle},
    {16, BloodCastle},
    {17, BloodCastle},
    {18, ChaosCastle},
    {19, ChaosCastle},
    {20, ChaosCastle},
    {21, ChaosCastle},
    {22, ChaosCastle},
    {23, ChaosCastle},
    {24, Kalima},
    {25, Kalima},
    {26, Kalima},
    {27, Kalima},
    {28, Kalima},
    {29, Kalima},
    {30, "Interface\\loren.tga"},
    {31, "Interface\\ordeal.tga"},
    {33, "Interface\\aida.tga"},
    {34, "Interface\\crywolffortress.tga"},
    {36, Kalima},
});

// 0x65dc30: el alfa con que entra.
constexpr float ShowAlpha = std::bit_cast<float>(0x3e4ccccdu); // 0,2
// 0x6b8a24, 0x6b87d4 y 0x6b86a0, de la .rdata.
constexpr float FadeStep = std::bit_cast<float>(0x3c75c28fu); // 0,015
constexpr double FadeInEnd = 1.0;
constexpr double FadeOutEnd = 0.0;
// 0x65dd20: lo que se queda (CMP con 0x1388, JBE).
constexpr std::uint32_t HoldMilliseconds = 5000;
// 0x6ba69c, 0x6baad8 y 0x6b882c.
constexpr double BannerTop = 220.0;
constexpr double BannerWidth = 166.0;
constexpr double Half = Math::HalfAsDouble;
} // namespace

MapBanner MakeTheMapBanner()
{
    MapBanner banner;
    for (const auto& [map, file] : Files)
        banner.files[map] = std::string(file);
    return banner;
}

void ResetTheMapBanner(MapBanner& banner, std::uint32_t now)
{
    banner.state = MapBannerState::Off;
    banner.shownMap = -1;
    banner.last = now;
    banner.elapsed = 0;
    banner.alpha = 1.0f;
}

void ShowTheMapBanner(MapBanner& banner, std::int32_t map)
{
    banner.state = MapBannerState::FadingIn;
    banner.alpha = ShowAlpha;
    banner.elapsed = 0;
    // 0x65dc3c..0x65dc5b: MOVSX del word +0x14 contra el int de 0x6c9dd0.
    if (static_cast<std::int32_t>(banner.shownMap) == map)
        return;
    banner.pendingImage = banner.files[map];
    banner.shownMap = static_cast<std::int16_t>(map);
}

void StepTheMapBanner(MapBanner& banner, std::uint32_t now)
{
    // 0x65dcde: la diferencia con el reloj de antes, sin signo.
    const std::uint32_t passed = now - banner.last;
    switch (banner.state)
    {
    case MapBannerState::FadingIn:
    {
        // 0x65dd3a..0x65dd55: FADD y FST al float, y FCOMP de la suma de la x87 contra 1.
        const double sum = static_cast<double>(banner.alpha) + static_cast<double>(FadeStep);
        banner.alpha = static_cast<float>(sum);
        if (FadeInEnd <= sum)
        {
            banner.state = MapBannerState::Holding;
            banner.alpha = 1.0f;
        }
        break;
    }
    case MapBannerState::Holding:
        banner.elapsed += passed;
        if (banner.elapsed > HoldMilliseconds)
        {
            banner.state = MapBannerState::FadingOut;
            banner.elapsed = 0;
            banner.last = now;
            return;
        }
        break;
    case MapBannerState::FadingOut:
    {
        const double sum = static_cast<double>(banner.alpha) - static_cast<double>(FadeStep);
        banner.alpha = static_cast<float>(sum);
        if (sum <= FadeOutEnd)
        {
            banner.last = now;
            banner.state = MapBannerState::Off;
            banner.alpha = 0.0f;
            return;
        }
        break;
    }
    case MapBannerState::Off:
        break;
    }
    banner.last = now;
}

Core::Expected<void, std::string> UploadTheMapBanner(MapBanner& banner, const std::filesystem::path& dataRoot,
                                                     Render::ITextureStore& textures)
{
    if (!banner.pendingImage)
        return {};
    const std::string image = std::move(*banner.pendingImage);
    banner.pendingImage.reset();
    // 0x65dc8a..0x65dcb3: 0x63bd00 con GL_NEAREST (0x2600), GL_CLAMP (0x2900), 0 y 1.
    const Data::Textures::TextureSetEntry entry{
        .path = image,
        .slot = MapBannerSlot,
        .sampling = {Data::Textures::Filter::Nearest, Data::Textures::Wrap::Clamp}};
    auto loaded = Data::Textures::LoadTextureSet(dataRoot, std::span(&entry, 1));
    if (!loaded.HasValue())
    {
        const Data::Textures::TextureSetError& error = loaded.Error();
        if (error.kind == Data::Textures::TextureSetErrorKind::Unreadable)
            return {};
        return Core::Unexpected(
            std::format("0x65dcb3: la imagen \"{}\" del cartel del mapa {} no está: el cargador del OZT "
                        "avisa \"File not exist\" con un MessageBox y cierra el juego (Q-C23)",
                        image, banner.shownMap));
    }
    auto uploaded = textures.Load(loaded.Value());
    if (!uploaded.HasValue())
        return Core::Unexpected(uploaded.Error());
    return {};
}

MapBannerQuad MapBannerQuadOf(std::int32_t worldWidth, float rx, float ry)
{
    // 0x65dde3..0x65ddf8: FILD del ancho, FMUL rx, FSUB 166, FMUL 0,5 y FSTP; 0x65ddac: FLD ry, FMUL 220 y FSTP.
    const double x = (static_cast<double>(worldWidth) * static_cast<double>(rx) - BannerWidth) * Half;
    const double y = static_cast<double>(ry) * BannerTop;
    return {.x = static_cast<float>(x), .y = static_cast<float>(y)};
}
} // namespace Client::Hud
