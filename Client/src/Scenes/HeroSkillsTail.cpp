#include "Scenes/HeroSkillsTail.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Protocol/Skill.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalAngles.h"
#include "Scenes/ActorState.h"
#include "Scenes/CombatActionSpeeds.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/HeroAttack.h"
#include "Scenes/HeroSkillCasts.h"
#include "Scenes/HeroSkillSupport.h"
#include "Scenes/HeroSkills.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/Skill4e9920.h"
#include "Scenes/Skill4f5290.h"
#include "Scenes/Skill551f40.h"
#include "Scenes/SkillDelays.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/OriginalFtol.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
constexpr std::uint8_t FirstSharedSkill = 0x43;
constexpr std::uint8_t LastSharedSkill = 0x48;
constexpr std::uint8_t SternSkill = 0x43;
constexpr std::uint16_t UniriaModel = 0x1c14;
constexpr std::uint16_t DinorantModel = 0x1c15;
constexpr std::uint16_t DarkHorseModel = 0x1c16;
constexpr std::uint16_t FenrirModel = 0x1c37;
constexpr std::uint8_t DarkHorseAction = 0x52;
constexpr std::uint8_t UniriaAction = 0x40;
constexpr std::uint8_t DinorantAction = 0x41;
constexpr std::uint8_t FenrirAction = 0x5d; // EBX, la constante del XOR32 de la función (0x58187f, 0x599d37)
constexpr std::uint8_t StandingAction = 0x3f;
constexpr std::uint8_t CancelAction = 0x87;                                       // 0x598d5b, la de la 0x44 y la 0x47
constexpr float CellReciprocal = std::bit_cast<float>(std::uint32_t{0x3c23d70a}); // [0x6b86c0], 0,01
constexpr float DirectionScale = std::bit_cast<float>(std::uint32_t{0x3f360b61}); // [0x6ba440], 256/360
constexpr std::int32_t MinimumOffset = -8;
constexpr std::int32_t MaximumOffset = 7;
constexpr std::uint8_t OffsetBias = 8;
constexpr unsigned HighNibble = 4;
constexpr std::uint8_t LowNibbleMask = 0xf;
// 0x57cf90(0): con el puntero nulo da 0 sin tocar el contador 0x77f3bad.
constexpr std::uint8_t NoMagicKey = 0;

bool InRange(std::int32_t value, std::int32_t first, std::int32_t last)
{
    return value >= first && value <= last;
}

std::int32_t Minus(std::int32_t left, std::int32_t right)
{
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(left) - static_cast<std::uint32_t>(right));
}

// 0x5ae300(x, y, blancoX, blancoY): la celda del blanco menos (x, y), cada eje entre −8 y 7; arriba la X + 8 y abajo
// la Y − 8 con el AND. El que llama guarda el byte de abajo.
std::uint8_t TowardsTheTarget(std::int32_t x, std::int32_t y, std::int32_t targetX, std::int32_t targetY)
{
    const auto dx = static_cast<std::uint8_t>(std::clamp(Minus(targetX, x), MinimumOffset, MaximumOffset));
    const auto dy = static_cast<std::uint8_t>(std::clamp(Minus(targetY, y), MinimumOffset, MaximumOffset));
    const auto high = static_cast<std::uint8_t>(static_cast<std::uint8_t>(dx + OffsetBias) << HighNibble);
    const auto low = static_cast<std::uint8_t>(static_cast<std::uint8_t>(dy - OffsetBias) & LowNibbleMask);
    return static_cast<std::uint8_t>(high | low);
}

Core::Expected<std::int32_t, std::string> Ftol(double value, std::string_view where)
{
    const auto truncated = World::OriginalFtolLow(value);
    if (!truncated)
        return Core::Unexpected(std::format("{}: fuera del perfil de ftol", where));
    return *truncated;
}

// Lo que hace cada una de las 0x44 a 0x48: sus direcciones y la acción sin montura.
struct MagicAttackSite
{
    std::string_view brake;
    std::string_view chosenBrake;
    std::string_view chosenKey;
    std::uint8_t standing = StandingAction;
};

// 0x599ed0, la tabla por la skill − 0x43 (de la 0x44 en adelante): el GetTickCount del freno sin elegido y con él, la
// clave del elegido y la acción del final.
MagicAttackSite SiteOf(std::uint8_t skill)
{
    switch (skill)
    {
    case 0x44:
        return {"0x594ebe", "0x59567d", "0x595858", CancelAction};
    case 0x45:
        return {"0x595ea1", "0x596660", "0x59683b", StandingAction};
    case 0x46:
        return {"0x596e46", "0x597603", "0x5977de", StandingAction};
    case 0x47:
        return {"0x597dfa", "0x5985bf", "0x598796", CancelAction};
    default:
        return {"0x598de1", "0x5995a0", "0x59977b", StandingAction};
    }
}

class SharedTail
{
public:
    SharedTail(ActorState& hero, std::uint8_t skill, const HeroSkillContext& context)
        : m_hero(hero), m_skill(skill), m_context(context), m_world(context.world), m_runtime(context.world.Runtime()),
          m_state(context.world.Runtime().movement)
    {
    }

    Core::Expected<void, std::string> Use();

private:
    SkillDelayContext Delays() const
    {
        return SkillDelayContext{.world = m_world, .random = m_context.random, .skills = m_context.skills};
    }
    Core::Expected<void, std::string> PayForTheSkill(const SkillCost& cost);
    Core::Expected<void, std::string> CastTheStern();
    Core::Expected<void, std::string> SendTheMagicAttack(const MagicAttackSite& site);
    Core::Expected<void, std::string> ActByTheMount(std::uint8_t standing);

    ActorState& m_hero;
    std::uint8_t m_skill;
    const HeroSkillContext& m_context;
    WorldState& m_world;
    WorldRuntime& m_runtime;
    GameMovementState& m_state;
};

Core::Expected<void, std::string> SharedTail::Use()
{
    // 0x5808ef..0x5808fa: el registro propio (0x4e79a0), abierto hasta 0x599d4c..0x599d58 (0x40f370, los 3.848 rand()
    // si vuelve a 0).
    const Render::Models::LogicalAccess own(m_runtime.ownRecordDepth, m_context.random,
                                            Render::Models::OwnRecordCloseRandoms);
    // 0x5808ff..0x580911: 0x551c70(skill, 1, 0, &maná, 0, &stamina).
    const auto cost = SkillCostOf(m_skill, m_context, "0x580911");
    if (!cost.HasValue())
        return Core::Unexpected(cost.Error());
    // 0x580916..0x580922: el héroe de 0x57cfc0 tiene que ser un jugador; si no, 0x599d4c (solo el cierre).
    if (m_hero.type != Data::Models::PlayerSkeletonModel)
        return {};
    if (auto paid = PayForTheSkill(cost.Value()); !paid.HasValue())
        return paid;
    // 0x593fe0..0x594002: la energía (0x551f40), que nadie lee ([ebp−0x84]), y la tabla 0x599ed0 por la skill − 0x43.
    if (const auto energy = SkillEnergyRequirement(m_skill, Delays()); !energy.HasValue())
        return Core::Unexpected(energy.Error());
    if (m_skill == SternSkill)
    {
        if (auto cast = CastTheStern(); !cast.HasValue())
            return cast;
        return ActByTheMount(StandingAction);
    }
    const MagicAttackSite site = SiteOf(m_skill);
    if (auto sent = SendTheMagicAttack(site); !sent.HasValue())
        return sent;
    return ActByTheMount(site.standing);
}

// 0x580928..0x580975 y 0x593fbd..0x593fdd: todos los caminos siguen en 0x593fe0, sin mirar si alcanzó (Q-C1226).
Core::Expected<void, std::string> SharedTail::PayForTheSkill(const SkillCost& cost)
{
    const OwnCharacterRecord& record = m_world.OwnCharacter();
    // 0x580928..0x580938: JLE con signo contra +0x24 de [0x7681eb0].
    if (static_cast<std::int32_t>(cost.mana) > static_cast<std::int32_t>(record.mana))
    {
        // 0x58093e..0x580954: 0x558110(3); sin poción, 0x593fe0. Con ella, 0x5af520, el C1 26 (0x593874) o el aviso
        // de 0x555190 (0x580961): del hito 5.
        return UseAManaPotion(m_context, "0x580954");
    }
    // 0x593fbd..0x593fc8: JG con signo contra +0x38.
    if (static_cast<std::int32_t>(cost.stamina) > static_cast<std::int32_t>(record.bp))
        return {};
    // 0x593fca..0x593fdd: 0x552c50 con +0x425 del héroe publicado; el resultado no lo mira nadie.
    const auto started = TryStartSkillDelay(m_world.Actors()[m_runtime.hero].skillIndex, Delays());
    if (!started.HasValue())
        return Core::Unexpected(started.Error());
    return {};
}

// 0x594009..0x594de9: la 0x43.
Core::Expected<void, std::string> SharedTail::CastTheStern()
{
    // 0x594009..0x594027: 0x4e93f0(+0x10, +0x14, +0x3a8, +0x3ac) en +0x24.
    m_hero.angles.z = static_cast<float>(Render::Models::CreateAngle(m_hero.position.x, m_hero.position.y,
                                                                     m_hero.attackPosition.x, m_hero.attackPosition.y));
    // 0x59402a..0x59404c: la celda del blanco, ftol de +0x3a8 · 0,01 y después de +0x3ac · 0,01.
    const auto targetX =
        Ftol(static_cast<double>(m_hero.attackPosition.x) * static_cast<double>(CellReciprocal), "0x594039");
    if (!targetX.HasValue())
        return Core::Unexpected(targetX.Error());
    const auto targetY =
        Ftol(static_cast<double>(m_hero.attackPosition.y) * static_cast<double>(CellReciprocal), "0x59404c");
    if (!targetY.HasValue())
        return Core::Unexpected(targetY.Error());
    // 0x594051..0x594082: 0x4e7e40 sobre +0x420 y después sobre +0x41c, y 0x5ae300.
    const std::uint8_t cellY = ReadTheSkillCell(m_hero.cellYAccesses, m_hero.cellY, m_context.random);
    const std::uint8_t cellX = ReadTheSkillCell(m_hero.cellXAccesses, m_hero.cellX, m_context.random);
    const std::uint8_t towards = TowardsTheTarget(cellX, cellY, targetX.Value(), targetY.Value());
    // 0x59407c..0x5940a4: 0x4e9920 con el ftol de +0x3ac y después el de +0x3a8, sin escalar.
    const auto wholeY = Ftol(static_cast<double>(m_hero.attackPosition.y), "0x594085");
    if (!wholeY.HasValue())
        return Core::Unexpected(wholeY.Error());
    const auto wholeX = Ftol(static_cast<double>(m_hero.attackPosition.x), "0x594091");
    if (!wholeX.HasValue())
        return Core::Unexpected(wholeX.Error());
    const auto back = RelativeTargetCell(m_hero.position.x, m_hero.position.y, wholeX.Value(), wholeY.Value());
    if (!back)
        return Core::Unexpected(std::string("0x59409f: 0x4e9920 fuera del perfil de ftol"));
    // 0x5940a7..0x5940c5: la clave de 0x558eb0 con 0x7758588, o 0xffff con −1.
    std::uint16_t key = Protocol::Skill::NoSkillTargetKey;
    if (m_state.weaponSkill.target != -1)
    {
        const auto chosen =
            ChosenKeyOf(m_world, SlotOf(m_world, m_hero), m_state.weaponSkill.target, m_context.controlDown);
        if (!chosen.HasValue())
            return Core::Unexpected(chosen.Error());
        key = static_cast<std::uint16_t>(static_cast<std::uint32_t>(chosen.Value()) & 0xffffu);
    }
    // 0x5940c8..0x594135: con "webzen" en el nombre del héroe publicado, a 0x594de9 sin mandar (ni el freno: no lo
    // tiene).
    if (!HasTheSilentName(m_world))
    {
        // 0x594137..0x594de1: 0x5767948 y el 1E. La celda lógica la lee 0x4e7e40 al armarlo: la X (0x5942c9) antes
        // que la Y (0x5943c8).
        m_state.lastShotSkill = m_skill;
        const std::uint8_t x = ReadTheSkillCell(m_hero.cellXAccesses, m_hero.cellX, m_context.random);
        const std::uint8_t y = ReadTheSkillCell(m_hero.cellYAccesses, m_hero.cellY, m_context.random);
        const auto direction =
            Ftol(static_cast<double>(m_hero.angles.z) * static_cast<double>(DirectionScale), "0x5944d0");
        if (!direction.HasValue())
            return Core::Unexpected(direction.Error());
        SendTheSkillPacket(
            m_context, Protocol::Skill::WriteDurationMagic({.skill = m_skill,
                                                            .x = x,
                                                            .y = y,
                                                            .direction = static_cast<std::uint8_t>(
                                                                static_cast<std::uint32_t>(direction.Value()) & 0xffu),
                                                            .towards = towards,
                                                            .back = *back,
                                                            .key = key,
                                                            .magicKey = NoMagicKey}));
    }
    // 0x594de9: las velocidades, con el registro propio todavía abierto.
    RefreshCombatActionSpeeds(m_world, m_context.random);
    return {};
}

// 0x594e42..0x595db3 (y sus pares de la 0x45 a la 0x48): el 19.
Core::Expected<void, std::string> SharedTail::SendTheMagicAttack(const MagicAttackSite& site)
{
    // El nombre "webzen" (0x6c920c..0x6c9254, uno por camino) se mira antes que el freno, que se mira antes de armarlo.
    if (HasTheSilentName(m_world))
        return {};
    const std::int32_t chosen = m_runtime.selectedTargets[2];
    const auto passes = PassesTheSkillBrake(m_skill, m_context, chosen == -1 ? site.brake : site.chosenBrake);
    if (!passes.HasValue())
        return Core::Unexpected(passes.Error());
    if (!passes.Value())
        return {};
    std::uint16_t key = m_context.playerKey;
    if (chosen != -1)
    {
        // El WORD +0x1f4 del elegido, sin mirar si está en el pool.
        if (chosen < 0 || static_cast<std::size_t>(chosen) >= m_world.Actors().size())
            return Core::Unexpected(std::format("{}: el elegido {} está fuera del pool", site.chosenKey, chosen));
        key = m_world.Actors()[static_cast<std::size_t>(chosen)].key;
    }
    SendTheSkillPacket(m_context, Protocol::Skill::WriteMagicAttack(m_skill, key));
    return {};
}

// 0x594dee..0x594e3d, 0x595db8..0x595e20 y los demás, hasta 0x599d45: la acción por la montura, cada una fuera de la
// zona segura (+0x3e2), y +0x378 en 0.
Core::Expected<void, std::string> SharedTail::ActByTheMount(std::uint8_t standing)
{
    const std::uint16_t mount = m_hero.pet.model;
    const bool outside = m_hero.safeZone == 0;
    std::uint8_t action = standing;
    if (mount == DarkHorseModel && outside)
        action = DarkHorseAction;
    else if (mount == UniriaModel && outside)
        action = UniriaAction;
    else if (mount == DinorantModel && outside)
        action = DinorantAction;
    else if (mount == FenrirModel && outside)
        action = FenrirAction;
    if (auto acted = SetTheActorAction(m_hero, action, CombatContextOf(m_context)); !acted.HasValue())
        return acted;
    m_hero.movement = 0;
    return {};
}
} // namespace

Core::Expected<void, std::string> UseTheSharedTail(ActorState& hero, std::uint8_t skill,
                                                   const HeroSkillContext& context)
{
    // 0x5808da..0x5808e9.
    if (!InRange(skill, FirstSharedSkill, LastSharedSkill))
        return {};
    return SharedTail(hero, skill, context).Use();
}
} // namespace Client::Scenes
