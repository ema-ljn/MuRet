#pragma once

#include "Core/Expected.h"
#include "Protocol/Outgoing.h"
#include "CastleSiege/CastleSiegeReceive.h"
#include "Ui/PanelDialog.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Tables
{
class TextTable;
}
namespace Client::Crt
{
class CrtRandom;
}
namespace Client::CastleSiege
{
struct CastleData;
}
namespace Client::Kernel
{
class WorldState;
struct WorldRuntime;
} // namespace Client::Kernel

namespace Client::Hud
{
struct GameUiContext;
} // namespace Client::Hud

namespace Client::CastleSiege::CastleUi
{
inline constexpr std::uint8_t Master = 0x80;
inline constexpr std::int32_t Disabled = 6;
inline constexpr std::int32_t WindowWidth = 190;
inline constexpr std::int32_t WindowHeight = 433;
inline constexpr std::int32_t CloseSize = 24;
inline constexpr std::int32_t CloseY = 395;
inline constexpr std::uint32_t TextBase = 0x76bd1dc;
[[nodiscard]] inline constexpr std::size_t TextId(std::uint32_t address)
{
    return (address - TextBase) / 300;
}

[[nodiscard]] Core::Expected<std::string, std::string> Text(const Tables::TextTable* texts, std::uint32_t address);
[[nodiscard]] Core::Expected<void, std::string> Send(const Hud::GameUiContext& context, Protocol::Outgoing packet);
[[nodiscard]] Core::Expected<std::uint32_t, std::string> Popup(const Hud::GameUiContext& context,
                                                               std::span<const std::string> lines,
                                                               Ui::PanelDialogOwner owner, std::uint32_t flags,
                                                               std::int32_t placed = 0);
[[nodiscard]] Core::Expected<std::uint32_t, std::string> PopupAt(const Hud::GameUiContext& context,
                                                                 std::initializer_list<std::uint32_t> addresses,
                                                                 Ui::PanelDialogOwner owner, std::uint32_t flags,
                                                                 std::int32_t placed = 0);
[[nodiscard]] Core::Expected<void, std::string> CancelPopup(Kernel::WorldState& world, const CastleSender& send);
[[nodiscard]] Core::Expected<void, std::string> FailurePopup(const CastleReceiveContext& context,
                                                             std::uint32_t address);
[[nodiscard]] CastleSender SenderOf(const Hud::GameUiContext& context);
void ConsumeClick(Kernel::WorldRuntime& runtime);
[[nodiscard]] std::uint32_t CountMarks(Kernel::WorldState& world, Crt::CrtRandom& random, std::uint8_t& depth);
[[nodiscard]] std::int32_t FirstMark(Kernel::WorldState& world, Crt::CrtRandom& random, std::uint8_t& depth);
[[nodiscard]] bool HeroCanRegister(Kernel::WorldState& world, Crt::CrtRandom& random);
[[nodiscard]] Core::Expected<std::pair<std::string_view, std::string_view>, std::string>
GuildNames(const Kernel::WorldState& world);
[[nodiscard]] Core::Expected<bool, std::string> HeroCanEditTaxes(const Kernel::WorldState& world);
[[nodiscard]] std::int32_t DefenseLevel(const CastleSiege::CastleData& data, std::uint32_t at);
[[nodiscard]] std::int32_t RecoveryLevel(const CastleSiege::CastleData& data, std::uint32_t at);
[[nodiscard]] std::int32_t NpcCost(const CastleSiege::CastleData& data, std::uint32_t at);
} // namespace Client::CastleSiege::CastleUi
