#pragma once

#include "Core/Expected.h"
#include "CastleSiege/CastleWindowSupport.h"
#include "Ui/GameHudContext.h"
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
void Center(Ui::GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text,
            std::int32_t width = 190);
[[nodiscard]] Core::Expected<void, std::string> Text(Ui::GameHudContext& context, std::int32_t x, std::int32_t y,
                                                     std::uint32_t address, std::int32_t width = 190);
template <typename... Args>
[[nodiscard]] Core::Expected<void, std::string> FormattedWidth(Ui::GameHudContext& context, std::int32_t x,
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
[[nodiscard]] Core::Expected<void, std::string> Formatted(Ui::GameHudContext& context, std::int32_t x, std::int32_t y,
                                                          std::uint32_t address, Args... args)
{
    return FormattedWidth(context, x, y, 190, address, args...);
}
[[nodiscard]] Core::Expected<void, std::string> Button(Ui::GameHudContext& context, Ui::OldButton& button,
                                                       std::int32_t x, std::int32_t y, std::uint32_t label,
                                                       std::int32_t state);
[[nodiscard]] Core::Expected<void, std::string> Back(Ui::GameHudContext& context, std::int32_t x, std::uint32_t title);
[[nodiscard]] Core::Expected<void, std::string> Close(Ui::GameHudContext& context, std::int32_t x);
[[nodiscard]] Core::Expected<void, std::string> Registration(Ui::GameHudContext& context,
                                                             CastleSiege::SiegeRegistrationWindow& window);
[[nodiscard]] Core::Expected<void, std::string> Hunt(Ui::GameHudContext& context, CastleSiege::HuntZoneWindow& window);
[[nodiscard]] Core::Expected<void, std::string> Senior(Ui::GameHudContext& context,
                                                       CastleSiege::CastleSeniorWindow& window,
                                                       CastleSiege::CastleData& data, std::int32_t tab);
[[nodiscard]] Core::Expected<void, std::string>
GuildList(Ui::GameHudContext& context, CastleSiege::SiegeRegistrationWindow& window, bool registered);
} // namespace Client::CastleSiege::CastleDraw
