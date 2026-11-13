#pragma once

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Protocol/CastleSiege.h"
#include "Protocol/Outgoing.h"
#include "Scenes/ChatReceive.h"
#include "UI/PanelRegistry.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// CL9 hito 12 (06 § Castle Siege): los paquetes del castillo que llegan (B2, B3, B4, B5, B6, B8 y el B9 03) y las
// ventanas de sus tres objetos: el registro para el asedio (0x80000, [0x7d536f8]), el Castle Senior (0x100000,
// [0x7d536fc]) y el guardia de la zona de caza (0x200000, [0x7d53700]). Lo que dibujan (0x64d860, 0x6694c0 y
// 0x64a060, desde 0x6579e0) es del hito 13: el dibujo es el que ubica las ventanas y sus botones, y los pasos leen
// lo que dejó el último (UI::CastleSiegeState).
namespace Client::Data::Text
{
class TextTable;
}

namespace Client::Render::Models
{
class CrtRandom;
}

namespace Client::Render::Text
{
class ITextMeasure;
}

namespace Client::Scenes
{
class WorldState;

// Las funciones que el diálogo de los paneles (0x6603c0) llama con la respuesta: 1, 2, 4, 8 o 0x10 según el botón.
enum class CastleCallback : std::uint8_t
{
    GiveUp,         // 0x64ccd0
    GateBuy,        // 0x6623e0
    GateLife,       // 0x662d50
    GateDefense,    // 0x663440
    StatueBuy,      // 0x664370
    StatueLife,     // 0x664ce0
    StatueDefense,  // 0x6653d0
    StatueRecovery, // 0x665ab0
    Taxes,          // 0x666b30
    MoneyOut,       // 0x667a40
};

// 0x6600d0(renglones, cuántos, el paso entre renglones, las banderas, la función, el último) sobre [0x7d53704].
struct PanelDialogRequest
{
    std::vector<std::string> lines;
    std::int32_t lineCount = 0;
    std::int32_t lineStride = 0x32;
    std::int32_t flags = 0;
    std::optional<CastleCallback> callback{};
    std::int32_t last = 0;
};

// El diálogo de los paneles ([0x7d53704], 0x65feb0) es del hito 8 (CL9.4, P-C90): lo que las ventanas le piden. Sin
// un gancho, lo que lo usa corta.
struct PanelDialogHooks
{
    // 0x6600d0: lo abre y devuelve su marca (+0x10, el GetTickCount de cuando abrió).
    std::function<Core::Expected<std::int32_t, std::string>(const PanelDialogRequest&)> open{};
    // 0x660340: la marca, 0 cerrado. Sin gancho, el diálogo cerrado (DelegatedPanels::panelDialog en 0) da 0.
    std::function<std::int32_t()> handle{};
    // 0x6603c0: lo cierra (cerrado no hace nada) y llama a la función con la respuesta.
    std::function<Core::Expected<void, std::string>()> close{};
    // 0x660250(a, b, c): el campo de texto del diálogo (+0xe28..+0xe30), el de sacar la plata.
    std::function<Core::Expected<void, std::string>(std::int32_t, std::int32_t, std::int32_t)> setField{};
    // 0x6602d0: el texto del campo (+0xa28).
    std::function<Core::Expected<std::string, std::string>()> fieldText{};
};

// La tabla de los gremios (0x787aa4c, 0x58 bytes por gremio) es del hito 8: 0x6691c0 compara su texto de +0 con el de
// +9 y con "" para dejar ver los impuestos.
struct GuildTableEntry
{
    std::string atZero;
    std::string atNine;
};

// Lo que las ventanas y los paquetes del castillo usan de afuera.
struct CastleServices
{
    // send() por el socket del juego, en claro (los B2/B3 ya con el XOR32).
    std::function<void(Protocol::Outgoing)> send{};
    // Text.bmd y lo que mide 0x555190.
    const Data::Text::TextTable* texts = nullptr;
    const Render::Text::ITextMeasure* measure = nullptr;
    App::PixelSize screen{640, 480};
    PanelDialogHooks dialog{};
    std::function<Core::Expected<GuildTableEntry, std::string>(std::int16_t)> guildEntry{};
    // 0x7d3682c y 0x7d36828: el mouse del paso.
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
};

// 0x6689d0: el héroe con un gremio (+0x1f2 ≥ 0) y maestro (+0x368 en 0x80).
[[nodiscard]] bool HeroSiegeMark(const WorldState& world);

// 0x64ae10, 0x661310 y 0x6487c0: lo que hace el objeto al abrirse, después de CloseByMask.
[[nodiscard]] Core::Expected<void, std::string> OpenTheCastleObject(WorldState& world, UI::PanelId id,
                                                                    const CastleServices& services);
// 0x64b200, 0x661bc0 y 0x6487d0: lo que hace el objeto cuando CloseByMask lo cierra.
[[nodiscard]] Core::Expected<void, std::string> CloseTheCastleObject(WorldState& world, UI::PanelRegistry& panels,
                                                                     UI::PanelId id, const CastleServices& services);
// 0x64d350, 0x6691c0 y 0x648a30: el paso de cada uno desde 0x657930.
[[nodiscard]] Core::Expected<void, std::string> StepTheCastleObject(WorldState& world,
                                                                    Render::Models::CrtRandom& random,
                                                                    UI::PanelRegistry& panels, UI::PanelId id,
                                                                    const CastleServices& services);
// La función del diálogo con la respuesta (0x6603c0 la llama).
[[nodiscard]] Core::Expected<void, std::string> AnswerTheCastleDialog(WorldState& world, CastleCallback callback,
                                                                      std::int32_t answer,
                                                                      const CastleServices& services);

// Los paquetes que llegan (0x4e2b10): lo que lee y escribe el castillo, con la sesión para lo que contestan.
struct CastleReceiveContext
{
    ChatReceiveContext chat;
    CastleServices services{};
};
// B2 (0x4e2b10 con la tabla 0x4e6140), B3 (0x4dfc60), B4 (0x4dfce0), B5 (0x4dfd80), B6 (0x4e0ed0), B8 (0x4e0700) y
// B9 03 (0x4e07f0).
[[nodiscard]] Core::Expected<void, std::string> ApplyCastlePacket(const Protocol::CastleSiege::CastlePacket& packet,
                                                                  const CastleReceiveContext& context);

// La lista de 0x46f320 y 0x4706f0: lo que les hacen los paquetes (0x46f7e0 / 0x470bb0, la vtable + 0x2c y 0x46f940).
// Las nuevas van adelante, la lista no pasa de 150 (0x4859a0 saca las de atrás) y queda mirando el final.
void AddTheSiegeGuild(UI::SiegeList<UI::SiegeGuildRow>& list, const Protocol::CastleSiege::GuildListEntry& entry);
void AddTheRegisteredGuild(UI::SiegeList<UI::RegisteredGuildRow>& list,
                           const Protocol::CastleSiege::RegisteredGuildEntry& entry);
void ClearTheSiegeGuilds(UI::SiegeList<UI::SiegeGuildRow>& list);
void ClearTheRegisteredGuilds(UI::SiegeList<UI::RegisteredGuildRow>& list);
// 0x46f940: std::sort de Visual C++ 6 sobre la deque con 0x46f920 (las marcas de menor a mayor, con signo).
void SortTheSiegeGuilds(UI::SiegeList<UI::SiegeGuildRow>& list);
} // namespace Client::Scenes
