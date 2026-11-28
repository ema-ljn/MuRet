#include "Hud/OptionsSettings.h"

#include "Kernel/WorldRuntime.h"
#include "FrontEnd/OptionsWindow.h"

namespace Client::Hud
{
FrontEnd::OptionsSettings OptionsSettingsOf(const Kernel::WorldRuntime& runtime)
{
    return FrontEnd::OptionsSettings{.autoAttack = runtime.ui.autoAttack,
                                     .noticeSound = runtime.chat.noticeSound,
                                     .slidesOn = runtime.chat.slidesOn,
                                     .volume = runtime.ui.volume,
                                     .glowDetail = runtime.ui.glowDetail};
}

void ApplyOption(Kernel::WorldRuntime& runtime, const FrontEnd::OptionsEvent& event)
{
    FrontEnd::OptionsSettings settings = OptionsSettingsOf(runtime);
    FrontEnd::ApplyOption(settings, event);
    runtime.ui.autoAttack = settings.autoAttack;
    runtime.chat.noticeSound = settings.noticeSound;
    runtime.chat.slidesOn = settings.slidesOn;
    runtime.ui.volume = settings.volume;
    runtime.ui.glowDetail = settings.glowDetail;
}
} // namespace Client::Hud
