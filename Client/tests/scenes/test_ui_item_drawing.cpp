#include "Scenes/UiItemDrawing.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Data/Tables/ItemTable.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/GameItemSpecialDrawing.h"
#include "Scenes/GameProjection.h"
#include "support/TableFiles.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_int2.hpp>
#include <glm/ext/vector_int4.hpp>
#include <glm/trigonometric.hpp>
#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

// El ítem 3D de la UI (CL8 hito 4, 06 § El inventario, "El ítem de la UI"): la cámara de 0x620480 en el pase de
// 0x5ac120, el punto de la caja de 0x5e1480, la pose de 0x5e0720 y el dibujo con el pase abierto por el primero. Los
// valores esperados salen de la x87 a mano (los floats con sus bits); los modelos son inventados.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::UiItemBox;
using Client::Scenes::UiItemCall;
using Client::Scenes::UiItemPlacement;
using Client::Scenes::UnprojectedPoint;

float Bits(std::uint32_t bits)
{
    return std::bit_cast<float>(bits);
}

std::uint32_t BitsOf(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}

// Item.bmd con el byte de las dos manos (+0x1e) del tipo `type` en `twoHands`.
Client::Data::Tables::ItemTable ItemsWithTwoHands(std::size_t type, std::uint8_t twoHands)
{
    using Client::Data::Tables::ItemRecordSize;
    std::vector<std::byte> plain(Client::Data::Tables::ItemTableSize);
    constexpr std::size_t TwoHands = 0x1e;
    plain[type * ItemRecordSize + TwoHands] = std::byte{twoHands};
    const std::vector<std::byte> encrypted = Client::Tests::EncryptRecords(std::move(plain), ItemRecordSize);
    return Client::Data::Tables::ItemTable(std::span<const std::byte>(encrypted));
}

// Item.bmd con todo en 0: las armas, con una mano.
const Client::Data::Tables::ItemTable& PlainItems()
{
    static const Client::Data::Tables::ItemTable items = ItemsWithTwoHands(0, 0);
    return items;
}

// La cámara de la UI en el origen y el punto (481, 251) de 640 × 480: (11,708546; −0,7999627; −2.000).
UnprojectedPoint Anchor640()
{
    return UnprojectedPoint{.camera = glm::vec3(0.0f),
                            .point = glm::vec3(Bits(0x413b5634u), Bits(0xbf4cca5bu), -2000.0f)};
}

Client::Core::Expected<UiItemPlacement, std::string> Place(std::int32_t model, std::int32_t level, glm::vec3& angles,
                                                           bool hovered = false, bool always = false,
                                                           float clock = 0.0f,
                                                           const Client::Data::Tables::ItemTable* items = &PlainItems())
{
    return Client::Scenes::PlaceUiItem(UiItemCall{.model = model, .level = level}, Anchor640(), hovered, always, clock,
                                       items, angles);
}
} // namespace

// 0x5ac120: 0x6203c0(1°, W / H, 20, 2.000) en la pantalla entera y la vista en la identidad. Las escalas y el centro
// de 0x6203c0 y 0x620d10 con los bits de la x87.
TEST(UiItemDrawingTest, TheUiPassCoversTheWholeScreenWithOneDegree)
{
    struct Case
    {
        Client::App::PixelSize screen;
        std::uint32_t horizontal;
        std::uint32_t vertical;
    };
    for (const Case& probe : {Case{{640, 480}, 0x38188357u, 0x38188356u}, Case{{1024, 768}, 0x37bea42cu, 0x37bea42cu}})
    {
        SCOPED_TRACE(probe.screen.width);
        const auto made = Client::Scenes::MakeUiItemPass(probe.screen);
        ASSERT_TRUE(made.HasValue()) << made.Error();
        const Client::Scenes::UiItemPass& pass = made.Value();
        EXPECT_EQ(pass.pass.viewport, glm::ivec4(0, 0, probe.screen.width, probe.screen.height));
        EXPECT_EQ(pass.pass.view, glm::mat4(1.0f));
        EXPECT_FLOAT_EQ(pass.pass.farPlane, 2000.0f);
        const float aspect = static_cast<float>(static_cast<double>(probe.screen.width) / probe.screen.height);
        EXPECT_EQ(pass.pass.projection, glm::perspective(glm::radians(1.0f), aspect, 20.0f, 2000.0f));
        EXPECT_EQ(BitsOf(pass.projection.horizontalScale), probe.horizontal);
        EXPECT_EQ(BitsOf(pass.projection.verticalScale), probe.vertical);
        EXPECT_EQ(pass.projection.centerX, probe.screen.width / 2);
        EXPECT_EQ(pass.projection.centerY, probe.screen.height / 2);
    }
}

// 0x620480 con la cámara de la UI: el píxel de 640 × 480 escalado en enteros sin signo, menos el centro, por la
// escala y por ±2.000; la cámara en el origen. En 1.024 × 768 los mismos puntos caen en otros píxeles enteros.
TEST(UiItemDrawingTest, TheUnprojectedPointFollowsThePixelsOfEachResolution)
{
    struct Case
    {
        Client::App::PixelSize screen;
        glm::ivec2 pixel;
        std::uint32_t x;
        std::uint32_t y;
    };
    const std::array<Case, 6> cases{{
        {{640, 480}, {320, 240}, 0x00000000u, 0x80000000u},
        {{640, 480}, {0, 0}, 0xc1ba2c54u, 0x418ba13eu},
        {{640, 480}, {481, 251}, 0x413b5634u, 0xbf4cca5bu},
        {{1024, 768}, {0, 0}, 0xc1ba2c53u, 0x418ba13eu},
        {{1024, 768}, {481, 251}, 0x413ae67fu, 0xbf45cf18u},
        {{1024, 768}, {100, 454}, 0xc17ffcf2u, 0xc178b737u},
    }};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(testing::Message() << probe.screen.width << " " << probe.pixel.x << "," << probe.pixel.y);
        const auto pass = Client::Scenes::MakeUiItemPass(probe.screen);
        ASSERT_TRUE(pass.HasValue()) << pass.Error();
        const UnprojectedPoint point =
            Client::Scenes::UnprojectGamePoint(pass.Value().projection, probe.pixel.x, probe.pixel.y);
        EXPECT_FLOAT_EQ(point.camera.x, 0.0f);
        EXPECT_FLOAT_EQ(point.camera.y, 0.0f);
        EXPECT_FLOAT_EQ(point.camera.z, 0.0f);
        EXPECT_EQ(BitsOf(point.point.x) & 0x7fffffffu, probe.x & 0x7fffffffu);
        EXPECT_EQ(BitsOf(point.point.y) & 0x7fffffffu, probe.y & 0x7fffffffu);
        EXPECT_FLOAT_EQ(point.point.z, -2000.0f);
    }
}

// 0x5e14e9..0x5e1d69: x + w · kx e y + h · ky en la x87 con los floats de la tabla, y _ftol. El 0,7 de 0x6b8a5c es
// 0,69999998: con 20 de ancho, 13,9999997 y la casilla de al lado. El 0x1a0e suma la altura entera; el 0x1a13 con el
// paso 3 y el 0x1a0b con el 2, la esquina (Q-C481).
TEST(UiItemDrawingTest, TheAnchorTakesTheFactorsOfTheTypeInTheX87)
{
    struct Case
    {
        std::int32_t type;
        std::uint32_t level;
        UiItemBox box;
        glm::ivec2 expected;
    };
    const std::array<Case, 10> cases{{
        {0x0000, 0, {465.0f, 200.0f, 20.0f, 60.0f}, {481, 251}},
        {0x040d, 0, {100.0f, 100.0f, 40.0f, 40.0f}, {124, 120}},
        {0x0808, 0, {100.0f, 100.0f, 20.0f, 20.0f}, {113, 113}},
        {0x0c0f, 0, {465.0f, 220.0f, 40.0f, 60.0f}, {485, 261}},
        {0x1a0e, 0, {10.0f, 20.0f, 20.0f, 20.0f}, {22, 40}},
        {0x1a0e, 8, {10.0f, 20.0f, 20.0f, 20.0f}, {21, 37}},
        {0x1a13, 3u << 3u, {10.7f, 20.9f, 20.0f, 20.0f}, {10, 20}},
        {0x1a0b, 2u << 3u, {10.7f, 20.9f, 20.0f, 20.0f}, {10, 20}},
        {0x1c0b, 0x70, {208.0f, 454.0f, 20.0f, 20.0f}, {218, 470}},
        {0x0000, 0, {-30.5f, -40.5f, 20.0f, 20.0f}, {-14, -23}},
    }};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(testing::Message() << std::hex << probe.type << " " << probe.level);
        const auto anchor = Client::Scenes::UiItemAnchor(probe.type, probe.level, probe.box);
        ASSERT_TRUE(anchor.HasValue()) << anchor.Error();
        EXPECT_EQ(anchor.Value(), probe.expected);
    }
    const auto far = Client::Scenes::UiItemAnchor(0x0000, 0, UiItemBox{1.0e20f, 0.0f, 20.0f, 20.0f});
    ASSERT_FALSE(far.HasValue());
    EXPECT_NE(far.Error().find("0x5e1d58"), std::string::npos) << far.Error();
}

// 0x5e1486..0x5e14e5: x ≤ mx < x + w e y ≤ my < y + h, si nadie tiene un ítem en el cursor o con el último argumento.
TEST(UiItemDrawingTest, TheHoverIsHalfOpenAndWaitsForAnEmptyCursor)
{
    const UiItemBox box{100.0f, 100.0f, 20.0f, 40.0f};
    EXPECT_TRUE(Client::Scenes::UiItemHovered(box, {100, 100}, 0, false));
    EXPECT_TRUE(Client::Scenes::UiItemHovered(box, {119, 139}, 0, false));
    EXPECT_FALSE(Client::Scenes::UiItemHovered(box, {120, 100}, 0, false));
    EXPECT_FALSE(Client::Scenes::UiItemHovered(box, {100, 140}, 0, false));
    EXPECT_FALSE(Client::Scenes::UiItemHovered(box, {99, 120}, 0, false));
    EXPECT_FALSE(Client::Scenes::UiItemHovered(box, {110, 120}, 1, false));
    EXPECT_TRUE(Client::Scenes::UiItemHovered(box, {110, 120}, 1, true));
}

// 0x5e1d6c..0x5e2267: el modelo y el nivel que 0x5e1480 le pasa a 0x5e0720.
TEST(UiItemDrawingTest, TheCallTakesTheModelAndTheLevelOfTheType)
{
    struct Case
    {
        std::int32_t type;
        std::uint32_t level;
        std::optional<std::pair<std::int32_t, std::int32_t>> expected;
    };
    const std::array<Case, 9> cases{{
        {0x0000, 0x38, std::pair{0x212, 0x38}},
        {0x1a13, 0, std::pair{0xc1c, -1}},
        {0x1a13, 8, std::pair{0x225, -1}},
        {0x1a13, 0x18, std::nullopt},
        {0x1c17, 8, std::pair{0x2246, -1}},
        {0x1c0b, 0x70, std::pair{0x223f, 0x70}},
        {0x1a14, 0x20, std::nullopt},
        {0x1c15, 8, std::pair{0x2245, 8}},
        {0x1c0c, 0x10, std::pair{0x1e1e, 0x10}},
    }};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(testing::Message() << std::hex << probe.type << " " << probe.level);
        const std::optional<UiItemCall> call = Client::Scenes::UiItemCallOf(probe.type, probe.level);
        ASSERT_EQ(call.has_value(), probe.expected.has_value());
        if (!call)
            continue;
        EXPECT_EQ(call->model, probe.expected->first);
        EXPECT_EQ(call->level, probe.expected->second);
    }
}

// 0x5e072f..0x5e075d y 0x5fc790: a 0,1 del camino de la cámara al punto, o a 0,07 con el último argumento; el 0x1031
// corre la x y la y con floats, y el 0x620 la x con el double de 0x6ba618.
TEST(UiItemDrawingTest, ThePlacementMovesTowardTheCameraWithItsShifts)
{
    glm::vec3 angles{};
    const auto plain = Place(0xa12, 0, angles);
    ASSERT_TRUE(plain.HasValue()) << plain.Error();
    EXPECT_EQ(BitsOf(plain.Value().position.x), 0x3f95de90u);
    EXPECT_EQ(BitsOf(plain.Value().position.y), 0xbda3d516u);
    EXPECT_EQ(plain.Value().position.z, -200.0f);

    const auto helm = Place(0x1031, 0, angles);
    ASSERT_TRUE(helm.HasValue()) << helm.Error();
    EXPECT_EQ(BitsOf(helm.Value().position.x), 0x3f99b59au);
    EXPECT_EQ(BitsOf(helm.Value().position.y), 0xbe0f5b2fu);
    EXPECT_EQ(helm.Value().angles, glm::vec3(-90.0f, 0.0f, 0.0f));

    const auto shield = Place(0x620, 0, angles, false, true);
    ASSERT_TRUE(shield.HasValue()) << shield.Error();
    EXPECT_EQ(BitsOf(shield.Value().position.x), 0x3f4f41d4u);
    EXPECT_EQ(BitsOf(shield.Value().position.y), 0x3d343c15u);
    EXPECT_EQ(shield.Value().position.z, -140.0f);
    EXPECT_EQ(shield.Value().angles, glm::vec3(180.0f, 90.0f, 13.0f));
}

// 0x5e09bd: las armas de 0x212 a 0xe11 se inclinan 15 o 25 por el byte de las dos manos de Item.bmd; sin la tabla,
// corta.
TEST(UiItemDrawingTest, TheWeaponsLeanByTheirTwoHandsByte)
{
    glm::vec3 angles{};
    const auto oneHand = ItemsWithTwoHands(0x10, 0);
    const auto twoHands = ItemsWithTwoHands(0x10, 1);
    const auto one = Place(0x222, 0, angles, false, false, 0.0f, &oneHand);
    ASSERT_TRUE(one.HasValue()) << one.Error();
    EXPECT_EQ(one.Value().angles, glm::vec3(180.0f, 270.0f, 15.0f));
    const auto two = Place(0x222, 0, angles, false, false, 0.0f, &twoHands);
    ASSERT_TRUE(two.HasValue()) << two.Error();
    EXPECT_EQ(two.Value().angles, glm::vec3(180.0f, 270.0f, 25.0f));
    EXPECT_EQ(angles, glm::vec3(180.0f, 270.0f, 25.0f));

    const auto missing = Place(0x222, 0, angles, false, false, 0.0f, nullptr);
    ASSERT_FALSE(missing.HasValue());
    EXPECT_NE(missing.Error().find("0x5e09bd"), std::string::npos) << missing.Error();
}

// 0x5e0de0: bajo el mouse, la y gira con el reloj por 0,45; los globales quedan con el giro.
TEST(UiItemDrawingTest, TheHoveredItemSpinsWithTheClock)
{
    glm::vec3 angles{};
    const auto spun = Place(0xa12, 0, angles, true, false, 1234.0f);
    ASSERT_TRUE(spun.HasValue()) << spun.Error();
    EXPECT_EQ(spun.Value().angles.x, 180.0f);
    EXPECT_EQ(BitsOf(spun.Value().angles.y), 0x440ad333u);
    EXPECT_EQ(spun.Value().angles.z, 15.0f);
    EXPECT_EQ(angles, spun.Value().angles);
}

// El zen (0x1e1e): con el paso 0 o 2 dibuja y posa otro modelo; con un paso que no es 0, 1 ni 2 no escribe los
// ángulos y se queda con los del ítem anterior (Q-C478).
TEST(UiItemDrawingTest, TheZenKeepsThePreviousAnglesWithAnUnknownStep)
{
    glm::vec3 angles{};
    const auto first = Place(0x1e1e, 0, angles);
    ASSERT_TRUE(first.HasValue()) << first.Error();
    EXPECT_EQ(first.Value().model, 0x223a);
    EXPECT_EQ(first.Value().poseModel, 0x223a);
    EXPECT_EQ(first.Value().angles, glm::vec3(180.0f, 0.0f, 0.0f));
    const auto second = Place(0x1e1e, 2 << 3, angles);
    ASSERT_TRUE(second.HasValue()) << second.Error();
    EXPECT_EQ(second.Value().model, 0x223b);
    EXPECT_EQ(second.Value().angles, glm::vec3(90.0f, 0.0f, 0.0f));
    const auto one = Place(0x1e1e, 1 << 3, angles);
    ASSERT_TRUE(one.HasValue()) << one.Error();
    EXPECT_EQ(one.Value().model, 0x1e1e);
    EXPECT_EQ(one.Value().angles, glm::vec3(270.0f, 90.0f, 0.0f));

    angles = glm::vec3(1.0f, 2.0f, 3.0f);
    const auto unknown = Place(0x1e1e, 3 << 3, angles);
    ASSERT_TRUE(unknown.HasValue()) << unknown.Error();
    EXPECT_EQ(unknown.Value().model, 0x1e1e);
    EXPECT_EQ(unknown.Value().angles, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(BitsOf(unknown.Value().scale), 0x3b656042u);
}

// 0x5e0e7c..0x5e13be: las armaduras posan con el 0x20a y la altura de la raíz de su familia; la escala, con los bits
// del binario.
TEST(UiItemDrawingTest, TheArmorsPoseWithThePlayerAndTheirFamilyHeight)
{
    struct Case
    {
        std::int32_t model;
        std::uint16_t pose;
        float root;
        std::uint32_t scale;
    };
    const std::array<Case, 7> cases{{
        {0x1012, 0x20a, -160.0f, 0x3b7f9724u},
        {0x1212, 0x20a, -100.0f, 0x3b7f9724u},
        {0x1412, 0x20a, -50.0f, 0x3b5844d0u},
        {0x1612, 0x20a, -70.0f, 0x3b79096cu},
        {0x1812, 0x20a, 0.0f, 0x3b51b717u},
        {0x1031, 0x20a, -160.0f, 0x3be56042u},
        {0x1a12, 0x1a12, 0.0f, 0x3b03126fu},
    }};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(testing::Message() << std::hex << probe.model);
        glm::vec3 angles{};
        const auto placed = Place(probe.model, 0, angles);
        ASSERT_TRUE(placed.HasValue()) << placed.Error();
        EXPECT_EQ(placed.Value().model, probe.model);
        EXPECT_EQ(placed.Value().poseModel, probe.pose);
        EXPECT_EQ(placed.Value().rootHeight, probe.root);
        EXPECT_EQ(BitsOf(placed.Value().scale), probe.scale);
    }
}

// El 0x225, el 0xc1c y el 0xa24 con el nivel en −1 (los del 0x1a13) van con otra escala y el nivel en 0; el 0x1e32
// sube 0,05 y apaga la x y la z de los ángulos.
TEST(UiItemDrawingTest, TheScaleTableRewritesTheLevelAndTheAngles)
{
    glm::vec3 angles{};
    const auto negative = Place(0x225, -1, angles);
    ASSERT_TRUE(negative.HasValue()) << negative.Error();
    EXPECT_EQ(negative.Value().level, 0);
    EXPECT_EQ(BitsOf(negative.Value().scale), 0x3a83126fu);
    const auto positive = Place(0x225, 8, angles);
    ASSERT_TRUE(positive.HasValue()) << positive.Error();
    EXPECT_EQ(positive.Value().level, 8);
    EXPECT_EQ(BitsOf(positive.Value().scale), 0x3b23d70au);

    const auto lifted = Place(0x1e32, 0, angles);
    ASSERT_TRUE(lifted.HasValue()) << lifted.Error();
    EXPECT_EQ(BitsOf(lifted.Value().position.y), 0xbcf5babeu);
    EXPECT_EQ(lifted.Value().angles, glm::vec3(0.0f, -10.0f, 0.0f));
    EXPECT_EQ(angles, glm::vec3(0.0f, -10.0f, 0.0f));
}

namespace
{
// Un triángulo con el vértice 0 en el origen del hueso: la x, la y y la z del primero son la posición del objeto.
class UiModels final : public Client::Scenes::IModelLibrary
{
public:
    UiModels()
    {
        Client::Data::Models::Mesh mesh;
        mesh.vertices = {{0, {0, 0, 0}}, {0, {1, 0, 0}}, {0, {0, 1, 0}}};
        mesh.normals = {{0, {0, 0, 1}, 0}};
        mesh.texCoords = {{0, 0}};
        mesh.triangles = {{3, {0, 1, 2, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}};
        mesh.textureName = "plain.jpg";
        model.meshes = {mesh};
        Client::Render::Models::SkeletonBone bone;
        bone.actions = {{Client::Render::Models::BoneKey{}}};
        skeleton.bones = {bone};
        skeleton.actions = {Client::Render::Models::ActionInfo{.keyCount = 1}};
    }
    const Client::Data::Models::Model* Find(std::uint16_t id) const override
    {
        return missing.contains(id) ? nullptr : &model;
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t id) const override
    {
        return missing.contains(id) ? nullptr : &skeleton;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return textures;
    }
    std::set<std::uint16_t> missing;
    Client::Data::Models::Model model;
    Client::Render::Models::Skeleton skeleton;
    std::array<Client::Render::TextureSlot, 1> textures{Client::Render::TextureSlot{0x400}};
};

struct UiDrawing
{
    UiDrawing()
    {
        special.random = &random;
        special.worldClock = 0.0f;
        context.special = &special;
        context.selections = &selections;
    }
    Client::Scenes::UiItemResources Resources(Client::App::PixelSize screen)
    {
        auto pass = Client::Scenes::MakeUiItemPass(screen);
        EXPECT_TRUE(pass.HasValue());
        return Client::Scenes::UiItemResources{.batch = batch,
                                               .models = models,
                                               .poseCarry = carry,
                                               .itemContext = context,
                                               .flushSprites = [this] { events.emplace_back("2d"); },
                                               .flushModels = [this] { events.emplace_back("3d"); },
                                               .pass = pass.HasValue() ? pass.Value() : Client::Scenes::UiItemPass{}};
    }
    UiModels models;
    Client::Render::ModelBatch batch;
    Client::Scenes::FaunaDrawCarry carry;
    CrtRandom random{0x2468aceu};
    Client::Scenes::ItemSpecialDrawContext special;
    std::map<std::uint16_t, std::int32_t> selections;
    Client::Scenes::ItemDrawContext context;
    std::vector<std::string> events;
};
} // namespace

// 0x5e13c5..0x5e146d: el primer ítem cierra el 2D y abre el pase de la UI; el modelo va en la posición de la pose, con
// los rand() de 0x607b60 (uno con el 0x220). El final de 0x5ac120 dibuja el lote una vez; sin ítems, nada.
TEST(UiItemDrawingTest, TheFirstItemOpensTheUiPassAndTheEndDrawsIt)
{
    for (const Client::App::PixelSize screen : {Client::App::PixelSize{640, 480}, Client::App::PixelSize{1024, 768}})
    {
        SCOPED_TRACE(screen.width);
        UiDrawing drawing;
        Client::Scenes::UiItemResources resources = drawing.Resources(screen);
        Client::Scenes::CloseUiItemPass(resources);
        EXPECT_TRUE(drawing.events.empty());

        glm::vec3 angles{};
        const auto placed = Place(0x220, 0, angles);
        ASSERT_TRUE(placed.HasValue()) << placed.Error();
        CrtRandom expected = drawing.random;
        static_cast<void>(expected.Next());
        const auto drawn = Client::Scenes::DrawUiItemModel(resources, drawing.random, placed.Value(), 0, 0);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        const auto again = Client::Scenes::DrawUiItemModel(resources, drawing.random, placed.Value(), 0, 0);
        ASSERT_TRUE(again.HasValue()) << again.Error();
        static_cast<void>(expected.Next());
        EXPECT_EQ(drawing.random.State(), expected.State());
        EXPECT_EQ(drawing.events, std::vector<std::string>{"2d"});
        EXPECT_EQ(drawing.batch.Pass().viewport, glm::ivec4(0, 0, screen.width, screen.height));
        EXPECT_FALSE(drawing.batch.InitializesMaterial());
        ASSERT_EQ(drawing.batch.Vertices().size(), 6u);
        EXPECT_EQ(drawing.batch.Vertices()[0].position, placed.Value().position);

        Client::Scenes::CloseUiItemPass(resources);
        Client::Scenes::CloseUiItemPass(resources);
        EXPECT_EQ(drawing.events, (std::vector<std::string>{"2d", "3d"}));
    }
}

// 0x60cc1c: con el set, el rand() % 100 del pase sale con el pulso de la x87 sin redondear en 0,01f o menos. Sin el
// float del mundo, con el reloj del frame: a los 4.036.258.298 ns el pulso todavía pasa el 0,01f (su float no) y no
// sale; a los 4.036.258.300, sí, después del de 0x607b60.
TEST(UiItemDrawingTest, TheSetRandomWaitsForTheUnroundedPulse)
{
    for (const std::int64_t nanoseconds : {std::int64_t{4036258298}, std::int64_t{4036258300}})
    {
        SCOPED_TRACE(nanoseconds);
        UiDrawing drawing;
        drawing.special.worldClock.reset();
        Client::Scenes::UiItemResources resources = drawing.Resources({640, 480});
        resources.frameClock = std::chrono::nanoseconds{nanoseconds};
        glm::vec3 angles{};
        const auto placed = Place(0x220, 0, angles);
        ASSERT_TRUE(placed.HasValue()) << placed.Error();
        CrtRandom expected = drawing.random;
        static_cast<void>(expected.Next());
        const bool off = nanoseconds == 4036258300;
        const auto selection = off ? expected.Next() % 100 : 0;

        const auto drawn = Client::Scenes::DrawUiItemModel(resources, drawing.random, placed.Value(), 0, 1);

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(drawing.random.State(), expected.State());
        EXPECT_EQ(drawing.selections.contains(0x220), off);
        if (off)
        {
            EXPECT_EQ(drawing.selections.at(0x220), static_cast<std::int32_t>(selection));
        }
    }
}

// Sin el modelo que dibuja o sin el esqueleto del que posa, corta (el original no dibujaría nada).
TEST(UiItemDrawingTest, AMissingModelStopsWithItsAddress)
{
    UiDrawing drawing;
    drawing.models.missing.insert(0x20a);
    Client::Scenes::UiItemResources resources = drawing.Resources({640, 480});
    glm::vec3 angles{};
    const auto placed = Place(0x1012, 0, angles);
    ASSERT_TRUE(placed.HasValue()) << placed.Error();

    const auto drawn = Client::Scenes::DrawUiItemModel(resources, drawing.random, placed.Value(), 0, 0);

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x5e13d9"), std::string::npos) << drawn.Error();
    EXPECT_TRUE(drawing.events.empty());
}
