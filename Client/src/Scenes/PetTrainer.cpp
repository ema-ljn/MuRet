#include "Scenes/PetTrainer.h"

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Protocol/Connection/Logout.h"
#include "Protocol/NpcWindows.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GameUi.h"
#include "Scenes/HudItems.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/ItemPrice.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameUiState.h"
#include "UI/PanelRegistry.h"
#include "World/ItemState.h"

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
// 0x724580: los modos de 0x44b4e0.
constexpr std::int32_t SummaryMode = 1;
constexpr std::int32_t RecoveryMode = 2;
constexpr std::int32_t BoxMode = 3;

// 0x44b941..0x44b95e: la ventana, cuyo mouse es de la UI (0x775857c).
constexpr std::int32_t WindowLeft = 0xd5;
constexpr std::int32_t WindowRight = 0x1aa;
constexpr std::int32_t WindowTop = 0x5a;
constexpr std::int32_t WindowBottom = 0x122;
// 0x44b96d..0x44b99a: los botones, de 0x78 × 0x16 desde (0x103, 0x96), cada 0x3c hasta 0x10e (dos).
constexpr std::int32_t ButtonLeft = 0x103;
constexpr std::int32_t ButtonRight = 0x17b;
constexpr std::int32_t FirstButtonTop = 0x96;
constexpr std::int32_t ButtonHeight = 0x16;
constexpr std::int32_t ButtonStep = 0x3c;
constexpr std::int32_t ButtonsEnd = 0x10e;
// 0x44b99c..0x44b9be: el botón de cerrar.
constexpr std::int32_t CloseLeft = 0x189;
constexpr std::int32_t CloseRight = 0x1a1;
constexpr std::int32_t CloseTop = 0x109;
constexpr std::int32_t CloseBottom = 0x121;

// 0x44b2b3 y 0x44b230: la mascota que mira 0x44b190 por botón, en su lugar del equipo (+0xdac y +0xbb4 del registro
// propio), que es también el Position del 34 (0x44c121 y 0x44bb19).
struct Pet
{
    std::uint16_t type;
    std::uint8_t slot;
};
constexpr std::array<Pet, 2> Pets{{{.type = 0x1a04, .slot = 8}, {.type = 0x1a05, .slot = 1}}};

// 0x44b2a5: sin la mascota, −1.
constexpr std::uint32_t NoPet = 0xffffffff;
// El rand() del relleno (0x44bd49 y 0x44c351) y el del serial (0x40b3f0 y 0x40b0a0) del C3.
constexpr std::uint32_t C3SendRandoms = 2;

// [x, right) × [y, bottom), con signo, como los JL y JGE del original.
bool InRect(const GameUiContext& context, std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom)
{
    return context.mouseX >= left && context.mouseX < right && context.mouseY >= top && context.mouseY < bottom;
}

Core::Expected<void, std::string> Send(const GameUiContext& context, Protocol::Outgoing packet, std::string_view what)
{
    if (!context.sendNpcWindow)
        return Core::Unexpected(std::format("{} sin la sesión", what));
    return context.sendNpcWindow(std::move(packet));
}

// 0x4499b0. La caja de 0x723c80 (0x449c10) está vacía en MuRet: la vacían 0x5bbe10 al entrar a la selección y 0x449940
// al abrir el entrenador, y lo que la llena (el 31 con la grilla 5, 0x4c5fde; el 86 con 100, 0x4da373; soltar en ella,
// 0x44aca9) corta antes. Con algo, el original avisa con el texto 1211 y no cierra.
Core::Expected<void, std::string> CloseTheTrainer(const GameUiContext& context)
{
    UI::DelegatedPanels& delegated = context.world.Runtime().panels.delegated;
    delegated.field724584 = 0;
    delegated.trainer = 0;
    return Send(context, Protocol::Connection::WriteWindowClose(), "0x449ad1: el C1 03 31");
}

// 0x44b190(botón + 1, buffer): 0x78abb10 en 0 y, con el registro propio abierto, el precio de recuperar la mascota del
// botón: −1 sin ella; si no, 0x5b13e0 sobre 0x54d0f0 en el modo 2 con la durabilidad debajo de la máxima (0x5b1ba0), o
// 0, y el doble con la durabilidad en 0. Con un precio, lo vuelve a abrir para el texto. Los textos que deja en el
// buffer (1229 a 1232, con 0x5b1130) no los lee 0x44b910 (Q-C949): MuRet no los arma.
Core::Expected<std::uint32_t, std::string> PriceTheRecovery(const GameUiContext& context, const Pet& pet)
{
    WorldRuntime& runtime = context.world.Runtime();
    runtime.field78abb10 = 0;
    const World::ItemState item = context.world.OwnCharacter().equipment[pet.slot];
    std::optional<RepairPrice> repair{};
    {
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                Render::Models::OwnRecordCloseRandoms);
        if (item.id != pet.type)
            return NoPet;
        const Data::Tables::ItemTable* items = context.inventory != nullptr ? context.inventory->items : nullptr;
        if (items == nullptr)
            return Core::Unexpected(std::string("0x44b31a: el precio de la mascota sin Item.bmd ([0x7758550])"));
        const auto record = items->Record(item.id);
        if (!record)
            return Core::Unexpected(std::format("0x44b31f: el tipo {:#x} lee fuera de Item.bmd", item.id));
        const std::int32_t level = static_cast<std::int32_t>((item.scalar1Low >> 3) & 0xfu);
        const std::uint16_t maximum = MaxDurability(item, *record, level);
        if (item.durability < maximum)
        {
            const PetPriceLevels pets{.raven = runtime.ravenLevel, .horse = runtime.darkHorseLevel};
            auto price = ItemPrice(item, *items, RepairPriceMode, pets);
            if (!price.HasValue())
                return Core::Unexpected(std::move(price).Error());
            repair = RepairPriceOf(price.Value(), item.durability, maximum, runtime.field78abb10);
        }
    }
    std::uint32_t price = repair ? static_cast<std::uint32_t>(repair->gold) : 0u;
    // 0x44b366..0x44b36d: el doble, en 32 bits.
    if (item.durability == 0)
        price += price;
    if (price == 0)
        return 0u;
    // 0x44b3c6..0x44b482: el texto con el zen que falta o el que hace falta, y el cierre de 0x40f370.
    const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                            Render::Models::OwnRecordCloseRandoms);
    return price;
}
} // namespace

Core::Expected<void, std::string> StepTheTrainer(const GameUiContext& context)
{
    switch (context.world.Runtime().panels.delegated.trainer)
    {
    case SummaryMode:
        return Core::Unexpected(
            std::string("0x44b501: el paso del entrenador con 0x724580 en 1 (0x44b540) es de la fase 3 del hito 6 "
                        "(08 P-C198)"));
    case RecoveryMode:
        return StepThePetRecovery(context);
    case BoxMode:
        return Core::Unexpected(
            std::string("0x44b4f7: el paso de la caja del entrenador (0x44d010, 0x723c80) es de la fase 3 del hito 6 "
                        "(08 P-C198)"));
    default:
        return {};
    }
}

Core::Expected<void, std::string> StepThePetRecovery(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    GameMovementState& movement = runtime.movement;
    if (InRect(context, WindowLeft, WindowTop, WindowRight, WindowBottom))
        runtime.ui.mouseOnUi = 1;
    std::optional<std::size_t> pressed{};
    std::int32_t top = FirstButtonTop;
    for (std::size_t button = 0; top < ButtonsEnd; ++button, top += ButtonStep)
    {
        if (InRect(context, ButtonLeft, top, ButtonRight, top + ButtonHeight) && movement.pendingClick)
        {
            pressed = button;
            break;
        }
    }
    if (!pressed)
    {
        if (!InRect(context, CloseLeft, CloseTop, CloseRight, CloseBottom) || !movement.pendingClick)
            return {};
        movement.leftHeld = false;
        movement.pendingClick = false;
        return CloseTheTrainer(context);
    }
    movement.leftHeld = false;
    movement.pendingClick = false;
    const Pet& pet = Pets[*pressed];
    auto priced = PriceTheRecovery(context, pet);
    if (!priced.HasValue())
        return Core::Unexpected(std::move(priced).Error());
    const std::uint32_t price = priced.Value();
    // 0x44ba0e..0x44ba84 y 0x44c638..: el registro propio abierto hasta el final del paso.
    const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                            Render::Models::OwnRecordCloseRandoms);
    if (context.world.OwnCharacter().zen < price || price == NoPet)
        return {};
    // 0x44ba9d..0x44c627: C1 05 34 <lugar> <el byte bajo del precio> en C3 (Q-C948).
    context.random.Discard(C3SendRandoms);
    if (auto sent = Send(context, Protocol::NpcWindows::WriteRepair(pet.slot, static_cast<std::uint8_t>(price & 0xffu)),
                         "0x44c3c4: el C1 05 34 del entrenador");
        !sent.HasValue())
        return sent;
    // 0x44c62e: 0x4499b0. 0x44c633: 0x421d90 vacía 0x723918..0x72395f, la caché del tooltip de las mascotas que lee
    // 0x422990, que corta antes (P-C126): MuRet no la tiene.
    return CloseTheTrainer(context);
}
} // namespace Client::Scenes
