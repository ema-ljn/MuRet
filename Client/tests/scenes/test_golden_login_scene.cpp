#include "Scenes/LoginScene.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/DarkHorse.h"
#include "Scenes/GameData.h"
#include "Scenes/GameModels.h"
#include "Scenes/LoadingScene.h"
#include "Scenes/SceneCameras.h"
#include "Scenes/SceneContext.h"
#include "Scenes/TextureSets.h"
#include "UI/Notices.h"
#include "UI/ServerListModel.h"
#include "support/FakeClock.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include "support/RecordingTextDrawer.h"
#include "Scenes/LoginModels.h"
#include "support/FixedRandom.h"
#include "Render/ModelBatch.h"
#include "Data/Textures/Sampling.h"
#include "Input/InputState.h"
#include "Data/Terrain/TerrainObjects.h"
#include "World/WorldObjects.h"
#include "Scenes/WorldModels.h"

#include <glm/ext/matrix_double4x4.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_double3.hpp>
#include <glm/ext/vector_double4.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <iterator>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// La carga y el login con el Data\ del laboratorio: entran (sus juegos de texturas en sus slots
// de la tabla falsa, el login con las de las ventanas), y el login dibuja sus tres textos de
// Text.bmd. Ningún texto va a la salida:
// se miran el largo y dónde quedó la versión, nunca lo que dicen.
namespace
{
using Client::Scenes::SingleModelLibrary;
using Client::Tests::FixedSeed;
using Client::Tests::FrameEvent;
using Kind = Client::Tests::FrameEvent::Kind;
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Render::TextureSlot;
using Client::Scenes::LoadGameData;
using Client::Scenes::LoadingScene;
using Client::Scenes::LoadingTextureSet;
using Client::Scenes::LoginScene;
using Client::Scenes::LoginTextureSet;
using Client::Scenes::SceneContext;
using Client::Scenes::WindowTextureSet;
using Client::Tests::FakeTextureStore;
using Client::Tests::RecordedText;
using Client::Tests::RecordingFrameTarget;
using Client::Tests::RecordingSink;
using Client::UI::FirstButtonSlot;
using Client::UI::GroupNameText;
using Client::UI::GroupSlot;
using Client::UI::LoadText;
using Client::UI::LoginTitleText;
using Client::UI::NoticeId;
using Client::UI::NoticeTexts;
using Client::UI::ServerGroupCount;
using Client::UI::TextsOf;

using GoldenLoginScene = Client::Tests::GoldenDataTest;

constexpr std::string_view Version = "0.0.0";

TEST_F(GoldenLoginScene, EntersWithTheLabData)
{
    auto data = LoadGameData(m_dataRoot);
    ASSERT_TRUE(data.HasValue()) << data.Error();
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    const SingleModelLibrary models;
    LoginScene scene(SceneContext{m_dataRoot, store, log}, data.Value(), models, nullptr, Version, FixedSeed);

    const auto entered = scene.Enter();

    ASSERT_TRUE(entered.HasValue()) << entered.Error();
    // Y las dos del logo (TheLogoLoads), las 15 del terreno (los 14 tiles de World56 y el 0x122,
    // TheTerrainGoesBeforeTheCharacter) y las 63 del mundo: las 3 del dragón y 62 de Object56\ que esta
    // tabla vacía no tiene, menos fire01 y fire02, que no están (GoldenWorldModels).
    ASSERT_EQ(store.Size(), LoginTextureSet.size() + WindowTextureSet.size() + 2 + 15 + 63);
    for (const auto& entry : LoginTextureSet)
    {
        EXPECT_TRUE(store.Find(TextureSlot{entry.slot}).has_value()) << entry.slot;
    }
    for (const auto& entry : WindowTextureSet)
    {
        EXPECT_TRUE(store.Find(TextureSlot{entry.slot}).has_value()) << entry.slot;
    }

    RecordingFrameTarget target;
    scene.Render(target.BeginFrame(std::chrono::milliseconds{40}), 0.0f);
    target.Present();
    scene.Leave();

    // Los tres están en el Text.bmd del laboratorio, y el 456 tiene un "%s" para la versión.
    const std::vector<RecordedText>& texts = target.Texts();
    ASSERT_EQ(texts.size(), 3u);
    for (const RecordedText& text : texts)
    {
        EXPECT_FALSE(text.text.empty());
    }
    EXPECT_NE(texts[2].text.find(Version), std::string::npos);
    EXPECT_EQ(texts[2].text.find("%s"), std::string::npos);
    EXPECT_EQ(store.Size(), 0u);
}

// Q-C92: Logo03.bmd tiene dos mallas, logo.jpg y logo2.jpg, que 0x60d5f0 sube a los dos slots que
// siguen al mundo (0x455 y 0x456 en el original; acá, sin la tabla de la carga, 0x45a y 0x45b) con
// GL_LINEAR; al salir, se sueltan. El logo se dibuja: un tramo opaco y el aditivo de su
// blendMesh, con la luz del primer paso.
TEST_F(GoldenLoginScene, TheLogoLoads)
{
    auto data = LoadGameData(m_dataRoot);
    ASSERT_TRUE(data.HasValue()) << data.Error();
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    const SingleModelLibrary models;
    LoginScene scene(SceneContext{m_dataRoot, store, log}, data.Value(), models, nullptr, Version, FixedSeed);

    ASSERT_TRUE(scene.Enter().HasValue());

    // Sin la tabla de la carga, el mundo deja el próximo en 0x45a (GoldenWorldModels).
    for (const std::uint16_t slot : {std::uint16_t{0x45a}, std::uint16_t{0x45b}})
    {
        const auto texture = store.Find(TextureSlot{slot});
        ASSERT_TRUE(texture.has_value()) << std::hex << slot;
        EXPECT_EQ(texture->sampling.filter, Client::Data::Textures::Filter::Linear) << std::hex << slot;
        EXPECT_EQ(texture->sampling.wrap, Client::Data::Textures::Wrap::Repeat) << std::hex << slot;
    }
    EXPECT_FALSE(store.Find(TextureSlot{0x45c}).has_value());

    scene.Step(Client::Input::InputState{});
    RecordingFrameTarget target;
    scene.Render(target.BeginFrame(std::chrono::milliseconds{40}), 0.0f);
    target.Present();
    const auto drawn = std::ranges::find(target.frames.back().events, Kind::Models, &FrameEvent::kind);
    ASSERT_NE(drawn, target.frames.back().events.end());
    // Sus dos tramos seguidos, entre los de los objetos del mundo (TheLogoIsAWorldObject): el opaco con
    // logo.jpg y el aditivo de su blendMesh con logo2.jpg.
    const auto& commands = drawn->modelCommands;
    const auto logo =
        std::ranges::find(commands, std::optional{TextureSlot{0x45a}},
                          [](const Client::Render::ModelDrawCommand& command) { return command.state.texture; });
    ASSERT_NE(logo, commands.end());
    ASSERT_NE(logo + 1, commands.end());
    EXPECT_EQ(logo->state.blend, Client::Render::ModelBlend::Opaque);
    EXPECT_EQ((logo + 1)->state.texture, TextureSlot{0x45b});
    EXPECT_EQ((logo + 1)->state.blend, Client::Render::ModelBlend::Additive);

    scene.Leave();
    EXPECT_FALSE(store.Find(TextureSlot{0x45a}).has_value());
    EXPECT_FALSE(store.Find(TextureSlot{0x45b}).has_value());
}

// CL7.1: el login carga el terreno de World56 y lo dibuja primero en el pase 3D (0x5fc260(0) antes de
// 0x510d00): los tramos del principio son de los tiles (0x23..0x30) y del 0x122, y en el login, con el
// lago al fondo, hay agua (TileWater01, el 0x28). Al salir, sus 15 slots se sueltan.
TEST_F(GoldenLoginScene, TheTerrainGoesBeforeTheCharacter)
{
    auto data = LoadGameData(m_dataRoot);
    ASSERT_TRUE(data.HasValue()) << data.Error();
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    // Con los modelos de la carga, para que el personaje se dibuje.
    Client::Scenes::GameModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, store, log).HasValue());
    LoginScene scene(SceneContext{m_dataRoot, store, log}, data.Value(), models, nullptr, Version, FixedSeed);
    ASSERT_TRUE(scene.Enter().HasValue());
    for (std::uint16_t slot = 0x23; slot <= 0x30; ++slot)
    {
        EXPECT_TRUE(store.Find(TextureSlot{slot}).has_value()) << std::hex << slot;
    }
    EXPECT_TRUE(store.Find(TextureSlot{0x122}).has_value());

    RecordingFrameTarget target;
    scene.Render(target.BeginFrame(std::chrono::milliseconds{40}), 0.0f);
    target.Present();
    const auto drawn = std::ranges::find(target.frames.back().events, Kind::Models, &FrameEvent::kind);
    ASSERT_NE(drawn, target.frames.back().events.end());
    const auto isTerrain = [](const Client::Render::ModelDrawCommand& command)
    {
        return command.state.texture.has_value() &&
               ((command.state.texture->value >= 0x23 && command.state.texture->value <= 0x30) ||
                command.state.texture->value == 0x122);
    };
    ASSERT_FALSE(drawn->modelCommands.empty());
    EXPECT_TRUE(isTerrain(drawn->modelCommands.front()));
    const auto firstOther = std::ranges::find_if_not(drawn->modelCommands, isTerrain);
    EXPECT_TRUE(std::all_of(firstOther, drawn->modelCommands.end(),
                            [&isTerrain](const auto& command) { return !isTerrain(command); }))
        << "el terreno va todo antes que los modelos";
    // Y el personaje está, después: sus partes van con las texturas de Player\ (desde 0x138, 0x60f7d0) o
    // las de la piel (0x12e..0x137).
    const auto isCharacter = [](const Client::Render::ModelDrawCommand& command)
    {
        return command.state.texture.has_value() && command.state.texture->value >= 0x12e &&
               command.state.texture->value < 0x25a;
    };
    EXPECT_NE(firstOther, drawn->modelCommands.end());
    EXPECT_TRUE(std::any_of(firstOther, drawn->modelCommands.end(), isCharacter)) << "el personaje";
    EXPECT_FALSE(std::any_of(drawn->modelCommands.begin(), firstOther, isCharacter));
    EXPECT_TRUE(std::ranges::any_of(drawn->modelCommands,
                                    [](const auto& command) { return command.state.texture == TextureSlot{0x28}; }))
        << "el lago";

    scene.Leave();
    EXPECT_FALSE(store.Find(TextureSlot{0x23}).has_value());
    EXPECT_FALSE(store.Find(TextureSlot{0x122}).has_value());
}

// CL7.2: con los modelos de la carga (GameModels y su tabla de slots, como en main.cpp), las texturas
// de los objetos de Object56\ van desde 0x41c y el logo sigue donde termina el mundo: en MuRet, 0x45a
// (0x455 en el original, que ya tiene cinco de esos nombres en la tabla: GoldenWorldModels).
constexpr std::uint16_t WorldFirstSlot = 0x41c;
constexpr std::uint16_t LogoFirstSlot = 0x45a;

bool IsWorldObject(const Client::Render::ModelDrawCommand& command)
{
    return command.state.texture.has_value() && command.state.texture->value >= WorldFirstSlot &&
           command.state.texture->value < LogoFirstSlot;
}

bool IsLogo(const Client::Render::ModelDrawCommand& command)
{
    return command.state.texture.has_value() &&
           (command.state.texture->value == LogoFirstSlot || command.state.texture->value == LogoFirstSlot + 1);
}

class GoldenLoginWorld : public Client::Tests::GoldenDataTest
{
protected:
    void SetUp() override
    {
        Client::Tests::GoldenDataTest::SetUp();
        auto data = LoadGameData(m_dataRoot);
        ASSERT_TRUE(data.HasValue()) << data.Error();
        m_data.emplace(std::move(data).Value());
        ASSERT_TRUE(m_models.Load(m_dataRoot, m_store, m_log).HasValue());
    }

    // Un frame del login después de `steps` pasos: sus tramos y sus vértices.
    FrameEvent Frame(LoginScene& scene, int steps)
    {
        for (int k = 0; k < steps; ++k)
        {
            scene.Step(Client::Input::InputState{});
        }
        RecordingFrameTarget target;
        scene.Render(target.BeginFrame(std::chrono::milliseconds{40}), 0.0f);
        target.Present();
        const auto drawn = std::ranges::find(target.frames.back().events, Kind::Models, &FrameEvent::kind);
        return drawn != target.frames.back().events.end() ? *drawn : FrameEvent{};
    }

    FakeTextureStore m_store;
    RecordingSink m_sink;
    Log m_log{m_sink, LogLevel::Debug};
    std::optional<Client::Scenes::GameData> m_data;
    Client::Scenes::GameModels m_models;
};

// 0x6046a0 después de 0x4453b0: los objetos del mundo van después del caballo.
TEST_F(GoldenLoginWorld, TheObjectsGoAfterTheHorse)
{
    LoginScene scene(SceneContext{m_dataRoot, m_store, m_log}, *m_data, m_models, nullptr, Version, FixedSeed,
                     &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    const FrameEvent frame = Frame(scene, 3);
    const auto horse = m_models.MeshTextures(Client::Scenes::DarkHorseModel);
    ASSERT_FALSE(horse.empty());
    std::optional<std::size_t> lastHorse;
    std::optional<std::size_t> firstObject;
    for (std::size_t k = 0; k < frame.modelCommands.size(); ++k)
    {
        const auto& command = frame.modelCommands[k];
        if (command.state.texture && std::ranges::find(horse, *command.state.texture) != horse.end())
        {
            lastHorse = k;
        }
        if (!firstObject && IsWorldObject(command))
        {
            firstObject = k;
        }
    }
    ASSERT_TRUE(lastHorse.has_value());
    ASSERT_TRUE(firstObject.has_value());
    EXPECT_LT(*lastHorse, *firstObject);
    scene.Leave();
}

// El logo es uno más de la lista (el 0xa2 en el sector (6, 9)): sus dos texturas, con GL_LINEAR, van al
// próximo slot que dejó el mundo, y se dibuja entre los objetos de los sectores de antes y de después.
TEST_F(GoldenLoginWorld, TheLogoIsAWorldObject)
{
    LoginScene scene(SceneContext{m_dataRoot, m_store, m_log}, *m_data, m_models, nullptr, Version, FixedSeed,
                     &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    for (const std::uint16_t slot : {LogoFirstSlot, static_cast<std::uint16_t>(LogoFirstSlot + 1)})
    {
        const auto texture = m_store.Find(TextureSlot{slot});
        ASSERT_TRUE(texture.has_value()) << std::hex << slot;
        EXPECT_EQ(texture->sampling.filter, Client::Data::Textures::Filter::Linear) << std::hex << slot;
    }
    EXPECT_FALSE(m_store.Find(TextureSlot{static_cast<std::uint16_t>(LogoFirstSlot + 2)}).has_value());

    const FrameEvent frame = Frame(scene, 1);
    const auto logo = std::ranges::find_if(frame.modelCommands, IsLogo);
    ASSERT_NE(logo, frame.modelCommands.end());
    EXPECT_TRUE(std::any_of(frame.modelCommands.begin(), logo, IsWorldObject)) << "los de los sectores de antes";
    EXPECT_TRUE(std::any_of(logo, frame.modelCommands.end(), IsWorldObject)) << "los de los sectores de después";

    scene.Leave();
    for (std::uint16_t slot = WorldFirstSlot; slot < LogoFirstSlot + 2; ++slot)
    {
        EXPECT_FALSE(m_store.Find(TextureSlot{slot}).has_value()) << std::hex << slot;
    }
}

// 0x606560 solo avanza los objetos que ya se dibujaron: los pasos que corren antes del primer dibujo
// (el del mismo frame que trae la carga, o los de la espera del connect, Q-C85) no los mueven.
TEST_F(GoldenLoginWorld, TheObjectsDoNotStepBeforeTheirFirstDraw)
{
    const auto objectVertices = [](const FrameEvent& frame)
    {
        std::vector<glm::vec3> positions;
        for (const auto& command : frame.modelCommands)
        {
            if (IsWorldObject(command))
            {
                for (std::size_t k = 0; k < command.vertexCount; ++k)
                {
                    positions.push_back(frame.modelVertices[command.firstVertex + k].position);
                }
            }
        }
        return positions;
    };
    LoginScene first(SceneContext{m_dataRoot, m_store, m_log}, *m_data, m_models, nullptr, Version, FixedSeed,
                     &m_models.TextureSlots());
    ASSERT_TRUE(first.Enter().HasValue());
    const auto drawnFirst = objectVertices(Frame(first, 0));
    first.Leave();
    LoginScene stepped(SceneContext{m_dataRoot, m_store, m_log}, *m_data, m_models, nullptr, Version, FixedSeed,
                       &m_models.TextureSlots());
    ASSERT_TRUE(stepped.Enter().HasValue());
    EXPECT_EQ(objectVertices(Frame(stepped, 5)), drawnFirst);
    stepped.Leave();
}

// El original saltea sin avisar un modelo que falta (Q-C23); MuRet lo dice una vez por entrada, con los
// tipos que pide el .obj y no tienen modelo: en el Data\ del laboratorio, el 82 (Object83.bmd).
TEST_F(GoldenLoginWorld, TheObjWarnsOfTheModelsThatAreMissing)
{
    LoginScene scene(SceneContext{m_dataRoot, m_store, m_log}, *m_data, m_models, nullptr, Version, FixedSeed,
                     &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    std::size_t warnings = 0;
    for (std::size_t k = 0; k < m_sink.lines.size(); ++k)
    {
        if (m_sink.levels[k] == LogLevel::Warning && m_sink.lines[k].find("no tienen modelo") != std::string::npos)
        {
            ++warnings;
            EXPECT_NE(m_sink.lines[k].find("82"), std::string::npos) << m_sink.lines[k];
        }
    }
    EXPECT_EQ(warnings, 1u);
    scene.Leave();
}

// Cada entrada vuelve a cargar el mundo (0x61a560) y a crear los objetos (0x606660): el primer frame
// de la segunda es el de la primera, aunque en el medio los remolinos giraron y el 90 se prendió.
TEST_F(GoldenLoginWorld, EnteringAgainCreatesTheObjectsAgain)
{
    LoginScene scene(SceneContext{m_dataRoot, m_store, m_log}, *m_data, m_models, nullptr, Version, FixedSeed,
                     &m_models.TextureSlots());
    const auto objectVertices = [](const FrameEvent& frame)
    {
        std::vector<glm::vec3> positions;
        for (const auto& command : frame.modelCommands)
        {
            if (IsWorldObject(command))
            {
                for (std::size_t k = 0; k < command.vertexCount; ++k)
                {
                    positions.push_back(frame.modelVertices[command.firstVertex + k].position);
                }
            }
        }
        return positions;
    };
    ASSERT_TRUE(scene.Enter().HasValue());
    const auto first = objectVertices(Frame(scene, 0));
    const auto later = objectVertices(Frame(scene, 20));
    EXPECT_NE(first, later) << "los remolinos giran";
    scene.Leave();
    ASSERT_TRUE(scene.Enter().HasValue());
    EXPECT_EQ(objectVertices(Frame(scene, 0)), first);
    scene.Leave();
}

// El agua del lago (TileWater01, el 0x28) se mueve: 0x5fc260 le corre la u con el reloj del frame,
// (ftol(T) % 20000) · 5e-5, y 0x5fbf60 le escribe la ola de la v en cada paso, con el reloj de la lógica
// (06 § El terreno del login). Un segundo después, cada esquina del agua está 0,05 más allá en u y la ola
// cambió.
TEST_F(GoldenLoginScene, TheLakeMovesWithTheClock)
{
    auto data = LoadGameData(m_dataRoot);
    ASSERT_TRUE(data.HasValue()) << data.Error();
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    Client::Tests::FakeClock clock;
    const SingleModelLibrary models;
    LoginScene scene(SceneContext{m_dataRoot, store, log, &clock}, data.Value(), models, nullptr, Version, FixedSeed);
    ASSERT_TRUE(scene.Enter().HasValue());
    const auto water = [&scene](Client::Core::Duration frameClock)
    {
        RecordingFrameTarget target;
        scene.Render(target.BeginFrame(frameClock), 0.0f);
        target.Present();
        std::vector<glm::vec2> uvs;
        const auto drawn = std::ranges::find(target.frames.back().events, Kind::Models, &FrameEvent::kind);
        if (drawn == target.frames.back().events.end())
        {
            return uvs;
        }
        for (const auto& command : drawn->modelCommands)
        {
            if (command.state.texture == TextureSlot{0x28})
            {
                for (std::size_t k = 0; k < command.vertexCount; ++k)
                {
                    uvs.push_back(drawn->modelVertices[command.firstVertex + k].uv);
                }
            }
        }
        return uvs;
    };
    clock.Advance(std::chrono::milliseconds{1000});
    scene.Step(Client::Input::InputState{});
    const std::vector<glm::vec2> before = water(std::chrono::milliseconds{1000});
    clock.Advance(std::chrono::milliseconds{1000});
    scene.Step(Client::Input::InputState{});
    const std::vector<glm::vec2> after = water(std::chrono::milliseconds{2000});

    ASSERT_FALSE(before.empty());
    ASSERT_EQ(before.size(), after.size());
    std::size_t wavesThatChanged = 0;
    for (std::size_t k = 0; k < before.size(); ++k)
    {
        EXPECT_NEAR(after[k].x - before[k].x, 0.05f, 1e-4f) << k;
        if (std::abs(after[k].y - before[k].y) > 1e-5f)
        {
            ++wavesThatChanged;
        }
    }
    EXPECT_GT(wavesThatChanged, before.size() / 2) << "la ola de la v sigue a los pasos";
}

// Cada entrada vuelve a leer el terreno (0x6349a0 → 0x61a560): al salir se sueltan sus slots, y al
// volver a entrar suben de nuevo y el primer frame dibuja lo mismo que el de la primera vez.
TEST_F(GoldenLoginScene, EnteringAgainLoadsTheTerrainAgain)
{
    auto data = LoadGameData(m_dataRoot);
    ASSERT_TRUE(data.HasValue()) << data.Error();
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    const SingleModelLibrary models;
    LoginScene scene(SceneContext{m_dataRoot, store, log}, data.Value(), models, nullptr, Version, FixedSeed);
    const auto terrainVertices = [&scene]()
    {
        RecordingFrameTarget target;
        scene.Render(target.BeginFrame(std::chrono::milliseconds{40}), 0.0f);
        target.Present();
        std::vector<glm::vec3> positions;
        const auto drawn = std::ranges::find(target.frames.back().events, Kind::Models, &FrameEvent::kind);
        if (drawn != target.frames.back().events.end())
        {
            for (const auto& vertex : drawn->modelVertices)
            {
                positions.push_back(vertex.position);
            }
        }
        return positions;
    };
    ASSERT_TRUE(scene.Enter().HasValue());
    const auto first = terrainVertices();
    ASSERT_FALSE(first.empty());
    scene.Leave();
    EXPECT_FALSE(store.Find(TextureSlot{0x28}).has_value());
    ASSERT_TRUE(scene.Enter().HasValue());
    EXPECT_TRUE(store.Find(TextureSlot{0x28}).has_value());
    EXPECT_EQ(terrainVertices(), first);
    scene.Leave();
}

// CL7.6 A: el dragón (el efecto 0xb4, con las texturas de Dragon.bmd en 0x69..0x6b) se dibuja después de
// los objetos del mundo y del logo (0x52d620 después de 0x6046a0), opaco.
bool IsDragon(const Client::Render::ModelDrawCommand& command)
{
    return command.state.texture.has_value() && command.state.texture->value >= 0x69 &&
           command.state.texture->value <= 0x6b;
}

TEST_F(GoldenLoginWorld, TheDragonDrawsAfterTheObjects)
{
    LoginScene scene(SceneContext{m_dataRoot, m_store, m_log}, *m_data, m_models, nullptr, Version, FixedSeed,
                     &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    const FrameEvent frame = Frame(scene, 1);
    const auto firstDragon = std::ranges::find_if(frame.modelCommands, IsDragon);
    ASSERT_NE(firstDragon, frame.modelCommands.end());
    EXPECT_FALSE(std::any_of(firstDragon, frame.modelCommands.end(), IsWorldObject)) << "los objetos van antes";
    EXPECT_FALSE(std::any_of(firstDragon, frame.modelCommands.end(), IsLogo)) << "el logo va antes";
    EXPECT_TRUE(std::all_of(firstDragon, frame.modelCommands.end(), [](const auto& command)
                            { return command.state.blend == Client::Render::ModelBlend::Opaque; }));
    scene.Leave();
}

// CL7.6 B1: con lo global de la tela (como en main.cpp), el dibujo del personaje pide los sprites del
// frame, los 30 destellos del casco blanco (0x455930) y después los cinco del cetro (0x509340, caso
// 0x620), y van aditivos en su pase, el último de los modelos, con la vista en la identidad. Siguen los
// dos del farol (CL7.6 B4). Después van las partículas (0x548550, CL7.6 B2): los alientos del caballo que
// haya.
TEST_F(GoldenLoginWorld, TheSpritesOfTheCharacterGoInTheirPass)
{
    Client::Render::Models::ClothWorld cloth;
    LoginScene scene(SceneContext{.dataRoot = m_dataRoot, .textures = m_store, .log = m_log, .cloth = &cloth}, *m_data,
                     m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    for (int k = 0; k < 3; ++k)
    {
        scene.Step(Client::Input::InputState{});
    }
    RecordingFrameTarget target;
    scene.Render(target.BeginFrame(std::chrono::milliseconds{40}), 0.0f);
    target.Present();
    std::vector<FrameEvent> models;
    std::ranges::copy_if(target.frames.back().events, std::back_inserter(models),
                         [](const FrameEvent& event) { return event.kind == Kind::Models; });
    ASSERT_EQ(models.size(), 2u) << "el mundo y los sprites";
    // Cada sprite, dos triángulos; el lote junta los seguidos con el mismo estado. Los pares van con
    // int: armarlos desde los literales con std::uint16_t haría la conversión adentro de std::pair, y
    // MSVC la avisa (C4242).
    constexpr std::size_t PerSprite = 6;
    std::vector<std::pair<int, int>> runs;
    for (const auto& command : models.back().modelCommands)
    {
        ASSERT_TRUE(command.state.texture.has_value());
        EXPECT_EQ(command.state.blend, Client::Render::ModelBlend::Additive);
        runs.emplace_back(command.state.texture->value, static_cast<int>(command.vertexCount / PerSprite));
    }
    // Después de los del personaje, los dos del farol (0x602ed0, CL7.6 B4), en el dibujo de los objetos.
    const std::vector<std::pair<int, int>> expected{{0x5ac, 30}, {0x5fd, 2}, {0x5ac, 1}, {0x5fd, 2}, {0x5ac, 2}};
    ASSERT_GE(runs.size(), expected.size());
    const std::vector<std::pair<int, int>> sprites(runs.begin(), runs.begin() + 5);
    EXPECT_EQ(sprites, expected);
    EXPECT_TRUE(std::all_of(runs.begin() + 5, runs.end(), [](const auto& run) { return run.first == 0x5f2; }))
        << "después, solo los alientos";
    // 0x549510 los marca vistos antes del pase: 0x549290 los mide con el alpha en 1. El primer destello
    // (la columna 0, escala 0,4, sin giro) va a 0,4 del ancho de flare01, en coordenadas de ojo.
    const auto flareSize = m_models.TextureSize(TextureSlot{0x5ac});
    ASSERT_TRUE(flareSize.has_value());
    const auto& vertices = models.back().modelVertices;
    ASSERT_GE(vertices.size(), PerSprite);
    const auto [left, right] =
        std::minmax_element(vertices.begin(), vertices.begin() + PerSprite,
                            [](const auto& a, const auto& b) { return a.position.x < b.position.x; });
    // Las esquinas van lejos del ojo: la resta pierde los últimos bits (sin 0x549510 daría 0,36).
    EXPECT_NEAR(right->position.x - left->position.x, 0.4f * flareSize->x, 0.01f);
    ASSERT_TRUE(models.back().pass.has_value());
    EXPECT_EQ(models.back().pass->view, glm::mat4(1.0f));
    scene.Leave();
}

// CL7.6 B2: el aliento del caballo (0x444010) sale del hueso 27 del buffer global de la pose. Lo escribe
// el último modelo posado que tiene ese hueso y no es dummy (0x4eb973 saltea los dummy): el dragón se dibuja
// después del caballo (0x52d620), pero su hueso 27 es dummy, y los objetos del mapa 55 tienen menos de 28.
// Queda el del caballo, Bone03, con la pose del dibujo anterior: el avance del caballo (0x4ec200, en
// 0x445039) corre después del aliento. El test repite los pasos del caballo y espera cada aliento nuevo en
// (50; −4; 0) del hueso 27 de la pose de antes de su paso, más la subida de su primer paso (0,2); antes
// de que el caballo se vea, en (0, 0, 0).
TEST_F(GoldenLoginWorld, TheBreathOfTheHorseComesFromItsHeadAsDrawnBefore)
{
    Client::Render::Models::ClothWorld cloth;
    LoginScene scene(SceneContext{.dataRoot = m_dataRoot, .textures = m_store, .log = m_log, .cloth = &cloth}, *m_data,
                     m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    const Client::Render::Models::Skeleton* horseSkeleton = m_models.SkeletonOf(Client::Scenes::DarkHorseModel);
    ASSERT_NE(horseSkeleton, nullptr);
    const glm::dvec3 rise{0.0, 0.0, 0.2};
    Client::Scenes::DarkHorse horse;
    std::size_t breaths = 0;
    std::size_t matched = 0;
    std::size_t previous = 0;
    for (int step = 0; step < 60; ++step)
    {
        RecordingFrameTarget before;
        scene.Render(before.BeginFrame(std::chrono::milliseconds{40 * step}), 0.0f);
        before.Present();
        // Lo que va a leer el aliento de este paso: el caballo como se dibujó recién. Con el alpha por debajo
        // de 0,01 (antes de su primer paso) no se dibujó ni escribió el hueso: el aliento nace en (0, 0, 0).
        const bool horseDrawn = horse.alpha >= 0.01f;
        const auto expected = Client::Scenes::PosedBonePoint(
            m_models,
            Client::Scenes::DrawnDarkHorse(horse, horseSkeleton, Client::Scenes::LoginCharacterTransform(),
                                           Client::Scenes::DarkHorseLoginScale, 0.0f),
            27, {50.0f, -4.0f, 0.0f});
        ASSERT_TRUE(expected.has_value());
        scene.Step(Client::Input::InputState{});
        Client::Scenes::StepDarkHorse(horse, *horseSkeleton, Client::Scenes::LoginCharacterStandingAction);

        RecordingFrameTarget after;
        scene.Render(after.BeginFrame(std::chrono::milliseconds{40 * step + 20}), 0.0f);
        after.Present();
        std::vector<FrameEvent> models;
        std::ranges::copy_if(after.frames.back().events, std::back_inserter(models),
                             [](const FrameEvent& event) { return event.kind == Kind::Models; });
        std::vector<glm::dvec3> centers;
        if (models.size() == 2 && models.front().pass.has_value())
        {
            const glm::dmat4 toWorld = glm::inverse(glm::dmat4(models.front().pass->view));
            for (const auto& command : models.back().modelCommands)
            {
                if (command.state.texture != TextureSlot{0x5f2})
                {
                    continue;
                }
                for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
                {
                    glm::dvec3 center(0.0);
                    for (std::size_t k = 0; k < 6; ++k)
                    {
                        center +=
                            glm::dvec3(models.back().modelVertices[command.firstVertex + (quad * 6) + k].position);
                    }
                    centers.push_back(glm::dvec3(toWorld * glm::dvec4(center / 6.0, 1.0)));
                }
            }
        }
        // Uno más que en el paso anterior (y ninguno se apaga antes de 16): nació en este.
        if (centers.size() > previous)
        {
            ++breaths;
            const glm::dvec3 want = (horseDrawn ? glm::dvec3(*expected) : glm::dvec3(0.0)) + rise;
            if (std::ranges::any_of(centers, [&want](const glm::dvec3& c) { return glm::length(c - want) < 0.05; }))
            {
                ++matched;
            }
        }
        previous = centers.size();
        if (step >= 14)
        {
            break;
        }
    }
    ASSERT_GE(breaths, 2u) << "en 15 pasos, rand() % 3 da 0 más de una vez";
    EXPECT_EQ(matched, breaths) << "cada aliento, en la cabeza del caballo del dibujo anterior";
    scene.Leave();
}

// CL7.6 B4: el farol del castillo (0x602ed0, caso 0x3c en la escena 2) pide dos sprites de flare01 en
// las traslaciones de los huesos 8 y 9 del buffer global de la pose, pero Object61.bmd tiene un solo
// hueso (Q-C140). Ningún objeto del .obj llega al hueso 8, pero antes que ellos la carga del mapa 55
// crea el dueño del dragón (0x6188ae: 0x606660(0xb4, (5.600; 23.000; 0), (0, 0, 0), 1)), en el sector
// (3, 14), antes que el farol (5, 14): no se dibuja (+0x58 en −2), pero 0x5ffdf0 lo posa con Dragon.bmd,
// y sus huesos 8 y 9 son Bip01 L Toe0 y Bip01 L Toe01. El brillo sale ahí, no en el caballo, con la luz
// (L; 0,5 · L; 0,2 · L) y L entre 0,7 y 0,99.
TEST_F(GoldenLoginWorld, TheLanternGlowsAtTheToesOfTheDragonsOwner)
{
    Client::Scenes::WorldModels world;
    ASSERT_TRUE(world.Load(m_dataRoot, 0x37, m_store, m_log, nullptr).HasValue());
    const Client::Render::Models::Skeleton* dragon = world.SkeletonOf(Client::Scenes::DragonModel);
    ASSERT_NE(dragon, nullptr);
    Client::Data::Terrain::TerrainObject record;
    record.type = static_cast<std::int16_t>(Client::Scenes::DragonModel);
    record.position = {5600.0f, 23000.0f, 0.0f};
    record.scale = 1.0f;
    Client::World::WorldObject owner =
        Client::World::CreateWorldObjects(std::span(&record, 1), 0x37, Client::World::ObjectScene::Login).front();
    const Client::Render::Models::Skeleton* horseSkeleton = m_models.SkeletonOf(Client::Scenes::DarkHorseModel);
    ASSERT_NE(horseSkeleton, nullptr);
    Client::Scenes::DarkHorse horse;

    Client::Render::Models::ClothWorld cloth;
    LoginScene scene(SceneContext{.dataRoot = m_dataRoot, .textures = m_store, .log = m_log, .cloth = &cloth}, *m_data,
                     m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    for (int step = 0; step < 3; ++step)
    {
        scene.Step(Client::Input::InputState{});
        // 0x606560 avanza solo los que se vieron en el último dibujo.
        if (owner.seen)
        {
            Client::World::StepWorldObject(owner, 0x37, dragon);
        }
        Client::Scenes::StepDarkHorse(horse, *horseSkeleton, Client::Scenes::LoginCharacterStandingAction);
        RecordingFrameTarget target;
        scene.Render(target.BeginFrame(std::chrono::milliseconds{40 * (step + 1)}), 0.0f);
        target.Present();
        owner.seen = true;
        std::vector<FrameEvent> models;
        std::ranges::copy_if(target.frames.back().events, std::back_inserter(models),
                             [](const FrameEvent& event) { return event.kind == Kind::Models; });
        ASSERT_EQ(models.size(), 2u) << "el mundo y los sprites";
        ASSERT_TRUE(models.front().pass.has_value());
        const glm::dmat4 toWorld = glm::inverse(glm::dmat4(models.front().pass->view));
        // Los sprites del farol: los de flare01 que siguen al último del cetro (0x5fd).
        const auto& commands = models.back().modelCommands;
        const auto lastOfTheScepter = std::find_if(commands.rbegin(), commands.rend(), [](const auto& command)
                                                   { return command.state.texture == TextureSlot{0x5fd}; });
        ASSERT_NE(lastOfTheScepter, commands.rend());
        std::vector<glm::dvec3> centers;
        std::vector<Client::Render::ModelVertex> vertices;
        for (auto it = lastOfTheScepter.base(); it != commands.end(); ++it)
        {
            const auto& command = *it;
            if (command.state.texture != TextureSlot{0x5ac})
            {
                continue;
            }
            for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
            {
                glm::dvec3 center(0.0);
                for (std::size_t k = 0; k < 6; ++k)
                {
                    const auto& vertex = models.back().modelVertices[command.firstVertex + (quad * 6) + k];
                    center += glm::dvec3(vertex.position);
                    vertices.push_back(vertex);
                }
                centers.push_back(glm::dvec3(toWorld * glm::dvec4(center / 6.0, 1.0)));
            }
        }
        ASSERT_EQ(centers.size(), 2u) << "el paso " << step;
        Client::Scenes::DrawnObject drawn;
        drawn.model = Client::Scenes::DragonModel;
        drawn.animation = owner.animation;
        drawn.transform = owner.transform;
        const Client::Scenes::DrawnObject drawnHorse = Client::Scenes::DrawnDarkHorse(
            horse, horseSkeleton, Client::Scenes::LoginCharacterTransform(), Client::Scenes::DarkHorseLoginScale, 0.0f);
        for (std::size_t bone = 8; bone <= 9; ++bone)
        {
            const auto expected = Client::Scenes::PosedBonePoint(world, drawn, bone, {});
            ASSERT_TRUE(expected.has_value());
            EXPECT_LT(glm::length(centers[bone - 8] - glm::dvec3(*expected)), 0.05)
                << "el paso " << step << ", el hueso " << bone;
            const auto onTheHorse = Client::Scenes::PosedBonePoint(m_models, drawnHorse, bone, {});
            ASSERT_TRUE(onTheHorse.has_value());
            EXPECT_GT(glm::length(centers[bone - 8] - glm::dvec3(*onTheHorse)), 1000.0) << "no en el caballo";
        }
        for (const auto& vertex : vertices)
        {
            EXPECT_GE(vertex.color.red, 0.7f);
            EXPECT_LE(vertex.color.red, 0.99f);
            EXPECT_FLOAT_EQ(vertex.color.green, vertex.color.red * 0.5f);
            EXPECT_NEAR(vertex.color.blue, vertex.color.red * 0.2f, 1e-6f);
        }
    }
    scene.Leave();
}

// CL7.6 B4: 0x602ed0 saca un rand() de la CRT de la tela por cada objeto visto en el dibujo (0x602ef4):
// los 276 del .obj, el dueño del dragón y el logo, todos vistos en el login. Con la tela y el cetro, que
// sacan menos de 100, un dibujo corre el rand() 278 lugares o más.
TEST_F(GoldenLoginWorld, EverySeenObjectTakesARandInTheDraw)
{
    Client::Render::Models::ClothWorld cloth;
    LoginScene scene(SceneContext{.dataRoot = m_dataRoot, .textures = m_store, .log = m_log, .cloth = &cloth}, *m_data,
                     m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    scene.Step(Client::Input::InputState{});
    {
        RecordingFrameTarget target;
        scene.Render(target.BeginFrame(std::chrono::milliseconds{40}), 0.0f);
        target.Present();
    }
    scene.Step(Client::Input::InputState{});
    Client::Render::Models::CrtRandom walk = cloth.random;
    RecordingFrameTarget target;
    scene.Render(target.BeginFrame(std::chrono::milliseconds{80}), 0.0f);
    target.Present();
    std::size_t taken = 0;
    while (walk.State() != cloth.random.State() && taken < 1000)
    {
        (void)walk.Next();
        ++taken;
    }
    EXPECT_GE(taken, 278u);
    EXPECT_LT(taken, 378u);
    scene.Leave();
}

// El centro de cada brasa del último frame, en el orden de su dibujo (el de sus lugares): las esquinas
// 0 y 2 de su cuadrado son opuestas.
std::vector<glm::vec3> EmberCentersOf(const RecordingFrameTarget& target)
{
    std::vector<glm::vec3> centers;
    const auto world = std::ranges::find(target.frames.back().events, Kind::Models, &FrameEvent::kind);
    if (world == target.frames.back().events.end())
    {
        return centers;
    }
    for (const auto& command : world->modelCommands)
    {
        if (command.state.texture != TextureSlot{0x635})
        {
            continue;
        }
        EXPECT_EQ(command.state.blend, Client::Render::ModelBlend::Additive);
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const std::size_t first = command.firstVertex + (quad * 6);
            centers.push_back((world->modelVertices[first].position + world->modelVertices[first + 2].position) / 2.0f);
        }
    }
    return centers;
}

// CL7.6 B3: las 80 brasas (0x530490 → 0x42b1e0) nacen alrededor del héroe, el personaje 0, al que 0x6350b0
// le escribe la posición en el dibujo: en el primer paso, antes de dibujar, nacen alrededor de (0, 0, 0)
// (Q-C137); después de un dibujo, alrededor de (11.679; 14.191; 333), en la caja de 0x42b1e0. Van en el
// pase del mundo, aditivas, con FireSnuff (0x635), después del dragón.
TEST_F(GoldenLoginWorld, TheEmbersAreBornAroundTheHero)
{
    Client::Render::Models::ClothWorld cloth;
    {
        LoginScene scene(SceneContext{.dataRoot = m_dataRoot, .textures = m_store, .log = m_log, .cloth = &cloth},
                         *m_data, m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
        ASSERT_TRUE(scene.Enter().HasValue());
        scene.Step(Client::Input::InputState{});
        RecordingFrameTarget target;
        scene.Render(target.BeginFrame(std::chrono::milliseconds{40}), 0.0f);
        target.Present();
        const std::vector<glm::vec3> first = EmberCentersOf(target);
        ASSERT_EQ(first.size(), 80u);
        EXPECT_TRUE(std::ranges::all_of(first, [](const glm::vec3& c) { return glm::length(c) < 2000.0f; }))
            << "antes del primer dibujo, el héroe en (0, 0, 0)";
        scene.Leave();
    }
    LoginScene scene(SceneContext{.dataRoot = m_dataRoot, .textures = m_store, .log = m_log, .cloth = &cloth}, *m_data,
                     m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    RecordingFrameTarget before;
    scene.Render(before.BeginFrame(std::chrono::milliseconds{0}), 0.0f);
    before.Present();
    scene.Step(Client::Input::InputState{});
    RecordingFrameTarget after;
    scene.Render(after.BeginFrame(std::chrono::milliseconds{40}), 0.0f);
    after.Present();
    const std::vector<glm::vec3> born = EmberCentersOf(after);
    ASSERT_EQ(born.size(), 80u);
    const glm::vec3 hero = Client::Scenes::LoginHeroPosition;
    for (const glm::vec3& c : born)
    {
        EXPECT_GE(c.x - hero.x, -800.5f);
        EXPECT_LE(c.x - hero.x, 799.5f);
        EXPECT_GE(c.y - hero.y, -500.5f);
        EXPECT_LE(c.y - hero.y, 899.5f);
        EXPECT_GE(c.z - hero.z, 49.5f);
        EXPECT_LE(c.z - hero.z, 1049.5f);
    }
    scene.Leave();
}

// CL7.6 B3: al entrar, 0x6349a0 deja la cámara en (0, 0, 0) (0x634b80) y 0x635ae0 le escribe 12.678 de y
// recién en el dibujo (0x635f7e). Las brasas del primer paso nacen con el héroe en (0, 0, 0) y la y de
// 0x42b1e0 entre −500 y 899: las que quedan debajo de la cámara + 400 van hacia +x (3,2 menos la caída) y
// las de 400 o más, hacia −x. Con 12.678, irían todas hacia +x. Las que tocan el suelo no se mueven.
TEST_F(GoldenLoginWorld, TheFirstEmbersSeeTheCameraAtTheOrigin)
{
    Client::Render::Models::ClothWorld cloth;
    LoginScene scene(SceneContext{.dataRoot = m_dataRoot, .textures = m_store, .log = m_log, .cloth = &cloth}, *m_data,
                     m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    scene.Step(Client::Input::InputState{});
    RecordingFrameTarget born;
    scene.Render(born.BeginFrame(std::chrono::milliseconds{40}), 0.0f);
    born.Present();
    scene.Step(Client::Input::InputState{});
    RecordingFrameTarget moved;
    scene.Render(moved.BeginFrame(std::chrono::milliseconds{80}), 0.0f);
    moved.Present();
    const std::vector<glm::vec3> before = EmberCentersOf(born);
    const std::vector<glm::vec3> after = EmberCentersOf(moved);
    ASSERT_EQ(before.size(), 80u);
    ASSERT_EQ(after.size(), 80u);
    std::size_t backwards = 0;
    std::size_t forwards = 0;
    for (std::size_t k = 0; k < before.size(); ++k)
    {
        const float dx = after[k].x - before[k].x;
        if (dx == 0.0f)
        {
            continue;
        }
        if (before[k].y >= 399.5f)
        {
            EXPECT_LT(dx, 0.0f) << "la brasa " << k << " nació en y " << before[k].y;
            ++backwards;
        }
        else
        {
            EXPECT_GT(dx, 0.0f) << "la brasa " << k << " nació en y " << before[k].y;
            ++forwards;
        }
    }
    EXPECT_GT(backwards, 0u);
    EXPECT_GT(forwards, 0u);
    scene.Leave();
}

// El dragón avanza por paso (0x52d220, 25 por segundo), no por frame: varios frames sin pasos no lo
// mueven, a cualquier FPS.
TEST_F(GoldenLoginWorld, TheDragonMovesOnlyWithTheSteps)
{
    LoginScene scene(SceneContext{m_dataRoot, m_store, m_log}, *m_data, m_models, nullptr, Version, FixedSeed,
                     &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    const auto dragon = [&scene](std::chrono::milliseconds clock)
    {
        RecordingFrameTarget target;
        scene.Render(target.BeginFrame(clock), 0.0f);
        target.Present();
        std::vector<glm::vec3> positions;
        const auto drawn = std::ranges::find(target.frames.back().events, Kind::Models, &FrameEvent::kind);
        if (drawn != target.frames.back().events.end())
        {
            for (const auto& command : drawn->modelCommands)
            {
                if (IsDragon(command))
                {
                    for (std::size_t k = 0; k < command.vertexCount; ++k)
                    {
                        positions.push_back(drawn->modelVertices[command.firstVertex + k].position);
                    }
                }
            }
        }
        return positions;
    };
    scene.Step(Client::Input::InputState{});
    const auto first = dragon(std::chrono::milliseconds{40});
    ASSERT_FALSE(first.empty());
    for (int frame = 1; frame <= 5; ++frame)
    {
        EXPECT_EQ(dragon(std::chrono::milliseconds{40 + (7 * frame)}), first) << frame;
    }
    scene.Step(Client::Input::InputState{});
    EXPECT_NE(dragon(std::chrono::milliseconds{80}), first);
    scene.Leave();
}

// Cada entrada vacía la tabla de efectos (0x6070a0) y la carga del mapa crea el dragón otra vez
// (0x606660 → 0x518cf0): el primer frame de la segunda entrada lo tiene donde la primera, aunque en el
// medio voló.
TEST_F(GoldenLoginWorld, EnteringAgainStartsTheDragonAgain)
{
    LoginScene scene(SceneContext{m_dataRoot, m_store, m_log}, *m_data, m_models, nullptr, Version, FixedSeed,
                     &m_models.TextureSlots());
    const auto dragonVertices = [](const FrameEvent& frame)
    {
        std::vector<glm::vec3> positions;
        for (const auto& command : frame.modelCommands)
        {
            if (IsDragon(command))
            {
                for (std::size_t k = 0; k < command.vertexCount; ++k)
                {
                    positions.push_back(frame.modelVertices[command.firstVertex + k].position);
                }
            }
        }
        return positions;
    };
    ASSERT_TRUE(scene.Enter().HasValue());
    const auto first = dragonVertices(Frame(scene, 0));
    ASSERT_FALSE(first.empty());
    const auto later = dragonVertices(Frame(scene, 30));
    EXPECT_NE(first, later) << "el dragón vuela";
    scene.Leave();
    ASSERT_TRUE(scene.Enter().HasValue());
    EXPECT_EQ(dragonVertices(Frame(scene, 0)), first);
    scene.Leave();
}

// Los textos de los avisos de la selección que vienen vacíos en el Text.bmd del laboratorio.
constexpr std::array<std::size_t, 6> SelectionEmptyTexts{1711, 1712, 1713, 1714, 1715, 1716};

// Los textos de las ventanas (CL5.5), por índice: los de cada aviso que abre el login (los del F1
// 01 de 0x15 a 0x26 y los suyos), los rótulos de la cuenta (450 y 451), los dos títulos, las tres
// cargas y el nombre de cada grupo con botón. Todos están y no son vacíos; si uno falla, sale su
// índice y no el texto.
TEST_F(GoldenLoginScene, TheTextsOfItsWindowsAreInTheLabTable)
{
    auto data = LoadGameData(m_dataRoot);
    ASSERT_TRUE(data.HasValue()) << data.Error();
    ASSERT_TRUE(data.Value().text.has_value());
    std::set<std::size_t> indices{450,         451,           LoginTitleText(4, 1), LoginTitleText(5, 1),
                                  LoadText(0), LoadText(100), LoadText(128)};
    for (int notice = 0x15; notice <= 0x70; ++notice)
    {
        if (const std::optional<NoticeTexts> texts = TextsOf(static_cast<NoticeId>(notice)))
        {
            indices.insert(texts->first);
            if (texts->second)
            {
                indices.insert(*texts->second);
            }
        }
    }
    for (std::uint16_t group = 0; group < ServerGroupCount; ++group)
    {
        if (const std::optional<int> slot = GroupSlot(group); slot && *slot >= FirstButtonSlot)
        {
            indices.insert(GroupNameText(group));
        }
    }

    // Los de los avisos de la selección que este Text.bmd deja vacíos (TheSelectionLeavesSixTextsEmpty).
    for (const std::size_t empty : SelectionEmptyTexts)
    {
        indices.erase(empty);
    }

    for (const std::size_t index : indices)
    {
        const std::optional<std::string_view> text = data.Value().text->Text(index);
        EXPECT_TRUE(text.has_value() && !text->empty()) << "el texto " << index;
    }
}

// Los textos 1711 a 1716 de los avisos de la selección (0x83, 0x5e, 0x5f, 0x60, 0x72 y 0x36) están
// vacíos en el Text.bmd del laboratorio: esas cajas salen sin texto, como en el original.
TEST_F(GoldenLoginScene, TheSelectionLeavesSixTextsEmpty)
{
    auto data = LoadGameData(m_dataRoot);
    ASSERT_TRUE(data.HasValue()) << data.Error();
    ASSERT_TRUE(data.Value().text.has_value());
    for (const std::size_t index : SelectionEmptyTexts)
    {
        const std::optional<std::string_view> text = data.Value().text->Text(index);
        ASSERT_TRUE(text.has_value()) << "el texto " << index;
        EXPECT_TRUE(text->empty()) << "el texto " << index;
    }
}

TEST_F(GoldenLoginScene, TheLoadingBeforeItEntersToo)
{
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    LoadingScene scene(SceneContext{m_dataRoot, store, log});

    const auto entered = scene.Enter();

    ASSERT_TRUE(entered.HasValue()) << entered.Error();
    EXPECT_EQ(store.loaded, (std::vector<std::uint16_t>{0x71e, 0x71f, 0x720, 0x721, 0x722, 0x723}));
    scene.Leave();
    EXPECT_EQ(store.released.size(), LoadingTextureSet.size());
}
} // namespace
