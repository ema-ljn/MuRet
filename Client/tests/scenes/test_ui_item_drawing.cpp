#include "Models/IModelLibrary.h"
#include "Items/UiItemDrawing.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Tables/ItemTable.h"
#include "Render/ModelBatch.h"
#include "Math/RoundedTransforms.h"
#include "Models/Skeleton.h"
#include "Models/ModelId.h"
#include "Render/TextureSlot.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Kernel/PoseBuffer.h"
#include "Actors/Drawing/GameItemSpecialDrawing.h"
#include "Scenes/GameProjection.h"
#include "support/TableFiles.h"

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

// The UI's 3D item (CL8 milestone 4, 06 § El inventario, "El ítem de la UI"): the camera of 0x620480 in the pass of
// 0x5ac120, the box point of 0x5e1480, the pose of 0x5e0720 and the drawing with the pass the first one opened. The
// expected values come from the x87 by hand (the floats with their bits); the models are invented.
namespace
{
using Client::Crt::CrtRandom;
using Client::Items::UiItemBox;
using Client::Items::UiItemCall;
using Client::Items::UiItemPlacement;
using Client::Kernel::UnprojectedPoint;

float Bits(std::uint32_t bits)
{
    return std::bit_cast<float>(bits);
}

std::uint32_t BitsOf(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}

// Item.bmd with the two-hands byte (+0x1e) of type `type` at `twoHands`.
Client::Tables::ItemTable ItemsWithTwoHands(std::size_t type, std::uint8_t twoHands)
{
    using Client::Tables::ItemRecordSize;
    std::vector<std::byte> plain(Client::Tables::ItemTableSize);
    constexpr std::size_t TwoHands = 0x1e;
    plain[type * ItemRecordSize + TwoHands] = std::byte{twoHands};
    const std::vector<std::byte> encrypted = Client::Tests::EncryptRecords(std::move(plain), ItemRecordSize);
    return Client::Tables::ItemTable(std::span<const std::byte>(encrypted));
}

// Item.bmd with everything at 0: the weapons, one-handed.
const Client::Tables::ItemTable& PlainItems()
{
    static const Client::Tables::ItemTable items = ItemsWithTwoHands(0, 0);
    return items;
}

// The UI camera at the origin and the point (481, 251) of 640 × 480: (11.708546; −0.7999627; −2000).
UnprojectedPoint Anchor640()
{
    return UnprojectedPoint{.camera = glm::vec3(0.0f),
                            .point = glm::vec3(Bits(0x413b5634u), Bits(0xbf4cca5bu), -2000.0f)};
}

Client::Core::Expected<UiItemPlacement, std::string> Place(std::int32_t model, std::int32_t level, glm::vec3& angles,
                                                           bool hovered = false, bool always = false,
                                                           float clock = 0.0f,
                                                           const Client::Tables::ItemTable* items = &PlainItems())
{
    return Client::Items::PlaceUiItem(UiItemCall{.model = model, .level = level}, Anchor640(), hovered, always, clock,
                                      items, angles);
}
} // namespace

// 0x5ac120: 0x6203c0(1°, W / H, 20, 2000) on the whole screen and the view at the identity. The scales and the center
// of 0x6203c0 and 0x620d10 with the x87's bits.
TEST(UiItemDrawingTest, TheUiPassCoversTheWholeScreenWithOneDegree)
{
    struct Case
    {
        Client::Core::PixelSize screen;
        std::uint32_t horizontal;
        std::uint32_t vertical;
    };
    for (const Case& probe : {Case{{640, 480}, 0x38188357u, 0x38188356u}, Case{{1024, 768}, 0x37bea42cu, 0x37bea42cu}})
    {
        SCOPED_TRACE(probe.screen.width);
        const auto made = Client::Items::MakeUiItemPass(probe.screen);
        ASSERT_TRUE(made.HasValue()) << made.Error();
        const Client::Items::UiItemPass& pass = made.Value();
        EXPECT_EQ(pass.pass.viewport, glm::ivec4(0, 0, probe.screen.width, probe.screen.height));
        EXPECT_EQ(pass.pass.view, glm::mat4(1.0f));
        EXPECT_FLOAT_EQ(pass.pass.farPlane, 2000.0f);
        const float aspect = static_cast<float>(static_cast<double>(probe.screen.width) / probe.screen.height);
        EXPECT_EQ(pass.pass.projection, Client::Math::RoundedPerspective(glm::radians(1.0f), aspect, 20.0f, 2000.0f));
        EXPECT_EQ(BitsOf(pass.projection.horizontalScale), probe.horizontal);
        EXPECT_EQ(BitsOf(pass.projection.verticalScale), probe.vertical);
        EXPECT_EQ(pass.projection.centerX, probe.screen.width / 2);
        EXPECT_EQ(pass.projection.centerY, probe.screen.height / 2);
    }
}

// 0x620480 with the UI camera: the 640 × 480 pixel scaled in unsigned integers, minus the center, times the scale and
// times ±2000; the camera at the origin. At 1024 × 768 the same points fall on other integer pixels.
TEST(UiItemDrawingTest, TheUnprojectedPointFollowsThePixelsOfEachResolution)
{
    struct Case
    {
        Client::Core::PixelSize screen;
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
        const auto pass = Client::Items::MakeUiItemPass(probe.screen);
        ASSERT_TRUE(pass.HasValue()) << pass.Error();
        const UnprojectedPoint point =
            Client::Kernel::UnprojectGamePoint(pass.Value().projection, probe.pixel.x, probe.pixel.y);
        EXPECT_FLOAT_EQ(point.camera.x, 0.0f);
        EXPECT_FLOAT_EQ(point.camera.y, 0.0f);
        EXPECT_FLOAT_EQ(point.camera.z, 0.0f);
        EXPECT_EQ(BitsOf(point.point.x) & 0x7fffffffu, probe.x & 0x7fffffffu);
        EXPECT_EQ(BitsOf(point.point.y) & 0x7fffffffu, probe.y & 0x7fffffffu);
        EXPECT_FLOAT_EQ(point.point.z, -2000.0f);
    }
}

// 0x5e14e9..0x5e1d69: x + w · kx and y + h · ky in the x87 with the table's floats, and _ftol. The 0.7 at 0x6b8a5c is
// 0.69999998: with a width of 20, 13.9999997 and the cell next to it. 0x1a0e adds the whole height; 0x1a13 with step 3
// and 0x1a0b with 2, the corner (Q-C481).
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
        const auto anchor = Client::Items::UiItemAnchor(probe.type, probe.level, probe.box);
        ASSERT_TRUE(anchor.HasValue()) << anchor.Error();
        EXPECT_EQ(anchor.Value(), probe.expected);
    }
    const auto far = Client::Items::UiItemAnchor(0x0000, 0, UiItemBox{1.0e20f, 0.0f, 20.0f, 20.0f});
    ASSERT_FALSE(far.HasValue());
    EXPECT_NE(far.Error().find("0x5e1d58"), std::string::npos) << far.Error();
}

// 0x5e1486..0x5e14e5: x ≤ mx < x + w and y ≤ my < y + h, if nobody has an item on the cursor or with the last argument.
TEST(UiItemDrawingTest, TheHoverIsHalfOpenAndWaitsForAnEmptyCursor)
{
    const UiItemBox box{100.0f, 100.0f, 20.0f, 40.0f};
    EXPECT_TRUE(Client::Items::UiItemHovered(box, {100, 100}, 0, false));
    EXPECT_TRUE(Client::Items::UiItemHovered(box, {119, 139}, 0, false));
    EXPECT_FALSE(Client::Items::UiItemHovered(box, {120, 100}, 0, false));
    EXPECT_FALSE(Client::Items::UiItemHovered(box, {100, 140}, 0, false));
    EXPECT_FALSE(Client::Items::UiItemHovered(box, {99, 120}, 0, false));
    EXPECT_FALSE(Client::Items::UiItemHovered(box, {110, 120}, 1, false));
    EXPECT_TRUE(Client::Items::UiItemHovered(box, {110, 120}, 1, true));
}

// 0x5e1d6c..0x5e2267: the model and the level that 0x5e1480 passes to 0x5e0720.
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
        const std::optional<UiItemCall> call = Client::Items::UiItemCallOf(probe.type, probe.level);
        ASSERT_EQ(call.has_value(), probe.expected.has_value());
        if (!call)
            continue;
        EXPECT_EQ(call->model, probe.expected->first);
        EXPECT_EQ(call->level, probe.expected->second);
    }
}

// 0x5e072f..0x5e075d and 0x5fc790: at 0.1 of the way from the camera to the point, or at 0.07 with the last argument;
// 0x1031 shifts x and y with floats, and 0x620 shifts x with the double at 0x6ba618.
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

// 0x5e09bd: the weapons from 0x212 to 0xe11 lean 15 or 25 by Item.bmd's two-hands byte; without the table, it cuts.
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

// 0x5e0de0: under the mouse, y turns with the clock times 0.45; the globals keep the turn.
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

// The zen (0x1e1e): with step 0 or 2 it draws and poses another model; with a step that is not 0, 1 or 2 it does not
// write the angles and keeps the previous item's (Q-C478).
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

// 0x5e0e7c..0x5e13be: the armors pose with 0x20a and the root height of their family; the scale, with the binary's
// bits.
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

// 0x225, 0xc1c and 0xa24 with the level at −1 (those of 0x1a13) go with another scale and the level at 0; 0x1e32 rises
// 0.05 and turns off the x and the z of the angles.
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
// A triangle with vertex 0 at the bone's origin: the first one's x, y and z are the object's position.
class UiModels final : public Client::Models::IModelLibrary
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
        Client::Models::SkeletonBone bone;
        bone.actions = {{Client::Models::BoneKey{}}};
        skeleton.bones = {bone};
        skeleton.actions = {Client::Models::ActionInfo{.keyCount = 1}};
    }
    const Client::Data::Models::Model* Find(Client::Models::ModelId id) const override
    {
        return missing.contains(id.Value()) ? nullptr : &model;
    }
    const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId id) const override
    {
        return missing.contains(id.Value()) ? nullptr : &skeleton;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(Client::Models::ModelId) const override
    {
        return textures;
    }
    std::set<std::uint16_t> missing;
    Client::Data::Models::Model model;
    Client::Models::Skeleton skeleton;
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
    Client::Items::UiItemResources Resources(Client::Core::PixelSize screen)
    {
        auto pass = Client::Items::MakeUiItemPass(screen);
        EXPECT_TRUE(pass.HasValue());
        return Client::Items::UiItemResources{.batch = batch,
                                              .models = models,
                                              .poseCarry = carry,
                                              .itemContext = context,
                                              .flushSprites = [this] { events.emplace_back("2d"); },
                                              .flushModels = [this] { events.emplace_back("3d"); },
                                              .pass = pass.HasValue() ? pass.Value() : Client::Items::UiItemPass{}};
    }
    UiModels models;
    Client::Render::ModelBatch batch;
    Client::Kernel::PoseBuffer carry;
    CrtRandom random{0x2468aceu};
    Client::Actors::Drawing::ItemSpecialDrawContext special;
    std::map<std::uint16_t, std::int32_t> selections;
    Client::Actors::Drawing::ItemDrawContext context;
    std::vector<std::string> events;
};
} // namespace

// 0x5e13c5..0x5e146d: the first item closes the 2D and opens the UI pass; the model goes at the pose's position, with
// the rand() of 0x607b60 (one with 0x220). The end of 0x5ac120 draws the batch once; with no items, nothing.
TEST(UiItemDrawingTest, TheFirstItemOpensTheUiPassAndTheEndDrawsIt)
{
    for (const Client::Core::PixelSize screen : {Client::Core::PixelSize{640, 480}, Client::Core::PixelSize{1024, 768}})
    {
        SCOPED_TRACE(screen.width);
        UiDrawing drawing;
        Client::Items::UiItemResources resources = drawing.Resources(screen);
        Client::Items::CloseUiItemPass(resources);
        EXPECT_TRUE(drawing.events.empty());

        glm::vec3 angles{};
        const auto placed = Place(0x220, 0, angles);
        ASSERT_TRUE(placed.HasValue()) << placed.Error();
        CrtRandom expected = drawing.random;
        static_cast<void>(expected.Next());
        const auto drawn = Client::Items::DrawUiItemModel(resources, drawing.random, placed.Value(), 0, 0);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        const auto again = Client::Items::DrawUiItemModel(resources, drawing.random, placed.Value(), 0, 0);
        ASSERT_TRUE(again.HasValue()) << again.Error();
        static_cast<void>(expected.Next());
        EXPECT_EQ(drawing.random.State(), expected.State());
        EXPECT_EQ(drawing.events, std::vector<std::string>{"2d"});
        EXPECT_EQ(drawing.batch.Pass().viewport, glm::ivec4(0, 0, screen.width, screen.height));
        EXPECT_FALSE(drawing.batch.InitializesMaterial());
        ASSERT_EQ(drawing.batch.Vertices().size(), 6u);
        EXPECT_EQ(drawing.batch.Vertices()[0].position, placed.Value().position);

        Client::Items::CloseUiItemPass(resources);
        Client::Items::CloseUiItemPass(resources);
        EXPECT_EQ(drawing.events, (std::vector<std::string>{"2d", "3d"}));
    }
}

// 0x60cc1c: with the set, the pass's rand() % 100 comes out with the x87 pulse unrounded at 0.01f or less. Without the
// world's float, with the frame clock: at 4036258298 ns the pulse still passes 0.01f (its float does not) and it does
// not come out; at 4036258300, it does, after the one of 0x607b60.
TEST(UiItemDrawingTest, TheSetRandomWaitsForTheUnroundedPulse)
{
    for (const std::int64_t nanoseconds : {std::int64_t{4036258298}, std::int64_t{4036258300}})
    {
        SCOPED_TRACE(nanoseconds);
        UiDrawing drawing;
        drawing.special.worldClock.reset();
        Client::Items::UiItemResources resources = drawing.Resources({640, 480});
        resources.frameClock = std::chrono::nanoseconds{nanoseconds};
        glm::vec3 angles{};
        const auto placed = Place(0x220, 0, angles);
        ASSERT_TRUE(placed.HasValue()) << placed.Error();
        CrtRandom expected = drawing.random;
        static_cast<void>(expected.Next());
        const bool off = nanoseconds == 4036258300;
        const auto selection = off ? expected.Next() % 100 : 0;

        const auto drawn = Client::Items::DrawUiItemModel(resources, drawing.random, placed.Value(), 0, 1);

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(drawing.random.State(), expected.State());
        EXPECT_EQ(drawing.selections.contains(0x220), off);
        if (off)
        {
            EXPECT_EQ(drawing.selections.at(0x220), static_cast<std::int32_t>(selection));
        }
    }
}

// 0x60b573: with the options' glow detail (0x6c9dcc) at 1, a level 13 item draws as a level 7 one: the body and the
// 0x44 pass.
TEST(UiItemDrawingTest, TheGlowDetailOfTheOptionsTrimsTheLevel)
{
    for (const std::uint8_t detail : {std::uint8_t{4}, std::uint8_t{1}})
    {
        SCOPED_TRACE(static_cast<int>(detail));
        UiDrawing drawing;
        drawing.context.glowDetail = detail;
        Client::Items::UiItemResources resources = drawing.Resources({640, 480});
        glm::vec3 angles{};
        const auto placed = Place(0xa12, 13 << 3, angles);
        ASSERT_TRUE(placed.HasValue()) << placed.Error();

        const auto drawn = Client::Items::DrawUiItemModel(resources, drawing.random, placed.Value(), 0, 0);

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(drawing.batch.Vertices().size(), detail == 4 ? 12u : 6u);
    }
}

// Without the model that draws or without the skeleton of the one that poses, it cuts (the original would draw
// nothing).
TEST(UiItemDrawingTest, AMissingModelStopsWithItsAddress)
{
    UiDrawing drawing;
    drawing.models.missing.insert(0x20a);
    Client::Items::UiItemResources resources = drawing.Resources({640, 480});
    glm::vec3 angles{};
    const auto placed = Place(0x1012, 0, angles);
    ASSERT_TRUE(placed.HasValue()) << placed.Error();

    const auto drawn = Client::Items::DrawUiItemModel(resources, drawing.random, placed.Value(), 0, 0);

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x5e13d9"), std::string::npos) << drawn.Error();
    EXPECT_TRUE(drawing.events.empty());
}
