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
#include "Scenes/ActorState.h"
#include "Scenes/LoginModels.h"
#include "Scenes/MapLoad.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/PanelRegistry.h"

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

// La ventana /move por GameScene (CL8 hito 6; 06 § HUD y ventanas, "La ventana /move"): el clic en un destino arma el
// texto 260 de Text.bmd, un espacio y el nombre (0x4487d7..0x4488d3), y lo manda por el chat. En el original el 260
// se lee de la tabla global (0x4487e7: [0x76d028c], el 260 de la de 0x76bd1dc), que siempre está cargada; en MuRet
// llega por el contexto que arma GameScene. El destino, el héroe y los textos son inventados.
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
    auto world = std::make_unique<Client::Scenes::WorldState>();
    Client::Models::ClothWorld cloth;
    const std::filesystem::path dataRoot = Client::Tests::RepoDataDir();
    const Client::Tables::TextTable texts = Client::Tests::MakeTextTable(Client::Tests::ChatTexts());
    // Lorencia cargada: el paso sigue después de la UI (las hojas leen el terreno).
    world->Runtime().map = 0;
    const auto map = Client::Scenes::LoadMap(0, {dataRoot, *world, Client::Scenes::SceneId::Game, 0, cloth.random});
    ASSERT_TRUE(map.HasValue()) << map.Error();
    world->Actors()[0].position = {13550.0f, 12550.0f, 0.0f};
    world->OwnCharacter().ClearEquipmentForEntry();
    // El cuerpo del héroe (0x4f9580) pide el esqueleto de Player.bmd.
    Client::Scenes::SingleModelLibrary models;
    models.Set(Client::Tables::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
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

    // La ventana abierta con un destino que pasa (nivel 10 y 100 de zen contra 20 y 500) en el primer renglón: x en
    // [3, 188) e y en [40, 54) con 640 de ancho (test_game_move_window).
    Client::Scenes::WorldRuntime& runtime = world->Runtime();
    runtime.drawingEnabled = true;
    runtime.panels.overlayMode = Client::UI::MoveOverlayMode;
    runtime.moves = {Client::Tables::MoveRequirement{
        .index = 1, .name = "Campo", .searchName = {}, .level = 10, .zen = 100, .gate = 0}};
    world->OwnCharacter().level = 20;
    world->OwnCharacter().zen = 500;
    auto& hero = world->Actors()[runtime.hero];
    hero = Client::Tests::UnarmedPlayer();
    hero.alive = true;
    hero.name = Client::Tests::NameBytes("Heroe");
    const std::size_t sentBefore = online.socket.sent.size();
    Client::Input::InputState input;
    input.Apply({.kind = Client::Input::InputEvent::Kind::Motion, .x = 50.0f, .y = 40.0f});
    input.Apply({.kind = Client::Input::InputEvent::Kind::ButtonDown, .button = Client::Input::MouseButton::Left});

    scene.Step(input);

    ASSERT_FALSE(scene.Failure().has_value()) << *scene.Failure();
    EXPECT_NE(runtime.panels.overlayMode, Client::UI::MoveOverlayMode);
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
