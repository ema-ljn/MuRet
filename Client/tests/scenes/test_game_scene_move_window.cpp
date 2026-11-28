#include "Network/Xor32Packet.h"
#include "Scenes/GameScene.h"

#include "Tables/PlayerModels.h"
#include "Tables/MoveRequirements.h"
#include "Tables/TextTable.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Network/PacketSerials.h"
#include "Models/Cloth.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Kernel/ActorState.h"
#include "FrontEnd/LoginModels.h"
#include "Session/MapLoad.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Scenes/SceneContext.h"
#include "Kernel/SceneId.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/PanelRegistry.h"
#include "Session/ModuleStates.h"

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

// The /move window through GameScene (CL8 milestone 6; 06 § HUD y ventanas, "La ventana /move"): clicking a
// destination builds Text.bmd's text 260, a space and the name (0x4487d7..0x4488d3), and sends it through the chat.
// In the original the 260 is read from the global table (0x4487e7: [0x76d028c], the 260 of 0x76bd1dc's), which is
// always loaded; in MuRet it comes through the context GameScene builds. The destination, hero and texts are made up.
namespace
{
class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind /*font*/,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

TEST(GameSceneMoveWindowTest, AClickOnADestinationSaysTheMoveTextOfTheScene)
{
    Client::Tests::LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    online.session.RequestCharacterList();
    online.ReceiveTheList({{0, "Heroe"}});
    online.session.ChooseCharacter(std::uint8_t{0});
    ASSERT_NE(online.session.ChosenCharacter(), nullptr);

    Client::Tests::FakeTextureStore store;
    auto world = std::make_unique<Client::Kernel::WorldState>();
    Client::Session::ModuleStates states;
    Client::Models::ClothWorld cloth;
    const std::filesystem::path dataRoot = Client::Tests::RepoDataDir();
    const Client::Tables::TextTable texts = Client::Tests::MakeTextTable(Client::Tests::ChatTexts());
    // Lorencia loaded: the step goes on after the UI (the leaves read the terrain).
    world->Runtime().map = 0;
    const auto map =
        Client::Scenes::LoadMap(0, {dataRoot, *world, states.terrain, Client::Kernel::SceneId::Game, 0, cloth.random});
    ASSERT_TRUE(map.HasValue()) << map.Error();
    world->Actors()[0].position = {13550.0f, 12550.0f, 0.0f};
    world->OwnCharacter().ClearEquipmentForEntry();
    // The hero's body (0x4f9580) asks for Player.bmd's skeleton.
    Client::FrontEnd::SingleModelLibrary models;
    models.Set(Client::Tables::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
    LetterMeasure letters;
    Client::Scenes::GameScene scene(Client::Scenes::SceneContext{.dataRoot = dataRoot,
                                                                 .textures = store,
                                                                 .log = online.log,
                                                                 .cloth = &cloth,
                                                                 .world = world.get(),
                                                                 .textMeasure = &letters,
                                                                 .states = &states},
                                    &online.session, &models, nullptr, nullptr, nullptr, nullptr, &texts);
    const auto entered = scene.Enter();
    ASSERT_TRUE(entered.HasValue()) << entered.Error();
    scene.Step(Client::Input::InputState{});
    ASSERT_FALSE(scene.Failure().has_value()) << *scene.Failure();

    // The window open with a destination that passes (level 10 and 100 zen against 20 and 500) in the first row: x in
    // [3, 188) and y in [40, 54) at 640 wide (test_game_move_window).
    Client::Kernel::WorldRuntime& runtime = world->Runtime();
    runtime.drawingEnabled = true;
    runtime.panels.overlayMode = Client::Ui::MoveOverlayMode;
    runtime.moves = {Client::Tables::MoveRequirement{
        .index = 1, .name = "Campo", .searchName = {}, .level = 10, .zen = 100, .gate = 0}};
    world->OwnCharacter().level = 20;
    world->OwnCharacter().zen = 500;
    auto& hero = world->Hero();
    hero = Client::Tests::UnarmedPlayer();
    hero.alive = true;
    hero.name = Client::Tests::NameBytes("Heroe");
    const std::size_t sentBefore = online.socket.sent.size();
    Client::Input::InputState input;
    input.Apply({.kind = Client::Input::InputEvent::Kind::Motion, .x = 50.0f, .y = 40.0f});
    input.Apply({.kind = Client::Input::InputEvent::Kind::ButtonDown, .button = Client::Input::MouseButton::Left});

    scene.Step(input);

    ASSERT_FALSE(scene.Failure().has_value()) << *scene.Failure();
    EXPECT_NE(runtime.panels.overlayMode, Client::Ui::MoveOverlayMode);
    Client::Network::PacketSerials serials;
    const auto received =
        Client::Tests::AsTheGameServerReceives(std::span(online.socket.sent).subspan(sentBefore), serials);
    ASSERT_EQ(received.packets.size(), 1u);
    EXPECT_TRUE(Client::Tests::SameBytes(
        Client::Network::Xor32Decode(received.packets[0]).Bytes(),
        Client::Tests::Bytes({0xc1, 0x1a, 0x00, 'H', 'e', 'r', 'o', 'e', 0,   0,   0,   0,   0,
                              '/',  'm',  'o',  'v', 'e', 'r', ' ', 'C', 'a', 'm', 'p', 'o', 0})));
}
} // namespace
