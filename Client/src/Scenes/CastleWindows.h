#pragma once

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Scenes/CastleSiegeReceive.h"
#include "UI/PanelDialog.h"
#include "UI/PanelRegistry.h"

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

// Las tres ventanas del castillo: 0x64d350, 0x6691c0 y 0x648a30, llamadas por 0x657930, y sus dibujos desde
// 0x6579e0. Los flags abiertos y la pestaña de Senior siguen en DelegatedPanels, sin copias paralelas.
namespace Client::Scenes
{
class GamePanels;
struct GameUiContext;
struct GameHudContext;

struct CastleWindowServices
{
    const Render::Text::ITextMeasure* measure = nullptr;
    const Tables::TextTable* texts = nullptr;
    Core::PixelSize screen{640, 480};
    std::uint32_t now = 0;
};

[[nodiscard]] bool CastleSeniorBusy(const Kernel::WorldState& world);
[[nodiscard]] bool HeroCanCraftAtTheCastle(const Kernel::WorldState& world);
void LendTheCastleLifecycleTo(UI::PanelContext& panels, Kernel::WorldState& world, Crt::CrtRandom& random,
                              const CastleSender& send, CastleWindowServices services = {});
void LendTheCastleWindowStepsTo(UI::PanelContext& panels, const GameUiContext& context);
// El mismo préstamo para callers que no reciben un paquete Castle ni tienen un log propio.
void LendTheCastlePanelServices(GamePanels& panels, Kernel::WorldState& world, Crt::CrtRandom& random,
                                const CastleSender& send, const WorldEntryResources* entryResources,
                                CastleWindowServices services = {});
void LendTheCastlePanelServices(GamePanels& panels, const CastleReceiveContext& context);
// Para los cierres de entrada, teleport, misiones, comercio y tiendas: conserva también sus transportes específicos.
void LendTheCastlePanelServices(GamePanels& panels, const WorldEntryResources& resources,
                                const CastleSender& send = {});
[[nodiscard]] Core::Expected<void, std::string> StepTheCastleWindow(const GameUiContext& context, UI::PanelId id);
[[nodiscard]] Core::Expected<void, std::string> DrawTheCastleWindows(GameHudContext& context);
[[nodiscard]] Core::Expected<bool, std::string> AnswerTheCastleWindowDialog(Kernel::WorldState& world,
                                                                            const CastleSender& send,
                                                                            UI::PanelDialogOwner owner,
                                                                            std::uint32_t reason);

// 0x4e07f0 y 0x4e0860: B9 03/05 son también el guardia de la zona de caza. El lector H13 presta los campos leídos.
[[nodiscard]] Core::Expected<void, std::string> ApplyHuntInformation(std::uint8_t mode, std::uint8_t allowed,
                                                                     std::uint32_t fee, std::uint32_t max,
                                                                     std::uint32_t step,
                                                                     const CastleReceiveContext& context);
[[nodiscard]] Core::Expected<void, std::string> ApplyHuntEntryResult(std::uint8_t result,
                                                                     const CastleReceiveContext& context);
} // namespace Client::Scenes
