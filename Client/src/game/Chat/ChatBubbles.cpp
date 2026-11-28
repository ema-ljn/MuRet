#include "Chat/ChatBubbles.h"

#include "Actors/ActorName.h"
#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Kernel/ActorState.h"
#include "Chat/GameMessages.h"
#include "Guild/GuildState.h"
#include "PersonalShop/PersonalShopTitles.h"
#include "PersonalShop/ShopSigns.h"
#include "Social/SocialState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/GameContext.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatInput.h"
#include "Chat/ChatLog.h"
#include "Ui/GameText.h"
#include "Ui/MessageArea.h"
#include "Ui/MessageLog.h"
#include "Audio/SoundId.h"
#include "Chat/ChatRuntime.h"
#include "Ui/GameUiState.h"

#include <algorithm>
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
// +0x88 of the characters 0x557560 searches, in this order.
constexpr std::uint8_t PlayerKind = 1;
constexpr std::uint8_t MonsterKind = 2;
// 0x55719c: with +0x88 at 4, the color is 0.
constexpr std::uint8_t NpcKind = 4;
// 0x557797: the owner's +0x376 to fill the whisper target.
constexpr std::uint16_t WhisperableWord376 = 0xff;
constexpr std::int32_t Width640 = 640;
constexpr std::int32_t Height480 = 480;

// Up to the first '\0', as strlen, strcpy and strcmp read it.
std::string_view CString(std::string_view text)
{
    const std::size_t end = text.find('\0');
    return end == std::string_view::npos ? text : text.substr(0, end);
}

// 0x556dd0: the Text.bmd texts (the address − 0x76bd1dc, / 300). The alliance by +0x370 (4, 2 and 8; 4 and 2 with the
// Guild Master, +0x368 at 0x80) and the post by +0x368 (0 the member, 0x80, 0x40 and 0x20).
constexpr std::size_t UnionMasterTexts[] = {1296, 1298}; // 0x771c09c and 0x771c2f4
constexpr std::size_t UnionMemberTexts[] = {1295, 1297}; // 0x771bf70 and 0x771c1c8
constexpr std::size_t UnionKind8Text = 1299;             // 0x771c420
constexpr std::size_t MemberPostText = 1330;             // 0x771e874
constexpr std::size_t MasterPostText = 1300;             // 0x771c54c
constexpr std::size_t AssistantPostText = 1301;          // 0x771c678
constexpr std::size_t BattlePostText = 1302;             // 0x771c7a4
constexpr std::uint8_t MasterStatus = 0x80;
constexpr std::uint8_t AssistantStatus = 0x40;
constexpr std::uint8_t BattleStatus = 0x20;
// +0x18..+0x63 of the entry, from +0x18: the alliance (+0x18) and the guild (+0x36), two buffers of 0x1e bytes, and
// the shop title (+0x54), of 0x10, up to the color (+0x64). 0x556dd0's sprintf calls have no limit: a long line runs
// on into what comes after (Q-C1283).
constexpr std::size_t UnionLineAt = 0;
constexpr std::size_t GuildPostAt = 0x1e;
constexpr std::size_t ShopTitleAt = 0x3c;
using GuildBytes = std::array<char, 0x4c>;

std::string_view NameOf(std::span<const char> name)
{
    return CString(std::string_view(name.data(), name.size()));
}

// "<%s> %s" and "[%s] %s" with the text, or "<%s>" and "[%s]" without it.
Core::Expected<std::string, std::string> GuildLine(char open, char close, std::string_view name,
                                                   std::optional<std::size_t> text, const Tables::TextTable* texts,
                                                   std::string_view where)
{
    std::string line;
    line.push_back(open);
    line.append(name);
    line.push_back(close);
    if (text)
    {
        const std::optional<std::string_view> found = Ui::TextAt(texts, *text);
        if (!found)
            return Core::Unexpected(std::string(where) + ": el texto " + std::to_string(*text) +
                                    " de Text.bmd no está");
        line.push_back(' ');
        line.append(CString(*found));
    }
    return line;
}

// The strcpy or the sprintf at +0x18 + `at`, with its zero: what overflows its buffer lands in the next one
// (Q-C1283). If it reaches the color (+0x64), it stops.
Core::Expected<void, std::string> PutAt(GuildBytes& bytes, std::size_t at, std::string_view text,
                                        std::string_view where)
{
    if (at + text.size() >= bytes.size())
        return Core::Unexpected(std::string(where) +
                                ": el renglón del gremio pisa el color (+0x64) del globo (Q-C1283)");
    std::ranges::copy(text, bytes.begin() + static_cast<std::ptrdiff_t>(at));
    bytes[at + text.size()] = '\0';
    return {};
}

// What the draw reads from +0x18 + `at`, up to the first zero.
std::string ReadAt(const GuildBytes& bytes, std::size_t at)
{
    return std::string(CString(std::string_view(bytes.data() + at, bytes.size() - at)));
}

// 0x556dd0(entry, character): without a shop sign (0x451810) the title empty (0x556f23), and with one the short title
// of 0x4518b0 with strcpy (0x556e26..0x556e60). Then, with a guild (+0x1f2 not negative), the alliance (the entry's
// +0x4) at +0x18 if it has a name, +0x65 = +0x374 and the guild (+0xd) with the post at +0x36; without a guild, both
// empty and +0x65 at 0.
Core::Expected<void, std::string> FillTheGuild(ChatBubble& bubble, const Kernel::ActorState& owner,
                                               const Guild::GuildState& guild, const Tables::TextTable* texts,
                                               const Social::PersonalShopState& shops, std::size_t ownerIndex)
{
    bubble.shopTitle = PersonalShop::ShortShopTitle(shops, ownerIndex);
    GuildBytes bytes{};
    if (auto title = PutAt(bytes, ShopTitleAt, bubble.shopTitle, "0x556e60"); !title.HasValue())
        return title;
    const auto index = static_cast<std::int16_t>(owner.word1f2);
    if (index < 0)
    {
        bubble.guild.clear();
        bubble.guildByte = 0;
        bubble.guildPost.clear();
        return {};
    }
    if (static_cast<std::size_t>(index) >= Guild::GuildTableSize)
        return Core::Unexpected(std::string("0x556e95: el índice del gremio cae fuera de la tabla (0x787aa48)"));
    const Guild::GuildTableEntry& entry = guild.table[static_cast<std::size_t>(index)];
    const bool master = owner.guildStatus == MasterStatus;
    const std::string_view unionName = NameOf(entry.unionName);
    // 0x557010: without an alliance, +0x18 at 0.
    if (!unionName.empty())
    {
        // 0x556eb4: with +0x370 at 1, "<%s> %s" with the 1295, which the default below overwrites with "<%s>".
        if (owner.byte370 == 1)
        {
            auto first = GuildLine('<', '>', unionName, UnionMemberTexts[0], texts, "0x556ecc");
            if (!first.HasValue())
                return Core::Unexpected(first.Error());
            if (auto put = PutAt(bytes, UnionLineAt, first.Value(), "0x556ecc"); !put.HasValue())
                return put;
        }
        std::optional<std::size_t> text;
        if (owner.byte370 == 4)
            text = master ? UnionMasterTexts[0] : UnionMemberTexts[0];
        else if (owner.byte370 == 2)
            text = master ? UnionMasterTexts[1] : UnionMemberTexts[1];
        else if (owner.byte370 == 8)
            text = UnionKind8Text;
        auto line = GuildLine('<', '>', unionName, text, texts, "0x556ed1");
        if (!line.HasValue())
            return Core::Unexpected(line.Error());
        if (auto put = PutAt(bytes, UnionLineAt, line.Value(), "0x556ed1"); !put.HasValue())
            return put;
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
    auto line = GuildLine('[', ']', NameOf(entry.guildName), post, texts, "0x557024");
    if (!line.HasValue())
        return Core::Unexpected(line.Error());
    // With 0x1e bytes or more, its zero (or its tail) lands in the shop title: with 8 letters and the 19-byte posts
    // (1300, 1302 and 1330) the title ends up empty, and the 1301 (32) leaves its tail there.
    if (auto put = PutAt(bytes, GuildPostAt, line.Value(), "0x557024"); !put.HasValue())
        return put;
    bubble.guild = ReadAt(bytes, UnionLineAt);
    bubble.guildPost = ReadAt(bytes, GuildPostAt);
    bubble.shopTitle = ReadAt(bytes, ShopTitleAt);
    return {};
}

// 0x557256..0x557268 and 0x55735a..0x557366: the new line, split in two from 20 bytes (the head above, at +0x16c, and
// the tail below, at +0x6c, 0x5549a0(text, +0x6c, +0x16c, length)) or whole in the lower one.
void PutTheLine(ChatBubble& bubble, std::string_view text, std::int32_t steps)
{
    if (text.size() >= ChatBubbleSplitBytes)
    {
        Ui::HalvedText halves = Ui::SplitInHalf(text);
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

Core::Expected<void, std::string> AddChatBubble(Kernel::WorldState& world, const ChatBubbleMeasure& measure,
                                                std::string_view name, std::string_view text, std::size_t owner,
                                                ChatBubbleMode mode, std::optional<std::uint8_t> color)
{
    const Kernel::ActorState& speaker = world.Actors()[owner];
    if (!speaker.alive || !speaker.seen)
        return {};
    name = CString(name);
    text = CString(text);
    Kernel::WorldRuntime& runtime = world.Runtime();
    ChatBubbleTable& bubbles = runtime.chat.bubbles;
    // 0x557190..0x5571a6: without a color (−1), the PK; with +0x88 at 4, 0.
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
        // 0x5571ff: the new one belongs to the character before the name is copied.
        bubble->owner = owner;
    }
    bubble->name = std::string(name);
    bubble->color = shown;
    if (auto guild = FillTheGuild(*bubble, speaker, runtime.guild, measure.texts, runtime.social.personalShop, owner);
        !guild.HasValue())
        return guild;
    if (text.empty())
    {
        bubble->nameTime = ownEntry ? ChatBubbleNameSteps : ChatBubbleNewNameSteps;
        return {};
    }
    if (ownEntry)
    {
        // 0x557286..0x5572a4: the live line moves up.
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
        // vtable + 0x94 of the log: 0x45bb20(the entry's name, text, 3, 0).
        Ui::MessageSink sink = MessageSinkOf(runtime, measure.measure, measure.screen);
        auto sound = Ui::AddToTheLog(sink.log, sink.modes, sink.measure, bubble->name, text, Ui::ChatMessage, 0,
                                     sink.noticeSound);
        if (!sound.HasValue())
            return Core::Unexpected(sound.Error());
        if (sound.Value())
            runtime.RequestSound(Audio::SoundId{*sound.Value()});
    }
    PutTheLine(*bubble, text, steps);
    return {};
}

Core::Expected<void, std::string> AddChatBubbleByName(Kernel::WorldState& world, const ChatBubbleMeasure& measure,
                                                      std::string_view name, std::string_view text, ChatBubbleMode mode)
{
    name = CString(name);
    // 0x557560 looks at the first 400, without the 401st.
    constexpr std::size_t Searched = Kernel::ActorPoolSize - 1;
    const std::span<const Kernel::ActorState, Kernel::ActorPoolSize> actors = world.Actors();
    for (const std::uint8_t kind : {PlayerKind, MonsterKind})
    {
        for (std::size_t index = 0; index < Searched; ++index)
        {
            const Kernel::ActorState& actor = actors[index];
            if (actor.alive && actor.byte88 == kind && Actors::ChatNameOf(actor) == name)
                return AddChatBubble(world, measure, name, text, index, mode);
        }
    }
    return {};
}

Core::Expected<void, std::string> StepTheChatBubbles(Kernel::WorldState& world, const ChatBubbleStepInput& input)
{
    Kernel::WorldRuntime& runtime = world.Runtime();
    const std::span<const Kernel::ActorState, Kernel::ActorPoolSize> actors = world.Actors();
    const Kernel::ActorState& hero = world.Hero();
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
        // 0x5576a0..0x5576c9: with the bubble in view (+time1 > 0) the owner's shop sign hides (0x451720, which with
        // the owner at NULL finds nothing); with an owner and +time1 at 0, it shows (0x4516d0).
        Social::PersonalShopState& shops = runtime.social.personalShop;
        if (bubble.time1 > 0 && bubble.owner)
            PersonalShop::HideShopTitle(shops, *bubble.owner);
        if (bubble.owner && bubble.time1 == 0)
            PersonalShop::ShowShopTitle(shops, *bubble.owner);
        if (input.screen.width <= 0 || input.screen.height <= 0)
            continue;
        // 0x557722..0x557738: the width and the height go to 640 × 480 with an unsigned division.
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
        if (CString(bubble.name) == Actors::ChatNameOf(hero) || !runtime.ui.rightClick || !bubble.owner ||
            actors[*bubble.owner].word376 != WhisperableWord376)
            continue;
        // 0x5577a2..0x55780d: the right click is spent, the name goes to the whisper EDIT (0x4694c0(0, 0, 1, name),
        // with 0x6c7b8c at 1) and with strcpy to the buffer and its length, and 0x19 sounds. Then the other bubbles.
        runtime.ui.rightClick = false;
        Ui::SetTheChatFields(runtime.chat.input, std::nullopt, CString(bubble.name));
        Ui::PutTheWhisperName(runtime.chat.input, CString(bubble.name));
        runtime.RequestSound(Ui::ChatButtonSound);
    }
    return {};
}
} // namespace Client::Chat
