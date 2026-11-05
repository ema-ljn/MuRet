#include "Scenes/LoginScene.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Render/TextureSlot.h"
#include "Scenes/GameData.h"
#include "Scenes/GameModels.h"
#include "Scenes/LoadingScene.h"
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
#include <optional>
#include <set>
#include <string>
#include <string_view>
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
    // Y las dos del logo (TheLogoLoads) y las 15 del terreno: los 14 tiles de World56 y el 0x122
    // (TheTerrainGoesBeforeTheCharacter).
    ASSERT_EQ(store.Size(), LoginTextureSet.size() + WindowTextureSet.size() + 2 + 15);
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

// Q-C92: Logo03.bmd tiene dos mallas, logo.jpg y logo2.jpg, que 0x60d5f0 sube al 0x455 y al 0x456
// con GL_LINEAR; al salir, se sueltan. El logo se dibuja: un tramo opaco y el aditivo de su
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

    for (const std::uint16_t slot : {std::uint16_t{0x455}, std::uint16_t{0x456}})
    {
        const auto texture = store.Find(TextureSlot{slot});
        ASSERT_TRUE(texture.has_value()) << std::hex << slot;
        EXPECT_EQ(texture->sampling.filter, Client::Data::Textures::Filter::Linear) << std::hex << slot;
        EXPECT_EQ(texture->sampling.wrap, Client::Data::Textures::Wrap::Repeat) << std::hex << slot;
    }
    EXPECT_FALSE(store.Find(TextureSlot{0x457}).has_value());

    scene.Step(Client::Input::InputState{});
    RecordingFrameTarget target;
    scene.Render(target.BeginFrame(std::chrono::milliseconds{40}), 0.0f);
    target.Present();
    const auto drawn = std::ranges::find(target.frames.back().events, Kind::Models, &FrameEvent::kind);
    ASSERT_NE(drawn, target.frames.back().events.end());
    // Los dos últimos tramos: antes va el terreno (TheTerrainGoesBeforeTheCharacter).
    const auto& commands = drawn->modelCommands;
    ASSERT_GE(commands.size(), 2u);
    EXPECT_EQ(commands[commands.size() - 2].state.texture, TextureSlot{0x455});
    EXPECT_EQ(commands[commands.size() - 2].state.blend, Client::Render::ModelBlend::Opaque);
    EXPECT_EQ(commands.back().state.texture, TextureSlot{0x456});
    EXPECT_EQ(commands.back().state.blend, Client::Render::ModelBlend::Additive);

    scene.Leave();
    EXPECT_FALSE(store.Find(TextureSlot{0x455}).has_value());
    EXPECT_FALSE(store.Find(TextureSlot{0x456}).has_value());
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
