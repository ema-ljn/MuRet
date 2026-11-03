#include "Scenes/LoginScene.h"

#include "Core/Log.h"
#include "Render/TextureSlot.h"
#include "Scenes/GameData.h"
#include "Scenes/LoadingScene.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/TextureSets.h"
#include "UI/Notices.h"
#include "UI/ServerListModel.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// La carga y el login con el Data\ del laboratorio: entran (sus juegos de texturas en sus slots
// de la tabla falsa, el login con las de las ventanas), y el login dibuja sus tres textos de
// Text.bmd. Ningún texto va a la salida:
// se miran el largo y dónde quedó la versión, nunca lo que dicen.
namespace
{
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Render::TextureSlot;
using Client::Scenes::LoadGameData;
using Client::Scenes::LoadingScene;
using Client::Scenes::LoadingTextureSet;
using Client::Scenes::LoginScene;
using Client::Scenes::LoginTextureSet;
using Client::Scenes::SceneContext;
using Client::Scenes::WindowTextureSet;
using Client::Tests::FakeTextureStore;
using Client::Tests::RecordedText;
using Client::Tests::RecordingFrameTarget;
using Client::Tests::RecordingSink;
using Client::UI::FirstButtonSlot;
using Client::UI::GroupNameText;
using Client::UI::GroupSlot;
using Client::UI::LoadText;
using Client::UI::LoginTitleText;
using Client::UI::NoticeId;
using Client::UI::NoticeTexts;
using Client::UI::ServerGroupCount;
using Client::UI::TextsOf;

using GoldenLoginScene = Client::Tests::GoldenDataTest;

constexpr std::string_view Version = "0.0.0";

TEST_F(GoldenLoginScene, EntersWithTheLabData)
{
    auto data = LoadGameData(m_dataRoot);
    ASSERT_TRUE(data.HasValue()) << data.Error();
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    LoginScene scene(SceneContext{m_dataRoot, store, log}, data.Value(), nullptr, Version);

    const auto entered = scene.Enter();

    ASSERT_TRUE(entered.HasValue()) << entered.Error();
    ASSERT_EQ(store.Size(), LoginTextureSet.size() + WindowTextureSet.size());
    for (const auto& entry : LoginTextureSet)
    {
        EXPECT_TRUE(store.Find(TextureSlot{entry.slot}).has_value()) << entry.slot;
    }
    for (const auto& entry : WindowTextureSet)
    {
        EXPECT_TRUE(store.Find(TextureSlot{entry.slot}).has_value()) << entry.slot;
    }

    RecordingFrameTarget target;
    scene.Render(target.BeginFrame(std::chrono::milliseconds{40}), 0.0f);
    target.Present();
    scene.Leave();

    // Los tres están en el Text.bmd del laboratorio, y el 456 tiene un "%s" para la versión.
    const std::vector<RecordedText>& texts = target.Texts();
    ASSERT_EQ(texts.size(), 3u);
    for (const RecordedText& text : texts)
    {
        EXPECT_FALSE(text.text.empty());
    }
    EXPECT_NE(texts[2].text.find(Version), std::string::npos);
    EXPECT_EQ(texts[2].text.find("%s"), std::string::npos);
    EXPECT_EQ(store.Size(), 0u);
}

// Los textos de las ventanas (CL5.5), por índice: los de cada aviso que abre el login (los del F1
// 01 de 0x15 a 0x26 y los suyos), los rótulos de la cuenta (450 y 451), los dos títulos, las tres
// cargas y el nombre de cada grupo con botón. Todos están y no son vacíos; si uno falla, sale su
// índice y no el texto.
TEST_F(GoldenLoginScene, TheTextsOfItsWindowsAreInTheLabTable)
{
    auto data = LoadGameData(m_dataRoot);
    ASSERT_TRUE(data.HasValue()) << data.Error();
    ASSERT_TRUE(data.Value().text.has_value());
    std::set<std::size_t> indices{450,         451,           LoginTitleText(4, 1), LoginTitleText(5, 1),
                                  LoadText(0), LoadText(100), LoadText(128)};
    for (int notice = 0x15; notice <= 0x70; ++notice)
    {
        if (const std::optional<NoticeTexts> texts = TextsOf(static_cast<NoticeId>(notice)))
        {
            indices.insert(texts->first);
            if (texts->second)
            {
                indices.insert(*texts->second);
            }
        }
    }
    for (std::uint16_t group = 0; group < ServerGroupCount; ++group)
    {
        if (const std::optional<int> slot = GroupSlot(group); slot && *slot >= FirstButtonSlot)
        {
            indices.insert(GroupNameText(group));
        }
    }

    for (const std::size_t index : indices)
    {
        const std::optional<std::string_view> text = data.Value().text->Text(index);
        EXPECT_TRUE(text.has_value() && !text->empty()) << "el texto " << index;
    }
}

TEST_F(GoldenLoginScene, TheLoadingBeforeItEntersToo)
{
    FakeTextureStore store;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    LoadingScene scene(SceneContext{m_dataRoot, store, log});

    const auto entered = scene.Enter();

    ASSERT_TRUE(entered.HasValue()) << entered.Error();
    EXPECT_EQ(store.loaded, (std::vector<std::uint16_t>{0x71e, 0x71f, 0x720, 0x721, 0x722, 0x723}));
    scene.Leave();
    EXPECT_EQ(store.released.size(), LoadingTextureSet.size());
}
} // namespace
