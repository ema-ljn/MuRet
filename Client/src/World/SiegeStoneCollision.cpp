#include "Math/Ftol.h"
#include "World/SiegeStoneEffects.h"
#include "Core/Expected.h"
#include "World/ModelEffects.h"
#include "World/WorldObjects.h"
#include <cmath>
#include <span>
#include <string>
namespace Client::World
{
Core::Expected<bool, std::string> CollideTheSiegeObject(ModelEffect& projectile, std::span<WorldObject> objects,
                                                        float radius, float heightLimit, bool markOnly)
{
    const auto& p = projectile.transform.position;
    // 6b8a54: 1/1600f, producto x87 sinstoreantesde_ftol.
    const auto sx = Math::OriginalFtolLow(static_cast<double>(p.x) * static_cast<double>(.000625f));
    const auto sy = Math::OriginalFtolLow(static_cast<double>(p.y) * static_cast<double>(.000625f));
    if (!sx || !sy)
        return Core::Unexpected(std::string("429ec0: sector delproyectilfuera de_ftol"));
    if (*sx < 0 || *sx >= 16 || *sy < 0 || *sy >= 16)
        return false;
    // 6066ff añade alfinaldelalista; objectsconservaelordendearchivoyluegoaltasespeciales.
    for (auto& object : objects)
    {
        if (object.sectorX != *sx || object.sectorY != *sy || !object.alive || object.state.hiddenMesh != -1 ||
            object.field91 == 0)
            continue;
        const auto& target = object.transform.position;
        const double dx = static_cast<double>(p.x) - target.x, dy = static_cast<double>(p.y) - target.y;
        if (std::sqrt(dx * dx + dy * dy) > radius)
            continue;
        if (!markOnly && !(std::abs(static_cast<double>(p.z) - target.z) < heightLimit))
            continue;
        if (object.field08 == 0)
        {
            object.field84 =
                static_cast<float>(static_cast<double>(object.field84) + static_cast<double>(markOnly ? .3f : 1.0f));
            if (object.field84 >= 5)
                object.field08 = 99;
        }
        if (!markOnly)
        {
            projectile.life = 15;
            projectile.subtype = 99;
            projectile.objectState.hiddenMesh = 99;
        }
        return true;
    }
    return false;
}
} // namespace Client::World
