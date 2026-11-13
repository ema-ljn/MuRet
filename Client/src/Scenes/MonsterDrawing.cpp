#include "Scenes/MonsterDrawing.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Data/Models/Model.h"
#include "GameLogic/CharacterAppearance.h"
#include "Render/ModelBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/ObjectState.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Render/Models/Skinning.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterCloth.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/GameCharacterEffects.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameCharacters.h"
#include "Scenes/GameHanging.h"
#include "Scenes/KalimaSiegeHelpers.h"
#include "Scenes/MonsterBody.h"
#include "Scenes/MonsterBodyPasses.h"
#include "Scenes/MonsterCloth.h"
#include "Scenes/MonsterHelpers.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/ChaosCastle.h"
#include "World/OriginalFtol.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "World/TerrainDecal.h"
#include "World/TerrainHeight.h"
#include "World/TerrainLighting.h"
#include "World/WorldTerrain.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <ratio>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Client::Scenes
{
namespace
{
using Render::Models::BoneTransform;
using Result = Core::Expected<void, std::string>;

constexpr std::uint8_t PlayerKind = 1;
constexpr std::uint8_t NpcKind = 4;
constexpr std::uint8_t UnwalledKind = 8;
constexpr std::uint8_t RedPk = 6;
constexpr std::int32_t FirstBloodCastle = 11;
constexpr std::int32_t LastBloodCastle = 17;
constexpr std::int32_t CastleMap = 30;
constexpr std::int32_t NoShadowMap = 10;
// 0x5ffdf0: por debajo de 0,01f (0x6b88d4) no posa ni dibuja; en Chaos Castle, con +0x1a1 del héroe, 0,15f menos
// (0x6b8ae8) en cada llamada.
constexpr float DrawnAlpha = std::bit_cast<float>(0x3c23d70au);
constexpr float ChaosCastleFade = std::bit_cast<float>(0x3e19999au);
// 0x50be04: la sombra desde 0,3f (0x6b89dc).
constexpr float ShadowAlpha = std::bit_cast<float>(0x3e99999au);
// 0x5ffdf0..0x600135: las constantes del contorno.
constexpr float OutlineScale = std::bit_cast<float>(0x3f99999au);      // 1,2
constexpr float InnerOutlineScale = std::bit_cast<float>(0x3f8a3d71u); // 1,08
constexpr float MinimumOutline = std::bit_cast<float>(0x3f8147aeu);    // 1,01
constexpr float NpcOutlineStep = std::bit_cast<float>(0x3df5c28fu);    // 0,12 (0x6ba1d8)
constexpr float OutlineStep = std::bit_cast<float>(0x3d23d70au);       // 0,04 (0x6b8aec)
constexpr float OuterSpan = std::bit_cast<float>(0x3dcccccdu);         // 0,1 (0x6b87b8)
// 0x50b870 y 0x50b920: sin(T · 0,002f (0x6b89bc)) · 0,3f (0x6b89dc).
constexpr float PulseRate = std::bit_cast<float>(0x3b03126fu);
constexpr float PulseSpan = std::bit_cast<float>(0x3e99999au);
constexpr std::uint16_t WideOutlineType = 0x172;
constexpr std::uint16_t KalimaGate = 0xc5;                                       // los monstruos 152 a 158 (0x514390)
constexpr auto ArmorPart = static_cast<std::size_t>(GameLogic::BodyPart::Armor); // +0x238
constexpr std::uint32_t RiderFlag = 0x4000;

float F32(double value)
{
    return static_cast<float>(value);
}

double D(float value)
{
    return static_cast<double>(value);
}

bool InChaosCastle(std::int32_t map)
{
    return World::IsChaosCastleMap(map);
}

bool InBloodCastle(std::int32_t map)
{
    return map >= FirstBloodCastle && map <= LastBloodCastle;
}

bool Within(std::uint16_t value, std::uint16_t first, std::uint16_t last)
{
    return value >= first && value <= last;
}

template <std::size_t N> bool OneOf(std::uint16_t value, const std::array<std::uint16_t, N>& values)
{
    return std::ranges::find(values, value) != values.end();
}

Result Cut(std::string_view address, std::string_view what, std::uint16_t type)
{
    return Core::Unexpected(std::format("{}: {} (el tipo {:#x}) no está portado", address, what, type));
}

// 0x50bd05..0x50bd7e: el cuarto argumento de 0x602e90, el +0x376 si está en la lista y si no 0.
std::uint16_t SpecialOf(std::uint16_t monster)
{
    constexpr std::array<std::uint16_t, 6> Listed{0x43, 0x4a, 0x4b, 0x87, 0x88, 0x89};
    if (OneOf(monster, Listed) || Within(monster, 0x12c, 0x12f) || Within(monster, 0x13a, 0x13f))
        return monster;
    return 0;
}

// 0x5ffce0: el color del modelo. El 0x189 en 0,6 y el 0xe4 con el seno del reloj no escriben la luz por vértice
// (+0x44 del modelo), que solo ponen en 0 los contornos: queda apagada (Q-C859, *inferido*). Los demás, la luz del
// terreno (0x5f99e0) más la propia (+0xf4), con la luz por vértice, o un décimo de ella sin.
struct ModelLight
{
    glm::vec3 color{1.0f};
    bool vertexLight = false;
};

ModelLight ModelLightOf(const ActorState& actor, const World::TerrainLighting& lighting, float worldClock)
{
    if (actor.type == 0x189)
        return {glm::vec3(std::bit_cast<float>(0x3f19999au)), false};
    if (actor.type == 0xe4)
    {
        const float shade = F32((std::sin(D(worldClock) * D(std::bit_cast<float>(0x3b449ba6u))) + 1.0) * 0.5);
        return {glm::vec3(shade), false};
    }
    const glm::vec3 terrain = World::LightAt(lighting, actor.position.x, actor.position.y);
    if (actor.byteE8 != 0)
        return {{F32(D(terrain.x) + D(actor.light.x)), F32(D(terrain.y) + D(actor.light.y)),
                 F32(D(terrain.z) + D(actor.light.z))},
                true};
    const float tenth = OuterSpan;
    return {{F32(D(terrain.x) * D(tenth) + D(actor.light.x)), F32(D(F32(D(terrain.y) * D(tenth))) + D(actor.light.y)),
             F32(D(F32(D(terrain.z) * D(tenth))) + D(actor.light.z))},
            false};
}

// 0x5fffbd..0x600135: los dos pases del contorno de 0x5ffdf0, con los colores del NPC (+0x88 en 4, fuera de Chaos
// Castle) o los de los demás, y la escala de +0x94 o la del objeto (+0xc). 0x622150 es el mayor de los dos (con
// empate, el segundo).
std::array<ObjectOutlinePass, 2> OutlinePassesOf(const ActorState& actor, std::int32_t map)
{
    const float scale94 = actor.float94;
    const auto atLeast = [](float value) { return value > MinimumOutline ? value : MinimumOutline; };
    if (!InChaosCastle(map) && actor.byte88 == NpcKind)
    {
        return {{{scale94, Render::Color{std::bit_cast<float>(0x3ca3d70au), OuterSpan, 0.0f, 1.0f}},
                 {atLeast(F32(D(scale94) - D(NpcOutlineStep))),
                  Render::Color{std::bit_cast<float>(0x3e23d70au), std::bit_cast<float>(0x3f333333u), 0.0f, 1.0f}}}};
    }
    const bool fixed = std::bit_cast<std::uint32_t>(scale94) == std::bit_cast<std::uint32_t>(OutlineScale);
    float outer = actor.type == WideOutlineType ? OutlineScale : F32(D(OuterSpan) / D(actor.scale) + 1.0);
    if (!fixed)
        outer = scale94;
    float inner = actor.type == WideOutlineType ? InnerOutlineScale : F32(D(OutlineStep) / D(actor.scale) + 1.0);
    if (!fixed)
        inner = atLeast(F32(D(scale94) - D(OutlineStep)));
    return {{{outer, Render::Color{OuterSpan, std::bit_cast<float>(0x3c23d70au), 0.0f, 1.0f}},
             {inner, Render::Color{std::bit_cast<float>(0x3f333333u), std::bit_cast<float>(0x3d8f5c29u), 0.0f, 1.0f}}}};
}

// La altura del terreno (0x5f93c0) del terreno del pase o, sin él, del mapa del mundo.
std::optional<double> GroundHeight(const WorldState& world, const GameCharacterPass& pass, float x, float y)
{
    const World::WorldTerrain& terrain = pass.terrain != nullptr ? *pass.terrain : world.Map().terrain;
    if (!terrain.heights || !terrain.attributes)
        return std::nullopt;
    return World::TerrainHeightAt(*terrain.heights, *terrain.attributes, x, y, pass.fixedHeight);
}

// 0x4eb860 (0x4eb86d..0x4eb94c): la key que deja en +0xa8 del modelo, el frame (+0x114) por 0x69fd3c en un short, en 0
// si es negativa o si llega a las keys de la acción del modelo (+0xa0, en 0 si el modelo no la tiene). Sin acciones no
// la toca; con un frame infinito, el original no está observado: tampoco.
std::optional<std::int16_t> PoseKeyOf(const Render::Models::Skeleton& skeleton,
                                      const Render::Models::AnimationState& animation)
{
    const auto truncated = World::OriginalFtolLow(D(animation.frame));
    if (skeleton.actions.empty() || !truncated)
        return std::nullopt;
    auto key = static_cast<std::int16_t>(*truncated);
    if (key < 0)
        key = 0;
    const std::size_t action = animation.action < skeleton.actions.size() ? animation.action : 0;
    if (key >= skeleton.actions[action].keyCount)
        key = 0;
    return key;
}

// Lo que dibuja un monstruo en un frame: el personaje (el registro o su copia), el rand() y los recursos.
struct MonsterFrame
{
    Render::ModelBatch& batch;
    const IModelLibrary& items;
    const IModelLibrary& fauna;
    WorldState& world;
    ActorState& actor;
    const GameCharacterPass& pass;
    Render::Models::CrtRandom& random;
    std::size_t slot = 0;
    std::int32_t map = 0;
    float worldClock = 0.0f;
    bool originalFrame = true;
};

// 0x50bae7..0x50bbca: la tela sobre la malla (MonsterCloth.h) entre 0x5ffdf0 y 0x6002e0, sobre `meshes` (lo que dejó
// 0x4ebc80 en 0x5786f9c) con los huesos `bones` y la posición del modelo. Entre frames del original, la de antes.
Result StepTheMeshCloth(MonsterFrame& frame, std::span<Render::Models::SkinnedMesh> meshes,
                        std::span<const BoneTransform> bones, const glm::vec3& position)
{
    const ActorState& actor = frame.actor;
    if (frame.slot >= frame.pass.cloths.size())
        return Core::Unexpected<std::string>("0x50bb96: falta la tela persistente del lugar del personaje");
    // objeto + 0x190: la del lugar, en sus telas.
    CharacterCloth& worn = frame.pass.cloths[frame.slot];
    MonsterMeshCloth& cloth = worn.OnTheMesh();
    const Render::Models::ClothBody body{
        .bones = bones, .angles = actor.angles, .scale = actor.scale, .position = position};
    if (!frame.originalFrame)
    {
        cloth.DrawCurrent(meshes, body);
        return {};
    }
    const Data::Models::Model* model = frame.fauna.Find(actor.type);
    if (model == nullptr)
        return Core::Unexpected(std::format("0x455ea0: falta el modelo {:#x} de la tela sobre la malla", actor.type));
    return cloth.Frame(*model, actor.type, meshes, body, *frame.pass.clothWorld, static_cast<std::uint8_t>(frame.map));
}

// 0x5ffdf0 (con su 0x5ffce0) y 0x6002e0 sin casos: la pose en los huesos propios, el contorno y el cuerpo. Con la tela
// sobre la malla (0x50bae7), 0x5ffdf0 y 0x6002e0 van separados y la tela entre los dos, con el cuarto argumento en 0.
Result DrawTheBody(MonsterFrame& frame, const Render::Models::Skeleton& skeleton, std::uint16_t special)
{
    ActorState& actor = frame.actor;
    const bool cloth = WearsClothOnTheMesh(actor.word376);
    // 0x5ffdf0: en Chaos Castle, con +0x1a1 del héroe, el alpha guardado baja 0,15f.
    if (InChaosCastle(frame.map) && frame.world.Actors()[frame.world.Runtime().hero].byte1a1 != 0)
        actor.alpha = F32(D(actor.alpha) - D(ChaosCastleFade));
    if (actor.alpha < DrawnAlpha)
    {
        if (!cloth)
            return {};
        // 0x50bafd: sin dibujo, la tela se arma y se simula igual, con lo que el último 0x4ebc80 dejó en 0x5786f9c, de
        // otro modelo; MuRet usa la pose guardada de este (Q-C862, *inferido*).
        if (actor.bodyPose.empty())
            return Core::Unexpected<std::string>(
                "0x50bb3b: la tela sobre la malla de un monstruo que no se dibujó lee 0x5786f9c de otro modelo");
        DrawnObject stale;
        stale.model = actor.type;
        stale.transform = {.position = actor.position, .angles = actor.angles, .scale = actor.scale};
        std::vector<Render::Models::SkinnedMesh> meshes = SkinObjectMeshes(frame.fauna, stale, actor.bodyPose);
        return StepTheMeshCloth(frame, meshes, actor.bodyPose, actor.position);
    }
    // 0x5ffe9d..0x5ffeef: el 0x18f y el 0x18e van 60 y 120 más allá en y (0x6b86cc y 0x6b86c8).
    Render::Models::ObjectTransform transform{.position = actor.position, .angles = actor.angles, .scale = actor.scale};
    if (actor.type == 0x18f)
        transform.position.y = F32(D(actor.position.y) + 60.0);
    else if (actor.type == 0x18e)
        transform.position.y = F32(D(actor.position.y) + 120.0);
    // 0x5ffef2: con un dueño (+0x108) con +0x78 & 0x1000, el frame (+0x114) a 0. Ninguna alta ni paquete escribe el
    // +0x108 de un personaje del pool: queda en 0 (Q-C224, *inferido*).
    // 0x4eb860 en el buffer propio (+0x120, que 0x511290 le dio a todo personaje): los huesos dummy quedan como los
    // dejó el dibujo anterior (Q-C318).
    std::vector<BoneTransform> bones(skeleton.bones.size(), BoneTransform(0.0f));
    std::copy_n(actor.bodyPose.begin(), std::min(actor.bodyPose.size(), bones.size()), bones.begin());
    if (!Render::Models::PoseSkeleton(skeleton, actor.animation, transform, actor.headTurn.angles, bones))
        return Core::Unexpected(std::format("0x4eb860: la pose del modelo {:#x} falló", actor.type));
    actor.bodyPose = bones;
    // 0x4eb8cd..0x4eb94c: la key en +0xa8 del modelo compartido, que lee el paso del 0x1f5 (0x4fce92). Entre frames del
    // original no se guarda.
    if (frame.originalFrame)
    {
        if (const auto key = PoseKeyOf(skeleton, actor.animation))
            frame.world.FaunaResources().RecordPoseKey(actor.type, *key);
    }
    if (frame.pass.dynamicLighting == nullptr)
        return Core::Unexpected<std::string>("0x5ffce0: falta el buffer dinámico de luz del terreno");
    const ModelLight light = ModelLightOf(actor, *frame.pass.dynamicLighting, frame.worldClock);
    DrawnObject object;
    object.model = actor.type;
    object.animation = actor.animation;
    object.transform = transform;
    // +0x6c y +0x70 (la U y la V) no los escribe ninguna alta: en 0 (*inferido*).
    object.state = Render::Models::ObjectState{.blendMesh = static_cast<std::int16_t>(actor.dword64),
                                               .light = actor.blendMeshLight,
                                               .uv = {},
                                               .hiddenMesh = static_cast<std::int16_t>(actor.dword58)};
    object.color = light.color;
    object.alpha = actor.alpha;
    object.vertexLight = light.vertexLight;
    // 0x5fffb4: el contorno, salvo con +0x78 & 0x4000 (0x60cd74).
    const auto slot = static_cast<std::int32_t>(frame.slot);
    if ((slot == frame.pass.hovered || slot == frame.pass.targeted) && (actor.flags78 & RiderFlag) == 0)
    {
        const auto passes = OutlinePassesOf(actor, frame.map);
        // 0x60cdd9: el 0xc5 (las puertas de Kalima) solo con su malla 0, y con el +0x58 de textura (0x60ce4e).
        if (actor.type == KalimaGate && actor.dword58 != -1)
            return Cut("0x60ce4e", "la textura +0x58 del contorno del 0xc5", actor.type);
        if (actor.type == KalimaGate)
            DrawObjectOutlineOfTheFirstMesh(frame.batch, frame.fauna, object, frame.pass.frameClock, bones, passes);
        else
            DrawObjectOutline(frame.batch, frame.fauna, object, frame.pass.frameClock, bones, passes);
    }
    // 0x600177: el último 0x4ebc80 de 0x5ffdf0, con la escala en 1, deja las mallas en 0x5786f9c.
    std::vector<Render::Models::SkinnedMesh> meshes = SkinObjectMeshes(frame.fauna, object, bones);
    if (cloth)
    {
        if (auto stepped = StepTheMeshCloth(frame, meshes, bones, transform.position); !stepped.HasValue())
            return stepped;
    }
    // 0x6002e0: con +0x58 en −2 (sin el modo editor, 0x77f3b18) no dibuja nada.
    if (actor.dword58 == -2)
        return {};
    // 0x600436..0x602e7d: los casos por tipo y por mapa, el cuerpo y lo que va después (MonsterBody.h).
    MonsterBodyModel model{.batch = frame.batch,
                           .models = frame.fauna,
                           .object = object,
                           .meshes = meshes,
                           .frameClock = frame.pass.frameClock};
    MonsterBodyFrame body{.model = model,
                          .actor = actor,
                          .bones = bones,
                          .map = frame.map,
                          .special = special,
                          .worldClock = frame.worldClock,
                          .random = frame.random,
                          .cloth = *frame.pass.clothWorld,
                          .siegeActive = frame.world.Runtime().siegeActive,
                          .lastSoundId = &frame.world.Runtime().lastSoundId,
                          .originalFrame = frame.originalFrame};
    return DrawMonsterBody(body);
}

// 0x50bed8..0x50bf3e: el calco 0x61e del +0x376 0xe8 y 0xe9, aditivo (0x620a90), con v = sin(T · 0,0015f) · 0,3f +
// 0,8f en la x87 y la luz (v · 0,5; v · 0,5; v): 0x5fb2f0(0x61e, x, y, 2,7f, 2,7f, &luz, −giro z, 1, 5). Después,
// +0x58 vuelve a −1.
Result DrawTheFloorMark(MonsterFrame& frame)
{
    constexpr std::int32_t Mark = 0x61e;
    constexpr float Side = std::bit_cast<float>(0x402ccccdu); // 2,7
    constexpr float Rate = std::bit_cast<float>(0x3ac49ba6u); // 0x6b8a60: 0,0015
    constexpr float Span = std::bit_cast<float>(0x3e99999au); // 0x6b89dc: 0,3
    constexpr float Base = std::bit_cast<float>(0x3f4ccccdu); // 0x6b8a38: 0,8
    constexpr float Lift = std::bit_cast<float>(0x40a00000u); // 5
    ActorState& actor = frame.actor;
    // 0x620a90: ONE / ONE, sin culling ni alpha test y sin escribir la profundidad.
    Render::ModelDrawState state = frame.batch.State();
    state.blend = Render::ModelBlend::Additive;
    state.cullEnabled = false;
    state.depthWrite = false;
    state.alphaTest = false;
    state.texture2d = true;
    constexpr std::array Operations{Render::ModelStateOperation::Blend, Render::ModelStateOperation::Cull,
                                    Render::ModelStateOperation::DepthWrite, Render::ModelStateOperation::AlphaTest,
                                    Render::ModelStateOperation::Texture2d};
    for (const Render::ModelStateOperation operation : Operations)
        frame.batch.ApplyOperation(state, operation);
    const World::WorldTerrain& terrain =
        frame.pass.terrain != nullptr ? *frame.pass.terrain : frame.world.Map().terrain;
    if (!terrain.heights)
        return Core::Unexpected<std::string>("0x5fb2f0: faltan las alturas del terreno del calco del 0xe8 y el 0xe9");
    const double pulse = std::sin(D(frame.worldClock) * D(Rate)) * D(Span) + D(Base);
    const float half = F32(pulse * D(std::bit_cast<float>(0x3f000000u)));
    const World::TerrainDecal decal{.slot = Mark,
                                    .x = actor.position.x,
                                    .y = actor.position.y,
                                    .width = Side,
                                    .height = Side,
                                    .color = {half, half, F32(pulse)},
                                    .angle = -actor.angles.z,
                                    .alpha = 1.0f,
                                    .lift = Lift};
    auto drawn = World::DrawTerrainDecal(frame.batch, *terrain.heights, decal);
    actor.dword58 = -1;
    return drawn;
}

// 0x4ec110(huesos propios + hueso · 0x30, corrimiento, salida, 1) de 0x50ba30.
glm::vec3 PointOnTheBone(const ActorState& actor, std::size_t bone, const glm::vec3& offset)
{
    const BoneTransform matrix = bone < actor.bodyPose.size() ? actor.bodyPose[bone] : BoneTransform(0.0f);
    return Render::Models::OriginalTransformPosition(Render::Models::OriginalMatrixOf(matrix), offset, actor.scale,
                                                     actor.position);
}

// 0x50bf98..0x50c0f0, el +0x376 0x45: x = (rand() % 30 + 70) · 0,01f en la x87; nueve flare01 (0x5ac) de 0,6f con la
// luz (x · 0,8f; x · 0,9f; x) en los huesos de la tabla de 0x6c88a0, y tres 0x53fe70(0x5c6, punto, ángulos, luz, 3, 1,
// 0) con la luz (v · 0,6f; v · 0,7f; x · 0,8f), v = f(x), cada una en un hueso al azar del modelo (rand() % +0x22)
// corrido (rand() % 20 − 10) en x, y y z: cuatro rand() antes de cada una. El corrimiento queda en [ebp−0x50], que un
// monstruo no vuelve a leer antes de 0x50ea23. Entre frames del original no crea las partículas.
Result DrawTheSparksOf0x45(MonsterFrame& frame)
{
    constexpr std::array<std::size_t, 9> Bones{10, 18, 37, 38, 51, 52, 58, 59, 66};
    constexpr std::uint16_t Flare = 0x5ac;
    constexpr float FlareScale = std::bit_cast<float>(0x3f19999au); // 0,6
    constexpr std::int32_t Spark = 0x5c6;
    constexpr std::int32_t SparkSubtype = 3;
    constexpr int Sparks = 3;
    const ActorState& actor = frame.actor;
    const auto point = [&actor](std::size_t bone, const glm::vec3& offset)
    { return PointOnTheBone(actor, bone, offset); };
    if (frame.pass.sprites == nullptr)
        return Core::Unexpected<std::string>("0x50c014: falta la tabla de los sprites del frame (0x5491e0)");
    const double x = static_cast<double>(frame.random.Next() % 30 + 70) * D(std::bit_cast<float>(0x3c23d70au));
    const float v = F32(x);
    const float red = F32(x * D(std::bit_cast<float>(0x3f4ccccdu)));
    const glm::vec3 flareLight{red, F32(x * D(std::bit_cast<float>(0x3f666666u))), v};
    for (const std::size_t bone : Bones)
        static_cast<void>(
            frame.pass.sprites->Create(Flare, point(bone, glm::vec3(0.0f)), FlareScale, flareLight, 0.0f, 0));
    const glm::vec3 sparkLight{F32(D(v) * D(std::bit_cast<float>(0x3f19999au))),
                               F32(D(v) * D(std::bit_cast<float>(0x3f333333u))), red};
    const Render::Models::Skeleton* skeleton = frame.fauna.SkeletonOf(actor.type);
    const auto bones = static_cast<std::int16_t>(skeleton != nullptr ? skeleton->bones.size() : 0);
    for (int spark = 0; spark < Sparks; ++spark)
    {
        glm::vec3 offset{};
        for (int axis = 0; axis < 3; ++axis)
            offset[axis] = static_cast<float>(static_cast<std::int32_t>(frame.random.Next() % 20) - 10);
        const std::int32_t drawn = frame.random.Next();
        if (bones <= 0)
            return Cut("0x50c0a9", "el hueso al azar de un modelo sin huesos (IDIV por 0) del +0x376 0x45", actor.type);
        const auto bone = static_cast<std::size_t>(drawn % bones);
        if (!frame.originalFrame)
            continue;
        if (frame.pass.particles == nullptr)
            return Core::Unexpected<std::string>("0x50c0db: falta el arreglo de las partículas (0x53fe70)");
        const auto created = frame.pass.particles->CreateChecked(Spark, point(bone, offset), actor.angles, sparkLight,
                                                                 SparkSubtype, 1.0f, frame.worldClock, frame.random);
        if (!created.HasValue())
            return Core::Unexpected(std::format("0x50c0db: la partícula {:#x}/{} del +0x376 0x45 falla con el error {}",
                                                Spark, SparkSubtype, static_cast<int>(created.Error())));
    }
    return {};
}

// 0x50c0fb..0x50c136, el +0x376 0x46: un flare01 (0x5ac) de 0,8f en el hueso 20 (+0x3c0), con el corrimiento de
// [ebp−0x50] (en 0 desde el prólogo) y la luz de [ebp−0x44] (1, 1, 1 desde 0x50bab9).
Result DrawTheFlareOf0x46(MonsterFrame& frame)
{
    constexpr std::size_t Bone = 20;
    constexpr std::uint16_t Flare = 0x5ac;
    constexpr float FlareScale = std::bit_cast<float>(0x3f4ccccdu); // 0,8
    if (frame.pass.sprites == nullptr)
        return Core::Unexpected<std::string>("0x50c131: falta la tabla de los sprites del frame (0x5491e0)");
    static_cast<void>(frame.pass.sprites->Create(Flare, PointOnTheBone(frame.actor, Bone, glm::vec3(0.0f)), FlareScale,
                                                 glm::vec3(1.0f), 0.0f, 0));
    return {};
}

// 0x50bf45..0x50c904: lo que se agrega por el +0x376. El 0x45, hasta sus partículas, y el 0x46; los demás cortan.
Result DrawTheDecorations(MonsterFrame& frame)
{
    if (frame.actor.word376 == 0x45)
        return DrawTheSparksOf0x45(frame);
    if (frame.actor.word376 == 0x46)
        return DrawTheFlareOf0x46(frame);
    return CheckMonsterDecorations(frame.actor.word376);
}

// 0x50bd89..0x50bf3e: la sombra de los monstruos.
Result DrawTheShadow(MonsterFrame& frame)
{
    ActorState& actor = frame.actor;
    constexpr std::array<std::uint16_t, 7> Unshadowed{0x19, 0x16, 0x2a, 0xf2, 0x3b, 0x3f, 0x98};
    if (actor.byte88 == UnwalledKind || OneOf(actor.word376, Unshadowed) || frame.map == NoShadowMap ||
        actor.alpha < ShadowAlpha)
        return {};
    // 0x50be15..0x50be50: en Blood Castle, con +0x1a1 y muriéndose, la z baja a la del terreno si es menor.
    if (InBloodCastle(frame.map) && actor.byte1a1 != 0 && actor.dying != 0)
    {
        const auto ground = GroundHeight(frame.world, frame.pass, actor.position.x, actor.position.y);
        if (!ground)
            return Core::Unexpected<std::string>("0x50be3b: falta el terreno de 0x5f93c0");
        if (*ground < D(actor.position.z))
            actor.position.z = F32(*ground);
    }
    const bool marked = actor.word376 == 0xe8 || actor.word376 == 0xe9;
    if (marked)
        actor.dword58 = 2;
    // 0x50be6e: sin sombra el 0x18e, el 0x18f y del 0xa8 al 0xac. 0x60cf70 → 0x60b3d0 con +0x98 en 1, sin +0x1bc en
    // 3 (MuRet no lo tiene: 0, *inferido*), sin +0x78 & 0x4000 y con +0x92.
    if (actor.type != 0x18e && actor.type != 0x18f && !Within(actor.type, 0xa8, 0xac) &&
        (actor.flags78 & RiderFlag) == 0 && actor.byte92 != 0 && actor.bodyPose.size() > 0)
    {
        DrawnObject object;
        object.model = actor.type;
        object.transform = {.position = actor.position, .angles = actor.angles, .scale = actor.scale};
        object.state.blendMesh = static_cast<std::int16_t>(actor.dword64);
        object.state.hiddenMesh = static_cast<std::int16_t>(actor.dword58);
        const auto meshes = SkinObjectMeshes(frame.fauna, object, actor.bodyPose);
        DrawObjectShadow(frame.batch, frame.fauna, object, meshes, frame.map);
    }
    if (marked)
        return DrawTheFloorMark(frame);
    return {};
}

// 0x50c972: con +0x1f2 (que solo escriben los paquetes de los jugadores; 0x511290 lo deja en 0xffff), los colores de la
// guerra de gremios (0x576796d) antes de la luz de 0x50cc55.
Result CheckTheGuild(const ActorState& actor)
{
    if (actor.word1f2 != NoItemModel)
        return Cut("0x50c99b", "el gremio de un personaje que no es un jugador", actor.type);
    return {};
}

// 0x50d363..0x50d58b y 0x50c929..0x50c95f: lo que 0x50ba30 viste en un personaje que no es el 0x20a.
Result CheckTheParts(const ActorState& actor, std::int32_t map)
{
    if (actor.byte3e3 == 0)
    {
        if (actor.byte88 == PlayerKind && InChaosCastle(map))
            return Cut("0x50d383", "el Angel de Chaos Castle en un personaje que no es un jugador", actor.type);
        const bool dressed = actor.head != NoItemModel || std::ranges::any_of(actor.body, [](const ActorItemPart& part)
                                                                              { return part.model != NoItemModel; });
        if (dressed)
            return Cut("0x50d3e3", "el lazo de las partes (0x60cf70) en un personaje que no es un jugador", actor.type);
    }
    return {};
}

// 0x50c929..0x50c96e y 0x50d58b..0x50db57: en un personaje que no es el 0x20a, bVar23 es solo el +0x376 0x37 fuera de
// los mapas 0x12 a 0x17; con él, el bloque de las telas de CharacterCloth sobre los huesos propios (+0x120) con el
// blanco de 0x50c979 (+0x1f2 en 0xffff). Entre frames del original, las de antes.
Result RunTheCloth(MonsterFrame& frame)
{
    constexpr std::uint16_t ClothFlare = 0x5ac; // flare01, aditivo (0x5491e0 con 0)
    const ActorState& actor = frame.actor;
    const auto map = static_cast<std::uint8_t>(frame.map);
    const bool wears = WearsTheMonsterCloth(actor.word376, map);
    if (!wears)
        return {};
    if (frame.slot >= frame.pass.cloths.size())
        return Core::Unexpected<std::string>("0x50d58b: falta la tela persistente del lugar del personaje");
    CharacterCloth& cloth = frame.pass.cloths[frame.slot];
    const ClothWearer wearer{
        .characterClass = actor.characterClass,
        .armor = actor.body[ArmorPart].model,
        .wings = actor.wings.model == NoItemModel ? std::nullopt : std::optional<std::uint16_t>(actor.wings.model),
        .monster = actor.word376,
        .objectFlags = actor.flags78};
    const Render::Models::ClothBody body{
        .bones = actor.bodyPose, .angles = actor.angles, .scale = actor.scale, .position = actor.position};
    if (frame.originalFrame)
        cloth.Frame(frame.batch, wearer, body, *frame.pass.clothWorld, map, frame.worldClock, wears);
    else
        cloth.DrawCurrent(frame.batch, wearer, body, frame.pass.frameClock, wears);
    if (frame.pass.sprites != nullptr)
    {
        for (const Render::Models::ClothFlare& flare : cloth.Flares())
            static_cast<void>(
                frame.pass.sprites->Create(ClothFlare, flare.position, flare.scale, glm::vec3(flare.light), 0.0f, 0));
    }
    return {};
}

// 0x50b870(objeto, sprite, escala, hueso, x, y, z): el punto del hueso propio con la escala y la posición del modelo y
// el sprite aditivo con la luz sin(T · 0,002f) · 0,3f + 0,7f, por 0,6f y 0,4f en verde y azul (con la luz en la pila).
void BoneSprite(const MonsterFrame& frame, std::uint16_t texture, float scale, std::size_t bone,
                const glm::vec3& offset)
{
    const ActorState& actor = frame.actor;
    const BoneTransform matrix = bone < actor.bodyPose.size() ? actor.bodyPose[bone] : BoneTransform(0.0f);
    const glm::vec3 point = Render::Models::OriginalTransformPosition(Render::Models::OriginalMatrixOf(matrix), offset,
                                                                      actor.scale, actor.position);
    const double lum =
        std::sin(D(frame.worldClock) * D(PulseRate)) * D(PulseSpan) + D(std::bit_cast<float>(0x3f333333u));
    const glm::vec3 light{F32(lum), F32(lum * D(std::bit_cast<float>(0x3f19999au))),
                          F32(lum * D(std::bit_cast<float>(0x3ecccccdu)))};
    static_cast<void>(frame.pass.sprites->Create(texture, point, scale, light, 0.0f, 0));
}

// 0x50b920(objeto, hueso a, hueso b, escala): el 0x5ff en (5, 0, 0) del hueso a y en (−5, 0, 0) del b, con la luz
// sin(T · 0,002f) · 0,3f + 0,8f guardada; el último punto queda en +0x40..+0x48.
void PairOfSprites(MonsterFrame& frame, std::size_t first, std::size_t second, float scale)
{
    ActorState& actor = frame.actor;
    const float lum =
        F32(std::sin(D(frame.worldClock) * D(PulseRate)) * D(PulseSpan) + D(std::bit_cast<float>(0x3f4ccccdu)));
    const auto at = [&](std::size_t bone, float x)
    {
        const BoneTransform matrix = bone < actor.bodyPose.size() ? actor.bodyPose[bone] : BoneTransform(0.0f);
        return Render::Models::OriginalTransformPosition(Render::Models::OriginalMatrixOf(matrix), {x, 0.0f, 0.0f},
                                                         actor.scale, actor.position);
    };
    constexpr std::uint16_t Glow = 0x5ff;
    static_cast<void>(frame.pass.sprites->Create(Glow, at(first, 5.0f), scale, glm::vec3(lum), 0.0f, 0));
    const glm::vec3 last = at(second, -5.0f);
    static_cast<void>(frame.pass.sprites->Create(Glow, last, scale, glm::vec3(lum), 0.0f, 0));
    actor.effectOrigin = last;
}

struct BoneSpriteCall
{
    std::uint16_t texture = 0;
    float scale = 1.0f;
    std::uint8_t bone = 0;
    float z = 0.0f;
};

void BoneSprites(const MonsterFrame& frame, std::span<const BoneSpriteCall> calls)
{
    for (const BoneSpriteCall& call : calls)
        BoneSprite(frame, call.texture, call.scale, call.bone, {0.0f, 0.0f, call.z});
}

// 0x510763..0x5108b1, el 0x1d9: el punto (3,5; −12; 10) del hueso 20, dos 0x5d5 a 0,3 con la luz sin(T · 0,002f) ·
// 0,35f (0x6b8bc4) + 0,65f (0x6b8bc0) guardada y girados T · 0,02f (0x6b89e4) y −T · 0,02f, y con rand() % 30 en 0,
// tres rand() % 100 más el punto menos 50 (0x6b86b0), en x, y y z, para el joint 0x618/6 (0x530860) desde ahí hacia
// el punto, con los ángulos del objeto, sin dueño y de ancho 20.
Result DrawTheFairy(MonsterFrame& frame)
{
    const ActorState& actor = frame.actor;
    constexpr std::size_t Bone = 20;
    constexpr std::uint16_t Flare = 0x5d5;
    const BoneTransform matrix = Bone < actor.bodyPose.size() ? actor.bodyPose[Bone] : BoneTransform(0.0f);
    glm::vec3 point = Render::Models::OriginalTransformPosition(Render::Models::OriginalMatrixOf(matrix),
                                                                {3.5f, -12.0f, 10.0f}, actor.scale, actor.position);
    const float lum = F32(std::sin(D(frame.worldClock) * D(PulseRate)) * D(std::bit_cast<float>(0x3eb33333u)) +
                          D(std::bit_cast<float>(0x3f266666u)));
    const float spin = std::bit_cast<float>(0x3ca3d70au);
    const float scale = std::bit_cast<float>(0x3e99999au);
    static_cast<void>(
        frame.pass.sprites->Create(Flare, point, scale, glm::vec3(lum), F32(D(frame.worldClock) * D(spin)), 0));
    static_cast<void>(
        frame.pass.sprites->Create(Flare, point, scale, glm::vec3(lum), F32(-D(frame.worldClock) * D(spin)), 0));
    if (frame.random.Next() % 30 != 0)
        return {};
    const auto scatter = [&frame](float axis)
    {
        constexpr double Spread = 50.0;
        return F32(static_cast<double>(frame.random.Next() % 100) + D(axis) - Spread);
    };
    glm::vec3 from{};
    from.x = scatter(point.x);
    from.y = scatter(point.y);
    from.z = scatter(point.z);
    if (frame.pass.joints.empty())
        return Core::Unexpected<std::string>("0x5108a9: faltan los joints del frame (0x530860)");
    glm::vec3 angles = actor.angles;
    constexpr std::int32_t FairyJoint = 0x618;
    constexpr std::int32_t FairySubtype = 6;
    constexpr float Width = 20.0f;
    const auto created = CreateFaunaJoint(
        frame.pass.joints, {FairyJoint, from, point, angles, FairySubtype, nullptr, Width, -1, 0, 0, -1}, frame.random);
    if (!created.HasValue())
        return Core::Unexpected(std::format("0x5108a9: {}", created.Error()));
    return {};
}

// Lo que leen los helpers de KalimaSiegeHelpers.h: el pase del frame y, en el 0x42a1b0, las alturas del terreno del
// pase (sin él, las del mapa del mundo). El sonido, solo en el frame del original.
KalimaSiegeHelperPass HelperPassOf(MonsterFrame& frame)
{
    const World::WorldTerrain& terrain =
        frame.pass.terrain != nullptr ? *frame.pass.terrain : frame.world.Map().terrain;
    return {.batch = frame.batch,
            .actor = frame.actor,
            .random = frame.random,
            .sprites = frame.pass.sprites,
            .particles = frame.pass.particles,
            .joints = frame.pass.joints,
            .effects = frame.pass.effects,
            .kalima = frame.pass.kalima,
            .cameraShake = frame.pass.cameraShake,
            .heights = terrain.heights.get(),
            .sound = frame.originalFrame ? &frame.world.Runtime().lastSoundId : nullptr,
            .worldClock = frame.worldClock,
            .map = frame.map,
            .siegeActive = frame.world.Runtime().siegeActive};
}

// 0x50e9fa..0x510a1e: el switch por modelo (la tabla de 0x510b80 sobre 0x510b50). Devuelve true si el tipo tiene su
// caso, que salta la cadena de 0x510a1e.
Core::Expected<bool, std::string> DrawTheModelCase(MonsterFrame& frame)
{
    const std::uint16_t type = frame.actor.type;
    constexpr std::array<std::uint16_t, 11> Cased{0x152, 0x170, 0x186, 0x177, 0x174, 0x176,
                                                  0x173, 0x1d8, 0x1f2, 0x1d9, 0x16e};
    if (!OneOf(type, Cased))
        return false;
    if (type == 0x16e)
    {
        // 0x5108b6 (KalimaSiegeHelpers.h), con los huesos del modelo.
        const Render::Models::Skeleton* skeleton = frame.fauna.SkeletonOf(type);
        if (skeleton == nullptr)
            return Core::Unexpected(std::format("0x5108b6: falta el modelo {:#x}", type));
        if (auto sparks = DrawTheBoneSparks(HelperPassOf(frame), *skeleton); !sparks.HasValue())
            return Core::Unexpected(sparks.Error());
        return true;
    }
    if (frame.pass.sprites == nullptr)
        return Core::Unexpected<std::string>("0x5491e0: falta la tabla de sprites del frame");
    constexpr std::uint16_t Flare = 0x5d5, Shine = 0x5fe, Spark = 0x5c5, Star = 0x5ac;
    switch (type)
    {
    case 0x152:
    case 0x170:
        // 0x5104d0: el 0x152 solo con +0x1d6 en 1.
        if (type == 0x170 || frame.actor.level == 1)
            PairOfSprites(frame, 0x16, 0x17, 1.0f);
        break;
    case 0x186:
        PairOfSprites(frame, 0x1a, 0x1b, 2.0f);
        break;
    case 0x177:
    {
        constexpr std::array<BoneSpriteCall, 2> Calls{{{Flare, 1.0f, 0x3f, 20.0f}, {Shine, 4.0f, 0x3f, 20.0f}}};
        BoneSprites(frame, Calls);
        break;
    }
    case 0x174:
    {
        constexpr std::array<BoneSpriteCall, 6> Calls{{{Flare, 0.5f, 0x1e, -5.0f},
                                                       {Flare, 0.5f, 0x27, -5.0f},
                                                       {Spark, 4.0f, 0x1e, -5.0f},
                                                       {Spark, 4.0f, 0x27, -5.0f},
                                                       {Shine, 2.0f, 0x1e, -5.0f},
                                                       {Shine, 2.0f, 0x27, -5.0f}}};
        BoneSprites(frame, Calls);
        break;
    }
    case 0x176:
    {
        PairOfSprites(frame, 0x2a, 0x2b, 1.0f);
        constexpr std::array<BoneSpriteCall, 8> Calls{{{Spark, 2.0f, 0x1a, 0.0f},
                                                       {Spark, 2.0f, 0x1f, 0.0f},
                                                       {Spark, 2.0f, 0x24, 0.0f},
                                                       {Spark, 2.0f, 0x29, 0.0f},
                                                       {Shine, 1.0f, 0x1a, 0.0f},
                                                       {Shine, 1.0f, 0x1f, 0.0f},
                                                       {Shine, 1.0f, 0x24, 0.0f},
                                                       {Shine, 1.0f, 0x29, 0.0f}}};
        BoneSprites(frame, Calls);
        break;
    }
    case 0x173:
    {
        constexpr std::array<BoneSpriteCall, 2> Calls{{{Spark, 4.0f, 9, 5.0f}, {Shine, 3.0f, 9, 5.0f}}};
        BoneSprites(frame, Calls);
        break;
    }
    case 0x1d8:
        BoneSprite(frame, Star, 1.5f, 0x20, {});
        break;
    case 0x1d9:
        if (auto drawn = DrawTheFairy(frame); !drawn.HasValue())
            return Core::Unexpected(drawn.Error());
        break;
    default: // 0x1f2
        BoneSprite(frame, Star, 2.5f, 6, {});
        break;
    }
    return true;
}

// 0x50db57..0x510ac2 sobre el cuerpo ya dibujado.
Result DrawTheTail(MonsterFrame& frame, const glm::vec3& terrainLight)
{
    ActorState& actor = frame.actor;
    const GameCharacterPass& pass = frame.pass;
    // 0x50db94: la luz de lo que cuelga; el rand() de 0x50db57 y los dos cierres de +0x38e de las manos los saca
    // DrawGameHanging (Q-C860).
    actor.drawnLight = HangingLightOf(actor, terrainLight, frame.map);
    const GameHangingPass hangingPass{.batch = frame.batch,
                                      .models = frame.items,
                                      .body = actor.bodyPose,
                                      .random = frame.random,
                                      .carry = pass.poseCarry,
                                      .sprites = pass.sprites,
                                      .joints = pass.joints,
                                      .particles = pass.particles,
                                      .itemContext = pass.itemContext,
                                      .frameClock = pass.frameClock,
                                      .worldClock = frame.worldClock,
                                      .map = frame.map,
                                      .heroByte1a1 = frame.world.Actors()[frame.world.Runtime().hero].byte1a1,
                                      .siegeActive = frame.world.Runtime().siegeActive,
                                      .logicSteps = pass.clothWorld->logicSteps,
                                      .originalFrame = frame.originalFrame};
    if (auto hung = DrawGameHanging(actor, hangingPass); !hung.HasValue())
        return hung;
    const auto cased = DrawTheModelCase(frame);
    if (!cased.HasValue())
        return Core::Unexpected(cased.Error());
    if (!cased.Value())
    {
        // 0x510a27 y 0x510a36: 0x43c150 y 0x42dfe0 (KalimaSiegeHelpers.h); el primero que tiene el tipo salta el resto.
        const auto helped = DrawKalimaSiegeHelpers(HelperPassOf(frame));
        if (!helped.HasValue())
            return Core::Unexpected(helped.Error());
        if (!helped.Value())
        {
            // 0x510a42..0x510a72: 0x441400, 0x42fa30, 0x434180 y 0x4271d0.
            // Las alturas del pase (sin él, las del mapa del mundo) para las marcas de 0x434180.
            const World::WorldTerrain& terrain = pass.terrain != nullptr ? *pass.terrain : frame.world.Map().terrain;
            MonsterHelperPass helperPass{.actor = actor,
                                         .slot = frame.slot,
                                         .world = frame.world,
                                         .random = frame.random,
                                         .sprites = pass.sprites,
                                         .worldClock = frame.worldClock,
                                         .map = frame.map,
                                         .originalFrame = frame.originalFrame,
                                         .batch = &frame.batch,
                                         .heights = terrain.heights.get()};
            if (auto helpers = DrawTheMonsterHelpers(helperPass); !helpers.HasValue())
                return helpers;
        }
    }
    // 0x510a7a: en la escena 5, con +0x88 en 1, fuera de Blood Castle, Chaos Castle y el 30, 0x510ef0 con +0x369 (y
    // +0x36c en 0 en MuRet, Q-C352).
    if (actor.byte88 == PlayerKind && !InBloodCastle(frame.map) && !InChaosCastle(frame.map) &&
        frame.map != CastleMap && actor.byte369 == 1 && frame.originalFrame)
        return Core::Unexpected<std::string>("0x510f4d: el efecto 0x140 de +0x369 no está portado");
    // 0x510abd: 0x420df0, el cuervo de +0x430, con cualquier tipo.
    if (!actor.darkSpirit)
        return {};
    if (pass.polygon == nullptr)
        return Core::Unexpected<std::string>("0x418320: falta el trapecio del dibujo para el cuervo");
    return DrawGameRaven(actor,
                         GameRavenPass{.batch = frame.batch,
                                       .models = frame.items,
                                       .polygon = *pass.polygon,
                                       .lighting = pass.dynamicLighting,
                                       .terrain = pass.terrain != nullptr ? *pass.terrain : frame.world.Map().terrain,
                                       .fixedHeight = pass.fixedHeight,
                                       .carry = pass.poseCarry,
                                       .frameClock = pass.frameClock,
                                       .map = frame.map,
                                       .heroByte1a1 = frame.world.Actors()[frame.world.Runtime().hero].byte1a1,
                                       .originalFrame = frame.originalFrame});
}
} // namespace

bool WearsClothOnTheMesh(std::uint16_t monster)
{
    constexpr std::array<std::uint16_t, 7> Cloths{0x59, 0x5f, 0x70, 0x76, 0x7c, 0x82, 0x8f};
    return OneOf(monster, Cloths);
}

Core::Expected<void, std::string> CheckMonsterDecorations(std::uint16_t monster)
{
    const auto cut = [monster](std::string_view address, std::string_view what) {
        return Core::Unexpected(
            std::format("{}: {} del +0x376 {:#x} (0x50bf45) no está portado", address, what, monster));
    };
    constexpr std::array<std::uint16_t, 5> Glowing{0x26, 0x2b, 0x34, 0x3b, 0x43};
    if (OneOf(monster, Glowing) || Within(monster, 0x4e, 0x53))
        return cut("0x50c671", "el brillo de 0x60b130");
    if (monster == 0x45)
        return cut("0x50bf98", "los sprites 0x5ac de la tabla 0x6c88a0 y las partículas 0x5c6");
    if (monster == 0x46)
        return cut("0x50c0fb", "el sprite 0x5ac");
    if (monster == 0x47 || monster == 0x4a || monster == 0x12d)
        return cut("0x50c59f", "la tela 0x64 de 0x4548b0");
    if (monster == 0x49 || monster == 0x4b)
        return cut("0x50c36b", "los sprites, los joints 0x614 y la partícula 0x5d9");
    if (monster == 0x4d)
        return cut("0x50c177", "el modelo, la tela y los huesos");
    if (monster == 0x35 || monster == 0x36)
        return cut("0x50c71b", "el brillo 0x48 con la textura 0x5fd");
    if (monster == 0x2a)
        return cut("0x50c762", "las armas 0xef y 0xf0 colgadas con 0x509340");
    if (Within(monster, 0x84, 0x86))
        return cut("0x50c83b", "el arma 0xc1c, 0x225 o 0xa24 colgada con 0x509340");
    if (monster == 0x87)
        return cut("0x50c8ce", "el brillo 0x140");
    return {};
}

glm::vec3 HangingLightOf(const ActorState& actor, const glm::vec3& terrainLight, std::int32_t map)
{
    const float faint = std::bit_cast<float>(0x3dcccccdu);
    if (actor.pk >= RedPk)
        return {1.0f, faint, faint};
    glm::vec3 light{F32(D(terrainLight.x) + D(actor.light.x)), F32(D(terrainLight.y) + D(actor.light.y)),
                    F32(D(terrainLight.z) + D(actor.light.z))};
    const std::int32_t level = map - 9;
    if (level <= 0 || level > 8)
        return light;
    const std::uint16_t monster = actor.word376;
    const bool keeper = Within(monster, 0x56, 0x59) || Within(monster, 0x5c, 0x5f) || Within(monster, 0x73, 0x76) ||
                        Within(monster, 0x79, 0x7c) || Within(monster, 0x7f, 0x82) || Within(monster, 0x8a, 0x8f);
    // 0x50dc2a: el cociente por 3 con IMUL 0x55555556 (con signo; level es positivo).
    const std::int32_t third = level / 3;
    if (!keeper || third == 0)
        return light;
    return {F32(static_cast<double>(third) * 0.5), faint, faint};
}

Core::Expected<void, std::string> DrawGameMonster(Render::ModelBatch& batch, const IModelLibrary& models,
                                                  WorldState& world, std::size_t slot, const GameCharacterPass& pass,
                                                  bool originalFrame)
{
    ActorState& stored = world.Actors()[slot];
    if (pass.clothWorld == nullptr)
        return Core::Unexpected<std::string>("0x50db57: falta el flujo global de rand del dibujo de los personajes");
    const IModelLibrary& fauna = world.FaunaResources();
    const Render::Models::Skeleton* skeleton = fauna.SkeletonOf(stored.type);
    if (skeleton == nullptr)
        return Core::Unexpected(std::format("0x50ba82: falta el modelo {:#x}", stored.type));
    // Entre frames del original, sobre una copia del personaje y del rand(): no guarda nada.
    std::optional<ActorState> projected;
    std::optional<Render::Models::CrtRandom> projectedRandom;
    ActorState& actor = originalFrame ? stored : projected.emplace(stored);
    Render::Models::CrtRandom& random =
        originalFrame ? pass.clothWorld->random : projectedRandom.emplace(pass.clothWorld->random);
    MonsterFrame frame{.batch = batch,
                       .items = models,
                       .fauna = fauna,
                       .world = world,
                       .actor = actor,
                       .pass = pass,
                       .random = random,
                       .slot = slot,
                       .map = world.Runtime().map,
                       .worldClock =
                           static_cast<float>(std::chrono::duration<double, std::milli>(pass.frameClock).count()),
                       .originalFrame = originalFrame};
    const std::uint16_t special = WearsClothOnTheMesh(actor.word376) ? 0 : SpecialOf(actor.word376);
    if (auto body = DrawTheBody(frame, *skeleton, special); !body.HasValue())
        return body;
    if (auto shadow = DrawTheShadow(frame); !shadow.HasValue())
        return shadow;
    if (auto decorated = DrawTheDecorations(frame); !decorated.HasValue())
        return decorated;
    if (auto guild = CheckTheGuild(actor); !guild.HasValue())
        return guild;
    // 0x50cc55: la luz del terreno (0x5f99e0) más la propia, en +0x3b4; el 0x189 en 0,6.
    if (pass.dynamicLighting == nullptr)
        return Core::Unexpected<std::string>("0x50cc55: falta el buffer dinámico de luz del terreno para 0x50ba30");
    const glm::vec3 terrainLight = World::LightAt(*pass.dynamicLighting, actor.position.x, actor.position.y);
    actor.drawnLight = terrainLight + actor.light;
    if (actor.type == 0x189)
        actor.drawnLight = glm::vec3(std::bit_cast<float>(0x3f19999au));
    if (auto parts = CheckTheParts(actor, frame.map); !parts.HasValue())
        return parts;
    if (auto clothed = RunTheCloth(frame); !clothed.HasValue())
        return clothed;
    return DrawTheTail(frame, terrainLight);
}
} // namespace Client::Scenes
