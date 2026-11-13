#include "Scenes/ReturnToSelection.h"

#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/MapLoad.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/PetStats.h"
#include "Scenes/Quests.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/WhisperSenders.h"
#include "World/GameCamera.h"

#include <cstdint>

namespace Client::Scenes
{
namespace
{
// 0x61ef04: 0x510f70 con −1, que solo deja vivo a uno con la clave 0xffff (Q-C212).
constexpr std::int32_t NoKey = -1;
// 0x4c24b3 y 0x4c2540: el mundo en curso en −1.
constexpr std::int32_t NoMap = -1;
} // namespace

void ApplyReturnToSelection(const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    WorldRuntime& runtime = world.Runtime();
    // 0x4c3626 y 0x4c362b: 0x622b00 y 0x41ee50 paran sonidos (MuRet no tiene audio hasta la CL10).
    // 0x4c3630: 0x5be150 sale en 0x5be160 sin un ítem en el cursor, y [0x7879fbc] solo lo escriben con un
    // valor 0x4d1c89 y 0x4d29cb, handlers que MuRet no tiene: queda en 0 (06, "Los stats del registro
    // propio"). Así no gasta rand() ni toca la caja de 0x723c80.
    // 0x4c3635: 0x61eef0. Primero 0x6070a0; después 0x612230 y 0x612d50 sueltan los modelos 0x152..0x207 y
    // los sonidos 0xa0..0x1c1, y 0x607b40 apaga los ítems del piso.
    ReleaseTheMapObjects(world.Map());
    ClearWorldTransientPools(world);
    for (auto& item : world.DrawTail().floorItems)
        item.alive = false;
    KillOtherActors(world, NoKey);
    // 0x4c363a: 0x42fdc0 pone en su valor inicial globales de la interfaz que MuRet no tiene, y en 0 el estado de
    // Crywolf (0x42fe32) y su etapa (0x723c05). 0x723c08, que también pone en 0, solo lo escriben paquetes (CL9.5).
    runtime.crywolf.state = 0;
    runtime.crywolf.stage = 0;
    // 0x4c363f: la escena 4.
    runtime.orderedScene = SceneId::CharacterSelect;
    // 0x4c3649..0x4c38e4: el estado 0x32 y el F3 00, de la sesión. 0x4c38eb y 0x4c38f2: las banderas de la
    // primera vez de la selección (0x7d3a13f) y del juego (0x7d3a140), que en MuRet son de cada escena
    // nueva (SelectionState::BeginSelection y GameScene::Enter).
    runtime.drawingEnabled = false; // 0x4c38f9
    // 0x4c3900: el estado 0 (la sesión espera la lista). 0x4c390a: 0x4c2480.
    resources.camera.level = 0; // 0x4c250e, el word 0x7d3a0ec
    // 0x4c2515..0x4c252d: los cinco del picking (0x6c8e28..0x6c8e38) en −1, también el personaje elegido.
    runtime.selectedTargets.fill(-1);
    runtime.ui.autoAttack = true; // 0x4c2533, 0x6c8e3c
    runtime.map = NoMap;          // 0x4c2540
    // 0x4c2546: 0x419900 manda el C1 03 31 (la sesión). 0x4c2575..0x4c25ea: abre el registro propio por la
    // tabla de protección (o lo da de alta con la cuenta en 1, que es lo mismo para MuRet); 0x4c2600..
    // 0x4c260b escribe; 0x4c260f..0x4c264b lo cierra, y el cierre que lo deja en 0 lo vuelve a cifrar
    // (0x40b4d0).
    {
        const Render::Models::LogicalAccess access(runtime.ownRecordDepth, resources.cloth.random,
                                                   Render::Models::OwnRecordCloseRandoms);
        OwnCharacterRecord& own = world.OwnCharacter();
        own.speedAdjustBits = 0;
        own.word3e = 0;
        own.word40 = 0;
        own.word42 = 0;
    }
    // 0x4c266b: el sonido de los avisos (0x77f3b78) en 0, y 0x4c2671: 0x555030 vacía la lista de los que susurraron
    // (el próximo lugar, 0x77f3b9c, queda).
    runtime.chat.noticeSound = false;
    UI::ClearTheWhisperSenders(runtime.chat.whisperSenders);
    // 0x4c268a..0x4c26d2: el objeto de las misiones, con +4 en 0xff para que la próxima entrada vuelva a pedir el A0
    // (Q-C215).
    ResetTheQuests(runtime.quest, runtime.panels.delegated.quest);
    // 0x4c2650..0x4c26fd: lo demás son globales y objetos que MuRet no tiene, y 0x657670, cuyo 0x4499b0 manda el
    // segundo C1 03 31 (la sesión, Q-C214) y cuyo 0x420cd0 pone en 0 las mascotas (CL9 hito 4).
    ResetThePets(runtime);
    // MuRet: la cámara copia la posición del héroe publicado, que la selección lee desde acá (Q-C213).
    resources.cameraInput.hero = world.Actors()[runtime.hero].position;
}
} // namespace Client::Scenes
