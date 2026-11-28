#include "CastleSiege/CastleSiegeReceive.h"
#include "CastleSiege/CastleWindows.h"

#include "Core/Expected.h"
#include "CastleSiege/CastleWindowSupport.h"
#include "Hud/GamePanels.h"
#include "Guild/GuildPanels.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/PanelDialog.h"
#include "Ui/PanelRegistry.h"
#include "CastleSiege/CastleSiegeState.h"

#include <array>
#include <bit>
#include <cstdint>
#include <string>
#include <utility>

namespace Client::CastleSiege
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
    auto popup = Guild::PopUpThePanelDialog(context.world.Runtime(), {.lines = lines, .flags = Ui::PanelDialogOk},
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
    Hud::GamePanels panels(context.world, context.random);
    LendTheCastlePanelServices(panels, context);
    auto opened = panels.Registry().Open(Ui::SecondCastleSiegePanel);
    if (!opened.HasValue())
        return Core::Unexpected(std::move(opened).Error());
    return {};
}

Core::Expected<void, std::string> ApplyHuntEntryResult(std::uint8_t result, const CastleReceiveContext& context)
{
    return result == 0 ? CastleUi::FailurePopup(context, 0x76fc1ac) : Core::Expected<void, std::string>{};
}
} // namespace Client::CastleSiege
