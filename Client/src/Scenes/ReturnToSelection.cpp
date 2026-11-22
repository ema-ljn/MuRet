#include "Scenes/ReturnToSelection.h"

#include "Core/Expected.h"

#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/CameraTour.h"
#include "Scenes/EventPackets.h"
#include "Scenes/GameDrawTail.h"
#include "Guild/GuildState.h"
#include "Guild/GuildWindow.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/MapLoad.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/PersonalShopLists.h"
#include "Scenes/PetStats.h"
#include "Scenes/PetTrainer.h"
#include "Scenes/NpcWindows.h"
#include "Protocol/Outgoing.h"
#include "UI/PanelDialog.h"
#include "Scenes/Quests.h"
#include "Scenes/SceneId.h"
#include "Scenes/SiegeBattleUi.h"
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
    if (world.Runtime().audio != nullptr)
    {
        world.Runtime().audio->ReleaseWorldSounds();
        world.FaunaResources().ResetSoundRegistrationStamps();
    }
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

// 0x4c2480: los callbacks de red observan el estado en sus dos sitios nativos.
Core::Expected<void, std::string> ResetAfterLogout(const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    WorldRuntime& runtime = world.Runtime();
    ResetSiegeClock(runtime);             // 0x4c2480..0x4c249c: conserva hora, minuto y tick.
    runtime.events.soccer.active = false; // 0x4c24e1.
    runtime.ui.field5767954 = 0;
    runtime.floor.pendingPickUp = -1;
    resources.camera.level = 0;
    runtime.selectedTargets.fill(-1);
    runtime.ui.autoAttack = true;
    runtime.map = NoMap;
    // 0x4c2546→0x419900: primero cierra el byte de la misión; el resto del reset todavía no corrió.
    runtime.panels.delegated.quest = 0;
    if (resources.sendWindowClose)
        resources.sendWindowClose();
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
    // 0x657670: reset silencioso de660090, sin callback ni cleanup del EDIT.
    UI::ResetThePanelDialog(runtime.panels.delegated.panelDialog);
    ClearTheGuildNotice(runtime.guild.window);
    const InventoryServices services = InventoryServicesOf(resources);
    // 0x4499b0 sólo manda el segundo31 si la caja está vacía. Con socket cerrado se intenta sin salida.
    const NpcWindowSender send = [&resources](Protocol::Outgoing) -> Core::Expected<void, std::string>
    {
        if (resources.sendWindowClose)
            resources.sendWindowClose();
        return {};
    };
    const auto trainer = CloseTheTrainer(world, &services, send);
    if (!trainer.HasValue())
        return Core::Unexpected(trainer.Error());
    // Un false por caja ocupada conserva al entrenador, pero NO impide la cola.
    ResetThePets(runtime);
    ResetThePersonalShops(runtime.social.personalShop);
    // 0x4c26ec..0x4c26fd: el recorrido de la cámara, 0x403300 y después 0x403d60 (que con +0x14c recién en 10000 no
    // borra a nadie).
    ResetTheCameraTour(runtime.events.cameraTour);
    // MuRet: la cámara copia la posición del héroe publicado, que la selección lee desde acá (Q-C213).
    resources.cameraInput.hero = world.Actors()[runtime.hero].position;
    return {};
}

Core::Expected<void, std::string> RestoreTheCursor(const WorldEntryResources& resources)
{
    const InventoryServices services = InventoryServicesOf(resources);
    return ReturnTheCursorItem({.world = resources.world, .random = resources.cloth.random, .services = services});
}

void StopAudioForSceneReturn(WorldRuntime& runtime)
{
    if (runtime.audio == nullptr)
        return;
    runtime.music.StopWorld(runtime.audio);
    for (std::int32_t sound = 0; sound < 0x27e; ++sound)
        runtime.audio->StopSound(sound, true);
}
} // namespace

Core::Expected<void, std::string> PrepareReturnToSelection(const WorldEntryResources& resources)
{
    // 0x4c3626..0x4c363a: sonidos, cursor vacío, 0x61eef0 y 0x42fdc0.
    StopAudioForSceneReturn(resources.world.Runtime());
    if (auto restored = RestoreTheCursor(resources); !restored.HasValue())
        return restored;
    ReleaseTheGame(resources.world);
    ResetTheInterface(resources.world.Runtime());
    resources.world.Runtime().orderedScene = SceneId::CharacterSelect; // 0x4c363f
    return {};
}

Core::Expected<void, std::string> CompleteReturnToSelection(const WorldEntryResources& resources)
{
    // 0x4c38eb y0x4c38f9: después delF3 00, antes de4c2480.
    resources.selection.BeginSelection();
    resources.world.Runtime().drawingEnabled = false;
    return ResetAfterLogout(resources);
}

Core::Expected<void, std::string> ApplyReturnToSelection(const WorldEntryResources& resources)
{
    auto prepared = PrepareReturnToSelection(resources);
    if (!prepared.HasValue())
        return prepared;
    return CompleteReturnToSelection(resources);
}

Core::Expected<void, std::string> PrepareReturnToServerList(const WorldEntryResources& resources)
{
    // 0x4c275c..0x4c2779: sólo en escena 5, pero ANTES de comprobar el cifrado.
    if (!resources.scene || resources.scene() != SceneId::Game)
        return {};
    ResetTheInterface(resources.world.Runtime());
    StopAudioForSceneReturn(resources.world.Runtime());
    if (auto restored = RestoreTheCursor(resources); !restored.HasValue())
        return restored;
    ReleaseTheGame(resources.world);
    return {};
}

Core::Expected<void, std::string> ApplyReturnToServerList(const WorldEntryResources& resources)
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
    return ResetAfterLogout(resources);
}
Core::Expected<void, std::string> ResetForTheSelection(const WorldEntryResources& resources)
{
    return ResetAfterLogout(resources);
}
} // namespace Client::Scenes
