#include "Scenes/ActorEffectFlags.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Scenes/ActorState.h"

#include <cstdint>
#include <optional>
#include <string>

// 0x4e6380, cotejado con el PE (1.418 bytes), y 0x42ab40 (266 bytes).
namespace Client::Scenes
{
namespace
{
constexpr std::uint32_t Bit40000 = 0x40000;
constexpr std::uint32_t Bit80000 = 0x80000;
constexpr std::uint32_t Bit400000 = 0x400000;
constexpr std::uint32_t Bit800000 = 0x800000;

bool Has(std::uint32_t flags, std::uint32_t bit)
{
    return (flags & bit) == bit;
}

// Los tipos 0x204 a 0x208 (con signo, 0x4e64a2..0x4e64a8) llevan la bandera en +0x7c, entera.
bool KeepsItIn7c(const ActorState& actor)
{
    constexpr std::int16_t FirstType = 0x204;
    constexpr std::int16_t LastType = 0x208;
    const auto type = static_cast<std::int16_t>(actor.type);
    return type >= FirstType && type <= LastType;
}

// 0x10000, 0x20000, 0x80000 (sin la parte del 0x40000) y 0x100000.
void PutBitOr7c(ActorState& actor, std::uint32_t bit)
{
    if (KeepsItIn7c(actor))
        actor.flags7c = bit;
    else
        actor.flags78 |= bit;
}

void ClearBit(std::uint32_t& flags, std::uint32_t bit)
{
    if (Has(flags, bit))
        flags -= bit;
}

Core::Expected<void, std::string> Cut(const char* text)
{
    return Core::Unexpected(std::string(text));
}
} // namespace

Core::Expected<void, std::string> ApplyEffectFlags(ActorState& actor, std::uint32_t flags, bool publishedHero,
                                                   std::optional<std::int32_t>& lastSound)
{
    // 0x4e6390..0x4e63b7.
    if (Has(flags, Bit800000))
    {
        ClearBit(actor.flags78, Bit40000);
        actor.flags78 |= Bit800000;
    }
    // 0x4e63c0..0x4e63ce.
    constexpr std::uint32_t Hidden = 0x10000000;
    if (Has(flags, Hidden))
        actor.flags7c |= Hidden;
    // 0x4e63d3..0x4e6412: los cuatro bits de abajo.
    actor.flags78 |= flags & 0xfu;
    // 0x4e641a..0x4e6492.
    constexpr std::uint32_t Bit100 = 0x100;
    if (Has(flags, Bit100))
    {
        if (!Has(actor.flags78, Bit100) && actor.type == Data::Models::PlayerSkeletonModel)
        {
            constexpr std::int32_t Bit100Sound = 0x84;
            lastSound = Bit100Sound;
            return Cut("0x4e644f: 0x534f90 y los cinco joints 0x10f (0x530860) de la bandera 0x100 son de la CL11");
        }
        actor.flags78 |= Bit100;
    }
    constexpr std::uint32_t Bit10000 = 0x10000;
    constexpr std::uint32_t Bit20000 = 0x20000;
    if (Has(flags, Bit10000))
        PutBitOr7c(actor, Bit10000);
    if (Has(flags, Bit20000))
        PutBitOr7c(actor, Bit20000);
    // 0x4e64df..0x4e6543: el primero de 0x80000, 0x400000 y 0x800000 que tenga, afuera, y el 0x40000 adentro.
    if (Has(flags, Bit40000))
    {
        if (KeepsItIn7c(actor))
        {
            actor.flags7c = Bit40000;
        }
        else
        {
            if (Has(actor.flags78, Bit80000))
                actor.flags78 -= Bit80000;
            else if (Has(actor.flags78, Bit400000))
                actor.flags78 -= Bit400000;
            else if (Has(actor.flags78, Bit800000))
                actor.flags78 -= Bit800000;
            actor.flags78 |= Bit40000;
        }
    }
    // 0x4e6546..0x4e6565.
    if (Has(flags, Bit80000))
    {
        if (KeepsItIn7c(actor))
        {
            actor.flags7c = Bit80000;
        }
        else
        {
            ClearBit(actor.flags78, Bit40000);
            actor.flags78 |= Bit80000;
        }
    }
    constexpr std::uint32_t Bit100000 = 0x100000;
    if (Has(flags, Bit100000))
        PutBitOr7c(actor, Bit100000);
    // 0x4e659d..0x4e65c1.
    if (Has(flags, Bit400000))
    {
        ClearBit(actor.flags78, Bit40000);
        actor.flags78 |= Bit400000;
    }
    // 0x4e65c8..0x4e65ec: el bit va antes de los joints.
    constexpr std::uint32_t Bit1000 = 0x1000;
    if (Has(flags, Bit1000))
    {
        actor.flags78 |= Bit1000;
        return Cut("0x4e662d: los tres joints 0x10f/8 (0x530860) de la bandera 0x1000 son de la CL11");
    }
    // 0x4e66c6..0x4e6709.
    for (const std::uint32_t bit : {0x4000u, 0x200u, 0x8000u})
    {
        if (Has(flags, bit))
            actor.flags78 |= bit;
    }
    if (Has(flags, 0x400u))
        return Cut("0x4e6725: 0x518ac0 y el efecto 0x5ac/2 (0x518cf0) de la bandera 0x400 son de la CL11");
    if (Has(flags, 0x10u))
        return Cut("0x4e676e: 0x518ac0 y el efecto 0x5ac/1 (0x518cf0) de la bandera 0x10 son de la CL11");
    // 0x4e67a6..0x4e6833: con el bit ya prendido en +0x78, solo lo vuelve a prender.
    constexpr std::uint32_t Bit20 = 0x20;
    if (Has(flags, Bit20))
    {
        if (!Has(actor.flags78, Bit20))
            return Cut("0x4e67d6: 0x518ac0 y los efectos 0xc1/1 y 0xc1/2 (0x518cf0) de la bandera 0x20 son de la CL11");
        actor.flags78 |= Bit20;
    }
    constexpr std::uint32_t Bit40 = 0x40;
    if (Has(flags, Bit40))
    {
        if (!Has(actor.flags78, Bit40))
            return Cut("0x4e6869: 0x518ac0, el efecto 0x628/0 (0x518cf0) y el sonido 0x85 de la bandera 0x40 son de la "
                       "CL11 y la CL10");
        actor.flags78 |= Bit40;
    }
    // 0x4e68a7..0x4e68cc.
    for (const std::uint32_t bit : {0x80u, 0x1000000u})
    {
        if (Has(flags, bit))
            actor.flags78 |= bit;
    }
    // 0x4e68d3..0x4e68fd.
    constexpr std::uint32_t Bit2000000 = 0x2000000;
    if (Has(flags, Bit2000000))
    {
        if (publishedHero)
            return Cut("0x4e68f5: el renglón de 0x555190 con 0x5767b6c y 0x774300c (sin leer) de la bandera 0x2000000 "
                       "en el héroe está abierto (P-C163)");
        actor.flags78 |= Bit2000000;
    }
    return {};
}

void ApplyCastleSiegeFlags(ActorState& actor, std::uint32_t flags, std::int32_t map)
{
    constexpr std::int32_t CastleSiegeMap = 30;
    if (map != CastleSiegeMap)
        return;
    constexpr std::uint8_t GuildLayer = 0xc;
    // 0x42ab52..0x42abe7: el primero de los tres, con la capa 0xc soltada antes.
    constexpr std::uint8_t FirstLayer = 6;
    std::uint8_t layer = FirstLayer;
    for (const std::uint32_t bit : {Bit80000, Bit400000, Bit800000})
    {
        if (Has(flags, bit))
        {
            if (actor.byte360 == GuildLayer)
                ReleaseTheLayer(actor);
            actor.byte360 = layer;
            return;
        }
        ++layer;
    }
    const bool siegeLayer = actor.byte360 == 6 || actor.byte360 == 7 || actor.byte360 == 8;
    // 0x42abe8..0x42ac1d.
    if (Has(flags, Bit40000))
    {
        if (siegeLayer)
            ReleaseTheLayer(actor);
        actor.byte360 = GuildLayer;
        return;
    }
    // 0x42ac1e..0x42ac45.
    if (siegeLayer || actor.byte360 == GuildLayer)
        ReleaseTheLayer(actor);
}
} // namespace Client::Scenes
