#include "Chat/ChatReceive.h"

#include "Actors/ActorName.h"
#include "Kernel/ActorSearch.h"
#include "Core/Expected.h"
#include "Protocol/Chat/Messages.h"
#include "Protocol/Player/OptionData.h"
#include "Models/LogicalAccess.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Kernel/ActorState.h"
#include "Chat/ChatBubbles.h"
#include "Hud/GamePanels.h"
#include "CastleSiege/CastleWindows.h"
#include "Guild/GuildState.h"
#include "Guild/GuildWindow.h"
#include "Chat/GameMessages.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/SceneId.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatInput.h"
#include "Ui/GameText.h"
#include "Ui/MessageArea.h"
#include "Ui/PanelRegistry.h"
#include "Ui/SlideBanner.h"
#include "Chat/WhisperSenders.h"
#include "Sound/SoundIds.h"
#include "Audio/SoundId.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Chat
{
namespace
{
// 0x4c62ed..0x4c6326: las marcas de pkt[0xd].
constexpr char PartyMark = '~';
constexpr char GuildMark = '@';
constexpr char BubbleMark = '#';
// 0x4c62f6, 0x4c6334, 0x4c6352 y 0x4c6386: 0xe DWORD y 3 bytes (59), 0xe DWORD y 2 (58), o 0xf DWORD (60).
constexpr std::size_t MarkedTextBytes = 59;
constexpr std::size_t GuildNoticeTextBytes = 58;
// Text.bmd: el error del susurro (0x76e06b4) y el formato del aviso del gremio (0x76e07e0).
constexpr std::size_t WhisperFailedText = 482;
constexpr std::size_t GuildNoticeText = 483;
// 0x4e3550: el sonido del susurro con 0x77f3b78.
constexpr Audio::SoundId WhisperSound = Sound::WhisperSound;
// 0x4e341d: el héroe en +0x368 con 0x80; 0x6c8404: "길드 마스터" en EUC-KR, los 12 bytes que compara REPE CMPSB.
constexpr std::uint8_t GuildMasterStatus = 0x80;
constexpr std::array<std::uint8_t, 12> GuildMasterName{0xb1, 0xe6, 0xb5, 0xe5, 0x20, 0xb8,
                                                       0xb6, 0xbd, 0xba, 0xc5, 0xcd, 0x00};
// 0x4c65b0: los tipos del 0D.
constexpr std::uint8_t CenterNotice = 0;
constexpr std::uint8_t SystemNotice = 1;
constexpr std::uint8_t GuildNotice = 2;
constexpr std::uint8_t FirstSlideNotice = 10;
constexpr std::uint8_t LastSlideNotice = 15;
// 0x6b8a34: 0,1f, la velocidad del cartel por cada unidad de pkt[0xc].
constexpr float SlideSpeedStep = 0.1f;
// 0x4dd7c3..0x4dd88d: las banderas y los campos del F3 30.
constexpr std::uint8_t AutoAttackFlag = 0x01;
constexpr std::uint8_t NoticeSoundFlag = 0x04;
constexpr std::uint8_t SlidesOffFlag = 0x10;
constexpr std::int32_t QuickSlotBase = 0x1c00;
constexpr std::uint8_t RowsPerStep = 3;
constexpr std::uint8_t NibbleShift = 4;
constexpr std::uint8_t LowNibble = 0x0f;
// 0x6b87b8: 0,1f.
constexpr float AlphaStep = 0.1f;
constexpr std::uint8_t NoSkill = 0xff;

// Hasta el primer '\0', como lo leen strlen y strcpy.
std::string_view CString(std::string_view text)
{
    const std::size_t end = text.find('\0');
    return end == std::string_view::npos ? text : text.substr(0, end);
}

// El nombre de pkt[3..12] en el buffer de 11 bytes en cero.
std::string SpeakerName(const Protocol::Chat::SpokenText& spoken)
{
    std::string name;
    for (const std::byte value : spoken.name)
    {
        if (value == std::byte{0})
            break;
        name.push_back(std::to_integer<char>(value));
    }
    return name;
}

// `count` bytes desde pkt[0xd + skip] en el buffer de 61 en cero: lo que no llegó es '\0'.
std::string_view CopiedText(std::string_view raw, std::size_t skip, std::size_t count)
{
    if (skip >= raw.size())
        return {};
    return CString(raw.substr(skip, count));
}

Core::Expected<const Render::Text::ITextMeasure*, std::string> MeasureOf(const ChatReceiveContext& context,
                                                                         std::string_view what)
{
    if (context.measure == nullptr)
        return Core::Unexpected(std::string(what) + ": no hay con qué medir el mensaje");
    return context.measure;
}

Core::Expected<void, std::string> AddMessage(const ChatReceiveContext& context, std::string_view name,
                                             std::string_view text, std::int32_t type)
{
    auto measure = MeasureOf(context, "0x555190");
    if (!measure.HasValue())
        return Core::Unexpected(measure.Error());
    return AddGameMessage(context.world.Runtime(), *measure.Value(), context.screen, name, text, type);
}

Core::Expected<std::string_view, std::string> TextOf(const ChatReceiveContext& context, std::size_t index)
{
    const std::optional<std::string_view> text = Ui::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected("el texto " + std::to_string(index) + " de Text.bmd no está");
    return CString(*text);
}

// REPE CMPSB de 12 bytes desde +0x1d9: los diez del nombre, +0x1e3 y +0x1e4, que MuRet no tiene y toma en 0.
bool IsTheGuildMaster(const Kernel::ActorState& actor)
{
    for (std::size_t i = 0; i < actor.name.size(); ++i)
    {
        if (std::to_integer<std::uint8_t>(actor.name[i]) != GuildMasterName[i])
            return false;
    }
    return actor.byte1e3 == GuildMasterName[actor.name.size()];
}

// 0x4e3450..0x4e34b4: 0x6599a0(0x40000, 0) sin mirar lo que devuelve; 0x77f3b68 en 1 y 0x6c8e68 en 0; 0x553570(0);
// 0x6c8e78 en 8 (el tope de la entrada sin EDIT, que MuRet no guarda) y 0x6c8e6c en 0; el héroe con +0x1f2 en −1 pasa
// al gremio nuevo (0x7cf).
Core::Expected<void, std::string> TalkToTheGuildMaster(const ChatReceiveContext& context)
{
    Kernel::WorldState& world = context.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    Hud::GamePanels panels(world, context.random);
    // 0x4e345c → 0x6599a0: abrir el gremio pasa por los cierres compartidos de NPC, castillo y batalla.
    CastleSiege::LendTheCastlePanelServices(
        panels, world, context.random, context.guildSend, context.worldEntry,
        {.measure = context.measure, .texts = context.texts, .screen = context.screen});
    // Conserva el cierre local del gremio también sin socket (LendTheSession sólo presta cables no vacíos).
    panels.LendTheGuild(world, context.guildSend);
    if (auto opened = panels.Registry().Open(Ui::SecondGuildPanel); !opened.HasValue())
        return Core::Unexpected(std::move(opened).Error());
    Chat::ChatInputState& input = runtime.chat.input;
    input.dialogFieldOn = true;
    runtime.ui.chatOpen = false;
    Chat::ResetTheTextBuffers(input, false);
    input.fieldCount = 0;
    Kernel::ActorState& hero = world.Hero();
    if (static_cast<std::int16_t>(hero.word1f2) == Guild::NoGuildIndex)
        hero.word1f2 = static_cast<std::uint16_t>(Guild::CreatingGuildIndex);
    return {};
}
} // namespace

Core::Expected<void, std::string> ApplyChat(const Protocol::Chat::Chat& chat, const ChatReceiveContext& context)
{
    if (context.scene == Scenes::SceneId::Login)
    {
        if (context.requestServerList)
            context.requestServerList();
        return {};
    }
    auto measure = MeasureOf(context, "0x4c6070");
    if (!measure.HasValue())
        return Core::Unexpected(measure.Error());
    const std::string name = SpeakerName(chat);
    const std::string_view raw = chat.text;
    const char mark = raw.empty() ? '\0' : raw.front();
    const ChatBubbleMeasure bubbleMeasure{
        .measure = *measure.Value(), .screen = context.screen, .texts = context.texts};
    if (mark == PartyMark)
        return AddMessage(context, name, CopiedText(raw, 1, MarkedTextBytes), Ui::PartyMessage);
    if (mark == GuildMark)
    {
        if (raw.size() > 1 && raw[1] == GuildMark)
            return AddMessage(context, name, CopiedText(raw, 2, GuildNoticeTextBytes), Ui::GuildNoticeMessage);
        return AddMessage(context, name, CopiedText(raw, 1, MarkedTextBytes), Ui::GuildMessage);
    }
    if (mark == BubbleMark)
        return AddChatBubbleByName(context.world, bubbleMeasure, name, CopiedText(raw, 1, MarkedTextBytes),
                                   ChatBubbleMode::Log);
    const std::string_view text = CopiedText(raw, 0, Protocol::Chat::SpokenTextSize);
    if (auto bubble = AddChatBubbleByName(context.world, bubbleMeasure, name, text, ChatBubbleMode::Chat);
        !bubble.HasValue())
        return bubble;
    return AddMessage(context, name, text, Ui::ChatMessage);
}

Core::Expected<void, std::string> ApplyKeyChat(const Protocol::Chat::KeyChat& chat, const ChatReceiveContext& context)
{
    Kernel::WorldState& world = context.world;
    const std::size_t index = Kernel::ActorWithKey(world, chat.key);
    const auto actors = world.Actors();
    const Kernel::ActorState& speaker = actors[index];
    if (world.Hero().guildStatus == GuildMasterStatus && IsTheGuildMaster(speaker))
        return TalkToTheGuildMaster(context);
    auto measure = MeasureOf(context, "0x4e34d6");
    if (!measure.HasValue())
        return Core::Unexpected(measure.Error());
    return AddChatBubble(
        world, ChatBubbleMeasure{.measure = *measure.Value(), .screen = context.screen, .texts = context.texts},
        Actors::ChatNameOf(speaker), CString(chat.text), index, ChatBubbleMode::Chat);
}

Core::Expected<void, std::string> ApplyWhisper(const Protocol::Chat::Whisper& whisper,
                                               const ChatReceiveContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (runtime.chat.whisperBlocked)
        return {};
    const std::string name = SpeakerName(whisper);
    const std::string_view text = CopiedText(whisper.text, 0, Protocol::Chat::SpokenTextSize);
    // 0x554d20(10, nombre): el nivel (+0xe) se lee con el registro abierto, y la lista se mira después de cerrarlo.
    std::uint16_t level = 0;
    {
        const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        level = context.world.OwnCharacter().level;
    }
    Chat::RememberTheWhisperSender(runtime.chat.whisperSenders, level, name);
    // 0x41ed10(0x26, 0, 0).
    if (runtime.chat.noticeSound)
        runtime.RequestSound(WhisperSound);
    return AddMessage(context, name, text, Ui::WhisperMessage);
}

Core::Expected<void, std::string> ApplyWhisperResult(const Protocol::Chat::WhisperResult& result,
                                                     const ChatReceiveContext& context)
{
    if (result.result != 0)
        return {};
    auto text = TextOf(context, WhisperFailedText);
    if (!text.HasValue())
        return Core::Unexpected(text.Error());
    const std::string target(CString(context.world.Runtime().chat.lastWhisperTarget));
    return AddMessage(context, target, text.Value(), Ui::ErrorMessage);
}

Core::Expected<void, std::string> ApplyNotice(const Protocol::Chat::Notice& notice, const ChatReceiveContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const std::string_view text = CString(notice.text);
    if (notice.type == CenterNotice)
    {
        auto measure = MeasureOf(context, "0x554610");
        if (!measure.HasValue())
            return Core::Unexpected(measure.Error());
        Ui::AddNotice(runtime.chat.area, *measure.Value(), runtime.hud.dcFont, text, 0);
        return {};
    }
    if (notice.type == SystemNotice)
    {
        // 0x4c65d8: fuera de la selección, 0x555190 con el "" de 0x5767980.
        if (context.scene != Scenes::SceneId::CharacterSelect)
            return AddMessage(context, {}, text, Ui::SystemMessage);
        // 0x4c65f9..0x4c662a: la franja de la barra (+0x510), una vez; después, con el "" de 0x5767984.
        if (runtime.chat.selectionNotice)
            return AddMessage(context, {}, text, Ui::SystemMessage);
        runtime.chat.selectionNotice = std::string(text);
        return {};
    }
    if (notice.type == GuildNotice)
    {
        auto format = TextOf(context, GuildNoticeText);
        if (!format.HasValue())
            return Core::Unexpected(format.Error());
        auto measure = MeasureOf(context, "0x554610");
        if (!measure.HasValue())
            return Core::Unexpected(measure.Error());
        // 0x4c6645: sprintf a un buffer de 100 bytes, sin tope (Q-C514).
        const std::array<std::string_view, 1> arguments{text};
        const std::string line = Ui::PlaceArguments(format.Value(), arguments);
        Ui::AddNotice(runtime.chat.area, *measure.Value(), runtime.hud.dcFont, line, 1);
        // 0x4c665f: 0x651680 con el texto del paquete, a la lista del aviso de la ventana del gremio.
        return Guild::AddTheGuildNotice(runtime.guild.window, *measure.Value(), runtime.hud.dcFont, context.screen,
                                        text);
    }
    if (notice.type < FirstSlideNotice || notice.type > LastSlideNotice || text.empty())
        return {};
    // 0x4c667d..0x4c66b3: 0x46b720(pkt[4], pkt[6..7], texto, tipo − 10, pkt[0xc] · 0,1f, pkt[8..0xb]). La x87
    // multiplica el byte por el float y guarda en float: el producto es exacto y se redondea una vez. 0x46b720 y
    // 0x46abf0 solo encolan en la escena 5.
    if (context.scene != Scenes::SceneId::Game)
        return {};
    const auto speed = static_cast<float>(static_cast<double>(notice.speed) * static_cast<double>(SlideSpeedStep));
    Ui::QueueTheSlide(runtime.hud.slides, notice.byte4, notice.word6, text, notice.type - FirstSlideNotice, speed,
                      notice.color);
    return {};
}

Core::Expected<void, std::string> ApplyOptionData(const Protocol::Player::OptionData& data,
                                                  const ChatReceiveContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    // 0x4dd632..0x4dd645: +0x26c + [0x6d5edc] · 0x100. Con −1 serían +0x16c..+0x26b, que MuRet no tiene.
    if (!context.chosenSlot || *context.chosenSlot >= Kernel::OwnSkillKeyPlaces)
        return Core::Unexpected(std::string("0x4dd645: sin elegido (0x6d5edc), el F3 30 escribe fuera de las teclas "
                                            "de las skills (+0x16c del registro propio)"));
    {
        const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        Kernel::OwnCharacterRecord& record = context.world.OwnCharacter();
        std::array<std::uint8_t, Kernel::OwnSkillKeyBytes>& keys = record.skillKeys[*context.chosenSlot];
        keys.fill(NoSkill);
        for (std::size_t key = 0; key < data.skillKeys.size(); ++key)
        {
            const std::uint8_t skill = data.skillKeys[key];
            if (skill == NoSkill)
                continue;
            for (std::size_t at = 0; at < Kernel::OwnSkillKeyBytes; ++at)
            {
                if (Kernel::OwnSkillByte(record, at) == skill)
                {
                    keys[at] = static_cast<std::uint8_t>(key);
                    break;
                }
            }
        }
    }
    runtime.ui.autoAttack = (data.flags & AutoAttackFlag) == AutoAttackFlag;
    runtime.chat.noticeSound = (data.flags & NoticeSoundFlag) == NoticeSoundFlag;
    runtime.chat.slidesOn = (data.flags & SlidesOffFlag) != SlidesOffFlag;
    for (std::size_t slot = 0; slot < data.quickSlots.size(); ++slot)
        runtime.hud.quickSlots.types[slot] = QuickSlotBase + data.quickSlots[slot];
    // 0x4dd82c..0x4dd839: IMUL de un byte: (pkt[0x12] >> 4) · 3, que no pasa de 45.
    const auto rows = static_cast<std::uint8_t>((data.chatWindow >> NibbleShift) * RowsPerStep);
    if (rows == 0)
        runtime.ui.chatModes.placement = 0;
    else
    {
        runtime.ui.chatModes.placement = 1;
        // vtable + 0x3c del registro: 0x472020, +0x104.
        runtime.chat.log.rows = rows;
    }
    // 0x4dd87e: FILD, FMUL por el float y FSTP: el producto es exacto y se redondea una vez.
    runtime.chat.log.alpha =
        static_cast<float>(static_cast<double>(data.chatWindow & LowNibble) * static_cast<double>(AlphaStep));
    return {};
}
} // namespace Client::Chat
