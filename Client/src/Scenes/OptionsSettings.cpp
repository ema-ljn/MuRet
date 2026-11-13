#include "Scenes/OptionsSettings.h"

#include "Scenes/WorldRuntime.h"
#include "UI/OptionsWindow.h"

namespace Client::Scenes
{
UI::OptionsSettings OptionsSettingsOf(const WorldRuntime& runtime)
{
    return UI::OptionsSettings{.autoAttack = runtime.ui.autoAttack,
                               .noticeSound = runtime.chat.noticeSound,
                               .slidesOn = runtime.chat.slidesOn,
                               .volume = runtime.ui.volume,
                               .glowDetail = runtime.ui.glowDetail};
}

void ApplyOption(WorldRuntime& runtime, const UI::OptionsEvent& event)
{
    UI::OptionsSettings settings = OptionsSettingsOf(runtime);
    UI::ApplyOption(settings, event);
    runtime.ui.autoAttack = settings.autoAttack;
    runtime.chat.noticeSound = settings.noticeSound;
    runtime.chat.slidesOn = settings.slidesOn;
    runtime.ui.volume = settings.volume;
    runtime.ui.glowDetail = settings.glowDetail;
}
} // namespace Client::Scenes
