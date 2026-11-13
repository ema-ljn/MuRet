#include "Scenes/GamePartyWindow.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Protocol/Party.h"
#include "Protocol/Viewport.h"
#include "Render/Models/Cloth.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameProjection.h"
#include "Scenes/GameUi.h"
#include "Scenes/LoginModels.h"
#include "Scenes/SelectionState.h"
#include "Scenes/SocialEvents.h"
#include "Scenes/SocialState.h"
#include "Scenes/ViewportPlayers.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/Widget.h"
#include "World/GameCamera.h"
#include "World/WorldTerrain.h"

#include "support/GoldenDir.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/RecordingTextDrawer.h"
#include "support/ViewportPackets.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// La vida de la party sobre las cabezas (0x5a89b0, CL9 hito 7, fase 2) con los otros jugadores que trae el 0x12 (hito
// 1) y el picking del frame (0x59ec80), en Lorencia (0) y Devias (2). Los nombres son inventados.
namespace
{
using Client::Data::Models::PlayerSkeletonModel;
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Tests::ViewportPlayerRecord;

constexpr std::uint32_t Seed = 0x5eed;
constexpr std::uint16_t HeroKey = 33;
constexpr int NoTexture = -1;
constexpr int Marker = -2;

std::uint8_t Byte(int value)
{
    return static_cast<std::uint8_t>(value);
}

void Name(std::span<std::byte> name, std::string_view text)
{
    for (std::size_t at = 0; at < name.size(); ++at)
        name[at] = at < text.size() ? static_cast<std::byte>(text[at]) : std::byte{0};
}

// El mundo del juego, en el heap.
struct Game
{
    Game()
    {
        models.Set(PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
        ActorState& hero = world.Actors()[0];
        hero = Client::Tests::UnarmedPlayer();
        hero.alive = true;
        hero.key = HeroKey;
        hero.characterClass = 1;
        Name(hero.name, "Tejon");
        world.Runtime().hero = 0;
    }
    Client::Scenes::WorldEntryResources Resources()
    {
        return {.world = world,
                .selection = selection,
                .dataRoot = {},
                .cloth = cloth,
                .camera = camera,
                .cameraInput = input,
                .models = models,
                .items = &items,
                .itemSetTables = &sets,
                .log = log};
    }
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection{world};
    Client::World::GameCamera camera;
    Client::World::GameCameraInput input;
    Client::Render::Models::ClothWorld cloth{.random = CrtRandom(Seed)};
    Client::Scenes::SingleModelLibrary models;
    Client::Data::Tables::ItemTable items = Client::Tests::ItemsMarking({});
    Client::Data::Tables::ItemSetTables sets =
        Client::Tests::ItemSetTablesFrom(std::vector<std::byte>(Client::Data::Tables::ItemSetTypeTableSize),
                                         std::vector<std::byte>(Client::Data::Tables::ItemSetOptionTableSize));
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
};

// Una proyección de prueba: el ojo en (x − 10.000, y − 10.000, −1.000), sin giro ni altura, con las escalas en −0,01:
// 10 unidades del mundo por punto desde el centro de 640 × 480.
Client::Scenes::GameProjection TestProjection()
{
    return Client::Scenes::GameProjection{
        .view = {1.0f, 0.0f, 0.0f, -10000.0f, 0.0f, 1.0f, 0.0f, -10000.0f, 0.0f, 0.0f, 0.0f, -1000.0f},
        .horizontalScale = -0.01f,
        .verticalScale = -0.01f,
        .centerX = 320,
        .centerY = 240,
        .screenWidth = 640,
        .screenHeight = 480};
}

struct DrawnQuad
{
    int slot = NoTexture;
    BlendMode blend = BlendMode::Alpha;
    Color color{};
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

std::vector<DrawnQuad> QuadsOf(const SpriteBatch& sprites)
{
    std::vector<DrawnQuad> quads;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        int slot = NoTexture;
        if (command.texture == Client::Tests::TextMarkerSlot)
            slot = Marker;
        else if (command.texture)
            slot = command.texture->value;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const auto& first = vertices[command.firstVertex + quad * 6];
            const auto& third = vertices[command.firstVertex + quad * 6 + 2];
            quads.push_back(DrawnQuad{.slot = slot,
                                      .blend = command.blend,
                                      .color = first.color,
                                      .x = first.x,
                                      .y = 480.0f - first.y,
                                      .width = third.x - first.x,
                                      .height = first.y - third.y});
        }
    }
    return quads;
}

class PartyLifeTest : public Client::Tests::GoldenDataTest, public ::testing::WithParamInterface<std::uint8_t>
{
protected:
    void SetUp() override
    {
        GoldenDataTest::SetUp();
        game = std::make_unique<Game>();
        const std::uint8_t map = GetParam();
        auto loaded = Client::World::LoadWorldTerrain(m_dataRoot, map);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        game->world.Map().terrain = std::move(loaded).Value();
        game->world.Runtime().map = map;
        m_sprites.Begin(Client::App::PixelSize{640, 480});
    }

    // La primera fila libre de cinco celdas desde (100, 100).
    [[nodiscard]] std::array<std::uint8_t, 2> FreeRow() const
    {
        const auto& cells = game->world.Map().terrain.attributes->cells;
        for (int y = 100; y < 200; ++y)
            for (int x = 100; x < 200; ++x)
            {
                bool free = true;
                for (int step = 0; step < 5 && free; ++step)
                    free = cells[Client::Data::Terrain::CellIndex(Byte(x + step), Byte(y))] == 0;
                if (free)
                    return {Byte(x), Byte(y)};
            }
        ADD_FAILURE() << "el mapa no tiene una fila libre";
        return {};
    }

    // El 0x12 con dos jugadores recién llegados en la fila libre, y lo que el paso les hace antes del primer dibujo
    // (0x508f00 los ve y el alpha sube; los dos, sin portar acá).
    void TwoPlayersArrive()
    {
        const auto [x, y] = FreeRow();
        const std::array<ViewportPlayerRecord, 2> records{
            {{.key = 0x0456, .isNew = true, .x = x, .y = y, .name = "Brezal", .targetX = x, .targetY = y},
             {.key = 0x0457,
              .isNew = true,
              .x = Byte(x + 2),
              .y = y,
              .name = "Zorzal",
              .targetX = Byte(x + 2),
              .targetY = y}}};
        auto read = Client::Protocol::Viewport::ReadPlayers(Client::Tests::ViewportPlayersPacket(records));
        ASSERT_TRUE(read.HasValue());
        const auto applied = Client::Scenes::ApplyViewportPlayers(read.Value(), game->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        for (const std::size_t slot : {std::size_t{1}, std::size_t{2}})
        {
            game->world.Actors()[slot].seen = true;
            game->world.Actors()[slot].alpha = 1.0f;
        }
    }

    // El 42 con los tres y el 44 con sus décimos.
    void ThePartyArrives()
    {
        Client::Protocol::Party::PartyList list;
        for (const std::string_view name :
             {std::string_view{"Zorzal"}, std::string_view{"Tejon"}, std::string_view{"Brezal"}})
        {
            Client::Protocol::Party::PartyMember member{
                .number = static_cast<std::uint8_t>(list.members.size()), .life = 10, .maxLife = 20};
            Name(member.name, name);
            list.members.push_back(member);
        }
        Client::Scenes::ApplyPartyList(list, game->world, game->log);
        const Client::Protocol::Party::PartyLife life{.entries = {0x07, 0x1a, 0x2a}};
        Client::Scenes::ApplyPartyLife(life, game->world, game->log);
    }

    // 0x59ec80 antes del primer dibujo: sin segmento.
    void Pick()
    {
        auto& runtime = game->world.Runtime();
        const auto picked = Client::Scenes::PickInTheFrame({.world = game->world,
                                                            .random = m_random,
                                                            .ownRecordDepth = runtime.ownRecordDepth,
                                                            .targets = runtime.selectedTargets,
                                                            .mouse = m_mouse});
        ASSERT_TRUE(picked.HasValue()) << picked.Error();
    }

    Client::Core::Expected<void, std::string> DrawTheLife()
    {
        auto& runtime = game->world.Runtime();
        ActorState& hero = game->world.Actors()[runtime.hero];
        Client::Scenes::GameHudContext context{.world = game->world,
                                               .random = m_random,
                                               .ownRecordDepth = runtime.ownRecordDepth,
                                               .cellXDepth = hero.cellXAccesses,
                                               .cellYDepth = hero.cellYAccesses,
                                               .hud = runtime.hud,
                                               .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                                               .sound = m_sound,
                                               .projection = m_projection ? &*m_projection : nullptr,
                                               .sprites = m_sprites,
                                               .text = m_text,
                                               .mouse = m_mouse};
        return Client::Scenes::DrawThePartyLife(context);
    }

    std::unique_ptr<Game> game;
    CrtRandom m_random{Seed};
    std::optional<std::int32_t> m_sound{};
    std::optional<Client::Scenes::GameProjection> m_projection = TestProjection();
    SpriteBatch m_sprites;
    Client::Tests::RecordingTextDrawer m_text{m_sprites};
    Client::UI::Point m_mouse{};
};

// Los dos del 0x12 encuentran su lugar en el picking (el héroe también, que se ve) y cada uno con personaje tiene su
// barra sobre la cabeza: la sombra con la mezcla normal y, sumados, el borde, el fondo y sus décimos. Sin rand().
TEST_P(PartyLifeTest, TheMembersInTheViewportShowTheirLifeOverTheirHeads)
{
    TwoPlayersArrive();
    ThePartyArrives();
    Pick();
    const auto& slots = game->world.Runtime().social.party.slots;
    EXPECT_EQ(slots[0].actorIndex, 2);
    EXPECT_EQ(slots[1].actorIndex, -3) << "el héroe de UnarmedPlayer no está a la vista";
    EXPECT_EQ(slots[2].actorIndex, 1);
    const CrtRandom before = m_random;
    ASSERT_TRUE(DrawTheLife().HasValue());
    EXPECT_EQ(m_random.State(), before.State());

    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    // Zorzal (7 décimos) y Brezal (10 de 0xa): sombra, borde, fondo y los décimos.
    ASSERT_EQ(quads.size(), (3U + 7U) + (3U + 10U));
    const ActorState& zorzal = game->world.Actors()[2];
    const auto above = Client::Scenes::ProjectGamePoint(
        TestProjection(), {zorzal.position.x, zorzal.position.y, zorzal.boundsTop + zorzal.position.z + 100.0f});
    ASSERT_TRUE(above.HasValue());
    const auto x = static_cast<float>(above.Value().x - 19);
    const auto y = static_cast<float>(above.Value().y);
    EXPECT_EQ(quads[0].blend, BlendMode::Alpha);
    EXPECT_EQ(quads[0].color, (Color{0.0f, 0.0f, 0.0f, 0.5f}));
    EXPECT_EQ(quads[0].x, x + 1.0f);
    EXPECT_NEAR(quads[0].y, y + 1.0f, 1e-3f);
    EXPECT_EQ(quads[0].width, 42.0f);
    EXPECT_EQ(quads[1].blend, BlendMode::Additive);
    EXPECT_EQ(quads[1].color, (Color{0.2f, 0.0f, 0.0f, 1.0f}));
    EXPECT_EQ(quads[1].x, x);
    EXPECT_EQ(quads[2].width, 38.0f);
    EXPECT_EQ(quads[2].height, 1.0f);
    EXPECT_EQ(quads[9].x, x + 2.0f + 6.0f * 4.0f);
    EXPECT_NEAR(quads[9].y, y + 2.0f, 1e-3f);
    EXPECT_EQ(m_sprites.Blend(), BlendMode::Opaque);
    EXPECT_TRUE(m_text.texts.empty());
}

// 0x5a8a42..0x5a8b3e: con el mouse en [x, x + 38) × [y − 2, y + 6), "HP : %d0%" 6 arriba, antes de la barra.
TEST_P(PartyLifeTest, TheMouseOverABarShowsTheTenths)
{
    TwoPlayersArrive();
    ThePartyArrives();
    Pick();
    const ActorState& brezal = game->world.Actors()[1];
    const auto above = Client::Scenes::ProjectGamePoint(
        TestProjection(), {brezal.position.x, brezal.position.y, brezal.boundsTop + brezal.position.z + 100.0f});
    ASSERT_TRUE(above.HasValue());
    const int x = above.Value().x - 19;
    const int y = above.Value().y;
    m_mouse = {.x = x + 37, .y = y + 5};
    ASSERT_TRUE(DrawTheLife().HasValue());
    ASSERT_EQ(m_text.texts.size(), 1U);
    EXPECT_EQ(m_text.texts[0].text, "HP : 100%");
    EXPECT_EQ(m_text.texts[0].x, x);
    EXPECT_EQ(m_text.texts[0].y, y - 6);

    m_sprites.Begin(Client::App::PixelSize{640, 480});
    m_text.texts.clear();
    m_mouse = {.x = x + 38, .y = y + 5};
    ASSERT_TRUE(DrawTheLife().HasValue());
    EXPECT_TRUE(m_text.texts.empty());
}

// Sin la proyección del pase del mundo, un miembro con personaje corta; sin ninguno, nada.
TEST_P(PartyLifeTest, WithoutTheProjectionAMemberInViewCuts)
{
    ThePartyArrives();
    Pick();
    m_projection.reset();
    ASSERT_TRUE(DrawTheLife().HasValue());
    EXPECT_TRUE(QuadsOf(m_sprites).empty());
    TwoPlayersArrive();
    Pick();
    const auto cut = DrawTheLife();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x5a8a2e"), std::string::npos) << cut.Error();
}

INSTANTIATE_TEST_SUITE_P(Maps, PartyLifeTest, ::testing::Values(std::uint8_t{0}, std::uint8_t{2}));
} // namespace
