#include "Data/Models/ModelLoadCalls.h"

#include "Data/Models/Model.h"
#include "Data/Models/ModelFiles.h"
#include "Data/Models/ModelTextureCalls.h"
#include "Data/Textures/TextureSet.h"
#include "Models/ModelTextureSlots.h"
#include "Render/TextureSlot.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
namespace Models = Client::Data::Models;

TEST(ModelLoadCallsTest, SiegeModelPathsAndNumbersFollowTheNativeBmdCalls)
{
    const std::array<std::string_view, 15> expected{
        "Skill\\BigStone01.bmd",       "Skill\\BigStone02.bmd",   "Object12\\StoneCoffin01.bmd",
        "Object12\\StoneCoffin02.bmd", "Skill\\m_Waves.bmd",      "Skill\\m_Piercing.bmd",
        "Skill\\FlyBigStone1.bmd",     "Skill\\FlyBigStone2.bmd", "Skill\\FlySmallStone1.bmd",
        "Skill\\FlySmallStone2.bmd",   "Skill\\WallStone1.bmd",   "Skill\\WallStone2.bmd",
        "Skill\\GatePart1.bmd",        "Skill\\GatePart2.bmd",    "Skill\\GatePart3.bmd"};
    const auto files = Models::SiegeModelFiles();
    ASSERT_EQ(files.size(), expected.size());
    for (std::size_t i = 0; i < files.size(); ++i)
        EXPECT_EQ(std::string(files[i].folder) + Models::ModelFileName(files[i].name, files[i].number), expected[i]);
}

TEST(ModelLoadCallsTest, CoffinsFollowTheRepeatedGateCallBeforeTheSkillSlotReset)
{
    const auto calls = Models::SiegeBeforeSkillTextureCalls();
    const std::array<std::uint16_t, 4> expected{0x10d, 0x10a, 0x10d, 0x10b};
    ASSERT_EQ(calls.size(), expected.size());
    for (std::size_t i = 0; i < calls.size(); ++i)
    {
        EXPECT_EQ(calls[i].model, expected[i]);
        EXPECT_EQ(calls[i].folder, "Monster\\");
    }
}

TEST(ModelLoadCallsTest, TheFinalSkillQueriesKeepTheFirstTextureFolderThroughTheNativeNameCache)
{
    std::map<std::uint16_t, Models::Model> models;
    for (const auto& file : Models::SiegeModelFiles())
    {
        Models::Mesh mesh;
        mesh.textureName = "castle" + std::to_string(file.model) + ".jpg";
        models[file.model].meshes.push_back(std::move(mesh));
    }
    Models::Mesh gate;
    gate.textureName = "gate.jpg";
    models[0x10d].meshes.push_back(std::move(gate));
    Client::Models::ModelTextureSlots slots(Client::Render::TextureSlot{0x69});
    std::map<std::uint16_t, std::vector<Client::Render::TextureSlot>> bindings;
    for (const auto& call : Models::SiegeBeforeSkillTextureCalls())
        bindings[call.model] = slots.Assign(models.at(call.model), call.folder, Models::ModelTextureSampling);
    slots.SetNext(Client::Render::TextureSlot{0x3b8});
    for (const auto& call : Models::SiegeTextureCalls())
        bindings[call.model] = slots.Assign(models.at(call.model), call.folder, Models::ModelTextureSampling);
    ASSERT_EQ(slots.ToLoad().size(), 16u); // One per model and Gate02, despite the repeated lookups.
    for (const auto& file : Models::SiegeModelFiles())
    {
        const std::string name = "castle" + std::to_string(file.model) + ".jpg";
        ASSERT_EQ(bindings.at(file.model).size(), 1u);
        const auto slot = bindings.at(file.model).front();
        const auto entry =
            std::ranges::find(slots.ToLoad(), slot.value, &Client::Data::Textures::TextureSetEntry::slot);
        ASSERT_NE(entry, slots.ToLoad().end());
        std::string folder = "Skill\\";
        if (file.model == 0x10a || file.model == 0x10b || (file.model >= 0x131 && file.model <= 0x133))
            folder = "Monster\\";
        else if (file.model == 0x12b)
            folder = "Npc\\";
        else if (file.model == 0x12f || file.model == 0x130)
            folder = "Object31\\";
        EXPECT_EQ(entry->path, folder + name);
        EXPECT_EQ(entry->sampling.filter, Models::ModelTextureSampling.filter);
        EXPECT_EQ(entry->sampling.wrap, Models::ModelTextureSampling.wrap);
    }
}
} // namespace
