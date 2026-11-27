#include "Actors/Drawing/CharacterDrawing.h"

#include "Assets/PlayerSkeleton.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Data/Models/ModelFiles.h"
#include "Data/Models/ModelTextures.h"
#include "Tables/PlayerModels.h"
#include "Kernel/CharacterAppearance.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Models/IModelLibrary.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Actors/Drawing/GameItemRenderState.h"
#include "Actors/Drawing/GameItemSpecialDrawing.h"
#include "Actors/Drawing/ItemDrawing.h"
#include "Camera/SceneCameras.h"

#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Task 1 of docs/superpowers/plans/2026-11-23-item-glow-unification.md: one worn part drawn by the first port
// (DrawCharacter, CharacterDrawPass::Parts) and by the full port (DrawWornParts) over the same pose, and the first
// thing that differs. Each row names the difference of the plan that explains it.
namespace
{
using Client::Crt::CrtRandom;
using Client::Data::Models::LoadModelFile;
using Client::Data::Models::Model;
using Client::Data::Models::ModelFile;
using Client::GameLogic::BodyPart;
using Client::GameLogic::CharacterAppearance;
using Client::GameLogic::WornItem;
using Client::Models::IModelLibrary;
using Client::Models::Skeleton;
using Client::Render::ModelBatch;
using Client::Render::ModelDrawCommand;
using Client::Render::ModelDrawState;
using Client::Render::ModelVertex;
using Client::Render::TextureSlot;
using Client::Scenes::DrawnCharacter;
using Client::Scenes::ItemDrawContext;
using Client::Scenes::ItemPart;
using Client::Scenes::ItemPartOwner;
using Client::Scenes::ItemSpecialDrawContext;
using Client::Scenes::ObjectOutlinePass;
using Client::Scenes::WornParts;

constexpr std::uint16_t ArmorMale17 = 0x1212 + 16;
constexpr std::uint16_t ArmorMale10 = 0x1212 + 9;
constexpr std::uint16_t ArmorChrome = 0x1221;
constexpr std::uint16_t ArmorClass = 0x221a;
constexpr std::uint8_t SecondClassBit = 8;
constexpr std::size_t ArmorPart = 2;

// The clock of the selection trace, where the ancient pulse is at its floor (0.0029), and one where it is not (0.9).
constexpr std::int64_t TraceClock = 26437420;
constexpr std::int64_t PulseOnClock = 1571;

class PartModels final : public IModelLibrary
{
public:
    [[nodiscard]] bool Load(const std::filesystem::path& dataRoot, std::uint16_t model)
    {
        return LoadOne(dataRoot, Client::Tables::PlayerSkeletonModel) && LoadOne(dataRoot, model);
    }

    [[nodiscard]] const Model* Find(std::uint16_t model) const override
    {
        const auto found = m_models.find(model);
        return found == m_models.end() ? nullptr : &found->second;
    }

    [[nodiscard]] const Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        const auto found = m_skeletons.find(model);
        return found == m_skeletons.end() ? nullptr : &found->second;
    }

    [[nodiscard]] std::span<const TextureSlot> MeshTextures(std::uint16_t model) const override
    {
        const auto found = m_slots.find(model);
        return found == m_slots.end() ? std::span<const TextureSlot>{} : std::span<const TextureSlot>(found->second);
    }

private:
    static std::optional<ModelFile> FileOf(std::uint16_t model)
    {
        for (const std::span<const ModelFile> table :
             {Client::Tables::PlayerModelFiles(), Client::Tables::ItemModelFiles()})
        {
            for (const ModelFile& file : table)
            {
                if (file.model == model)
                    return file;
            }
        }
        return std::nullopt;
    }

    bool LoadOne(const std::filesystem::path& dataRoot, std::uint16_t model)
    {
        if (m_models.contains(model))
            return true;
        const std::optional<ModelFile> file = FileOf(model);
        if (!file)
        {
            ADD_FAILURE() << "no table has model " << model;
            return false;
        }
        auto loaded = LoadModelFile(dataRoot, *file);
        if (!loaded.HasValue())
        {
            ADD_FAILURE() << "model " << model << " did not load";
            return false;
        }
        Model read = std::move(loaded).Value();
        auto skeleton = model == Client::Tables::PlayerSkeletonModel ? Client::Assets::MakePlayerSkeleton(read)
                                                                     : Client::Models::MakeSkeleton(read);
        if (!skeleton.HasValue())
        {
            ADD_FAILURE() << "the skeleton of model " << model;
            return false;
        }
        std::vector<TextureSlot> slots;
        for (std::size_t index = 0; index < read.meshes.size(); ++index)
        {
            const auto fixed = Client::Data::Models::SkinSlotOf(read.meshes[index].textureName);
            slots.push_back(TextureSlot{fixed.value_or(static_cast<std::uint16_t>(0x700 + index))});
        }
        m_slots.emplace(model, std::move(slots));
        m_skeletons.emplace(model, std::move(skeleton).Value());
        m_models.emplace(model, std::move(read));
        return true;
    }

    std::map<std::uint16_t, Model> m_models;
    std::map<std::uint16_t, Skeleton> m_skeletons;
    std::map<std::uint16_t, std::vector<TextureSlot>> m_slots;
};

struct ItemDrawCase
{
    const char* name = "";
    std::uint16_t model = ArmorMale17;
    std::uint8_t level = 0;
    bool excellent = false;
    bool ancient = false;
    std::uint8_t characterClass = 0;
    std::uint8_t detail = 4;
    bool outlined = false;
    float alpha = 1.0f;
    std::int64_t clock = TraceClock;
};

enum class Aspect : std::uint8_t
{
    Same,
    Count,
    State,
    Position,
    Uv,
    Color,
    Random,
};

struct Expectation
{
    Aspect aspect = Aspect::Same;
    // The row of "Where the two ports differ" in the plan; 0 when nothing differs.
    int difference = 0;
};

struct Row
{
    ItemDrawCase drawn;
    Expectation expected;
};

void PrintTo(const Row& row, std::ostream* out)
{
    *out << row.drawn.name;
}

std::ostream& operator<<(std::ostream& out, Aspect aspect)
{
    constexpr std::array<const char*, 7> Names{"same", "count", "state", "position", "uv", "color", "random"};
    return out << Names[static_cast<std::size_t>(aspect)];
}

struct Drawn
{
    ModelBatch batch;
    std::uint32_t random = 0;
};

struct Found
{
    Aspect aspect = Aspect::Same;
    std::size_t vertex = 0;
};

constexpr float PositionError = 0.001f;
constexpr float UvError = 0.0001f;
constexpr float ColorError = 0.0001f;

std::vector<ModelDrawState> StatePerVertex(const ModelBatch& batch)
{
    std::vector<ModelDrawState> states(batch.Vertices().size());
    for (const ModelDrawCommand& command : batch.Commands())
    {
        for (std::size_t k = 0; k < command.vertexCount; ++k)
            states[command.firstVertex + k] = command.state;
    }
    return states;
}

bool Near(float a, float b, float error)
{
    return std::fabs(a - b) <= error;
}

Found FirstDifference(const Drawn& first, const Drawn& full)
{
    const auto a = first.batch.Vertices();
    const auto b = full.batch.Vertices();
    const auto statesA = StatePerVertex(first.batch);
    const auto statesB = StatePerVertex(full.batch);
    for (std::size_t k = 0; k < std::min(a.size(), b.size()); ++k)
    {
        if (statesA[k].texture != statesB[k].texture || statesA[k].blend != statesB[k].blend)
            return {Aspect::State, k};
        const ModelVertex& x = a[k];
        const ModelVertex& y = b[k];
        for (int axis = 0; axis < 3; ++axis)
        {
            if (!Near(x.position[axis], y.position[axis], PositionError))
                return {Aspect::Position, k};
        }
        if (!Near(x.uv.x, y.uv.x, UvError) || !Near(x.uv.y, y.uv.y, UvError))
            return {Aspect::Uv, k};
        if (!Near(x.color.red, y.color.red, ColorError) || !Near(x.color.green, y.color.green, ColorError) ||
            !Near(x.color.blue, y.color.blue, ColorError) || !Near(x.color.alpha, y.color.alpha, ColorError))
            return {Aspect::Color, k};
    }
    if (a.size() != b.size())
        return {Aspect::Count, std::min(a.size(), b.size())};
    if (first.random != full.random)
        return {Aspect::Random, 0};
    return {};
}

DrawnCharacter CharacterOf(const ItemDrawCase& drawn)
{
    DrawnCharacter character;
    CharacterAppearance appearance;
    appearance.characterClass = drawn.characterClass;
    appearance.bodyParts[static_cast<std::size_t>(BodyPart::Armor)] =
        WornItem{drawn.model, drawn.level, drawn.excellent, drawn.ancient};
    character.appearance = appearance;
    character.object = Client::Models::ObjectTransform{{12880.0f, 15046.0f, 163.0f}, {0.0f, 0.0f, 100.0f}, 0.9f, 0.0f};
    character.alpha = drawn.alpha;
    character.outlined = drawn.outlined;
    return character;
}

Client::Render::Pass3D TestPass()
{
    return Client::Render::MakePass3D(Client::Core::PixelSize{1024, 768}, Client::Scenes::WorldViewport,
                                      Client::Scenes::SelectionCamera());
}

Drawn DrawFirst(const PartModels& models, const ItemDrawCase& drawn)
{
    Drawn result;
    result.batch.Begin(TestPass());
    Client::Scenes::DrawCharacter(result.batch, models, CharacterOf(drawn), std::chrono::milliseconds{drawn.clock},
                                  Client::Scenes::CharacterDrawPass::Parts);
    result.random = CrtRandom{}.State();
    return result;
}

// What the worn parts draw (DrawWornParts) does for one armor, with the outline of a player (+0x88 in 1, +0x94 in
// 1.2).
Drawn DrawFull(const PartModels& models, const ItemDrawCase& drawn)
{
    Drawn result;
    result.batch.Begin(TestPass());
    const DrawnCharacter character = CharacterOf(drawn);
    const auto body = Client::Scenes::PoseCharacterBody(models, character);
    const Client::Core::Duration clock = std::chrono::milliseconds{drawn.clock};
    constexpr float PlayerOutlineScale = 1.2f;
    const auto inner = static_cast<float>(static_cast<double>(PlayerOutlineScale) -
                                          static_cast<double>(std::bit_cast<float>(0x3ca3d70au)));
    const std::array<ObjectOutlinePass, 2> passes{
        {{PlayerOutlineScale,
          Client::Render::Color{std::bit_cast<float>(0x3dcccccdu), std::bit_cast<float>(0x3cf5c28fu), 0.0f, 1.0f}},
         {inner,
          Client::Render::Color{std::bit_cast<float>(0x3f333333u), std::bit_cast<float>(0x3e4ccccdu), 0.0f, 1.0f}}}};
    float blendMeshLight = 1.0f;
    ItemPartOwner owner;
    owner.transform = character.object;
    owner.bones = body;
    owner.light = character.light;
    owner.alpha = character.alpha;
    owner.blendMeshLight = &blendMeshLight;
    if (drawn.outlined)
        owner.outline = passes;
    WornParts parts{};
    parts[ArmorPart] = ItemPart{.model = drawn.model,
                                .level = drawn.level,
                                .excellent = drawn.excellent ? std::uint8_t{1} : std::uint8_t{0},
                                .ancient = drawn.ancient ? std::uint8_t{1} : std::uint8_t{0}};
    const auto worldClock = static_cast<float>(drawn.clock);
    CrtRandom random;
    Client::Scenes::GameItemRenderState render;
    Client::Scenes::UpdateGameItemGlowPulse(render, worldClock);
    ItemSpecialDrawContext special;
    special.flags = 2;
    special.worldClock = worldClock;
    special.random = &random;
    special.pulse = &render.specialPulse;
    ItemDrawContext context;
    context.special = &special;
    context.glowPulse = render.glowPulse;
    context.selections = &render.selections;
    context.glowDetail = drawn.detail;
    const auto done =
        Client::Scenes::DrawWornParts(result.batch, models, owner, parts, drawn.characterClass, clock, context, {});
    EXPECT_TRUE(done.HasValue()) << (done.HasValue() ? std::string{} : done.Error());
    result.random = random.State();
    return result;
}

class ItemDrawEquivalence : public Client::Tests::GoldenDataTest, public ::testing::WithParamInterface<Row>
{
};

TEST_P(ItemDrawEquivalence, DiffersOnlyWhereThePlanSays)
{
    const Row& row = GetParam();
    PartModels models;
    ASSERT_TRUE(models.Load(m_dataRoot, row.drawn.model));
    const Drawn first = DrawFirst(models, row.drawn);
    const Drawn full = DrawFull(models, row.drawn);
    const Found found = FirstDifference(first, full);
    EXPECT_EQ(found.aspect, row.expected.aspect)
        << "first difference at vertex " << found.vertex << " of " << first.batch.Vertices().size() << " and "
        << full.batch.Vertices().size() << " (plan difference " << row.expected.difference << ")";
}

constexpr Expectation Same{};

INSTANTIATE_TEST_SUITE_P(
    WornArmor, ItemDrawEquivalence,
    ::testing::Values(
        Row{{.name = "Level0"}, Same}, Row{{.name = "Level1", .level = 1}, Same},
        Row{{.name = "Level2", .level = 2}, Same}, Row{{.name = "Level3", .level = 3}, {Aspect::Color, 2}},
        Row{{.name = "Level4", .level = 4}, {Aspect::Color, 2}},
        Row{{.name = "Level5", .level = 5}, {Aspect::Color, 2}},
        Row{{.name = "Level6", .level = 6}, {Aspect::Color, 2}}, Row{{.name = "Level7", .level = 7}, Same},
        Row{{.name = "Level8", .level = 8}, Same}, Row{{.name = "Level9", .level = 9}, Same},
        Row{{.name = "Level10", .level = 10}, Same}, Row{{.name = "Level11", .level = 11}, Same},
        Row{{.name = "Level12", .level = 12}, Same}, Row{{.name = "Level13", .level = 13}, Same},
        Row{{.name = "Level14", .level = 14}, Same}, Row{{.name = "Level15", .level = 15}, Same},
        Row{{.name = "Level13OddClock", .level = 13, .clock = TraceClock + 1}, {Aspect::Uv, 5}},
        Row{{.name = "Level13ExcellentDetail0", .level = 13, .excellent = true, .detail = 0}, {Aspect::Color, 1}},
        Row{{.name = "Level9Detail1", .level = 9, .detail = 1}, {Aspect::Color, 1}},
        Row{{.name = "Level0ExcellentDetail0", .excellent = true, .detail = 0}, {Aspect::Count, 1}},
        Row{{.name = "Level0Excellent", .excellent = true}, Same},
        Row{{.name = "AncientPulseAtTheFloor", .ancient = true}, {Aspect::Random, 4}},
        Row{{.name = "AncientPulseOn", .ancient = true, .clock = PulseOnClock}, Same},
        Row{{.name = "Level13Outlined", .level = 13, .outlined = true}, Same},
        Row{{.name = "Level0HalfAlpha", .alpha = 0.5f}, Same}, Row{{.name = "Level0Alpha001", .alpha = 0.01f}, Same},
        Row{{.name = "Male10Level0", .model = ArmorMale10}, Same},
        Row{{.name = "ArmorChromeLevel0", .model = ArmorChrome}, {Aspect::State, 3}},
        Row{{.name = "ClassArmorClass0", .model = ArmorClass}, Same},
        Row{{.name = "ClassArmorClass1", .model = ArmorClass + 1, .characterClass = 1}, Same},
        Row{{.name = "ClassArmorSecondClass", .model = ArmorClass + 5, .characterClass = SecondClassBit}, Same}),
    [](const ::testing::TestParamInfo<Row>& param) { return std::string(param.param.drawn.name); });
} // namespace
