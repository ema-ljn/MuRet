#include "Events/EventPackets.h"
#include "Actors/ActorName.h"
#include "Kernel/ActorSearch.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Kernel/ModelEffectArguments.h"
#include "Tables/PlayerModels.h"
#include "Tables/DialogTable.h"
#include "Tables/TextTable.h"
#include "Tables/PlayerActions.h"
#include "Maps/MapId.h"
#include "Protocol/Events/Events.h"
#include "Protocol/Movement/Movement.h"
#include "Protocol/Outgoing.h"
#include "Models/Animation.h"
#include "Models/IModelLibrary.h"
#include "Models/ModelId.h"
#include "Models/LogicalAccess.h"
#include "Kernel/ActorState.h"
#include "Chat/ChatBubbles.h"
#include "Events/Crywolf/CrywolfContract.h"
#include "Events/EventChipWindow.h"
#include "Events/EventGlobals.h"
#include "Events/EventMapObject.h"
#include "Fauna/FaunaObject.h"
#include "Fauna/FaunaState.h"
#include "Chat/GameMessages.h"
#include "Hud/GamePanels.h"
#include "Items/InventoryMoves.h"
#include "CastleSiege/CastleWindows.h"
#include "Items/ItemNames.h"
#include "ChaosMix/JewelMix.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Terrain/TerrainMarks.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatInput.h"
#include "Ui/Dialogs.h"
#include "Ui/GameDialogs.h"
#include "Ui/GameText.h"
#include "Ui/MessageArea.h"
#include "Crt/OriginalFormat.h"
#include "Ui/PanelRegistry.h"
#include "Terrain/WorldTerrain.h"
#include "Effects/ModelEffects.h"
#include "Terrain/TerrainHeight.h"
#include "Protocol/Opcodes.h"
#include "Sound/SoundIds.h"
#include "Audio/SoundId.h"
#include "Chat/ChatRuntime.h"
#include "Ui/GameUiState.h"

#include <algorithm>
#include <bit>
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

namespace Client::Events
{
namespace
{
namespace Events = Protocol::Events;
using Applied = Core::Expected<void, std::string>;

// The event names the formats take as an argument (a pointer to their Text.bmd record).
constexpr std::size_t BloodCastleName = 1146; // 0x77110d4
constexpr std::size_t ChaosCastleName = 1147; // 0x7711200

// The notices shared by 9A (0x4da780) and AF 01 (0x4daa90).
constexpr std::size_t WrongCloakText = 854;      // 0x76fbaa4
constexpr std::size_t EntryOverText = 852;       // 0x76fb84c: "...en% s ya ha terminado"
constexpr std::size_t CapacityText = 853;        // 0x76fb978: "...permitido y% d"
constexpr std::size_t TimesPerDayText = 867;     // 0x76fc9e0: "...más que% d por día."
constexpr std::size_t TooStrongText = 686;       // 0x76ef5c4
constexpr std::size_t TooWeakText = 687;         // 0x76ef6f0
constexpr std::size_t ChaosCastleNoteText = 423; // 0x76dc190, AF 01 with 7
// 0x4da803 and 0x4da86b: the number passed to the "% d" of 853, after the name.
constexpr std::int32_t BloodCastleCapacity = 10;
constexpr std::int32_t ChaosCastleCapacity = 100;
// 0x4da828 and 0x4dab38: the one of 867.
constexpr std::int32_t TimesPerDay = 6;
// The stack buffers of 0x4da780 and 0x4daa90: 0x64 bytes at [EBP − 0x64] and 0x80 at [EBP − 0x80] (the one of 867).
constexpr std::size_t SmallStackBuffer = 0x64;
constexpr std::size_t LargeStackBuffer = 0x80;
// 0x4da8c0, 0x4e4570, 0x4da540 and 0x40bab0: 0x100 bytes.
constexpr std::size_t PageStackBuffer = 0x100;

// 0x4e4894: the notices of 90 with pkt[3] from 1 to 5 (table 0x4e60ec).
constexpr std::array<std::size_t, 5> DevilSquareEntryTexts{677, 678, 686, 687, 679};

// 0x4da8c0: the notices of 91.
constexpr std::uint8_t DevilSquareOpenTime = 1;
constexpr std::uint8_t BloodCastleOpenTime = 2;
constexpr std::uint8_t ChaosCastleOpenTime = 4;
constexpr std::size_t DevilSquareOpenText = 643;    // 0x76ec360
constexpr std::size_t DevilSquareMinutesText = 644; // 0x76ec48c
constexpr std::size_t CanEnterNowText = 850;        // 0x76fb5f4: "...en% s ahora"
constexpr std::size_t CanEnterInText = 851;         // 0x76fb720: "...en% d minutos"
constexpr std::size_t ChaosCastleOpenText = 1156;   // 0x7711c8c, the second line's format
constexpr std::size_t ChaosCastleHoursText = 1164;  // 0x77125ec: "Cuando %d"
// 0x4da9bc: the third argument of the 1156 format.
constexpr std::int32_t ChaosCastleOpenArgument = 100;
// 0x4da9f0..0x4daa0a: the division by 60 of the word pkt[4]:pkt[6].
constexpr std::int32_t MinutesPerHour = 60;

// 0x4e4a86: the notices of 9D with pkt[3] from 0 to 4 (0x76fe024 + 300 · n) and with 5.
constexpr std::size_t FirstSerialText = 886;
constexpr std::uint8_t LastIndexedSerialResult = 4;
constexpr std::uint8_t SerialAlreadyRegistered = 5;
constexpr std::size_t SerialAlreadyRegisteredText = 899; // 0x76fef60

// 0x4e4ad4: the sounds of 9E with the word of pkt[4] at 0, 1 and 2.
constexpr std::array<Audio::SoundId, 3> EventSounds{Audio::SoundId{0x5b}, Audio::SoundId{0x5c}, Audio::SoundId{0x5d}};

// 0x4e4a40: the dialogs of 99 with pkt[3] at 0 and 1.
constexpr std::array<std::int32_t, 2> EventDialogs{0x8f, 0x90};

// 0x4e3621..0x4e365e: 0B with pkt[4] at 1 (the storm at 1 or 0) or at 3 (at 3 or 0).
constexpr std::uint8_t StormEvent = 1;
constexpr std::uint8_t StrongStormEvent = 3;
constexpr std::int32_t Storm = 1;
constexpr std::int32_t StrongStorm = 3;

// 0x4dab90: the type of pkt[3].
constexpr std::uint8_t AreaAttributes = 0;
constexpr std::uint8_t CellAttributes = 1;

// The event maps (0x6c9dd0).
using Maps::FirstBloodCastleMap;
using Maps::FirstChaosCastleMap;
using Maps::LastBloodCastleMap;
using Maps::LastChaosCastleMap;

// 0x4dac0b and 0x40c221: the Blood Castle collapse (attribute 8 of 46 and 9B with 3).
constexpr std::uint8_t BloodCastleGateAttribute = 8;
constexpr std::int32_t BloodCastleCollapseType = 0x24;
constexpr std::int32_t BloodCastleCollapseCounter = 0x14;
// 0x404f2e: Chaos Castle's.
constexpr std::int32_t ChaosCastleCollapseType = 1;
constexpr std::int32_t ChaosCastleCollapseCounter = 0x28;
constexpr std::int32_t CollapseDelay = 1;

// 0x40bd20, 0x40c0c0 and 0x404da0: the first button of the results box and the dialogs.
constexpr Ui::DialogButton DevilSquareResultButton{.state = 1, .x = 0x47, .y = 0x104, .width = 0x46, .height = 0x15};
constexpr Ui::DialogButton CastleResultButton{.state = 1, .x = 0x47, .y = 0x82, .width = 0x46, .height = 0x15};
constexpr std::int32_t DevilSquareResultDialog = 0x8b;
constexpr std::int32_t BloodCastleResultDialog = 0x97;
constexpr std::int32_t ChaosCastleResultDialog = 0x9b;
// 0x40bd2a: Devil Square copies pkt[4] records of 24 bytes if pkt[4] < 200.
constexpr std::int32_t DevilSquareResultLimit = 200;
constexpr std::size_t ResultRecordBytes = 24;
// 0x40c0cb and 0x404dab: Blood Castle with pkt[4] at 0xff and Chaos Castle with 0xfe copy one.
constexpr std::uint8_t BloodCastleResultCount = 0xff;
constexpr std::uint8_t ChaosCastleResultCount = 0xfe;

// 0x40ba80: the time the Blood Castle and Chaos Castle 9B leave in +0x14.
constexpr std::int32_t PlayTime = 0x384;
// 0x5111a0(0xc0) and 0x41ed10(0x8b, 0, 1): the entry to Blood Castle.
constexpr std::uint8_t GateAction = 0xc0;
constexpr float GateAngle = 180.0f;
constexpr Audio::SoundId BloodCastleMusic = Sound::BloodCastleSound;
// 0x40c1ab..0x40c1c3: without an owner (0xffff) or without a weapon (0 or 0xff), nothing.
constexpr std::uint16_t NoWeaponOwner = 0xffff;
constexpr std::uint8_t NoWeapon = 0xff;
// 0x40c1c9: the key without the high bit.
constexpr std::uint16_t KeyMask = 0x7fff;
// 0x511150 and 0x5111a0 walk the first 400; without the key, 0x511150 gives 400.
constexpr std::size_t ActorsSearched = 400;

// 0x404e20: the Chaos Castle cases (pkt[3] from 5 to 10).
constexpr std::uint8_t ChaosCastleStart = 5;
constexpr std::uint8_t ChaosCastlePlay = 6;
constexpr std::uint8_t ChaosCastleEnd = 7;
constexpr std::uint8_t ChaosCastleFirstCollapse = 8;
constexpr std::uint8_t ChaosCastleSecondCollapse = 9;
constexpr std::uint8_t ChaosCastleThirdCollapse = 10;
constexpr Audio::SoundId ChaosCastleMusic = Sound::ChaosCastleSound;
constexpr Audio::SoundId ChaosCastleWaitMusic = Sound::ChaosSound;
constexpr Audio::SoundId ChaosCastleEndSound{0x9f};
constexpr Audio::SoundId ChaosCastleCollapseSound = Sound::WallFallSound;
// 0x404e5a..0x404e75: the open chat field closes, and with the EDITs (0x6c7b8c at 1, as in MuRet) 0x469450(3) hides
// them.
constexpr std::int32_t HideTheChatFields = 3;
// 0x404e53: the area at 0xff; cases 8, 9 and 10 expect 0, 3 and 6 and leave 1, 4 and 7 (0x405e00 moves it).
constexpr std::uint8_t NoChaosCastleArea = 0xff;
constexpr std::uint8_t ChaosCastleTrapArmed = 1;

// Attribute 8 of the Chaos Castle collapses, in the rectangles of 0x5f8750.
constexpr std::uint8_t ChaosCastleCollapseAttribute = 8;
// 0x404f41..0x404f83, 0x404fae..0x404fee and 0x405014..0x405054.
constexpr std::array<std::array<Terrain::TerrainArea, 4>, 3> ChaosCastleCollapses{{
    {{{0x17, 0x4b, 0x16, 2}, {0x2b, 0x4d, 2, 0x20}, {0x17, 0x6b, 0x14, 2}, {0x17, 0x4d, 2, 0x1e}}},
    {{{0x19, 0x4d, 0x12, 2}, {0x29, 0x4f, 2, 0x1c}, {0x19, 0x69, 0x10, 2}, {0x19, 0x4f, 2, 0x1a}}},
    {{{0x1b, 0x4f, 0xe, 2}, {0x27, 0x51, 2, 0x18}, {0x1b, 0x67, 0xc, 2}, {0x1b, 0x51, 2, 0x16}}},
}};
// What the three collapses expect in 0x6c634c, and what they leave.
constexpr std::array<std::uint8_t, 3> ChaosCastleCollapseAreas{0, 3, 6};

// 0x4dd8a0: the mode of 0x78abb00 (pkt[3] + 1) with the Muto numbers and with the serial number.
constexpr std::int32_t MutoWindow = 1;
constexpr std::uint16_t NoMutoNumber = 0xffff;
constexpr std::uint16_t NoChipCount = 0xffff;

// 0x4d6990: the notice of 47 with the two 100-byte stack buffers zeroed, the prefix of 0x10000 (620, 0x76ea86c) or of
// 0x20000 (1089, 0x770ce08) with "%s " (0x6c844c and 0x6c8450), the type in the low 13 bits, what 0x2000 (176,
// 0x76ca01c), 0x8000 (177, 0x76ca148) and 0x4000 (178, 0x76ca274) add in that order, and "%s %s" (0x6c8454) with 918
// (0x77005a4).
constexpr std::size_t ItemNoticeBuffer = 100;
constexpr std::uint32_t ItemTypeMask = 0x1fff;
constexpr std::uint32_t FirstPrefixFlag = 0x10000;
constexpr std::uint32_t SecondPrefixFlag = 0x20000;
constexpr std::size_t FirstPrefixText = 620;
constexpr std::size_t SecondPrefixText = 1089;
constexpr std::string_view ItemPrefixFormat = "%s ";
struct ItemSuffix
{
    std::uint32_t flag;
    std::size_t text;
};
constexpr std::array<ItemSuffix, 3> ItemSuffixes{{{0x2000, 176}, {0x8000, 177}, {0x4000, 178}}};
constexpr std::size_t ItemAcquiredText = 918;
constexpr std::string_view ItemNoticeFormat = "%s %s";

// 0x4e4547 and 0x4da540: the results of BC 00 and BC 01.
constexpr std::size_t JewelMixText = 1801;       // 0x7741068
constexpr std::size_t JewelUnmixText = 1800;     // 0x7740f3c
constexpr std::size_t JewelToText = 1816;        // 0x77421fc
constexpr std::size_t JewelTimesText = 868;      // 0x76fcb0c
constexpr std::size_t JewelMissingText = 1817;   // 0x7742328
constexpr std::size_t JewelNoZenText = 1811;     // 0x7741c20
constexpr std::size_t JewelWrongItemText = 1812; // 0x7741d4c
constexpr std::size_t JewelNoSpaceText = 1815;   // 0x77420d0
// 0x6c8490 and 0x6c8498: "%s%s %s".
constexpr std::string_view JewelFormat = "%s%s %s";
constexpr std::int32_t JewelDialog = 0xa6;

// 0x42febf..0x42fef5: what the end stage leaves in 0x6c73dc with the peace state and, with any state but the war
// one, in 0x6c73f8.
constexpr std::int32_t CrywolfPeaceCounter = 0xb;
constexpr std::int32_t CrywolfFightTime = 0x186;
// 0x42ff3f..0x42ff75: the states with their light; another leaves the buffer unwritten.
constexpr std::uint8_t LastLitCrywolfState = Events::CrywolfWarState;
// 0x4e1010: the BD 03 results, the level from which 0 gives 0x3a (0x4e1144), the first altar key (0x4e11b7) and the
// sitting action (0x4e11cc).
constexpr std::uint8_t ContractFailed = 0;
constexpr std::uint8_t ContractSigned = 1;
constexpr std::int32_t ContractLevel = 0x15e;
constexpr std::int32_t FirstAltarKey = 0x13c;
using Tables::SittingAction;
// 0x4303c0: the map 0x22, which the BD 04 tests with the started stage (0x4359f0).
using Maps::CrywolfMap;
// 0x437620: five places (0x6c73e4..0x6c73f4, 0x723a30..0x723a34 and 0x7239f8..0x723a2e).
constexpr std::size_t CrywolfRankingPlaces = 5;

// 0x4d9f59: the scoreboard with no match (pkt[0xc] at 0xff).
constexpr std::uint8_t NoSoccerScore = 0xff;

// 0x4da070: the F3 40 commands.
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
constexpr Audio::SoundId CommandSoundId{0x5a};
constexpr std::uint8_t HighNoticeFrom = 0x14;
constexpr std::size_t LowNoticeBase = 0x28a;    // 650
constexpr std::size_t HighNoticeBase = 0x32a;   // 810
constexpr std::size_t MessageBase = 710;        // 0x76f11e4
constexpr std::size_t FixedNoticeText = 449;    // 0x76de008
constexpr std::size_t CastleNoticeBase = 0x7e4; // 2020, 0x775110c
constexpr std::int32_t CastleNoticeDialog = 0xaa;
constexpr std::size_t GuildNoticeText = 1826; // 0x7742db4
constexpr std::size_t CommandTextBase = 1776; // 0x773f31c

// 0x40bab0: the 30 s countdown.
constexpr std::int32_t FirstCountdown = 1;
constexpr std::int32_t LastCountdown = 14;
constexpr std::uint32_t CountdownMs = 30000;
constexpr std::uint32_t MsPerSecond = 1000;
constexpr std::int32_t CountdownSeconds = 30;
constexpr std::int32_t FirstBloodCastleCountdown = 4;
constexpr std::int32_t LastBloodCastleCountdown = 7;
constexpr std::int32_t FirstChaosCastleCountdown = 11;
constexpr std::int32_t LastChaosCastleCountdown = 14;
// 0x40bb31: 0x76f92cc + 300 · n, Blood Castle's text 820 + n.
constexpr std::size_t BloodCastleCountdownBase = 820;
// 0x40bb6b..0x40bb77: Chaos Castle's 813 + n, with 825 replaced by 828.
constexpr std::size_t ChaosCastleCountdownBase = 0x32d;
constexpr std::size_t ChaosCastleSkippedText = 0x339;
constexpr std::size_t ChaosCastleReplacementText = 0x33c;
// 0x40bba5: 0x76ebeb0 + 300 · n, the others' 639 + n.
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
    // ----- What the handlers share.

    // Where the original reads or writes outside its tables and goes on: what came before stays done, with its rand(),
    // and the rest of the packet is not applied. Not a session error: it goes to EventPacketResult::discarded (Q-C174,
    // Q-C778).
    Applied Discard(std::string reason)
    {
        m_result.discarded = std::move(reason);
        return {};
    }

    [[nodiscard]] Core::Expected<std::string_view, std::string> Text(std::size_t index, std::string_view where) const
    {
        const std::optional<std::string_view> text = Ui::TextAt(m_context.texts, index);
        if (!text)
            return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
        return *text;
    }

    [[nodiscard]] Core::Expected<Crt::FormatArgument, std::string> TextArgument(std::size_t index,
                                                                                std::string_view where) const
    {
        auto text = Text(index, where);
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        return Crt::FormatArgument{Crt::TextArgument{.index = index, .text = text.Value()}};
    }

    // `function` with Text.bmd format `format` into a buffer of `bufferBytes`: what does not fit with its '\0' would
    // overwrite what follows, and it stops.
    [[nodiscard]] Core::Expected<std::string, std::string> Format(Crt::FormatFunction function, std::string_view format,
                                                                  std::span<const Crt::FormatArgument> arguments,
                                                                  std::size_t bufferBytes, std::string_view where) const
    {
        auto formatted = Crt::FormatOriginal(function, format, arguments, where);
        if (!formatted.HasValue())
            return Core::Unexpected(std::move(formatted).Error());
        if (formatted.Value().size() >= bufferBytes)
            return Core::Unexpected(
                std::format("{}: \"{}\" formateado ocupa {} bytes con su '\\0' y el buffer tiene {}", where, format,
                            formatted.Value().size() + 1, bufferBytes));
        return formatted;
    }

    [[nodiscard]] Core::Expected<std::string, std::string> FormatText(Crt::FormatFunction function, std::size_t format,
                                                                      std::span<const Crt::FormatArgument> arguments,
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
        Ui::OpenMessageBox(m_runtime.dialogBox, m_runtime.dialogs, text);
    }

    // 0x632e30 with a Text.bmd record.
    Applied MessageBoxOf(std::size_t index, std::string_view where)
    {
        auto text = Text(index, where);
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        MessageBox(text.Value());
        return {};
    }

    // sprintf or wsprintfA of a Text.bmd format and 0x632e30 with the result.
    Applied FormattedMessageBox(Crt::FormatFunction function, std::size_t format,
                                std::span<const Crt::FormatArgument> arguments, std::size_t bufferBytes,
                                std::string_view where)
    {
        auto text = FormatText(function, format, arguments, bufferBytes, where);
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        MessageBox(text.Value());
        return {};
    }

    // 0x555190(name, text, 1) with one of the empty .bss names (0x5767a54..0x5767a6c).
    Applied Message(std::string_view text, std::string_view where)
    {
        if (m_context.measure == nullptr)
            return Core::Unexpected(std::format("{}: sin el medidor de los mensajes (0x555190)", where));
        return Chat::AddGameMessage(m_runtime, *m_context.measure, m_context.screen, std::string_view{}, text,
                                    Ui::SystemMessage);
    }

    // 0x657a90 sobre [0x576352c].
    Applied CloseAllPanels()
    {
        Hud::GamePanels panels(m_world, m_context.random);
        panels.LendTheSession(m_world, m_context.random, m_context.send, m_context.sendWindowClose);
        panels.LendTheCastleRequests(m_world, m_context.send,
                                     CastleSiege::CastleWindowServices{.measure = m_context.measure,
                                                                       .texts = m_context.texts,
                                                                       .screen = m_context.screen,
                                                                       .now = m_context.tickCount});
        // 0x657a90 also reaches the warehouse, the chaos box, the trainer and the NPC shop (0x657ab0).
        panels.LendTheNpcWindows(m_world, m_context.random,
                                 m_context.inventory != nullptr ? *m_context.inventory : Items::InventoryServices{},
                                 m_context.sendNpcWindow);
        return panels.Registry().CloseAll();
    }

    // 0x41ed10: the native call's third argument asks for the loop.
    void Play(Audio::SoundId sound, bool loop = false)
    {
        m_result.sounds.push_back(sound);
        m_runtime.RequestSound(sound, std::nullopt, loop);
    }

    // 0x41ee00: the events stop the track and rewind it.
    void Stop(Audio::SoundId sound)
    {
        m_result.stoppedSounds.push_back(sound);
        m_runtime.StopSound(sound, true);
    }

    // 0x5ff930(map, type, count, delay): the FILD of the delay stores it as a float.
    void RequestTheCollapse(std::int32_t map, std::int32_t type, std::int32_t counter, std::int32_t delay)
    {
        m_runtime.events.objectCollapse =
            ObjectCollapse{.map = map, .type = type, .counter = counter, .delay = static_cast<float>(delay)};
    }

    // 0x5f8710 (OR) and 0x5f8730 (AND with the complement) on the WORD (y << 8) + x of 0x7d0d348.
    Applied MarkTheCell(std::int32_t x, std::int32_t y, std::uint8_t attribute, bool add, std::string_view where)
    {
        return Terrain::MarkTheTerrainCell(Terrain::TerrainOrEmpty(m_context.terrain).attributes.get(), x, y, attribute,
                                           add, where);
    }

    // 0x5f8750(x, y, width, height, attribute, add).
    Applied MarkTheArea(const Terrain::TerrainArea& area, std::uint8_t attribute, bool add, std::string_view where)
    {
        return Terrain::MarkTheTerrainArea(Terrain::TerrainOrEmpty(m_context.terrain).attributes.get(), area, attribute,
                                           add, where);
    }

    // 0x5111a0(action): each live one among the first 400 that is a player (0x20a), with the angles at (0, 0, 180) and
    // the action by 0x4ea030 with the Player.bmd skeleton.
    Applied TurnThePlayersToTheGate(std::string_view where)
    {
        if (m_context.playerSkeleton == nullptr)
            return Core::Unexpected(std::format("{}: 0x5111a0 pone la acción 0xc0 con el esqueleto de Player.bmd, "
                                                "que no está cargado",
                                                where));
        const auto actors = m_world.Actors();
        for (std::size_t index = 0; index < ActorsSearched; ++index)
        {
            Kernel::ActorState& actor = actors[index];
            if (!actor.alive || actor.type != Tables::PlayerSkeletonModel)
                continue;
            actor.angles = {0.0f, 0.0f, GateAngle};
            Models::SetAction(actor.animation, *m_context.playerSkeleton, GateAction);
        }
        return {};
    }

    // 0x511150(key): +0x360 at 0 in the first 400, and the last live one with the key (MOVSX against the key without
    // the high bit), or 400 if none (Q-C1326).
    std::size_t ClearTheWeaponsAndFind(std::uint16_t key)
    {
        const auto actors = m_world.Actors();
        std::size_t found = ActorsSearched;
        for (std::size_t index = 0; index < ActorsSearched; ++index)
        {
            Kernel::ActorState& actor = actors[index];
            if (actor.alive && static_cast<std::int32_t>(static_cast<std::int16_t>(actor.key)) == key)
                found = index;
            actor.byte360 = 0;
        }
        return found;
    }

    // 0x40ba80(state, 900, pkt[4], pkt[6], pkt[8]) and, with an owner and a weapon, the weapon in the owner's +0x360
    // (0x40c1a7..0x40c1fa; Chaos Castle ignores it).
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

    // The buttons of 0x7d368b8 zeroed and the first with `button`.
    void SetTheResultButtons(const Ui::DialogButton& button)
    {
        m_runtime.dialogBox.buttons = {};
        m_runtime.dialogBox.buttons[0] = button;
    }

    // The rep movs of `count` 24-byte records from pkt[5] to +0x2c. With fewer bytes in the packet (the original reads
    // the receive buffer) or more than 11 records (overwrites the heap, Q-C1324) it does not copy, drops the rest of
    // the packet and returns false.
    [[nodiscard]] bool CopyTheResults(EventMapObject& event, const Events::EventResult& packet, std::size_t count,
                                      std::string_view where)
    {
        const std::size_t bytes = count * ResultRecordBytes;
        if (bytes > packet.scores.size())
        {
            m_result.discarded = std::format("{}: copia {} bytes desde pkt[5] y el paquete trae {}: el resto lo "
                                             "leería del buffer de la recepción",
                                             where, bytes, packet.scores.size());
            return false;
        }
        if (bytes > event.field2c.size() * sizeof(std::int32_t))
        {
            m_result.discarded = std::format("{}: {} registros de 24 bytes pasan los 0x108 de +0x2c y pisan el heap "
                                             "después del objeto de 0x134 bytes (Q-C1324)",
                                             where, count);
            return false;
        }
        for (std::size_t at = 0; at < bytes; ++at)
        {
            const std::size_t word = at / sizeof(std::int32_t);
            const std::size_t shift = (at % sizeof(std::int32_t)) * 8;
            const auto byte = static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(packet.scores[at]));
            auto value = static_cast<std::uint32_t>(event.field2c[word]);
            value = (value & ~(0xffu << shift)) | (byte << shift);
            event.field2c[word] = static_cast<std::int32_t>(value);
        }
        return true;
    }

    // ----- 0B (0x4e3594).

    // 0x4e3621..0x4e3663, after the 0x77f27b4 queue (the session keeps it): the storm (0x723c50) with pkt[4] at 1 or 3,
    // and always the 40 birds off (0x4455c0).
    Applied On(const Events::EventState& packet)
    {
        if (packet.event == StormEvent)
            m_runtime.storm = packet.state != 0 ? Storm : 0;
        else if (packet.event == StrongStormEvent)
            m_runtime.storm = packet.state != 0 ? StrongStorm : 0;
        for (Fauna::FaunaObject& bird : m_world.Fauna().Birds())
            bird.active = 0;
        return {};
    }

    // ----- 46 (0x4dab90).

    Applied On(const Events::MapAttributes& packet)
    {
        if (packet.type == CellAttributes)
        {
            // 0x4dabba..0x4dabf5: each cell with 0x5f8730 if pkt[5] is not 0 and with 0x5f8710 if it is 0.
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
        // 0x4dabfc..0x4dac18: in Blood Castle, attribute 8 requests the collapse.
        const std::int32_t map = m_runtime.map;
        if (map >= FirstBloodCastleMap && map <= LastBloodCastleMap && packet.attribute == BloodCastleGateAttribute)
            RequestTheCollapse(map, BloodCastleCollapseType, 0, CollapseDelay);
        // 0x4dac2c..0x4dac53: 0x5f8750(x1, y1, x2 − x1 + 1, y2 − y1 + 1, attribute, 1 − pkt[5]) with the subtraction's
        // byte: pkt[5] at 0 adds and at 1 removes, but at 2 (0xff) it also adds (Q-C1320).
        const bool add = static_cast<std::uint8_t>(1 - packet.mode) != 0;
        for (const Events::AttributeArea& area : packet.areas)
        {
            const Terrain::TerrainArea rectangle{
                .x = area.x1, .y = area.y1, .width = area.x2 - area.x1 + 1, .height = area.y2 - area.y1 + 1};
            if (auto marked = MarkTheArea(rectangle, packet.attribute, add, "0x4dac53"); !marked.HasValue())
                return marked;
        }
        return {};
    }

    // ----- 47 (0x4d6990).

    Applied On(const Events::ItemAcquired& packet)
    {
        // 0x4d69ad..0x4d69d1: with the hero's key, nothing.
        const std::size_t owner = Kernel::ActorWithKey(m_world, packet.key);
        if (owner == m_runtime.hero)
            return {};
        // 0x4d6a09..0x4d6a3e: the prefix, with sprintf into the first buffer.
        std::string line;
        const std::optional<std::size_t> prefix = (packet.item & FirstPrefixFlag) != 0 ? FirstPrefixText
                                                  : (packet.item & SecondPrefixFlag) != 0
                                                      ? SecondPrefixText
                                                      : std::optional<std::size_t>{};
        if (prefix)
        {
            auto argument = TextArgument(*prefix, "0x4d6a3e");
            if (!argument.HasValue())
                return Core::Unexpected(std::move(argument).Error());
            const std::array<Crt::FormatArgument, 1> arguments{argument.Value()};
            auto formatted =
                Format(Crt::FormatFunction::Sprintf, ItemPrefixFormat, arguments, ItemNoticeBuffer, "0x4d6a3e");
            if (!formatted.HasValue())
                return Core::Unexpected(std::move(formatted).Error());
            line = std::move(formatted).Value();
        }
        // 0x4d6a54: 0x5b1c70(type, pkt[0xc]) into the second buffer, zeroed; where it writes nothing it stays empty.
        auto name = Items::ItemNameOf(m_context.items, m_context.texts, &m_context.world.Runtime().npcNames,
                                      static_cast<std::uint16_t>(packet.item & ItemTypeMask), packet.level);
        if (!name.HasValue())
            return Core::Unexpected(std::format("0x4d6a54: {}", name.Error()));
        if (name.Value().size() >= ItemNoticeBuffer)
            return Core::Unexpected(std::format("0x4d6a54: el nombre del ítem ocupa {} bytes con su '\\0' y el "
                                                "buffer tiene {}",
                                                name.Value().size() + 1, ItemNoticeBuffer));
        // 0x4d6a59..0x4d6b1f: the strcat calls on the first buffer.
        line += name.Value();
        for (const ItemSuffix& suffix : ItemSuffixes)
        {
            if ((packet.item & suffix.flag) == 0)
                continue;
            auto text = Text(suffix.text, "0x4d6a92");
            if (!text.HasValue())
                return Core::Unexpected(std::move(text).Error());
            line += text.Value();
        }
        if (line.size() >= ItemNoticeBuffer)
            return Core::Unexpected(std::format("0x4d6b1f: el strcat ocupa {} bytes con su '\\0' y el buffer tiene {}",
                                                line.size() + 1, ItemNoticeBuffer));
        // 0x4d6b21..0x4d6b4d: sprintf(second buffer, "%s %s", the first, 918) and 0x555190(the owner's +0x1d9, text,
        // 1).
        auto acquired = TextArgument(ItemAcquiredText, "0x4d6b36");
        if (!acquired.HasValue())
            return Core::Unexpected(std::move(acquired).Error());
        const std::array<Crt::FormatArgument, 2> arguments{Crt::StringArgument{.text = line}, acquired.Value()};
        auto text = Format(Crt::FormatFunction::Sprintf, ItemNoticeFormat, arguments, ItemNoticeBuffer, "0x4d6b36");
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        if (m_context.measure == nullptr)
            return Core::Unexpected(std::string("0x4d6b4d: sin el medidor de los mensajes (0x555190)"));
        return Chat::AddGameMessage(m_runtime, *m_context.measure, m_context.screen,
                                    Actors::ChatNameOf(m_world.Actors()[owner]), text.Value(), Ui::SystemMessage);
    }

    // ----- 90 (0x4e4870).

    Applied On(const Events::DevilSquareEntryResult& packet)
    {
        // 0x4e487d: 0x65cae0(0x1000, 0).
        Hud::GamePanels panels(m_world, m_context.random);
        panels.LendTheWindowClose(m_context.sendWindowClose);
        if (auto closed = panels.Registry().Close(Ui::EventNpcPanel); !closed.HasValue())
            return Core::Unexpected(std::move(closed).Error());
        if (packet.result < 1 || packet.result > DevilSquareEntryTexts.size())
            return {};
        return MessageBoxOf(DevilSquareEntryTexts[packet.result - 1U], "0x4e4894");
    }

    // ----- 91 (0x4da8c0), with wsprintfA ([0x6b851c], P-C290).

    Applied On(const Events::EventOpenTime& packet)
    {
        constexpr auto Wsprintf = Crt::FormatFunction::Wsprintf;
        if (packet.kind == DevilSquareOpenTime)
        {
            if (packet.value4 == 0)
                return MessageBoxOf(DevilSquareOpenText, "0x4da8dc");
            const std::array<Crt::FormatArgument, 1> arguments{std::int32_t{packet.value4}};
            return FormattedMessageBox(Wsprintf, DevilSquareMinutesText, arguments, PageStackBuffer, "0x4da901");
        }
        if (packet.kind == BloodCastleOpenTime)
        {
            auto name = TextArgument(BloodCastleName, "0x4da923");
            if (!name.HasValue())
                return Core::Unexpected(std::move(name).Error());
            if (packet.value4 == 0)
            {
                const std::array<Crt::FormatArgument, 1> arguments{name.Value()};
                return FormattedMessageBox(Wsprintf, CanEnterNowText, arguments, PageStackBuffer, "0x4da938");
            }
            const std::array<Crt::FormatArgument, 2> arguments{std::int32_t{packet.value4}, name.Value()};
            return FormattedMessageBox(Wsprintf, CanEnterInText, arguments, PageStackBuffer, "0x4da968");
        }
        if (packet.kind == ChaosCastleOpenTime)
            return OpenTheChaosCastleTime(packet);
        return {};
    }

    // 0x4da98e..0x4daa75: the word pkt[4]:pkt[6]. The texts go to Text.bmd records 1154 and 1155 in memory (300 bytes
    // each) and from there 0x633300 copies them to the 0x9a box; MuRet passes them directly (only reader, 0x633300 with
    // 0x482).
    Applied OpenTheChaosCastleTime(const Events::EventOpenTime& packet)
    {
        constexpr auto Wsprintf = Crt::FormatFunction::Wsprintf;
        const auto remaining = static_cast<std::int32_t>((packet.value4 << 8u) | packet.value6);
        auto name = TextArgument(ChaosCastleName, "0x4da9a3");
        if (!name.HasValue())
            return Core::Unexpected(std::move(name).Error());
        if (remaining == 0)
        {
            const std::array<Crt::FormatArgument, 1> nowArguments{name.Value()};
            auto first = FormatText(Wsprintf, CanEnterNowText, nowArguments, Crt::TextRecordBytes, "0x4da9b2");
            if (!first.HasValue())
                return Core::Unexpected(std::move(first).Error());
            const std::array<Crt::FormatArgument, 3> openArguments{name.Value(), std::int32_t{packet.value5},
                                                                   ChaosCastleOpenArgument};
            auto second = FormatText(Wsprintf, ChaosCastleOpenText, openArguments, Crt::TextRecordBytes, "0x4da9ce");
            if (!second.HasValue())
                return Core::Unexpected(std::move(second).Error());
            const std::array<std::string_view, 2> texts{first.Value(), second.Value()};
            return Ui::OpenTheChaosCastleDialog(m_runtime.dialogBox, m_runtime.dialogs, texts);
        }
        const std::int32_t hours = remaining / MinutesPerHour;
        const std::int32_t minutes = remaining - hours * MinutesPerHour;
        const std::array<Crt::FormatArgument, 1> hourArguments{hours};
        auto first = FormatText(Wsprintf, ChaosCastleHoursText, hourArguments, Crt::TextRecordBytes, "0x4daa17");
        if (!first.HasValue())
            return Core::Unexpected(std::move(first).Error());
        const std::array<Crt::FormatArgument, 2> minuteArguments{minutes, name.Value()};
        auto second = FormatText(Wsprintf, CanEnterInText, minuteArguments, PageStackBuffer, "0x4daa39");
        if (!second.HasValue())
            return Core::Unexpected(std::move(second).Error());
        // 0x4daa3b..0x4daa73: the strcat of the second onto record 1154. If it does not fit with its '\0', the original
        // overwrites record 1155 and opens 0x9a anyway: inside the handler the rest of the packet is dropped, without
        // 0x9a, and the session goes on with the notice (Q-C1109; the texts do not go to the log, only their length).
        std::string joined = std::move(first).Value() + second.Value();
        if (joined.size() >= Crt::TextRecordBytes)
            return Discard(std::format("0x4daa73: el strcat ocupa {} bytes con su '\\0' y pisa el registro 1155 de "
                                       "Text.bmd (Q-C1109)",
                                       joined.size() + 1));
        const std::array<std::string_view, 1> texts{joined};
        return Ui::OpenTheChaosCastleDialog(m_runtime.dialogBox, m_runtime.dialogs, texts);
    }

    // ----- 92 (0x4e4930): with the object, 0x40ba60(pkt[3] + 1): +8 and GetTickCount in +0xc.

    Applied On(const Events::EventCountdown& packet)
    {
        if (!m_runtime.eventObject)
            return {};
        m_runtime.eventObject->field08 = std::int32_t{packet.type} + 1;
        m_runtime.eventObject->field0c = static_cast<std::int32_t>(m_context.tickCount);
        return {};
    }

    // ----- 93 (0x4e4959 → 0x4e7360 → the object's method +0xc with (pkt[4], pkt[3], pkt + 5, pkt[3])).

    Applied On(const Events::EventResult& packet)
    {
        if (!m_runtime.eventObject)
            return {};
        EventMapObject& event = *m_runtime.eventObject;
        switch (event.kind)
        {
        case EventMapKind::DevilSquare:
        {
            // 0x40bd20: with pkt[4] < 200, the ranking of pkt[4] records.
            if (std::int32_t{packet.count} >= DevilSquareResultLimit)
                return {};
            SetTheResultButtons(DevilSquareResultButton);
            event.field24 = packet.count;
            event.field28 = packet.rank;
            if (!CopyTheResults(event, packet, packet.count, "0x40bd97"))
                return {};
            Ui::PushDialog(m_runtime.dialogs, DevilSquareResultDialog);
            return {};
        }
        case EventMapKind::BloodCastle:
            // 0x40c0c0: only with pkt[4] at 0xff, one record and +0x24 = pkt[3].
            return ShowTheCastleResult(event, packet, BloodCastleResultCount, BloodCastleResultDialog, "0x40c12a");
        case EventMapKind::ChaosCastle:
            // 0x404da0: same with 0xfe.
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
        if (!CopyTheResults(event, packet, 1, where))
            return {};
        Ui::PushDialog(m_runtime.dialogs, dialog);
        return {};
    }

    // ----- 94 (0x4dd8a0).

    Applied On(const Events::EventChipInfo& packet)
    {
        if (auto closed = CloseAllPanels(); !closed.HasValue())
            return closed;
        Ui::PanelState& panels = m_runtime.panels;
        panels.field78abb04 = static_cast<std::int16_t>(packet.count);
        panels.field78abb00 = std::int32_t{packet.type} + 1;
        if (panels.field78abb00 == SerialNumberWindow)
        {
            // 0x4dd8ce..0x4dd920.
            OpenTheSerialEntry(m_runtime);
            return {};
        }
        if (panels.field78abb00 != MutoWindow)
            return {};
        // 0x4dd926..0x4dd949: the three numbers if none is 0xffff. Without them, the original reads them from the
        // receive buffer: they are dropped, after the close (with its rand()) and 0x78abb00/04.
        if (!packet.muto)
            return Discard("0x4dd92c: el 94 con el tipo 0 lee pkt[6..0xb], que el paquete no trae");
        const auto& numbers = *packet.muto;
        if (numbers[0] == NoMutoNumber || numbers[1] == NoMutoNumber || numbers[2] == NoMutoNumber)
            return {};
        m_runtime.events.mutoNumbers = numbers;
        return {};
    }

    // ----- 95 (0x4e49d4): the unsigned word in 0x78abb04, except 0xffff.

    Applied On(const Events::EventChipCount& packet)
    {
        if (packet.count != NoChipCount)
            m_runtime.panels.field78abb04 = packet.count;
        return {};
    }

    // ----- 96 (0x4e49fb): the three numbers if none is 0xffff.

    Applied On(const Events::MutoNumbers& packet)
    {
        const auto& numbers = packet.numbers;
        if (numbers[0] == NoMutoNumber || numbers[1] == NoMutoNumber || numbers[2] == NoMutoNumber)
            return {};
        m_runtime.events.mutoNumbers = numbers;
        return {};
    }

    // ----- 99 (0x4e4a40): 0x6236f0 with 0x8f or 0x90.

    Applied On(const Events::EventDialog& packet)
    {
        if (packet.kind < EventDialogs.size())
            Ui::PushDialog(m_runtime.dialogs, EventDialogs[packet.kind]);
        return {};
    }

    // ----- 9A (0x4da780), with sprintf (0x69fd9b).

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

    // sprintf(buffer, 852, name): "% s" with the space flag, which %s does not use.
    Applied EntryOverBox(std::size_t name, std::string_view where)
    {
        auto argument = TextArgument(name, where);
        if (!argument.HasValue())
            return Core::Unexpected(std::move(argument).Error());
        const std::array<Crt::FormatArgument, 1> arguments{argument.Value()};
        return FormattedMessageBox(Crt::FormatFunction::Sprintf, EntryOverText, arguments, SmallStackBuffer, where);
    }

    // sprintf(buffer, 853, name, number): the "% d" takes the pointer to the name and prints its address (Q-C1321).
    Applied CapacityBox(std::size_t name, std::int32_t capacity, std::string_view where)
    {
        auto argument = TextArgument(name, where);
        if (!argument.HasValue())
            return Core::Unexpected(std::move(argument).Error());
        const std::array<Crt::FormatArgument, 2> arguments{argument.Value(), capacity};
        return FormattedMessageBox(Crt::FormatFunction::Sprintf, CapacityText, arguments, SmallStackBuffer, where);
    }

    Applied TimesPerDayBox(std::string_view where)
    {
        const std::array<Crt::FormatArgument, 1> arguments{TimesPerDay};
        return FormattedMessageBox(Crt::FormatFunction::Sprintf, TimesPerDayText, arguments, LargeStackBuffer, where);
    }

    // ----- 9B (0x4e4999): with the object, its method +8.

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

    // 0x40c140, by pkt[3] from 0 to 4 (table 0x40c2b0).
    Applied PlayBloodCastle(EventMapObject& event, const Events::EventPlayState& packet)
    {
        switch (packet.state)
        {
        case 0:
            // 0x40c165..0x40c178, and falls through to case 1.
            if (auto turned = TurnThePlayersToTheGate("0x40c16a"); !turned.HasValue())
                return turned;
            Play(BloodCastleMusic, true);
            [[fallthrough]];
        case 1:
        case 4:
            // 0x40c180..0x40c1fa and 0x40c23c..0x40c2a1: +0x10 = pkt[3] + 1.
            SetThePlayState(event, static_cast<std::uint8_t>(packet.state + 1), packet);
            GiveTheWeapon(packet);
            return {};
        case 2:
            // 0x40c205: 0x40ba20 and the music off.
            ResetThePlayState(event);
            Stop(BloodCastleMusic);
            return {};
        case 3:
            // 0x40c221: the gate's collapse.
            RequestTheCollapse(m_runtime.map, BloodCastleCollapseType, BloodCastleCollapseCounter, CollapseDelay);
            return {};
        default:
            return {};
        }
    }

    // 0x40ba20: +0x10 at 0 and +0x14..+0x20 at −1.
    static void ResetThePlayState(EventMapObject& event)
    {
        event.field10 = 0;
        event.field14 = {-1, -1, -1, -1};
    }

    // 0x404e20, by pkt[3] from 5 to 10 (table 0x405070).
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
                Ui::ShowTheChatFields(m_runtime.chat.input, HideTheChatFields);
            }
            if (auto turned = TurnThePlayersToTheGate("0x404e7f"); !turned.HasValue())
                return turned;
            Stop(ChaosCastleWaitMusic);
            Play(ChaosCastleMusic, true);
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
                Play(ChaosCastleWaitMusic, true);
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

    // 0x404f1c, 0x404f88 and 0x404ff2: with the expected area, the next one, the collapse and four rectangles with
    // attribute 8 (0x5f8750 with add at 1) and sound 0x61.
    Applied CollapseChaosCastle(std::size_t step)
    {
        if (m_runtime.chaosCastleArea != ChaosCastleCollapseAreas[step])
            return {};
        m_runtime.chaosCastleArea = static_cast<std::uint8_t>(ChaosCastleCollapseAreas[step] + 1);
        RequestTheCollapse(m_runtime.map, ChaosCastleCollapseType, ChaosCastleCollapseCounter, CollapseDelay);
        for (const Terrain::TerrainArea& area : ChaosCastleCollapses[step])
        {
            if (auto marked = MarkTheArea(area, ChaosCastleCollapseAttribute, true, "0x405056"); !marked.HasValue())
                return marked;
        }
        Play(ChaosCastleCollapseSound);
        return {};
    }

    // ----- 9D (0x4e4a86): the notice and the 0x40 bytes in 0x78ab078, with any result.

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

    // ----- AF 01 (0x4e5032 → 0x4daa90), by pkt[4] from 1 to 7 (table 0x4dab6c).

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

    // ----- BC 00 (0x4e4547) and BC 01 (0x4da540).

    Applied On(const Events::JewelMixResult& packet)
    {
        if (packet.subcode == Protocol::JewelMix::MixSubcode)
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

    // sprintf(0x100 buffer, "%s%s %s", title, 1816, 868), the message and 0x402e50.
    Applied JewelMessage(std::size_t title, std::string_view where)
    {
        std::array<Crt::FormatArgument, 3> arguments{};
        const std::array<std::size_t, 3> texts{title, JewelToText, JewelTimesText};
        for (std::size_t index = 0; index < texts.size(); ++index)
        {
            auto argument = TextArgument(texts[index], where);
            if (!argument.HasValue())
                return Core::Unexpected(std::move(argument).Error());
            arguments[index] = argument.Value();
        }
        auto text = Format(Crt::FormatFunction::Sprintf, JewelFormat, arguments, PageStackBuffer, where);
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

    // 0x4e45d9 and 0x4da5af: 0x6236f0(0) and 0x6236f0(0xa6).
    Applied OpenTheJewelDialog()
    {
        Ui::PushDialog(m_runtime.dialogs, 0);
        Ui::PushDialog(m_runtime.dialogs, JewelDialog);
        return {};
    }

    // 0x402e50 (ChaosMix/JewelMix.h): back to choosing the jewel; with another mode, 0x402eb0 closes everything and
    // sends C1 03 31 in the clear through the world packets' send(). Without it, that send stops.
    Applied CloseTheJewelWindow()
    {
        return ChaosMix::ReturnToTheJewelChoice(m_world,
                                                [this](Protocol::Outgoing packet) -> Core::Expected<void, std::string>
                                                {
                                                    if (!m_context.send)
                                                        return Core::Unexpected(
                                                            std::string("0x402f91: el C1 03 31 sin la sesión"));
                                                    m_context.send(std::move(packet));
                                                    return {};
                                                });
    }

    // ----- BD 00 (0x4e5965): 0x42fe80(pkt[4], pkt[5]) and 0x4033b0(pkt[5]) on [0x6e5904].

    Applied On(const Events::CrywolfState& packet)
    {
        // With a state above 2 (Q-C1328) the rest is dropped, 0x4033b0 too.
        if (auto changed = ChangeTheCrywolfState(packet.state, packet.stage); !changed.HasValue() || m_result.discarded)
            return changed;
        // 0x4033b0: +0x154 of the camera tour, which starts with 2 (Events/Crywolf/CameraTour.h).
        m_runtime.events.cameraTour.stage = packet.stage;
        return {};
    }

    // 0x42fe80(estado, etapa).
    Applied ChangeTheCrywolfState(std::uint8_t state, std::uint8_t stage)
    {
        Kernel::CrywolfGlobals& crywolf = m_runtime.crywolf;
        Kernel::CrywolfInterface& ui = crywolf.ui;
        if (crywolf.state == state && crywolf.stage == stage)
            return {};
        crywolf.stage = stage;
        if (stage == Events::CrywolfStartStage)
            ui.byte723bec = 1;
        else
        {
            ui.byte723a3d = 0;
            if (stage == Events::CrywolfEndStage)
            {
                // 0x42febf..0x42fef5: 0x6c73e4..0x6c73f4 at −1 and, except with state 2, the notice.
                for (std::size_t index = 1; index < ui.dwords6c73e0.size(); ++index)
                    ui.dwords6c73e0[index] = -1;
                if (state != Events::CrywolfWarState)
                {
                    ui.message = 1;
                    if (state == Events::CrywolfPeaceState)
                        ui.dword6c73dc = CrywolfPeaceCounter;
                    ui.dword6c73f8 = CrywolfFightTime;
                }
            }
            else if (stage == Events::CrywolfEndCycleStage)
                ResetTheCrywolfInterface(m_runtime);
        }
        if (crywolf.state == state)
            return {};
        crywolf.state = state;
        if (state > LastLitCrywolfState)
            return Discard(std::format("0x42ff81: con el estado {} de Crywolf, 0x5f8f40 y 0x5f8390 leen el buffer de "
                                       "la pila sin escribir (Q-C1328)",
                                       static_cast<int>(state)));
        return ReloadTheCrywolfTerrain(state);
    }

    // 0x42ff16..0x42ffe8: the light and the .att of the current map, whatever it is (Q-C1340), with the map's "World%d"
    // folder + 1 (0x6c7330, without the guard of 0x61a560): TerrainLight, TerrainLight1 or TerrainLight2 by the state
    // (0x6c72e8, 0x6c73fc and 0x6c72fc) and EncTerrain<map + 1>.att, <map · 10 + 0xb> or <map · 10 + 0xc> (0x6c7314).
    // Neither checks what the loader returns: a missing file leaves the fatal notice and what was there before.
    Applied ReloadTheCrywolfTerrain(std::uint8_t state)
    {
        if (m_context.dataRoot.empty())
            return Core::Unexpected(std::format("0x42ff81: la luz y el .att de World{} con el estado {} de Crywolf "
                                                "se leen de la carpeta Data, que no llegó",
                                                m_runtime.map + 1, static_cast<int>(state)));
        static constexpr std::array<std::string_view, 3> LightNames{"TerrainLight", "TerrainLight1", "TerrainLight2"};
        const std::int32_t map = m_runtime.map;
        const std::string folder = std::format("World{}", map + 1);
        if (m_context.terrain == nullptr)
            return Core::Unexpected(std::format("0x42ff81: no hay terreno para la luz y el .att de World{}", map + 1));
        Terrain::WorldTerrain& terrain = *m_context.terrain;
        std::optional<std::string>& fatal = m_runtime.fatalDataMessage;
        if (auto light = Terrain::ReloadWorldTerrainLight(m_context.dataRoot, folder, LightNames[state],
                                                          static_cast<std::uint8_t>(map), terrain, fatal);
            !light.HasValue())
            return light;
        const std::int32_t number = state == Events::CrywolfPeaceState ? map + 1 : map * 10 + 0xa + state;
        return Terrain::ReloadWorldTerrainAttributes(m_context.dataRoot, folder, number, terrain, fatal);
    }

    // ----- BD 02 (0x4e5990 → 0x4e0fe0 → 0x430000): the light and the five altars, in any map and stage.

    Applied On(const Events::CrywolfAltars& packet)
    {
        Kernel::CrywolfInterface& ui = m_runtime.crywolf.ui;
        ui.light = packet.light;
        ui.altars = packet.altars;
        return {};
    }

    // ----- BD 03 (0x4e59a5 → 0x4e1010): the contract result, in any map; 0x435b00 and 0x435ad0 check the stage and the
    // map.

    Applied On(const Events::CrywolfContract& packet)
    {
        constexpr std::uint32_t SittingFlag = 0x200000;
        Kernel::ActorState& hero = m_world.Hero();
        if (packet.result == ContractFailed)
        {
            // 0x4e1050..0x4e106e: the hero stands up.
            hero.flags7c &= ~SittingFlag;
            // 0x4e1071..0x4e1142: the level (+0xe) with the own record open.
            std::int32_t level = 0;
            {
                const Models::LogicalAccess own(m_runtime.ownRecordDepth, m_context.random,
                                                Models::OwnRecordCloseRandoms);
                level = m_world.OwnCharacter().level;
            }
            if (level >= ContractLevel)
                return Crywolf::ShowTheContractMessage(m_runtime, m_context.texts,
                                                       {.message = Crywolf::ContractLevelMessage});
            if (auto shown = Crywolf::ShowTheContractMessage(m_runtime, m_context.texts,
                                                             {.message = Crywolf::ContractFailedMessage});
                !shown.HasValue())
                return shown;
            return Crywolf::ShowTheContractMessage(m_runtime, m_context.texts,
                                                   {.message = Crywolf::ContractFailedSecondMessage, .line = 1});
        }
        if (packet.result != ContractSigned)
            return {};
        // 0x4e119b..0x4e11d9: the two lines, the key's altar, the hero seated and its action 0xca.
        if (auto shown = Crywolf::ShowTheContractMessage(m_runtime, m_context.texts,
                                                         {.message = Crywolf::ContractSignedMessage});
            !shown.HasValue())
            return shown;
        if (auto shown = Crywolf::ShowTheContractMessage(m_runtime, m_context.texts,
                                                         {.message = Crywolf::ContractSignedSecondMessage, .line = 1});
            !shown.HasValue())
            return shown;
        // 0x4e11b5..0x4e11c2: the key − 0x13c with no bound (Q-C1347).
        const std::int32_t altar = static_cast<std::int32_t>(packet.key) - FirstAltarKey;
        if (!Crywolf::SetTheAltar(m_runtime, altar, packet.value))
            return Discard(std::format("0x435aeb: el altar {} (la clave {:#x} − 0x13c) escribe fuera de "
                                       "0x6c73cc..0x6c73d0 (Q-C1347)",
                                       altar, packet.key));
        hero.flags7c |= SittingFlag;
        // 0x4ea036..0x4ea054: the actions of the model of the hero's type (+2), Player.bmd or the transformed one's.
        const auto* skeleton =
            m_context.models != nullptr ? m_context.models->SkeletonOf(Models::ModelId{hero.type}) : nullptr;
        if (skeleton == nullptr)
            return Core::Unexpected(std::format("0x4e11d9: 0x4ea030 sienta al héroe (0xca) con las acciones del modelo "
                                                "{:#x}, que no está cargado",
                                                hero.type));
        Models::SetAction(hero.animation, *skeleton, SittingAction);
        // 0x4e11e1..0x4e15bd: C1 05 18 with the hero's heading (+0x24) and 0x82.
        const auto direction = Protocol::Movement::DirectionFromAngle(hero.angles.z);
        if (!direction.HasValue())
            return Core::Unexpected(std::string("0x4e1275: el ángulo del héroe está fuera del perfil de ftol"));
        if (!m_context.send)
            return Core::Unexpected(std::string("0x4e11e1: el C1 05 18 del contrato sin el send() de la sesión"));
        m_context.send(Protocol::Movement::WriteAction(direction.Value(), Protocol::Movement::SittingAction));
        return {};
    }

    // ----- BD 0C (0x4e59d6 → 0x4e15f0 → 0x430040).

    Applied On(const Events::CrywolfUnported& packet)
    {
        if (packet.bytes.size() < 10)
            return Discard("0x4e15f0: BD0C lee pkt6..9 fuera del paquete");
        const auto selector = std::to_integer<std::uint8_t>(packet.bytes[6]);
        const auto x = std::to_integer<std::uint8_t>(packet.bytes[8]);
        const auto y = std::to_integer<std::uint8_t>(packet.bytes[9]);
        const std::int32_t subtype = selector == 0x7a || selector == 0x74 ? 2 : selector == 0xb7 ? 1 : 0;
        const glm::vec3 target{static_cast<float>(x) * 100.0f, static_cast<float>(y) * 100.0f, 100};
        glm::vec3 position = target;
        position.z = 800;
        if (subtype == 0)
            position.x -= 800;
        else if (subtype == 1)
            position.x += 800;
        else
            position.y += 800;
        static_cast<void>(m_world.Map().effects.CreateCrywolfMissile({.type = 0xee,
                                                                      .position = position,
                                                                      .angles = target,
                                                                      .light = m_world.Hero().light,
                                                                      .subtype = subtype,
                                                                      .owner = &m_world.Hero()}));
        return {};
    }

    // ----- BD 04 (0x4e59ba → 0x435a20): with stage 4 in 34, pkt[4], the minutes and GetTickCount.

    Applied On(const Events::CrywolfTimer& packet)
    {
        Kernel::CrywolfGlobals& crywolf = m_runtime.crywolf;
        if (crywolf.stage != Events::CrywolfStartStage || m_runtime.map != CrywolfMap)
            return {};
        crywolf.ui.dwords723bdc[2] = packet.value4;
        crywolf.ui.dwords723bdc[3] = packet.minutes;
        crywolf.ui.dword7239f4 = static_cast<std::int32_t>(m_context.tickCount);
        crywolf.ui.byte723bf8 = 1;
        return {};
    }

    // ----- BD 05 (0x4e59eb → 0x435a70): with stage 3 or 4 in 34, pkt[8] in 0x723bd8; the bar turns on with the dword
    // above 0 and off with 0 or less (0x435a96..0x435ab5), and the dword goes to 0x6c73d4 (0x436590).

    Applied On(const Events::CrywolfBoss& packet)
    {
        Kernel::CrywolfGlobals& crywolf = m_runtime.crywolf;
        if (!Crywolf::InTheCrywolfDefense(m_runtime))
            return {};
        Kernel::CrywolfInterface& ui = crywolf.ui;
        ui.dword723bd8 = packet.count;
        if (packet.life > 0)
        {
            if (ui.byte723a3d == 0)
                ui.byte723a3d = 1;
        }
        else if (ui.byte723a3d == 1)
            ui.byte723a3d = 0;
        ui.dword6c73d4 = packet.life;
        return {};
    }

    // ----- BD 07 (0x4e5a09 → 0x437600): the rank and the experience, in any map.

    Applied On(const Events::CrywolfRank& packet)
    {
        Kernel::CrywolfInterface& ui = m_runtime.crywolf.ui;
        ui.byte723bed = packet.rank;
        ui.dword723bf0 = packet.experience;
        return {};
    }

    // ----- BD 08 (0x4e5a39 → 0x4e1680 → 0x437620 per record): the place's points, class and name, and the results
    // window. The place is the record's byte with no bound: with 5 or more it writes outside the three tables
    // (Q-C1341), and MuRet drops from that record on; the earlier ones stay written.

    Applied On(const Events::CrywolfRanking& packet)
    {
        Kernel::CrywolfInterface& ui = m_runtime.crywolf.ui;
        for (const Events::CrywolfRankingEntry& entry : packet.entries)
        {
            if (entry.place >= CrywolfRankingPlaces)
                return Discard(std::format("0x437632: el lugar {} de los resultados de Crywolf escribe fuera de "
                                           "0x6c73e4, 0x723a30 y 0x7239f8 (Q-C1341)",
                                           static_cast<int>(entry.place)));
            const std::size_t place = entry.place;
            ui.dwords6c73e0[place + 1] = entry.score;
            ui.rankingClasses[place] = entry.classByte;
            std::copy(entry.name.begin(), entry.name.end(), ui.rankingNames[place].begin());
            ui.rankingNames[place][entry.name.size()] = '\0';
            ui.byte723bf9 = 1;
        }
        return {};
    }

    // ----- BD 09 (0x4e5a25): pkt[4] a 0x7d2dc9d.

    Applied On(const Events::CrywolfChaosBox& packet)
    {
        m_runtime.crywolf.ui.byte7d2dc9d = packet.value;
        return {};
    }

    // ----- F3 22 (0x4e31f8).

    Applied On(const Events::EventTime& packet)
    {
        m_runtime.events.timeKind = packet.value4;
        m_runtime.events.timeValue = packet.value6;
        return {};
    }

    Applied FireworkAt(std::int32_t cellX, std::int32_t cellY)
    {
        const auto& terrain = Terrain::TerrainOrEmpty(m_context.terrain);
        if (!terrain.heights || !terrain.attributes)
            return Core::Unexpected(std::string("0x4d9fb1/0x4da0e4: el fuego artificial sin el terreno"));
        glm::vec3 position{static_cast<float>((static_cast<double>(cellX) + 0.5) * 100.0),
                           static_cast<float>((static_cast<double>(cellY) + 0.5) * 100.0), 0};
        position.z =
            static_cast<float>(Terrain::TerrainHeightAt(*terrain.heights, *terrain.attributes, position.x, position.y));
        static_cast<void>(m_world.Map().effects.CreateFireworkEmitter(
            Kernel::ModelEffectArguments{.type = 0x60e, .position = position, .angles = {}, .light = glm::vec3(1)}));
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
        m_runtime.guild.ourScore = packet.firstScore;
        m_runtime.guild.theirScore = packet.secondScore;
        const Kernel::ActorState& hero = m_world.Hero();
        // 0x4d9f78/94: FILD directly, without opening the two protected entries.
        return FireworkAt(std::bit_cast<std::int32_t>(hero.cellXLeft), std::bit_cast<std::int32_t>(hero.cellYLeft));
    }

    Applied On(const Events::GuildWarOutcome& packet)
    {
        if (packet.present >= 3)
            return {};
        m_runtime.events.guildWarOutcome = {.present = packet.present,
                                            .firstName = packet.firstName,
                                            .secondName = packet.secondName,
                                            .firstScore = packet.firstScore,
                                            .secondScore = packet.secondScore};
        return {};
    }

    // ----- F3 40 (0x4da070), by pkt[4] from 0 to 0x31 (tables 0x4da24c and 0x4da224).

    Applied On(const Events::ServerCommand& packet)
    {
        const std::uint8_t command = packet.command;
        if (command >= FirstCommandText && command <= LastCommandText)
            return MessageBoxOf(CommandTextBase + command, "0x4da215");
        switch (command)
        {
        case CommandEffect:
            return FireworkAt(packet.value5, packet.value6);
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
            // 0x4da18f..0x4da195: 0x632f70(pkt[5]) with no bound; from 200 on it reads outside 0x7688ac0 (Q-C1325).
            if (packet.value5 >= Tables::DialogCount)
                return Discard(std::format("0x4da195: el diálogo {} de 0x632f70 cae fuera de los 200 de 0x7688ac0 "
                                           "(Q-C1325)",
                                           static_cast<int>(packet.value5)));
            return Ui::OpenTheAnswersDialog(m_runtime.dialogBox, m_runtime.dialogs, &m_runtime.dialogTable,
                                            m_context.texts, packet.value5);
        case CommandFixedNotice:
            return MessageBoxOf(FixedNoticeText, "0x4da1a6");
        case CommandCastleNotice:
            if (packet.value5 == 0)
                return Ui::OpenTextDialog(m_runtime.dialogBox, m_runtime.dialogs, m_context.texts, 1,
                                          static_cast<std::int32_t>(CastleNoticeBase), CastleNoticeDialog);
            return MessageBoxOf(CastleNoticeBase + packet.value5, "0x4da1e7");
        case CommandGuildNotice:
            return MessageBoxOf(GuildNoticeText, "0x4da1f8");
        default:
            return {};
        }
    }

    const EventPacketContext& m_context;
    Kernel::WorldState& m_world;
    Kernel::WorldRuntime& m_runtime;
    EventPacketResult m_result{};
};
} // namespace

Core::Expected<EventPacketResult, std::string> ApplyEventPacket(const Protocol::Events::EventPacket& packet,
                                                                const EventPacketContext& context)
{
    return Applier(context).Apply(packet);
}

Core::Expected<std::optional<std::string>, std::string>
EventCountdownText(EventMapObject& event, std::uint32_t tickCount, const Tables::TextTable* texts, bool write)
{
    const std::int32_t kind = event.field08;
    if (kind < FirstCountdown || kind > LastCountdown)
        return std::optional<std::string>{};
    // 0x40bad7..0x40baf1: the unsigned subtraction against +0xc.
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
        const std::optional<std::string_view> text = Ui::TextAt(texts, index);
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
    std::vector<Crt::FormatArgument> arguments;
    if (name)
    {
        auto nameText = textOf(*name);
        if (!nameText.HasValue())
            return Core::Unexpected(std::move(nameText).Error());
        arguments.emplace_back(Crt::TextArgument{.index = *name, .text = nameText.Value()});
    }
    arguments.emplace_back(left);
    auto text = Crt::FormatOriginal(Crt::FormatFunction::Wsprintf, formatText.Value(), arguments, "0x40bb56");
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    if (text.Value().size() >= PageStackBuffer)
        return Core::Unexpected(std::format("0x40bb56: la cuenta ocupa {} bytes con su '\\0' y el buffer tiene {}",
                                            text.Value().size() + 1, PageStackBuffer));
    return std::optional<std::string>{std::move(text).Value()};
}

void ResetTheCrywolfInterface(Kernel::WorldRuntime& runtime)
{
    Kernel::CrywolfInterface& ui = runtime.crywolf.ui;
    ui.float723a40 = 0.0f;
    ui.byte723ca0 = 0;
    ui.dwords723bdc = {};
    ui.dword7239f4 = -1;
    ui.dword6c73dc = 0x0a;
    // 0x42fdf8: the arrival's chosen NPC (0x6c8e54), which belongs to GameMovementState.
    runtime.chosenNpc.index = -1;
    ui.dword6c73d8 = 1;
    ui.byte723bec = 0;
    // 0x42fe0d: only the first of the six.
    ui.dwords6c73e0[0] = -1;
    ui.byte723a3d = 0;
    ui.message = -1;
    ui.byte723a44 = 0;
    ui.dword723bd8 = 0;
    ui.dword6c73d4 = 0x64;
    runtime.crywolf.state = Events::CrywolfPeaceState;
    runtime.crywolf.stage = Events::CrywolfNoStage;
    ui.light = 0;
    // 0x42fe41: 0x6c8e2c, the second of the chosen targets.
    runtime.selectedTargets[1] = -1;
}
} // namespace Client::Events
