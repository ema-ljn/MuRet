#include "Scenes/MapServerMoveApply.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/GameData.h"
#include "Scenes/GameDialogSteps.h"
#include "Scenes/MapLoad.h"
#include "Scenes/ReturnToSelection.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/MacroFile.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace Client::Scenes
{
namespace
{
// 0x412c10: 0x510f70 con −1, que solo deja vivo a uno con la clave 0xffff (Q-C212).
constexpr std::int32_t NoKey = -1;
} // namespace

Core::Expected<void, std::string> ApplyMapServerLeave(const WorldEntryResources& resources,
                                                      std::optional<std::uint8_t> chosenSlot)
{
    // 0x412b63: 0x61c210. El C1 1C F3 30 va al socket que 0x4c1680 ya cerró: send falla y no sale nada.
    auto body = OptionBodyOf(resources.world, resources.cloth.random, chosenSlot);
    if (!body.HasValue())
        return Core::Unexpected(std::move(body).Error());
    // 0x412b6d: 0x61c170("Data\\Macro.txt").
    return WriteMacroFile(resources.dataRoot, UI::WrittenMacros(resources.world.Runtime().chat.input.macros));
}

void ApplyMapServerJoin(const WorldEntryResources& resources)
{
    KillOtherActors(resources.world, NoKey);
    ResetForTheSelection(resources);
    static_cast<void>(resources.cloth.random.Next());
    std::uint8_t serialDepth = 0;
    {
        const Render::Models::LogicalAccess serial(serialDepth, resources.cloth.random,
                                                   Render::Models::SerialCloseRandoms);
    }
}
} // namespace Client::Scenes
