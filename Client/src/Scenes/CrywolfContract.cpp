#include "Scenes/CrywolfContract.h"

#include "Core/Expected.h"
#include "Protocol/Events.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameText.h"
#include "UI/OriginalFormat.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Scenes
{
namespace
{
using Result = Core::Expected<void, std::string>;

// 0x435a00 (la etapa 0x723c05 en 3 o 4) y 0x4303c0 (el mapa 0x6c9dd0 en 0x22).
constexpr std::uint8_t DefenseStage = 3;
constexpr std::uint8_t WarningStage = 4;
constexpr std::int32_t CrywolfMap = 0x22;

// 0x435b5c y 0x435b6c: las dos ventanas.
constexpr std::uint8_t QuestionWindow = 1;
constexpr std::uint8_t NoticeWindow = 2;
// [0x774bf04 + 300 · n] y [0x77500a4]: 1950 + n y el formato de 0x38.
constexpr std::size_t FirstMessageText = 1950;
constexpr std::size_t OfferText = 2006;
// 0x435b42: los contratos que quedan, el nibble bajo del altar; 0x4365ac, el alto con 1, contratado.
constexpr std::uint8_t AltarLowBits = 0x0f;
constexpr std::uint8_t AltarHighBits = 0xf0;
constexpr std::uint8_t AltarContracted = 0x10;

// 0x435c5c..0x436220: los botones apretados (0x723bdc) y las cajas, sin los bordes.
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
// 0x435cfe..0x435d16: las mascotas con las que el Sí no manda.
constexpr std::array<std::uint16_t, 3> RefusedPets{0x1c14, 0x1c15, 0x1c37};

// 0x567b36..0x567b5b: los altares en el viewport y la clase de la elfa.
constexpr std::uint16_t FirstAltarType = 0x204;
constexpr std::uint16_t LastAltarType = 0x208;
constexpr std::uint8_t ClassBits = 7;
constexpr std::uint8_t ElfClass = 2;

bool InTheDefense(const WorldRuntime& runtime)
{
    const std::uint8_t stage = runtime.crywolf.stage;
    return (stage == DefenseStage || stage == WarningStage) && runtime.map == CrywolfMap;
}

bool Inside(const Box& box, std::int32_t x, std::int32_t y)
{
    return x > box.left && x < box.right && y > box.top && y < box.bottom;
}

// strcpy y wsprintfA: el texto y su '\0'; detrás queda lo que había.
void CopyTheLine(std::array<char, 200>& line, std::string_view text)
{
    std::copy(text.begin(), text.end(), line.begin());
    line[text.size()] = '\0';
}

// 0x4365a0: el nibble alto del altar en 1.
bool Contracted(const CrywolfInterface& ui, std::size_t altar)
{
    return (ui.altars[altar] & AltarHighBits) == AltarContracted;
}
} // namespace

Result ShowTheContractMessage(WorldRuntime& runtime, const Data::Text::TextTable* texts, const ContractMessage& message)
{
    if (!InTheDefense(runtime) || runtime.ui.logoutRequested)
        return {};
    CrywolfInterface& ui = runtime.crywolf.ui;
    if (message.line < 0 || static_cast<std::size_t>(message.line) >= ui.contractLines.size())
        return Core::Unexpected(std::format("0x435b00: el renglón {} escribe fuera de 0x723a48", message.line));
    std::array<char, 200>& line = ui.contractLines[static_cast<std::size_t>(message.line)];
    std::string text;
    if (message.message == AltarOfferMessage)
    {
        // 0x435b33..0x435b5f: wsprintfA(renglón, [0x77500a4], altar & 0xf) y la ventana 1, sin pasar por la 2.
        if (message.altar < 0 || static_cast<std::size_t>(message.altar) >= ui.altars.size())
            return Core::Unexpected(std::format("0x435b39: el altar {} lee fuera de 0x6c73cc", message.altar));
        const std::optional<std::string_view> format = UI::TextAt(texts, OfferText);
        if (!format)
            return Core::Unexpected(std::format("0x435b56: sin el texto {} de Text.bmd", OfferText));
        const std::array<UI::FormatArgument, 1> arguments{
            static_cast<std::int32_t>(ui.altars[static_cast<std::size_t>(message.altar)] & AltarLowBits)};
        auto formatted = UI::FormatOriginal(UI::FormatFunction::Wsprintf, *format, arguments, "0x435b56");
        if (!formatted.HasValue())
            return Core::Unexpected(std::move(formatted).Error());
        text = std::move(formatted).Value();
    }
    else
    {
        // 0x435b61..0x435bac: strcpy del texto 1950 + n; la ventana 2.
        const std::int64_t index = static_cast<std::int64_t>(FirstMessageText) + message.message;
        const std::optional<std::string_view> found =
            index < 0 ? std::nullopt : UI::TextAt(texts, static_cast<std::size_t>(index));
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

Result SetTheAltar(WorldRuntime& runtime, std::int32_t altar, std::uint8_t value)
{
    if (!InTheDefense(runtime))
        return {};
    CrywolfInterface& ui = runtime.crywolf.ui;
    if (altar < 0 || static_cast<std::size_t>(altar) >= ui.altars.size())
        return Core::Unexpected(std::format("0x435aeb: el altar {} escribe fuera de 0x6c73cc..0x6c73d0", altar));
    ui.altars[static_cast<std::size_t>(altar)] = value;
    return {};
}

Result StepTheContractWindow(const ContractWindowContext& context)
{
    WorldRuntime& runtime = context.runtime;
    CrywolfInterface& ui = runtime.crywolf.ui;
    GameMovementState& movement = runtime.movement;
    const std::int32_t x = context.mouseX;
    const std::int32_t y = context.mouseY;
    if (ui.byte723a44 == QuestionWindow)
    {
        // 0x435c6b..0x435cb4: el No.
        if (Inside(RefuseBox, x, y) && movement.leftHeld)
        {
            ui.dwords723bdc[0] = RefusePushed;
            if (auto shown = ShowTheContractMessage(runtime, context.texts, {.message = ContractRefusedMessage});
                !shown.HasValue())
                return shown;
        }
        // 0x435cb7..0x435cec: el Sí; con el mouse encima y el botón arriba, nada.
        if (Inside(AcceptBox, x, y))
        {
            if (!movement.leftHeld)
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
        if (movement.leftHeld)
        {
            movement.leftHeld = false;
            movement.pendingClick = false;
        }
        return {};
    }
    // 0x4361dc..0x436216: el OK.
    if (ui.byte723a44 == NoticeWindow && Inside(ConfirmBox, x, y) && movement.leftHeld)
    {
        movement.leftHeld = false;
        movement.pendingClick = false;
        ui.dwords723bdc[0] = ConfirmPushed;
    }
    return {};
}

Core::Expected<AltarClick, std::string> ClickTheCrywolfAltar(WorldRuntime& runtime, const Data::Text::TextTable* texts,
                                                             const ActorState& hero, const ActorState& target)
{
    if (!InTheDefense(runtime))
        return AltarClick::NotAnAltar;
    if (target.type < FirstAltarType || target.type > LastAltarType)
        return AltarClick::NotAnAltar;
    if ((hero.characterClass & ClassBits) != ElfClass)
        return AltarClick::Talk;
    const std::size_t altar = target.type - FirstAltarType;
    const CrywolfInterface& ui = runtime.crywolf.ui;
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
        // 0x567b83..0x567bd3: la clave con MOVSX.
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

bool CrywolfAltarGoal(const WorldState& world)
{
    // 0x4eac0a..0x4eac1a.
    const WorldRuntime& runtime = world.Runtime();
    if (!InTheDefense(runtime))
        return false;
    // 0x4eac1c..0x4eac3f: [0x743a16c] + 0x438 · [0x6c8e54] + 2, con CMP de 16 bits con signo.
    const std::int32_t chosen = runtime.movement.chosenNpc;
    const auto actors = world.Actors();
    if (chosen < 0 || static_cast<std::size_t>(chosen) >= actors.size())
        return false;
    const auto type = static_cast<std::int16_t>(actors[static_cast<std::size_t>(chosen)].type);
    return type >= static_cast<std::int16_t>(FirstAltarType) && type <= static_cast<std::int16_t>(LastAltarType);
}
} // namespace Client::Scenes
