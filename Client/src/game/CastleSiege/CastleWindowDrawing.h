#pragma once

#include "Core/Expected.h"
#include "CastleSiege/CastleWindowSupport.h"
#include "Hud/GameHud.h"
#include "CastleSiege/CastleSeniorWindow.h"
#include "CastleSiege/CastleSiegeState.h"
#include "Ui/OldWindowBase.h"
#include "Crt/SprintfFormat.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace Client::CastleSiege::CastleDraw
{
void Center(Hud::GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text,
            std::int32_t width = 190);
[[nodiscard]] Core::Expected<void, std::string> Text(Hud::GameHudContext& context, std::int32_t x, std::int32_t y,
                                                     std::uint32_t address, std::int32_t width = 190);
template <typename... Args>
[[nodiscard]] Core::Expected<void, std::string> FormattedWidth(Hud::GameHudContext& context, std::int32_t x,
                                                               std::int32_t y, std::int32_t width,
                                                               std::uint32_t address, Args... args)
{
    auto text = CastleUi::Text(context.texts, address);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    Center(context, x, y, Crt::Sprintf(text.Value(), args...), width);
    return {};
}
template <typename... Args>
[[nodiscard]] Core::Expected<void, std::string> Formatted(Hud::GameHudContext& context, std::int32_t x, std::int32_t y,
                                                          std::uint32_t address, Args... args)
{
    return FormattedWidth(context, x, y, 190, address, args...);
}
[[nodiscard]] Core::Expected<void, std::string> Button(Hud::GameHudContext& context, Ui::OldButton& button,
                                                       std::int32_t x, std::int32_t y, std::uint32_t label,
                                                       std::int32_t state);
[[nodiscard]] Core::Expected<void, std::string> Back(Hud::GameHudContext& context, std::int32_t x, std::uint32_t title);
[[nodiscard]] Core::Expected<void, std::string> Close(Hud::GameHudContext& context, std::int32_t x);
[[nodiscard]] Core::Expected<void, std::string> Registration(Hud::GameHudContext& context,
                                                             CastleSiege::SiegeRegistrationWindow& window);
[[nodiscard]] Core::Expected<void, std::string> Hunt(Hud::GameHudContext& context, CastleSiege::HuntZoneWindow& window);
[[nodiscard]] Core::Expected<void, std::string> Senior(Hud::GameHudContext& context,
                                                       CastleSiege::CastleSeniorWindow& window,
                                                       CastleSiege::CastleData& data, std::int32_t tab);
[[nodiscard]] Core::Expected<void, std::string>
GuildList(Hud::GameHudContext& context, CastleSiege::SiegeRegistrationWindow& window, bool registered);
} // namespace Client::CastleSiege::CastleDraw
