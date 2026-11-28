#include "Session/MapServerMoveApply.h"

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Models/LogicalAccess.h"
#include "Tables/GameData.h"
#include "Hud/Dialogs/GameDialogSteps.h"
#include "Session/MapLoad.h"
#include "Session/ReturnToSelection.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/MacroFile.h"

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
    auto body = Hud::Dialogs::OptionBodyOf(resources.world, resources.cloth.random, chosenSlot);
    if (!body.HasValue())
        return Core::Unexpected(std::move(body).Error());
    // 0x412b6d guarda las macros y, al retornar, 0x412b77 espera antes de conectar.
    const auto saved =
        Tables::WriteMacroFile(resources.dataRoot, Chat::WrittenMacros(resources.world.Runtime().chat.input.macros));
    if (!saved.HasValue())
        resources.log.Warning("no se pudo guardar Macro.txt durante el cambio de servidor: {}", saved.Error());
    return {};
}

Core::Expected<void, std::string> ApplyMapServerJoin(const WorldEntryResources& resources)
{
    KillOtherActors(resources.world, NoKey);
    return ResetForTheSelection(resources);
}

void PrepareMapServerAuthentication(const WorldEntryResources& resources)
{
    static_cast<void>(resources.cloth.random.Next());
    std::uint8_t serialDepth = 0;
    {
        const Models::LogicalAccess serial(serialDepth, resources.cloth.random, Models::SerialCloseRandoms);
    }
}
} // namespace Client::Scenes
