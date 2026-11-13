#include "Scenes/ChatBubbles.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameMessages.h"
#include "Scenes/GuildState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/ChatLog.h"
#include "UI/GameText.h"
#include "UI/MessageArea.h"
#include "UI/MessageLog.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Scenes
{
namespace
{
// +0x88 de los personajes que busca 0x557560, en este orden.
constexpr std::uint8_t PlayerKind = 1;
constexpr std::uint8_t MonsterKind = 2;
// 0x55719c: con +0x88 en 4, el color es 0.
constexpr std::uint8_t NpcKind = 4;
// 0x557797: +0x376 del dueño para llenar el destino del susurro.
constexpr std::uint16_t WhisperableWord376 = 0xff;
constexpr std::int32_t Width640 = 640;
constexpr std::int32_t Height480 = 480;

// Hasta el primer '\0', como lo leen strlen, strcpy y strcmp.
std::string_view CString(std::string_view text)
{
    const std::size_t end = text.find('\0');
    return end == std::string_view::npos ? text : text.substr(0, end);
}

// 0x556dd0: los textos de Text.bmd (la dirección − 0x76bd1dc, / 300). La alianza por +0x370 (4, 2 y 8; 4 y 2 con el
// Guild Master, +0x368 en 0x80) y el puesto por +0x368 (0 el miembro, 0x80, 0x40 y 0x20).
constexpr std::size_t UnionMasterTexts[] = {1296, 1298}; // 0x771c09c y 0x771c2f4
constexpr std::size_t UnionMemberTexts[] = {1295, 1297}; // 0x771bf70 y 0x771c1c8
constexpr std::size_t UnionKind8Text = 1299;             // 0x771c420
constexpr std::size_t MemberPostText = 1330;             // 0x771e874
constexpr std::size_t MasterPostText = 1300;             // 0x771c54c
constexpr std::size_t AssistantPostText = 1301;          // 0x771c678
constexpr std::size_t BattlePostText = 1302;             // 0x771c7a4
constexpr std::uint8_t MasterStatus = 0x80;
constexpr std::uint8_t AssistantStatus = 0x40;
constexpr std::uint8_t BattleStatus = 0x20;
// +0x18..+0x35 y +0x36..+0x53: los dos buffers de 0x1e bytes del sprintf. El primero de +0x370 en 1 recién pisa algo
// que queda (el título de la tienda, +0x54) desde 0x3c.
constexpr std::size_t GuildLineBytes = 0x1e;
constexpr std::size_t UntilTheShopTitle = 0x3c;

std::string_view NameOf(std::span<const char> name)
{
    return CString(std::string_view(name.data(), name.size()));
}

// "<%s> %s" y "[%s] %s" con el texto, o "<%s>" y "[%s]" sin él. Uno de `bytes` o más pisa lo que sigue de la
// entrada (el otro renglón o el título de la tienda): corta (Q-C1283).
Core::Expected<std::string, std::string> GuildLine(char open, char close, std::string_view name,
                                                   std::optional<std::size_t> text, const Data::Text::TextTable* texts,
                                                   std::size_t bytes, std::string_view where)
{
    std::string line;
    line.push_back(open);
    line.append(name);
    line.push_back(close);
    if (text)
    {
        const std::optional<std::string_view> found = UI::TextAt(texts, *text);
        if (!found)
            return Core::Unexpected(std::string(where) + ": el texto " + std::to_string(*text) +
                                    " de Text.bmd no está");
        line.push_back(' ');
        line.append(CString(*found));
    }
    if (line.size() >= bytes)
        return Core::Unexpected(std::string(where) + ": el renglón del gremio no entra en sus 0x1e bytes (Q-C1283)");
    return line;
}

// 0x556dd0(entrada, personaje): sin tiendas (0x7245c8, CL9.4) el título vacío. Con gremio (+0x1f2 no negativo), la
// alianza (+0x4 de la entrada) en +0x18 si tiene nombre, +0x65 = +0x374 y el gremio (+0xd) con el puesto en +0x36; sin
// gremio, los dos vacíos y +0x65 en 0.
Core::Expected<void, std::string> FillTheGuild(ChatBubble& bubble, const ActorState& owner, const GuildState& guild,
                                               const Data::Text::TextTable* texts)
{
    bubble.shopTitle.clear();
    const auto index = static_cast<std::int16_t>(owner.word1f2);
    if (index < 0)
    {
        bubble.guild.clear();
        bubble.guildByte = 0;
        bubble.guildPost.clear();
        return {};
    }
    if (static_cast<std::size_t>(index) >= GuildTableSize)
        return Core::Unexpected(std::string("0x556e95: el índice del gremio cae fuera de la tabla (0x787aa48)"));
    const GuildTableEntry& entry = guild.table[static_cast<std::size_t>(index)];
    const bool master = owner.guildStatus == MasterStatus;
    const std::string_view unionName = NameOf(entry.unionName);
    if (unionName.empty())
    {
        bubble.guild.clear();
    }
    else
    {
        // 0x556eb4: con +0x370 en 1, "<%s> %s" con el 1295, que el default de abajo pisa con "<%s>".
        if (owner.byte370 == 1)
        {
            if (auto first = GuildLine('<', '>', unionName, UnionMemberTexts[0], texts, UntilTheShopTitle, "0x556ecc");
                !first.HasValue())
                return Core::Unexpected(first.Error());
        }
        std::optional<std::size_t> text;
        if (owner.byte370 == 4)
            text = master ? UnionMasterTexts[0] : UnionMemberTexts[0];
        else if (owner.byte370 == 2)
            text = master ? UnionMasterTexts[1] : UnionMemberTexts[1];
        else if (owner.byte370 == 8)
            text = UnionKind8Text;
        auto line = GuildLine('<', '>', unionName, text, texts, GuildLineBytes, "0x556ed1");
        if (!line.HasValue())
            return Core::Unexpected(line.Error());
        bubble.guild = std::move(line).Value();
    }
    bubble.guildByte = owner.byte374;
    std::optional<std::size_t> post;
    if (owner.guildStatus == 0)
        post = MemberPostText;
    else if (owner.guildStatus == MasterStatus)
        post = MasterPostText;
    else if (owner.guildStatus == AssistantStatus)
        post = AssistantPostText;
    else if (owner.guildStatus == BattleStatus)
        post = BattlePostText;
    auto line = GuildLine('[', ']', NameOf(entry.guildName), post, texts, GuildLineBytes, "0x557024");
    if (!line.HasValue())
        return Core::Unexpected(line.Error());
    bubble.guildPost = std::move(line).Value();
    return {};
}

// 0x557256..0x557268 y 0x55735a..0x557366: el renglón nuevo, cortado en dos desde 20 bytes (la cabeza arriba, en
// +0x16c, y la cola abajo, en +0x6c, 0x5549a0(texto, +0x6c, +0x16c, largo)) o entero en el de abajo.
void PutTheLine(ChatBubble& bubble, std::string_view text, std::int32_t steps)
{
    if (text.size() >= ChatBubbleSplitBytes)
    {
        UI::HalvedText halves = UI::SplitInHalf(text);
        bubble.line1 = std::move(halves.tail);
        bubble.line2 = std::move(halves.head);
        bubble.time1 = steps;
        bubble.time2 = steps;
        return;
    }
    bubble.line1 = std::string(text);
    bubble.time1 = steps;
}
} // namespace

std::string ChatNameOf(const ActorState& actor)
{
    std::string name(HeroNameOf(actor));
    if (name.size() == actor.name.size() && actor.byte1e3 != 0)
        name.push_back(static_cast<char>(actor.byte1e3));
    return name;
}

Core::Expected<void, std::string> AddChatBubble(WorldState& world, const ChatBubbleMeasure& measure,
                                                std::string_view name, std::string_view text, std::size_t owner,
                                                ChatBubbleMode mode, std::optional<std::uint8_t> color)
{
    const ActorState& speaker = world.Actors()[owner];
    if (!speaker.alive || !speaker.seen)
        return {};
    name = CString(name);
    text = CString(text);
    WorldRuntime& runtime = world.Runtime();
    ChatBubbleTable& bubbles = runtime.chat.bubbles;
    // 0x557190..0x5571a6: sin color (−1), el PK; con +0x88 en 4, 0.
    const std::uint8_t shown = color.value_or(speaker.byte88 == NpcKind ? std::uint8_t{0} : speaker.pk);
    ChatBubble* bubble = nullptr;
    bool ownEntry = false;
    for (ChatBubble& entry : bubbles)
    {
        if (entry.owner == owner)
        {
            bubble = &entry;
            ownEntry = true;
            break;
        }
    }
    if (bubble == nullptr)
    {
        for (ChatBubble& entry : bubbles)
        {
            if (entry.nameTime <= 0 && entry.time1 <= 0)
            {
                bubble = &entry;
                break;
            }
        }
        if (bubble == nullptr)
            return {};
        // 0x5571ff: la nueva es del personaje antes de copiar el nombre.
        bubble->owner = owner;
    }
    bubble->name = std::string(name);
    bubble->color = shown;
    if (auto guild = FillTheGuild(*bubble, speaker, runtime.guild, measure.texts); !guild.HasValue())
        return guild;
    if (text.empty())
    {
        bubble->nameTime = ownEntry ? ChatBubbleNameSteps : ChatBubbleNewNameSteps;
        return {};
    }
    if (ownEntry)
    {
        // 0x557286..0x5572a4: el renglón vivo sube.
        if (bubble->time1 > 0)
        {
            bubble->line2 = bubble->line1;
            bubble->time2 = bubble->time1;
        }
        bubble->owner = owner;
    }
    const auto length = static_cast<std::int32_t>(text.size());
    std::int32_t steps = 2 * length + ChatBubbleBaseSteps;
    if (mode == ChatBubbleMode::Log)
    {
        steps = ChatBubbleLogSteps;
        // vtable + 0x94 del registro: 0x45bb20(nombre de la entrada, texto, 3, 0).
        UI::MessageSink sink = MessageSinkOf(runtime, measure.measure, measure.screen);
        auto sound = UI::AddToTheLog(sink.log, sink.modes, sink.measure, bubble->name, text, UI::ChatMessage, 0,
                                     sink.noticeSound);
        if (!sound.HasValue())
            return Core::Unexpected(sound.Error());
        if (sound.Value())
            runtime.lastSoundId = *sound.Value();
    }
    PutTheLine(*bubble, text, steps);
    return {};
}

Core::Expected<void, std::string> AddChatBubbleByName(WorldState& world, const ChatBubbleMeasure& measure,
                                                      std::string_view name, std::string_view text, ChatBubbleMode mode)
{
    name = CString(name);
    // 0x557560 mira los 400 primeros, sin el 401.
    constexpr std::size_t Searched = ActorPoolSize - 1;
    const std::span<const ActorState, ActorPoolSize> actors = world.Actors();
    for (const std::uint8_t kind : {PlayerKind, MonsterKind})
    {
        for (std::size_t index = 0; index < Searched; ++index)
        {
            const ActorState& actor = actors[index];
            if (actor.alive && actor.byte88 == kind && ChatNameOf(actor) == name)
                return AddChatBubble(world, measure, name, text, index, mode);
        }
    }
    return {};
}

Core::Expected<void, std::string> StepTheChatBubbles(WorldState& world, const ChatBubbleStepInput& input)
{
    WorldRuntime& runtime = world.Runtime();
    const std::span<const ActorState, ActorPoolSize> actors = world.Actors();
    const ActorState& hero = actors[runtime.hero];
    for (ChatBubble& bubble : runtime.chat.bubbles)
    {
        if (bubble.nameTime > 0)
            --bubble.nameTime;
        if (bubble.time1 > 0)
            --bubble.time1;
        if (bubble.time2 > 0)
            --bubble.time2;
        if (bubble.owner && (!actors[*bubble.owner].alive || !actors[*bubble.owner].seen))
        {
            bubble.nameTime = 0;
            bubble.time1 = 0;
            bubble.time2 = 0;
        }
        // 0x557701..0x557722: las tiendas personales (0x451720 y 0x4516d0) no tienen ninguna hasta la CL9.4.
        if (input.screen.width <= 0 || input.screen.height <= 0)
            continue;
        // 0x557722..0x557738: el ancho y el alto pasan a 640 × 480 con una división sin signo.
        const auto right = static_cast<std::int32_t>(static_cast<std::uint32_t>(bubble.width * Width640) /
                                                     static_cast<std::uint32_t>(input.screen.width)) +
                           bubble.x;
        const auto bottom = static_cast<std::int32_t>(static_cast<std::uint32_t>(bubble.height * Height480) /
                                                      static_cast<std::uint32_t>(input.screen.height)) +
                            bubble.y;
        if (input.mouseX < bubble.x || input.mouseX >= right || input.mouseY < bubble.y || input.mouseY >= bottom)
            continue;
        if (!runtime.ui.chatOpen || hero.safeZone == 0)
            continue;
        if (CString(bubble.name) == ChatNameOf(hero) || !runtime.ui.rightClick || !bubble.owner ||
            actors[*bubble.owner].word376 != WhisperableWord376)
            continue;
        // 0x5577a2..0x55780d: el derecho se gasta, el nombre va al EDIT del susurro (0x4694c0(0, 0, 1, nombre), con
        // 0x6c7b8c en 1) y con strcpy al buffer y su largo, y suena el 0x19. Sigue con los otros globos.
        runtime.ui.rightClick = false;
        UI::SetTheChatFields(runtime.chat.input, std::nullopt, CString(bubble.name));
        UI::PutTheWhisperName(runtime.chat.input, CString(bubble.name));
        runtime.lastSoundId = UI::ChatButtonSound;
    }
    return {};
}
} // namespace Client::Scenes
