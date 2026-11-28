#pragma once

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "CastleSiege/CastleSiegeReceive.h"
#include "Ui/PanelDialog.h"
#include "Ui/PanelRegistry.h"

#include <cstdint>
#include <string>

namespace Client::Tables
{
class TextTable;
}
namespace Client::Render::Text
{
class ITextMeasure;
}
namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Hud
{
class GamePanels;
struct GameUiContext;
struct GameHudContext;
} // namespace Client::Hud

// The three castle windows: 0x64d350, 0x6691c0 and 0x648a30, called by 0x657930, and their draws from 0x6579e0.
// The open flags and Senior's tab stay in DelegatedPanels, without parallel copies.
namespace Client::CastleSiege
{

struct CastleWindowServices
{
    const Render::Text::ITextMeasure* measure = nullptr;
    const Tables::TextTable* texts = nullptr;
    Core::PixelSize screen{640, 480};
    std::uint32_t now = 0;
};

[[nodiscard]] bool CastleSeniorBusy(const Kernel::WorldState& world);
[[nodiscard]] bool HeroCanCraftAtTheCastle(const Kernel::WorldState& world);
void LendTheCastleLifecycleTo(Ui::PanelContext& panels, Kernel::WorldState& world, Crt::CrtRandom& random,
                              const CastleSender& send, CastleWindowServices services = {});
void LendTheCastleWindowStepsTo(Ui::PanelContext& panels, const Hud::GameUiContext& context);
// The same lending for callers that get no Castle packet and have no log of their own.
void LendTheCastlePanelServices(Hud::GamePanels& panels, Kernel::WorldState& world, Crt::CrtRandom& random,
                                const CastleSender& send, const Scenes::WorldEntryResources* entryResources,
                                CastleWindowServices services = {});
void LendTheCastlePanelServices(Hud::GamePanels& panels, const CastleReceiveContext& context);
// For the closes of entry, teleport, quests, trade and shops: it also keeps their specific transports.
void LendTheCastlePanelServices(Hud::GamePanels& panels, const Scenes::WorldEntryResources& resources,
                                const CastleSender& send = {});
[[nodiscard]] Core::Expected<void, std::string> StepTheCastleWindow(const Hud::GameUiContext& context, Ui::PanelId id);
[[nodiscard]] Core::Expected<void, std::string> DrawTheCastleWindows(Hud::GameHudContext& context);
[[nodiscard]] Core::Expected<bool, std::string> AnswerTheCastleWindowDialog(Kernel::WorldState& world,
                                                                            const CastleSender& send,
                                                                            Ui::PanelDialogOwner owner,
                                                                            std::uint32_t reason);

// 0x4e07f0 and 0x4e0860: B9 03/05 are also the hunting zone guard. The H13 reader lends the fields it read.
[[nodiscard]] Core::Expected<void, std::string> ApplyHuntInformation(std::uint8_t mode, std::uint8_t allowed,
                                                                     std::uint32_t fee, std::uint32_t max,
                                                                     std::uint32_t step,
                                                                     const CastleReceiveContext& context);
[[nodiscard]] Core::Expected<void, std::string> ApplyHuntEntryResult(std::uint8_t result,
                                                                     const CastleReceiveContext& context);
} // namespace Client::CastleSiege
