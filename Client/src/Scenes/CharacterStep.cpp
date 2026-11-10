#include "Scenes/CharacterStep.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/TerrainCulling.h"
#include "World/TerrainWalls.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

namespace Client::Scenes
{
namespace
{
// 0x50919a y 0x5091c8: los dos lazos de 0x508f00 recorren 400 registros (0x69780 / 0x438); el 401 no.
constexpr std::size_t SteppedCharacters = 400;
// 0x412162: en el mapa 30 (0x1e), 0x412140 arma los objetos del asedio.
constexpr std::int32_t CastleMap = 30;
// 0x506acf: con +0x375 en 6 o más (sin signo), las manos no dan luz.
constexpr std::uint8_t FirstPkWithoutLights = 6;
// 0x506aec..0x506b26: los modelos de las manos que piden una luz a 0x5f9780.
constexpr std::array<std::uint16_t, 5> LightModels{0x21e, 0x225, 0xa24, 0xc1c, 0x61f};
// 0x506d01 y 0x506d8b: los mapas del caso del 0x20a con efectos.
constexpr std::int32_t ClockEffectMap = 7;
constexpr std::int32_t RandomEffectMap = 9;
// 0x506e5d..0x506e63 y 0x506f62: las acciones del 0x20a con efectos.
constexpr std::uint8_t FirstEffectAction = 0x44;
constexpr std::uint8_t SecondEffectAction = 0x45;
constexpr std::uint8_t BoneEffectAction = 0x96;
// 0x508654..0x508666: las acciones con los pasos del héroe.
constexpr std::uint8_t FirstStepAction = 0x0d;
constexpr std::uint8_t LastStepAction = 0x21;
constexpr std::array<std::uint8_t, 3> OtherStepActions{0x8b, 0x8c, 0x4b};
// 0x508e79: el tipo que, muriéndose, 0x508ba0 baja a la altura del terreno.
constexpr std::uint16_t SinkingDeadType = 0x154;
// 0x508be3: el tipo con el que 0x508ba0 no busca camino.
constexpr std::uint16_t PathlessType = 0x159;

std::string MapText(std::int32_t map)
{
    return "mapa " + std::to_string(map);
}

bool HasStepSounds(std::uint8_t action)
{
    return (action >= FirstStepAction && action <= LastStepAction) ||
           std::ranges::find(OtherStepActions, action) != OtherStepActions.end();
}

// 0x508ba0(c, c) desde 0x508ed8 con otro que el héroe publicado ([0x743a170]; con él sale en 0x508bad).
Core::Expected<void, std::string> StepOtherCharacter(ActorState& actor, std::size_t slot,
                                                     Render::Models::CrtRandom& random)
{
    // 0x508bb6: muriéndose (+0x389), sale; el 0x154 antes toma la altura del terreno (0x508e76..0x508e8e).
    if (actor.dying != 0)
    {
        if (actor.type == SinkingDeadType)
        {
            return Core::Unexpected(std::format("0x508e89: el {:#x} del lugar {} se muere y toma la altura "
                                                "de 0x5f93c0, que no está portada",
                                                actor.type, slot));
        }
        return {};
    }
    // 0x508bc4: caminando (+0x378), 0x508e26..0x508e6e sigue el camino (0x4f37c0, 0x4ea270, 0x4f2250 y 0x508a80).
    if (actor.movement != 0)
    {
        return Core::Unexpected(std::format("0x508e27: el personaje del lugar {} camina (+0x378); 0x4f37c0 y 0x508a80 "
                                            "no están portados",
                                            slot));
    }
    // 0x508bd2: +0x3e5 en 0, sin salir: solo lo sube 0x4c9790 (0x4c982b), un paquete que MuRet no aplica
    // (*estático*). 0x508be3: el 0x159 sale sin leer la celda.
    if (actor.type == PathlessType)
    {
        return {};
    }
    // 0x508bef..0x508c75: la X lógica (+0x41c) en un acceso que se cierra con 0x412890, contra la copia en
    // claro (+0x39a); 0x508c86..0x508ce9: la Y (+0x420), con 0x40b800, contra +0x39b. Con las dos iguales sale
    // en 0x508cfb, con 8 rand() si los dos cierres dejan su cuenta en 0 (Q-C236). MuRet guarda una sola copia de
    // cada una (0x511290 las escribe iguales): siempre coinciden, y el camino de 0x508d01..0x508e1a (0x4eac00 y
    // +0x378 en 1) no se alcanza.
    {
        const Render::Models::LogicalAccess access(actor.cellXAccesses, random,
                                                   Render::Models::SmallLogicalCloseRandoms);
    }
    {
        const Render::Models::LogicalAccess access(actor.cellYAccesses, random,
                                                   Render::Models::SmallLogicalCloseRandoms);
    }
    return {};
}

// 0x505de0(héroe, héroe) desde 0x505e0b. La escena es la 5: solo la llama GameScene.
Core::Expected<void, std::string> StepTheHero(ActorState& hero, std::int32_t map,
                                              const Data::Terrain::TerrainLayer<std::uint16_t>& walls,
                                              Render::Models::CrtRandom& random)
{
    // 0x505e0b: un modelo sin acciones (+0x26 de 0x5769ad8 + tipo · 0xe0) va a la caja de 0x505e15. El
    // 0x20a, Player.bmd, las tiene; los demás no están leídos.
    if (hero.type != Data::Models::PlayerSkeletonModel)
    {
        return Core::Unexpected(std::format("0x505e0b: el modelo {:#x} del héroe no está leído en 0x505de0, solo el "
                                            "0x20a",
                                            hero.type));
    }
    // 0x505ec6..0x505ee4: la posición, la escala y la acción van al modelo compartido (+0x6c, +0x68 y
    // +0xa0), que leen los 0x4ec110 de más abajo y que el dibujo vuelve a escribir antes de posar. MuRet
    // no tiene el modelo compartido.
    // 0x505ef2: sin verse, nada más.
    if (!hero.seen)
    {
        return {};
    }
    // 0x505ef8..0x506866 y 0x50686e..0x5068b2: la zona segura es el bit 0x01 del byte bajo de la celda
    // lógica, la de +0x41c y +0x420 (06 § Caminar), no la de la posición dibujada.
    const LogicalCell cell = ReadLogicalCell(hero, random);
    const std::uint16_t attribute = walls[Data::Terrain::CellIndex(cell.x, cell.y)];
    hero.safeZone = static_cast<std::uint8_t>((attribute & World::SafeZoneWallBit) != 0 ? 1 : 0);
    // Frontera (08 P-C44): la cabeza (0x5068b9..0x5069ab). Con el héroe no sortea ni gasta rand(): gira
    // +0x28 y +0x2c hacia +0x34 y +0x38, el blanco que MoveHero escribe con el mouse (0x59b1c2..0x59b287,
    // CL7.4). MuRet no tiene esos campos del héroe; los lee la pose del dibujo, que corta antes.
    // 0x5069ae: +0x3e5 en 0, sin los veinte efectos: solo lo sube 0x4c9790 (0x4c982b), un paquete que
    // MuRet no aplica (*estático*).
    // 0x506acf..0x506b62: con +0x375 debajo de 6, la luz de cada mano, la derecha y la izquierda.
    if (hero.pk < FirstPkWithoutLights)
    {
        for (const ActorItemPart& hand : hero.hands)
        {
            if (std::ranges::find(LightModels, hand.model) != LightModels.end())
            {
                return Core::Unexpected(std::format("0x5f9780: la luz del modelo {:#x} en la mano (0x506b50) va al "
                                                    "buffer dinámico 0x7b4ccc4, que MuRet no tiene",
                                                    hand.model));
            }
        }
    }
    // 0x506b64: +0x3d8 en 0, sin los efectos de +0x3d4: en los personajes solo lo escribe 0x4fd5f0, que lo
    // baja si es positivo (0x4fd75e); los otros que escriben +0x3d4 y +0x3d8 son de la UI (*estático*).
    // 0x506cac: rand() & 7, sin condición. El camino del 0x20a no lee el valor (*estático*, Q-C206).
    static_cast<void>(random.Next());
    // 0x506ccc..0x506cf1: el caso del tipo − 0x152; el 0x20a cae en 0x506cf8.
    if (map == ClockEffectMap)
    {
        return Core::Unexpected(std::string("0x506d01: en el mapa 7 el jugador saca el efecto 0x607 según el reloj "
                                            "del mundo (0x5767b88), que no está portado"));
    }
    if (map == RandomEffectMap)
    {
        return Core::Unexpected(std::string("0x506d8b: en el mapa 9 el jugador sortea dos efectos 0x68 con rand() & 3, "
                                            "que no están portados"));
    }
    const std::uint8_t action = hero.animation.action;
    if (action == FirstEffectAction || action == SecondEffectAction)
    {
        return Core::Unexpected(std::format("0x506e5d: la acción {:#x} del jugador saca los efectos 0x5ac y los de "
                                            "0x5189f0, que no están portados",
                                            action));
    }
    if (action == BoneEffectAction)
    {
        return Core::Unexpected(std::string("0x506f62: la acción 0x96 del jugador saca diez efectos 0x5d9 en huesos "
                                            "de rand(), que no están portados"));
    }
    // 0x506fc1..0x507001: 0x4ec110 con el hueso de +0x2bd a un local que nadie lee, y a 0x50862f.
    // 0x50862f..0x5086ca: los pasos del héroe, el sonido de 0x505770 con +0x3e0 y +0x3e1.
    if (HasStepSounds(action))
    {
        return Core::Unexpected(std::format("0x508668: los pasos del héroe en la acción {:#x} (0x505770, con +0x3e0 y "
                                            "+0x3e1) no están portados",
                                            action));
    }
    // 0x5086cf..0x508801: las acciones de ahí son todas de los pasos, que ya cortaron.
    return {};
}
} // namespace

bool CharacterSeen(const World::TerrainPolygon& polygon, const glm::vec3& position)
{
    constexpr double CellsPerUnit = static_cast<double>(0.01f);
    constexpr double ViewMargin = -20.0;
    const auto cells = [](float coordinate)
    { return static_cast<double>(static_cast<float>(static_cast<double>(coordinate) * CellsPerUnit)); };
    return World::PolygonContains(polygon, cells(position.x), cells(position.y), ViewMargin);
}

LogicalCell ReadLogicalCell(ActorState& actor, Render::Models::CrtRandom& random)
{
    LogicalCell cell;
    {
        const Render::Models::LogicalAccess access(actor.cellYAccesses, random,
                                                   Render::Models::SmallLogicalCloseRandoms);
        cell.y = actor.cellY;
    }
    {
        const Render::Models::LogicalAccess access(actor.cellXAccesses, random,
                                                   Render::Models::SmallLogicalCloseRandoms);
        cell.x = actor.cellX;
    }
    return cell;
}

Core::Expected<void, std::string> StepCharacters(const CharacterStepContext& context)
{
    WorldState& world = context.world;
    const WorldRuntime& runtime = world.Runtime();
    // 0x508f06..0x508f0c: 0x412140 con el héroe. Fuera del mapa 30 llama a 0x412560, que solo suelta el
    // objeto de 0x5763530 si lo hay, y ese objeto lo crea 0x412140 en el mapa 30 (*estático*).
    if (runtime.map == CastleMap)
    {
        return Core::Unexpected(std::string("0x412140: en el mapa 30 el paso de los personajes arma los objetos del "
                                            "asedio (0x5763530), que no están portados"));
    }
    if (!world.Map().terrain.attributes)
    {
        return Core::Unexpected("0x508f14: el TerrainWall (0x7d0d348) del " + MapText(runtime.map) +
                                " no está cargado (0x61a560)");
    }
    Data::Terrain::TerrainLayer<std::uint16_t>& walls = world.Map().terrain.attributes->cells;
    World::ClearCharacterWalls(walls);
    // 0x5fba90 lee los globales de 0x78ecbf0 y 0x78ecc00: el trapecio del último dibujo de cualquier escena.
    const World::TerrainPolygon& polygon = runtime.terrainPolygon;
    const auto actors = world.Actors();
    for (std::size_t slot = 0; slot < SteppedCharacters; ++slot)
    {
        ActorState& actor = actors[slot];
        // 0x508f49..0x509165: vivo, sin morirse y sin el 8 de +0x88, ocupa su celda lógica.
        if (actor.alive && actor.dying == 0 && actor.byte88 != World::NonBlockingKind)
        {
            const LogicalCell cell = ReadLogicalCell(actor, context.random);
            World::MarkCharacterWall(walls, cell.x, cell.y);
        }
        // 0x50916d..0x5091a0: vivo o no, si se ve, con el trapecio del último 0x5fb570.
        actor.seen = CharacterSeen(polygon, actor.position);
    }
    for (std::size_t slot = 0; slot < SteppedCharacters; ++slot)
    {
        ActorState& actor = actors[slot];
        // 0x508ea7: sin vivir, nada.
        if (!actor.alive)
        {
            continue;
        }
        // 0x508eac..0x508ed2: otra vez, con el mismo trapecio.
        actor.seen = CharacterSeen(polygon, actor.position);
        // 0x508ed8: 0x508ba0, que sale con el héroe (0x508bad).
        const bool hero = slot == runtime.hero;
        if (!hero)
        {
            if (auto other = StepOtherCharacter(actor, slot, context.random); !other.HasValue())
            {
                return other;
            }
        }
        // 0x508edf: 0x4fd5f0, el cuerpo, la animación y el alpha, de 32 KB (0x4fd5f0..0x5054b3), con al menos
        // un rand() % 6 sin condición (0x4fd7e4). Frontera (08 P-C44 a) solo con el héroe del juego: corre
        // también en la espera, con el 54 de la selección (la frontera de T8, P-C41 a). Con los demás, y en la
        // selección y el login también con el héroe, corta (08 P-C50).
        if (!hero || context.scene != SceneId::Game)
        {
            return Core::Unexpected(std::format("0x4fd5f0: el cuerpo, la animación y el alpha del personaje "
                                                "del lugar {} (0x508edf, con el rand() % 6 de 0x4fd7e4) no están "
                                                "portados",
                                                slot));
        }
        auto stepped = StepTheHero(actor, runtime.map, walls, context.random);
        if (!stepped.HasValue())
        {
            return stepped;
        }
        // 0x508eed: 0x42da90 fuera del mapa 30 devuelve 0 (0x42da9a).
    }
    // Frontera (08 P-C44): 0x52e500 (0x5091d0), el paso de las estelas de 0x75e9870, sin rand(). Las
    // crea 0x52e470 desde las habilidades, los efectos y la acción 3 del caballo (0x444671); MuRet no
    // tiene la tabla. Corre también en la espera.
    return {};
}
} // namespace Client::Scenes
