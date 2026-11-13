#include "Scenes/EventPackets.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Text/TextTable.h"
#include "Protocol/Events.h"
#include "Render/Models/Animation.h"
#include "Scenes/ActorState.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/EventGlobals.h"
#include "Scenes/EventMapObject.h"
#include "Scenes/FaunaObject.h"
#include "Scenes/FaunaState.h"
#include "Scenes/GameMessages.h"
#include "Scenes/GamePanels.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/Dialogs.h"
#include "UI/GameDialogs.h"
#include "UI/GameText.h"
#include "UI/MessageArea.h"
#include "UI/OriginalFormat.h"
#include "UI/PanelRegistry.h"
#include "World/WorldTerrain.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace Client::Scenes
{
namespace
{
namespace Events = Protocol::Events;
using Applied = Core::Expected<void, std::string>;

// Los nombres de los eventos que los formatos toman como argumento (un puntero a su registro de Text.bmd).
constexpr std::size_t BloodCastleName = 1146; // 0x77110d4
constexpr std::size_t ChaosCastleName = 1147; // 0x7711200

// Los avisos que comparten el 9A (0x4da780) y el AF 01 (0x4daa90).
constexpr std::size_t WrongCloakText = 854;      // 0x76fbaa4
constexpr std::size_t EntryOverText = 852;       // 0x76fb84c: "...en% s ya ha terminado"
constexpr std::size_t CapacityText = 853;        // 0x76fb978: "...permitido y% d"
constexpr std::size_t TimesPerDayText = 867;     // 0x76fc9e0: "...más que% d por día."
constexpr std::size_t TooStrongText = 686;       // 0x76ef5c4
constexpr std::size_t TooWeakText = 687;         // 0x76ef6f0
constexpr std::size_t ChaosCastleNoteText = 423; // 0x76dc190, el AF 01 con 7
// 0x4da803 y 0x4da86b: el número que pasan al "% d" de 853, después del nombre.
constexpr std::int32_t BloodCastleCapacity = 10;
constexpr std::int32_t ChaosCastleCapacity = 100;
// 0x4da828 y 0x4dab38: el de 867.
constexpr std::int32_t TimesPerDay = 6;
// Los buffers de la pila de 0x4da780 y 0x4daa90: 0x64 bytes en [EBP − 0x64] y 0x80 en [EBP − 0x80] (el de 867).
constexpr std::size_t SmallStackBuffer = 0x64;
constexpr std::size_t LargeStackBuffer = 0x80;
// 0x4da8c0, 0x4e4570, 0x4da540 y 0x40bab0: 0x100 bytes.
constexpr std::size_t PageStackBuffer = 0x100;

// 0x4e4894: los avisos del 90 con pkt[3] de 1 a 5 (la tabla 0x4e60ec).
constexpr std::array<std::size_t, 5> DevilSquareEntryTexts{677, 678, 686, 687, 679};

// 0x4da8c0: los avisos del 91.
constexpr std::uint8_t DevilSquareOpenTime = 1;
constexpr std::uint8_t BloodCastleOpenTime = 2;
constexpr std::uint8_t ChaosCastleOpenTime = 4;
constexpr std::size_t DevilSquareOpenText = 643;    // 0x76ec360
constexpr std::size_t DevilSquareMinutesText = 644; // 0x76ec48c
constexpr std::size_t CanEnterNowText = 850;        // 0x76fb5f4: "...en% s ahora"
constexpr std::size_t CanEnterInText = 851;         // 0x76fb720: "...en% d minutos"
constexpr std::size_t ChaosCastleOpenText = 1156;   // 0x7711c8c, el formato del segundo renglón
constexpr std::size_t ChaosCastleHoursText = 1164;  // 0x77125ec: "Cuando %d"
// 0x4da9bc: el tercer argumento del formato de 1156.
constexpr std::int32_t ChaosCastleOpenArgument = 100;
// 0x4da9f0..0x4daa0a: la división por 60 de la palabra pkt[4]:pkt[6].
constexpr std::int32_t MinutesPerHour = 60;

// 0x4e4a86: los avisos del 9D con pkt[3] de 0 a 4 (0x76fe024 + 300 · n) y con 5.
constexpr std::size_t FirstSerialText = 886;
constexpr std::uint8_t LastIndexedSerialResult = 4;
constexpr std::uint8_t SerialAlreadyRegistered = 5;
constexpr std::size_t SerialAlreadyRegisteredText = 899; // 0x76fef60

// 0x4e4ad4: los sonidos del 9E con la palabra de pkt[4] en 0, 1 y 2.
constexpr std::array<std::int32_t, 3> EventSounds{0x5b, 0x5c, 0x5d};

// 0x4e4a40: los diálogos del 99 con pkt[3] en 0 y 1.
constexpr std::array<std::int32_t, 2> EventDialogs{0x8f, 0x90};

// 0x4e3621..0x4e365e: el 0B con pkt[4] en 1 (la tormenta en 1 o 0) o en 3 (en 3 o 0).
constexpr std::uint8_t StormEvent = 1;
constexpr std::uint8_t StrongStormEvent = 3;
constexpr std::int32_t Storm = 1;
constexpr std::int32_t StrongStorm = 3;

// 0x4dab90: el tipo de pkt[3].
constexpr std::uint8_t AreaAttributes = 0;
constexpr std::uint8_t CellAttributes = 1;

// Los mapas de los eventos (0x6c9dd0).
constexpr std::int32_t FirstBloodCastleMap = 11;
constexpr std::int32_t LastBloodCastleMap = 17;
constexpr std::int32_t FirstChaosCastleMap = 18;
constexpr std::int32_t LastChaosCastleMap = 23;

// 0x4dac0b y 0x40c221: el derrumbe de Blood Castle (el atributo 8 del 46 y el 9B con 3).
constexpr std::uint8_t BloodCastleGateAttribute = 8;
constexpr std::int32_t BloodCastleCollapseType = 0x24;
constexpr std::int32_t BloodCastleCollapseCounter = 0x14;
// 0x404f2e: el de Chaos Castle.
constexpr std::int32_t ChaosCastleCollapseType = 1;
constexpr std::int32_t ChaosCastleCollapseCounter = 0x28;
constexpr std::int32_t CollapseDelay = 1;

// 0x40bd20, 0x40c0c0 y 0x404da0: el primer botón de la caja de los resultados y los diálogos.
constexpr UI::DialogButton DevilSquareResultButton{.state = 1, .x = 0x47, .y = 0x104, .width = 0x46, .height = 0x15};
constexpr UI::DialogButton CastleResultButton{.state = 1, .x = 0x47, .y = 0x82, .width = 0x46, .height = 0x15};
constexpr std::int32_t DevilSquareResultDialog = 0x8b;
constexpr std::int32_t BloodCastleResultDialog = 0x97;
constexpr std::int32_t ChaosCastleResultDialog = 0x9b;
// 0x40bd2a: Devil Square copia pkt[4] registros de 24 bytes si pkt[4] < 200.
constexpr std::int32_t DevilSquareResultLimit = 200;
constexpr std::size_t ResultRecordBytes = 24;
// 0x40c0cb y 0x404dab: Blood Castle con pkt[4] en 0xff y Chaos Castle con 0xfe copian uno.
constexpr std::uint8_t BloodCastleResultCount = 0xff;
constexpr std::uint8_t ChaosCastleResultCount = 0xfe;

// 0x40ba80: el tiempo que dejan el 9B de Blood Castle y el de Chaos Castle en +0x14.
constexpr std::int32_t PlayTime = 0x384;
// 0x5111a0(0xc0) y 0x41ed10(0x8b, 0, 1): la entrada a Blood Castle.
constexpr std::uint8_t GateAction = 0xc0;
constexpr float GateAngle = 180.0f;
constexpr std::int32_t BloodCastleMusic = 0x8b;
// 0x40c1ab..0x40c1c3: sin dueño (0xffff) o sin arma (0 o 0xff), nada.
constexpr std::uint16_t NoWeaponOwner = 0xffff;
constexpr std::uint8_t NoWeapon = 0xff;
// 0x40c1c9: la clave sin el bit alto.
constexpr std::uint16_t KeyMask = 0x7fff;
// 0x511150 y 0x5111a0 recorren los 400 primeros; sin la clave, 0x511150 da el 400.
constexpr std::size_t ActorsSearched = 400;

// 0x404e20: los casos de Chaos Castle (pkt[3] de 5 a 10).
constexpr std::uint8_t ChaosCastleStart = 5;
constexpr std::uint8_t ChaosCastlePlay = 6;
constexpr std::uint8_t ChaosCastleEnd = 7;
constexpr std::uint8_t ChaosCastleFirstCollapse = 8;
constexpr std::uint8_t ChaosCastleSecondCollapse = 9;
constexpr std::uint8_t ChaosCastleThirdCollapse = 10;
constexpr std::int32_t ChaosCastleMusic = 0x99;
constexpr std::int32_t ChaosCastleWaitMusic = 0x9e;
constexpr std::int32_t ChaosCastleEndSound = 0x9f;
constexpr std::int32_t ChaosCastleCollapseSound = 0x61;
// 0x404e5a..0x404e75: el campo del chat abierto se cierra, y con los EDIT (0x6c7b8c en 1, como en MuRet) los
// oculta 0x469450(3).
constexpr std::int32_t HideTheChatFields = 3;
// 0x404e53: el área en 0xff; los casos 8, 9 y 10 esperan 0, 3 y 6 y dejan 1, 4 y 7 (0x405e00 la mueve).
constexpr std::uint8_t NoChaosCastleArea = 0xff;
constexpr std::uint8_t ChaosCastleTrapArmed = 1;

// Un rectángulo de 0x5f8750 con el atributo 8 de los derrumbes de Chaos Castle.
struct CollapseArea
{
    std::int32_t x;
    std::int32_t y;
    std::int32_t width;
    std::int32_t height;
};
constexpr std::uint8_t ChaosCastleCollapseAttribute = 8;
// 0x404f41..0x404f83, 0x404fae..0x404fee y 0x405014..0x405054.
constexpr std::array<std::array<CollapseArea, 4>, 3> ChaosCastleCollapses{{
    {{{0x17, 0x4b, 0x16, 2}, {0x2b, 0x4d, 2, 0x20}, {0x17, 0x6b, 0x14, 2}, {0x17, 0x4d, 2, 0x1e}}},
    {{{0x19, 0x4d, 0x12, 2}, {0x29, 0x4f, 2, 0x1c}, {0x19, 0x69, 0x10, 2}, {0x19, 0x4f, 2, 0x1a}}},
    {{{0x1b, 0x4f, 0xe, 2}, {0x27, 0x51, 2, 0x18}, {0x1b, 0x67, 0xc, 2}, {0x1b, 0x51, 2, 0x16}}},
}};
// Lo que esperan en 0x6c634c los tres derrumbes, y lo que dejan.
constexpr std::array<std::uint8_t, 3> ChaosCastleCollapseAreas{0, 3, 6};

// 0x4dd8a0: el modo de 0x78abb00 (pkt[3] + 1) con los números del Muto y con el número de serie.
constexpr std::int32_t MutoWindow = 1;
constexpr std::int32_t SerialWindow = 3;
constexpr std::uint16_t NoMutoNumber = 0xffff;
constexpr std::uint16_t NoChipCount = 0xffff;

// 0x4e4547 y 0x4da540: los resultados del BC 00 y del BC 01.
constexpr std::size_t JewelMixText = 1801;       // 0x7741068
constexpr std::size_t JewelUnmixText = 1800;     // 0x7740f3c
constexpr std::size_t JewelToText = 1816;        // 0x77421fc
constexpr std::size_t JewelTimesText = 868;      // 0x76fcb0c
constexpr std::size_t JewelMissingText = 1817;   // 0x7742328
constexpr std::size_t JewelNoZenText = 1811;     // 0x7741c20
constexpr std::size_t JewelWrongItemText = 1812; // 0x7741d4c
constexpr std::size_t JewelNoSpaceText = 1815;   // 0x77420d0
// 0x6c8490 y 0x6c8498: "%s%s %s".
constexpr std::string_view JewelFormat = "%s%s %s";
constexpr std::int32_t JewelDialog = 0xa6;

// 0x42fe80: las etapas de Crywolf que tocan la interfaz.
constexpr std::uint8_t CrywolfWarningStage = 4;
constexpr std::uint8_t CrywolfFightStage = 5;
constexpr std::uint8_t CrywolfEndStage = 6;
constexpr std::uint8_t CrywolfPeaceState = 0;
constexpr std::uint8_t CrywolfLostState = 2;
constexpr std::int32_t CrywolfPeaceCounter = 0xb;
constexpr std::int32_t CrywolfFightTime = 0x186;
// 0x42ff3f..0x42ff75: los estados con su luz; otro deja el buffer sin escribir.
constexpr std::uint8_t LastLitCrywolfState = 2;

// 0x4d9f59: el tanteador sin partido (pkt[0xc] en 0xff).
constexpr std::uint8_t NoSoccerScore = 0xff;

// 0x4da070: los comandos del F3 40.
constexpr std::uint8_t CommandEffect = 0;
constexpr std::uint8_t CommandNotice = 1;
constexpr std::uint8_t CommandSound = 2;
constexpr std::uint8_t CommandMessage = 3;
constexpr std::uint8_t CommandDialog = 5;
constexpr std::uint8_t CommandFixedNotice = 6;
constexpr std::uint8_t CommandCastleNotice = 7;
constexpr std::uint8_t CommandGuildNotice = 0xd;
constexpr std::uint8_t FirstCommandText = 0x2f;
constexpr std::uint8_t LastCommandText = 0x31;
constexpr std::int32_t CommandSoundId = 0x5a;
constexpr std::uint8_t HighNoticeFrom = 0x14;
constexpr std::size_t LowNoticeBase = 0x28a;    // 650
constexpr std::size_t HighNoticeBase = 0x32a;   // 810
constexpr std::size_t MessageBase = 710;        // 0x76f11e4
constexpr std::size_t FixedNoticeText = 449;    // 0x76de008
constexpr std::size_t CastleNoticeBase = 0x7e4; // 2020, 0x775110c
constexpr std::int32_t CastleNoticeDialog = 0xaa;
constexpr std::size_t GuildNoticeText = 1826; // 0x7742db4
constexpr std::size_t CommandTextBase = 1776; // 0x773f31c

// 0x40bab0: la cuenta de 30 s.
constexpr std::int32_t FirstCountdown = 1;
constexpr std::int32_t LastCountdown = 14;
constexpr std::uint32_t CountdownMs = 30000;
constexpr std::uint32_t MsPerSecond = 1000;
constexpr std::int32_t CountdownSeconds = 30;
constexpr std::int32_t FirstBloodCastleCountdown = 4;
constexpr std::int32_t LastBloodCastleCountdown = 7;
constexpr std::int32_t FirstChaosCastleCountdown = 11;
constexpr std::int32_t LastChaosCastleCountdown = 14;
// 0x40bb31: 0x76f92cc + 300 · n, el texto 820 + n de Blood Castle.
constexpr std::size_t BloodCastleCountdownBase = 820;
// 0x40bb6b..0x40bb77: el 813 + n de Chaos Castle, con el 825 cambiado por el 828.
constexpr std::size_t ChaosCastleCountdownBase = 0x32d;
constexpr std::size_t ChaosCastleSkippedText = 0x339;
constexpr std::size_t ChaosCastleReplacementText = 0x33c;
// 0x40bba5: 0x76ebeb0 + 300 · n, el 639 + n de los demás.
constexpr std::size_t OtherCountdownBase = 639;

class Applier
{
public:
    explicit Applier(const EventPacketContext& context)
        : m_context(context), m_world(context.world), m_runtime(context.world.Runtime())
    {
    }

    Core::Expected<EventPacketResult, std::string> Apply(const Events::EventPacket& packet)
    {
        auto applied = std::visit([this](const auto& value) { return On(value); }, packet);
        if (!applied.HasValue())
            return Core::Unexpected(std::move(applied).Error());
        return std::move(m_result);
    }

private:
    // ----- Lo que comparten los handlers.

    [[nodiscard]] Core::Expected<std::string_view, std::string> Text(std::size_t index, std::string_view where) const
    {
        const std::optional<std::string_view> text = UI::TextAt(m_context.texts, index);
        if (!text)
            return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
        return *text;
    }

    [[nodiscard]] Core::Expected<UI::FormatArgument, std::string> TextArgument(std::size_t index,
                                                                               std::string_view where) const
    {
        auto text = Text(index, where);
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        return UI::FormatArgument{UI::TextArgument{.index = index, .text = text.Value()}};
    }

    // `function` con el formato `format` de Text.bmd en un buffer de `bufferBytes`: lo que no entra con su '\0'
    // pisaría lo que sigue y corta.
    [[nodiscard]] Core::Expected<std::string, std::string> Format(UI::FormatFunction function, std::string_view format,
                                                                  std::span<const UI::FormatArgument> arguments,
                                                                  std::size_t bufferBytes, std::string_view where) const
    {
        auto formatted = UI::FormatOriginal(function, format, arguments, where);
        if (!formatted.HasValue())
            return Core::Unexpected(std::move(formatted).Error());
        if (formatted.Value().size() >= bufferBytes)
            return Core::Unexpected(
                std::format("{}: \"{}\" formateado ocupa {} bytes con su '\\0' y el buffer tiene {}", where, format,
                            formatted.Value().size() + 1, bufferBytes));
        return formatted;
    }

    [[nodiscard]] Core::Expected<std::string, std::string> FormatText(UI::FormatFunction function, std::size_t format,
                                                                      std::span<const UI::FormatArgument> arguments,
                                                                      std::size_t bufferBytes,
                                                                      std::string_view where) const
    {
        auto text = Text(format, where);
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        return Format(function, text.Value(), arguments, bufferBytes, where);
    }

    // 0x632e30(texto).
    void MessageBox(std::string_view text)
    {
        UI::OpenMessageBox(m_runtime.dialogBox, m_runtime.dialogs, text);
    }

    // 0x632e30 con un registro de Text.bmd.
    Applied MessageBoxOf(std::size_t index, std::string_view where)
    {
        auto text = Text(index, where);
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        MessageBox(text.Value());
        return {};
    }

    // sprintf o wsprintfA de un formato de Text.bmd y 0x632e30 con lo que salió.
    Applied FormattedMessageBox(UI::FormatFunction function, std::size_t format,
                                std::span<const UI::FormatArgument> arguments, std::size_t bufferBytes,
                                std::string_view where)
    {
        auto text = FormatText(function, format, arguments, bufferBytes, where);
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        MessageBox(text.Value());
        return {};
    }

    // 0x555190(nombre, texto, 1) con uno de los nombres vacíos de la .bss (0x5767a54..0x5767a6c).
    Applied Message(std::string_view text, std::string_view where)
    {
        if (m_context.measure == nullptr)
            return Core::Unexpected(std::format("{}: sin el medidor de los mensajes (0x555190)", where));
        return AddGameMessage(m_runtime, *m_context.measure, m_context.screen, std::string_view{}, text,
                              UI::SystemMessage);
    }

    // 0x657a90 sobre [0x576352c].
    Applied CloseAllPanels()
    {
        GamePanels panels(m_world, m_context.random);
        panels.LendTheWindowClose(m_context.sendWindowClose);
        return panels.Registry().CloseAll();
    }

    // 0x41ed10: MuRet todavía no tiene audio (CL10); queda el pedido.
    void Play(std::int32_t sound)
    {
        m_result.sounds.push_back(sound);
        m_runtime.lastSoundId = sound;
    }

    // 0x41ee00: el pedido de apagar (CL10).
    void Stop(std::int32_t sound)
    {
        m_result.stoppedSounds.push_back(sound);
    }

    // 0x5ff930(mapa, tipo, cuenta, demora): el FILD de la demora la guarda como float.
    void RequestTheCollapse(std::int32_t map, std::int32_t type, std::int32_t counter, std::int32_t delay)
    {
        m_runtime.events.objectCollapse =
            ObjectCollapse{.map = map, .type = type, .counter = counter, .delay = static_cast<float>(delay)};
    }

    // 0x5f8710 (OR) y 0x5f8730 (AND con el complemento) sobre el WORD (y << 8) + x de 0x7d0d348.
    Applied MarkTheCell(std::int32_t x, std::int32_t y, std::uint8_t attribute, bool add, std::string_view where)
    {
        auto* attributes = m_world.Map().terrain.attributes.get();
        if (attributes == nullptr)
            return Core::Unexpected(std::format("{}: el atributo de ({}, {}) va al .att del mapa (0x7d0d348), que no "
                                                "está cargado",
                                                where, x, y));
        const std::int64_t index = (std::int64_t{y} << 8) + x;
        if (index < 0 || index >= static_cast<std::int64_t>(Data::Terrain::TerrainCellCount))
            return Core::Unexpected(
                std::format("{}: ({}, {}) cae fuera de los 0x10000 WORD de 0x7d0d348", where, x, y));
        std::uint16_t& cell = attributes->cells[static_cast<std::size_t>(index)];
        if (add)
            cell = static_cast<std::uint16_t>(cell | attribute);
        else
            cell = static_cast<std::uint16_t>(cell & ~std::uint16_t{attribute});
        return {};
    }

    // 0x5f8750(x, y, ancho, alto, atributo, agregar): fila por fila, de y a y + alto − 1, y en cada una de x a x +
    // ancho − 1; con alto o ancho en 0 o menos, nada.
    Applied MarkTheArea(const CollapseArea& area, std::uint8_t attribute, bool add, std::string_view where)
    {
        for (std::int32_t row = 0; row < area.height; ++row)
        {
            for (std::int32_t column = 0; column < area.width; ++column)
            {
                if (auto marked = MarkTheCell(area.x + column, area.y + row, attribute, add, where); !marked.HasValue())
                    return marked;
            }
        }
        return {};
    }

    // 0x5111a0(acción): cada uno de los 400 primeros vivo que es un jugador (0x20a) con los ángulos en (0, 0, 180) y
    // la acción por 0x4ea030 con el esqueleto de Player.bmd.
    Applied TurnThePlayersToTheGate(std::string_view where)
    {
        if (m_context.playerSkeleton == nullptr)
            return Core::Unexpected(std::format("{}: 0x5111a0 pone la acción 0xc0 con el esqueleto de Player.bmd, "
                                                "que no está cargado",
                                                where));
        const auto actors = m_world.Actors();
        for (std::size_t index = 0; index < ActorsSearched; ++index)
        {
            ActorState& actor = actors[index];
            if (!actor.alive || actor.type != Data::Models::PlayerSkeletonModel)
                continue;
            actor.angles = {0.0f, 0.0f, GateAngle};
            Render::Models::SetAction(actor.animation, *m_context.playerSkeleton, GateAction);
        }
        return {};
    }

    // 0x511150(clave): +0x360 en 0 en los 400 primeros, y el último vivo con la clave (MOVSX contra la clave sin el
    // bit alto), o el 400 sin ninguno (Q-C1326).
    std::size_t ClearTheWeaponsAndFind(std::uint16_t key)
    {
        const auto actors = m_world.Actors();
        std::size_t found = ActorsSearched;
        for (std::size_t index = 0; index < ActorsSearched; ++index)
        {
            ActorState& actor = actors[index];
            if (actor.alive && static_cast<std::int32_t>(static_cast<std::int16_t>(actor.key)) == key)
                found = index;
            actor.byte360 = 0;
        }
        return found;
    }

    // 0x40ba80(estado, 900, pkt[4], pkt[6], pkt[8]) y, con dueño y arma, el arma en +0x360 del dueño (0x40c1a7..
    // 0x40c1fa; Chaos Castle no la mira).
    static void SetThePlayState(EventMapObject& event, std::uint8_t state, const Events::EventPlayState& packet)
    {
        event.field10 = state;
        event.field14 = {PlayTime, packet.remaining, packet.maxKill, packet.currentKill};
    }

    void GiveTheWeapon(const Events::EventPlayState& packet)
    {
        if (packet.weaponOwner == NoWeaponOwner || packet.weapon == NoWeapon || packet.weapon == 0)
            return;
        const std::size_t owner = ClearTheWeaponsAndFind(static_cast<std::uint16_t>(packet.weaponOwner & KeyMask));
        m_world.Actors()[owner].byte360 = packet.weapon;
    }

    // Los botones de 0x7d368b8 en cero y el primero con `button`.
    void SetTheResultButtons(const UI::DialogButton& button)
    {
        m_runtime.dialogBox.buttons = {};
        m_runtime.dialogBox.buttons[0] = button;
    }

    // El rep movs de `count` registros de 24 bytes desde pkt[5] a +0x2c.
    Applied CopyTheResults(EventMapObject& event, const Events::EventResult& packet, std::size_t count,
                           std::string_view where)
    {
        const std::size_t bytes = count * ResultRecordBytes;
        if (bytes > packet.scores.size())
            return Core::Unexpected(std::format("{}: copia {} bytes desde pkt[5] y el paquete trae {}: el resto lo "
                                                "leería del buffer de la recepción",
                                                where, bytes, packet.scores.size()));
        if (bytes > event.field2c.size() * sizeof(std::int32_t))
            return Core::Unexpected(std::format("{}: {} registros de 24 bytes pasan los 0x108 de +0x2c y pisan el "
                                                "heap después del objeto de 0x134 bytes (Q-C1324)",
                                                where, count));
        for (std::size_t at = 0; at < bytes; ++at)
        {
            const std::size_t word = at / sizeof(std::int32_t);
            const std::size_t shift = (at % sizeof(std::int32_t)) * 8;
            const auto byte = static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(packet.scores[at]));
            auto value = static_cast<std::uint32_t>(event.field2c[word]);
            value = (value & ~(0xffu << shift)) | (byte << shift);
            event.field2c[word] = static_cast<std::int32_t>(value);
        }
        return {};
    }

    // ----- 0B (0x4e3594).

    // 0x4e3621..0x4e3663, después de la cola de 0x77f27b4 (la guarda la sesión): la tormenta (0x723c50) con pkt[4]
    // en 1 o 3, y siempre los 40 pájaros apagados (0x4455c0).
    Applied On(const Events::EventState& packet)
    {
        if (packet.event == StormEvent)
            m_runtime.storm = packet.state != 0 ? Storm : 0;
        else if (packet.event == StrongStormEvent)
            m_runtime.storm = packet.state != 0 ? StrongStorm : 0;
        for (FaunaObject& bird : m_world.Fauna().Birds())
            bird.active = 0;
        return {};
    }

    // ----- 46 (0x4dab90).

    Applied On(const Events::MapAttributes& packet)
    {
        if (packet.type == CellAttributes)
        {
            // 0x4dabba..0x4dabf5: cada celda con 0x5f8730 si pkt[5] no es 0 y con 0x5f8710 si es 0.
            for (const Events::AttributeCell& cell : packet.cells)
            {
                if (auto marked = MarkTheCell(cell.x, cell.y, packet.attribute, packet.mode == 0, "0x4dabd0");
                    !marked.HasValue())
                    return marked;
            }
            return {};
        }
        if (packet.type != AreaAttributes)
            return {};
        // 0x4dabfc..0x4dac18: en Blood Castle, el atributo 8 pide el derrumbe.
        const std::int32_t map = m_runtime.map;
        if (map >= FirstBloodCastleMap && map <= LastBloodCastleMap && packet.attribute == BloodCastleGateAttribute)
            RequestTheCollapse(map, BloodCastleCollapseType, 0, CollapseDelay);
        // 0x4dac2c..0x4dac53: 0x5f8750(x1, y1, x2 − x1 + 1, y2 − y1 + 1, atributo, 1 − pkt[5]) con el byte de la resta:
        // pkt[5] en 0 agrega y en 1 quita, pero en 2 (0xff) también agrega (Q-C1320).
        const bool add = static_cast<std::uint8_t>(1 - packet.mode) != 0;
        for (const Events::AttributeArea& area : packet.areas)
        {
            const CollapseArea rectangle{
                .x = area.x1, .y = area.y1, .width = area.x2 - area.x1 + 1, .height = area.y2 - area.y1 + 1};
            if (auto marked = MarkTheArea(rectangle, packet.attribute, add, "0x4dac53"); !marked.HasValue())
                return marked;
        }
        return {};
    }

    // ----- 47 (0x4d6990).

    Applied On(const Events::ItemAcquired& packet)
    {
        // 0x4d69ad..0x4d69d1: con la clave del héroe, nada.
        if (ActorWithKey(m_world, packet.key) == m_runtime.hero)
            return {};
        return Core::Unexpected(std::string("0x4d6a54: el aviso del 47 lleva el nombre del ítem (0x5b1c70), que es "
                                            "de la CL8"));
    }

    // ----- 90 (0x4e4870).

    Applied On(const Events::DevilSquareEntryResult& packet)
    {
        // 0x4e487d: 0x65cae0(0x1000, 0).
        GamePanels panels(m_world, m_context.random);
        panels.LendTheWindowClose(m_context.sendWindowClose);
        if (auto closed = panels.Registry().Close(UI::EventNpcPanel); !closed.HasValue())
            return Core::Unexpected(std::move(closed).Error());
        if (packet.result < 1 || packet.result > DevilSquareEntryTexts.size())
            return {};
        return MessageBoxOf(DevilSquareEntryTexts[packet.result - 1U], "0x4e4894");
    }

    // ----- 91 (0x4da8c0), con wsprintfA ([0x6b851c], P-C290).

    Applied On(const Events::EventOpenTime& packet)
    {
        constexpr auto Wsprintf = UI::FormatFunction::Wsprintf;
        if (packet.kind == DevilSquareOpenTime)
        {
            if (packet.value4 == 0)
                return MessageBoxOf(DevilSquareOpenText, "0x4da8dc");
            const std::array<UI::FormatArgument, 1> arguments{std::int32_t{packet.value4}};
            return FormattedMessageBox(Wsprintf, DevilSquareMinutesText, arguments, PageStackBuffer, "0x4da901");
        }
        if (packet.kind == BloodCastleOpenTime)
        {
            auto name = TextArgument(BloodCastleName, "0x4da923");
            if (!name.HasValue())
                return Core::Unexpected(std::move(name).Error());
            if (packet.value4 == 0)
            {
                const std::array<UI::FormatArgument, 1> arguments{name.Value()};
                return FormattedMessageBox(Wsprintf, CanEnterNowText, arguments, PageStackBuffer, "0x4da938");
            }
            const std::array<UI::FormatArgument, 2> arguments{std::int32_t{packet.value4}, name.Value()};
            return FormattedMessageBox(Wsprintf, CanEnterInText, arguments, PageStackBuffer, "0x4da968");
        }
        if (packet.kind == ChaosCastleOpenTime)
            return OpenTheChaosCastleTime(packet);
        return {};
    }

    // 0x4da98e..0x4daa75: la palabra pkt[4]:pkt[6]. Los textos van a los registros 1154 y 1155 de Text.bmd en la
    // memoria (300 bytes cada uno) y de ahí 0x633300 los copia a la caja del 0x9a; MuRet se los pasa directo
    // (único lector, 0x633300 con 0x482).
    Applied OpenTheChaosCastleTime(const Events::EventOpenTime& packet)
    {
        constexpr auto Wsprintf = UI::FormatFunction::Wsprintf;
        const auto remaining = static_cast<std::int32_t>((packet.value4 << 8u) | packet.value6);
        auto name = TextArgument(ChaosCastleName, "0x4da9a3");
        if (!name.HasValue())
            return Core::Unexpected(std::move(name).Error());
        if (remaining == 0)
        {
            const std::array<UI::FormatArgument, 1> nowArguments{name.Value()};
            auto first = FormatText(Wsprintf, CanEnterNowText, nowArguments, UI::TextRecordBytes, "0x4da9b2");
            if (!first.HasValue())
                return Core::Unexpected(std::move(first).Error());
            const std::array<UI::FormatArgument, 3> openArguments{name.Value(), std::int32_t{packet.value5},
                                                                  ChaosCastleOpenArgument};
            auto second = FormatText(Wsprintf, ChaosCastleOpenText, openArguments, UI::TextRecordBytes, "0x4da9ce");
            if (!second.HasValue())
                return Core::Unexpected(std::move(second).Error());
            const std::array<std::string_view, 2> texts{first.Value(), second.Value()};
            return UI::OpenTheChaosCastleDialog(m_runtime.dialogBox, m_runtime.dialogs, texts);
        }
        const std::int32_t hours = remaining / MinutesPerHour;
        const std::int32_t minutes = remaining - hours * MinutesPerHour;
        const std::array<UI::FormatArgument, 1> hourArguments{hours};
        auto first = FormatText(Wsprintf, ChaosCastleHoursText, hourArguments, UI::TextRecordBytes, "0x4daa17");
        if (!first.HasValue())
            return Core::Unexpected(std::move(first).Error());
        const std::array<UI::FormatArgument, 2> minuteArguments{minutes, name.Value()};
        auto second = FormatText(Wsprintf, CanEnterInText, minuteArguments, PageStackBuffer, "0x4daa39");
        if (!second.HasValue())
            return Core::Unexpected(std::move(second).Error());
        // 0x4daa3b..0x4daa73: el strcat del segundo sobre el registro 1154.
        std::string joined = std::move(first).Value() + second.Value();
        if (joined.size() >= UI::TextRecordBytes)
            return Core::Unexpected(std::format("0x4daa73: el strcat ocupa {} bytes con su '\\0' y pisa el registro "
                                                "1155 de Text.bmd",
                                                joined.size() + 1));
        const std::array<std::string_view, 1> texts{joined};
        return UI::OpenTheChaosCastleDialog(m_runtime.dialogBox, m_runtime.dialogs, texts);
    }

    // ----- 92 (0x4e4930): con el objeto, 0x40ba60(pkt[3] + 1): +8 y GetTickCount en +0xc.

    Applied On(const Events::EventCountdown& packet)
    {
        if (!m_runtime.eventObject)
            return {};
        m_runtime.eventObject->field08 = std::int32_t{packet.type} + 1;
        m_runtime.eventObject->field0c = static_cast<std::int32_t>(m_context.tickCount);
        return {};
    }

    // ----- 93 (0x4e4959 → 0x4e7360 → el método +0xc del objeto con (pkt[4], pkt[3], pkt + 5, pkt[3])).

    Applied On(const Events::EventResult& packet)
    {
        if (!m_runtime.eventObject)
            return {};
        EventMapObject& event = *m_runtime.eventObject;
        switch (event.kind)
        {
        case EventMapKind::DevilSquare:
        {
            // 0x40bd20: con pkt[4] < 200, el ranking de pkt[4] registros.
            if (std::int32_t{packet.count} >= DevilSquareResultLimit)
                return {};
            SetTheResultButtons(DevilSquareResultButton);
            event.field24 = packet.count;
            event.field28 = packet.rank;
            if (auto copied = CopyTheResults(event, packet, packet.count, "0x40bd97"); !copied.HasValue())
                return copied;
            UI::PushDialog(m_runtime.dialogs, DevilSquareResultDialog);
            return {};
        }
        case EventMapKind::BloodCastle:
            // 0x40c0c0: solo con pkt[4] en 0xff, un registro y +0x24 = pkt[3].
            return ShowTheCastleResult(event, packet, BloodCastleResultCount, BloodCastleResultDialog, "0x40c12a");
        case EventMapKind::ChaosCastle:
            // 0x404da0: ídem con 0xfe.
            return ShowTheCastleResult(event, packet, ChaosCastleResultCount, ChaosCastleResultDialog, "0x404e0a");
        }
        return {};
    }

    Applied ShowTheCastleResult(EventMapObject& event, const Events::EventResult& packet, std::uint8_t count,
                                std::int32_t dialog, std::string_view where)
    {
        if (packet.count != count)
            return {};
        SetTheResultButtons(CastleResultButton);
        event.field24 = packet.rank;
        if (auto copied = CopyTheResults(event, packet, 1, where); !copied.HasValue())
            return copied;
        UI::PushDialog(m_runtime.dialogs, dialog);
        return {};
    }

    // ----- 94 (0x4dd8a0).

    Applied On(const Events::EventChipInfo& packet)
    {
        if (auto closed = CloseAllPanels(); !closed.HasValue())
            return closed;
        UI::PanelState& panels = m_runtime.panels;
        panels.field78abb04 = static_cast<std::int16_t>(packet.count);
        panels.field78abb00 = std::int32_t{packet.type} + 1;
        if (panels.field78abb00 == SerialWindow)
        {
            // 0x4dd8ce..0x4dd8ec: el número de serie en cero, 0x553570(0) y el campo del chat cerrado.
            m_runtime.events.serial = {};
            UI::ResetTheTextBuffers(m_runtime.chat.input, false);
            m_runtime.ui.chatOpen = false;
            return Core::Unexpected(std::string("0x4dd8f2: la entrada del número de serie (0x77f3b6a, 0x77f3b6c, "
                                                "0x78abacc, 0x6c8e78 en 0xc, 0x6c8e6c en 1 y 0x77f3b6b en 1, que lee "
                                                "el Enter del chat) es de la fase 2 del hito 11 de la CL9"));
        }
        if (panels.field78abb00 != MutoWindow)
            return {};
        // 0x4dd926..0x4dd949: los tres números si ninguno es 0xffff.
        if (!packet.muto)
            return Core::Unexpected(std::string("0x4dd92c: el 94 con el tipo 0 lee pkt[6..0xb], que el paquete no "
                                                "trae"));
        const auto& numbers = *packet.muto;
        if (numbers[0] == NoMutoNumber || numbers[1] == NoMutoNumber || numbers[2] == NoMutoNumber)
            return {};
        m_runtime.events.mutoNumbers = numbers;
        return {};
    }

    // ----- 95 (0x4e49d4): la palabra sin signo en 0x78abb04, salvo 0xffff.

    Applied On(const Events::EventChipCount& packet)
    {
        if (packet.count != NoChipCount)
            m_runtime.panels.field78abb04 = packet.count;
        return {};
    }

    // ----- 96 (0x4e49fb): los tres números si ninguno es 0xffff.

    Applied On(const Events::MutoNumbers& packet)
    {
        const auto& numbers = packet.numbers;
        if (numbers[0] == NoMutoNumber || numbers[1] == NoMutoNumber || numbers[2] == NoMutoNumber)
            return {};
        m_runtime.events.mutoNumbers = numbers;
        return {};
    }

    // ----- 99 (0x4e4a40): 0x6236f0 con 0x8f o 0x90.

    Applied On(const Events::EventDialog& packet)
    {
        if (packet.kind < EventDialogs.size())
            UI::PushDialog(m_runtime.dialogs, EventDialogs[packet.kind]);
        return {};
    }

    // ----- 9A (0x4da780), con sprintf (0x69fd9b).

    Applied On(const Events::BloodCastleEntryResult& packet)
    {
        if (auto closed = CloseAllPanels(); !closed.HasValue())
            return closed;
        switch (packet.result)
        {
        case 1:
            return MessageBoxOf(WrongCloakText, "0x4da7ad");
        case 2:
            return EntryOverBox(BloodCastleName, "0x4da7cc");
        case 3:
            return MessageBoxOf(TooStrongText, "0x4da7e1");
        case 4:
            return MessageBoxOf(TooWeakText, "0x4da7f2");
        case 5:
            return CapacityBox(BloodCastleName, BloodCastleCapacity, "0x4da813");
        case 6:
            return TimesPerDayBox("0x4da833");
        case 8:
            return EntryOverBox(ChaosCastleName, "0x4da856");
        case 9:
            return CapacityBox(ChaosCastleName, ChaosCastleCapacity, "0x4da87b");
        default:
            return {};
        }
    }

    // sprintf(buffer, 852, nombre): "% s" con la bandera del espacio, que %s no usa.
    Applied EntryOverBox(std::size_t name, std::string_view where)
    {
        auto argument = TextArgument(name, where);
        if (!argument.HasValue())
            return Core::Unexpected(std::move(argument).Error());
        const std::array<UI::FormatArgument, 1> arguments{argument.Value()};
        return FormattedMessageBox(UI::FormatFunction::Sprintf, EntryOverText, arguments, SmallStackBuffer, where);
    }

    // sprintf(buffer, 853, nombre, número): el "% d" toma el puntero al nombre e imprime su dirección (Q-C1321).
    Applied CapacityBox(std::size_t name, std::int32_t capacity, std::string_view where)
    {
        auto argument = TextArgument(name, where);
        if (!argument.HasValue())
            return Core::Unexpected(std::move(argument).Error());
        const std::array<UI::FormatArgument, 2> arguments{argument.Value(), capacity};
        return FormattedMessageBox(UI::FormatFunction::Sprintf, CapacityText, arguments, SmallStackBuffer, where);
    }

    Applied TimesPerDayBox(std::string_view where)
    {
        const std::array<UI::FormatArgument, 1> arguments{TimesPerDay};
        return FormattedMessageBox(UI::FormatFunction::Sprintf, TimesPerDayText, arguments, LargeStackBuffer, where);
    }

    // ----- 9B (0x4e4999): con el objeto, su método +8.

    Applied On(const Events::EventPlayState& packet)
    {
        if (!m_runtime.eventObject)
            return {};
        EventMapObject& event = *m_runtime.eventObject;
        switch (event.kind)
        {
        case EventMapKind::DevilSquare:
            // 0x40bdc0: ret 4.
            return {};
        case EventMapKind::BloodCastle:
            return PlayBloodCastle(event, packet);
        case EventMapKind::ChaosCastle:
            return PlayChaosCastle(event, packet);
        }
        return {};
    }

    // 0x40c140, por pkt[3] de 0 a 4 (la tabla 0x40c2b0).
    Applied PlayBloodCastle(EventMapObject& event, const Events::EventPlayState& packet)
    {
        switch (packet.state)
        {
        case 0:
            // 0x40c165..0x40c178, y sigue en el caso 1.
            if (auto turned = TurnThePlayersToTheGate("0x40c16a"); !turned.HasValue())
                return turned;
            Play(BloodCastleMusic);
            [[fallthrough]];
        case 1:
        case 4:
            // 0x40c180..0x40c1fa y 0x40c23c..0x40c2a1: +0x10 = pkt[3] + 1.
            SetThePlayState(event, static_cast<std::uint8_t>(packet.state + 1), packet);
            GiveTheWeapon(packet);
            return {};
        case 2:
            // 0x40c205: 0x40ba20 y la música apagada.
            ResetThePlayState(event);
            Stop(BloodCastleMusic);
            return {};
        case 3:
            // 0x40c221: el derrumbe de la puerta.
            RequestTheCollapse(m_runtime.map, BloodCastleCollapseType, BloodCastleCollapseCounter, CollapseDelay);
            return {};
        default:
            return {};
        }
    }

    // 0x40ba20: +0x10 en 0 y +0x14..+0x20 en −1.
    static void ResetThePlayState(EventMapObject& event)
    {
        event.field10 = 0;
        event.field14 = {-1, -1, -1, -1};
    }

    // 0x404e20, por pkt[3] de 5 a 10 (la tabla 0x405070).
    Applied PlayChaosCastle(EventMapObject& event, const Events::EventPlayState& packet)
    {
        switch (packet.state)
        {
        case ChaosCastleStart:
        {
            // 0x404e45..0x404e99.
            m_runtime.chaosCastleTrapArmed = ChaosCastleTrapArmed;
            m_runtime.chaosCastleArea = NoChaosCastleArea;
            if (m_runtime.ui.chatOpen)
            {
                m_runtime.ui.chatOpen = false;
                UI::ShowTheChatFields(m_runtime.chat.input, HideTheChatFields);
            }
            if (auto turned = TurnThePlayersToTheGate("0x404e7f"); !turned.HasValue())
                return turned;
            Stop(ChaosCastleWaitMusic);
            Play(ChaosCastleMusic);
            return {};
        }
        case ChaosCastlePlay:
            // 0x404ea6..0x404ec3.
            SetThePlayState(event, static_cast<std::uint8_t>(packet.state + 1), packet);
            return {};
        case ChaosCastleEnd:
        {
            // 0x404ecd..0x404f0f.
            ResetThePlayState(event);
            Stop(ChaosCastleMusic);
            const std::int32_t map = m_runtime.map;
            if (map >= FirstChaosCastleMap && map <= LastChaosCastleMap)
            {
                Play(ChaosCastleWaitMusic);
                Play(ChaosCastleEndSound);
            }
            return {};
        }
        case ChaosCastleFirstCollapse:
        case ChaosCastleSecondCollapse:
        case ChaosCastleThirdCollapse:
            return CollapseChaosCastle(static_cast<std::size_t>(packet.state - ChaosCastleFirstCollapse));
        default:
            return {};
        }
    }

    // 0x404f1c, 0x404f88 y 0x404ff2: con el área esperada, la siguiente, el derrumbe y cuatro rectángulos con el
    // atributo 8 (0x5f8750 con agregar en 1) y el sonido 0x61.
    Applied CollapseChaosCastle(std::size_t step)
    {
        if (m_runtime.chaosCastleArea != ChaosCastleCollapseAreas[step])
            return {};
        m_runtime.chaosCastleArea = static_cast<std::uint8_t>(ChaosCastleCollapseAreas[step] + 1);
        RequestTheCollapse(m_runtime.map, ChaosCastleCollapseType, ChaosCastleCollapseCounter, CollapseDelay);
        for (const CollapseArea& area : ChaosCastleCollapses[step])
        {
            if (auto marked = MarkTheArea(area, ChaosCastleCollapseAttribute, true, "0x405056"); !marked.HasValue())
                return marked;
        }
        Play(ChaosCastleCollapseSound);
        return {};
    }

    // ----- 9D (0x4e4a86): el aviso y los 0x40 bytes en 0x78ab078, con cualquier resultado.

    Applied On(const Events::SerialResult& packet)
    {
        if (packet.result <= LastIndexedSerialResult)
        {
            if (auto shown = MessageBoxOf(FirstSerialText + packet.result, "0x4e4ab1"); !shown.HasValue())
                return shown;
        }
        else if (packet.result == SerialAlreadyRegistered)
        {
            if (auto shown = MessageBoxOf(SerialAlreadyRegisteredText, "0x4e4ab1"); !shown.HasValue())
                return shown;
        }
        m_runtime.events.serial = packet.data;
        return {};
    }

    // ----- 9E (0x4e4ad4).

    Applied On(const Events::EventSound& packet)
    {
        if (packet.sound < EventSounds.size())
            Play(EventSounds[packet.sound]);
        return {};
    }

    // ----- AF 01 (0x4e5032 → 0x4daa90), por pkt[4] de 1 a 7 (la tabla 0x4dab6c).

    Applied On(const Events::ChaosCastleEntryResult& packet)
    {
        if (auto closed = CloseAllPanels(); !closed.HasValue())
            return closed;
        switch (packet.result)
        {
        case 1:
            return MessageBoxOf(WrongCloakText, "0x4daabd");
        case 2:
            return EntryOverBox(ChaosCastleName, "0x4daadc");
        case 3:
            return MessageBoxOf(TooStrongText, "0x4daaf1");
        case 4:
            return MessageBoxOf(TooWeakText, "0x4dab02");
        case 5:
            return CapacityBox(ChaosCastleName, ChaosCastleCapacity, "0x4dab23");
        case 6:
            return TimesPerDayBox("0x4dab43");
        case 7:
            return MessageBoxOf(ChaosCastleNoteText, "0x4dab58");
        default:
            return {};
        }
    }

    // ----- BC 00 (0x4e4547) y BC 01 (0x4da540).

    Applied On(const Events::JewelMixResult& packet)
    {
        if (packet.subcode == Events::JewelMixSubcode)
        {
            switch (packet.result)
            {
            case 0:
            case 2:
            case 3:
                return JewelMessage(JewelMixText, "0x4e45ad");
            case 1:
                return OpenTheJewelDialog();
            case 4:
                return JewelNotice(JewelMissingText, "0x4e4605");
            case 5:
                return JewelNotice(JewelNoZenText, "0x4e462a");
            default:
                return {};
            }
        }
        switch (packet.result)
        {
        case 0:
        case 5:
            return JewelMessage(JewelUnmixText, "0x4da58b");
        case 1:
            return OpenTheJewelDialog();
        case 2:
        case 3:
        case 4:
        case 6:
            return JewelNotice(JewelWrongItemText, "0x4da5d3");
        case 7:
            return JewelNotice(JewelNoSpaceText, "0x4da5f0");
        case 8:
            return JewelNotice(JewelNoZenText, "0x4da60d");
        default:
            return {};
        }
    }

    // sprintf(buffer de 0x100, "%s%s %s", título, 1816, 868), el mensaje y 0x402e50.
    Applied JewelMessage(std::size_t title, std::string_view where)
    {
        std::array<UI::FormatArgument, 3> arguments{};
        const std::array<std::size_t, 3> texts{title, JewelToText, JewelTimesText};
        for (std::size_t index = 0; index < texts.size(); ++index)
        {
            auto argument = TextArgument(texts[index], where);
            if (!argument.HasValue())
                return Core::Unexpected(std::move(argument).Error());
            arguments[index] = argument.Value();
        }
        auto text = Format(UI::FormatFunction::Sprintf, JewelFormat, arguments, PageStackBuffer, where);
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        if (auto said = Message(text.Value(), where); !said.HasValue())
            return said;
        return CloseTheJewelWindow();
    }

    Applied JewelNotice(std::size_t index, std::string_view where)
    {
        auto text = Text(index, where);
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        if (auto said = Message(text.Value(), where); !said.HasValue())
            return said;
        return CloseTheJewelWindow();
    }

    // 0x4e45d9 y 0x4da5af: 0x6236f0(0) y 0x6236f0(0xa6).
    Applied OpenTheJewelDialog()
    {
        UI::PushDialog(m_runtime.dialogs, 0);
        UI::PushDialog(m_runtime.dialogs, JewelDialog);
        return {};
    }

    // 0x402e50: la ventana de la mezcla de joyas (0x6e5794..0x6e5799 y 0x6c62e0), que MuRet no tiene.
    static Applied CloseTheJewelWindow()
    {
        return Core::Unexpected(std::string("0x402e50: cerrar la ventana de la mezcla de joyas (0x6e5798, 0x6e5799, "
                                            "0x6c62e0 y los diálogos 0xa2 y 0xa4, o 0x402eb0) es del hito 6 de la "
                                            "CL9"));
    }

    // ----- BD 00 (0x4e5965): 0x42fe80(pkt[4], pkt[5]) y 0x4033b0(pkt[5]) sobre [0x6e5904].

    Applied On(const Events::CrywolfState& packet)
    {
        if (auto changed = ChangeTheCrywolfState(packet.state, packet.stage); !changed.HasValue())
            return changed;
        m_runtime.events.cameraMoveStage = packet.stage;
        return {};
    }

    // 0x42fe80(estado, etapa).
    Applied ChangeTheCrywolfState(std::uint8_t state, std::uint8_t stage)
    {
        CrywolfGlobals& crywolf = m_runtime.crywolf;
        CrywolfInterface& ui = crywolf.ui;
        if (crywolf.state == state && crywolf.stage == stage)
            return {};
        crywolf.stage = stage;
        if (stage == CrywolfWarningStage)
            ui.byte723bec = 1;
        else
        {
            ui.byte723a3d = 0;
            if (stage == CrywolfFightStage)
            {
                // 0x42febf..0x42fef5: 0x6c73e4..0x6c73f4 en −1 y, salvo con el estado 2, el aviso.
                for (std::size_t index = 1; index < ui.dwords6c73e0.size(); ++index)
                    ui.dwords6c73e0[index] = -1;
                if (state != CrywolfLostState)
                {
                    ui.message = 1;
                    if (state == CrywolfPeaceState)
                        ui.dword6c73dc = CrywolfPeaceCounter;
                    ui.dword6c73f8 = CrywolfFightTime;
                }
            }
            else if (stage == CrywolfEndStage)
                ResetTheCrywolfInterface(m_runtime);
        }
        if (crywolf.state == state)
            return {};
        crywolf.state = state;
        if (state > LastLitCrywolfState)
            return Core::Unexpected(std::format("0x42ff81: con el estado {} de Crywolf, 0x5f8f40 lee el buffer de la "
                                                "luz sin escribir (Q-C1328)",
                                                static_cast<int>(state)));
        return Core::Unexpected(std::format("0x42ff81: la luz (0x5f8f40) y el .att (0x5f8390) de World{} con el estado "
                                            "{} de Crywolf son de la fase 2 del hito 11 de la CL9",
                                            m_runtime.map + 1, static_cast<int>(state)));
    }

    // ----- F3 22 (0x4e31f8).

    Applied On(const Events::EventTime& packet)
    {
        m_runtime.events.timeKind = packet.value4;
        m_runtime.events.timeValue = packet.value6;
        return {};
    }

    // ----- F3 23 (0x4d9f00).

    Applied On(const Events::SoccerScore& packet)
    {
        SoccerScoreGlobals& soccer = m_runtime.events.soccer;
        soccer.firstName = {};
        soccer.secondName = {};
        for (std::size_t index = 0; index < packet.firstName.size(); ++index)
        {
            soccer.firstName[index] = packet.firstName[index];
            soccer.secondName[index] = packet.secondName[index];
        }
        soccer.active = packet.firstScore != NoSoccerScore;
        soccer.firstScore = packet.firstScore;
        soccer.secondScore = packet.secondScore;
        return Core::Unexpected(std::string("0x4d9ff2: el efecto 0x60e (0x518cf0) sobre la celda del héroe es de la "
                                            "CL11"));
    }

    // ----- F3 40 (0x4da070), por pkt[4] de 0 a 0x31 (las tablas 0x4da24c y 0x4da224).

    Applied On(const Events::ServerCommand& packet)
    {
        const std::uint8_t command = packet.command;
        if (command >= FirstCommandText && command <= LastCommandText)
            return MessageBoxOf(CommandTextBase + command, "0x4da215");
        switch (command)
        {
        case CommandEffect:
            return Core::Unexpected(std::string("0x4da125: el efecto 0x60e (0x518cf0) en la celda del F3 40 es de la "
                                                "CL11"));
        case CommandNotice:
        {
            const std::size_t base = packet.value5 >= HighNoticeFrom ? HighNoticeBase : LowNoticeBase;
            return MessageBoxOf(base + packet.value5, "0x4da15f");
        }
        case CommandSound:
            Play(CommandSoundId);
            return {};
        case CommandMessage:
            return MessageBoxOf(MessageBase + packet.value5, "0x4da183");
        case CommandDialog:
            return Core::Unexpected(std::format("0x4da195: el diálogo {} de la tabla de 0x632f70 es del hito 2 de la "
                                                "CL9",
                                                static_cast<int>(packet.value5)));
        case CommandFixedNotice:
            return MessageBoxOf(FixedNoticeText, "0x4da1a6");
        case CommandCastleNotice:
            if (packet.value5 == 0)
                return UI::OpenTextDialog(m_runtime.dialogBox, m_runtime.dialogs, m_context.texts, 1,
                                          static_cast<std::int32_t>(CastleNoticeBase), CastleNoticeDialog);
            return MessageBoxOf(CastleNoticeBase + packet.value5, "0x4da1e7");
        case CommandGuildNotice:
            return MessageBoxOf(GuildNoticeText, "0x4da1f8");
        default:
            return {};
        }
    }

    const EventPacketContext& m_context;
    WorldState& m_world;
    WorldRuntime& m_runtime;
    EventPacketResult m_result{};
};
} // namespace

Core::Expected<EventPacketResult, std::string> ApplyEventPacket(const Protocol::Events::EventPacket& packet,
                                                                const EventPacketContext& context)
{
    return Applier(context).Apply(packet);
}

Core::Expected<std::optional<std::string>, std::string>
EventCountdownText(EventMapObject& event, std::uint32_t tickCount, const Data::Text::TextTable* texts, bool write)
{
    const std::int32_t kind = event.field08;
    if (kind < FirstCountdown || kind > LastCountdown)
        return std::optional<std::string>{};
    // 0x40bad7..0x40baf1: la resta sin signo contra +0xc.
    const std::uint32_t elapsed = tickCount - static_cast<std::uint32_t>(event.field0c);
    if (elapsed > CountdownMs)
    {
        if (write)
            event.field08 = 0;
        return std::optional<std::string>{};
    }
    const auto left = static_cast<std::int32_t>(CountdownSeconds - static_cast<std::int32_t>(elapsed / MsPerSecond));
    const auto textOf = [texts](std::size_t index) -> Core::Expected<std::string_view, std::string>
    {
        const std::optional<std::string_view> text = UI::TextAt(texts, index);
        if (!text)
            return Core::Unexpected(std::format("0x40bab0: sin el texto {} de Text.bmd", index));
        return *text;
    };
    std::size_t format = OtherCountdownBase + static_cast<std::size_t>(kind);
    std::optional<std::size_t> name;
    if (kind >= FirstBloodCastleCountdown && kind <= LastBloodCastleCountdown)
    {
        format = BloodCastleCountdownBase + static_cast<std::size_t>(kind);
        name = BloodCastleName;
    }
    else if (kind >= FirstChaosCastleCountdown && kind <= LastChaosCastleCountdown)
    {
        format = ChaosCastleCountdownBase + static_cast<std::size_t>(kind);
        if (format == ChaosCastleSkippedText)
            format = ChaosCastleReplacementText;
        name = ChaosCastleName;
    }
    auto formatText = textOf(format);
    if (!formatText.HasValue())
        return Core::Unexpected(std::move(formatText).Error());
    std::vector<UI::FormatArgument> arguments;
    if (name)
    {
        auto nameText = textOf(*name);
        if (!nameText.HasValue())
            return Core::Unexpected(std::move(nameText).Error());
        arguments.emplace_back(UI::TextArgument{.index = *name, .text = nameText.Value()});
    }
    arguments.emplace_back(left);
    auto text = UI::FormatOriginal(UI::FormatFunction::Wsprintf, formatText.Value(), arguments, "0x40bb56");
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    if (text.Value().size() >= PageStackBuffer)
        return Core::Unexpected(std::format("0x40bb56: la cuenta ocupa {} bytes con su '\\0' y el buffer tiene {}",
                                            text.Value().size() + 1, PageStackBuffer));
    return std::optional<std::string>{std::move(text).Value()};
}

Core::Expected<void, std::string> CheckTheObjectCollapse(const WorldRuntime& runtime)
{
    const ObjectCollapse& collapse = runtime.events.objectCollapse;
    if (collapse.map < 0 || collapse.type < 0 || collapse.counter < 0 || runtime.map != collapse.map)
        return {};
    return Core::Unexpected(std::format("0x5ff960: el derrumbe de los objetos {:#x} del mapa {} (la cuenta en {}, "
                                        "0x405630 primero) es de la fase 2 del hito 11 de la CL9",
                                        collapse.type, collapse.map, collapse.counter));
}

void CountDownTheObjectCollapse(WorldRuntime& runtime)
{
    std::int32_t& counter = runtime.events.objectCollapse.counter;
    counter = static_cast<std::int32_t>(static_cast<std::uint32_t>(counter) - 1U);
}

void ResetTheCrywolfInterface(WorldRuntime& runtime)
{
    CrywolfInterface& ui = runtime.crywolf.ui;
    ui.dword723a40 = 0;
    ui.byte723ca0 = 0;
    ui.dwords723bdc = {};
    ui.dword7239f4 = -1;
    ui.dword6c73dc = 0x0a;
    ui.dword6c8e54 = -1;
    ui.dword6c73d8 = 1;
    ui.byte723bec = 0;
    // 0x42fe0d: solo el primero de los seis.
    ui.dwords6c73e0[0] = -1;
    ui.byte723a3d = 0;
    ui.message = -1;
    ui.byte723a44 = 0;
    ui.dword723bd8 = 0;
    ui.dword6c73d4 = 0x64;
    runtime.crywolf.state = 0;
    runtime.crywolf.stage = 0;
    ui.light = 0;
    // 0x42fe41: 0x6c8e2c, el segundo de los objetivos elegidos.
    runtime.selectedTargets[1] = -1;
}
} // namespace Client::Scenes
