#include "Models/ModelTextureSlots.h"

#include "Data/Models/Model.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/TextureSet.h"
#include "Render/TextureSlot.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// 0x60d530, 0x60d550 and 0x60d5f0 as data.
namespace Client::Render
{
// For GoogleTest messages: the slot in hex, like the research.
void PrintTo(const TextureSlot& slot, std::ostream* out)
{
    *out << std::format("{:#x}", slot.value);
}
} // namespace Client::Render

namespace
{
using Client::Data::Models::Mesh;
using Client::Data::Models::Model;
using Client::Data::Textures::Filter;
using Client::Data::Textures::Sampling;
using Client::Data::Textures::TextureSetEntry;
using Client::Data::Textures::Wrap;
using Client::Models::ModelTextureSlots;
using Client::Render::TextureSlot;

// The first slot of the character parts (0x60f7d0) and of the items (0x611850).
constexpr TextureSlot PlayerParts{0x138};
constexpr TextureSlot Items{0x25a};
// The sampling of the model loaders.
constexpr Sampling ModelSampling{Filter::Linear, Wrap::Repeat};

// One mesh per texture, without vertices: the assigner only looks at the names.
Model ModelWith(std::vector<std::string_view> textures)
{
    Model model;
    for (const std::string_view texture : textures)
    {
        Mesh mesh;
        mesh.textureName = texture;
        model.meshes.push_back(mesh);
    }
    return model;
}

std::vector<TextureSlot> Slots(std::vector<std::uint16_t> values)
{
    std::vector<TextureSlot> slots;
    for (const std::uint16_t value : values)
    {
        slots.push_back(TextureSlot{value});
    }
    return slots;
}

TEST(ModelTextureSlotsTest, ANewNameTakesTheNextSlot)
{
    ModelTextureSlots slots(PlayerParts);

    const std::vector<TextureSlot> assigned =
        slots.Assign(ModelWith({"armor_a.jpg", "armor_b.jpg"}), "Player\\", ModelSampling);

    EXPECT_EQ(assigned, Slots({0x138, 0x139}));
    EXPECT_EQ(slots.Next(), TextureSlot{0x13a});
    ASSERT_EQ(slots.ToLoad().size(), 2u);
    EXPECT_EQ(slots.ToLoad()[0].path, "Player\\armor_a.jpg");
    EXPECT_EQ(slots.ToLoad()[0].slot, 0x138);
    EXPECT_EQ(slots.ToLoad()[1].path, "Player\\armor_b.jpg");
}

// In the same model or in another, without a request.
TEST(ModelTextureSlotsTest, ARepeatedNameReusesItsSlot)
{
    ModelTextureSlots slots(PlayerParts);
    (void)slots.Assign(ModelWith({"armor_a.jpg"}), "Player\\", ModelSampling);

    const std::vector<TextureSlot> assigned =
        slots.Assign(ModelWith({"armor_a.jpg", "armor_a.jpg"}), "Player\\", ModelSampling);

    EXPECT_EQ(assigned, Slots({0x138, 0x138}));
    EXPECT_EQ(slots.ToLoad().size(), 1u);
    EXPECT_EQ(slots.Next(), TextureSlot{0x139});
}

TEST(ModelTextureSlotsTest, AReusedSlotKeepsEachRequestsExactTextureKey)
{
    ModelTextureSlots slots(PlayerParts);
    Client::Models::ModelTextureRequests requests;
    std::vector<std::optional<std::size_t>> bindings;
    const auto first = slots.Assign(ModelWith({"panel.tga.extra", "panel.tga.extra"}), "Player\\", ModelSampling,
                                    &requests, &bindings);
    EXPECT_EQ(first, Slots({0x138, 0x138}));
    ASSERT_EQ(bindings.size(), 2u);
    EXPECT_EQ(bindings[0], 0u);
    EXPECT_EQ(bindings[1], 0u);
    slots.SetNext(PlayerParts);
    const auto second = slots.Assign(ModelWith({"replacement.jpg"}), "Item\\", ModelSampling, &requests, &bindings);
    EXPECT_EQ(second, Slots({0x138}));
    ASSERT_EQ(bindings.size(), 1u);
    EXPECT_EQ(bindings[0], 1u);
    ASSERT_EQ(requests.names.size(), 2u);
    EXPECT_EQ(requests.names[0].requested, "panel.tga.extra");
    EXPECT_TRUE(requests.names[0].previous.empty());
    EXPECT_EQ(requests.names[1].requested, "replacement.jpg");
    EXPECT_EQ(requests.names[1].previous, "panel.tga.extra");
    ASSERT_EQ(slots.ToLoad().size(), 2u);
    EXPECT_EQ(slots.ToLoad()[0].path, "Player\\panel.tga");
    EXPECT_EQ(slots.ToLoad()[1].path, "Item\\replacement.jpg");
    EXPECT_EQ(slots.NameOf(PlayerParts), "replacement.jpg");
}

// Q-C90: the bare name is searched, without the folder: one from another folder is reused.
TEST(ModelTextureSlotsTest, ANameIsFoundInAnyFolder)
{
    ModelTextureSlots slots(PlayerParts);
    (void)slots.Assign(ModelWith({"flag.jpg"}), "Skill\\", ModelSampling);

    const std::vector<TextureSlot> assigned = slots.Assign(ModelWith({"flag.jpg"}), "Item\\", ModelSampling);

    EXPECT_EQ(assigned, Slots({0x138}));
    ASSERT_EQ(slots.ToLoad().size(), 1u);
    EXPECT_EQ(slots.ToLoad()[0].path, "Skill\\flag.jpg");
}

// Q-C90: with strcmp, case-sensitive.
TEST(ModelTextureSlotsTest, TheSearchIsCaseSensitive)
{
    ModelTextureSlots slots(PlayerParts);

    const std::vector<TextureSlot> assigned =
        slots.Assign(ModelWith({"Flag.jpg", "flag.jpg"}), "Item\\", ModelSampling);

    EXPECT_EQ(assigned, Slots({0x138, 0x139}));
}

// 0x60d530 sets the search at 0, not at the first slot or at the last one set: what another loader requested lower
// down is reused.
TEST(ModelTextureSlotsTest, TheSearchStartsAtSlotZero)
{
    ModelTextureSlots slots(Items);
    slots.SetNext(PlayerParts);
    (void)slots.Assign(ModelWith({"armor_a.jpg"}), "Player\\", ModelSampling);
    slots.SetNext(TextureSlot{Items.value + 1});

    const std::vector<TextureSlot> assigned =
        slots.Assign(ModelWith({"armor_a.jpg", "sword.jpg"}), "Item\\", ModelSampling);

    EXPECT_EQ(assigned, Slots({0x138, 0x25b}));
}

// A loader that follows another with its table (0x61ca30 with 0x60d530(0x6f8)): it sees the earlier names, starts at
// its next and has nothing of the others to upload.
TEST(ModelTextureSlotsTest, AContinuedTableKeepsTheNamesAndNothingToLoad)
{
    ModelTextureSlots game(PlayerParts);
    (void)game.Assign(ModelWith({"armor_a.jpg"}), "Player\\", ModelSampling);

    ModelTextureSlots selection = game.ContinuedAt(TextureSlot{0x6f8});
    const std::vector<TextureSlot> assigned =
        selection.Assign(ModelWith({"armor_a.jpg", "face.jpg"}), "Logo\\", ModelSampling);

    EXPECT_EQ(assigned, Slots({0x138, 0x6f8}));
    EXPECT_EQ(selection.Next(), TextureSlot{0x6f9});
    ASSERT_EQ(selection.ToLoad().size(), 1u);
    EXPECT_EQ(selection.ToLoad()[0].slot, 0x6f8);
    EXPECT_EQ(selection.NameOf(TextureSlot{0x138}), "armor_a.jpg");
    EXPECT_EQ(game.Next(), TextureSlot{0x139});
    EXPECT_EQ(game.ToLoad().size(), 1u);
}

// The search goes up to the next free slot: with it moved back (0x615700 goes back to 0x138, Q-C89), a name higher up
// is no longer found and is requested again, on top.
TEST(ModelTextureSlotsTest, TheSearchStopsAtNext)
{
    ModelTextureSlots slots(Items);
    (void)slots.Assign(ModelWith({"sword.jpg"}), "Item\\", ModelSampling);
    slots.SetNext(PlayerParts);

    const std::vector<TextureSlot> assigned = slots.Assign(ModelWith({"sword.jpg"}), "Item\\", ModelSampling);

    EXPECT_EQ(assigned, Slots({0x138}));
    EXPECT_EQ(slots.ToLoad().size(), 2u);
}

// The next free slot is not searched either: moved back exactly onto a name, that name is requested again in it.
TEST(ModelTextureSlotsTest, TheNextSlotItselfIsNotSearched)
{
    ModelTextureSlots slots(PlayerParts);
    (void)slots.Assign(ModelWith({"armor_a.jpg", "armor_b.jpg"}), "Player\\", ModelSampling);
    slots.SetNext(TextureSlot{0x139});

    const std::vector<TextureSlot> assigned = slots.Assign(ModelWith({"armor_b.jpg"}), "Player\\", ModelSampling);

    EXPECT_EQ(assigned, Slots({0x139}));
    EXPECT_EQ(slots.ToLoad().size(), 3u);
    EXPECT_EQ(slots.Next(), TextureSlot{0x13a});
}

// ToLoad's paths point to the ones the assigner stores: a copy would point to the other's, so it is not copied. It
// moves, and the paths stay valid after the old one is gone.
TEST(ModelTextureSlotsTest, MovesButDoesNotCopy)
{
    EXPECT_FALSE(std::is_copy_constructible_v<ModelTextureSlots>);
    EXPECT_FALSE(std::is_copy_assignable_v<ModelTextureSlots>);

    std::optional<ModelTextureSlots> before(std::in_place, PlayerParts);
    (void)before->Assign(ModelWith({"armor_with_a_long_name.jpg"}), "Player\\", ModelSampling);
    const ModelTextureSlots moved(std::move(*before));
    before.reset();

    ASSERT_EQ(moved.ToLoad().size(), 1u);
    EXPECT_EQ(moved.ToLoad()[0].path, "Player\\armor_with_a_long_name.jpg");
}

// OZTs always use GL_NEAREST; the wrap is the requester's. The path carries the extension of the loader the letter
// chose, so that LoadTextureSet opens the same file.
TEST(ModelTextureSlotsTest, OztIsAlwaysNearest)
{
    ModelTextureSlots slots(PlayerParts);

    (void)slots.Assign(ModelWith({"wing.tga", "body.jpg", "x.tga.jpg"}), "Item\\", ModelSampling);

    const std::span<const TextureSetEntry> toLoad = slots.ToLoad();
    ASSERT_EQ(toLoad.size(), 3u);
    EXPECT_EQ(toLoad[0].path, "Item\\wing.tga");
    EXPECT_EQ(toLoad[0].sampling.filter, Filter::Nearest);
    EXPECT_EQ(toLoad[0].sampling.wrap, Wrap::Repeat);
    EXPECT_EQ(toLoad[1].path, "Item\\body.jpg");
    EXPECT_EQ(toLoad[1].sampling.filter, Filter::Linear);
    EXPECT_EQ(toLoad[2].path, "Item\\x.tga");
    EXPECT_EQ(toLoad[2].sampling.filter, Filter::Nearest);
}

// Skin and hair go to their fixed slots after loading: the requested one stays taken.
TEST(ModelTextureSlotsTest, TheSkinMeshesPointAtTheSkinSlots)
{
    ModelTextureSlots slots(PlayerParts);

    const std::vector<TextureSlot> assigned =
        slots.Assign(ModelWith({"skin_barbarian_01.jpg", "hair_r.tga", "hide.jpg", "level_man01.jpg", "armor.jpg"}),
                     "Player\\", ModelSampling);

    EXPECT_EQ(assigned, Slots({0x12e, 0x137, 0x12d, 0x12e, 0x13c}));
    EXPECT_EQ(slots.ToLoad().size(), 5u);
}

// A skin texture already requested is not requested again, but it still goes to its fixed slot.
TEST(ModelTextureSlotsTest, ASkinNameStillTakesASlot)
{
    ModelTextureSlots slots(PlayerParts);
    (void)slots.Assign(ModelWith({"skin_barbarian_01.jpg"}), "Player\\", ModelSampling);

    const std::vector<TextureSlot> assigned =
        slots.Assign(ModelWith({"skin_barbarian_01.jpg"}), "Player\\", ModelSampling);

    EXPECT_EQ(assigned, Slots({0x12e}));
    EXPECT_EQ(slots.ToLoad().size(), 1u);
    EXPECT_EQ(slots.Next(), TextureSlot{0x139});
}
// The name each slot keeps, like the table at 0x7d3a198: the mesh's, without the folder; one requested on top of
// another (Q-C89) keeps the last one; empty for one nobody requested and for one outside.
TEST(ModelTextureSlotsTest, EachSlotKeepsTheNameOfItsMesh)
{
    ModelTextureSlots slots(PlayerParts);
    (void)slots.Assign(ModelWith({"armor_a.jpg", "Sword.JPG"}), "Player\\", ModelSampling);
    slots.SetNext(PlayerParts);
    (void)slots.Assign(ModelWith({"nfb_R.jpg"}), "Item\\", ModelSampling);

    EXPECT_EQ(slots.NameOf(TextureSlot{0x138}), "nfb_R.jpg");
    EXPECT_EQ(slots.NameOf(TextureSlot{0x139}), "Sword.JPG");
    EXPECT_EQ(slots.NameOf(TextureSlot{0x13a}), "");
    EXPECT_EQ(slots.NameOf(TextureSlot{0x137}), "");
    EXPECT_EQ(slots.NameOf(TextureSlot{0xffff}), "");
}
} // namespace
