#pragma once

#include "Data/Text/MoveRequirements.h"
#include "GameLogic/MovementSpeed.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ChatBubbles.h"
#include "Scenes/EventGlobals.h"
#include "Scenes/EventMapObject.h"
#include "Scenes/FullSetScan.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GameItemRenderState.h"
#include "Scenes/HudItems.h"
#include "Scenes/ItemHelpLevels.h"
#include "Scenes/SceneId.h"
#include "Scenes/TooltipLines.h"
#include "Scenes/WorldTexts.h"
#include "UI/ChatInput.h"
#include "UI/ChatLog.h"
#include "UI/Dialogs.h"
#include "UI/GameDialogs.h"
#include "UI/GameUiState.h"
#include "UI/MessageArea.h"
#include "UI/OldWindows.h"
#include "UI/PanelRegistry.h"
#include "UI/SlideBanner.h"
#include "UI/WhisperSenders.h"
#include "World/GroundMarks.h"
#include "World/ItemState.h"
#include "World/LeafWeather.h"
#include "World/ObjectModelStepState.h"
#include "World/TerrainCulling.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Los globales en claro que sobreviven a los cambios de escena: el mundo en curso, el estado compartido
// de la velocidad y lo que hace falta para la espera de la entrada al mundo (06, "La entrada al mundo",
// pasos 1 a 4 y 6). Arrancan en 0, como la .bss del original; las cuentas de accesos de la tabla de
// protección, que el original guarda al lado de cada entrada, las pone MuRet en 0 sin haberlas leído
// (CL7.P, P-C22 c).
namespace Client::Scenes
{
// .data[0x6c8c5c] = 1e 00 00 00; los otros cuatro campos del clima están en .bss.
inline constexpr std::int32_t InitialLeafCount = 30;

// Los globales de la interfaz de Crywolf que escriben el BD 00 (0x42fe80), los BD 02 a 09 y el dibujo (0x4365c0), y que
// 0x42fdc0 vuelve a su valor inicial (no todos: los de la .data en 0x6c73cc.., los demás en la .bss).
struct CrywolfInterface
{
    // 0x723a40: un float (FLD, FADD y FSTP en 0x4370d6 y 0x4372dc), lo que baja el adorno 0x6b8 del estado: sube de a
    // 1 hasta 21 con 0x723a3d en 1 y vuelve de a 1 a 0 con 0x723a3d en 0. 0x42fdc0 escribe el dword 0.
    float float723a40 = 0.0f;
    // 0x723a3d: la barra de 0x6b7 (con 0x723a40 en 21); la pone y la saca el BD 05 por el signo de su dword.
    std::uint8_t byte723a3d = 0;
    // 0x723a44: la ventana del contrato de los altares (06 § La interfaz de Crywolf), 1 con Sí y No o 2 con OK. La
    // abre 0x435b00 y la cierran el dibujo (0x436240, con el botón apretado bajo el mouse), el de los resultados
    // (0x436b15) y 0x42fdc0.
    std::uint8_t byte723a44 = 0;
    // 0x723a48 y 0x723b10: los dos renglones de 200 bytes de esa ventana, que llena 0x435b00 (strcpy de Text.bmd o
    // wsprintfA) y dibuja 0x436240. 0x42fdc0 no los toca.
    std::array<std::array<char, 200>, 2> contractLines{};
    std::uint8_t byte723ca0 = 0;
    std::uint8_t byte723bec = 0;
    // 0x723bd8: pkt[8] del BD 05 (0x435a8e): el número del texto 1948 y el ícono 0x6bb (o 0x6bc con 0).
    std::int32_t dword723bd8 = 0;
    // 0x723bdc, 0x723be0, 0x723be4 y 0x723be8, en 0 en 0x42fdc0. El primero es el botón apretado: 5 el de los
    // resultados, que con el mouse encima el dibujo cierra (0x436ad8), y 1 (No), 2 (Sí) o 3 (OK) los del contrato
    // (0x435c5c, 0x436274); el segundo, el valor de 0x435b00 (la clave del altar, que manda el Sí); el tercero y el
    // cuarto son pkt[4] y pkt[5] del BD 04 (0x435a46, con la etapa 4 en el 34), el cuarto los minutos del reloj del
    // estado.
    std::array<std::int32_t, 4> dwords723bdc{};
    // 0x7239f4: GetTickCount del BD 04 (0x435a51), desde donde cuenta el reloj del estado.
    std::int32_t dword7239f4 = 0;
    // 0x6c73d1 (.data 0xff): el aviso, con −1 (con signo) 0x4365c0 no dibuja el cartel (0x436bc7).
    std::int8_t message = -1;
    // 0x6c73d4 (.data 0x64): el dword del BD 05 (0x436590), el largo de la barra 0x6b7.
    std::int32_t dword6c73d4 = 0x64;
    // 0x6c73d8 (.data 1): la cuenta de los frames del cartel.
    std::int32_t dword6c73d8 = 1;
    // 0x6c73dc (.data 0xa): el cartel es 0x6b6 + esto (0x6c0, icon_failure; 0x6c1, icon_success con el estado 1).
    std::int32_t dword6c73dc = 0x0a;
    // 0x6c73e0..0x6c73f4, seis dwords en −1: el primero, el consejo de 0x430180 (de 0 a 3); los otros cinco, los
    // puntos de los resultados (0x6c73e4 + 4 · lugar, del BD 08), con −1 una fila vacía.
    std::array<std::int32_t, 6> dwords6c73e0{-1, -1, -1, -1, -1, -1};
    // 0x6c73f8 (.data 0x186): lo que le falta al rango (0x6d1) para llegar; baja de a 15 por frame.
    std::int32_t dword6c73f8 = 0x186;
    std::int32_t dword6c8e54 = -1;
    // 0x723c08: la luz de los dos 0x4ecc30 del estado 2 (0x430a33) y el largo de la barra 0x6bf; la escribe el BD 02.
    std::int32_t light = 0;
    // 0x6c73cc..0x6c73d0 (.data 02 × 5): los cinco altares del BD 02 (0x430000): con 0x1? en el nibble alto, 0x6cd,
    // 0x6ce o 0x6cf por el bajo (1, 2 u otro); si no, 0x6bd o 0x6be con 1 o 2, y nada con otro.
    std::array<std::uint8_t, 5> altars{2, 2, 2, 2, 2};
    // 0x723bed y 0x723bf0: el rango (0x6d2 + esto) y la experiencia (nueve cifras) del BD 07 (0x437600).
    std::uint8_t byte723bed = 0;
    std::int32_t dword723bf0 = 0;
    // 0x723a30 + lugar y 0x7239f8 + 11 · lugar: la clase y el nombre (10 bytes y el 0) de los resultados (0x437620).
    std::array<std::uint8_t, 5> rankingClasses{};
    std::array<std::array<char, 11>, 5> rankingNames{};
    // 0x723bf9: la ventana de los resultados, que abre el BD 08 y cierra el botón (0x436b21).
    std::uint8_t byte723bf9 = 0;
    // 0x723bf8: en 1 con el BD 04 y con cada dibujo del estado (0x4373fb).
    std::uint8_t byte723bf8 = 0;
    // 0x723bf4, 0x723bfc y 0x723c00 (.bss 0): los minutos que quedan, los segundos que van (de 0 a 59) y el último
    // segundo contado desde 0x7239f4 (0x437412..0x437490).
    std::int32_t dword723bf4 = 0;
    std::int32_t dword723bfc = 0;
    std::int32_t dword723c00 = 0;
    // 0x7d3a0e4 y 0x7d3a0e8 (.bss 0): el reloj de 0x63b0e3, que solo pide 0x430180 (cada 10.000 ms de timeGetTime).
    std::uint8_t tipTimerRestart = 0;
    std::uint32_t tipTimerStart = 0;
    // 0x7d2dc9d: pkt[4] del BD 09 (y de 0x4e1629); lo lee la caja del caos (0x5ff2f2, CL9.3) y lo borra 0x65a94f.
    std::uint8_t byte7d2dc9d = 0;
};

// Los globales de Crywolf (el mapa 34), todos en la .bss.
struct CrywolfGlobals
{
    // 0x723c05: la etapa que leen los dibujos del chat (con 3 o 4 bajan sus renglones). Solo la escriben el BD 00
    // (0x42fe80, CL9 hito 11) y 0x42fdc0 (la vuelta a la selección y la etapa 6).
    std::uint8_t stage = 0;
    // 0x723c04: el estado del evento que lee 0x42fe50. Lo cambia solo el BD 00 (0x42fe80, CL9 hito 11) y lo vuelve
    // a 0 0x42fdc0. Con 0, la carga del 34 toma EncTerrain35.att y TerrainLight (0x61a560) y el dibujo de sus
    // objetos, los casos del estado 0 (Q-C340).
    std::uint8_t state = 0;
    CrywolfInterface ui{};
    // 0x5763358: el clima, rand() % 3 cada 70.001 frames de sus objetos (0x430510). Lo leen las hojas (0x530490
    // y 0x5306a0) y el 0x49 del dibujo (0x430942).
    std::int32_t weather = 0;
    // 0x723c0c: los frames de los objetos desde el último sorteo del clima (0x430523).
    std::int32_t frameCount = 0;
    // 0x723c10: el giro del segundo sprite 0x60f del 0x29, de 0 a 359 (0x4305db).
    float glowRotation = 0.0f;
    // 0x576353c: el 0x49 lo pone en 1 con el clima en 0 (0x430950); lo gasta el HUD (0x5a7520 → 0x435930, CL8),
    // que MuRet no tiene.
    std::uint8_t sandOverlay = 0;
};

// El chat (CL8 hito 3; 06 § El chat), que vive del login al juego: el registro nuevo (el objeto de 0x576350c) y el
// área vieja y los avisos (0x77d9868 y 0x77f3b88 con sus relojes). Lo que escriben sus dibujos va en HudState.
struct ChatRuntime
{
    UI::ChatLogState log{};
    UI::MessageAreaState area{};
    // 0x77f3b78 (.bss 0): el sonido de los avisos del filtro. Lo cambian el F3 30 (0x4dd570) y el menú de Esc
    // (0x62567b).
    bool noticeSound = false;
    // 0x77f3b98 (.bss 0): con 1, el 02 no hace nada (0x4e34f1). Lo escriben /whisper off y on y el menú de Esc.
    bool whisperBlocked = false;
    // 0x57678c4: los 11 bytes del destino del último susurro que salió (0x4b87e9, al mandar). El 0C con 0 los da de
    // nombre al error (0x4e3684); sin ninguno, vacío como la .bss.
    std::string lastWhisperTarget{};
    // 0x77997f8 y 0x77f3b9c: los que susurraron con el nivel propio menor que 10 (0x554d20).
    UI::WhisperSenders whisperSenders{};
    // 0x77e1be0: los globos.
    ChatBubbleTable bubbles{};
    // +0x510 y +0x511 de la barra de la selección (la ventana +0x7f50 de la UI nueva, 0x7d53728): el primer 0D con 1
    // que llega en la selección (0x63eda0) y no se vuelve a escribir, porque la barra se arma una vez (0x63e6e0).
    // Vacío, +0x510 en 0.
    std::optional<std::string> selectionNotice{};
    // 0x6c7a8c (.data 1): los carteles de Slide.bmd prendidos. Lo apaga el bit 0x10 del F3 30 (0x4dd7fa).
    bool slidesOn = true;
    // La entrada del chat (los EDIT de [0x57634f8], sus buffers, el freno y las macros).
    UI::ChatInputState input{};
};

// Los globales del HUD del juego (0x5a7320 y sus hijos, CL8 hito 2), en la .bss salvo los que dicen otra cosa.
struct HudState
{
    // 0x78aba48: cuántos hay en la party. Lo pone en 0 abrir la ventana (0x659aa0) y lo escribe la lista (C1 42,
    // 0x4d67e0, CL9.4).
    std::int32_t partyCount = 0;
    // 0x78a7068 y 0x78a706c: la esquina de la ventana de la party, que pone su dibujo (0x5f0be0) con la ventana
    // abierta, en (0x1c2, 0). La lee su paso (0x5e3c80), que antes del primer dibujo ve los ceros de la .bss (Q-C650).
    std::int32_t partyX = 0;
    std::int32_t partyY = 0;
    // +0xc y +0x10 de [0x6e592c]: la esquina de la ventana de los comandos, que pone su dibujo (0x40a650) con la
    // ventana abierta, en (0x1c2, 0). La lee su paso (0x40a450), que antes del primer dibujo ve los ceros del
    // constructor (*inferido*, Q-C650).
    std::int32_t commandsX = 0;
    std::int32_t commandsY = 0;
    // 0x5767960: la vida del invocado, de 0 a 100. La escribe su paquete (CL9.2).
    std::int32_t summonLife = 0;
    // 0x77f3b74: el freno de las macros. Una que llega al freno lo deja en 0x64 (0x5a1ea1) y 0x637d44 lo baja de a
    // uno por paso; con más de 0 no sale ninguna (0x5a116f) y el HUD dibuja "Macro Time" (0x5a7369).
    std::int32_t macroTime = 0;
    QuickSlots quickSlots{};
    // 0x6c8e5c (.data −1) y 0x6c8e64 (.data 0x80000000): el color del texto y el del fondo que lee 0x465950. Cada
    // texto del HUD deja los suyos y el que sigue sin poner otros sale con ellos, también en el frame siguiente.
    Render::Text::TextColors textColors{.text = 0xffffffffu, .background = 0x80000000u};
    // 0x77f3b66: lo prenden un aviso de durabilidad (0x5aac20) y un globo (0x555b00), y lo apaga la barra (0x5a9290).
    // Lo leen 0x554000, que con él corre la y de los textos (MuRet, solo en los globos: 08, P-C83), y el inventario
    // (hito 4).
    std::uint8_t durabilityWarning = 0;
    // 0x77f31b8 y 0x77f31bc: el tamaño en 640 × 480 del último texto que lo guardó: las flechas o los virotes
    // (0x5ab810), el área vieja (0x5554e0) y los avisos (0x554810); el de un globo (0x555b00) es en píxeles.
    std::int32_t arrowsTextWidth = 0;
    std::int32_t arrowsTextHeight = 0;
    // 0x77f3b5c: con el botón izquierdo abajo, el cursor 0xa (CursorDontMove) en vez del 3 (0x5ac7a2). Quién lo
    // escribe, sin leer: queda en 0.
    std::uint8_t pushCursor = 0;
    // Lo que escriben los dibujos del chat (hito 3): el marco del registro (su esquina la pone el paso), los anchos y
    // los parpadeos del área vieja y de los avisos.
    UI::ChatLogFrame chatFrame{};
    UI::MessageFrameState messageFrame{};
    // La fuente del último SelectObject sobre el DC de los textos (0x57634f4), que miden 0x45bb20, 0x555190 y
    // 0x554610. 0x61bcd0 crea el DC sin ninguna (la del sistema) y el login dibuja con una antes del primer mensaje:
    // MuRet empieza con la normal.
    Render::Text::FontKind dcFont = Render::Text::FontKind::Normal;
    // 0x78a5a14 y 0x78a5a10: la esquina del inventario, que pone su dibujo (0x5f2340) en cada frame: x en 0x104 con
    // el personaje o la party abiertos y en 0x1c2 si no, y siempre y en 0. Los leen los ítems 3D de sus grillas
    // (0x5f7610) y las cifras (0x5f7840) del mismo frame (CL8 hito 4).
    std::int32_t inventoryX = 0;
    std::int32_t inventoryY = 0;
    // 0x78aba14 y 0x78aba18: la esquina de la ventana del personaje, que pone su dibujo (0x5ee2e0) en cada frame, en
    // (0x1c2, 0). Los leen su paso (0x5e4510) y su botón de cerrar (0x5f74b4), CL8 hito 5.
    std::int32_t characterX = 0;
    std::int32_t characterY = 0;
    // 0x78aadcc..0x78aadd4 (.bss, en 0): los ángulos con que 0x5e0720 posa cada ítem de la UI. Los escribe en cada
    // ítem y duran al siguiente y al frame siguiente: el 0x1e1e con un paso que no es 0, 1 ni 2 posa con los del
    // anterior (Q-C478).
    glm::vec3 uiItemAngles{};
    // [0x5763528]: el cartel de Slide.bmd (0x46aea0), que dibuja el 15 (0x46b090) y escribe al dibujar (la opacidad,
    // el corrimiento, el texto que pasa y 0x5762ee4). Lo llenan el timer 0x3eb y el 0D, y lo pasa el paso de la UI.
    UI::SlideBanner slides{};
    // 0x78793c8, 0x787a560 y 0x78a9230: los renglones de los tooltips, sus colores y sus negritas (CL8 hito 5).
    TooltipLines tooltip{};
    // 0x787a15c y 0x6c9334: la tabla por nivel de la ayuda del ítem y su ítem (−1 en la .data), que llena 0x5afc00.
    ItemHelpLevels itemHelp{};
};

// 0x78ab9b8, la grilla del ítem del cursor: un puntero en la .bss (0 al arrancar). Lo escriben levantar de una grilla
// (0x5c52e2, con la que sea) y del equipo y MoveHero (0x5c2d37 y 0x59bf32, con 0x78a9bb0). MuRet solo levanta del
// inventario: las otras grillas (0x78a92a8, 0x78a7070, 0x7d2d380, 0x78ab0b8 y 0x723c80) cortan antes.
enum class CursorGrid : std::uint8_t
{
    None,      // 0
    Inventory, // 0x78a9bb0
};

struct WorldRuntime
{
    // Q-C180/Q-C27: primer aviso de datos que pidió WM_DESTROY. Durable incluso si Poll falla;
    // main muestra el modal antes de cerrar. Unsupported-map y errores sin aviso no lo escriben.
    std::optional<std::string> fatalDataMessage{};
    GameItemRenderState itemRender{};
    bool siegeActive = false; // 07239ec, separado del número de mapa (4294b0).
    // 0x7239f0 (.bss 0): cuándo 0x42b540 eligió la música del mapa 30, en ms de timeGetTime guardados en float. Lo
    // ponen en 0 la carga del 30 (0x42a795) y 0x4294c0 (los paquetes del asedio, CL9.5).
    float siegeMusicTime = 0.0f;
    // 0x6c649a (.data 0xff): el puesto del héroe en su gremio, que solo copia 0x412120 desde 0x4d9260 (el
    // paquete del gremio, CL9). Sin ese paquete queda en 0xff, sin gremio.
    std::uint8_t heroGuildPost = 0xff;
    // 0x5763530: el panel del asedio que 0x412140 arma en el mapa 30 y 0x412560 suelta en los demás, por su
    // clase (+0x18, lo que devuelve su primer método): 0 sin gremio (0x20 bytes, vtable 0x6b88a8, en (489; 259)),
    // 1 y 2 con él. Lo dibujan 0x59ff10 y 0x5a7320, la interfaz (CL8).
    std::optional<std::uint8_t> siegePanelKind{};
    // El de la clase 0: 0x412195 lo arma de nuevo cada vez que no había panel o era de otra clase.
    UI::SiegePanelState siegePanel{};
    // Los bytes de los paneles (0x7d53708..0x7d53718 y los globales que escriben sus funciones), los diálogos
    // (0x7d3a10c, 0x7d3a110) y lo demás de la UI del juego (CL8 hito 1): la .bss, que nadie vacía al cambiar de
    // escena.
    UI::PanelState panels{};
    UI::DialogState dialogs{};
    // 0x7d36a98, 0x7d368e4, 0x7d368b8 y 0x7d3a0f4..0x7d3a0f9: el texto y los botones de las cajas viejas (hito 6).
    UI::DialogBoxState dialogBox{};
    UI::GameUiState ui{};
    // *(0x5763514), el administrador de las ventanas viejas, y la lista de ventanas de [0x5763524] (hito 7): los arma
    // WinMain y duran todo el juego.
    UI::OldWindowManager oldWindows{};
    HudState hud{};
    ChatRuntime chat{};
    GameMovementState movement{};
    // Los cinco selectores del mouse arrancan en -1 en la .data.
    std::array<std::int32_t, 5> selectedTargets{-1, -1, -1, -1, -1};
    // 0x723c18: la cola de los textos 3D (WorldTexts.h). La llena 0x4381f0 desde el cuerpo de los personajes
    // (0x4fd603, que MuRet corta: CL9.5) y la vacía su dibujo (0x4383f0) en cada frame del juego.
    WorldTextQueue worldTexts{};
    // 0x723c68 (+8, la lista; +0xc, la cuenta): los destinos de movereq.bmd que 0x448440 lee en la carga (0x61ccb0) y
    // que la ventana /move recorre en su paso (0x448580) y en su dibujo (0x448fa0). main.cpp los copia de GameData.
    std::vector<Data::Text::MoveRequirement> moves{};
    // 0x7879f80: el ítem del cursor, un ItemState de 0x48 bytes en la .bss. Lo dibuja 0x5f7b10 (CL8 hito 4).
    World::ItemState cursorItem{};
    // 0x7879fbc, el +0x3c (occupied) de cursorItem: mayor que 0 con un ítem en el cursor. MuRet lo guarda aparte, y
    // quien levanta o suelta el ítem escribe los dos.
    std::int32_t cursorItemCount = 0;
    // 0x78ab9b8: la grilla de donde salió el ítem del cursor (CL8 hito 4).
    CursorGrid cursorGrid = CursorGrid::None;
    // 0x77f3cc8: el lugar adonde va el C1 .. 24 que sale (0x5ca4b0, 0x5bf209, 0x59bf46).
    std::int32_t itemMoveTarget = 0;
    // 0x787a518: la copia del ítem bajo el mouse que lee el cartel (0x5c42a7 y 0x5c3aba). Levantar un ítem le pone el
    // tipo en 0xffff (0x5c5358, 0x5c2d74).
    World::ItemState hoveredItemCopy{};
    std::int32_t storm = 0;
    std::int32_t field78abb10 = 0;
    std::uint8_t byte6c8e4d = 0xff;
    std::optional<std::int32_t> lastSoundId{};
    World::ObjectModelStepState objectModelState{};
    // 7d365a0, .bss0.6061c0 lee el count retenido antes de60657a;6063d0 lo produce
    // sólo paraIcarus/Chaos/asedio. Kalima conserva0 después del reset del pase exitoso.
    std::int32_t objectSelectionCount = 0;
    // 0x723c4c, .bss 0: la hora del sistema (timeGetTime) del último ambiente 0x1ef de Land of Trials (0x44016b).
    std::uint32_t trialsAmbientTime = 0;
    // DAT_00723c60, el objeto de evento que arma 0x447d70. Vacío como la .bss; sólo 0x447d70 lo suelta
    // (0x447eb0), y sólo para armar otro: al salir del evento a un mapa común sigue vivo (Q-C320).
    std::optional<EventMapObject> eventObject{};
    // 0x6c634c (.data 0xff): el área de Chaos Castle que leen 0x404e20 y 0x405e00 (Q-C229); 0x404d60 la vuelve
    // a poner en 0xff.
    std::uint8_t chaosCastleArea = 0xff;
    // 0x6c634d (.data 1): la trampa de Chaos Castle armada. 0x4055b0 la gasta con el primer 3 visto; solo el
    // 9B de Chaos Castle (0x404e45, CL9 hito 11) la vuelve a armar.
    std::uint8_t chaosCastleTrapArmed = 1;
    // Lo demás que escriben los paquetes de los eventos (CL9 hito 11).
    EventGlobals events{};
    // Lo de Crywolf que leen y escriben la carga, sus objetos y sus hojas.
    CrywolfGlobals crywolf{};
    // 0x5763538 (.bss 0): el contador de 0x6046a0, que da la vuelta de 1 a 41 al empezar cada dibujo de los
    // objetos, en el login, la selección y el juego (0x6046be..0x6046ce). Solo lo lee el 0x29 de Crywolf.
    std::int32_t objectDrawCounter = 0;
    // 0x743a180, 0x743a184 y 0x743a188 (.bss 0): la celda del héroe y el reloj del mundo que 0x4fbe10 guarda en
    // los mapas 18 a 23. Nadie más los escribe: sobreviven a la carga de otro mapa.
    std::int32_t reportedCellX = 0;
    std::int32_t reportedCellY = 0;
    float reportedCellTime = 0.0f;
    // DAT_006c9dd0, el mundo en curso. Quien llama a 0x61a560 lo escribe justo antes (05, "La carpeta
    // de cada mapa"): el login con 55 (06, "El login, al entrar") y la selección con 54 en 0x633f20
    // (06, "Al entrar"). Ese 54 sigue en la carga del mundo y en la espera de la escena 5, que corre
    // con lo que dejó la selección hasta que la respuesta del F3 03 escribe el del juego (06, "La
    // entrada al mundo", paso 1 y fila 1).
    // El −1 de antes de la primera escritura es el que MuRet ya usaba; el inicial del original no
    // está leído. Con −1, 0x511290 no suma el corrimiento de la mascota (06, CL7.3 B).
    std::int32_t map = -1;
    // 0x7d3a0d0 y 0x7d3a0d4, los de Fenrir: uno solo para todos los personajes (06, "Caminar", la
    // velocidad). 0x4f2250 pone el primero en 0 (0x4f22a4). Los ceros del DTO no afirman el inicial del
    // original.
    GameLogic::MovementSpeedSharedState movementSpeed;

    // DAT_07d3a101, el mundo cargado: en 0, el paso del juego sale en 0x636db5 y su dibujo devuelve
    // 0 en 0x6371a6, sin leer el contador. Lo ponen en 1 la carga del 54 de la selección (0x633f30,
    // dentro de 0x633f20) y el paso del juego con el estado 0x3d (0x636dac), y en 0 solo el F1 02
    // (0x4c2f39 con 2 y 0x4c38f9 con 1, que MuRet sigue: ApplyReturnToSelection). El login también lo
    // pone en 1 al cargar el 55 (0x6349ab); MuRet no lo escribe ahí porque el juego, su único lector
    // portado, llega siempre después de 0x633f20.
    bool drawingEnabled = false;
    // DAT_006d5dc8 escrito por un paquete: el F1 02 con 1 pone la 4 (0x4c363f) mientras se lee la red, y
    // el próximo paso del original ya es el de la selección. MuRet la deja acá y el SceneManager la toma
    // después del Poll (main.cpp) y entra en su próximo paso. Vacía, nadie la pidió.
    std::optional<SceneId> orderedScene{};
    // DAT_07d3a13e: si la carga del mundo (0x6356f0) ya armó el contador y subió sus texturas. La
    // baja la selección justo antes de pasar a la escena 3 (0x633efe, 0x634469, 0x634631) y la sube
    // 0x6356f0 (0x635980). Nadie más la escribe.
    std::uint8_t loadingOnceFlag = 0;
    // DAT_077f3b04, el contador de la espera y de los gates, que vive en la tabla de protección
    // (06, "La tabla de protección"): 9.999.999 en la escena 3 y en un pedido de gate, 30 en la
    // respuesta del F3 03, y baja de a 1 en MoveHero (0x59af01). Con más de 30 el dibujo del juego no
    // presenta (0x63722e).
    std::int32_t warpCounter = 0;
    // Cuántos accesos tienen abierto el contador: el byte que sigue a su copia cifrada.
    std::uint8_t warpDepth = 0;
    // Cuántos accesos tienen abierto el registro propio ([0x7681eb4], 0xf08 bytes).
    std::uint8_t ownRecordDepth = 0;
    // [0x72397c], el nivel del caballo oscuro: lo escribe 0x421db0 con el A9 de las mascotas (el tipo 1 en el
    // lugar 8, 0x421e9e), sin portar (CL9), y lo leen 0x4225d0 (el byte de abajo) y 0x422930. Queda en el 0 de
    // la .bss.
    std::uint16_t darkHorseLevel = 0;
    // [0x723968], el nivel del cuervo: la mascota 0 de 0x421db0 (el A9 con el tipo 0, sin portar, CL9). Lo lee el
    // requisito de liderazgo del 0x1a5 (0x5bf027). Queda en el 0 de la .bss.
    std::uint16_t ravenLevel = 0;
    // DAT_057678b0: el lugar del pool que la respuesta del F3 03 elige para el héroe, rand() % 400
    // (0x4c44d4, la única escritura; 06, "La entrada al mundo", fila 3). Cada respuesta lo vuelve a
    // sortear.
    std::int32_t heroSlot = 0;
    // DAT_0743a170, el héroe publicado, como lugar del pool: WinMain lo deja en el 0 (0x4bafdf) y la
    // respuesta del F3 03 lo pasa al de heroSlot recién después de vestirlo con 0x512a30 (0x4c4725). No
    // lo escribe nadie más: en una segunda respuesta, hasta ahí es el héroe anterior, y después de la vuelta
    // a la selección sigue en el lugar del juego (Q-C213).
    std::size_t hero = 0;
    // [0x743a178] y [0x743a17c], lo que dejó el último 0x505820 (06, "El conjunto completo de 0x505820"): en
    // la .bss, 0 al arrancar. Lo escriben 0x512a30 con el héroe publicado y el dibujo de los personajes
    // (0x50ba30 en 0x50c91e), el del login y el de la selección (ScanTheDrawnCharacter). nullopt después de un
    // frame del juego que llega a los personajes (0x6374dd → 0x510d00), que MuRet no dibuja: no se sabe qué
    // dejó, y 0x550990 corta con error si lo lee.
    std::optional<Scan505820> fullSetScan = Scan505820{};
    // [0x78a6758], en la .bss: 0 al arrancar. 0x5bc740, después de poner un ítem en una de las grillas del
    // inventario, lo compara con signo con 12 (0x5bcd0b y 0x5bd08b) y, con menos, abre el registro propio
    // para las velocidades (0x54fe10). Lo escriben quince lugares de la interfaz del inventario y del juego
    // (0x44add8, 0x561b6d, 0x561bf7, 0x59bf3c, 0x5c2d5c, 0x5c5131, 0x5c5154, 0x5c51c3, 0x5c51f8, 0x5c5277,
    // 0x5c5301, 0x5c5330, 0x5c6c15, 0x5c8edd y 0x5c8f53; estático). MuRet tiene los del inventario: levantar de la
    // grilla (0x5c5301) y del equipo (0x5c2d5c) y MoveHero (0x59bf3c), el lugar de donde salió el ítem del cursor.
    std::int32_t slot78a6758 = 0;
    // El trapecio del último 0x5fb570, en celdas: las Y en 0x78ecbf0 y las X en 0x78ecc00, que solo escriben
    // 0x5fba55 y 0x5fba68 (por su dirección, en .text). A 0x5fb570 lo llama solo 0x5fbb00 (0x5fbeef), y a
    // 0x5fbb00 los dibujos del login (0x6351b7), de la selección (0x63475c) y del juego (0x637366): es uno
    // para las tres escenas. Lo lee solo 0x5fba90, con la escena de ese momento (0x6d5dc8): el primer paso del
    // juego después de la espera (0x508f00) lee el del último dibujo de la selección, y el primer paso del
    // login, el que haya. En la .bss (más allá de los 0x20000 bytes de la .data del archivo, desde 0x6c6000):
    // ceros, un trapecio sin área en (0, 0) con el que 0x5fba90 da 0 en los cuatro lados (*estático*, 06 § El
    // paso de los personajes y Q-C217).
    World::TerrainPolygon terrainPolygon{};
    // 0x7b4ccb8, el nivel efectivo del trapecio de un 0x5fb570 al siguiente, en las tres escenas: solo lo
    // escribe 0x5fb570 (0x5fb731). En la .bss: 0.
    World::TerrainPolygonState terrainPolygonState{};
    // Las marcas del suelo, 0x760cab0..0x760f990, en la .bss: todas apagadas al arrancar. Solo las tocan
    // 0x548fe0, 0x549090 y 0x549130, y ninguna carga las vacía: 0x6070a0 apaga las tablas de al lado y no esta
    // (Q-C228). Las llenan 0x505de0 con el modelo 0x182 (0x507486) y 0x52e810 (0x52e96c), desde el paso del
    // cuerpo (0x4fd5f0 → 0x4fcd00), y MuRet no corre ninguno de los dos: corta antes, en 0x505e0b y 0x4fcb87
    // (08 P-C49).
    World::GroundMarkTable groundMarks{};
    // 0x77f36d0, la capa 1 de la celda del héroe, en la .bss: 0 al arrancar. La escribe MoveHero (0x59e2ba),
    // después del contador. La leen el paso de los objetos
    // (los techos de 0x604ab0, 0x604bc4 y 0x604c06) y el de las hojas (0x637082..0x6370e5), entre otros.
    std::int32_t heroCellLayer = 0;
    // 0x75fdc38: meta del 0F. 0x75fdc3c es la cuenta actual, que ese paquete no cambia.
    // Estado durable de 0x530490 para el futuro binder de hojas; recibir 0F no crea lluvia.
    World::LeafWeather leafWeather{.firstWave = InitialLeafCount};
};

// 0x63722e y 0x59afd6: con el contador en más de esto, el juego no dibuja ni mueve al héroe.
inline constexpr std::int32_t WaitDrawThreshold = 30;
// 0x6357eb: lo que deja la carga del mundo en el contador, 0x98967f.
inline constexpr std::int32_t WorldLoadingWait = 9'999'999;
} // namespace Client::Scenes
