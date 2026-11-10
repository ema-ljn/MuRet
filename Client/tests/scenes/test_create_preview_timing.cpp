#include "Scenes/CharacterSelectScene.h"

#include "Core/IClock.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemTable.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Pose.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameData.h"
#include "Scenes/LoginModels.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldState.h"
#include "UI/CreateCharacterWindow.h"
#include "World/GameCamera.h"
#include "World/WorldTerrain.h"

#include "support/FakeClock.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/PlayerActors.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include "support/UiInput.h"

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

// 0x63e460: la cámara de 0x6338a0 precede los stores de ElfX+4,8 (0x63e5b7) y MGZ+1,8
// (0x63e5f7). La ventana cerrada no dibuja la vista previa y no debe recuperar esos dibujos al abrir.
namespace
{
using Client::Render::Models::OriginalFrameTime;

glm::vec3 ClassOffset(std::uint8_t characterClass)
{
    return characterClass == 2 ? glm::vec3{4.8f, 0.0f, 0.0f} : glm::vec3{0.0f, 0.0f, 1.8f};
}

class CreatePreviewTimingTest : public Client::Tests::GoldenDataTest
{
protected:
    void SetUp() override
    {
        Client::Tests::GoldenDataTest::SetUp();
        ASSERT_FALSE(HasFatalFailure());
        m_models.Set(Client::Data::Models::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
        m_data.items =
            Client::Data::Tables::LoadedItemTable{.table = Client::Tests::ItemsMarking({}), .checksumMatches = true};
        const Client::Scenes::SceneContext context{.dataRoot = m_dataRoot,
                                                   .textures = m_store,
                                                   .log = m_log,
                                                   .clock = &m_clock,
                                                   .cloth = &m_cloth,
                                                   .input = &m_input,
                                                   .camera = &m_camera,
                                                   .cameraInput = &m_cameraInput,
                                                   .selection = &m_selection,
                                                   .world = &m_world};
        m_scene = std::make_unique<Client::Scenes::CharacterSelectScene>(context, m_data, m_models, nullptr);
        const auto entered = m_scene->Enter();
        ASSERT_TRUE(entered.HasValue()) << entered.Error();
        auto terrain = Client::World::LoadWorldTerrain(m_dataRoot, 54);
        ASSERT_TRUE(terrain.HasValue()) << terrain.Error();
        m_selection.RetainTerrain(54, std::move(terrain).Value());
        ASSERT_NO_FATAL_FAILURE(Step());
        ASSERT_EQ(&m_scene->PreviewActor(), &m_selection.PreviewActor());
    }

    void TearDown() override
    {
        if (m_scene != nullptr)
            m_scene->Leave();
    }

    void Step()
    {
        m_scene->Step(m_input);
        m_input.ConsumeEdges();
        ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
    }

    void OpenWindow()
    {
        Client::Tests::ClickAt(m_input, 30, 410);
        ASSERT_NO_FATAL_FAILURE(Step());
        ASSERT_TRUE(m_scene->WantsTextInput());
    }

    void CloseWindow()
    {
        Client::Tests::TapKey(m_input, Client::Input::Key::Escape);
        ASSERT_NO_FATAL_FAILURE(Step());
        ASSERT_FALSE(m_scene->WantsTextInput());
    }

    void ChooseClass(std::uint8_t characterClass)
    {
        const auto layout = Client::UI::LayoutCreateWindow({640, 480}, characterClass);
        Client::Tests::ClickAt(m_input, layout.classes[characterClass].x + 5, layout.classes[characterClass].y + 5);
        ASSERT_NO_FATAL_FAILURE(Step());
        ASSERT_EQ(m_scene->PreviewActor().characterClass, characterClass);
    }

    void DrawAt(Client::Core::Duration clock)
    {
        m_clock.Advance(clock - m_clock.Now());
        m_scene->Render(m_target.BeginFrame(clock), 0.0f);
        ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
        m_target.Present();
    }

    [[nodiscard]] const Client::Tests::FrameEvent* LastPreviewPass() const
    {
        for (const Client::Tests::FrameEvent& event : m_target.frames.back().events)
        {
            if (event.kind == Client::Tests::FrameEvent::Kind::Models && event.pass.has_value() &&
                event.pass->viewport.z == 410)
                return &event;
        }
        return nullptr;
    }

    void ExpectCameraBeforeTheClassOffset(const glm::vec3& position) const
    {
        const Client::Tests::FrameEvent* preview = LastPreviewPass();
        ASSERT_NE(preview, nullptr);
        ASSERT_FALSE(preview->modelVertices.empty());
        // El origen del ojo es la posición anterior del actor + (10,-500,48), no la posición dibujada.
        const glm::vec4 eye = preview->pass->view * glm::vec4(position + glm::vec3{10.0f, -500.0f, 48.0f}, 1.0f);
        EXPECT_NEAR(eye.x, 0.0f, 0.001f);
        EXPECT_NEAR(eye.y, 0.0f, 0.001f);
        EXPECT_NEAR(eye.z, 0.0f, 0.001f);
    }

    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
    Client::Tests::FakeTextureStore m_store;
    Client::Tests::FakeClock m_clock;
    Client::Input::InputState m_input;
    Client::Render::Models::ClothWorld m_cloth;
    Client::Scenes::WorldState m_world;
    Client::Scenes::SelectionState m_selection{m_world};
    Client::World::GameCamera m_camera;
    Client::World::GameCameraInput m_cameraInput;
    Client::Scenes::SingleModelLibrary m_models;
    Client::Scenes::GameData m_data;
    Client::Tests::RecordingFrameTarget m_target;
    std::unique_ptr<Client::Scenes::CharacterSelectScene> m_scene;
};

struct ReopenCase
{
    std::uint8_t initialClass;
    std::uint8_t reopenedClass;
    bool drawClosedFrames;
};

class CreatePreviewReopenTest : public CreatePreviewTimingTest, public ::testing::WithParamInterface<ReopenCase>
{
};

TEST_P(CreatePreviewReopenTest, ReopeningCountsOneDrawAndKeepsTheCameraBeforeTheClassOffset)
{
    const ReopenCase probe = GetParam();
    ASSERT_NO_FATAL_FAILURE(OpenWindow());
    ASSERT_NO_FATAL_FAILURE(ChooseClass(probe.initialClass));
    const Client::Scenes::ActorState* durable = &m_selection.PreviewActor();
    const glm::vec3 first = durable->position;
    ASSERT_NO_FATAL_FAILURE(DrawAt({}));
    EXPECT_EQ(durable->position, first + ClassOffset(probe.initialClass));
    ASSERT_NO_FATAL_FAILURE(ExpectCameraBeforeTheClassOffset(first));
    ASSERT_NO_FATAL_FAILURE(CloseWindow());
    const glm::vec3 closed = durable->position;
    const std::uint64_t revision = durable->revision;
    if (probe.drawClosedFrames)
    {
        for (const int frame : {2, 6, 10})
        {
            ASSERT_NO_FATAL_FAILURE(DrawAt(OriginalFrameTime * frame));
            EXPECT_EQ(LastPreviewPass(), nullptr);
            EXPECT_EQ(durable->position, closed);
            EXPECT_EQ(durable->revision, revision);
        }
    }
    else
        m_clock.Advance(OriginalFrameTime * 10);
    ASSERT_NO_FATAL_FAILURE(OpenWindow());
    if (probe.reopenedClass != probe.initialClass)
    {
        ASSERT_NO_FATAL_FAILURE(ChooseClass(probe.reopenedClass));
        EXPECT_GT(durable->revision, revision);
    }
    else
    {
        EXPECT_EQ(durable->position, closed);
        EXPECT_EQ(durable->revision, revision);
    }
    ASSERT_EQ(&m_scene->PreviewActor(), durable);
    const glm::vec3 reopened = durable->position;

    ASSERT_NO_FATAL_FAILURE(DrawAt(OriginalFrameTime * 12));

    EXPECT_EQ(durable->position, reopened + ClassOffset(probe.reopenedClass));
    EXPECT_EQ(durable->characterClass, probe.reopenedClass);
    EXPECT_EQ(durable->type, 0xa8 + probe.reopenedClass);
    ASSERT_NO_FATAL_FAILURE(ExpectCameraBeforeTheClassOffset(reopened));
}

std::string ReopenCaseName(const ::testing::TestParamInfo<ReopenCase>& parameter)
{
    return std::string(parameter.param.initialClass == 2 ? "Elf" : "MG") + "To" +
           (parameter.param.reopenedClass == 2 ? "Elf" : "MG") +
           (parameter.param.drawClosedFrames ? "DrawnClosed" : "BetweenDraws");
}

INSTANTIATE_TEST_SUITE_P(ClassChanges, CreatePreviewReopenTest,
                         ::testing::Values(ReopenCase{2, 2, true}, ReopenCase{3, 3, true}, ReopenCase{2, 3, true},
                                           ReopenCase{3, 2, true}, ReopenCase{2, 2, false}, ReopenCase{3, 3, false},
                                           ReopenCase{2, 3, false}, ReopenCase{3, 2, false}),
                         ReopenCaseName);

class CreatePreviewOpenClockTest : public CreatePreviewTimingTest, public ::testing::WithParamInterface<std::uint8_t>
{
};

TEST_P(CreatePreviewOpenClockTest, AnOpenWindowKeepsZeroFrameAndCatchUpDrawing)
{
    const std::uint8_t characterClass = GetParam();
    ASSERT_NO_FATAL_FAILURE(OpenWindow());
    ASSERT_NO_FATAL_FAILURE(ChooseClass(characterClass));
    ASSERT_NO_FATAL_FAILURE(DrawAt({}));
    ASSERT_NE(LastPreviewPass(), nullptr);
    const auto view = LastPreviewPass()->pass->view;
    const auto animation = m_scene->PreviewActor().animation;
    const glm::vec3 before = m_scene->PreviewActor().position;

    ASSERT_NO_FATAL_FAILURE(DrawAt(OriginalFrameTime / 2));

    EXPECT_EQ(m_scene->PreviewActor().position, before);
    ASSERT_NE(LastPreviewPass(), nullptr);
    EXPECT_EQ(LastPreviewPass()->pass->view, view);
    EXPECT_EQ(m_scene->PreviewActor().animation.action, animation.action);
    EXPECT_EQ(m_scene->PreviewActor().animation.frame, animation.frame);
    EXPECT_EQ(m_scene->PreviewActor().animation.previousAction, animation.previousAction);
    EXPECT_EQ(m_scene->PreviewActor().animation.previousKey, animation.previousKey);

    ASSERT_NO_FATAL_FAILURE(DrawAt(OriginalFrameTime * 3));

    glm::vec3 expected = before;
    for (int frame = 0; frame < 3; ++frame)
        expected += ClassOffset(characterClass);
    EXPECT_EQ(m_scene->PreviewActor().position, expected);
    ASSERT_NO_FATAL_FAILURE(ExpectCameraBeforeTheClassOffset(expected - ClassOffset(characterClass)));
    EXPECT_EQ(m_scene->PreviewActor().animation.frame, animation.frame);
}

std::string OffsetClassName(const ::testing::TestParamInfo<std::uint8_t>& parameter)
{
    return parameter.param == 2 ? "Elf" : "MG";
}

INSTANTIATE_TEST_SUITE_P(OffsetClasses, CreatePreviewOpenClockTest, ::testing::Values(std::uint8_t{2}, std::uint8_t{3}),
                         OffsetClassName);
} // namespace
