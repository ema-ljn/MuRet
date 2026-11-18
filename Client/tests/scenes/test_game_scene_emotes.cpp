#include "Scenes/GameScene.h"

#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Text/TextTable.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Network/Crypto/Xor32.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/ModelTextureSlots.h"
#include "Render/Models/Skeleton.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/LoginModels.h"
#include "Scenes/SceneContext.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include "support/Bytes.h"
#include "support/CharacterPackets.h"
#include "support/ChatTexts.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/PlayerActors.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"
#include "support/SessionHarness.h"
#include "support/TestGameServer.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string_view>

// El chat atraviesa GameScene antes del primer dibujo: el modelo del transformado vive en fauna y Player en la
// biblioteca base. Consultar sólo la base cortaría en 0x576abf antes de los dos C1 18 y del chat (06 § El emote).
namespace
{
constexpr std::uint16_t TransformedModel = 0x171;
constexpr std::int32_t Monster32Resource = 0x1f;
constexpr std::int32_t LorenciaMap = 0;
constexpr Client::Render::TextureSlot FaunaFirstTexture{0x500};

class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind /*font*/,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

TEST(GameSceneEmoteTest, ATransformedHeroSendsBothActionsAndTheChatBeforeTheFirstDraw)
{
    Client::Tests::LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    online.session.RequestCharacterList();
    online.ReceiveTheList({{0, "Heroe"}});
    online.session.ChooseCharacter(std::uint8_t{0});
    ASSERT_NE(online.session.ChosenCharacter(), nullptr);

    // Fauna conserva el backend: store vive más que world, también si un ASSERT termina el test antes.
    Client::Tests::FakeTextureStore store;
    auto world = std::make_unique<Client::Scenes::WorldState>();
    Client::Render::Models::ClothWorld cloth;
    Client::Scenes::SingleModelLibrary models;
    models.Set(Client::Data::Models::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
    const std::filesystem::path dataRoot = Client::Tests::RepoDataDir();
    const Client::Data::Text::TextTable texts = Client::Tests::MakeTextTable(Client::Tests::ChatTexts());
    LetterMeasure letters;
    Client::Scenes::GameScene scene(Client::Scenes::SceneContext{.dataRoot = dataRoot,
                                                                 .textures = store,
                                                                 .log = online.log,
                                                                 .cloth = &cloth,
                                                                 .world = world.get(),
                                                                 .textMeasure = &letters},
                                    &online.session, &models, nullptr, nullptr, nullptr, nullptr, &texts);
    const auto entered = scene.Enter();
    ASSERT_TRUE(entered.HasValue()) << entered.Error();
    scene.Step(Client::Input::InputState{});
    ASSERT_FALSE(scene.Failure().has_value()) << *scene.Failure();
    ASSERT_EQ(online.session.State(), Client::App::SessionState::EnteringWorld);
    ASSERT_FALSE(world->Runtime().drawingEnabled);

    Client::Render::Models::ModelTextureSlots slots{FaunaFirstTexture};
    Client::Scenes::FaunaModelLoadContext load{dataRoot, store, online.log, slots};
    const auto loaded = world->FaunaResources().LoadMonster(Monster32Resource, LorenciaMap, load);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    ASSERT_EQ(models.SkeletonOf(TransformedModel), nullptr);
    const auto* skeleton = world->FaunaResources().SkeletonOf(TransformedModel);
    ASSERT_NE(skeleton, nullptr);
    ASSERT_EQ(skeleton->actions.size(), 8u);
    auto& hero = world->Actors()[world->Runtime().hero];
    hero = Client::Tests::UnarmedPlayer();
    hero.alive = true;
    hero.type = TransformedModel;
    hero.name = Client::Tests::NameBytes("Heroe");
    hero.animation.action = 1;
    hero.angles.z = 100.0f;
    const std::size_t sentBefore = online.socket.sent.size();
    Client::Input::InputState input;
    input.Apply(Client::Input::InputEvent{.kind = Client::Input::InputEvent::Kind::Text, .text = "\rhola ^^\r"});

    scene.Step(input);

    ASSERT_FALSE(scene.Failure().has_value()) << *scene.Failure();
    EXPECT_FALSE(world->Runtime().drawingEnabled);
    EXPECT_EQ(hero.animation.action, 1); // 0x4ea030 no cambia a 0xaf: Monster32 sólo tiene ocho acciones.
    Client::Network::PacketSerials serials;
    const auto received =
        Client::Tests::AsTheGameServerReceives(std::span(online.socket.sent).subspan(sentBefore), serials);
    ASSERT_EQ(received.packets.size(), 3u);
    EXPECT_TRUE(Client::Tests::SameBytes(Client::Network::Crypto::Xor32Decode(received.packets[0]).Bytes(),
                                         Client::Tests::Bytes({0xc1, 5, 0x18, 3, 0x8e})));
    EXPECT_TRUE(Client::Tests::SameBytes(Client::Network::Crypto::Xor32Decode(received.packets[1]).Bytes(),
                                         Client::Tests::Bytes({0xc1, 5, 0x18, 3, 0x8e})));
    EXPECT_TRUE(Client::Tests::SameBytes(Client::Network::Crypto::Xor32Decode(received.packets[2]).Bytes(),
                                         Client::Tests::Bytes({0xc1, 0x15, 0x00, 'H', 'e', 'r', 'o', 'e', 0,   0, 0,
                                                               0,    0,    'h',  'o', 'l', 'a', ' ', '^', '^', 0})));
}
} // namespace
