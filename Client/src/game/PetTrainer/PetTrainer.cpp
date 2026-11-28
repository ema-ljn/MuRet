#include "PetTrainer/PetTrainer.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/ItemTable.h"
#include "Protocol/Session/Logout.h"
#include "Protocol/Npc/NpcWindows.h"
#include "Protocol/Outgoing.h"
#include "Models/LogicalAccess.h"
#include "ChaosMix/ChaosMix.h"
#include "Hud/GameUi.h"
#include "Items/HudItems.h"
#include "Items/InventoryMoves.h"
#include "Items/ItemPrice.h"
#include "Npc/NpcWindowState.h"
#include "Npc/NpcWindows.h"
#include "Kernel/MouseState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameUiState.h"
#include "Ui/MessageArea.h"
#include "Ui/PanelRegistry.h"
#include "Kernel/ItemState.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::PetTrainer
{
namespace
{
// 0x724580 en 4: 0x44b4e0 y 0x44b510 no hacen nada (*estático*: nadie lo escribe así).
constexpr std::int32_t IdleMode = 4;

// Una ventana o un botón: [left, right) × [top, bottom), con signo, como los JL y JGE del original.
struct Rect
{
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;
};

// 0x44b54f..0x44b56c y 0x44b941..0x44b95e: la ventana de los modos 1 y 2, cuyo mouse es de la UI (0x775857c).
constexpr Rect SmallWindow{.left = 0xd5, .top = 0x5a, .right = 0x1aa, .bottom = 0x122};
// 0x44d03b..0x44d05c: la del modo 3.
constexpr Rect BoxWindow{.left = 0x104, .top = 0, .right = 0x1c2, .bottom = 0x1b1};
// 0x44b5aa..0x44b5c8 y 0x44b99c..0x44b9be: el botón de cerrar de los modos 1 y 2.
constexpr Rect SmallClose{.left = 0x189, .top = 0x109, .right = 0x1a1, .bottom = 0x121};
// 0x44d063..0x44d08e y 0x44d61c..0x44d63b: el botón de mezclar y el de cerrar del modo 3.
constexpr Rect MixButton{.left = 0x14f, .top = 0x12c, .right = 0x17b, .bottom = 0x14c};
constexpr Rect BoxClose{.left = 0x118, .top = 0x18b, .right = 0x130, .bottom = 0x1a3};
// Los botones de los modos 1 y 2, de 0x78 × 0x16: el 1 desde 0xb4 cada 0x1e hasta 0x10e (tres, 0x44b57b..0x44b5a8),
// el 2 desde 0x96 cada 0x3c (dos, 0x44b96d..0x44b99a).
constexpr std::int32_t ButtonLeft = 0x103;
constexpr std::int32_t ButtonRight = 0x17b;
constexpr std::int32_t ButtonHeight = 0x16;
constexpr std::int32_t ButtonsEnd = 0x10e;
constexpr std::int32_t MenuFirstTop = 0xb4;
constexpr std::int32_t MenuStep = 0x1e;
constexpr std::int32_t RecoveryFirstTop = 0x96;
constexpr std::int32_t RecoveryStep = 0x3c;
// 0x44b5e3..0x44b605: los botones del menú.
constexpr std::size_t RecoveryButton = 0;
constexpr std::size_t BoxButton = 1;

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

// 0x449bc7 y 0x658de3: el texto 1211 de Text.bmd (0x7715d00), de error, con la caja ocupada.
constexpr std::size_t BoxNotEmptyText = 1211;

// 0x44ae40..0x44aea6: lo que cuenta 0x44ae10.
constexpr std::uint16_t EmptyCell = 0xffff;
constexpr std::int16_t BlessJewel = 0x1c0d;
constexpr std::int16_t SoulJewel = 0x1c0e;
constexpr std::int16_t CreationJewel = 0x1c16;
constexpr std::int16_t ChaosJewel = 0x180f;
constexpr std::int16_t PetSpirit = 0x1a1f;
// 0x44ae73..0x44ae84: el nivel del espíritu, 0 el del caballo y 1 el del cuervo.
constexpr std::int32_t HorseSpiritLevel = 0;
constexpr std::int32_t RavenSpiritLevel = 1;
constexpr std::int32_t LevelShift = 3;
constexpr std::int32_t LevelMask = 0xf;
// 0x44aeb1..0x44af00: las dos mezclas.
constexpr std::uint8_t HorseMix = 1;
constexpr std::uint8_t RavenMix = 2;
constexpr std::int32_t HorseJewels = 5;
constexpr std::int32_t RavenJewels = 2;
// 0x6b8c68: el precio de cada mezcla, desde la 0.
constexpr std::array<std::uint32_t, 3> MixPrices{0, 5'000'000, 1'000'000};
// 0x44d0c7..0x44d22b y 0x44d379..0x44d4df: el byte del 86 de cada mezcla.
constexpr std::uint8_t HorseMixKind = 0x0d;
constexpr std::uint8_t RavenMixKind = 0x0e;

bool Inside(const Hud::GameUiContext& context, const Rect& rect)
{
    return context.mouseX >= rect.left && context.mouseX < rect.right && context.mouseY >= rect.top &&
           context.mouseY < rect.bottom;
}

// 0x7d36874 y 0x7d366d4 en 0.
void ConsumeTheClick(Kernel::MouseState& mouse)
{
    mouse.leftHeld = false;
    mouse.pendingClick = false;
}

// El botón de `count` desde `firstTop` cada `step` bajo el mouse con el clic, como lo recorren 0x44b580 y 0x44b972.
std::optional<std::size_t> PressedButton(const Hud::GameUiContext& context, std::int32_t firstTop, std::int32_t step)
{
    if (!context.world.Runtime().mouse.pendingClick)
        return std::nullopt;
    std::size_t button = 0;
    for (std::int32_t top = firstTop; top < ButtonsEnd; top += step, ++button)
    {
        if (Inside(context, Rect{.left = ButtonLeft, .top = top, .right = ButtonRight, .bottom = top + ButtonHeight}))
            return button;
    }
    return std::nullopt;
}

Core::Expected<void, std::string> Send(const Npc::NpcWindowSender& send, Protocol::Outgoing packet,
                                       std::string_view what)
{
    if (!send)
        return Core::Unexpected(std::format("{} sin la sesión", what));
    return send(std::move(packet));
}

// 0x555190(0x6e57a4, el texto 1211, 2).
Core::Expected<void, std::string> WarnTheBoxIsNotEmpty(const Items::InventoryServices* services, std::string_view where)
{
    if (services == nullptr || !services->systemMessage)
        return Core::Unexpected(std::format("{}: 0x555190 sin el chat", where));
    return services->systemMessage(BoxNotEmptyText, Ui::ErrorMessage);
}

// El primer acceso de 0x44b190 (0x44b197..0x44b3be): el precio sin el doble y el texto de 0x5b13e0, o −1 sin la
// mascota. Con la durabilidad en la máxima o más, 0.
Core::Expected<std::optional<Items::RepairPrice>, std::string> PriceThePet(const PetQuoteContext& context,
                                                                           const Pet& pet, bool& found)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const Kernel::ItemState& item = context.world.OwnCharacter().equipment[pet.slot];
    const Models::LogicalAccess own(context.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
    found = item.id == pet.type;
    if (!found)
        return std::optional<Items::RepairPrice>{};
    if (context.items == nullptr)
        return Core::Unexpected(std::string("0x44b31a: el precio de la mascota sin Item.bmd ([0x7758550])"));
    const auto record = context.items->Record(item.id);
    if (!record)
        return Core::Unexpected(std::format("0x44b31f: el tipo {:#x} lee fuera de Item.bmd", item.id));
    const std::int32_t level = static_cast<std::int32_t>((item.scalar1Low >> LevelShift) & LevelMask);
    const std::uint16_t maximum = Items::MaxDurability(item, *record, level);
    if (item.durability >= maximum)
        return std::optional<Items::RepairPrice>{};
    const Items::PetPriceLevels pets{.raven = runtime.pets[Kernel::RavenPetStats].level,
                                     .horse = runtime.pets[Kernel::DarkHorsePetStats].level};
    auto price = Items::ItemPrice(item, *context.items, Items::RepairPriceMode, pets);
    if (!price.HasValue())
        return Core::Unexpected(std::move(price).Error());
    // 0x78abb10 ya está en 0 (0x44b1a3).
    return std::optional<Items::RepairPrice>{Items::RepairPriceOf(price.Value(), item.durability, maximum, 0)};
}
} // namespace

void OpenTheTrainer(Kernel::WorldState& world, std::int32_t mode)
{
    Kernel::WorldRuntime& runtime = world.Runtime();
    runtime.panels.delegated.trainer = mode;
    runtime.npcWindows.trainerBox = Npc::EmptyTrainerBox();
}

bool TrainerBoxHasItems(const Kernel::WorldState& world)
{
    return std::ranges::any_of(world.Runtime().npcWindows.trainerBox,
                               [](const Kernel::ItemState& cell) { return cell.id != EmptyCell; });
}

Core::Expected<bool, std::string> CloseTheTrainer(Kernel::WorldState& world, const Items::InventoryServices* services,
                                                  const Npc::NpcWindowSender& send)
{
    if (TrainerBoxHasItems(world))
    {
        if (auto warned = WarnTheBoxIsNotEmpty(services, "0x449bd3"); !warned.HasValue())
            return Core::Unexpected(std::move(warned).Error());
        return false;
    }
    Ui::DelegatedPanels& delegated = world.Runtime().panels.delegated;
    delegated.field724584 = 0;
    delegated.trainer = 0;
    if (auto sent = Send(send, Protocol::Session::WriteWindowClose(), "0x449ad1: el C1 03 31"); !sent.HasValue())
        return Core::Unexpected(std::move(sent).Error());
    return true;
}

std::uint32_t TrainerMixPrice(std::uint8_t kind)
{
    return kind < MixPrices.size() ? MixPrices[kind] : 0u;
}

void ScanTheTrainerBox(Kernel::WorldState& world, Crt::CrtRandom& random)
{
    Kernel::WorldRuntime& runtime = world.Runtime();
    Npc::NpcWindowState& npc = runtime.npcWindows;
    runtime.panels.delegated.field724584 = 0;
    npc.trainerZenShort = 0;
    std::int32_t bless = 0;
    std::int32_t soul = 0;
    std::int32_t creation = 0;
    std::int32_t chaos = 0;
    std::int32_t horse = 0;
    std::int32_t raven = 0;
    for (const Kernel::ItemState& cell : npc.trainerBox)
    {
        if (cell.id == EmptyCell || cell.occupied == 0)
            continue;
        const auto type = static_cast<std::int16_t>(cell.id);
        if (type == BlessJewel)
            ++bless;
        else if (type == SoulJewel)
            ++soul;
        else if (type == CreationJewel)
            ++creation;
        else if (type == ChaosJewel)
            ++chaos;
        else if (type == PetSpirit)
        {
            // 0x44ae73: SAR del dword de +4.
            const std::int32_t level = (static_cast<std::int32_t>(cell.scalar1Low) >> LevelShift) & LevelMask;
            horse += level == HorseSpiritLevel ? 1 : 0;
            raven += level == RavenSpiritLevel ? 1 : 0;
        }
        // 0x44aea5: la joya de la vida (0x1c10) suma en ESI, que nadie lee después.
    }
    std::uint8_t kind = 0;
    if (horse == 1 && bless == HorseJewels && soul == bless && creation == 1 && chaos == 1)
        kind = HorseMix;
    else if (raven == 1 && bless == RavenJewels && soul == bless && creation == 1 && chaos == 1)
        kind = RavenMix;
    else
        return;
    runtime.panels.delegated.field724584 = kind;
    // 0x44af07..0x44b185: el registro propio abierto para leer el zen.
    const Models::LogicalAccess own(runtime.ownRecordDepth, random, Models::OwnRecordCloseRandoms);
    if (world.OwnCharacter().zen < MixPrices[kind])
        npc.trainerZenShort = 1;
}

void ForgetThePetTooltipQuery(Kernel::WorldState& world)
{
    // 0x421d90: REP STOSD de 18 DWORD desde 0x723918. La clave 0x723914 y las estadísticas 0x723960 quedan fuera.
    Kernel::PetTooltipQuery& query = world.Runtime().petTooltipQuery;
    query.type = 0;
    query.scalar = 0;
}

Core::Expected<PetRecoveryQuote, std::string> QuoteThePetRecovery(const PetQuoteContext& context, std::size_t button)
{
    if (button >= Pets.size())
        return Core::Unexpected(std::format("0x44b306: 0x44b190 con {} lee la mascota del buffer", button + 1));
    const Pet& pet = Pets[button];
    if (context.writesGlobals)
        context.world.Runtime().field78abb10 = 0;
    bool found = false;
    auto repair = PriceThePet(context, pet, found);
    if (!repair.HasValue())
        return Core::Unexpected(std::move(repair).Error());
    if (!found)
        return PetRecoveryQuote{.price = NoPet, .line = PetQuoteLine::NoPet, .number = {}};
    const std::optional<Items::RepairPrice>& priced = repair.Value();
    std::uint32_t price = priced ? static_cast<std::uint32_t>(priced->gold) : 0u;
    // 0x44b366..0x44b36d: el doble, en 32 bits, con la durabilidad en 0.
    if (context.world.OwnCharacter().equipment[pet.slot].durability == 0)
        price += price;
    if (price == 0)
        return PetRecoveryQuote{.price = 0, .line = PetQuoteLine::Full, .number = {}};
    // 0x44b3c6..0x44b482: el zen contra el precio, con el registro abierto otra vez, y el cierre de 0x40f370.
    const Models::LogicalAccess own(context.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
    const std::uint32_t zen = context.world.OwnCharacter().zen;
    if (zen < price)
        return PetRecoveryQuote{
            .price = price, .line = PetQuoteLine::Short, .number = ChaosMix::ThousandsText(price - zen)};
    // 0x44b465: el buffer de quien llama tiene lo que escribió 0x5b13e0, el precio sin el doble (Q-C955).
    return PetRecoveryQuote{
        .price = price, .line = PetQuoteLine::Cost, .number = priced ? priced->text : std::string{}};
}

Core::Expected<void, std::string> StepTheTrainer(const Hud::GameUiContext& context, Ui::PanelRegistry& panels)
{
    switch (context.world.Runtime().panels.delegated.trainer)
    {
    case TrainerMenuMode:
        return StepTheTrainerMenu(context, panels);
    case TrainerRecoveryMode:
        return StepThePetRecovery(context);
    case TrainerBoxMode:
        return StepTheTrainerBox(context, panels);
    case IdleMode:
    default:
        return {};
    }
}

Core::Expected<void, std::string> StepTheTrainerMenu(const Hud::GameUiContext& context, Ui::PanelRegistry& panels)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (Inside(context, SmallWindow))
        runtime.ui.mouseOnUi = 1;
    const std::optional<std::size_t> pressed = PressedButton(context, MenuFirstTop, MenuStep);
    if (!pressed)
    {
        if (!Inside(context, SmallClose) || !runtime.mouse.pendingClick)
            return {};
        ConsumeTheClick(runtime.mouse);
        auto closed = CloseTheTrainer(context.world, context.inventory, context.sendNpcWindow);
        if (!closed.HasValue())
            return Core::Unexpected(std::move(closed).Error());
        return {};
    }
    ConsumeTheClick(runtime.mouse);
    if (*pressed == RecoveryButton)
    {
        // 0x449960.
        runtime.panels.delegated.trainer = TrainerRecoveryMode;
        return {};
    }
    if (*pressed != BoxButton)
        return {};
    // 0x449970: el modo 3 y 0x6599a0(0x4000000, 0), que con el modo 3 no cierra el entrenador.
    runtime.panels.delegated.trainer = TrainerBoxMode;
    auto opened = panels.Open(Ui::InventoryPanel);
    if (!opened.HasValue())
        return Core::Unexpected(std::move(opened).Error());
    return {};
}

Core::Expected<void, std::string> StepThePetRecovery(const Hud::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Kernel::MouseState& mouse = runtime.mouse;
    if (Inside(context, SmallWindow))
        runtime.ui.mouseOnUi = 1;
    const std::optional<std::size_t> pressed = PressedButton(context, RecoveryFirstTop, RecoveryStep);
    if (!pressed)
    {
        if (!Inside(context, SmallClose) || !mouse.pendingClick)
            return {};
        ConsumeTheClick(mouse);
        auto closed = CloseTheTrainer(context.world, context.inventory, context.sendNpcWindow);
        if (!closed.HasValue())
            return Core::Unexpected(std::move(closed).Error());
        return {};
    }
    ConsumeTheClick(mouse);
    const Pet& pet = Pets[*pressed];
    auto quoted =
        QuoteThePetRecovery(PetQuoteContext{.world = context.world,
                                            .random = context.random,
                                            .ownRecordDepth = runtime.ownRecordDepth,
                                            .items = context.inventory != nullptr ? context.inventory->items : nullptr},
                            *pressed);
    if (!quoted.HasValue())
        return Core::Unexpected(std::move(quoted).Error());
    const std::uint32_t price = quoted.Value().price;
    // 0x44ba0e..0x44ba84 y 0x44c638..: el registro propio abierto hasta el final del paso.
    const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
    if (context.world.OwnCharacter().zen < price || price == NoPet)
        return {};
    // 0x44ba9d..0x44c627: C1 05 34 <lugar> <el byte bajo del precio> en C3 (Q-C948).
    context.random.Discard(C3SendRandoms);
    if (auto sent =
            Send(context.sendNpcWindow, Protocol::Npc::WriteRepair(pet.slot, static_cast<std::uint8_t>(price & 0xffu)),
                 "0x44c3c4: el C1 05 34 del entrenador");
        !sent.HasValue())
        return sent;
    // 0x44c62e: 0x4499b0, sin mirar si cerró. 0x44c633: 0x421d90.
    auto closed = CloseTheTrainer(context.world, context.inventory, context.sendNpcWindow);
    if (!closed.HasValue())
        return Core::Unexpected(std::move(closed).Error());
    ForgetThePetTooltipQuery(context.world);
    return {};
}

Core::Expected<void, std::string> StepTheTrainerBox(const Hud::GameUiContext& context, Ui::PanelRegistry& panels)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Kernel::MouseState& mouse = runtime.mouse;
    if (Inside(context, BoxWindow))
        runtime.ui.mouseOnUi = 1;
    if (Inside(context, MixButton) && mouse.pendingClick)
    {
        ConsumeTheClick(mouse);
        const std::uint8_t kind = runtime.panels.delegated.field724584;
        if (kind == HorseMix || kind == RavenMix)
        {
            // 0x44d0c7..0x44d363 y 0x44d379..0x44d608: el C1 04 86 armado de a un byte con el XOR32, en claro.
            if (auto sent = Send(context.sendNpcWindow,
                                 Protocol::Npc::WriteChaosMix(kind == HorseMix ? HorseMixKind : RavenMixKind),
                                 "0x44d29c: el C1 04 86 del entrenador");
                !sent.HasValue())
                return sent;
        }
    }
    if (Inside(context, BoxClose) && mouse.pendingClick)
    {
        ConsumeTheClick(mouse);
        // 0x44d661: 0x65cae0(0x8000, 0).
        if (auto closed = panels.Close(Ui::TrainerPanel); !closed.HasValue())
            return Core::Unexpected(std::move(closed).Error());
    }
    // 0x44d666.
    ScanTheTrainerBox(context.world, context.random);
    return {};
}
} // namespace Client::PetTrainer
