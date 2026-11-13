#include "Scenes/MonsterKinds.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"

#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace Client::Scenes
{
namespace
{
bool Runs(const MonsterStep& step, const MonsterCreationContext& context, std::int32_t r) noexcept
{
    switch (step.when)
    {
    case MonsterStepWhen::Always:
        return true;
    case MonsterStepWhen::RandomZero:
        return r == 0;
    case MonsterStepWhen::RandomOne:
        return r == 1;
    case MonsterStepWhen::InWorlds:
        return context.world >= step.low && context.world <= step.high;
    case MonsterStepWhen::OutsideWorlds:
        return context.world < step.low || context.world > step.high;
    case MonsterStepWhen::AtEitherX:
        return context.x == step.low || context.x == step.high;
    }
    return false;
}
} // namespace

MonsterCreation MonsterCreationOf(std::int32_t type, std::int32_t world) noexcept
{
    // 0x5141f6..0x5142fc: cada creador de evento con (tipo, x, y, clave); el primero que devuelve un personaje gana.
    // Los de 0x42d070, 0x42f190 y 0x430c90 miran el mapa al entrar (30, 35 y 34: 0x42d07e, 0x42ee20 y 0x4303c0).
    for (const EventMonsterKind& event : EventMonsterKinds())
    {
        if (event.kind.type == type && (event.world < 0 || event.world == world))
            return {event.creator, event.kind};
    }
    // 0x514305..0x51431c: tipo − 1 sin signo hasta 300 por la tabla de bytes; lo demás, al default.
    for (const MonsterKind& kind : MainMonsterKinds())
    {
        if (kind.type == type)
            return {0x5141e0, kind};
    }
    return {0x5141e0, DefaultMonsterKind()};
}

Core::Expected<std::optional<MonsterStep>, std::string> MonsterStepResolver::Next(MonsterStep step, std::int32_t type,
                                                                                  Render::Models::CrtRandom& random)
{
    if (!Runs(step, m_context, m_r))
        return std::optional<MonsterStep>{};
    if (step.kind == MonsterStepKind::Random)
    {
        // AND 0x80000001 con el ajuste del signo: el rand() no es negativo y queda el bit 0.
        m_r = random.Next() % 2;
        step.value = static_cast<std::uint32_t>(m_r);
    }
    if (step.kind == MonsterStepKind::Siege && m_context.siegeActive)
        return Core::Unexpected(std::format("{:#x}: con el asedio prendido (0x7239ec) el tipo {} sigue la rama "
                                            "del asedio, de la CL9.5",
                                            step.address, type));
    if (step.plusRandom)
        step.value += static_cast<std::uint32_t>(m_r);
    return std::optional<MonsterStep>{step};
}

Core::Expected<std::vector<MonsterStep>, std::string> ResolveMonsterSteps(const MonsterCreation& creation,
                                                                          const MonsterCreationContext& context,
                                                                          Render::Models::CrtRandom& random)
{
    std::vector<MonsterStep> steps;
    steps.reserve(creation.kind.steps.size());
    MonsterStepResolver resolver(context);
    for (const MonsterStep& step : creation.kind.steps)
    {
        auto resolved = resolver.Next(step, creation.kind.type, random);
        if (!resolved.HasValue())
            return Core::Unexpected(resolved.Error());
        if (resolved.Value())
            steps.push_back(*resolved.Value());
    }
    return steps;
}

MonsterTail MonsterTailOf(std::int32_t type, std::int32_t world) noexcept
{
    MonsterTail tail;
    // 0x5140cd..0x514112: mapa − 9 de 1 a 8 y el tipo de 0x54 a 0x8f.
    const std::int32_t steps = world - 9;
    if (steps > 0 && steps <= 8 && type >= 0x54 && type <= 0x8f)
    {
        tail.clearsLevel = true;
        tail.scaleSteps = steps / 3;
    }
    // 0x514183..0x5141ca: la clase del objeto.
    if (type == 0xc8 || type >= 0x104)
        tail.objectClass = 2;
    else if (type > 0xc8)
        tail.objectClass = 4;
    else if (type >= 0x64 && type <= 0x6e)
        tail.objectClass = 8;
    else
        tail.objectClass = 2;
    return tail;
}

float AddTailScale(float scale, std::int32_t steps) noexcept
{
    constexpr float Step = 0.05f; // 0x6b8a48
    return static_cast<float>(static_cast<double>(steps) * static_cast<double>(Step) + static_cast<double>(scale));
}
} // namespace Client::Scenes
