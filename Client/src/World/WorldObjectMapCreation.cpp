#include "World/WorldObjectMapCreation.h"

#include "Render/Models/Cloth.h"
#include "World/IWorldObjectCreationServices.h"
#include "World/WorldObjectRegistrations.h"
#include "World/WorldObjects.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

namespace Client::World
{
namespace
{
std::uint8_t CreateChaosCastleObject(WorldObject& object, std::int32_t map)
{
    // 0x405950: el mapa aceptado retorna AL1 incluso si el tipo no escribe nada.
    constexpr std::int32_t FirstChaosCastle = 18;
    constexpr std::int32_t LastChaosCastle = 23;
    if (map < FirstChaosCastle || map > LastChaosCastle)
    {
        return 0;
    }
    if ((object.type >= 18 && object.type <= 21) || (object.type >= 24 && object.type <= 35))
    {
        object.basePosition.x = object.transform.position.x;
        const float y = object.transform.position.y;
        const float z = object.transform.position.z;
        object.state.hiddenMesh = HiddenObject;
        object.basePosition.y = y;
        object.basePosition.z = z;
    }
    return 1;
}

std::uint8_t CreateMapObject438A80(WorldObject&)
{
    // Los tres bytes verificados 32 c0 c3 de 0x438a80 retornan AL0 sin leer el objeto ni el mapa.
    return 0;
}

enum class CastleStateGroup : std::uint8_t
{
    None,
    VisibleWhenNonzero,
    HiddenWhenNonzero,
};

CastleStateGroup CastleStateGroupOf(std::int16_t type)
{
    // 0x42b480: la selección signed16 precede al getter y no se vuelve a evaluar tras él.
    constexpr std::array<std::int16_t, 20> VisibleWhenNonzero{4,  11, 37, 41, 43, 44, 45, 46, 47, 48,
                                                              67, 68, 69, 71, 72, 73, 74, 75, 76, 88};
    constexpr std::array<std::int16_t, 16> HiddenWhenNonzero{16, 38, 39, 40, 50, 58, 59, 78,
                                                             80, 81, 82, 83, 84, 85, 86, 87};
    if (std::find(VisibleWhenNonzero.begin(), VisibleWhenNonzero.end(), type) != VisibleWhenNonzero.end())
    {
        return CastleStateGroup::VisibleWhenNonzero;
    }
    if (std::find(HiddenWhenNonzero.begin(), HiddenWhenNonzero.end(), type) != HiddenWhenNonzero.end())
    {
        return CastleStateGroup::HiddenWhenNonzero;
    }
    return CastleStateGroup::None;
}

void ApplyCastlePredicate(WorldObject& object, std::uint8_t state, bool hideWhenNonzero)
{
    const bool hidden = hideWhenNonzero ? state != 0 : state == 0;
    constexpr std::int16_t NoHiddenMesh = -1;
    object.state.hiddenMesh = hidden ? HiddenObject : NoHiddenMesh;
}

std::optional<WorldObjectCreationError>
ApplyCastlePredicateState(WorldObject& object, const WorldObjectCreationContext& context, bool hideWhenNonzero)
{
    if (context.state7239Ec == nullptr)
    {
        return WorldObjectCreationError::UnsupportedMapHandler;
    }
    // 0x4294b0: un BYTE vivo por invocación, sin normalización ni valor inicial inventado.
    ApplyCastlePredicate(object, *context.state7239Ec, hideWhenNonzero);
    return std::nullopt;
}

std::optional<WorldObjectCreationError> ApplyCastleObjectState(WorldObject& object,
                                                               const WorldObjectCreationContext& context)
{
    if (CastleStateGroupOf(object.type) == CastleStateGroup::None)
    {
        return std::nullopt;
    }
    if (context.state7239Ec == nullptr)
    {
        return WorldObjectCreationError::UnsupportedMapHandler;
    }
    ApplyCastleSiegeObjectState(object, *context.state7239Ec);
    return std::nullopt;
}

bool ApplyCreationRandomPhase(WorldObject& object, Render::Models::CrtRandom* random, const float& factor)
{
    if (random == nullptr)
    {
        return false;
    }
    // 0x42c711..42c72b y 0x43ff90..43ffa9: un rand, resto signed1000 y store en +84.
    constexpr int RandomSpan = 1000;
    const int remainder = random->Next() % RandomSpan;
    // FMUL m32[6b88d4] viene después del RNG: tomar aquí el valor del almacenamiento prestado.
    const float factorValue = factor;
    object.field84 = static_cast<float>(static_cast<double>(remainder) * static_cast<double>(factorValue));
    return true;
}

std::optional<WorldObjectCreationError> ApplyCastleSpecialCase(WorldObject& object, WorldObjectCreationContext& context)
{
    // 0x42c6c4 guarda una sola lectura del tipo; ningún callback cambia la ruta ya elegida.
    const std::int16_t type = object.type;
    if (type == 7 || type == 10 || type == 13 || type == 14 || (type >= 61 && type <= 64))
    {
        object.field08 = 0;
        object.field91 = 1;
    }
    else if (type == 8 || type == 9)
    {
        object.field08 = 0;
    }
    else if (type == 19 || type == 415)
    {
        object.transform.scale = 1.0f;
        if (type == 415)
        {
            return ApplyCastlePredicateState(object, context, true);
        }
    }
    else if (type == 39)
    {
        object.state.hiddenMesh = HiddenObject;
    }
    else if (type == 41 && !ApplyCreationRandomPhase(object, context.random, context.randomPhaseFactor.get()))
    {
        return WorldObjectCreationError::UnsupportedMapHandler;
    }
    if (type == 77 || type == 84)
    {
        RegisterWorldObject(context.registrations, object.identity);
    }
    if (type == 79 &&
        (context.services == nullptr ||
         !context.services->CreateModelEffect({309, object.transform.position, 0.0f, object.identity}, object)))
    {
        return WorldObjectCreationError::UnsupportedEffect;
    }
    return std::nullopt;
}

std::optional<WorldObjectCreationError> CreateCastleSiegeObject(WorldObject& object,
                                                                WorldObjectCreationContext& context)
{
    constexpr std::int32_t CastleSiegeMap = 30;
    if (context.map != CastleSiegeMap)
    {
        return std::nullopt;
    }
    const auto error = ApplyCastleSpecialCase(object, context);
    if (error.has_value())
    {
        return error;
    }
    // H lee el tipo vivo tras el efecto309; su retorno AL no corta la cola de 0x606660.
    return ApplyCastleObjectState(object, context);
}
std::optional<WorldObjectCreationError> CreateMapObject43FF70(WorldObject& object, WorldObjectCreationContext& context)
{
    // 0x43ff10 lee el DWORD vivo aquí, después de C/H; no reutilizar el mapa del dispatch inicial.
    constexpr std::int32_t LandOfTrialMap = 31;
    if (context.map != LandOfTrialMap)
    {
        return std::nullopt;
    }
    const std::int16_t type = object.type;
    if ((type == 27 || type == 54) &&
        !ApplyCreationRandomPhase(object, context.random, context.randomPhaseFactor.get()))
    {
        return WorldObjectCreationError::UnsupportedMapHandler;
    }
    return std::nullopt;
}

std::uint8_t CreateMapObject42EE30(const WorldObjectCreationContext& context)
{
    // 0x42ee20: un DWORD vivo igual a 35; el wrapper normaliza AL sin efectos ni RNG.
    constexpr std::int32_t Map35 = 35;
    return static_cast<std::uint8_t>(context.map == Map35);
}

std::uint8_t CreateMapObject4250E0(const WorldObjectCreationContext& context)
{
    // 0x425070: una única lectura de 32 bits para ambas igualdades; el wrapper conserva AL0/1.
    constexpr std::int32_t Map33 = 33;
    constexpr std::int32_t CharacterSelectMap = 54;
    const std::int32_t map = context.map;
    return static_cast<std::uint8_t>(map == Map33 || map == CharacterSelectMap);
}

std::uint8_t CreateMapObject4303D0(const WorldObjectCreationContext& context)
{
    // 0x4303c0: un DWORD vivo igual a 34; el wrapper normaliza AL sin efectos ni RNG.
    constexpr std::int32_t Map34 = 34;
    return static_cast<std::uint8_t>(context.map == Map34);
}
} // namespace

void ApplyCastleSiegeObjectState(WorldObject& object, std::uint8_t siegeState)
{
    const CastleStateGroup group = CastleStateGroupOf(object.type);
    if (group == CastleStateGroup::None)
    {
        return;
    }
    // El store nativo DWORD de 0x42b480 ocurre DESPUÉS del getter y pisa el valor anterior.
    ApplyCastlePredicate(object, siegeState, group == CastleStateGroup::HiddenWhenNonzero);
}

std::optional<WorldObjectCreationError> ApplyDefaultWorldObjectCreation(WorldObject& object,
                                                                        WorldObjectCreationContext& context)
{
    if (CreateChaosCastleObject(object, context.map) == 1)
    {
        return std::nullopt;
    }
    if (CreateMapObject438A80(object) == 1)
    {
        return std::nullopt;
    }
    const auto castleError = CreateCastleSiegeObject(object, context);
    if (castleError.has_value())
    {
        return castleError;
    }
    // Los retornos AL de esta cola se ignoran. Sólo se consulta el gate de integración del port.
    const auto fourthError = CreateMapObject43FF70(object, context);
    if (fourthError.has_value())
    {
        return fourthError;
    }
    (void)CreateMapObject42EE30(context);
    (void)CreateMapObject4250E0(context);
    (void)CreateMapObject4303D0(context);
    return std::nullopt;
}
} // namespace Client::World
