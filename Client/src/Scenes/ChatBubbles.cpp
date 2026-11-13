#include "Scenes/ChatBubbles.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameMessages.h"
#include "Scenes/PersonalShopTitles.h"
#include "Scenes/SocialState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/ChatLog.h"
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

// 0x556dd0(entrada, personaje): sin cartel de tienda (0x451810) el título vacío, y con él el título corto de 0x4518b0,
// que corta; sin gremio (+0x1f2 negativo) el gremio, el puesto y +0x65 en 0.
Core::Expected<void, std::string> FillTheGuild(ChatBubble& bubble, const ActorState& owner, bool hasShop)
{
    if (hasShop)
        return Core::Unexpected(std::string("0x556e26: el título corto de la tienda personal en el globo (0x4518b0) es "
                                            "del hito 7 (08 P-C208 a)"));
    bubble.shopTitle.clear();
    if (static_cast<std::int16_t>(owner.word1f2) >= 0)
        return Core::Unexpected(std::string("0x556e95: el gremio del globo lee la tabla de los gremios (0x787aa4c), "
                                            "que es de la CL9.4"));
    bubble.guild.clear();
    bubble.guildByte = 0;
    bubble.guildPost.clear();
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
    if (auto guild = FillTheGuild(*bubble, speaker, HasShopTitle(world.Runtime().social.personalShop, owner));
        !guild.HasValue())
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
        // 0x5576a0..0x5576c9: con el globo a la vista (+time1 > 0) el cartel de la tienda del dueño se esconde
        // (0x451720, que con el dueño en NULL no encuentra nada); con un dueño y +time1 en 0, se muestra (0x4516d0).
        PersonalShopState& shops = runtime.social.personalShop;
        if (bubble.time1 > 0 && bubble.owner)
            HideShopTitle(shops, *bubble.owner);
        if (bubble.owner && bubble.time1 == 0)
            ShowShopTitle(shops, *bubble.owner);
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
