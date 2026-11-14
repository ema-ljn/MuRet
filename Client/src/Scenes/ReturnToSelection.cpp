#include "Scenes/ReturnToSelection.h"

#include "Core/Expected.h"

#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/CameraTour.h"
#include "Scenes/EventPackets.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/GuildState.h"
#include "Scenes/GuildWindow.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/MapLoad.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/PersonalShopLists.h"
#include "Scenes/PetStats.h"
#include "Scenes/Quests.h"
#include "Scenes/SceneId.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/WhisperSenders.h"
#include "World/GameCamera.h"

#include <cstdint>
#include <string>

namespace Client::Scenes
{
namespace
{
// 0x61ef04: 0x510f70 con −1, que solo deja vivo a uno con la clave 0xffff (Q-C212).
constexpr std::int32_t NoKey = -1;
// 0x4c24b3 y 0x4c2540: el mundo en curso en −1.
constexpr std::int32_t NoMap = -1;

// 0x61eef0: objetos, pools, ítems del piso y personajes; conserva terreno y héroe publicado.
void ReleaseTheGame(WorldState& world)
{
    ReleaseTheMapObjects(world.Map());
    ClearWorldTransientPools(world);
    for (auto& item : world.DrawTail().floorItems)
        item.alive = false;
    KillOtherActors(world, NoKey);
}

// 0x42fdc0. Con 2 precede a 0x61eef0; con 1 va después.
void ResetTheInterface(WorldRuntime& runtime)
{
    // Los globales de la interfaz de Crywolf a su valor inicial, el estado (0x42fe32), la etapa (0x723c05) y
    // 0x723c08 en 0 y el NPC elegido de la llegada (0x6c8e54, 0x42fdf8) en −1 (CL9 hito 11).
    ResetTheCrywolfInterface(runtime);
}

// 0x4c2480, común a los motivos 1 y 2. Los dos 31 sólo los manda la sesión con el GS abierto.
void ResetAfterLogout(const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    WorldRuntime& runtime = world.Runtime();
    runtime.ui.field5767954 = 0;
    runtime.floor.pendingPickUp = -1;
    resources.camera.level = 0;
    runtime.selectedTargets.fill(-1);
    runtime.ui.autoAttack = true;
    runtime.map = NoMap;
    // 0x4c254b: el personaje que se sigue (0x6c8e50) en −1.
    runtime.movement.followTarget = -1;
    {
        // 0x4c2575..0x4c264b: cierre independiente de 3.848 rand() cuando no quedan accesos.
        const Render::Models::LogicalAccess access(runtime.ownRecordDepth, resources.cloth.random,
                                                   Render::Models::OwnRecordCloseRandoms);
        OwnCharacterRecord& own = world.OwnCharacter();
        own.speedAdjustBits = 0;
        own.word3e = 0;
        own.word40 = 0;
        own.word42 = 0;
    }
    runtime.chat.noticeSound = false;
    UI::ClearTheWhisperSenders(runtime.chat.whisperSenders);
    ResetTheQuests(runtime.quest, runtime.panels.delegated.quest);
    // 0x4c2650..0x4c26e7: lo demás son globales y objetos que MuRet no tiene, y 0x657670, que vacía la lista del aviso
    // del gremio (0x651790 en 0x657681, CL9 hito 8), cuyo 0x4499b0 manda el segundo C1 03 31 (la sesión, Q-C214),
    // cuyo 0x420cd0 pone en 0 las mascotas (CL9 hito 4) y cuyo jmp final (0x657690) vacía las tiendas personales
    // (0x5eb960, CL9 hito 7).
    ClearTheGuildNotice(runtime.guild.window);
    ResetThePets(runtime);
    ResetThePersonalShops(runtime.social.personalShop);
    // 0x4c26ec..0x4c26fd: el recorrido de la cámara, 0x403300 y después 0x403d60 (que con +0x14c recién en 10000 no
    // borra a nadie).
    ResetTheCameraTour(runtime.events.cameraTour);
    // MuRet: la cámara copia la posición del héroe publicado, que la selección lee desde acá (Q-C213).
    resources.cameraInput.hero = world.Actors()[runtime.hero].position;
}

Core::Expected<void, std::string> RestoreTheCursor(const WorldEntryResources& resources)
{
    const InventoryServices services = InventoryServicesOf(resources);
    return ReturnTheCursorItem({.world = resources.world, .random = resources.cloth.random, .services = services});
}
} // namespace

Core::Expected<void, std::string> ApplyReturnToSelection(const WorldEntryResources& resources)
{
    // 0x4c3626..0x4c363a: sonidos, cursor vacío, 0x61eef0 y 0x42fdc0.
    if (auto restored = RestoreTheCursor(resources); !restored.HasValue())
        return restored;
    ReleaseTheGame(resources.world);
    ResetTheInterface(resources.world.Runtime());
    resources.world.Runtime().orderedScene = SceneId::CharacterSelect; // 0x4c363f
    // 0x4c38eb: antes de recibir la próxima lista, aunque llegue en este mismo Poll.
    resources.selection.BeginSelection();
    resources.world.Runtime().drawingEnabled = false; // 0x4c38f9
    ResetAfterLogout(resources);
    return {};
}

Core::Expected<void, std::string> PrepareReturnToServerList(const WorldEntryResources& resources)
{
    // 0x4c275c..0x4c2779: sólo en escena 5, pero ANTES de comprobar el cifrado.
    if (!resources.scene || resources.scene() != SceneId::Game)
        return {};
    ResetTheInterface(resources.world.Runtime());
    if (auto restored = RestoreTheCursor(resources); !restored.HasValue())
        return restored;
    ReleaseTheGame(resources.world);
    return {};
}

void ApplyReturnToServerList(const WorldEntryResources& resources)
{
    // 0x4c2f0f -> 0x61cc50: también desde selección. No llama 0x607b40 (ítems del piso).
    ReleaseTheMapObjects(resources.world.Map());
    ClearWorldTransientPools(resources.world);
    KillOtherActors(resources.world, NoKey);
    // 0x4c2ef3..0x4c2f51: el socket ya está cerrado. Leave/Enter liberan los recursos de escena
    // (0x61cc50) y reconstruyen el login, cuyas banderas de primera vez se reinician aquí.
    resources.world.Runtime().orderedScene = SceneId::Login;
    resources.selection.BeginSelection();
    resources.world.Runtime().drawingEnabled = false;
    ResetAfterLogout(resources);
}
void ResetForTheSelection(const WorldEntryResources& resources)
{
    ResetAfterLogout(resources);
}
} // namespace Client::Scenes
