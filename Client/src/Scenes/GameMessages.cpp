#include "Scenes/GameMessages.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/ActorState.h"
#include "Scenes/ITextDrawer.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatLog.h"
#include "UI/MessageArea.h"
#include "UI/MessageLog.h"
#include "UI/Widget.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Scenes
{
std::string_view HeroNameOf(const ActorState& hero)
{
    std::size_t size = 0;
    while (size < hero.name.size() && hero.name[size] != std::byte{0})
        ++size;
    return {reinterpret_cast<const char*>(hero.name.data()), size};
}

UI::MessageSink MessageSinkOf(WorldRuntime& runtime, const Render::Text::ITextMeasure& measure, App::PixelSize screen)
{
    ChatRuntime& chat = runtime.chat;
    return UI::MessageSink{.log = chat.log,
                           .area = chat.area,
                           .modes = runtime.ui.chatModes,
                           .measure =
                               UI::ChatMeasure{.measure = measure, .dcFont = runtime.hud.dcFont, .screen = screen},
                           .noticeSound = chat.noticeSound};
}

Core::Expected<void, std::string> AddGameMessage(WorldRuntime& runtime, const Render::Text::ITextMeasure& measure,
                                                 App::PixelSize screen, std::string_view name, std::string_view text,
                                                 std::int32_t type)
{
    auto sound = UI::AddMessage(MessageSinkOf(runtime, measure, screen), name, text, type);
    if (!sound.HasValue())
        return Core::Unexpected(sound.Error());
    if (sound.Value())
        runtime.lastSoundId = *sound.Value();
    return {};
}

Core::Expected<void, std::string> AddTheEmptyMessages(WorldRuntime& runtime, const Render::Text::ITextMeasure* measure)
{
    if (measure == nullptr)
        return {};
    // Dos textos vacíos no se miden con otra pantalla que la de 640 × 480: ": " no llega a los 256 de 0x555190 ni a
    // los 20 bytes del corte de 0x45bb20.
    for (std::size_t i = 0; i < UI::MessageAreaRows; ++i)
    {
        if (auto added = AddGameMessage(runtime, *measure, App::PixelSize{640, 480}, {}, {}, 0); !added.HasValue())
            return added;
    }
    return {};
}

Core::Expected<void, std::string> StepTheMessageClocks(WorldRuntime& runtime, const Render::Text::ITextMeasure& measure,
                                                       App::PixelSize screen)
{
    UI::StepTheNoticeClock(runtime.chat.area, measure, runtime.hud.dcFont);
    auto sound = UI::StepTheMessageClock(MessageSinkOf(runtime, measure, screen));
    if (!sound.HasValue())
        return Core::Unexpected(sound.Error());
    if (sound.Value())
        runtime.lastSoundId = *sound.Value();
    // 0x637d2c y 0x637d44: el freno del chat y el de las macros, de a uno por paso (el 0x637cfd de PrintScreen, entre
    // los relojes y los frenos, no los mira).
    if (runtime.chat.input.brake > 0)
        --runtime.chat.input.brake;
    if (runtime.hud.macroTime > 0)
        --runtime.hud.macroTime;
    return {};
}

void DrawTheMessages(const WorldState& world, HudState& hud, Render::SpriteBatch& sprites, ITextDrawer& text,
                     UI::Point mouse, bool inTheGame)
{
    const WorldRuntime& runtime = world.Runtime();
    const UI::MessageDrawContext context{.sprites = sprites,
                                         .text = text,
                                         .dcFont = hud.dcFont,
                                         .textColors = hud.textColors,
                                         .textWidth = hud.arrowsTextWidth,
                                         .textHeight = hud.arrowsTextHeight};
    UI::DrawTheNotices(runtime.chat.area, hud.messageFrame, context, runtime.ui.chatModes.placement);
    if (runtime.ui.chatModes.placement != 1 && inTheGame)
        return;
    const ActorState& hero = world.Actors()[runtime.hero];
    const UI::MessageAreaView view{.mouseX = mouse.x,
                                   .mouseY = mouse.y,
                                   .chatOpen = runtime.ui.chatOpen,
                                   .heroSafeZone = hero.safeZone != 0,
                                   .heroName = HeroNameOf(hero),
                                   .showChat = runtime.ui.chatModes.showChat,
                                   .crywolfStage = runtime.crywolf.stage};
    UI::DrawTheMessageArea(runtime.chat.area, hud.messageFrame, context, view);
}
} // namespace Client::Scenes
