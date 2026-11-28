#include "support/CheckedWorldObjectProjection.h"
#include "Kernel/HelperTable.h"

#include "Crt/CrtRandom.h"
#include "Scenes/LoginScene.h"
#include "Scenes/CharacterSelectScene.h"
#include "FrontEnd/CharacterSlots.h"
#include "FrontEnd/SelectionState.h"
#include "Kernel/WorldState.h"
#include "Session/CharacterEvent.h"
#include "Protocol/FrontEnd/CharacterList.h"
#include "Data/DataPath.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Camera/GameCamera.h"
#include "Terrain/TerrainHeight.h"
#include "support/SceneDataFiles.h"
#include "support/TerrainFiles.h"
#include "support/TemporaryFolder.h"
#include "support/SessionHarness.h"
#include "support/FakeSocket.h"
#include "support/SessionPackets.h"

#include "Kernel/PacingMode.h"
#include "Session/ServerSession.h"
#include "Session/SessionState.h"
#include "Core/IClock.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Models/Animation.h"
#include "Models/Cloth.h"
#include "Math/OriginalAngles.h"
#include "Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Kernel/CharacterHead.h"
#include "Pets/DarkHorse.h"
#include "Tables/GameData.h"
#include "Kernel/DrawTailState.h"
#include "Assets/GameModels.h"
#include "Scenes/LoadingScene.h"
#include "Camera/SceneCameras.h"
#include "Scenes/SceneContext.h"
#include "Assets/TextureSets.h"
#include "Ui/Notices.h"
#include "FrontEnd/ServerListModel.h"
#include "support/FakeClock.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/PlayerActors.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include "support/RecordingTextDrawer.h"
#include "FrontEnd/LoginModels.h"
#include "support/FixedRandom.h"
#include "Render/ModelBatch.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "Input/InputState.h"
#include "Data/Terrain/TerrainObjects.h"
#include "Actors/Drawing/ScepterGlow.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Terrain/TerrainLighting.h"
#include "Terrain/DynamicWorldLight.h"
#include "Terrain/TerrainState.h"
#include "Session/ModuleStates.h"
#include <memory>
#include "WorldObjects/WorldObjects.h"
#include "Assets/WorldModels.h"

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
#include <filesystem>
#include <system_error>
#include <ios>
#include <iterator>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The loading and the login with the lab's Data\: they enter (their texture sets in their slots of the fake table,
// the login with the windows' ones), and the login draws its three Text.bmd texts. No text goes to the output: the
// length and where the version ended up are checked, never what they say.
namespace
{
using Client::FrontEnd::SingleModelLibrary;
using Client::Tests::FixedSeed;
using Client::Tests::FrameEvent;
using Kind = Client::Tests::FrameEvent::Kind;
using Client::Assets::LoadingTextureSet;
using Client::Assets::LoginTextureSet;
using Client::Assets::WindowTextureSet;
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::FrontEnd::FirstButtonSlot;
using Client::FrontEnd::GroupNameText;
using Client::FrontEnd::GroupSlot;
using Client::FrontEnd::LoadText;
using Client::FrontEnd::LoginTitleText;
using Client::FrontEnd::ServerGroupCount;
using Client::Render::TextureSlot;
using Client::Scenes::LoadingScene;
using Client::Scenes::LoginScene;
using Client::Scenes::SceneContext;
using Client::Tables::LoadGameData;
using Client::Tests::FakeTextureStore;
using Client::Tests::RecordedText;
using Client::Tests::RecordingFrameTarget;
using Client::Tests::RecordingSink;
using Client::Ui::NoticeId;
using Client::Ui::NoticeTexts;
using Client::Ui::TextsOf;

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
    // And the logo's two (TheLogoLoads), the terrain's 15 (World56's 14 tiles and 0x122,
    // TheTerrainGoesBeforeTheCharacter) and the world's 63: the dragon's 3 and 62 of Object56\ that this empty table
    // lacks, minus fire01 and fire02, which are missing (GoldenWorldModels).
    constexpr std::array weatherSlots{TextureSlot{0x64}, TextureSlot{0x65}, TextureSlot{0x66}, TextureSlot{0x67},
                                      TextureSlot{0x68}};
    // 0x61ac3e..0x61ad7a: two world leaves and three global rain images; 0x616eeb: the drop (0x607).
    // 0x618dda..0x618e29: the 32 caustics are uploaded on map 55 too, before the tiles.
    ASSERT_EQ(store.Size(),
              LoginTextureSet.size() + WindowTextureSet.size() + 2 + 15 + 1 + 32 + weatherSlots.size() + 63);
    for (const auto slot : weatherSlots)
        EXPECT_TRUE(store.Find(slot).has_value()) << slot.value;
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

    // The three are in the lab's Text.bmd, and 456 has a "%s" for the version.
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

// Q-C92: Logo03.bmd has two meshes, logo.jpg and logo2.jpg, which 0x60d5f0 uploads to the two slots after the world
// (0x455 and 0x456 in the original; here, without the loading's table, 0x45a and 0x45b) with GL_LINEAR; on leaving,
// they are released. The logo is drawn: an opaque stretch and the additive one of its blendMesh, with the first
// step's light.
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

    // Without the loading's table, the world leaves the next one at 0x45a (GoldenWorldModels).
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
    // Its two stretches in a row, among the world objects' (TheLogoIsAWorldObject): the opaque one with logo.jpg and
    // the additive one of its blendMesh with logo2.jpg.
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

// CL7.1: the login loads World56's terrain and draws it first in the 3D pass (0x5fc260(0) before 0x510d00): the
// first stretches are the tiles' (0x23..0x30) and 0x122's, and in the login, with the lake at the back, there is
// water (TileWater01, 0x28). On leaving, its 15 slots are released.
TEST_F(GoldenLoginScene, TheTerrainGoesBeforeTheCharacter)
{
    auto data = LoadGameData(m_dataRoot);
    ASSERT_TRUE(data.HasValue()) << data.Error();
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    // With the loading's models, so the character is drawn.
    Client::Assets::GameModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, store, log).HasValue());
    LoginScene scene(SceneContext{m_dataRoot, store, log}, data.Value(), models, nullptr, Version, FixedSeed);
    ASSERT_TRUE(scene.Enter().HasValue());
    for (std::uint16_t slot = 0x23; slot <= 0x30; ++slot)
    {
        EXPECT_TRUE(store.Find(TextureSlot{slot}).has_value()) << std::hex << slot;
    }
    EXPECT_TRUE(store.Find(TextureSlot{0x122}).has_value());

    // 0x508f00 writes +0x16c before the first frame the original allows drawing.
    scene.Step(Client::Input::InputState{});
    ASSERT_FALSE(scene.Failure().has_value());
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
    // And the character is there, after: its parts go with the Player\ textures (from 0x138, 0x60f7d0) or the skin's
    // (0x12e..0x137).
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

// CL7.2: with the loading's models (GameModels and its slot table, as in main.cpp), the textures of Object56\'s
// objects go from 0x41c. The five names already global leave 57 new ones and the logo stays at 0x455, inferred from
// the ASM; GoldenWorldModels checks the reuse of those names.
constexpr std::uint16_t WorldFirstSlot = 0x41c;
constexpr std::uint16_t LogoFirstSlot = 0x455;

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

    // Real world with synthetic 0x40 attributes; the scene keeps going through its real consumers.
    void MakeFixedHeightWorld(const std::filesystem::path& copyRoot)
    {
        std::error_code error;
        const auto world = Client::Data::ResolveDataPath(m_dataRoot, "World56", error);
        ASSERT_TRUE(world.has_value());
        std::filesystem::copy(*world, copyRoot / "World56", std::filesystem::copy_options::recursive);
        const auto accept = DataFile("Interface\\accept_box01.OZJ");
        ASSERT_TRUE(accept.has_value());
        std::filesystem::create_directories(copyRoot / "Interface");
        std::filesystem::copy_file(*accept, copyRoot / "Interface" / accept->filename());
        // 0x616eeb: the drop that 0x616ec0 uploads on every map.
        const auto drop = DataFile("Object8\\drop01.OZJ");
        ASSERT_TRUE(drop.has_value());
        std::filesystem::create_directories(copyRoot / "Object8");
        std::filesystem::copy_file(*drop, copyRoot / "Object8" / drop->filename());
        Client::Tests::WriteTextureSet(copyRoot, Client::Assets::LoginTextureSet);
        Client::Tests::WriteTextureSet(copyRoot, Client::Assets::WindowTextureSet);
        const auto attributes = Client::Data::ResolveDataPath(copyRoot, "World56\\EncTerrain56.att", error);
        ASSERT_TRUE(attributes.has_value());
        auto plain = Client::Tests::PlainTerrainAttributes(56, Client::Tests::AttributeCells::Bytes);
        std::fill(plain.begin() + Client::Data::Terrain::TerrainAttributesHeaderSize, plain.end(), std::byte{0x40});
        Client::Tests::WriteSceneFile(copyRoot, std::filesystem::relative(*attributes, copyRoot),
                                      Client::Tests::EncryptTerrainAttributes(std::move(plain)));
    }

    FakeTextureStore m_store;
    RecordingSink m_sink;
    Log m_log{m_sink, LogLevel::Debug};
    std::optional<Client::Tables::GameData> m_data;
    Client::Assets::GameModels m_models;
};

TEST_F(GoldenLoginWorld, LeavingLoginKeepsTheCommonFireSnuffSlot)
{
    // 0x61ccb0 loads this slot first and 0x61c7c0 repeats the same upload.
    constexpr TextureSlot fireSnuff{0x635};
    EXPECT_EQ(m_models.TextureSlotOf(fireSnuff.value), fireSnuff);
    LoginScene scene(SceneContext{m_dataRoot, m_store, m_log}, *m_data, m_models, nullptr, Version, FixedSeed,
                     &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    ASSERT_TRUE(m_store.Find(fireSnuff).has_value());
    scene.Leave();
    EXPECT_TRUE(m_store.Find(fireSnuff).has_value());
    EXPECT_FALSE(m_store.Find(TextureSlot{0x15}).has_value());
}

void DrawAt(LoginScene& scene, Client::Core::Duration clock)
{
    RecordingFrameTarget target;
    scene.Render(target.BeginFrame(clock), 0.0f);
    target.Present();
}

// 0x6046a0 after 0x4453b0.
TEST_F(GoldenLoginWorld, TheObjectsGoAfterTheHorse)
{
    LoginScene scene(SceneContext{m_dataRoot, m_store, m_log}, *m_data, m_models, nullptr, Version, FixedSeed,
                     &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    const FrameEvent frame = Frame(scene, 3);
    const auto horse = m_models.MeshTextures(Client::Models::ModelId{Client::Pets::DarkHorseModel});
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

// The logo is one more of the list (0xa2 in sector (6, 9)): its two textures, with GL_LINEAR, go to the next slot
// the world left, and it is drawn among the objects of the sectors before and after.
TEST_F(GoldenLoginWorld, TheLogoIsAWorldObject)
{
    LoginScene scene(SceneContext{m_dataRoot, m_store, m_log}, *m_data, m_models, nullptr, Version, FixedSeed,
                     &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    constexpr std::array<std::string_view, 2> paths{"Logo\\logo.jpg", "Logo\\logo2.jpg"};
    for (std::size_t index = 0; index < paths.size(); ++index)
    {
        const std::uint16_t slot = static_cast<std::uint16_t>(LogoFirstSlot + index);
        const auto texture = m_store.Find(TextureSlot{slot});
        ASSERT_TRUE(texture.has_value()) << std::hex << slot;
        EXPECT_EQ(texture->sampling.filter, Client::Data::Textures::Filter::Linear) << std::hex << slot;
        EXPECT_EQ(texture->sampling.wrap, Client::Data::Textures::Wrap::Repeat);
        EXPECT_EQ(texture->texture.format, Client::Data::Textures::PixelFormat::Rgb);
        const std::array<Client::Data::Textures::TextureSetEntry, 1> entry{{{paths[index], slot, texture->sampling}}};
        const auto original = Client::Data::Textures::LoadTextureSet(m_dataRoot, entry);
        ASSERT_TRUE(original.HasValue());
        EXPECT_EQ(texture->texture.pixels, original.Value().front().texture.pixels);
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

// 0x606560 only advances the objects already drawn: the steps that run before the first draw (the one in the same
// frame that brings the load, or those of the connect wait, Q-C85) do not move them.
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

// The original skips a missing model without warning (Q-C23); MuRet says so once per entry, with the types the .obj
// asks for that have no model: in the lab's Data\, 82 (Object83.bmd).
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

// Each entry loads the world again (0x61a560) and creates the objects again (0x606660): the second's first frame is
// the first's, even though in between the whirlpools turned and the 90 lit up.
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

// The lake water (TileWater01, 0x28) moves: 0x5fc260 shifts its u with the frame clock, (ftol(T) % 20000) · 5e-5,
// and 0x5fbf60 writes the v wave on each step, with the logic clock (06 § El terreno del login). A second later, each
// water corner is 0.05 further in u and the wave changed.
TEST_F(GoldenLoginScene, TheLakeMovesWithTheClock)
{
    auto data = LoadGameData(m_dataRoot);
    ASSERT_TRUE(data.HasValue()) << data.Error();
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    Client::Tests::FakeClock clock;
    Client::Assets::GameModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, store, log).HasValue());
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

// Each entry reads the terrain again (0x6349a0 → 0x61a560): on leaving its slots are released, and on entering again
// they are uploaded again and the first frame draws the same as the first time.
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

// CL7.6 A: the dragon (effect 0xb4, with Dragon.bmd's textures at 0x69..0x6b) is drawn after the world objects and
// the logo (0x52d620 after 0x6046a0), opaque.
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

// Q-C278: a single table survives login → list wait → load 54 → game arrival.
TEST_F(GoldenLoginWorld, LoginUpdatesTheSharedLightBeforeItsDrawPublishesANewRange)
{
    auto world = std::make_unique<Client::Kernel::WorldState>();
    Client::Session::ModuleStates states;
    Client::Tests::FakeClock clock;
    clock.Advance(std::chrono::milliseconds{40});
    const auto base = Client::Terrain::LoadTerrainLighting(m_dataRoot, 55);
    ASSERT_TRUE(base.HasValue()) << base.Error();
    states.terrain.dynamicLight.Lighting().cells[1000][0] = -77.0f;
    LoginScene scene(SceneContext{.dataRoot = m_dataRoot,
                                  .textures = m_store,
                                  .log = m_log,
                                  .clock = &clock,
                                  .world = world.get(),
                                  .states = &states},
                     *m_data, m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    EXPECT_EQ(states.terrain.dynamicLight.Range().lastX, 255) << "cargar no ejecuta 0x5fbb00";
    scene.Step(Client::Input::InputState{});
    ASSERT_FALSE(scene.Failure().has_value());
    EXPECT_EQ(states.terrain.dynamicLight.Lighting().cells[1000][0], base.Value()->cells[1000][0]);
    RecordingFrameTarget target;
    scene.Render(target.BeginFrame(clock.Now()), 0.0f);
    const auto& range = states.terrain.dynamicLight.Range();
    EXPECT_EQ(range.firstX, 0);
    EXPECT_EQ(range.lastX, 128);
    EXPECT_EQ(range.firstY, 120);
    EXPECT_EQ(range.lastY, 251);
    scene.Leave();
}

TEST_F(GoldenLoginWorld, LoginClearsTheSharedParticlePoolWithoutResettingWind)
{
    auto world = std::make_unique<Client::Kernel::WorldState>();
    auto& particles = world->Particles();
    particles.All()[0].alive = true;
    particles.Wind().wind = {3.0f, 4.0f};
    particles.Wind().change = {1.0f, 2.0f};
    LoginScene scene(SceneContext{.dataRoot = m_dataRoot, .textures = m_store, .log = m_log, .world = world.get()},
                     *m_data, m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    EXPECT_FALSE(particles.Any());
    EXPECT_EQ(particles.Wind().wind, glm::vec2(3.0f, 4.0f));
    EXPECT_EQ(particles.Wind().change, glm::vec2(1.0f, 2.0f));
    particles.All()[0].alive = true;
    scene.Leave();
    EXPECT_TRUE(particles.Any());
}

TEST_F(GoldenLoginWorld, ArrivalReusesTheVisibilityLeftByTheLoginDragon)
{
    auto world = std::make_unique<Client::Kernel::WorldState>();
    Client::Terrain::TerrainState terrain;
    Client::FrontEnd::SelectionState selection(*world);
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput input;
    LoginScene scene(SceneContext{.dataRoot = m_dataRoot,
                                  .textures = m_store,
                                  .log = m_log,
                                  .camera = &camera,
                                  .cameraInput = &input,
                                  .selection = &selection,
                                  .world = world.get()},
                     *m_data, m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    auto effects = world->Map().effects.Effects();
    ASSERT_TRUE(effects[0].alive);
    EXPECT_FALSE(effects[0].seen);
    ASSERT_TRUE(effects[0].hasOwner);
    ASSERT_NE(effects[0].ownerContext, nullptr);
    EXPECT_EQ(effects[0].disturbanceState, 0xffff);
    const auto* owner = static_cast<const Client::Kernel::WorldObject*>(effects[0].ownerContext);
    EXPECT_EQ(owner->type, Client::Effects::DragonEffect);
    EXPECT_EQ(owner->transform.position, glm::vec3(5600, 23000, 0));
    static_cast<void>(Frame(scene, 1));
    EXPECT_TRUE(effects[0].seen);
    scene.Leave();
    EXPECT_TRUE(effects[0].alive);
    EXPECT_FALSE(effects[0].hasOwner);
    EXPECT_EQ(effects[0].ownerContext, nullptr);
    selection.BeginSelection();
    ASSERT_TRUE(selection.InitializeSelection(m_dataRoot, terrain, input).HasValue());
    EXPECT_TRUE(effects[0].alive) << "sin lista no corre la carga del mapa54";
    const std::array<Client::App::CharacterEvent, 1> list{
        Client::App::CharacterListReceived{.list = {.characters = {}}}};
    ASSERT_TRUE(selection.Apply(list, camera, input).HasValue());
    ASSERT_TRUE(selection.InitializeSelection(m_dataRoot, terrain, input).HasValue());
    EXPECT_FALSE(effects[0].alive);
    EXPECT_TRUE(effects[0].seen);
    ASSERT_EQ(world->Map().effects.Create(Client::Effects::ArrivalEffect, {}, {}, {}, 0.0f), 0u);
    ASSERT_EQ(world->Map().effects.Create(Client::Effects::ArrivalEffect, {}, {}, {}, 0.0f), 1u);
    EXPECT_TRUE(effects[0].seen);
    EXPECT_FALSE(effects[1].seen);
}

// The world outlives the scene: only the links to its objects are removed, even in an inactive slot.
TEST_F(GoldenLoginWorld, TheLoginDragonOwnerIsDetachedOnReentryAndDestruction)
{
    auto world = std::make_unique<Client::Kernel::WorldState>();
    int unrelatedOwner = 0;
    auto effects = world->Map().effects.Effects();
    {
        LoginScene scene(SceneContext{.dataRoot = m_dataRoot, .textures = m_store, .log = m_log, .world = world.get()},
                         *m_data, m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
        ASSERT_TRUE(scene.Enter().HasValue());
        ASSERT_TRUE(effects[0].hasOwner);
        ASSERT_NE(effects[0].ownerContext, nullptr);
        effects[1].hasOwner = true;
        effects[1].ownerContext = effects[0].ownerContext;
        effects[1].seen = true;
        effects[2].hasOwner = true;
        effects[2].ownerContext = &unrelatedOwner;
        ASSERT_TRUE(scene.Enter().HasValue());
        EXPECT_FALSE(effects[1].hasOwner);
        EXPECT_EQ(effects[1].ownerContext, nullptr);
        EXPECT_TRUE(effects[1].seen);
        ASSERT_TRUE(effects[0].hasOwner);
        ASSERT_NE(effects[0].ownerContext, nullptr);
        const auto* owner = static_cast<const Client::Kernel::WorldObject*>(effects[0].ownerContext);
        EXPECT_EQ(owner->type, Client::Effects::DragonEffect);
        EXPECT_EQ(owner->transform.position, glm::vec3(5600, 23000, 0));
        effects[0].seen = true;
        EXPECT_TRUE(effects[0].alive);
    }
    EXPECT_TRUE(effects[0].alive);
    EXPECT_TRUE(effects[0].seen);
    EXPECT_FALSE(effects[0].hasOwner);
    EXPECT_EQ(effects[0].ownerContext, nullptr);
    EXPECT_TRUE(effects[2].hasOwner);
    EXPECT_EQ(effects[2].ownerContext, &unrelatedOwner);
}

// CL7.6 B1: with the cloth's globals (as in main.cpp), the character's draw asks for the frame's sprites, the white
// helm's 30 flares (0x455930) and then the scepter's five (0x509340, case 0x620), and they go additive in their pass,
// the last of the models, with the view at identity. Then the lantern's two (CL7.6 B4). Then the particles (0x548550,
// CL7.6 B2): the horse's breaths, the raven's sparks (CL7.6 C2) and the clouds of whatever 0x54 there is (with the
// rand() the dragon's step draws at 0x5208c5, this frame's guard drops one).
TEST_F(GoldenLoginWorld, TheSpritesOfTheCharacterGoInTheirPass)
{
    Client::Models::ClothWorld cloth;
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
    // Each sprite, two triangles; the batch joins consecutive ones with the same state. The pairs are int: building
    // them from the literals with std::uint16_t would do the conversion inside std::pair, and MSVC warns (C4242). Only
    // the stretches that draw: the particles publish before each quad the geometry-less effects of their material and
    // their binder (0x620a90 and 0x620780), with the texture that was bound.
    constexpr std::size_t PerSprite = 6;
    std::vector<std::pair<int, int>> runs;
    for (const auto& command : models.back().modelCommands)
    {
        if (command.operation != Client::Render::ModelStateOperation::Draw)
        {
            continue;
        }
        ASSERT_TRUE(command.state.texture.has_value());
        // The clouds 0x622/10 of marker 0x54 (0x6044bc), if their guard's rand() drops them, go subtractive.
        EXPECT_EQ(command.state.blend, command.state.texture->value == 0x622 ? Client::Render::ModelBlend::Subtractive
                                                                             : Client::Render::ModelBlend::Additive);
        runs.emplace_back(command.state.texture->value, static_cast<int>(command.vertexCount / PerSprite));
    }
    // After the character's, the lantern's two (0x602ed0, CL7.6 B4), in the objects' draw.
    const std::vector<std::pair<int, int>> expected{{0x5ac, 30}, {0x5fd, 2}, {0x5ac, 1}, {0x5fd, 2}, {0x5ac, 2}};
    ASSERT_GE(runs.size(), expected.size());
    const std::vector<std::pair<int, int>> sprites(runs.begin(), runs.begin() + 5);
    EXPECT_EQ(sprites, expected);
    EXPECT_TRUE(std::all_of(runs.begin() + 5, runs.end(), [](const auto& run)
                            { return run.first == 0x5f2 || run.first == 0x5c6 || run.first == 0x622; }))
        << "después, solo las partículas: los alientos, las chispas del cuervo y las nubes";
    // 0x549510 marks them seen before the pass: 0x549290 measures them with alpha at 1. The first flare (column 0,
    // scale 0.4, no rotation) goes at 0.4 of flare01's width, in eye coordinates.
    const auto flareSize = m_models.TextureSize(TextureSlot{0x5ac});
    ASSERT_TRUE(flareSize.has_value());
    const auto& vertices = models.back().modelVertices;
    ASSERT_GE(vertices.size(), PerSprite);
    const auto [left, right] =
        std::minmax_element(vertices.begin(), vertices.begin() + PerSprite,
                            [](const auto& a, const auto& b) { return a.position.x < b.position.x; });
    // The corners are far from the eye: the subtraction loses the last bits (without 0x549510 it would give 0.36).
    EXPECT_NEAR(right->position.x - left->position.x, 0.4f * flareSize->x, 0.01f);
    ASSERT_TRUE(models.back().pass.has_value());
    EXPECT_EQ(models.back().pass->view, glm::mat4(1.0f));
    scene.Leave();
}

// 0x505de0 (0x5068eb..0x5069ab) on each step of 0x508ea0: the login's Dark Lord is seen, is not the hero and does not
// die, so it picks where to look (rand() % 32 at 0) and the head turns toward there by 20 %. On entering, straight;
// in 400 steps it picks at some point, and each step ends up as close to its target as the previous one or closer.
TEST_F(GoldenLoginWorld, TheLoginConstructsItsCharacterInThePool)
{
    Client::Kernel::WorldState world;
    Client::Session::ModuleStates states;
    Client::Models::ClothWorld cloth;
    const auto before = cloth.random;
    LoginScene scene(SceneContext{.dataRoot = m_dataRoot,
                                  .textures = m_store,
                                  .log = m_log,
                                  .cloth = &cloth,
                                  .world = &world,
                                  .states = &states},
                     *m_data, m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());

    ASSERT_TRUE(scene.Enter().HasValue());

    const auto& actor = world.Actors()[2];
    EXPECT_TRUE(actor.alive);
    EXPECT_EQ(actor.type, 0x20a);
    EXPECT_EQ(actor.key, 2);
    EXPECT_EQ(actor.characterClass, 4);
    EXPECT_EQ(actor.cellX, 0);
    EXPECT_EQ(actor.cellY, 0);
    EXPECT_EQ(actor.position, glm::vec3(10603.0f, 14112.0f, 580.0f));
    EXPECT_EQ(actor.angles, glm::vec3(0.0f, 0.0f, 188.0f));
    EXPECT_FLOAT_EQ(actor.scale, 1.6f);
    EXPECT_EQ(actor.animation.action, 0x4a);
    EXPECT_EQ(actor.hands[0].model, 0x620);
    EXPECT_EQ(actor.hands[1].model, 0x1c17);
    EXPECT_EQ(actor.pet.model, 0x1c16);
    EXPECT_FALSE(world.Actors()[0].alive);
    auto expected = before;
    // The 28 of 0x511290, the suffix of 0x4f2250 and the 12 of 0x420d60.
    for (int draw = 0; draw < 41; ++draw)
    {
        static_cast<void>(expected.Next());
    }
    EXPECT_EQ(cloth.random.State(), expected.State());
    scene.Step(Client::Input::InputState{});
    EXPECT_FALSE(scene.Failure().has_value());
    EXPECT_TRUE(actor.seen);
    EXPECT_GT(actor.animation.frame, 0.0f);
    EXPECT_FALSE(states.terrain.current.attributes) << "las paredes son del LoadedWorld de la escena";
}

TEST_F(GoldenLoginWorld, TheDrawingAndTheNextEntryUseThePoolCharacter)
{
    Client::Kernel::WorldState world;
    Client::Models::ClothWorld cloth;
    LoginScene scene(
        SceneContext{.dataRoot = m_dataRoot, .textures = m_store, .log = m_log, .cloth = &cloth, .world = &world},
        *m_data, m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    auto& actor = world.Actors()[2];
    actor.animation.action = 0x54;
    actor.animation.frame = 3.0f;
    actor.headTurn.angles = {17.0f, 23.0f};
    actor.position.x = 10700.0f;
    actor.hands[0].model = 0x222;
    const auto drawn = scene.TheCharacterToDraw(0.0f);
    EXPECT_EQ(drawn.body.action, 0x54);
    EXPECT_FLOAT_EQ(drawn.body.frame, 3.0f);
    EXPECT_EQ(drawn.headAngles, glm::vec2(17.0f, 23.0f));
    EXPECT_FLOAT_EQ(drawn.object.position.x, 10700.0f);
    ASSERT_TRUE(drawn.appearance.rightHand);
    EXPECT_EQ(drawn.appearance.rightHand->model, 0x222);

    scene.Leave();
    ASSERT_TRUE(scene.Enter().HasValue());
    EXPECT_EQ(actor.headTurn.angles, glm::vec2(17.0f, 23.0f)) << "0x511290 no escribe la cabeza";
    EXPECT_EQ(actor.animation.action, 0x4a);
    EXPECT_EQ(actor.hands[0].model, 0x620);
    ASSERT_TRUE(actor.darkSpirit);
    EXPECT_EQ(actor.darkSpirit->position, glm::vec3(50.0f, 50.0f, 750.0f));
}

// The login's Dark Lord runs what hangs and the effects of 0x50ba30 like the game (0x50db57..0x510a7a): the light of
// what hangs (0x50db94) is the terrain's at its place plus +0xf4, the body's, or a PK's red from 6; a frame of the
// original leaves the body in its bones (+0x120), and an intermediate one does not touch them.
TEST_F(GoldenLoginWorld, TheLoginCharacterRunsTheTailOfTheGame)
{
    using namespace std::chrono_literals;
    Client::Kernel::WorldState world;
    Client::Models::ClothWorld cloth;
    LoginScene scene(
        SceneContext{.dataRoot = m_dataRoot, .textures = m_store, .log = m_log, .cloth = &cloth, .world = &world},
        *m_data, m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    // 0x634d13 ends at 0x5091d0 after advancing the login's characters.
    auto& ribbon = world.DrawTail().ribbons[0];
    ribbon.alive = true;
    ribbon.count = 3;
    ribbon.life = 4;
    scene.Step(Client::Input::InputState{});
    EXPECT_EQ(ribbon.count, 2);
    EXPECT_EQ(ribbon.life, 3);
    auto& actor = world.Actors()[2];
    DrawAt(scene, 40ms);
    ASSERT_FALSE(scene.Failure().has_value()) << *scene.Failure();
    EXPECT_EQ(actor.drawnLight, scene.TheCharacterToDraw(0.0f).light);
    ASSERT_FALSE(actor.bodyPose.empty());
    const auto posed = actor.bodyPose;

    actor.pk = 6;
    actor.animation.frame += 1.0f;
    DrawAt(scene, 40ms);
    EXPECT_EQ(actor.drawnLight, glm::vec3(1.0f, 0.1f, 0.1f));
    EXPECT_EQ(actor.bodyPose, posed) << "un dibujo intermedio no posa";
    DrawAt(scene, 87ms);
    EXPECT_NE(actor.bodyPose, posed);
    EXPECT_FALSE(scene.Failure().has_value());
    scene.Leave();
}

TEST_F(GoldenLoginWorld, TheLoginPublishesTheCharacterCutAndKeepsItsRandomState)
{
    Client::Kernel::WorldState world;
    Client::Models::ClothWorld cloth;
    LoginScene scene(
        SceneContext{.dataRoot = m_dataRoot, .textures = m_store, .log = m_log, .cloth = &cloth, .world = &world},
        *m_data, m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    world.Actors()[2].movement = 1;
    world.Actors()[2].animation.action = 0x4b;
    // 0x508e27 already runs the movement. The body's continuation with the name of 0x6c88b0 reaches the name message
    // (0x4fcd82), which in the login has no timeGetTime() and stops.
    constexpr std::array<std::uint8_t, 11> MessageName{0xc8, 0xb2, 0xb1, 0xdd, 0xc0, 0xc7,
                                                       0x20, 0xb1, 0xc3, 0xbc, 0xf6};
    for (std::size_t i = 0; i < world.Actors()[2].name.size(); ++i)
        world.Actors()[2].name[i] = std::byte{MessageName[i]};
    world.Actors()[2].byte1e3 = MessageName[10];
    constexpr std::uint32_t Seed = 1;
    cloth.random = Client::Crt::CrtRandom(Seed);
    cloth.logicSteps = 77;
    const auto wind = cloth.wind;
    const float windBase = cloth.windBase;
    auto& ribbon = world.DrawTail().ribbons[0];
    ribbon.alive = true;
    ribbon.life = 3;
    scene.Step(Client::Input::InputState{});
    ASSERT_TRUE(scene.Failure());
    EXPECT_NE(scene.Failure()->find("0x4fcd82"), std::string::npos);
    EXPECT_GT(world.Actors()[2].run, 0) << "el movimiento precede al corte publicado del cuerpo";
    EXPECT_EQ(ribbon.life, 3) << "el corte del personaje precede a 0x5091d0";
    // Eighty embers with 10 rand; then 8 of TerrainWall and 1 of SetActorWalk. The body stops before the effects, the
    // particles and the cloth.
    EXPECT_EQ(cloth.random.State(), Client::Tests::AfterDraws(Seed, 809).State());
    EXPECT_EQ(cloth.logicSteps, 77);
    EXPECT_EQ(cloth.wind, wind);
    EXPECT_FLOAT_EQ(cloth.windBase, windBase);
    const auto stopped = cloth.random.State();
    scene.Step(Client::Input::InputState{});
    EXPECT_EQ(cloth.random.State(), stopped);
}

TEST_F(GoldenLoginWorld, TheHeadOfTheDarkLordLooksAround)
{
    Client::Models::ClothWorld cloth;
    LoginScene scene(SceneContext{.dataRoot = m_dataRoot, .textures = m_store, .log = m_log, .cloth = &cloth}, *m_data,
                     m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    EXPECT_EQ(scene.Head().angles, glm::vec2(0.0f));
    EXPECT_EQ(scene.Head().target, glm::vec2(0.0f));

    std::size_t targets = 0;
    glm::vec2 target = scene.Head().target;
    for (int step = 0; step < 400; ++step)
    {
        const Client::Kernel::CharacterHead before = scene.Head();
        scene.Step(Client::Input::InputState{});
        const Client::Kernel::CharacterHead& after = scene.Head();
        if (after.target != target)
        {
            ++targets;
            target = after.target;
            continue;
        }
        for (int k = 0; k < 2; ++k)
        {
            EXPECT_LE(Client::Math::AngleDistance(after.angles[k], after.target[k], true),
                      Client::Math::AngleDistance(before.angles[k], before.target[k], true))
                << "paso " << step << ", ángulo " << k;
        }
    }
    EXPECT_GT(targets, 3u);
    EXPECT_NE(scene.Head().angles, glm::vec2(0.0f));
    EXPECT_EQ(scene.TheCharacterToDraw(0.5f).headAngles, scene.Head().angles);
}

// 0x505de0 runs with the character seen (+0x16c, which 0x508ea0 writes with 0x5fba90: its place times 0.01f, with
// the −20 margin), and in the login it is always seen (P-C27 i): with the camera fixed, each draw's trapezoid has it
// inside. After drawing, the head picks a target, and on each step between draws it turns toward it. What the
// original sees before the first draw is P-C27 e, and is not checked here.
TEST_F(GoldenLoginWorld, TheHeadSeesTheCharacterAfterEveryDraw)
{
    using namespace std::chrono_literals;
    constexpr int MaxSteps = 400;
    constexpr int Turns = 10;
    Client::Models::ClothWorld cloth;
    LoginScene scene(SceneContext{.dataRoot = m_dataRoot, .textures = m_store, .log = m_log, .cloth = &cloth}, *m_data,
                     m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    scene.Step(Client::Input::InputState{});
    DrawAt(scene, 40ms);
    for (int step = 0; step < MaxSteps && scene.Head().target == glm::vec2(0.0f); ++step)
    {
        scene.Step(Client::Input::InputState{});
    }
    ASSERT_NE(scene.Head().target, glm::vec2(0.0f)) << "con el trapecio de un dibujo, la cabeza sortea";
    for (int turn = 0; turn < Turns; ++turn)
    {
        DrawAt(scene, 80ms + (40ms * turn));
        const glm::vec2 before = scene.Head().angles;
        scene.Step(Client::Input::InputState{});
        EXPECT_NE(scene.Head().angles, before) << "el paso " << turn << " después de un dibujo";
    }
    scene.Leave();
}

// The scepter rolls once per frame of the original: what hangs (0x509340) runs with the global stream only on a frame
// the clock counts, and an intermediate draw repeats its sprites from the last one. Two MuRet frames with the same
// clock draw the same five sprites, in the same place; one 47 ms later, others.
TEST_F(GoldenLoginWorld, TheScepterGlowChangesOncePerOriginalFrame)
{
    Client::Models::ClothWorld cloth;
    LoginScene scene(SceneContext{.dataRoot = m_dataRoot, .textures = m_store, .log = m_log, .cloth = &cloth}, *m_data,
                     m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    scene.Step(Client::Input::InputState{});
    // The vertices of the scepter's five (after the helm's 30 flares) of a frame with `clock`.
    const auto scepterAt = [&scene](std::chrono::milliseconds clock)
    {
        RecordingFrameTarget target;
        scene.Render(target.BeginFrame(clock), 0.0f);
        target.Present();
        std::vector<FrameEvent> models;
        std::ranges::copy_if(target.frames.back().events, std::back_inserter(models),
                             [](const FrameEvent& event) { return event.kind == Kind::Models; });
        constexpr std::size_t PerSprite = 6;
        std::vector<glm::vec3> positions;
        if (models.size() == 2 && models.back().modelVertices.size() >= 35 * PerSprite)
        {
            for (std::size_t k = 30 * PerSprite; k < 35 * PerSprite; ++k)
            {
                positions.push_back(models.back().modelVertices[k].position);
            }
        }
        return positions;
    };

    const Client::Crt::CrtRandom beforeFirst = cloth.random;
    const std::vector<glm::vec3> first = scepterAt(std::chrono::milliseconds{40});
    const std::uint32_t beforeAgain = cloth.random.State();
    const std::vector<glm::vec3> again = scepterAt(std::chrono::milliseconds{40});
    const std::uint32_t afterAgain = cloth.random.State();
    const std::vector<glm::vec3> next = scepterAt(std::chrono::milliseconds{87});

    ASSERT_EQ(first.size(), 30u);
    EXPECT_NE(beforeAgain, beforeFirst.State());
    EXPECT_EQ(again, first);
    // The intermediate draw takes nothing from the global stream: neither the scepter nor the objects.
    EXPECT_EQ(afterAgain, beforeAgain);
    EXPECT_NE(next, first);
    scene.Leave();
}

// Each entry starts the pace of what hangs and of the cloth (P-C27 i): the original runs them on each frame it draws
// (0x509340, 0x50d58b), and between one entry and the other the login is not drawn. The second's first draw runs
// one, however long passed since the first's last (by the clock, 5 s would be 64): it takes from the global stream
// what the first entry's first one did.
TEST_F(GoldenLoginWorld, EnteringAgainStartsTheScepterClock)
{
    using namespace std::chrono_literals;
    Client::Models::ClothWorld cloth;
    LoginScene scene(SceneContext{.dataRoot = m_dataRoot, .textures = m_store, .log = m_log, .cloth = &cloth}, *m_data,
                     m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    const auto spent = [&cloth](const Client::Crt::CrtRandom& before)
    {
        Client::Crt::CrtRandom walk = before;
        int count = 0;
        while (walk.State() != cloth.random.State() && count < 1'000'000)
        {
            static_cast<void>(walk.Next());
            ++count;
        }
        return count;
    };
    ASSERT_TRUE(scene.Enter().HasValue());
    scene.Step(Client::Input::InputState{});
    const Client::Crt::CrtRandom beforeFirstEntry = cloth.random;
    DrawAt(scene, 40ms);
    const int firstEntry = spent(beforeFirstEntry);
    scene.Leave();

    ASSERT_TRUE(scene.Enter().HasValue());
    scene.Step(Client::Input::InputState{});
    const Client::Crt::CrtRandom beforeSecondEntry = cloth.random;
    DrawAt(scene, 40ms + 5s);
    const int secondEntry = spent(beforeSecondEntry);

    // A frame costs about 370 (the cloth, the scepter and one rand() per object, with some that depend on what comes
    // out); 64 would be thousands.
    ASSERT_GT(firstEntry, 0);
    EXPECT_LT(secondEntry, firstEntry + firstEntry / 2) << "un frame del original, no 64";
    EXPECT_GT(secondEntry, firstEntry / 2);
    scene.Leave();
}

// In faithful mode (the context's pacing at Faithful), the cloth and the scepter run once per drawn frame even if the
// frame does not last exactly 46.875 ms: the faithful login, with the first frame 0.3 ms late and the others 0.1 ms,
// takes the same rand() as one by the clock with exact frames. By the clock, the faithful one's second frame would
// run none.
TEST_F(GoldenLoginWorld, InTheFaithfulModeTheClothAndTheScepterRunOncePerDrawnFrame)
{
    using namespace std::chrono_literals;
    using Client::Models::OriginalFrameTime;
    Client::Models::ClothWorld faithfulCloth;
    Client::Models::ClothWorld exactCloth;
    LoginScene faithful(SceneContext{.dataRoot = m_dataRoot,
                                     .textures = m_store,
                                     .log = m_log,
                                     .cloth = &faithfulCloth,
                                     .pacing = Client::Kernel::PacingMode::Faithful},
                        *m_data, m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    LoginScene exact(SceneContext{.dataRoot = m_dataRoot, .textures = m_store, .log = m_log, .cloth = &exactCloth},
                     *m_data, m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    ASSERT_TRUE(faithful.Enter().HasValue());
    ASSERT_TRUE(exact.Enter().HasValue());
    faithful.Step(Client::Input::InputState{});
    exact.Step(Client::Input::InputState{});
    ASSERT_EQ(faithfulCloth.random.State(), exactCloth.random.State());
    for (int frame = 0; frame < 6; ++frame)
    {
        const Client::Core::Duration exactClock = OriginalFrameTime * (frame + 1);
        const Client::Core::Duration lateClock = exactClock + (frame == 0 ? 300us : 100us);
        RecordingFrameTarget faithfulTarget;
        faithful.Render(faithfulTarget.BeginFrame(lateClock), 0.0f);
        faithfulTarget.Present();
        RecordingFrameTarget exactTarget;
        exact.Render(exactTarget.BeginFrame(exactClock), 0.0f);
        exactTarget.Present();
        EXPECT_EQ(faithfulCloth.random.State(), exactCloth.random.State()) << "frame " << frame;
    }
    faithful.Leave();
    exact.Leave();
}

// A login with its session on a FakeSocket and the cloth's globals, to hold the frame with a connect (Q-C85).
struct LoginWithASession
{
    Client::Tests::LoggedInSession online;
    Client::Models::ClothWorld cloth;
    std::optional<LoginScene> scene;
};

// A GameLoop frame with one step: the session and the scene's step. If the scene holds the frame, it is told so like
// SceneManager::HoldsTheLastFrame (IScene::OnFrameHeld).
void StepWithTheSession(LoginWithASession& login)
{
    static_cast<void>(login.online.session.Poll());
    login.scene->Step(Client::Input::InputState{});
    if (login.scene->HoldsTheLastFrame())
    {
        login.scene->OnFrameHeld();
    }
}

// P-C27 f: the connect to the GS (0x4e3396) blocks inside a step, and the original does not draw meanwhile (Q-C85).
// On the first frame it draws after, the cloth (0x50ba30) and the scepter (0x509340) run one, as on any drawn frame.
// MuRet holds the frame while it connects, and in normal mode counts them by the clock: without restarting their
// clocks with the held frame, the first after 5 s of connect would run 64 frames of the original at once. Both
// logins do the same steps and draw with the same clocks; the faithful one runs one per draw, and the normal one must
// take the same rand() as it.
TEST_F(GoldenLoginWorld, AfterAHeldFrameTheClothAndTheScepterRunOneFrame)
{
    using namespace std::chrono_literals;
    using Client::Models::OriginalFrameTime;
    using Client::Session::SessionState;
    constexpr std::uint16_t LabServer = 400;
    constexpr std::string_view GameHost = "10.0.0.2";
    constexpr std::uint16_t GamePort = 55901;
    constexpr int FramesBefore = 3;
    constexpr int MaxSteps = 10;
    std::array<LoginWithASession, 2> logins;
    const std::array<Client::Kernel::PacingMode, 2> pacings{Client::Kernel::PacingMode::Decoupled,
                                                            Client::Kernel::PacingMode::Faithful};
    for (std::size_t k = 0; k < logins.size(); ++k)
    {
        LoginWithASession& login = logins[k];
        login.scene.emplace(
            SceneContext{
                .dataRoot = m_dataRoot, .textures = m_store, .log = m_log, .cloth = &login.cloth, .pacing = pacings[k]},
            *m_data, m_models, &login.online.session, Version, FixedSeed, &m_models.TextureSlots());
        ASSERT_TRUE(login.scene->Enter().HasValue());
        // The CS hello and list, and the choice: the session asks for the GS address.
        login.online.socket.reads = {Client::Tests::Hello()};
        StepWithTheSession(login);
        login.online.socket.reads = {Client::Tests::ServerList({LabServer})};
        StepWithTheSession(login);
        ASSERT_EQ(login.online.session.State(), SessionState::ChoosingServer);
        login.online.session.ChooseServer(LabServer);
        StepWithTheSession(login);
        ASSERT_EQ(login.online.session.State(), SessionState::WaitingServerAddress);
        ASSERT_FALSE(login.scene->HoldsTheLastFrame());
        // A few frames exactly one original frame apart: by the clock they also run one each.
        for (int frame = 0; frame < FramesBefore; ++frame)
        {
            StepWithTheSession(login);
            DrawAt(*login.scene, OriginalFrameTime * (frame + 1));
        }
    }
    ASSERT_EQ(logins[0].cloth.random.State(), logins[1].cloth.random.State()) << "antes del connect";

    for (LoginWithASession& login : logins)
    {
        // The address: the connect to the GS takes two Polls, and meanwhile the frame stays held.
        login.online.socket.pollsUntilConnected = 2;
        login.online.socket.reads = {Client::Tests::AddressReply(GameHost, GamePort)};
        StepWithTheSession(login);
        ASSERT_EQ(login.online.session.State(), SessionState::ConnectingGameServer);
        ASSERT_TRUE(login.scene->HoldsTheLastFrame());
        for (int step = 0; step < MaxSteps && login.online.session.State() == SessionState::ConnectingGameServer;
             ++step)
        {
            StepWithTheSession(login);
        }
        ASSERT_EQ(login.online.session.State(), SessionState::WaitingGameHello);
        ASSERT_FALSE(login.scene->HoldsTheLastFrame());
        // The first frame after the connect, which took 5 s.
        DrawAt(*login.scene, (OriginalFrameTime * FramesBefore) + 5s);
    }
    EXPECT_EQ(logins[0].cloth.random.State(), logins[1].cloth.random.State())
        << "la tela (75 rand() por frame) y el cetro (6) corren un frame, no 64";

    for (LoginWithASession& login : logins)
    {
        login.scene->Leave();
    }
}

// CL7.6 B2: the horse's breath (0x444010) comes out of bone 27 of the pose's global buffer. It is written by the last
// posed model that has that bone and is not a dummy (0x4eb973 skips dummies): the dragon is drawn after the horse
// (0x52d620), but its bone 27 is a dummy, and map 55's objects have fewer than 28. The horse's remains, Bone03, with
// the previous draw's pose: the horse's advance (0x4ec200, at 0x445039) runs after the breath. The test repeats the
// horse's steps and expects each new breath at (50, −4, 0) of bone 27 of the pose before its step, plus its first
// step's rise (0.2); before the horse is seen, at (0, 0, 0).
TEST_F(GoldenLoginWorld, TheBreathOfTheHorseComesFromItsHeadAsDrawnBefore)
{
    Client::Models::ClothWorld cloth;
    LoginScene scene(SceneContext{.dataRoot = m_dataRoot, .textures = m_store, .log = m_log, .cloth = &cloth}, *m_data,
                     m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    const Client::Models::Skeleton* horseSkeleton =
        m_models.SkeletonOf(Client::Models::ModelId{Client::Pets::DarkHorseModel});
    ASSERT_NE(horseSkeleton, nullptr);
    const glm::dvec3 rise{0.0, 0.0, 0.2};
    Client::Kernel::DarkHorse horse;
    std::size_t breaths = 0;
    std::size_t matched = 0;
    std::size_t previous = 0;
    for (int step = 0; step < 60; ++step)
    {
        RecordingFrameTarget before;
        scene.Render(before.BeginFrame(std::chrono::milliseconds{40 * step}), 0.0f);
        before.Present();
        // What this step's breath will read: the horse as just drawn. With alpha below 0.01 (before its first step) the
        // bone was neither drawn nor written: the breath is born at (0, 0, 0).
        const bool horseDrawn = horse.alpha >= 0.01f;
        const auto expected = Client::Actors::Drawing::PosedBonePoint(
            m_models,
            Client::Pets::DrawnDarkHorse(horse, horseSkeleton, Client::FrontEnd::LoginCharacterTransform(),
                                         Client::Pets::DarkHorseLoginScale, 0.0f),
            27, {50.0f, -4.0f, 0.0f});
        ASSERT_TRUE(expected.has_value());
        scene.Step(Client::Input::InputState{});
        Client::Pets::StepDarkHorse(horse, *horseSkeleton, Client::FrontEnd::LoginCharacterStandingAction);

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
        // One more than in the previous step (and none goes out before 16): it was born in this one.
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

// CL7.6 B4: the castle lantern (0x602ed0, case 0x3c in scene 2) asks for two flare01 sprites at the translations of
// bones 8 and 9 of the pose's global buffer, but Object61.bmd has a single bone (Q-C140). No .obj object reaches bone
// 8, but before them map 55's load creates the dragon's owner (0x6188ae: 0x606660(0xb4, (5600, 23000, 0), (0, 0, 0),
// 1)), in sector (3, 14), before the lantern (5, 14): it is not drawn (+0x58 at −2), but 0x5ffdf0 poses it with
// Dragon.bmd, and its bones 8 and 9 are Bip01 L Toe0 and Bip01 L Toe01. The glow comes out there, not on the horse,
// with the light (L, 0.5 · L, 0.2 · L) and L between 0.7 and 0.99.
TEST_F(GoldenLoginWorld, TheLanternGlowsAtTheToesOfTheDragonsOwner)
{
    Client::Assets::WorldModels world;
    ASSERT_TRUE(world.Load(m_dataRoot, 0x37, m_store, m_log, nullptr).HasValue());
    const Client::Models::Skeleton* dragon = world.SkeletonOf(Client::Models::ModelId{Client::Assets::DragonModel});
    ASSERT_NE(dragon, nullptr);
    Client::Data::Terrain::TerrainObject record;
    record.type = static_cast<std::int16_t>(Client::Assets::DragonModel);
    record.position = {5600.0f, 23000.0f, 0.0f};
    record.scale = 1.0f;
    Client::Kernel::WorldObject owner = Client::Tests::CheckedWorldObjectProjection(
                                            std::span(&record, 1), 0x37, Client::WorldObjects::ObjectScene::Login)
                                            .front();
    const Client::Models::Skeleton* horseSkeleton =
        m_models.SkeletonOf(Client::Models::ModelId{Client::Pets::DarkHorseModel});
    ASSERT_NE(horseSkeleton, nullptr);
    Client::Kernel::DarkHorse horse;

    Client::Models::ClothWorld cloth;
    LoginScene scene(SceneContext{.dataRoot = m_dataRoot, .textures = m_store, .log = m_log, .cloth = &cloth}, *m_data,
                     m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    for (int step = 0; step < 3; ++step)
    {
        scene.Step(Client::Input::InputState{});
        // 0x606560 only advances the ones seen in the last draw.
        if (owner.seen)
        {
            Client::WorldObjects::StepWorldObject(owner, 0x37, dragon);
        }
        Client::Pets::StepDarkHorse(horse, *horseSkeleton, Client::FrontEnd::LoginCharacterStandingAction);
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
        // The lantern's sprites: the flare01 ones that follow the scepter's last (0x5fd).
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
        Client::Actors::Drawing::DrawnObject drawn;
        drawn.model = Client::Assets::DragonModel;
        drawn.animation = owner.animation;
        drawn.transform = owner.transform;
        const Client::Actors::Drawing::DrawnObject drawnHorse = Client::Pets::DrawnDarkHorse(
            horse, horseSkeleton, Client::FrontEnd::LoginCharacterTransform(), Client::Pets::DarkHorseLoginScale, 0.0f);
        for (std::size_t bone = 8; bone <= 9; ++bone)
        {
            const auto expected = Client::Actors::Drawing::PosedBonePoint(world, drawn, bone, {});
            ASSERT_TRUE(expected.has_value());
            EXPECT_LT(glm::length(centers[bone - 8] - glm::dvec3(*expected)), 0.05)
                << "el paso " << step << ", el hueso " << bone;
            const auto onTheHorse = Client::Actors::Drawing::PosedBonePoint(m_models, drawnHorse, bone, {});
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

// CL7.6 B4: 0x602ed0 takes one rand() from the cloth's CRT for each object seen in the draw (0x602ef4): the .obj's
// 276, the dragon's owner and the logo, all seen in the login. Of them, the two 0x54 add one guard each and 0, 5 or
// 10 creation rand. The cloth and the scepter take fewer than 100 more. The test uses Faithful: each draw stands for
// an original frame even though 40 ms pass between their clocks.
TEST_F(GoldenLoginWorld, EverySeenObjectTakesARandInTheDraw)
{
    Client::Models::ClothWorld cloth;
    LoginScene scene(SceneContext{.dataRoot = m_dataRoot,
                                  .textures = m_store,
                                  .log = m_log,
                                  .cloth = &cloth,
                                  .pacing = Client::Kernel::PacingMode::Faithful},
                     *m_data, m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    scene.Step(Client::Input::InputState{});
    {
        RecordingFrameTarget target;
        scene.Render(target.BeginFrame(std::chrono::milliseconds{40}), 0.0f);
        target.Present();
    }
    scene.Step(Client::Input::InputState{});
    Client::Crt::CrtRandom walk = cloth.random;
    RecordingFrameTarget target;
    scene.Render(target.BeginFrame(std::chrono::milliseconds{80}), 0.0f);
    target.Present();
    std::size_t taken = 0;
    while (walk.State() != cloth.random.State() && taken < 1000)
    {
        (void)walk.Next();
        ++taken;
    }
    constexpr std::size_t SeenObjects = 278;
    constexpr std::size_t CloudGuards = 2;
    constexpr std::size_t MaxCreationRandoms = CloudGuards * 5;
    constexpr std::size_t OtherDrawRandomsLimit = 100;
    EXPECT_GE(taken, SeenObjects + CloudGuards);
    EXPECT_LT(taken, SeenObjects + CloudGuards + MaxCreationRandoms + OtherDrawRandomsLimit);
    scene.Leave();
}

// CL7.6 C1: the raven (0xc0) is drawn with mesh 3 (dksbody.tga, at 0x26f) and mesh 2 (dkonebody.jpg, at 0x3c9). The
// centre of its mesh 2's vertices in the last frame, if it was drawn.
std::optional<glm::vec3> RavenCenterOf(const RecordingFrameTarget& target)
{
    const auto world = std::ranges::find(target.frames.back().events, Kind::Models, &FrameEvent::kind);
    if (world == target.frames.back().events.end())
    {
        return std::nullopt;
    }
    glm::dvec3 sum(0.0);
    std::size_t count = 0;
    for (const auto& command : world->modelCommands)
    {
        if (command.state.texture != TextureSlot{0x3c9})
        {
            continue;
        }
        for (std::size_t k = 0; k < command.vertexCount; ++k)
        {
            sum += glm::dvec3(world->modelVertices[command.firstVertex + k].position);
            ++count;
        }
    }
    if (count == 0)
    {
        return std::nullopt;
    }
    return glm::vec3(sum / static_cast<double>(count));
}

// CL7.6 C1: the raven is born at the owner's cell (0, 0) (Q-C130: 0x6349a0 does not write the cells of the login's
// character), at the terrain height there (450) plus 300: the centre of its mesh 2 ends up about 35 from
// (50, 50, 750). It travels far and jumps next to the rider when the distance counter passes 90: at the earliest on
// step 91. Past the climb, it circles at 350 above the owner on map 55 (Q-C131): with 250 the mean height would give
// about 330; with 350, about 500.
TEST_F(GoldenLoginWorld, TheRavenIsBornAtCell0AndJumpsToTheRider)
{
    Client::Models::ClothWorld cloth;
    LoginScene scene(SceneContext{.dataRoot = m_dataRoot, .textures = m_store, .log = m_log, .cloth = &cloth}, *m_data,
                     m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    const glm::vec3 rider = Client::FrontEnd::LoginCharacterTransform().position;
    std::optional<int> jumped;
    std::size_t heights = 0;
    double sum = 0.0;
    // It is drawn every 5 steps until the jump (one before 91 would land on one of them) and, 60 past the climb, every
    // 8: the draw is the test's cost. The draw takes rand() (0x602ed0, the cloth) and runs the step's jump, but not
    // before 91.
    constexpr int Earliest = 91;
    constexpr int Climb = 60;
    constexpr std::size_t Samples = 90;
    for (int step = 1; step <= 1400 && heights < Samples; ++step)
    {
        scene.Step(Client::Input::InputState{});
        const bool look = step == 1 || (jumped ? (step > *jumped + Climb && step % 8 == 0) : (step % 5 == 0));
        if (!look)
        {
            continue;
        }
        RecordingFrameTarget target;
        scene.Render(target.BeginFrame(std::chrono::milliseconds{40 * step}), 0.0f);
        target.Present();
        const std::optional<glm::vec3> raven = RavenCenterOf(target);
        ASSERT_TRUE(raven.has_value()) << "el paso " << step;
        if (step == 1)
        {
            EXPECT_LT(glm::length(glm::dvec3(*raven) - glm::dvec3(50.0, 50.0, 750.0)), 60.0)
                << "nace en (50, 50) a la altura del terreno más 300";
        }
        const double horizontal = glm::length(glm::dvec2(raven->x - rider.x, raven->y - rider.y));
        if (!jumped)
        {
            if (horizontal < 640.0)
            {
                jumped = step;
            }
            else
            {
                EXPECT_LT(glm::length(glm::dvec2(raven->x, raven->y)), 3000.0)
                    << "el paso " << step << ": antes del salto, cerca de la celda (0, 0)";
            }
            continue;
        }
        EXPECT_LT(horizontal, 640.0) << "el paso " << step << ": después del salto, junto al jinete";
        sum += static_cast<double>(raven->z - rider.z);
        ++heights;
    }
    ASSERT_TRUE(jumped.has_value()) << "en 600 pasos tiene que saltar";
    EXPECT_GE(*jumped, Earliest) << "el contador pasa de 90";
    ASSERT_EQ(heights, Samples);
    const double mean = sum / static_cast<double>(heights);
    // The mean depends on the 41 entry rand() and the full step (walls, body, raven and head). The threshold separates
    // the two heights; the one that fixes the 350 bit for bit is DarkSpiritTest.ItFliesHigherInTheLoginMap.
    EXPECT_GT(mean, 390.0) << "a 350 sobre el dueño en el mapa 55, no a 250";
    EXPECT_LT(mean, 600.0);
    scene.Leave();
}

// Each entry builds the character and its raven again (0x517270 and 0x420d60, from 0x6349a0), with the character's
// cells at (0, 0) (Q-C130, P-C27 i): on the second entry the raven is born at (50, 50, 750) again, even though on the
// first it already moved away.
TEST_F(GoldenLoginWorld, EnteringAgainTheRavenIsBornAtCell0Again)
{
    using namespace std::chrono_literals;
    constexpr int Flight = 40;
    constexpr double Near = 60.0;
    const glm::dvec3 birth{50.0, 50.0, 750.0};
    Client::Models::ClothWorld cloth;
    LoginScene scene(SceneContext{.dataRoot = m_dataRoot, .textures = m_store, .log = m_log, .cloth = &cloth}, *m_data,
                     m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    // How far mesh 2's centre is from where it is born after `steps` steps, in a draw.
    const auto distanceAfter = [&scene, &birth](int steps) -> std::optional<double>
    {
        for (int step = 0; step < steps; ++step)
        {
            scene.Step(Client::Input::InputState{});
        }
        RecordingFrameTarget target;
        scene.Render(target.BeginFrame(40ms * steps), 0.0f);
        target.Present();
        const std::optional<glm::vec3> raven = RavenCenterOf(target);
        if (!raven)
        {
            return std::nullopt;
        }
        return glm::length(glm::dvec3(*raven) - birth);
    };
    ASSERT_TRUE(scene.Enter().HasValue());
    const std::optional<double> born = distanceAfter(1);
    ASSERT_TRUE(born.has_value());
    EXPECT_LT(*born, Near);
    const std::optional<double> flown = distanceAfter(Flight);
    ASSERT_TRUE(flown.has_value());
    ASSERT_GT(*flown, Near) << "en la primera entrada se alejó";
    scene.Leave();

    ASSERT_TRUE(scene.Enter().HasValue());
    const std::optional<double> again = distanceAfter(1);
    ASSERT_TRUE(again.has_value());
    EXPECT_LT(*again, Near) << "nace otra vez en la celda (0, 0), a la altura del terreno más 300";
    scene.Leave();
}

TEST_F(GoldenLoginWorld, TheRavenBirthAndShadowUseTheSharedFixedHeight)
{
    Client::Tests::TemporaryFolder copy{"raven-shared-height"};
    ASSERT_NO_FATAL_FAILURE(MakeFixedHeightWorld(copy.Path()));
    const auto sample = [&](float height) -> std::optional<glm::vec2>
    {
        Client::Camera::GameCamera camera;
        camera.fixedHeight = height;
        Client::Camera::GameCameraInput cameraInput;
        Client::Models::ClothWorld cloth;
        LoginScene scene(SceneContext{.dataRoot = copy.Path(),
                                      .textures = m_store,
                                      .log = m_log,
                                      .cloth = &cloth,
                                      .camera = &camera,
                                      .cameraInput = &cameraInput},
                         *m_data, m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
        const auto entered = scene.Enter();
        if (!entered.HasValue())
        {
            ADD_FAILURE() << entered.Error();
            return std::nullopt;
        }
        // The first common step publishes the owner's visibility and moves its raven before the draw.
        scene.Step(Client::Input::InputState{});
        if (scene.Failure())
        {
            ADD_FAILURE() << *scene.Failure();
            return std::nullopt;
        }
        RecordingFrameTarget target;
        scene.Render(target.BeginFrame(std::chrono::milliseconds{0}), 0.0f);
        target.Present();
        const auto center = RavenCenterOf(target);
        const auto& events = target.frames.back().events;
        const auto frame = std::ranges::find(events, Kind::Models, &FrameEvent::kind);
        if (!center || frame == events.end())
        {
            ADD_FAILURE() << "no se dibujó el cuervo";
            scene.Leave();
            return std::nullopt;
        }
        const auto body = std::ranges::find_if(frame->modelCommands, [](const auto& command)
                                               { return command.state.texture == TextureSlot{0x3c9}; });
        if (body == frame->modelCommands.end() || std::next(body) == frame->modelCommands.end() ||
            std::next(body)->state.blend != Client::Render::ModelBlend::BlendedShadow)
        {
            ADD_FAILURE() << "falta la sombra después del cuervo";
            scene.Leave();
            return std::nullopt;
        }
        const float shadow = frame->modelVertices[std::next(body)->firstVertex].position.z;
        scene.Leave();
        return glm::vec2(center->z, shadow);
    };
    const auto before = sample(1200.0f);
    const auto after = sample(1201.0f);
    ASSERT_TRUE(before.has_value());
    ASSERT_TRUE(after.has_value());
    EXPECT_NEAR(after->x - before->x, 1.0f, 0.001f) << "el cuerpo nace una unidad más alto";
    EXPECT_EQ(before->y, 1205.0f);
    EXPECT_EQ(after->y, 1206.0f) << "la sombra queda cinco unidades sobre la altura compartida";
}

// CL7.6 C2: the raven's spark, one per step from a bone of the last draw's pose (Q-C133), is born with life 10 and
// the particles' step, at the end of the same step, already lowers it by one: each draw has 9, in the eye pass with
// Effect\Spark03.jpg (0x5c6), additive. Past the jump they surround the raven. Each step's scale and light are fixed
// by the vectors (ParticlesTest).
TEST_F(GoldenLoginWorld, TheRavenLeavesSparks)
{
    Client::Models::ClothWorld cloth;
    LoginScene scene(SceneContext{.dataRoot = m_dataRoot, .textures = m_store, .log = m_log, .cloth = &cloth}, *m_data,
                     m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    for (int step = 1; step <= 260; ++step)
    {
        scene.Step(Client::Input::InputState{});
        if (step % 10 != 0 && step < 250)
        {
            continue;
        }
        RecordingFrameTarget target;
        scene.Render(target.BeginFrame(std::chrono::milliseconds{40 * step}), 0.0f);
        target.Present();
        if (step < 250)
        {
            continue;
        }
        const std::optional<glm::vec3> raven = RavenCenterOf(target);
        ASSERT_TRUE(raven.has_value());
        std::vector<FrameEvent> models;
        std::ranges::copy_if(target.frames.back().events, std::back_inserter(models),
                             [](const FrameEvent& event) { return event.kind == Kind::Models; });
        ASSERT_EQ(models.size(), 2u) << "el mundo y el pase de ojo";
        ASSERT_TRUE(models.front().pass.has_value());
        const glm::dmat4 toWorld = glm::inverse(glm::dmat4(models.front().pass->view));
        std::size_t sparks = 0;
        for (const auto& command : models.back().modelCommands)
        {
            // The geometry-less effects go with the texture that was bound: a cloud that follows a spark changes the
            // blending (0x620b10) before binding its own (0x620780).
            if (command.state.texture != TextureSlot{0x5c6} ||
                command.operation != Client::Render::ModelStateOperation::Draw)
            {
                continue;
            }
            EXPECT_EQ(command.state.blend, Client::Render::ModelBlend::Additive);
            for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
            {
                glm::dvec3 center(0.0);
                for (std::size_t k = 0; k < 6; ++k)
                {
                    center += glm::dvec3(models.back().modelVertices[command.firstVertex + (quad * 6) + k].position);
                }
                const glm::dvec3 world = glm::dvec3(toWorld * glm::dvec4(center / 6.0, 1.0));
                EXPECT_LT(glm::length(world - glm::dvec3(*raven)), 400.0) << "el paso " << step;
                ++sparks;
            }
        }
        EXPECT_EQ(sparks, 9u) << "el paso " << step << ": una por paso, de 10 pasos de vida";
    }
    scene.Leave();
}

// CL7.6 C1: 0x4182f0 at the end of the character's draw (0x50ba30), before the horse: mesh 3, mesh 2 and the shadow
// of the five meshes (Q-C132), black, opaque and blended.
TEST_F(GoldenLoginWorld, TheRavenDrawsMeshes3And2WithItsShadow)
{
    Client::Models::ClothWorld cloth;
    LoginScene scene(SceneContext{.dataRoot = m_dataRoot, .textures = m_store, .log = m_log, .cloth = &cloth}, *m_data,
                     m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    const FrameEvent frame = Frame(scene, 3);
    const auto& commands = frame.modelCommands;
    const auto body =
        std::ranges::find_if(commands, [](const auto& command) { return command.state.texture == TextureSlot{0x26f}; });
    ASSERT_NE(body, commands.end()) << "la malla 3";
    const auto wing = std::next(body);
    ASSERT_NE(wing, commands.end());
    EXPECT_EQ(wing->state.texture, TextureSlot{0x3c9}) << "después, la malla 2";
    std::size_t shadows = 0;
    std::size_t shadowVertices = 0;
    for (auto it = std::next(wing);
         it != commands.end() && it->state.blend == Client::Render::ModelBlend::BlendedShadow; ++it)
    {
        ++shadows;
        shadowVertices += it->vertexCount;
        for (std::size_t k = 0; k < it->vertexCount; ++k)
        {
            const auto& vertex = frame.modelVertices[it->firstVertex + k];
            EXPECT_EQ(vertex.color.red, 0.0f);
            EXPECT_EQ(vertex.color.alpha, 1.0f);
        }
    }
    EXPECT_GE(shadows, 1u) << "la sombra, mezclada, después de las mallas";
    // Flattened onto the terrain height under the raven (0x600a0b): flat, and about 300 below it at the birth place
    // (450 + 300); the horse's rule would leave it at its height.
    float lowest = std::numeric_limits<float>::max();
    for (auto it = body; it != std::next(wing); ++it)
    {
        for (std::size_t k = 0; k < it->vertexCount; ++k)
        {
            lowest = std::min(lowest, frame.modelVertices[it->firstVertex + k].position.z);
        }
    }
    std::optional<float> flat;
    for (auto it = std::next(wing);
         it != commands.end() && it->state.blend == Client::Render::ModelBlend::BlendedShadow; ++it)
    {
        for (std::size_t k = 0; k < it->vertexCount; ++k)
        {
            const float z = frame.modelVertices[it->firstVertex + k].position.z;
            if (!flat)
            {
                flat = z;
            }
            EXPECT_EQ(z, *flat) << "plana";
        }
    }
    ASSERT_TRUE(flat.has_value());
    EXPECT_LT(*flat, lowest - 200.0f) << "en el suelo, no a la altura del cuervo";
    // The five meshes of DarkSpirit.bmd.
    EXPECT_EQ(shadowVertices, 3u * (288u + 96u + 111u + 300u + 206u));
    scene.Leave();
}

// The centre of each ember of the last frame, in its draw order (that of their places): corners 0 and 2 of its square
// are opposite.
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

TEST_F(GoldenLoginWorld, TheSceneTerrainUsesTheSharedFixedHeight)
{
    Client::Tests::TemporaryFolder copy{"terrain-shared-height"};
    ASSERT_NO_FATAL_FAILURE(MakeFixedHeightWorld(copy.Path()));
    Client::Camera::GameCamera camera;
    camera.fixedHeight = 1201.0f;
    Client::Camera::GameCameraInput cameraInput;
    LoginScene scene(
        SceneContext{
            .dataRoot = copy.Path(), .textures = m_store, .log = m_log, .camera = &camera, .cameraInput = &cameraInput},
        *m_data, m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    const FrameEvent frame = Frame(scene, 0);
    ASSERT_FALSE(frame.modelCommands.empty());
    const auto& terrain = frame.modelCommands.front();
    ASSERT_TRUE(terrain.state.texture.has_value());
    EXPECT_GE(terrain.state.texture->value, 0x23);
    EXPECT_LE(terrain.state.texture->value, 0x30);
    ASSERT_GT(terrain.vertexCount, 0u);
    const auto first = frame.modelVertices.begin() + static_cast<std::ptrdiff_t>(terrain.firstVertex);
    const auto last = first + static_cast<std::ptrdiff_t>(terrain.vertexCount);
    EXPECT_TRUE(std::all_of(first, last, [](const auto& vertex) { return vertex.position.z == 1201.0f; }));
    scene.Leave();
}

TEST_F(GoldenLoginWorld, TheSceneEmbersReadTheSharedGroundHeight)
{
    Client::Tests::TemporaryFolder copy{"embers-shared-height"};
    ASSERT_NO_FATAL_FAILURE(MakeFixedHeightWorld(copy.Path()));
    Client::Camera::GameCamera camera;
    camera.fixedHeight = 1201.0f;
    Client::Camera::GameCameraInput cameraInput;
    Client::Models::ClothWorld cloth;
    LoginScene scene(SceneContext{.dataRoot = copy.Path(),
                                  .textures = m_store,
                                  .log = m_log,
                                  .cloth = &cloth,
                                  .camera = &camera,
                                  .cameraInput = &cameraInput},
                     *m_data, m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    // Before the first draw, the startup's known hero is 0 and they are born below 1200.
    scene.Step(Client::Input::InputState{});
    scene.Step(Client::Input::InputState{});
    RecordingFrameTarget target;
    scene.Render(target.BeginFrame(std::chrono::milliseconds{80}), 0.0f);
    target.Present();
    const auto centers = EmberCentersOf(target);
    ASSERT_FALSE(centers.empty());
    EXPECT_GT(std::ranges::count_if(centers, [](const glm::vec3& p) { return p.z == 1201.0f; }), 0)
        << "las brasas con x/y positivos aterrizan en la altura global actual";
    scene.Leave();
}

TEST_F(GoldenLoginWorld, ASelectionDrawSetsTheHeroHeightUsedByTheFirstLoginEmbers)
{
    Client::Tests::TemporaryFolder copy{"selection-login-ember-height"};
    ASSERT_NO_FATAL_FAILURE(MakeFixedHeightWorld(copy.Path()));
    Client::Tests::WriteTextureSet(copy.Path(), Client::Assets::SelectionTextureSet);
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput cameraInput;
    cameraInput.hero = Client::Camera::LoginHeroPosition;
    Client::Models::ClothWorld cloth;
    const SceneContext context{.dataRoot = copy.Path(),
                               .textures = m_store,
                               .log = m_log,
                               .cloth = &cloth,
                               .camera = &camera,
                               .cameraInput = &cameraInput};
    Client::Tests::LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    // F3/00 list received with 0 records: the gate already allows drawing without slot 0.
    Client::Scenes::CharacterSelectScene selection(context, *m_data, m_models, &online.session,
                                                   &m_models.TextureSlots());
    ASSERT_TRUE(selection.Enter().HasValue());
    online.ReceiveTheList({});
    selection.Step(Client::Input::InputState{});
    RecordingFrameTarget selected;
    selection.Render(selected.BeginFrame(std::chrono::milliseconds{0}), 0.0f);
    selected.Present();
    EXPECT_EQ(cameraInput.hero.z, Client::FrontEnd::CharacterSlotHeight);
    selection.Leave();

    LoginScene login(context, *m_data, m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    ASSERT_TRUE(login.Enter().HasValue());
    // They are born with the Hero of the last selection draw, before the login's draw rewrites it.
    login.Step(Client::Input::InputState{});
    RecordingFrameTarget target;
    login.Render(target.BeginFrame(std::chrono::milliseconds{40}), 0.0f);
    target.Present();
    const auto centers = EmberCentersOf(target);
    ASSERT_EQ(centers.size(), 80u);
    EXPECT_TRUE(std::ranges::all_of(centers,
                                    [](const glm::vec3& center)
                                    {
                                        return center.z >= Client::FrontEnd::CharacterSlotHeight + 49.5f &&
                                               center.z <= Client::FrontEnd::CharacterSlotHeight + 1049.5f;
                                    }));
    // With HeroZ 333 all would be above 383; the fixed seed also observes the low end.
    EXPECT_TRUE(std::ranges::any_of(centers, [](const glm::vec3& center) { return center.z < 383.0f; }));
    login.Leave();
}

TEST_F(GoldenLoginWorld, AFailedLoginEntryRetainsItsTerrainBeforeDestruction)
{
    Client::Tests::TemporaryFolder copy{"failed-login-retains-cpu"};
    ASSERT_NO_FATAL_FAILURE(MakeFixedHeightWorld(copy.Path()));
    // The first scene texture fails, after the world loads correctly.
    const auto texture = Client::Tests::SceneTextureFile(Client::Assets::LoginTextureSet.front());
    ASSERT_TRUE(std::filesystem::remove(copy.Path() / texture));
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput input;
    Client::Kernel::WorldState worldState;
    Client::FrontEnd::SelectionState state(worldState);
    const SceneContext context{.dataRoot = copy.Path(),
                               .textures = m_store,
                               .log = m_log,
                               .camera = &camera,
                               .cameraInput = &input,
                               .selection = &state};
    {
        LoginScene scene(context, *m_data, m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
        EXPECT_FALSE(scene.Enter().HasValue());
        ASSERT_NE(state.Terrain(), nullptr);
        scene.Leave();
    }
    ASSERT_NE(state.Terrain(), nullptr);
    ASSERT_NE(state.Terrain()->heights, nullptr);
    ASSERT_NE(state.Terrain()->attributes, nullptr);
    EXPECT_EQ(state.Map(), 55);
    EXPECT_EQ(Client::Terrain::TerrainHeightAt(*state.Terrain()->heights, *state.Terrain()->attributes, 50.0f, 50.0f,
                                               camera.fixedHeight),
              1200.0);
}

// Render163 does not survive another creation of slot 0, even if the selection is not drawn again.
TEST_F(GoldenLoginWorld, ARecreatedSlotZeroSuppliesTheFirstLoginEmbersWithoutAnotherSelectionDraw)
{
    Client::Tests::TemporaryFolder copy{"recreated-selection-login"};
    ASSERT_NO_FATAL_FAILURE(MakeFixedHeightWorld(copy.Path()));
    std::error_code error;
    const auto world = Client::Data::ResolveDataPath(m_dataRoot, "World55", error);
    ASSERT_TRUE(world.has_value());
    std::filesystem::copy(*world, copy.Path() / "World55", std::filesystem::copy_options::recursive);
    const auto attributes = Client::Data::ResolveDataPath(copy.Path(), "World55/EncTerrain55.att", error);
    ASSERT_TRUE(attributes.has_value());
    auto plain = Client::Tests::PlainTerrainAttributes(55, Client::Tests::AttributeCells::Bytes);
    std::fill(plain.begin() + Client::Data::Terrain::TerrainAttributesHeaderSize, plain.end(), std::byte{0x40});
    Client::Tests::WriteSceneFile(copy.Path(), std::filesystem::relative(*attributes, copy.Path()),
                                  Client::Tests::EncryptTerrainAttributes(std::move(plain)));
    Client::Tests::WriteTextureSet(copy.Path(), Client::Assets::SelectionTextureSet);
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput cameraInput;
    // On the heap: WorldState weighs about 900 KB with the NPC windows (milestone 6) and, with the session and the two
    // draw targets, the frame does not fit in MSVC's 1 MB stack.
    const auto ownedWorld = std::make_unique<Client::Kernel::WorldState>();
    Client::Kernel::WorldState& worldState = *ownedWorld;
    Client::FrontEnd::SelectionState state(worldState);
    Client::Session::ModuleStates states;
    Client::Models::ClothWorld cloth;
    const SceneContext context{.dataRoot = copy.Path(),
                               .textures = m_store,
                               .log = m_log,
                               .cloth = &cloth,
                               .camera = &camera,
                               .cameraInput = &cameraInput,
                               .selection = &state,
                               .states = &states};
    {
        LoginScene login(context, *m_data, m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
        ASSERT_TRUE(login.Enter().HasValue());
        ASSERT_NE(state.Terrain(), nullptr);
        login.Leave();
    }
    ASSERT_NE(state.Terrain(), nullptr);
    EXPECT_EQ(state.Map(), 55);
    Client::Tests::LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    Client::Scenes::CharacterSelectScene selection(context, *m_data, m_models, &online.session,
                                                   &m_models.TextureSlots());
    ASSERT_TRUE(selection.Enter().HasValue());
    online.ReceiveTheList({{0, "char0001"}});
    ASSERT_TRUE(state.Apply(online.eventSink.TakeCharacterEvents(), camera, cameraInput).HasValue());
    EXPECT_EQ(cameraInput.hero.z, 1200.0f);
    EXPECT_EQ(state.Map(), 55);
    RecordingFrameTarget selected;
    selection.Render(selected.BeginFrame(std::chrono::milliseconds{0}), 0.0f);
    selected.Present();
    EXPECT_EQ(cameraInput.hero.z, 1200.0f);
    EXPECT_FALSE(state.IsInitialized());
    selection.Step(Client::Input::InputState{});
    ASSERT_TRUE(state.IsInitialized());
    EXPECT_EQ(state.Map(), 54);
    selection.Render(selected.BeginFrame(std::chrono::milliseconds{40}), 0.0f);
    selected.Present();
    EXPECT_EQ(cameraInput.hero.z, Client::FrontEnd::CharacterSlotHeight);
    camera.fixedHeight = 2000.0f;
    Client::Protocol::FrontEnd::CharacterEntry recreated;
    recreated.withoutEquipment = true;
    const std::array<Client::App::CharacterEvent, 1> events{Client::App::CharacterCreated{recreated}};
    ASSERT_TRUE(state.Apply(events, camera, cameraInput).HasValue());
    EXPECT_EQ(cameraInput.hero.z, 2000.0f);
    selection.Leave();
    LoginScene login(context, *m_data, m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    ASSERT_TRUE(login.Enter().HasValue());
    EXPECT_EQ(cameraInput.hero.z, 2000.0f);
    login.Step(Client::Input::InputState{});
    RecordingFrameTarget target;
    login.Render(target.BeginFrame(std::chrono::milliseconds{80}), 0.0f);
    target.Present();
    const auto centers = EmberCentersOf(target);
    ASSERT_EQ(centers.size(), 80u);
    EXPECT_TRUE(std::ranges::all_of(centers, [](const glm::vec3& center)
                                    { return center.z >= 2049.5f && center.z <= 3049.5f; }));
    EXPECT_TRUE(std::ranges::any_of(centers, [](const glm::vec3& center) { return center.z < 2383.0f; }));
    login.Leave();
}

// CL7.6 B3: the 80 embers (0x530490 → 0x42b1e0) are born around the hero, character 0, whose position 0x6350b0
// writes in the draw: on the first step, before drawing, they are born around (0, 0, 0) (Q-C137); after a draw,
// around (11679, 14191, 333), in 0x42b1e0's box. They go in the world pass, additive, with FireSnuff (0x635), after
// the dragon.
TEST_F(GoldenLoginWorld, TheEmbersAreBornAroundTheHero)
{
    Client::Models::ClothWorld cloth;
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
    const glm::vec3 hero = Client::Camera::LoginHeroPosition;
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

// CL7.6 B3: on entering, 0x6349a0 leaves the camera at (0, 0, 0) (0x634b80) and 0x635ae0 writes its y of 12678 only
// in the draw (0x635f7e). The first step's embers are born with the hero at (0, 0, 0) and 0x42b1e0's y between −500
// and 899: those below the camera + 400 go toward +x (3.2 minus the fall) and those at 400 or more, toward −x. With
// 12678, all would go toward +x. Those touching the ground do not move.
TEST_F(GoldenLoginWorld, TheFirstEmbersSeeTheCameraAtTheOrigin)
{
    Client::Models::ClothWorld cloth;
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

// Each entry loads the map again (0x61a560), and the load turns the leaves off (0x6070a0, the live byte of each entry
// of 0x75e34b8): the previous entry's embers do not go on (P-C27 i). On the first step the 80 are born again, in
// 0x42b1e0's box around the hero, who stayed where the last draw left it (0x6349a0 does not write its position).
TEST_F(GoldenLoginWorld, EnteringAgainTheEmbersAreBornAgain)
{
    using namespace std::chrono_literals;
    constexpr int Drift = 100;
    constexpr std::size_t Embers = 80;
    const glm::vec3 hero = Client::Camera::LoginHeroPosition;
    const auto bornAroundTheHero = [&hero](const glm::vec3& center)
    {
        const glm::vec3 offset = center - hero;
        return offset.x >= -800.5f && offset.x <= 799.5f && offset.y >= -500.5f && offset.y <= 899.5f &&
               offset.z >= 49.5f && offset.z <= 1049.5f;
    };
    Client::Models::ClothWorld cloth;
    LoginScene scene(SceneContext{.dataRoot = m_dataRoot, .textures = m_store, .log = m_log, .cloth = &cloth}, *m_data,
                     m_models, nullptr, Version, FixedSeed, &m_models.TextureSlots());
    ASSERT_TRUE(scene.Enter().HasValue());
    scene.Step(Client::Input::InputState{});
    DrawAt(scene, 40ms);
    for (int step = 0; step < Drift; ++step)
    {
        scene.Step(Client::Input::InputState{});
    }
    RecordingFrameTarget drifted;
    scene.Render(drifted.BeginFrame(40ms * (Drift + 1)), 0.0f);
    drifted.Present();
    const std::vector<glm::vec3> before = EmberCentersOf(drifted);
    ASSERT_EQ(before.size(), Embers);
    ASSERT_FALSE(std::ranges::all_of(before, bornAroundTheHero)) << "en la primera entrada se alejaron";
    scene.Leave();

    ASSERT_TRUE(scene.Enter().HasValue());
    scene.Step(Client::Input::InputState{});
    RecordingFrameTarget born;
    scene.Render(born.BeginFrame(40ms), 0.0f);
    born.Present();
    const std::vector<glm::vec3> after = EmberCentersOf(born);
    ASSERT_EQ(after.size(), Embers);
    EXPECT_TRUE(std::ranges::all_of(after, bornAroundTheHero)) << "las 80 nacieron en este paso";
    scene.Leave();
}

// The dragon advances per step (0x52d220, 25 per second), not per frame: several frames without steps do not move
// it, at any FPS.
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

// Each entry empties the effects table (0x6070a0) and the map load creates the dragon again (0x606660 → 0x518cf0):
// the second entry's first frame has it where the first did, even though it flew in between.
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

// The selection notices' texts that are empty in the lab's Text.bmd.
constexpr std::array<std::size_t, 6> SelectionEmptyTexts{1711, 1712, 1713, 1714, 1715, 1716};

// The windows' texts (CL5.5), by index: those of each notice the login opens (the F1 01's from 0x15 to 0x26 and its
// own), the account labels (450 and 451), the two titles, the three loads and the name of each group with a button.
// All are there and none is empty; if one fails, its index comes out and not the text.
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

    // The selection notices' ones that this Text.bmd leaves empty (TheSelectionLeavesSixTextsEmpty).
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

// Texts 1711 to 1716 of the selection notices (0x83, 0x5e, 0x5f, 0x60, 0x72 and 0x36) are empty in the lab's
// Text.bmd: those boxes come out without text, as in the original.
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
