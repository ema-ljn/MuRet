#include "World/WorldObjects.h"

#include "Data/Terrain/TerrainObjects.h"
#include "Render/Models/Animation.h"
#include "Render/Models/ObjectState.h"
#include "Render/Models/Skeleton.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <span>
#include <vector>

namespace Client::World
{
namespace
{
// 0x606660: la velocidad de la animación (0x3e23d70a), y la escala y la luz del farol en el login y la
// selección (0x3f4ccccd y 0x3e4ccccd).
constexpr float ObjectSpeed = std::bit_cast<float>(0x3E23D70Au);
constexpr std::int16_t DragonOwner = 0xb4;
constexpr float StreetLightScale = std::bit_cast<float>(0x3F4CCCCDu);
constexpr float StreetLightLight = std::bit_cast<float>(0x3E4CCCCDu);
// Los remolinos del mapa 0x37 (0x606d03): a (8.600; 25.000; 5.000), con escalas 60 y 110.
constexpr std::uint8_t LoginMap = 0x37;
constexpr std::int16_t RedWhirlpool = 0x56;
constexpr std::int16_t DarkWhirlpool = 0x5a;
constexpr glm::vec3 WhirlpoolPlace{8600.0f, 25000.0f, 5000.0f};
constexpr float RedWhirlpoolScale = 60.0f;
constexpr float DarkWhirlpoolScale = 110.0f;
// 0x604ab0 en el mapa 0x37: el giro de los remolinos por paso (0x6b8a20 y 0x6ba154) y el alpha del
// rojo (0x3e4ccccd).
constexpr float RedWhirlpoolTurn = std::bit_cast<float>(0x3F19999Au);
constexpr float DarkWhirlpoolTurn = std::bit_cast<float>(0x3E6B851Fu);
constexpr float RedWhirlpoolAlpha = std::bit_cast<float>(0x3E4CCCCDu);
// 0x4e9980: un décimo de lo que le falta al alpha (0x6b87b8).
constexpr float AlphaApproach = std::bit_cast<float>(0x3DCCCCCDu);
// 0x606660: la posición por 0,000625 da el sector (de 16 × 16 celdas).
constexpr float SectorPerUnit = 0.000625f;
constexpr int SectorCount = 16;

bool HasSceneCases(ObjectScene scene)
{
    return scene == ObjectScene::Login || scene == ObjectScene::CharacterSelect;
}

// 0x5ffdf0: el alpha por debajo del cual no dibuja (0x3c23d70a).
constexpr float MinimumAlpha = std::bit_cast<float>(0x3C23D70Au);

// La rama de 0x604ab0 que oculta: los tipos 0x54, 0x55, 0x57 y 0x59 del mapa 0x37.
bool IsLoginMarker(std::int16_t type)
{
    return type == 0x54 || type == 0x55 || type == 0x57 || type == 0x59;
}
} // namespace

std::vector<WorldObject> CreateWorldObjects(std::span<const Data::Terrain::TerrainObject> records, std::uint8_t map,
                                            ObjectScene scene)
{
    std::vector<WorldObject> objects;
    objects.reserve(records.size());
    for (const Data::Terrain::TerrainObject& record : records)
    {
        WorldObject object;
        object.type = record.type;
        // 0x606660 no crea un objeto fuera de la grilla de sectores; el sector sale de la posición del
        // archivo, antes de los casos por tipo.
        object.sectorX = SectorOf(record.position[0]);
        object.sectorY = SectorOf(record.position[1]);
        if (object.sectorX < 0 || object.sectorY < 0)
        {
            continue;
        }
        object.transform.position = glm::vec3(record.position[0], record.position[1], record.position[2]);
        object.transform.angles = glm::vec3(record.angle[0], record.angle[1], record.angle[2]);
        object.transform.scale = record.scale;
        object.speed = ObjectSpeed;
        if (HasSceneCases(scene) && object.type == StreetLight)
        {
            object.transform.scale = StreetLightScale;
            object.light = glm::vec3(StreetLightLight);
        }
        // 0x606660, en las escenas 2 y 4: el 0xb4 es el dueño del dragón (06 § El dragón de fondo), no se
        // dibuja y va con luz blanca; el dragón es un efecto aparte.
        if (HasSceneCases(scene) && object.type == DragonOwner)
        {
            object.state.hiddenMesh = HiddenObject;
            object.light = glm::vec3(1.0f);
        }
        if (map == LoginMap && (object.type == RedWhirlpool || object.type == DarkWhirlpool))
        {
            object.transform.position = WhirlpoolPlace;
            object.transform.angles = glm::vec3(0.0f);
            object.transform.scale = object.type == RedWhirlpool ? RedWhirlpoolScale : DarkWhirlpoolScale;
        }
        objects.push_back(object);
    }
    return objects;
}

void StepWorldObject(WorldObject& object, std::uint8_t map, const Render::Models::Skeleton* skeleton)
{
    // 0x4e9980, con +0x16d en 0: el alpha un décimo hacia su meta, y la luz del blendMesh no por encima.
    // En la x87: la resta, el producto y la suma en double, y el resultado guardado en float.
    object.alpha = static_cast<float>(((static_cast<double>(object.targetAlpha) - static_cast<double>(object.alpha)) *
                                       static_cast<double>(AlphaApproach)) +
                                      static_cast<double>(object.alpha));
    object.state.light = std::min(object.state.light, object.alpha);
    // 0x604ab0 sale acá con el alpha por debajo de 0,01.
    if (object.alpha < MinimumAlpha)
    {
        return;
    }
    if (skeleton != nullptr && object.animation.action < skeleton->actions.size())
    {
        Render::Models::AdvanceAnimation(object.animation, skeleton->actions[object.animation.action], object.speed,
                                         Render::Models::AnimationScene::Other);
    }
    if (map != LoginMap)
    {
        return;
    }
    if (IsLoginMarker(object.type))
    {
        object.state.hiddenMesh = HiddenObject;
    }
    else if (object.type == RedWhirlpool)
    {
        object.transform.angles.z -= RedWhirlpoolTurn;
        object.alpha = RedWhirlpoolAlpha;
        object.light = glm::vec3(1.0f, 0.0f, 0.0f);
        object.state.blendMesh = Render::Models::EveryMeshBlends;
    }
    else if (object.type == DarkWhirlpool)
    {
        object.transform.angles.z -= DarkWhirlpoolTurn;
        object.state.blendMesh = Render::Models::EveryMeshBlends;
        object.alpha = 1.0f;
        object.light = glm::vec3(1.0f);
    }
}

ObjectDraw DrawOf(const WorldObject& object, const glm::vec3& terrainLight, std::uint8_t map)
{
    ObjectDraw draw;
    draw.alpha = object.alpha;
    draw.state = object.state;
    draw.vertexLight = object.vertexLight;
    // 0x5ffce0: con la luz por vértice, la del terreno más la del objeto; sin ella, la del terreno por
    // 0,1 (0x6b87b8) más la del objeto: el producto de G y B se guarda en float antes de la suma, y el de
    // R se suma desde la x87.
    if (object.vertexLight)
    {
        draw.color = terrainLight + object.light;
    }
    else
    {
        draw.color.x = static_cast<float>((static_cast<double>(terrainLight.x) * static_cast<double>(AlphaApproach)) +
                                          static_cast<double>(object.light.x));
        draw.color.y = (terrainLight.y * AlphaApproach) + object.light.y;
        draw.color.z = (terrainLight.z * AlphaApproach) + object.light.z;
    }
    // 0x5ffdf0 no dibuja con el alpha por debajo de 0,01, y 0x6002e0 sale con +0x58 = −2.
    draw.posed = !(object.alpha < MinimumAlpha);
    if (!draw.posed || object.state.hiddenMesh == HiddenObject)
    {
        draw.kind = ObjectDrawKind::Hidden;
        return draw;
    }
    if (map == LoginMap && object.type == DarkWhirlpool)
    {
        // El caso del mapa 55: la malla 0 sola, en blanco y sin luz, por la rama del blendMesh
        // (GL_ZERO, GL_ONE_MINUS_SRC_COLOR). Antes de su primer paso, sin el blendMesh, 0x4ecc30 no ata
        // textura ni cambia la mezcla (Q-C135): MuRet no la dibuja.
        draw.kind = object.state.blendMesh == Render::Models::EveryMeshBlends ? ObjectDrawKind::SubtractiveMesh
                                                                              : ObjectDrawKind::Hidden;
        draw.color = glm::vec3(1.0f);
        draw.alpha = 1.0f;
        draw.state.light = 1.0f;
        draw.vertexLight = false;
        return draw;
    }
    draw.kind = ObjectDrawKind::Body;
    return draw;
}

int SectorOf(float coordinate)
{
    // 0x60666c: el producto queda en la x87 (exacto) y ftol trunca hacia el 0: de −1.600 a 0 todavía es
    // el sector 0. Un NaN o algo fuera de los int da 0x80000000, que no es un sector.
    const double sector = static_cast<double>(coordinate) * static_cast<double>(SectorPerUnit);
    if (!(sector > -1.0 && sector < static_cast<double>(SectorCount)))
    {
        return -1;
    }
    return static_cast<int>(sector);
}

glm::vec3 StreetLightGlowLight(int random)
{
    constexpr int Span = 30;
    constexpr int Base = 70;
    constexpr double Step = static_cast<double>(0.01f);
    constexpr double Green = 0.5;
    constexpr double Blue = static_cast<double>(0.2f);
    // `cdq` e `idiv`: con un rand() no negativo, el resto.
    const auto light = static_cast<float>(static_cast<double>((random % Span) + Base) * Step);
    return {light, static_cast<float>(static_cast<double>(light) * Green),
            static_cast<float>(static_cast<double>(light) * Blue)};
}

int SectorX(const WorldObject& object)
{
    return object.sectorX;
}

int SectorY(const WorldObject& object)
{
    return object.sectorY;
}

std::vector<std::size_t> DrawOrder(std::span<const WorldObject> objects)
{
    std::vector<std::size_t> order(objects.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(),
                     [&objects](std::size_t a, std::size_t b)
                     {
                         const int sectorA = (SectorX(objects[a]) * SectorCount) + SectorY(objects[a]);
                         const int sectorB = (SectorX(objects[b]) * SectorCount) + SectorY(objects[b]);
                         return sectorA < sectorB;
                     });
    return order;
}
} // namespace Client::World
