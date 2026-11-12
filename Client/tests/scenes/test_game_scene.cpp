#include "Scenes/GameScene.h"

#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "App/WorldEvent.h"
#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/GateRecord.h"
#include "Data/Tables/GateTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Input/InputState.h"
#include "Network/PacketSerials.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/Skeleton.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/DarkHorse.h"
#include "Scenes/FullSetScan.h"
#include "Scenes/FaunaStep.h"
#include "Scenes/FaunaState.h"
#include "Scenes/GameLeaves.h"
#include "Scenes/GameData.h"
#include "Scenes/GameModels.h"
#include "Scenes/HelperTable.h"
#include "Scenes/MapLoad.h"
#include "Scenes/SceneCameras.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldModels.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/GroundMarks.h"
#include "World/Leaves.h"
#include "World/LeafWeather.h"
#include "World/Particles.h"
#include "World/TerrainCells.h"
#include "World/TerrainCulling.h"
#include "World/TerrainWalls.h"
#include "World/WorldObjects.h"
#include "World/WorldTerrain.h"
#include "UI/ChatLog.h"
#include "UI/MessageArea.h"

#include "support/AppPrinters.h"
#include "support/Bytes.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/PlayerActors.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include "support/ScenePrinters.h"
#include "support/SessionHarness.h"
#include "support/TestGameServer.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::Scenes::GameScene: la escena 5 mientras espera el mundo y con el mundo. Manda el F3 03 en su
// primer paso (0x636290), baja el contador de la espera como MoveHero (0x59af01) y retiene el frame
// mientras pase de 30 (0x6371a0); los mapas de referencia presentan el frame con recursos completos.
// 06 § La entrada al mundo, § El paso del juego y § El dibujo del juego.
// La espera y los cortes deliberados conservan sus oráculos de estado y de la CRT.
namespace
{
using Client::App::SessionState;
using Client::Render::Models::ClothWorld;
using Client::Render::Models::CrtRandom;
using Client::Scenes::GameScene;
using Client::Scenes::SceneContext;
using Client::Scenes::SceneId;
using Client::Scenes::WaitDrawThreshold;
using Client::Scenes::WorldLoadingWait;
using Client::Scenes::WorldRuntime;
using Client::Scenes::WorldState;
using Client::Tests::FakeTextureStore;
using Client::Tests::LoggedInSession;

constexpr std::uint32_t Seed = 0x13572468u;
// El cuerpo del héroe quieto en 0x508f00 (0x4fd5f0): el registro propio que abre y cierra 0x4fc450, con los
// 3.848 rand() de 0x40b4d0, y el rand() % 6 de 0x4fd7e4.
constexpr int HeroBodyDraws = Client::Render::Models::OwnRecordCloseRandoms + 1;
// 0x637128: el viento de las partículas toma dos rand(), incluso con la tabla vacía.
constexpr int ParticleWindDraws = 2;
// 0x636ded..0x63705b, la UI del paso con todo cerrado: el registro propio de la barra de skills (0x5a05e5), las
// consultas del comercio y de la tienda de NPC de Q, W y E (0x5a343e) y las de IsOpen(0) (0x5a5511).
constexpr int GameUiStepDraws = Client::Render::Models::OwnRecordCloseRandoms + 2 + 2;
// 0x636920, en el primer paso de la escena: 0x46b010 y 0x46b0e0, que abre y cierra el registro propio. Sin Slide.bmd
// ningún tramo tiene textos y no sortea.
constexpr int SlideStartDraws = Client::Render::Models::OwnRecordCloseRandoms;
// El primer paso de cada escena: el cartel y, después, la UI del paso.
constexpr int FirstStepDraws = SlideStartDraws + GameUiStepDraws;
// 0x637641: el picking del frame del Dark Wizard (la clase 0) lee su skill del registro propio (0x59efda).
constexpr int FramePickingDraws = Client::Render::Models::OwnRecordCloseRandoms;
// 0x637660: el HUD (0x5a7320) en el estado base: 19 cierres del registro propio (las flechas, los doce avisos de
// durabilidad, la barra de abajo, la experiencia, el AG y los tres de la barra principal), los de las dos celdas del
// héroe y nueve consultas del comercio y de la tienda de NPC (los avisos, las ventanas, el 3D, las grillas y el panel
// del asedio).
constexpr int HudBaseDraws =
    19 * Client::Render::Models::OwnRecordCloseRandoms + 2 * Client::Render::Models::SmallLogicalCloseRandoms + 9;

// El rand() de la CRT sembrado con Seed después de `draws` llamadas.
CrtRandom RandomAfter(int draws)
{
    CrtRandom random(Seed);
    for (int i = 0; i < draws; ++i)
    {
        static_cast<void>(random.Next());
    }
    return random;
}

CrtRandom Advanced(CrtRandom random, int draws)
{
    for (int i = 0; i < draws; ++i)
        static_cast<void>(random.Next());
    return random;
}
constexpr int HeroActionPrefixRandoms = Client::Render::Models::OwnRecordCloseRandoms + 60;

// El rand() después de un paso: `draws` de la escena y, detrás, la tela (0x637700).
CrtRandom RandomAfterAStep(int draws)
{
    ClothWorld reference{.random = RandomAfter(draws)};
    Client::Render::Models::StepTheClothWorld(reference);
    return reference.random;
}

std::uint32_t StateAfterAStep(int draws)
{
    return RandomAfterAStep(draws).State();
}

// 599f41..599ffd: Gate.bmd real, una lectura lógica (4 rand) por comparación
// alcanzada, con cortocircuito X>=x1, Y>=y1, X<=x2, Y<=y2. Al entrar al rectángulo
// se consulta además la espera: estos fixtures conservan el contador positivo.
int GateProbeRandoms(const Client::Data::Tables::GateTable& gates, std::int32_t map,
                     const Client::Scenes::ActorState& hero)
{
    int draws = 0;
    for (std::size_t slot = 0; slot < gates.Count(); ++slot)
    {
        const auto gate = Client::Data::Tables::ReadGateRecord(gates, slot);
        if (!gate || gate->flag != 1 || gate->map != map)
            continue;
        draws += 4;
        if (hero.cellX < gate->x1)
            continue;
        draws += 4;
        if (hero.cellY < gate->y1)
            continue;
        draws += 4;
        if (hero.cellX > gate->x2)
            continue;
        draws += 4;
        if (hero.cellY > gate->y2)
            continue;
        draws += 4; // 59a018: ReadWait; con espera positiva no abre el registro propio.
    }
    return draws;
}

// Oráculo de composición: ejecuta los kernels ya contrastados de 637082/6370ea/6370ef en
// dueños nuevos, con el héroe y clima anteriores al paso. No toma el estado de la CRT observado.
// Las familias 0/2 no necesitan creadores externos; la biblioteca ya cargada sólo presta BMD.
Client::Core::Expected<void, std::string> AdvanceEnvironmentPrefix(Client::Scenes::WorldState& world,
                                                                   const Client::Scenes::ActorState& hero,
                                                                   Client::World::LeafWeather weather,
                                                                   std::int32_t heroCellLayer,
                                                                   Client::Render::Models::ClothWorld& reference)
{
    Client::World::Leaves leaves;
    Client::Scenes::FaunaState fauna;
    const float cameraY = 0.0f; // La escena aún no hizo su primer dibujo de juego.
    const float worldClock = 0.0f;
    const auto& terrain = world.Map().terrain;
    Client::Scenes::GameLeafContext leafContext{leaves,           weather,   *terrain.heights,    *terrain.attributes,
                                                hero.position,    cameraY,   world.Runtime().map, heroCellLayer,
                                                reference.random, worldClock};
    if (auto stepped = Client::Scenes::StepGameLeaves(leafContext); !stepped.HasValue())
        return stepped;
    Client::Scenes::FaunaStepContext faunaContext{fauna,
                                                  world.Runtime().map,
                                                  world.Runtime().storm,
                                                  hero.position,
                                                  hero.animation.action,
                                                  hero.safeZone,
                                                  terrain,
                                                  world.DynamicLighting().Lighting(),
                                                  world.FaunaResources(),
                                                  reference,
                                                  worldClock};
    return Client::Scenes::StepGameFauna(faunaContext);
}

// Con el login, la lista y el lugar 3 elegido: la sesión espera la entrada.
void ChooseSlotThree(LoggedInSession& online)
{
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    online.session.RequestCharacterList();
    online.ReceiveTheList({{0, "char0001"}, {3, "char0002"}});
    online.session.ChooseCharacter(std::uint8_t{3});
    ASSERT_NE(online.session.ChosenCharacter(), nullptr);
}

// Los dobles de sesión/espera no representan un mapa real. Prestan una grilla inerte para
// satisfacer los bindings anteriores al contador sin crear hojas ni fauna de una familia real.
void GiveInertTerrain(WorldState& world)
{
    world.Map().terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    world.Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    world.Map().terrain.mapping = std::make_unique<Client::Data::Terrain::TerrainMapping>();
}
constexpr int InertFishCandidateRandoms = 3 * 2; // 447030: tres slots inactivos, X/Y; mapa−1 rechaza.

// Seis píxeles por byte: los textos de los tests no llegan a los 256 del corte de 0x555190.
class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind /*font*/,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

// El WorldState, el rand() de la CRT y la escena, sin sesión salvo que se pase una.
struct Harness
{
    explicit Harness(Client::Core::Log& log, Client::App::ServerSession* session = nullptr)
        : scene(SceneContext{.dataRoot = Client::Tests::RepoDataDir(),
                             .textures = store,
                             .log = log,
                             .cloth = &cloth,
                             .world = &world,
                             .textMeasure = &letters},
                session)
    {
        GiveInertTerrain(world);
    }

    [[nodiscard]] WorldRuntime& Runtime()
    {
        return world.Runtime();
    }

    FakeTextureStore store;
    LetterMeasure letters;
    ClothWorld cloth{.random = CrtRandom(Seed)};
    WorldState world;
    GameScene scene;
    const Client::Input::InputState input;
};

TEST(GameSceneTest, TheFirstStepSendsF303)
{
    LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(ChooseSlotThree(online));
    Harness tested(online.log, &online.session);
    ASSERT_TRUE(tested.scene.Enter().HasValue());

    tested.scene.Step(tested.input);
    tested.scene.Step(tested.input);

    EXPECT_EQ(online.session.State(), SessionState::EnteringWorld);
    EXPECT_FALSE(std::ranges::any_of(online.sink.lines, [](const std::string& line)
                                     { return line.find("se ignora la entrada al mundo") != std::string::npos; }));
}

// 0x63684b..0x6368f4: el primer paso cierra el campo del chat, vacía el área vieja con 120 entradas, el registro y el
// filtro. Los pasos que siguen no lo repiten.
TEST(GameSceneTest, TheFirstStepEmptiesTheChat)
{
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Harness tested(log);
    WorldRuntime& runtime = tested.Runtime();
    runtime.ui.chatOpen = true;
    runtime.chat.log.entries.push_back(Client::UI::ChatEntry{.name = "Ana", .text = "hola", .type = 0});
    runtime.chat.log.filter[0] = "dragon";
    runtime.chat.area.rows[0].text = "viejo";
    runtime.chat.area.count = 1;
    ASSERT_TRUE(tested.scene.Enter().HasValue());

    tested.scene.Step(tested.input);

    ASSERT_FALSE(tested.scene.Failure().has_value()) << *tested.scene.Failure();
    EXPECT_FALSE(runtime.ui.chatOpen);
    EXPECT_TRUE(runtime.chat.log.entries.empty());
    EXPECT_TRUE(runtime.chat.log.filter[0].empty());
    EXPECT_EQ(runtime.chat.area.rows[0].text, "");
    EXPECT_EQ(runtime.chat.area.count, static_cast<std::int32_t>(Client::UI::MessageAreaRows));

    runtime.chat.log.entries.push_back(Client::UI::ChatEntry{.name = "Beto", .text = "chau", .type = 0});
    tested.scene.Step(tested.input);
    EXPECT_EQ(runtime.chat.log.entries.size(), 1u);
}

// Q-C70: en el juego (el modo 5), el FD_CLOSE no abre un aviso y el juego sigue sin conexión: el
// texto 3 en el área de mensajes es de la CL9. La escena no vuelve al login.
TEST(GameSceneTest, AFailedSessionKeepsTheGame)
{
    LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(ChooseSlotThree(online));
    Harness tested(online.log, &online.session);
    ASSERT_TRUE(tested.scene.Enter().HasValue());
    tested.scene.Step(tested.input);

    online.socket.peerClosed = true;
    static_cast<void>(online.session.Poll());
    tested.scene.Step(tested.input);

    EXPECT_EQ(online.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.scene.Next(), std::nullopt);
    EXPECT_FALSE(tested.scene.WantsToQuit());
}

// El original loguea el lugar y el nombre; MuRet, el lugar (el nombre es del jugador).
TEST(GameSceneTest, TheLogHasTheSlotNotTheName)
{
    LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(ChooseSlotThree(online));
    Harness tested(online.log, &online.session);
    ASSERT_TRUE(tested.scene.Enter().HasValue());

    tested.scene.Step(tested.input);

    EXPECT_TRUE(std::ranges::any_of(online.sink.lines, [](const std::string& line)
                                    { return line.find("del lugar 3") != std::string::npos; }));
    EXPECT_FALSE(std::ranges::any_of(online.sink.lines, [](const std::string& line)
                                     { return line.find("char000") != std::string::npos; }));
}

// La espera vive en el WorldState y sus cierres gastan el rand() de la tela: sin alguno de los dos,
// el juego no entra en vez de dibujar sin esperar.
TEST(GameSceneTest, DoesNotEnterWithoutTheWorldOrTheRandom)
{
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Harness tested(log);
    GameScene withoutWorld(
        SceneContext{
            .dataRoot = Client::Tests::RepoDataDir(), .textures = tested.store, .log = log, .cloth = &tested.cloth},
        nullptr);
    GameScene withoutRandom(
        SceneContext{
            .dataRoot = Client::Tests::RepoDataDir(), .textures = tested.store, .log = log, .world = &tested.world},
        nullptr);

    EXPECT_FALSE(withoutWorld.Enter().HasValue());
    EXPECT_FALSE(withoutRandom.Enter().HasValue());
}

// 06, "La entrada al mundo", pasos 3, 4 y 6: con el mundo cargado y el contador que dejó la carga, el
// dibujo retiene; los pasos lo bajan sin llegar, y la respuesta del F3 03 lo pone en 30 (0x4c48eb),
// que ya no retiene.
TEST(GameSceneTest, HoldsTheFrameUntilTheWorldArrives)
{
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Harness tested(log);
    tested.Runtime().drawingEnabled = true;
    tested.Runtime().warpCounter = WorldLoadingWait;
    ASSERT_TRUE(tested.scene.Enter().HasValue());
    EXPECT_TRUE(tested.scene.HoldsTheLastFrame()) << "antes del primer PrepareFrame retiene";

    for (int step = 0; step < 3; ++step)
    {
        tested.scene.Step(tested.input);
        tested.scene.PrepareFrame();
        EXPECT_TRUE(tested.scene.HoldsTheLastFrame());
    }
    EXPECT_GT(tested.Runtime().warpCounter, WaitDrawThreshold);

    tested.Runtime().warpCounter = WaitDrawThreshold;
    tested.scene.PrepareFrame();

    EXPECT_FALSE(tested.scene.HoldsTheLastFrame());
}

// 0x6374dd: con el mundo cargado y el contador en 30 o menos, el dibujo del juego llega a los personajes
// (0x510d00), y 0x50ba30 corre 0x505820 con cada jugador que ve (0x50c91e). MuRet no los dibuja: no sabe
// qué quedó en [0x743a178] y [0x743a17c]. Retenido, o sin el mundo cargado, no llega y quedan.
TEST(GameSceneTest, AFrameThatReachesTheCharactersForgetsTheLastScan)
{
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Harness tested(log);
    tested.Runtime().fullSetScan = Client::Scenes::Scan505820{.dword743a178 = 12, .byte743a17c = 1};
    ASSERT_TRUE(tested.scene.Enter().HasValue());

    tested.scene.PrepareFrame();
    EXPECT_TRUE(tested.Runtime().fullSetScan.has_value());

    tested.Runtime().drawingEnabled = true;
    tested.Runtime().warpCounter = WaitDrawThreshold + 1;
    tested.scene.PrepareFrame();
    ASSERT_TRUE(tested.scene.HoldsTheLastFrame());
    EXPECT_TRUE(tested.Runtime().fullSetScan.has_value());

    tested.Runtime().warpCounter = WaitDrawThreshold;
    tested.scene.PrepareFrame();
    ASSERT_FALSE(tested.scene.HoldsTheLastFrame());
    EXPECT_FALSE(tested.Runtime().fullSetScan.has_value());
}

// 0x6371b7..0x637229: el dibujo lee el contador con un acceso que cierra (0x40b800) aunque después
// retenga; el cierre gasta 4 rand() solo si deja el acceso en 0. 0x63722e compara con signo: 31
// retiene y 30 no. Preguntar después no vuelve a leer.
TEST(GameSceneTest, WarpGateReadsAndClosesOnceEvenWhenItRetainsTheFrame)
{
    struct Case
    {
        std::int32_t counter;
        std::uint8_t depth;
        bool holds;
        int draws;
    };
    for (const Case& probe : {Case{31, 0, true, 4}, Case{31, 1, true, 0}, Case{31, 255, true, 0}, Case{30, 0, false, 4},
                              Case{30, 1, false, 0}, Case{30, 255, false, 0}, Case{-1, 0, false, 4}})
    {
        SCOPED_TRACE(::testing::Message() << probe.counter << " con " << static_cast<int>(probe.depth));
        Client::Tests::RecordingSink sink;
        Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
        Harness tested(log);
        tested.Runtime().drawingEnabled = true;
        tested.Runtime().warpCounter = probe.counter;
        tested.Runtime().warpDepth = probe.depth;
        ASSERT_TRUE(tested.scene.Enter().HasValue());

        tested.scene.PrepareFrame();
        const bool holds = tested.scene.HoldsTheLastFrame();
        const bool again = tested.scene.HoldsTheLastFrame();

        EXPECT_EQ(holds, probe.holds);
        EXPECT_EQ(again, probe.holds);
        EXPECT_EQ(tested.Runtime().warpCounter, probe.counter);
        EXPECT_EQ(tested.Runtime().warpDepth, probe.depth);
        EXPECT_EQ(tested.cloth.random.State(), RandomAfter(probe.draws).State());
    }
}

// 0x59ae8d..0x59afd1: MoveHero baja el contador si es positivo en un acceso y lo vuelve a leer en
// otro; con los dos cerrando en 0 son 8 rand(), antes de los de la tela (0x637700). Los gates de
// anteriores a ese contador se cubren en GameMovement. La grilla inerte permite completar
// personajes, viento y tela sin confundir un doble de espera con un mapa real.
TEST(GameSceneTest, AStepLowersTheCounterWithTwoAccesses)
{
    struct Case
    {
        std::int32_t counter;
        std::uint8_t depth;
        std::int32_t after;
        int draws;
    };
    for (const Case& probe : {Case{WorldLoadingWait, 0, WorldLoadingWait - 1, 8}, Case{31, 1, 30, 0},
                              Case{1, 255, 0, 0}, Case{0, 0, 0, 8}, Case{-5, 0, -5, 8}})
    {
        SCOPED_TRACE(::testing::Message() << probe.counter << " con " << static_cast<int>(probe.depth));
        Client::Tests::RecordingSink sink;
        Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
        Harness tested(log);
        tested.Runtime().drawingEnabled = true;
        tested.Runtime().movement.inputBlocked = true;
        tested.Runtime().warpCounter = probe.counter;
        tested.Runtime().warpDepth = probe.depth;
        ASSERT_TRUE(tested.scene.Enter().HasValue());

        tested.scene.Step(tested.input);

        EXPECT_EQ(tested.Runtime().warpCounter, probe.after);
        EXPECT_EQ(tested.Runtime().warpDepth, probe.depth);
        ASSERT_EQ(tested.scene.Failure(), std::nullopt);
        const int wind = ParticleWindDraws; // El caller sigue con partículas también durante la espera.
        EXPECT_EQ(tested.cloth.random.State(),
                  StateAfterAStep(FirstStepDraws + InertFishCandidateRandoms + probe.draws + wind));
    }
}

// Solo el esqueleto del caballo oscuro (0xbf), con una acción de una key en un hueso.
class HorseSkeletonOnly final : public Client::Scenes::IModelLibrary
{
public:
    HorseSkeletonOnly()
    {
        Client::Render::Models::SkeletonBone bone;
        bone.parent = -1;
        bone.actions = {{Client::Render::Models::BoneKey{}}};
        m_skeleton.bones = {bone};
        m_skeleton.actions = {
            Client::Render::Models::ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 1, .lockPositions = false}};
    }

    [[nodiscard]] const Client::Data::Models::Model* Find(std::uint16_t /*model*/) const override
    {
        return nullptr;
    }
    [[nodiscard]] const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        if (model == Client::Data::Models::PlayerSkeletonModel)
        {
            return &m_player;
        }
        return model == Client::Scenes::DarkHorseModel ? &m_skeleton : nullptr;
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t /*model*/) const override
    {
        return {};
    }

private:
    Client::Render::Models::Skeleton m_skeleton;
    // El jinete vivo pasa por el paso de los personajes (0x4f9580), que pide Player.bmd.
    Client::Render::Models::Skeleton m_player = Client::Tests::PlayerActionsSkeleton();
};

// 0x6370f4: 0x445230, los ayudantes, antes de MoveHero (0x63710a) y también en la espera. Con el jinete
// muerto, el paso de la escena 5 apaga su caballo y la espera sigue, con los 8 rand() de MoveHero. Con el
// jinete vivo fuera de una celda segura, el aliento saca un rand() después de los seis candidatos
// de peces del mapa inerte (con Seed, tres rand() antes, el cartel y la UI del paso, % 3 da 0) y, sin ningún dibujo
// antes, sale del hueso 27 en cero del buffer global: la 5f2 nace, MoveHero baja el contador y la espera sigue, con el
// cuerpo del jinete avanzado por Player.bmd (0x4f9580).
TEST(GameSceneTest, TheHorseStepsBeforeMoveHeroAlsoInTheWait)
{
    constexpr std::size_t RiderSlot = 5;
    constexpr int Skipped = 3;
    CrtRandom first = RandomAfter(Skipped + FirstStepDraws + InertFishCandidateRandoms);
    ASSERT_EQ(first.Next() % 3, 0);
    for (const bool riderAlive : {false, true})
    {
        SCOPED_TRACE(riderAlive ? "con el jinete vivo" : "con el jinete muerto");
        Client::Tests::RecordingSink sink;
        Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
        FakeTextureStore store;
        ClothWorld cloth{.random = RandomAfter(Skipped)};
        WorldState world;
        GiveInertTerrain(world);
        const HorseSkeletonOnly models;
        GameScene scene(SceneContext{.dataRoot = Client::Tests::RepoDataDir(),
                                     .textures = store,
                                     .log = log,
                                     .cloth = &cloth,
                                     .world = &world},
                        nullptr, &models);
        world.Runtime().drawingEnabled = true;
        world.Runtime().warpCounter = WorldLoadingWait;
        world.Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        Client::Scenes::ActorState& rider = world.Actors()[RiderSlot];
        rider = Client::Tests::UnarmedPlayer(1);
        rider.alive = true;
        rider.byte88 = 1;
        rider.position = {10050.0f, 10050.0f, 170.0f};
        const auto created =
            Client::Scenes::CreateHelper(world, Client::Scenes::DarkHorseModel, rider.position, RiderSlot);
        ASSERT_TRUE(created.HasValue()) << created.Error();
        ASSERT_EQ(created.Value(), std::optional<std::size_t>{0});
        rider.alive = riderAlive;
        ASSERT_TRUE(scene.Enter().HasValue());

        scene.Step(Client::Input::InputState{});

        if (riderAlive)
        {
            EXPECT_EQ(scene.Failure(), std::nullopt) << scene.Failure().value_or("");
            EXPECT_TRUE(world.Helpers()[0].alive);
            EXPECT_TRUE(world.Particles().All()[0].alive);
            EXPECT_EQ(world.Particles().All()[0].type, Client::World::BreathParticle);
            EXPECT_EQ(world.Runtime().warpCounter, WorldLoadingWait - 1);
        }
        else
        {
            EXPECT_EQ(scene.Failure(), std::nullopt);
            EXPECT_FALSE(world.Helpers()[0].alive);
            EXPECT_EQ(world.Runtime().warpCounter, WorldLoadingWait - 1);
            EXPECT_EQ(cloth.random.State(),
                      StateAfterAStep(Skipped + FirstStepDraws + InertFishCandidateRandoms + 8 + ParticleWindDraws));
        }
    }
}

// 0x636da3 y 0x6371a6: sin el mundo cargado y sin el estado 0x3d, el paso sale antes de MoveHero y el
// dibujo devuelve 0 sin leer el contador. El cartel del primer paso (0x636920) y la tela corren igual.
TEST(GameSceneTest, WithoutTheLoadedWorldNeitherTheStepNorTheDrawReadTheCounter)
{
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Harness tested(log);
    tested.Runtime().warpCounter = 30;
    ASSERT_TRUE(tested.scene.Enter().HasValue());

    tested.scene.Step(tested.input);
    tested.scene.PrepareFrame();

    EXPECT_TRUE(tested.scene.HoldsTheLastFrame());
    EXPECT_FALSE(tested.Runtime().drawingEnabled);
    EXPECT_EQ(tested.Runtime().warpCounter, 30);
    EXPECT_EQ(tested.cloth.random.State(), StateAfterAStep(SlideStartDraws));
}

// 0x636da3..0x636dac: con la sesión en el mundo (el 0x3d que publica la respuesta del F3 03), el paso
// marca el mundo cargado aunque el F1 02 lo hubiera bajado, y sigue hasta MoveHero.
TEST(GameSceneTest, TheWorldStateTurnsTheDrawingOn)
{
    LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(ChooseSlotThree(online));
    Harness tested(online.log, &online.session);
    ASSERT_TRUE(tested.scene.Enter().HasValue());
    tested.scene.Step(tested.input);
    ASSERT_EQ(online.session.State(), SessionState::EnteringWorld);
    online.eventSink.onWorld = [](const Client::App::WorldEvent&,
                                  Client::App::ServerSession& session) -> Client::Core::Expected<void, std::string>
    {
        session.PublishWorldEntry();
        return {};
    };
    // Una respuesta sintética de 0x34 bytes: la escena no mira sus campos.
    Client::Network::PacketSerials serials;
    online.socket.reads = {Client::Tests::FromTheGameServer(
        Client::Tests::Bytes({0xC1, 0x34, 0xF3, 0x03, 0x10, 0x20, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
                              0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12,
                              0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
                              0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x00, 0x00, 0x26, 0x27, 0x28, 0x29, 0x2A}),
        serials)};
    ASSERT_TRUE(online.session.Poll().HasValue());
    ASSERT_EQ(online.session.State(), SessionState::InWorld);
    tested.Runtime().drawingEnabled = false;
    tested.Runtime().movement.inputBlocked = true;
    tested.Runtime().warpCounter = 31;

    tested.scene.Step(tested.input);
    tested.scene.PrepareFrame();

    EXPECT_TRUE(tested.Runtime().drawingEnabled);
    EXPECT_EQ(tested.Runtime().warpCounter, 30);
    EXPECT_FALSE(tested.scene.HoldsTheLastFrame());
    ASSERT_EQ(tested.scene.Failure(), std::nullopt);
}

// 0x636dc2..0x636ddb: con el mundo cargado, cada paso deja el temblor de la cámara en su 0,2 (0x6b8854),
// en float; sin el mundo cargado el paso sale antes y no lo toca.
TEST(GameSceneTest, AStepWithTheWorldLeavesAFifthOfTheShake)
{
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    FakeTextureStore store;
    ClothWorld cloth{.random = CrtRandom(Seed)};
    WorldState world;
    GiveInertTerrain(world);
    Client::World::GameCameraInput cameraInput;
    GameScene scene(SceneContext{.dataRoot = Client::Tests::RepoDataDir(),
                                 .textures = store,
                                 .log = log,
                                 .cloth = &cloth,
                                 .cameraInput = &cameraInput,
                                 .world = &world},
                    nullptr);
    ASSERT_TRUE(scene.Enter().HasValue());
    cameraInput.shake = 3.0f;
    scene.Step(Client::Input::InputState{});
    EXPECT_EQ(cameraInput.shake, 3.0f) << "sin el mundo cargado, 0x636dbc sale antes";

    world.Runtime().drawingEnabled = true;
    world.Runtime().warpCounter = WorldLoadingWait;
    const auto decay = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));
    scene.Step(Client::Input::InputState{});
    const auto once = static_cast<float>(3.0 * decay);
    EXPECT_EQ(cameraInput.shake, once);
    scene.Step(Client::Input::InputState{});
    EXPECT_EQ(cameraInput.shake, static_cast<float>(static_cast<double>(once) * decay));
}

// Con el frame retenido el dibujo no dibuja ni corta: solo queda el cierre de 0x637229.
TEST(GameSceneTest, AHeldFrameDrawsNothing)
{
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Harness tested(log);
    tested.Runtime().drawingEnabled = true;
    tested.Runtime().warpCounter = WaitDrawThreshold + 1;
    ASSERT_TRUE(tested.scene.Enter().HasValue());
    Client::Tests::RecordingFrameTarget target;

    tested.scene.PrepareFrame();
    tested.scene.Render(target.BeginFrame(Client::Core::Duration{}), 0.0f);
    target.Present();

    ASSERT_EQ(target.frames.size(), 1u);
    EXPECT_TRUE(target.frames[0].events.empty());
    EXPECT_EQ(tested.scene.Failure(), std::nullopt);
    EXPECT_FALSE(tested.scene.MouseRay().has_value());
    EXPECT_EQ(tested.cloth.random.State(), RandomAfter(4).State());
}

// El dibujo del juego con un mapa cargado de la Data del repo (0x61a560, MapLoad) y el héroe publicado
// en su celda, sin pasar por el F3 03.
class GameSceneDrawTest : public Client::Tests::GoldenDataTest
{
protected:
    void SetUp() override
    {
        GoldenDataTest::SetUp();
        auto loadedData = Client::Scenes::LoadGameData(m_dataRoot);
        ASSERT_TRUE(loadedData.HasValue()) << loadedData.Error();
        data = std::move(loadedData).Value();
        const auto loadedModels = playerModels.Load(m_dataRoot, store, log);
        ASSERT_TRUE(loadedModels.HasValue()) << loadedModels.Error();
    }
    // El mapa `map` cargado en el juego y el héroe en el lugar HeroSlot, en la celda (x, y), publicado,
    // con la cámara de arranque y el rand() en la semilla.
    void Prepare(std::uint8_t map, std::uint8_t x, std::uint8_t y)
    {
        world.Runtime().map = map;
        const auto loaded = Client::Scenes::LoadMap(map, Client::Scenes::MapLoadContext{.dataRoot = m_dataRoot,
                                                                                        .world = world,
                                                                                        .scene = SceneId::Game,
                                                                                        .playerKey = 5,
                                                                                        .random = cloth.random});
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        Client::Scenes::ActorState& hero = world.Actors()[HeroSlot];
        hero.alive = true;
        hero.cellX = x;
        hero.cellY = y;
        hero.position = {(static_cast<float>(x) * 100.0f) + 50.0f, (static_cast<float>(y) * 100.0f) + 50.0f, 165.0f};
        world.Runtime().heroSlot = static_cast<std::int32_t>(HeroSlot);
        world.Runtime().hero = HeroSlot;
        world.Runtime().drawingEnabled = true;
        world.Runtime().warpCounter = WaitDrawThreshold;
        camera = Client::World::GameCamera{};
        cameraInput = Client::World::GameCameraInput{};
        cloth.random = CrtRandom(Seed);
    }

    // El héroe como lo deja 0x511290: un jugador de pie (la acción 1) con el PK 3 y la zona segura en 1, en la
    // celda dibujada de Prepare y con la celda lógica en (x, y), la que va una adelante (06 § Caminar). El
    // registro propio, con el equipo vacío que deja la respuesta del F3 03 (0x4c47c8).
    void MakeTheHeroAPlayer(std::uint8_t x, std::uint8_t y)
    {
        world.OwnCharacter().ClearEquipmentForEntry();
        Client::Scenes::ActorState& hero = world.Actors()[HeroSlot];
        const glm::vec3 position = hero.position;
        hero = Client::Tests::UnarmedPlayer(1);
        hero.word376 = 0xffff;
        hero.alive = true;
        hero.alpha = hero.alphaTarget = 1.0f;
        hero.head = Client::Scenes::NoItemModel;
        for (auto& part : hero.body)
            part.model = Client::Scenes::NoItemModel;
        hero.byte88 = 1;
        hero.pk = 3;
        hero.animation.action = 1;
        hero.position = position;
        hero.cellX = x;
        hero.cellY = y;
        hero.targetX = x;
        hero.targetY = y;
        hero.safeZone = 1;
    }

    // El word del TerrainWall (0x7d0d348) en la celda (x, y) del mapa cargado.
    [[nodiscard]] std::uint16_t Cell(std::uint8_t x, std::uint8_t y)
    {
        return world.Map().terrain.attributes->cells[Client::Data::Terrain::CellIndex(x, y)];
    }

    // El trapecio que deja el dibujo de la selección (0x63475c): el de su punto fijo, con su cámara.
    [[nodiscard]] static Client::World::TerrainPolygon SelectionPolygon()
    {
        return Client::World::TerrainPolygonOf(Client::World::CullingScene::CharacterSelect, 54,
                                               Client::Scenes::SelectionCullingPoint,
                                               Client::Scenes::SelectionCamera().angles.z);
    }

    [[nodiscard]] GameScene Scene()
    {
        return GameScene(SceneContext{.dataRoot = m_dataRoot,
                                      .textures = store,
                                      .log = log,
                                      .cloth = &cloth,
                                      .camera = &camera,
                                      .cameraInput = &cameraInput,
                                      .world = &world},
                         nullptr, &playerModels, &data.items->table, &playerModels.TextureSlots(), &*data.gates,
                         &*data.skills);
    }

    static constexpr std::size_t HeroSlot = 42;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    FakeTextureStore store;
    ClothWorld cloth{.random = CrtRandom(Seed)};
    WorldState world;
    Client::World::GameCamera camera;
    Client::World::GameCameraInput cameraInput;
    Client::Scenes::GameModels playerModels;
    Client::Scenes::GameData data;
};

// El primer lote de modelos contiene terreno, pasto y objetos; la cola puede agregar otros lotes.
const Client::Tests::FrameEvent* ModelsOf(const Client::Tests::RecordedFrame& frame)
{
    const Client::Tests::FrameEvent* models = nullptr;
    for (const Client::Tests::FrameEvent& event : frame.events)
    {
        if (event.kind == Client::Tests::FrameEvent::Kind::Models)
        {
            if (models == nullptr)
                models = &event;
        }
    }
    return models;
}

// La z más baja y la más alta de los quads del pasto en `slot`.
struct GrassHeights
{
    float lowest = std::numeric_limits<float>::max();
    float highest = std::numeric_limits<float>::lowest();
};

GrassHeights GrassHeightsOf(const Client::Tests::FrameEvent& models, std::uint16_t slot)
{
    GrassHeights heights;
    for (const Client::Render::ModelDrawCommand& command : models.modelCommands)
    {
        if (command.state.texture != Client::Render::TextureSlot{slot})
        {
            continue;
        }
        for (std::size_t v = 0; v < command.vertexCount; ++v)
        {
            const float z = models.modelVertices[command.firstVertex + v].position.z;
            heights.lowest = std::min(heights.lowest, z);
            heights.highest = std::max(heights.highest, z);
        }
    }
    return heights;
}

// Los objetos que vio el dibujo (+0x16c), en el orden de 0x6046a0 (World::DrawOrder).
std::vector<std::size_t> SeenObjects(const WorldState& world)
{
    std::vector<std::size_t> seen;
    for (const std::size_t index : Client::World::DrawOrder(world.Map().objects))
    {
        if (world.Map().objects[index].seen)
        {
            seen.push_back(index);
        }
    }
    return seen;
}

// Lo que ve el dibujo de Lorencia con el héroe en (143, 120), la celda de la traza: 83 objetos de los sectores
// (7, 7), (8, 7), (8, 8), (9, 6), (9, 7) y (9, 8), con el 0x69 (Waterspout01, 1720), que con la semilla saca
// un rand() % 2 impar y no tira gotas (*simulado*, con el trapecio de MuRet).
constexpr std::array<std::size_t, 83> LorenciaSeen{
    1366, 1372, 1373, 1374, 1378, 1400, 1432, 1686, 1687, 1688, 1689, 1690, 1691, 1693, 1694, 1695, 1696,
    1697, 1698, 1699, 1700, 1701, 1702, 1703, 1704, 1705, 1706, 1707, 1709, 1710, 1711, 1712, 1713, 1714,
    1715, 1717, 1718, 1719, 1720, 1721, 1740, 1742, 1745, 1748, 1749, 1750, 1751, 1754, 1896, 1899, 1900,
    1901, 1902, 1909, 1910, 1911, 1912, 1913, 1920, 1921, 1922, 1923, 1924, 1926, 1927, 1928, 1929, 1930,
    1931, 1933, 1934, 1935, 1936, 1937, 1940, 1941, 1943, 1950, 1963, 1978, 1986, 1987, 1988};
// Y el de Devias con el héroe en (220, 30): 54 objetos de los sectores (12, 1) a (14, 2), ninguno con un
// caso de 0x602ed0 (*simulado*).
constexpr std::array<std::size_t, 54> DeviasSeen{
    1262, 1263, 1264, 1265, 1273, 1276, 1277, 1278, 1282, 1457, 1462, 1463, 1464, 1465, 1466, 1468, 1469, 1470,
    1471, 1472, 1473, 1478, 1480, 1481, 1482, 1483, 1485, 1486, 1713, 1715, 1716, 1717, 1722, 1723, 1724, 1729,
    1733, 1738, 1746, 1747, 1752, 1753, 1754, 1756, 1757, 1759, 1770, 1774, 1777, 1778, 1779, 1788, 1789, 1790};

// Si `slot` es una textura del terreno del juego: los tiles de 0x23 a 0x30 y el 0x122.
bool IsTerrainTile(std::uint16_t slot)
{
    return (slot >= Client::World::FirstTileSlot && slot < Client::World::FirstTileSlot + 14) || slot == 0x122;
}

// 0x63723a y la cola del dibujo en Lorencia (el 0) y Devias (el 2), los mapas de referencia: la cámara con los dos
// accesos a la celda del héroe (8 rand() después de los 4 de 0x637229), el rayo del mouse, el primer
// pase del terreno (las celdas, con los tiles de 0x23 a 0x30 y el 0x122), el del pasto (0x5fc353, sin
// rand()) con la única textura del pasto que tiene cada mundo, las marcas del suelo (0x5fc365), que con la
// tabla de la .bss no dibujan ninguna ni sacan rand(), y los objetos (0x6374ce, 0x6046a0): los que ve el
// trapecio, sector por sector, cada uno con su rand() de 0x602ef4 y con las texturas de su modelo
// (Object1\ y Object3\, desde 0x41c), después del terreno; el 0x69 de Lorencia, con sus cuatro mallas y el
// rand() % 2 de 0x603011. Después, sin efectos con modelo (la carga del mapa los apaga), ni pájaros, ni el
// héroe visto (0x511290 lo deja sin ver), los efectos del suelo y los personajes no dibujan ni sacan
// rand(), y 0x42da60 abre las puertas del TerrainWall (Q-C276). Con las tablas de materiales
// reales, los pases restantes vacíos completan el frame y lo presentan.
TEST_F(GameSceneDrawTest, TheReferenceMapsDrawTheTerrainTheGrassTheGroundMarksAndTheObjects)
{
    struct Case
    {
        std::uint8_t map;
        std::uint8_t x;
        std::uint8_t y;
        // World1 solo tiene TileGrass01.OZT y World3 solo TileGrass02.OZT: las otras dos se saltean.
        std::uint16_t grass;
        std::span<const std::size_t> seen;
        // Los rand() de 0x602ef4: uno por objeto visto que llega a 0x602ed0; y el rand() % 2 del 0x69.
        int objectDraws;
        // Las celdas (x, 115) de la puerta (119, 114) y, en Devias, las (x, 205) de la (93, 204), con el 0x04
        // antes del dibujo.
        std::span<const std::uint8_t> gateRows;
    };
    constexpr std::array<std::uint8_t, 1> LorenciaGates{115};
    constexpr std::array<std::uint8_t, 2> DeviasGates{115, 205};
    for (const Case& probe : {Case{0, 143, 120, 0x32, LorenciaSeen, 83 + 1, LorenciaGates},
                              Case{2, 220, 30, 0x33, DeviasSeen, 54, DeviasGates}})
    {
        SCOPED_TRACE(static_cast<int>(probe.map));
        ASSERT_NO_FATAL_FAILURE(Prepare(probe.map, probe.x, probe.y));
        GameScene scene = Scene();
        ASSERT_TRUE(scene.Enter().HasValue());
        Client::Tests::RecordingFrameTarget target;
        for (const std::uint8_t row : probe.gateRows)
        {
            const std::uint8_t gate = row == 115 ? 119 : 93;
            ASSERT_EQ(Cell(gate, row) & 0x04, 0x04) << static_cast<int>(row);
        }
        ASSERT_FALSE(world.Actors()[HeroSlot].seen);

        scene.PrepareFrame();
        ASSERT_FALSE(scene.HoldsTheLastFrame());
        scene.Render(target.BeginFrame(std::chrono::milliseconds{1000}), 0.0f);

        ASSERT_FALSE(scene.Failure().has_value()) << *scene.Failure();
        ASSERT_EQ(target.frames.size(), 1u);
        const Client::Tests::FrameEvent* models = ModelsOf(target.frames[0]);
        ASSERT_NE(models, nullptr);
        EXPECT_FALSE(models->modelVertices.empty());
        // Primero las celdas del terreno, después los quads del pasto y al final los objetos, todos con su
        // textura y la mezcla.
        std::size_t grassVertices = 0;
        std::size_t objectVertices = 0;
        for (const Client::Render::ModelDrawCommand& command : models->modelCommands)
        {
            ASSERT_TRUE(command.state.texture.has_value());
            const std::uint16_t slot = command.state.texture->value;
            if (IsTerrainTile(slot))
            {
                EXPECT_EQ(objectVertices, 0u) << "los objetos van después del terreno: " << slot;
                EXPECT_EQ(grassVertices, 0u) << "el pasto va después de todo el terreno";
            }
            else if (slot == probe.grass)
            {
                EXPECT_EQ(objectVertices, 0u) << "los objetos van después del pasto";
                EXPECT_EQ(command.state.blend, Client::Render::ModelBlend::Alpha);
                grassVertices += command.vertexCount;
            }
            else
            {
                // La tabla real reutiliza texturas globales anteriores a 0x41c por nombre.
                objectVertices += command.vertexCount;
            }
        }
        EXPECT_GT(grassVertices, 0u);
        EXPECT_EQ(grassVertices % 6, 0u);
        EXPECT_GT(objectVertices, 0u);
        EXPECT_EQ(objectVertices % 3, 0u);
        const std::vector<std::size_t> seen = SeenObjects(world);
        EXPECT_TRUE(std::ranges::equal(seen, probe.seen));
        EXPECT_TRUE(scene.MouseRay().has_value());
        EXPECT_EQ(cameraInput.hero, world.Actors()[HeroSlot].position);
        EXPECT_EQ(cloth.random.State(),
                  RandomAfter(4 + 8 + probe.objectDraws + FramePickingDraws + HudBaseDraws).State());
        for (const std::uint8_t row : probe.gateRows)
        {
            const std::uint8_t gate = row == 115 ? 119 : 93;
            for (int dx = -1; dx <= 2; ++dx)
            {
                EXPECT_EQ(Cell(static_cast<std::uint8_t>(gate + dx), row) & 0x04, 0) << dx << ", " << int{row};
            }
        }
        EXPECT_NE(std::ranges::find(store.loaded, Client::World::FirstTileSlot), store.loaded.end());
        EXPECT_NE(std::ranges::find(store.loaded, std::uint16_t{0x122}), store.loaded.end());
        EXPECT_NE(std::ranges::find(store.loaded, Client::Scenes::WorldModelsFirstSlot.value), store.loaded.end());
        for (std::uint16_t slot = 0x32; slot <= 0x34; ++slot)
        {
            EXPECT_EQ(store.Find(Client::Render::TextureSlot{slot}).has_value(), slot == probe.grass) << slot;
        }
        if (probe.map == 0)
        {
            // La traza de Lorencia, con este héroe y esta cámara: 25 quads de TileGrass01, de z 165 a 293
            // (06 § El pasto, *verificado*).
            EXPECT_EQ(grassVertices / 6, 25u);
            const auto [lowest, highest] = GrassHeightsOf(*models, probe.grass);
            EXPECT_EQ(lowest, 165.0f);
            EXPECT_EQ(highest, 293.0f);
        }

        target.Present();
        EXPECT_TRUE(target.frames.back().presented);
        scene.Leave();
        EXPECT_FALSE(store.Find(Client::Render::TextureSlot{probe.grass}).has_value());
        EXPECT_FALSE(store.Find(Client::Render::TextureSlot{Client::World::FirstTileSlot}).has_value());
    }
}

// 63706a: el paso posterior al dibujo actualiza los techos y el blendMesh de los objetos vistos.
// El dibujo usa recursos completos; el paso aisla estos objetos para no mezclar sus tres rand()
// con creadores de fuego o puertas. Continúa hasta personajes, viento y tela sin cortar.
TEST_F(GameSceneDrawTest, TheStepAfterADrawStepsTheSeenObjects)
{
    struct Case
    {
        std::uint8_t map;
        std::uint8_t x;
        std::uint8_t y;
        std::int32_t layer;
        int drawDraws;
        std::vector<std::size_t> roofs;
        std::vector<std::size_t> flickers;
        int stepDraws;
    };
    for (const Case& probe :
         {Case{0, 143, 120, 4, 4 + 8 + 83 + 1 + HudBaseDraws, {1400}, {1366, 1372, 1690}, 3},
          Case{2, 220, 30, 3, 4 + 8 + 54 + HudBaseDraws, {1282, 1471, 1472, 1473}, {1262, 1462, 1464}, 3}})
    {
        SCOPED_TRACE(static_cast<int>(probe.map));
        ASSERT_NO_FATAL_FAILURE(Prepare(probe.map, probe.x, probe.y));
        MakeTheHeroAPlayer(probe.x, probe.y);
        {
            GameScene draw = Scene();
            ASSERT_TRUE(draw.Enter().HasValue());
            Client::Tests::RecordingFrameTarget target;
            draw.PrepareFrame();
            draw.Render(target.BeginFrame(std::chrono::milliseconds{1000}), 0.0f);
            ASSERT_FALSE(draw.Failure().has_value()) << *draw.Failure();
            draw.Leave();
        }
        ASSERT_EQ(cloth.random.State(), RandomAfter(probe.drawDraws).State());
        world.Runtime().heroCellLayer = probe.layer;
        std::vector<Client::World::WorldObject>& objects = world.Map().objects;
        for (const std::size_t roof : probe.roofs)
        {
            ASSERT_TRUE(objects[roof].seen) << roof;
            ASSERT_EQ(objects[roof].targetAlpha, 1.0f) << roof;
        }
        // Lo que saca cada uno: los rand() del paso, en el orden de los objetos.
        CrtRandom flicker = RandomAfter(probe.drawDraws + FirstStepDraws);
        std::vector<float> lights;
        for (std::size_t k = 0; k < probe.flickers.size(); ++k)
        {
            lights.push_back(
                static_cast<float>(static_cast<double>((flicker.Next() & 3) + 4) * static_cast<double>(0.1f)));
        }
        // Aislamiento contractual del pase de techos y blendMesh: el dibujo anterior comprobó
        // el conjunto visto completo; este paso sólo vuelve a publicar los objetos bajo prueba.
        // Los creadores de fuego y puertas tienen sus pruebas de servicios propias.
        for (auto& object : objects)
            object.seen = false;
        for (const auto slot : probe.roofs)
            objects[slot].seen = true;
        for (const auto slot : probe.flickers)
            objects[slot].seen = true;
        const auto heroBefore = world.Actors()[HeroSlot];
        const auto weatherBefore = world.Runtime().leafWeather;
        ClothWorld reference = cloth;
        reference.random = Advanced(reference.random, FirstStepDraws + probe.stepDraws);
        GameScene scene = Scene();
        ASSERT_TRUE(scene.Enter().HasValue());

        scene.Step(Client::Input::InputState{});

        ASSERT_EQ(scene.Failure(), std::nullopt);
        ASSERT_TRUE(AdvanceEnvironmentPrefix(world, heroBefore, weatherBefore, probe.layer, reference).HasValue());
        // MoveHero, 57cfc0, TerrainWall, cuerpo, celda vista y viento, detrás de los tres blendMesh.
        reference.random =
            Advanced(reference.random, 8 + HeroActionPrefixRandoms + 8 + HeroBodyDraws + 8 + 1 + ParticleWindDraws +
                                           GateProbeRandoms(*data.gates, world.Runtime().map, heroBefore));
        Client::Render::Models::StepTheClothWorld(reference);

        for (const std::size_t roof : probe.roofs)
        {
            EXPECT_EQ(objects[roof].targetAlpha, 0.0f) << roof;
            EXPECT_LT(objects[roof].alpha, 1.0f) << roof;
        }
        for (std::size_t k = 0; k < probe.flickers.size(); ++k)
        {
            EXPECT_EQ(objects[probe.flickers[k]].state.light, lights[k]) << probe.flickers[k];
        }
        EXPECT_TRUE(world.Actors()[HeroSlot].seen);
        EXPECT_EQ(world.Runtime().warpCounter, WaitDrawThreshold - 1);
        EXPECT_EQ(cloth.random.State(), reference.random.State());
        scene.Leave();
        EXPECT_FALSE(store.Find(Client::Render::TextureSlot{Client::World::FirstTileSlot}).has_value());
        world.Runtime().heroCellLayer = 0;
    }
}

// 0x606560 en un mapa sin los casos de 0x604ab0 portados: con un objeto visto (esos mapas cortan antes, en la
// carga o en 0x6374ce; acá se carga el 18 y se cambia el mapa a mano), el paso corta antes de tocarlo, con los
// rand() de la UI del paso y ninguno más.
TEST_F(GameSceneDrawTest, TheStepStopsAtTheSeenObjectsOfTheOtherMaps)
{
    ASSERT_NO_FATAL_FAILURE(Prepare(18, 128, 128));
    world.Runtime().map = 35;
    ASSERT_FALSE(world.Map().objects.empty());
    world.Map().objects.front().seen = true;
    const Client::World::WorldObject before = world.Map().objects.front();
    GameScene scene = Scene();
    ASSERT_TRUE(scene.Enter().HasValue());

    scene.Step(Client::Input::InputState{});

    ASSERT_TRUE(scene.Failure().has_value());
    EXPECT_NE(scene.Failure()->find("0x606560"), std::string::npos) << *scene.Failure();
    EXPECT_EQ(world.Map().objects.front().alpha, before.alpha);
    EXPECT_EQ(cloth.random.State(), RandomAfter(FirstStepDraws).State());
    scene.Leave();
}

// 0x5fa973..0x5fa9b1 y 0x5fc33e: Atlans, en (226, 103), con agua en la capa 2 cerca, dibuja el cuadro entero: el
// agua de 0x5fa4b0, aditiva y con la cáustica 0x41 + el cuadro de DAT_07d2d360, y ningún quad del pasto
// (0x32..0x34), que el mapa 7 saltea aunque tenga 0x6c9d58.
TEST_F(GameSceneDrawTest, AtlansDrawsItsCausticWaterAndNoGrass)
{
    ASSERT_NO_FATAL_FAILURE(Prepare(7, 226, 103));
    GameScene scene = Scene();
    ASSERT_TRUE(scene.Enter().HasValue());
    Client::Tests::RecordingFrameTarget target;

    scene.PrepareFrame();
    scene.Render(target.BeginFrame(std::chrono::milliseconds{1000}), 0.0f);

    ASSERT_FALSE(scene.Failure().has_value()) << *scene.Failure();
    const Client::Tests::FrameEvent* models = ModelsOf(target.frames.at(0));
    ASSERT_NE(models, nullptr);
    const auto caustic = Client::Render::TextureSlot{static_cast<std::uint16_t>(0x41 + cloth.waterTextureFrame)};
    std::size_t water = 0;
    for (const Client::Render::ModelDrawCommand& command : models->modelCommands)
    {
        ASSERT_TRUE(command.state.texture.has_value());
        const std::uint16_t slot = command.state.texture->value;
        EXPECT_FALSE(slot >= 0x32 && slot <= 0x34) << slot;
        if (slot >= 0x41 && slot <= 0x60)
        {
            EXPECT_EQ(*command.state.texture, caustic);
            EXPECT_EQ(command.state.blend, Client::Render::ModelBlend::Additive);
            ++water;
        }
    }
    EXPECT_GT(water, 0u);
    scene.Leave();
}

// 0x5fc365 con una marca prendida (como la dejarían 0x505de0 o 0x52e810, que MuRet no corre): 0x549130 lee el
// byte +0x28 del slot 0x5e3, Effect\blood01.tga, que el juego de MuRet no sube (0x61d7aa). Corta ahí, con el
// terreno y el pasto dibujados, los 4 + 8 rand() de siempre y sin un vértice del calco (08 P-C49).
TEST_F(GameSceneDrawTest, ALiveGroundMarkStopsWhereMuRetDoesNotKnowItsTexture)
{
    struct Case
    {
        std::uint8_t map;
        std::uint8_t x;
        std::uint8_t y;
    };
    for (const Case& probe : {Case{0, 143, 120}, Case{2, 220, 30}})
    {
        SCOPED_TRACE(static_cast<int>(probe.map));
        ASSERT_NO_FATAL_FAILURE(Prepare(probe.map, probe.x, probe.y));
        world.Runtime().groundMarks = {};
        world.Runtime().groundMarks[17] = Client::World::GroundMark{.live = true,
                                                                    .type = 0x5e3,
                                                                    .size = 0.65f,
                                                                    .position = world.Actors()[HeroSlot].position,
                                                                    .color = glm::vec3(0.1f, 0.0f, 0.0f),
                                                                    .alpha = 1.0f,
                                                                    .life = 60};
        GameScene scene = Scene();
        ASSERT_TRUE(scene.Enter().HasValue());
        Client::Tests::RecordingFrameTarget target;

        scene.PrepareFrame();
        scene.Render(target.BeginFrame(std::chrono::milliseconds{1000}), 0.0f);

        ASSERT_TRUE(scene.Failure().has_value());
        EXPECT_NE(scene.Failure()->find("0x549152"), std::string::npos) << *scene.Failure();
        EXPECT_NE(scene.Failure()->find("mapa " + std::to_string(probe.map)), std::string::npos) << *scene.Failure();
        const Client::Tests::FrameEvent* models = ModelsOf(target.frames.at(0));
        ASSERT_NE(models, nullptr);
        EXPECT_FALSE(models->modelVertices.empty());
        for (const Client::Render::ModelDrawCommand& command : models->modelCommands)
        {
            EXPECT_NE(command.state.texture, Client::Render::TextureSlot{0x5e3});
        }
        EXPECT_EQ(cloth.random.State(), RandomAfter(4 + 8).State());
        scene.Leave();
    }
    world.Runtime().groundMarks = {};
}

// Los mapas 18 a 23 no tienen pase del pasto (0x6c9d58 en 0), y 0x5fa830 llama a 0x405e00 con cada celda del
// primer pase que no tiene el bit 0x08 (0x5facb6): un rand() por celda y, con el área en 0xff, nada más
// (0x405e1e..0x405e2f). Con el terreno de World19 puesto a mano y todas las celdas ocultas salvo la del héroe, el
// cuadro termina con los 4 + 8 rand() de siempre y uno de esa celda, que se dibuja.
TEST_F(GameSceneDrawTest, ChaosCastleDrawsRandomForEachShownCellAndNoGrass)
{
    constexpr std::uint8_t ChaosCastle = 18;
    auto terrain = Client::World::LoadWorldTerrain(m_dataRoot, ChaosCastle);
    ASSERT_TRUE(terrain.HasValue()) << terrain.Error();
    world.Map().terrain = std::move(terrain).Value();
    for (std::uint16_t& cell : world.Map().terrain.attributes->cells)
        cell = static_cast<std::uint16_t>(cell | Client::World::HiddenCellBit);
    const auto heroCell = Client::Data::Terrain::CellIndex(128, 128);
    world.Map().terrain.attributes->cells[heroCell] =
        static_cast<std::uint16_t>(world.Map().terrain.attributes->cells[heroCell] & ~Client::World::HiddenCellBit);
    world.Runtime().map = ChaosCastle;
    Client::Scenes::ActorState& hero = world.Actors()[HeroSlot];
    hero.alive = true;
    hero.cellX = 128;
    hero.cellY = 128;
    hero.position = {12850.0f, 12850.0f, 165.0f};
    world.Runtime().heroSlot = static_cast<std::int32_t>(HeroSlot);
    world.Runtime().hero = HeroSlot;
    world.Runtime().drawingEnabled = true;
    world.Runtime().warpCounter = WaitDrawThreshold;
    GameScene scene = Scene();
    ASSERT_TRUE(scene.Enter().HasValue());
    Client::Tests::RecordingFrameTarget target;

    scene.PrepareFrame();
    scene.Render(target.BeginFrame({}), 0.0f);

    ASSERT_FALSE(scene.Failure().has_value()) << *scene.Failure();
    const Client::Tests::FrameEvent* models = ModelsOf(target.frames[0]);
    ASSERT_NE(models, nullptr);
    EXPECT_FALSE(models->modelVertices.empty());
    EXPECT_EQ(cloth.random.State(), RandomAfter(4 + 8 + 1 + FramePickingDraws + HudBaseDraws).State());
    scene.Leave();
}

// 0x63710f y 0x63475c: el primer paso después de la espera llega a los personajes (0x508f00) antes del primer
// dibujo del juego, con el trapecio que dejó el último dibujo de la selección: el de su punto fijo
// (SelectionCullingPoint), que no sigue al héroe (Q-C216). El TerrainWall pierde el 0x02 (el de (221, 11) en
// Lorencia, Q-C170) y marca la celda lógica. En Lorencia el héroe que sale del pueblo por el oeste, de (96, 126)
// a (95, 126), cae adentro: con la celda lógica queda fuera de la zona segura. En Devias el que sale por el norte,
// de (220, 76) a (220, 77), cae afuera y +0x3e2 queda como estaba. Los rand(): los 8 de MoveHero, los 8 del
// TerrainWall, los del cuerpo (0x4fd5f0: el registro propio y el rand() % 6) y, con el héroe visto, los 8 de la
// celda y el rand() & 7; después, el viento de las partículas y la tela. El dibujo que sigue pasa las marcas del suelo
// (0x5fc365) y los objetos (0x6374ce) con sus 4 + 8 y uno por objeto visto (20 en Lorencia, 44 en Devias, ninguno con
// un caso), los efectos del suelo sin ninguno vivo y deja su propio trapecio. En los personajes (0x510d00), Lorencia
// dibuja al héroe visto sin armas fuera de la zona segura, después de los 3.848 rand() del color propio
// y el rand() de 0x50db57; Devias no lo ve. Los dos abren las puertas y presentan el frame completo.
// El oráculo nuevo antepone hojas/fauna, sondea Gate.bmd y cierra own/Skill antes del cuerpo.
TEST_F(GameSceneDrawTest, TheFirstStepAfterTheWaitReadsTheTrapezoidOfTheSelection)
{
    struct Case
    {
        std::uint8_t map;
        std::uint8_t drawnX;
        std::uint8_t drawnY;
        std::uint8_t logicalX;
        std::uint8_t logicalY;
        bool seen;
        std::uint8_t safeZone;
        int draws;
        int objectDraws;
    };
    for (const Case& probe : {Case{0, 96, 126, 95, 126, true, 0, 8 + 8 + HeroBodyDraws + 8 + 1, 20},
                              Case{2, 220, 76, 220, 77, false, 1, 8 + 8 + HeroBodyDraws, 44}})
    {
        SCOPED_TRACE(static_cast<int>(probe.map));
        ASSERT_NO_FATAL_FAILURE(Prepare(probe.map, probe.drawnX, probe.drawnY));
        MakeTheHeroAPlayer(probe.logicalX, probe.logicalY);
        const Client::World::TerrainPolygon selection = SelectionPolygon();
        world.Runtime().terrainPolygon = selection;
        GameScene scene = Scene();
        ASSERT_TRUE(scene.Enter().HasValue());
        const std::uint16_t before = Cell(221, 11);
        const Client::Scenes::ActorState& hero = world.Actors()[HeroSlot];
        ASSERT_EQ(Cell(probe.drawnX, probe.drawnY) & 1, 1);
        ASSERT_EQ(Cell(probe.logicalX, probe.logicalY) & 1, 0);

        const auto heroBefore = hero;
        const auto weatherBefore = world.Runtime().leafWeather;
        const auto layerBefore = world.Runtime().heroCellLayer;
        ClothWorld reference = cloth;
        reference.random = Advanced(reference.random, FirstStepDraws);
        scene.Step(Client::Input::InputState{});

        ASSERT_EQ(scene.Failure(), std::nullopt);
        ASSERT_TRUE(AdvanceEnvironmentPrefix(world, heroBefore, weatherBefore, layerBefore, reference).HasValue());
        reference.random =
            Advanced(reference.random, probe.draws + HeroActionPrefixRandoms + ParticleWindDraws +
                                           GateProbeRandoms(*data.gates, world.Runtime().map, heroBefore));
        Client::Render::Models::StepTheClothWorld(reference);
        EXPECT_EQ(world.Runtime().warpCounter, WaitDrawThreshold - 1);
        EXPECT_EQ(Cell(221, 11), before & ~0x02);
        if (probe.map == 0)
        {
            EXPECT_EQ(before, 2);
        }
        EXPECT_EQ(Cell(probe.logicalX, probe.logicalY) & Client::World::CharacterWallBit,
                  Client::World::CharacterWallBit);
        EXPECT_EQ(hero.seen, probe.seen);
        EXPECT_EQ(hero.safeZone, probe.safeZone);
        EXPECT_EQ(world.Runtime().terrainPolygon.x, selection.x) << "el paso no escribe el trapecio";
        EXPECT_EQ(cloth.random.State(), reference.random.State());

        Client::Tests::RecordingFrameTarget target;
        scene.PrepareFrame();
        ASSERT_FALSE(scene.HoldsTheLastFrame());
        scene.Render(target.BeginFrame(std::chrono::milliseconds{1000}), 0.0f);

        ASSERT_EQ(scene.Failure(), std::nullopt);
        target.Present();
        EXPECT_TRUE(target.frames.back().presented);
        EXPECT_EQ(Cell(119, 115) & 0x04, 0) << "las puertas se abren después del jugador sin armas";
        CrtRandom expected = reference.random;
        // 50e298..50e6af: dos cierres de byte38e, uno por mano vacía fuera de la espalda.
        const int characterDraws = probe.seen ? Client::Render::Models::OwnRecordCloseRandoms + 1 + 2 : 0;
        for (int i = 0; i < 4 + 8 + probe.objectDraws + characterDraws + HudBaseDraws; ++i)
        {
            static_cast<void>(expected.Next());
        }
        EXPECT_EQ(cloth.random.State(), expected.State());
        EXPECT_NE(world.Runtime().terrainPolygon.x, selection.x) << "el dibujo deja el trapecio del juego";
        scene.Leave();
    }
}

// 0x63712d: después de los personajes, el paso del juego envejece las marcas del suelo (0x549090), sin rand(): la
// prendida pierde un paso de vida, crece 0,004 y se pone roja (0,1; 0; 0); la apagada no cambia. Con el héroe de
// TheFirstStepAfterTheWaitReadsTheTrapezoidOfTheSelection, en Lorencia y en Devias.
TEST_F(GameSceneDrawTest, TheStepAgesTheGroundMarksAfterTheCharacters)
{
    struct Case
    {
        std::uint8_t map;
        std::uint8_t drawnX;
        std::uint8_t drawnY;
        std::uint8_t logicalX;
        std::uint8_t logicalY;
        int draws;
    };
    for (const Case& probe :
         {Case{0, 96, 126, 95, 126, 8 + 8 + HeroBodyDraws + 8 + 1}, Case{2, 220, 76, 220, 77, 8 + 8 + HeroBodyDraws}})
    {
        SCOPED_TRACE(static_cast<int>(probe.map));
        ASSERT_NO_FATAL_FAILURE(Prepare(probe.map, probe.drawnX, probe.drawnY));
        MakeTheHeroAPlayer(probe.logicalX, probe.logicalY);
        world.Runtime().terrainPolygon = SelectionPolygon();
        Client::World::GroundMarkTable& marks = world.Runtime().groundMarks;
        marks = {};
        const Client::World::GroundMark blood{.live = true,
                                              .type = 0x5e3,
                                              .size = 0.65f,
                                              .position = glm::vec3(1000.0f, 2000.0f, 0.0f),
                                              .color = glm::vec3(1.0f, 1.0f, 1.0f),
                                              .alpha = 1.0f,
                                              .life = 60};
        marks[17] = blood;
        marks[18] = blood;
        marks[18].live = false;
        GameScene scene = Scene();
        ASSERT_TRUE(scene.Enter().HasValue());

        const auto heroBefore = world.Actors()[HeroSlot];
        const auto weatherBefore = world.Runtime().leafWeather;
        const auto layerBefore = world.Runtime().heroCellLayer;
        ClothWorld reference = cloth;
        reference.random = Advanced(reference.random, FirstStepDraws);
        scene.Step(Client::Input::InputState{});

        ASSERT_EQ(scene.Failure(), std::nullopt);
        ASSERT_TRUE(AdvanceEnvironmentPrefix(world, heroBefore, weatherBefore, layerBefore, reference).HasValue());
        reference.random =
            Advanced(reference.random, probe.draws + HeroActionPrefixRandoms + ParticleWindDraws +
                                           GateProbeRandoms(*data.gates, world.Runtime().map, heroBefore));
        Client::Render::Models::StepTheClothWorld(reference);
        EXPECT_TRUE(marks[17].live);
        EXPECT_EQ(marks[17].life, 59);
        EXPECT_EQ(marks[17].size, static_cast<float>(static_cast<double>(0.65f) + static_cast<double>(0.004f)));
        EXPECT_EQ(marks[17].color, glm::vec3(0.1f, 0.0f, 0.0f));
        EXPECT_EQ(marks[17].alpha, 1.0f);
        Client::World::GroundMark off = blood;
        off.live = false;
        EXPECT_EQ(marks[18], off);
        EXPECT_EQ(cloth.random.State(), reference.random.State());
        scene.Leave();
    }
    world.Runtime().groundMarks = {};
}

// Q-C217: sin un dibujo antes (un WorldState recién hecho), el trapecio es el de la .bss, todo en cero, y 0x5fba90
// ve a todos con el margen −20. El héroe de Devias que no se ve con el de la selección se ve con este y toma la
// zona segura de la celda lógica.
TEST_F(GameSceneDrawTest, WithoutAPreviousDrawTheFirstStepSeesWithTheTrapezoidOfTheBss)
{
    ASSERT_NO_FATAL_FAILURE(Prepare(2, 220, 76));
    MakeTheHeroAPlayer(220, 77);
    ASSERT_EQ(world.Runtime().terrainPolygon.x, (std::array<float, 4>{}));
    ASSERT_EQ(world.Runtime().terrainPolygon.y, (std::array<float, 4>{}));
    GameScene scene = Scene();
    ASSERT_TRUE(scene.Enter().HasValue());

    const auto heroBefore = world.Actors()[HeroSlot];
    const auto weatherBefore = world.Runtime().leafWeather;
    const auto layerBefore = world.Runtime().heroCellLayer;
    ClothWorld reference = cloth;
    reference.random = Advanced(reference.random, FirstStepDraws);
    scene.Step(Client::Input::InputState{});

    ASSERT_EQ(scene.Failure(), std::nullopt);
    ASSERT_TRUE(AdvanceEnvironmentPrefix(world, heroBefore, weatherBefore, layerBefore, reference).HasValue());
    reference.random =
        Advanced(reference.random, 8 + 8 + HeroBodyDraws + 8 + 1 + HeroActionPrefixRandoms + ParticleWindDraws +
                                       GateProbeRandoms(*data.gates, world.Runtime().map, heroBefore));
    Client::Render::Models::StepTheClothWorld(reference);
    const Client::Scenes::ActorState& hero = world.Actors()[HeroSlot];
    EXPECT_TRUE(hero.seen);
    EXPECT_EQ(hero.safeZone, 0);
    EXPECT_EQ(cloth.random.State(), reference.random.State());
    scene.Leave();
}

// Sin Player.bmd (main sin los modelos, o un test que no los pasa), el cuerpo del héroe corta donde 0x4f9580 lee
// sus acciones, después del registro propio de 0x4fc450 y antes del rand() % 6 de 0x4fd7e4. En Lorencia y en
// Devias.
TEST_F(GameSceneDrawTest, WithoutPlayerBmdTheStepStopsAtTheBody)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(static_cast<int>(map));
        ASSERT_NO_FATAL_FAILURE(Prepare(map, 128, 128));
        MakeTheHeroAPlayer(128, 128);
        GameScene scene(SceneContext{.dataRoot = m_dataRoot,
                                     .textures = store,
                                     .log = log,
                                     .cloth = &cloth,
                                     .camera = &camera,
                                     .cameraInput = &cameraInput,
                                     .world = &world},
                        nullptr, nullptr, &data.items->table, nullptr, &*data.gates, &*data.skills);
        ASSERT_TRUE(scene.Enter().HasValue());

        const auto heroBefore = world.Actors()[HeroSlot];
        const auto weatherBefore = world.Runtime().leafWeather;
        const auto layerBefore = world.Runtime().heroCellLayer;
        ClothWorld reference = cloth;
        reference.random = Advanced(reference.random, FirstStepDraws);
        scene.Step(Client::Input::InputState{});

        ASSERT_TRUE(AdvanceEnvironmentPrefix(world, heroBefore, weatherBefore, layerBefore, reference).HasValue());
        ASSERT_TRUE(scene.Failure().has_value());
        EXPECT_NE(scene.Failure()->find("0x4f9580"), std::string::npos) << *scene.Failure();
        EXPECT_EQ(cloth.random.State(),
                  Advanced(reference.random, 8 + 8 + Client::Render::Models::OwnRecordCloseRandoms +
                                                 HeroActionPrefixRandoms +
                                                 GateProbeRandoms(*data.gates, world.Runtime().map, heroBefore))
                      .State());
        scene.Leave();
    }
}

// Un dibujo cortado deja la escena en Failure: el paso que sigue no corre, aunque el dibujo haya dejado su
// trapecio. La marca de sangre usa el selector nativo 0x5e3, cuya metadata sigue ausente.
TEST_F(GameSceneDrawTest, AStepAfterACutDrawDoesNothing)
{
    ASSERT_NO_FATAL_FAILURE(Prepare(0, 143, 120));
    world.Runtime().groundMarks[0] = Client::World::GroundMark{.live = true,
                                                               .type = 0x5e3,
                                                               .size = 1.0f,
                                                               .position = world.Actors()[HeroSlot].position,
                                                               .color = glm::vec3(1.0f),
                                                               .alpha = 1.0f,
                                                               .life = 60};
    GameScene scene = Scene();
    ASSERT_TRUE(scene.Enter().HasValue());
    Client::Tests::RecordingFrameTarget target;
    scene.PrepareFrame();
    scene.Render(target.BeginFrame(std::chrono::milliseconds{1000}), 0.0f);
    ASSERT_TRUE(scene.Failure().has_value());
    const std::string cut = *scene.Failure();
    EXPECT_NE(cut.find("0x549152"), std::string::npos);
    const std::uint32_t random = cloth.random.State();

    scene.Step(Client::Input::InputState{});

    EXPECT_EQ(scene.Failure(), cut);
    EXPECT_EQ(world.Runtime().warpCounter, WaitDrawThreshold);
    EXPECT_EQ(cloth.random.State(), random);
    scene.Leave();
}
} // namespace
