#include "Events/Crywolf/CrywolfContract.h"

#include "Core/Expected.h"
#include "Maps/MapId.h"
#include "Protocol/Events/Events.h"
#include "Kernel/MouseState.h"
#include "Kernel/ActorState.h"
#include "Actors/WalkPath.h"
#include "Kernel/WorldRuntime.h"
#include "Ui/GameText.h"
#include "Crt/OriginalFormat.h"
#include "Kernel/CharacterClass.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Events::Crywolf
{
namespace
{
using Result = Core::Expected<void, std::string>;

// 0x4303c0: the map 0x6c9dd0 is 0x22.
using Maps::CrywolfMap;

// 0x435b5c and 0x435b6c: the two windows.
constexpr std::uint8_t QuestionWindow = 1;
constexpr std::uint8_t NoticeWindow = 2;
// [0x774bf04 + 300 · n] and [0x77500a4]: 1950 + n and the format of the 0x38.
constexpr std::size_t FirstMessageText = 1950;
constexpr std::size_t OfferText = 2006;
// 0x435b42: the contracts left, the altar's low nibble; 0x4365ac, the high one at 1, contracted.
constexpr std::uint8_t AltarLowBits = 0x0f;
constexpr std::uint8_t AltarHighBits = 0xf0;
constexpr std::uint8_t AltarContracted = 0x10;

// 0x435c5c..0x436220: the pressed buttons (0x723bdc) and the boxes, without the edges.
constexpr std::int32_t RefusePushed = 1;
constexpr std::int32_t AcceptPushed = 2;
constexpr std::int32_t ConfirmPushed = 3;
struct Box
{
    std::int32_t left;
    std::int32_t right;
    std::int32_t top;
    std::int32_t bottom;
};
constexpr Box RefuseBox{0x14a, 0x180, 0xfa, 0x118};
constexpr Box AcceptBox{0xfa, 0x130, 0xfa, 0x118};
constexpr Box ConfirmBox{0x122, 0x158, 0xfa, 0x118};
// 0x435cfe..0x435d16: the pets with which the Yes does not send.
constexpr std::array<std::uint16_t, 3> RefusedPets{0x1c14, 0x1c15, 0x1c37};

// 0x567b36..0x567b5b: the altars in the viewport and the elf's class.
constexpr std::uint16_t FirstAltarType = Actors::FirstCrywolfAltarType;
constexpr std::uint16_t LastAltarType = Actors::LastCrywolfAltarType;

bool Inside(const Box& box, std::int32_t x, std::int32_t y)
{
    return x > box.left && x < box.right && y > box.top && y < box.bottom;
}

// strcpy and wsprintfA: the text and its '\0'; what was there stays behind.
void CopyTheLine(std::array<char, 200>& line, std::string_view text)
{
    std::copy(text.begin(), text.end(), line.begin());
    line[text.size()] = '\0';
}

// 0x4365a0: the altar's high nibble at 1.
bool Contracted(const Kernel::CrywolfInterface& ui, std::size_t altar)
{
    return (ui.altars[altar] & AltarHighBits) == AltarContracted;
}
} // namespace

bool InTheCrywolfDefense(const Kernel::WorldRuntime& runtime)
{
    return Protocol::Events::CrywolfIsReadyOrStarted(runtime.crywolf.stage) && runtime.map == CrywolfMap;
}

Result ShowTheContractMessage(Kernel::WorldRuntime& runtime, const Tables::TextTable* texts,
                              const ContractMessage& message)
{
    if (!InTheCrywolfDefense(runtime) || runtime.ui.logoutRequested)
        return {};
    Kernel::CrywolfInterface& ui = runtime.crywolf.ui;
    if (message.line < 0 || static_cast<std::size_t>(message.line) >= ui.contractLines.size())
        return Core::Unexpected(std::format("0x435b00: el renglón {} escribe fuera de 0x723a48", message.line));
    std::array<char, 200>& line = ui.contractLines[static_cast<std::size_t>(message.line)];
    std::string text;
    if (message.message == AltarOfferMessage)
    {
        // 0x435b33..0x435b5f: wsprintfA(line, [0x77500a4], altar & 0xf) and window 1, without going through window 2.
        // With the 0x38 only 0x567ba4 calls it, with the target's type − 0x204 already in 0..4: the guard cannot be
        // reached from a packet.
        if (message.altar < 0 || static_cast<std::size_t>(message.altar) >= ui.altars.size())
            return Core::Unexpected(std::format("0x435b39: el altar {} lee fuera de 0x6c73cc", message.altar));
        const std::optional<std::string_view> format = Ui::TextAt(texts, OfferText);
        if (!format)
            return Core::Unexpected(std::format("0x435b56: sin el texto {} de Text.bmd", OfferText));
        const std::array<Crt::FormatArgument, 1> arguments{
            static_cast<std::int32_t>(ui.altars[static_cast<std::size_t>(message.altar)] & AltarLowBits)};
        auto formatted = Crt::FormatOriginal(Crt::FormatFunction::Wsprintf, *format, arguments, "0x435b56");
        if (!formatted.HasValue())
            return Core::Unexpected(std::move(formatted).Error());
        text = std::move(formatted).Value();
    }
    else
    {
        // 0x435b61..0x435bac: strcpy of text 1950 + n; window 2.
        const std::int64_t index = static_cast<std::int64_t>(FirstMessageText) + message.message;
        const std::optional<std::string_view> found =
            index < 0 ? std::nullopt : Ui::TextAt(texts, static_cast<std::size_t>(index));
        if (!found)
            return Core::Unexpected(std::format("0x435b8f: sin el texto {} de Text.bmd", index));
        text = std::string(*found);
        ui.byte723a44 = NoticeWindow;
    }
    if (text.size() >= line.size())
        return Core::Unexpected(std::format("0x435b00: \"{}\" ocupa {} bytes con su '\\0' y el renglón {} de 0x723a48 "
                                            "tiene {}",
                                            text, text.size() + 1, message.line, line.size()));
    CopyTheLine(line, text);
    if (message.message == AltarOfferMessage || message.message == AltarQuestionMessage)
        ui.byte723a44 = QuestionWindow;
    // 0x435bb9..0x435bc8.
    if (message.line == 0)
        ui.contractLines[1][0] = '\0';
    ui.dwords723bdc[1] = message.value;
    return {};
}

bool SetTheAltar(Kernel::WorldRuntime& runtime, std::int32_t altar, std::uint8_t value)
{
    if (!InTheCrywolfDefense(runtime))
        return true;
    Kernel::CrywolfInterface& ui = runtime.crywolf.ui;
    // 0x435aeb: no bound in the original (Q-C1347).
    if (altar < 0 || static_cast<std::size_t>(altar) >= ui.altars.size())
        return false;
    ui.altars[static_cast<std::size_t>(altar)] = value;
    return true;
}

Result StepTheContractWindow(const ContractWindowContext& context)
{
    Kernel::WorldRuntime& runtime = context.runtime;
    Kernel::CrywolfInterface& ui = runtime.crywolf.ui;
    Kernel::MouseState& mouse = runtime.mouse;
    const std::int32_t x = context.mouseX;
    const std::int32_t y = context.mouseY;
    if (ui.byte723a44 == QuestionWindow)
    {
        // 0x435c6b..0x435cb4: the No.
        if (Inside(RefuseBox, x, y) && mouse.leftHeld)
        {
            ui.dwords723bdc[0] = RefusePushed;
            if (auto shown = ShowTheContractMessage(runtime, context.texts, {.message = ContractRefusedMessage});
                !shown.HasValue())
                return shown;
        }
        // 0x435cb7..0x435cec: the Yes; with the mouse over it and the button up, nothing.
        if (Inside(AcceptBox, x, y))
        {
            if (!mouse.leftHeld)
                return {};
            if (std::ranges::find(RefusedPets, context.heroPet) != RefusedPets.end())
            {
                // 0x4361a4.
                if (auto shown = ShowTheContractMessage(runtime, context.texts, {.message = ContractWithPetMessage});
                    !shown.HasValue())
                    return shown;
            }
            else
            {
                // 0x435d1c..0x4361a2.
                ui.dwords723bdc[0] = AcceptPushed;
                if (!context.send)
                    return Core::Unexpected(std::string("0x435d26: el C1 06 BD 03 sin el send() del paso"));
                context.send(Protocol::Events::WriteCrywolfContract(ui.dwords723bdc[1]));
            }
        }
        // 0x4361b4..0x4361c6.
        if (mouse.leftHeld)
        {
            mouse.leftHeld = false;
            mouse.pendingClick = false;
        }
        return {};
    }
    // 0x4361dc..0x436216: the OK.
    if (ui.byte723a44 == NoticeWindow && Inside(ConfirmBox, x, y) && mouse.leftHeld)
    {
        mouse.leftHeld = false;
        mouse.pendingClick = false;
        ui.dwords723bdc[0] = ConfirmPushed;
    }
    return {};
}

Core::Expected<AltarClick, std::string> ClickTheCrywolfAltar(Kernel::WorldRuntime& runtime,
                                                             const Tables::TextTable* texts,
                                                             const Kernel::ActorState& hero,
                                                             const Kernel::ActorState& target)
{
    if (!InTheCrywolfDefense(runtime))
        return AltarClick::NotAnAltar;
    if (target.type < FirstAltarType || target.type > LastAltarType)
        return AltarClick::NotAnAltar;
    if (Kernel::BaseClassOf(hero.characterClass) != Kernel::CharacterClass::Elf)
        return AltarClick::Talk;
    const std::size_t altar = target.type - FirstAltarType;
    const Kernel::CrywolfInterface& ui = runtime.crywolf.ui;
    Result shown;
    if (Contracted(ui, altar))
    {
        // 0x567bed..0x567c23.
        shown = ShowTheContractMessage(runtime, texts, {.message = AltarTakenMessage});
    }
    else if ((ui.altars[altar] & AltarLowBits) == 0)
    {
        // 0x567bd8..0x567be8.
        shown = ShowTheContractMessage(runtime, texts, {.message = AltarSpentMessage});
    }
    else
    {
        // 0x567b83..0x567bd3: the key with MOVSX.
        const std::int32_t key = static_cast<std::int16_t>(target.key);
        shown = ShowTheContractMessage(
            runtime, texts,
            {.message = AltarOfferMessage, .line = 0, .value = key, .altar = static_cast<std::int32_t>(altar)});
        if (shown.HasValue())
            shown = ShowTheContractMessage(runtime, texts, {.message = AltarQuestionMessage, .line = 1, .value = key});
    }
    if (!shown.HasValue())
        return Core::Unexpected(std::move(shown).Error());
    return AltarClick::Contract;
}
} // namespace Client::Events::Crywolf
