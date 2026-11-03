#include "Scenes/LoginScene.h"

#include "Core/Log.h"
#include "Render/TextureSlot.h"
#include "Scenes/GameData.h"
#include "Scenes/LoadingScene.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/TextureSets.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// La carga y el login con el Data\ del laboratorio: entran (sus juegos de texturas en sus slots
// de la tabla falsa), y el login dibuja sus tres textos de Text.bmd. Ningún texto va a la salida:
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
using Client::Tests::FakeTextureStore;
using Client::Tests::RecordedText;
using Client::Tests::RecordingFrameTarget;
using Client::Tests::RecordingSink;

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
    ASSERT_EQ(store.Size(), LoginTextureSet.size());
    for (const auto& entry : LoginTextureSet)
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
