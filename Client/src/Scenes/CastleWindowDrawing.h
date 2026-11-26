#pragma once

#include "Core/Expected.h"
#include "Scenes/CastleWindowSupport.h"
#include "Scenes/GameHud.h"
#include "UI/CastleSeniorWindow.h"
#include "UI/CastleSiegeState.h"
#include "UI/OldWindowBase.h"
#include "Crt/SprintfFormat.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Scenes::CastleDraw
{
void Center(GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text, std::int32_t width = 190);
[[nodiscard]] Core::Expected<void, std::string> Text(GameHudContext& context, std::int32_t x, std::int32_t y,
                                                     std::uint32_t address, std::int32_t width = 190);
template <typename... Args>
[[nodiscard]] Core::Expected<void, std::string> FormattedWidth(GameHudContext& context, std::int32_t x, std::int32_t y,
                                                               std::int32_t width, std::uint32_t address, Args... args)
{
    auto text = CastleUi::Text(context.texts, address);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    Center(context, x, y, Crt::Sprintf(text.Value(), args...), width);
    return {};
}
template <typename... Args>
[[nodiscard]] Core::Expected<void, std::string> Formatted(GameHudContext& context, std::int32_t x, std::int32_t y,
                                                          std::uint32_t address, Args... args)
{
    return FormattedWidth(context, x, y, 190, address, args...);
}
[[nodiscard]] Core::Expected<void, std::string> Button(GameHudContext& context, UI::OldButton& button, std::int32_t x,
                                                       std::int32_t y, std::uint32_t label, std::int32_t state);
[[nodiscard]] Core::Expected<void, std::string> Back(GameHudContext& context, std::int32_t x, std::uint32_t title);
[[nodiscard]] Core::Expected<void, std::string> Close(GameHudContext& context, std::int32_t x);
[[nodiscard]] Core::Expected<void, std::string> Registration(GameHudContext& context,
                                                             UI::SiegeRegistrationWindow& window);
[[nodiscard]] Core::Expected<void, std::string> Hunt(GameHudContext& context, UI::HuntZoneWindow& window);
[[nodiscard]] Core::Expected<void, std::string> Senior(GameHudContext& context, UI::CastleSeniorWindow& window,
                                                       UI::CastleData& data, std::int32_t tab);
[[nodiscard]] Core::Expected<void, std::string> GuildList(GameHudContext& context, UI::SiegeRegistrationWindow& window,
                                                          bool registered);
} // namespace Client::Scenes::CastleDraw
