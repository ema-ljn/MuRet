#include "Platform/Sdl/Clipboard.h"

#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_video.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace
{
char EmptyPayload[] = "";

const void* ProvideEmptyPayload(void* userdata, const char*, std::size_t* size)
{
    *size = 0;
    return userdata;
}

class SdlDummyClipboardTest : public testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
        if (!SDL_Init(SDL_INIT_VIDEO))
            FAIL() << SDL_GetError();
        m_initialized = true;
        ASSERT_STREQ(SDL_GetCurrentVideoDriver(), "dummy");
        ASSERT_TRUE(SDL_ClearClipboardData()) << SDL_GetError();
    }

    void TearDown() override
    {
        if (m_initialized)
            SDL_Quit();
    }

private:
    bool m_initialized = false;
};

TEST(ClipboardWithoutSdlTest, ReadTextWithoutSdlInitializationHasNoValue)
{
    SDL_Quit();
    ASSERT_EQ(SDL_WasInit(0), 0u);

    Client::Platform::Clipboard clipboard;

    EXPECT_FALSE(clipboard.ReadText().has_value());
}

TEST_F(SdlDummyClipboardTest, ClipboardWithoutTextFormatsHasNoValue)
{
    constexpr std::array mimeTypes{"application/octet-stream"};
    ASSERT_TRUE(SDL_SetClipboardData(ProvideEmptyPayload, nullptr, EmptyPayload, mimeTypes.data(), mimeTypes.size()))
        << SDL_GetError();
    ASSERT_TRUE(SDL_HasClipboardData(mimeTypes.front()));
    ASSERT_FALSE(SDL_HasClipboardText());

    Client::Platform::Clipboard clipboard;

    EXPECT_FALSE(clipboard.ReadText().has_value());
}

TEST_F(SdlDummyClipboardTest, ValidEmptyTextRemainsDistinctFromUnavailableText)
{
    constexpr std::array mimeTypes{"text/plain;charset=utf-8"};
    ASSERT_TRUE(SDL_SetClipboardData(ProvideEmptyPayload, nullptr, EmptyPayload, mimeTypes.data(), mimeTypes.size()))
        << SDL_GetError();
    ASSERT_TRUE(SDL_HasClipboardText());

    Client::Platform::Clipboard clipboard;
    const std::optional<std::string> text = clipboard.ReadText();

    ASSERT_TRUE(text.has_value());
    EXPECT_TRUE(text->empty());
}

TEST_F(SdlDummyClipboardTest, TextRoundTripsThroughSdl)
{
    ASSERT_TRUE(SDL_SetClipboardText("€")) << SDL_GetError();

    Client::Platform::Clipboard clipboard;
    const std::optional<std::string> text = clipboard.ReadText();

    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(std::string_view(*text), "€");
}
} // namespace
