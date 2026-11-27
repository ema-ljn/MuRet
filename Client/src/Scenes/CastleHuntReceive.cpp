#include "Scenes/CastleSiegeReceive.h"
#include "Scenes/CastleWindows.h"

#include "Core/Expected.h"
#include "Scenes/CastleWindowSupport.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GuildPanels.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "UI/PanelDialog.h"
#include "UI/PanelRegistry.h"

#include <array>
#include <bit>
#include <cstdint>
#include <string>
#include <utility>

namespace Client::Scenes
{
Core::Expected<void, std::string> CastleUi::FailurePopup(const CastleReceiveContext& context, std::uint32_t address)
{
    if (auto cancelled = CastleUi::CancelPopup(context.world, context.send); !cancelled.HasValue())
        return cancelled;
    auto text = CastleUi::Text(context.texts, address);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    if (context.measure == nullptr)
        return Core::Unexpected(std::string("0x6600d0: la respuesta de HuntZone sin medidor"));
    const std::array<std::string, 1> lines{std::move(text).Value()};
    auto popup = PopUpThePanelDialog(context.world.Runtime(), {.lines = lines, .flags = UI::PanelDialogOk},
                                     *context.measure, context.screen, context.tick);
    if (!popup.HasValue())
        return Core::Unexpected(std::move(popup).Error());
    return {};
}

Core::Expected<void, std::string> ApplyHuntInformation(std::uint8_t mode, std::uint8_t allowed, std::uint32_t fee,
                                                       std::uint32_t max, std::uint32_t step,
                                                       const CastleReceiveContext& context)
{
    if (mode == 0)
        return CastleUi::FailurePopup(context, 0x76fc1ac);
    auto& window = context.world.Runtime().castleSiege.huntZone;
    window.mode = mode;
    window.allowed = allowed != 0 ? 1 : 0;
    window.fee = fee;
    window.editedFee = std::bit_cast<std::int32_t>(fee);
    window.max = std::bit_cast<std::int32_t>(max);
    window.step = std::bit_cast<std::int32_t>(step);
    GamePanels panels(context.world, context.random);
    LendTheCastlePanelServices(panels, context);
    auto opened = panels.Registry().Open(UI::SecondCastleSiegePanel);
    if (!opened.HasValue())
        return Core::Unexpected(std::move(opened).Error());
    return {};
}

Core::Expected<void, std::string> ApplyHuntEntryResult(std::uint8_t result, const CastleReceiveContext& context)
{
    return result == 0 ? CastleUi::FailurePopup(context, 0x76fc1ac) : Core::Expected<void, std::string>{};
}
} // namespace Client::Scenes
