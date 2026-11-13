#include "Scenes/ChatReceive.h"

#include "Core/Expected.h"
#include "Protocol/Messages.h"
#include "Protocol/OptionData.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/ActorState.h"
#include "Scenes/ChatBubbles.h"
#include "Scenes/GameMessages.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameText.h"
#include "UI/MessageArea.h"
#include "UI/SlideBanner.h"
#include "UI/WhisperSenders.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Client::Scenes
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
constexpr std::int32_t WhisperSound = 0x26;
// 0x4e341d: el héroe en +0x368 con 0x80; 0x6c8404: "길드 마스터" en EUC-KR, los 12 bytes que compara REPE CMPSB.
constexpr std::uint8_t GuildMasterStatus = 0x80;
constexpr std::array<std::uint8_t, 12> GuildMasterName{0xb1, 0xe6, 0xb5, 0xe5, 0x20, 0xb8,
                                                       0xb6, 0xbd, 0xba, 0xc5, 0xcd, 0x00};
// 0x5110d0: los 400 que recorre y el que devuelve sin ninguno.
constexpr std::size_t KeySearched = 400;
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
std::string SpeakerName(const Protocol::Messages::SpokenText& spoken)
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
    const std::optional<std::string_view> text = UI::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected("el texto " + std::to_string(index) + " de Text.bmd no está");
    return CString(*text);
}

// 0x5110d0: el primero de los 400 vivo con la clave (MOVSX de +0x1f4 contra el int del paquete: una clave desde
// 0x8000 no la tiene nadie); sin ninguno, el 400.
std::size_t ActorWithKey(const WorldState& world, std::uint16_t key)
{
    const auto actors = world.Actors();
    for (std::size_t index = 0; index < KeySearched; ++index)
    {
        const ActorState& actor = actors[index];
        if (actor.alive && static_cast<std::int32_t>(static_cast<std::int16_t>(actor.key)) == key)
            return index;
    }
    return KeySearched;
}

// REPE CMPSB de 12 bytes desde +0x1d9: los diez del nombre, +0x1e3 y +0x1e4, que MuRet no tiene y toma en 0.
bool IsTheGuildMaster(const ActorState& actor)
{
    for (std::size_t i = 0; i < actor.name.size(); ++i)
    {
        if (std::to_integer<std::uint8_t>(actor.name[i]) != GuildMasterName[i])
            return false;
    }
    return actor.byte1e3 == GuildMasterName[actor.name.size()];
}
} // namespace

Core::Expected<void, std::string> ApplyChat(const Protocol::Messages::Chat& chat, const ChatReceiveContext& context)
{
    auto measure = MeasureOf(context, "0x4c6070");
    if (!measure.HasValue())
        return Core::Unexpected(measure.Error());
    const std::string name = SpeakerName(chat);
    const std::string_view raw = chat.text;
    const char mark = raw.empty() ? '\0' : raw.front();
    const ChatBubbleMeasure bubbleMeasure{.measure = *measure.Value(), .screen = context.screen};
    if (mark == PartyMark)
        return AddMessage(context, name, CopiedText(raw, 1, MarkedTextBytes), UI::PartyMessage);
    if (mark == GuildMark)
    {
        if (raw.size() > 1 && raw[1] == GuildMark)
            return AddMessage(context, name, CopiedText(raw, 2, GuildNoticeTextBytes), UI::GuildNoticeMessage);
        return AddMessage(context, name, CopiedText(raw, 1, MarkedTextBytes), UI::GuildMessage);
    }
    if (mark == BubbleMark)
        return AddChatBubbleByName(context.world, bubbleMeasure, name, CopiedText(raw, 1, MarkedTextBytes),
                                   ChatBubbleMode::Log);
    const std::string_view text = CopiedText(raw, 0, Protocol::Messages::SpokenTextSize);
    if (auto bubble = AddChatBubbleByName(context.world, bubbleMeasure, name, text, ChatBubbleMode::Chat);
        !bubble.HasValue())
        return bubble;
    return AddMessage(context, name, text, UI::ChatMessage);
}

Core::Expected<void, std::string> ApplyKeyChat(const Protocol::Messages::KeyChat& chat,
                                               const ChatReceiveContext& context)
{
    WorldState& world = context.world;
    const std::size_t index = ActorWithKey(world, chat.key);
    const auto actors = world.Actors();
    const ActorState& speaker = actors[index];
    if (actors[world.Runtime().hero].guildStatus == GuildMasterStatus && IsTheGuildMaster(speaker))
        return Core::Unexpected(std::string("0x4e3450: el 01 del Guild Master abre la ventana del gremio "
                                            "(0x6599a0 con 0x40000), que es de la CL9.4"));
    auto measure = MeasureOf(context, "0x4e34d6");
    if (!measure.HasValue())
        return Core::Unexpected(measure.Error());
    return AddChatBubble(world, ChatBubbleMeasure{.measure = *measure.Value(), .screen = context.screen},
                         ChatNameOf(speaker), CString(chat.text), index, ChatBubbleMode::Chat);
}

Core::Expected<void, std::string> ApplyWhisper(const Protocol::Messages::Whisper& whisper,
                                               const ChatReceiveContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    if (runtime.chat.whisperBlocked)
        return {};
    const std::string name = SpeakerName(whisper);
    const std::string_view text = CopiedText(whisper.text, 0, Protocol::Messages::SpokenTextSize);
    // 0x554d20(10, nombre): el nivel (+0xe) se lee con el registro abierto, y la lista se mira después de cerrarlo.
    std::uint16_t level = 0;
    {
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                Render::Models::OwnRecordCloseRandoms);
        level = context.world.OwnCharacter().level;
    }
    UI::RememberTheWhisperSender(runtime.chat.whisperSenders, level, name);
    // 0x41ed10(0x26, 0, 0): MuRet no tiene audio hasta la CL10.
    if (runtime.chat.noticeSound)
        runtime.lastSoundId = WhisperSound;
    return AddMessage(context, name, text, UI::WhisperMessage);
}

Core::Expected<void, std::string> ApplyWhisperResult(const Protocol::Messages::WhisperResult& result,
                                                     const ChatReceiveContext& context)
{
    if (result.result != 0)
        return {};
    auto text = TextOf(context, WhisperFailedText);
    if (!text.HasValue())
        return Core::Unexpected(text.Error());
    const std::string target(CString(context.world.Runtime().chat.lastWhisperTarget));
    return AddMessage(context, target, text.Value(), UI::ErrorMessage);
}

Core::Expected<void, std::string> ApplyNotice(const Protocol::Messages::Notice& notice,
                                              const ChatReceiveContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    const std::string_view text = CString(notice.text);
    if (notice.type == CenterNotice)
    {
        auto measure = MeasureOf(context, "0x554610");
        if (!measure.HasValue())
            return Core::Unexpected(measure.Error());
        UI::AddNotice(runtime.chat.area, *measure.Value(), runtime.hud.dcFont, text, 0);
        return {};
    }
    if (notice.type == SystemNotice)
    {
        // 0x4c65d8: fuera de la selección, 0x555190 con el "" de 0x5767980.
        if (context.scene != SceneId::CharacterSelect)
            return AddMessage(context, {}, text, UI::SystemMessage);
        // 0x4c65f9..0x4c662a: la franja de la barra (+0x510), una vez; después, con el "" de 0x5767984.
        if (runtime.chat.selectionNotice)
            return AddMessage(context, {}, text, UI::SystemMessage);
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
        const std::string line = UI::PlaceArguments(format.Value(), arguments);
        UI::AddNotice(runtime.chat.area, *measure.Value(), runtime.hud.dcFont, line, 1);
        return Core::Unexpected(std::string("0x4c665f: 0x651680 guarda el aviso del gremio en su ventana "
                                            "([0x7d536f0]), que es de la CL9.4"));
    }
    if (notice.type < FirstSlideNotice || notice.type > LastSlideNotice || text.empty())
        return {};
    // 0x4c667d..0x4c66b3: 0x46b720(pkt[4], pkt[6..7], texto, tipo − 10, pkt[0xc] · 0,1f, pkt[8..0xb]). La x87
    // multiplica el byte por el float y guarda en float: el producto es exacto y se redondea una vez. 0x46b720 y
    // 0x46abf0 solo encolan en la escena 5.
    if (context.scene != SceneId::Game)
        return {};
    const auto speed = static_cast<float>(static_cast<double>(notice.speed) * static_cast<double>(SlideSpeedStep));
    UI::QueueTheSlide(runtime.hud.slides, notice.byte4, notice.word6, text, notice.type - FirstSlideNotice, speed,
                      notice.color);
    return {};
}

Core::Expected<void, std::string> ApplyOptionData(const Protocol::OptionData::OptionData& data,
                                                  const ChatReceiveContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    // 0x4dd632..0x4dd645: +0x26c + [0x6d5edc] · 0x100. Con −1 serían +0x16c..+0x26b, que MuRet no tiene.
    if (!context.chosenSlot || *context.chosenSlot >= OwnSkillKeyPlaces)
        return Core::Unexpected(std::string("0x4dd645: sin elegido (0x6d5edc), el F3 30 escribe fuera de las teclas "
                                            "de las skills (+0x16c del registro propio)"));
    {
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                Render::Models::OwnRecordCloseRandoms);
        OwnCharacterRecord& record = context.world.OwnCharacter();
        std::array<std::uint8_t, OwnSkillKeyBytes>& keys = record.skillKeys[*context.chosenSlot];
        keys.fill(NoSkill);
        for (std::size_t key = 0; key < data.skillKeys.size(); ++key)
        {
            const std::uint8_t skill = data.skillKeys[key];
            if (skill == NoSkill)
                continue;
            for (std::size_t at = 0; at < OwnSkillKeyBytes; ++at)
            {
                if (OwnSkillByte(record, at) == skill)
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
} // namespace Client::Scenes
