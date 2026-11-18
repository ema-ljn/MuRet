#pragma once

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "GameLogic/CharacterAppearance.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Pass3D.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterCloth.h"
#include "Scenes/GameCharacters.h"
#include "Scenes/HelperTable.h"
#include "UI/OldWindowBase.h"
#include "UI/OldWindows.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace Client::Render
{
class ModelBatch;
}

namespace Client::Render::Models
{
struct SkinnedMesh;
}

// El personaje del retrato de las cartas (vtable 0x6ba008, 0x495e50): el objeto que la ventana de leer (+0xb0) y la de
// escribir (+0xb4) llevan, con su personaje propio (+0x7c), su ayudante propio (+0x4b4) y lo que lo arma, lo viste, lo
// copia del héroe y le cambia la acción. Lo de la ventana (el lugar, el mouse, el ángulo, el zoom y la acción de
// +0x6a4) es de UI (UI::OldPortraitPart). Contrato: Client/docs/research/06-render-escenas-y-ui.md, "El retrato de las
// cartas". Todo *estático*, del ASM.
namespace Client::Scenes
{
struct DrawnObject;
struct GameHudContext;
class IModelLibrary;
struct WorldEntryResources;

struct LetterPortrait
{
    // +0x7c: un registro de 0x438 bytes como los del pool, fuera de él. Nunca es el héroe publicado.
    ActorState actor{};
    // +0x4b4: un lugar de 0x1d4 bytes como los de la tabla de los ayudantes (0x7d335d0), fuera de ella. El jinete
    // (+0x108) es el personaje de arriba: en MuRet, `rider` en ActorPoolSize.
    HelperState helper{};
    // +0x688: la escala del ayudante, por la del personaje (0x497810 y la cola de 0x496530) y el zoom (0x495760); el
    // dibujo (0x445260 con su segundo argumento en 1) la pisa con 0x3f3ae148 (Q-C1010).
    float helperScale = 0.0f;
    // +0x698: la acción quieta del dibujo (0x497c10); 0x497910 lo pone en 1.
    std::int32_t idle = 0;
    // +0x6a0: la última acción que mostró 0x495a20.
    std::int32_t shownAction = 0;
    // +0x6a8: el frame entero que guarda el dibujo (0x497c10); 0x497910 lo pone en 0.
    std::int32_t frameMark = 0;
    // Las telas del personaje (objeto + 0x18c/+0x190), que suelta 0x4fb1a0 y arma el dibujo (0x50ba30).
    CharacterCloth cloth{};
};

// 0x4961d0 (la vtable + 0x30): el ayudante en cero; las entradas de la celda del personaje (+0x41c y +0x420) abiertas
// mientras borra el personaje y cerradas, la X y después la Y (4 rand() cada una si quedan en 0); la celda lógica del
// héroe publicado, la Y y después la X (otros 4 y 4); 0x511290(personaje, 0x20a, X, Y, 0), con sus 28 rand(); el
// lugar en (−300, −300, −300); y +0x68c en 1 (0x496415: en MuRet, OldPortraitPart::built, que pone el gancho). Desde
// cero, 44 rand().
[[nodiscard]] Core::Expected<void, std::string> BuildThePortrait(LetterPortrait& portrait,
                                                                 const WorldEntryResources& resources);

// 0x4977d0 (la vtable + 0x34), armado: la clase (+0x1d4) y 0x513100.
[[nodiscard]] Core::Expected<void, std::string>
SetThePortraitClass(LetterPortrait& portrait, std::uint8_t characterClass, const WorldEntryResources& resources);

// 0x497810 (la vtable + 0x38), armado: 0x5139f0(0, equipo, personaje, ayudante) (ApplyCharacterSetTo), +0x688 con la
// escala del ayudante por 0,7f (0x6b8a5c) entre la del héroe publicado ([0x743a170] + 0xc), y la zona segura (+0x3e2)
// en 1 con alas y la acción `action` (+0x6a4) pasada de 0x82, en 0 si no.
[[nodiscard]] Core::Expected<void, std::string> DressThePortrait(LetterPortrait& portrait,
                                                                 GameLogic::Equipment equipment, std::int32_t action,
                                                                 const WorldEntryResources& resources);

// 0x496530 (la vtable + 0x3c), armado: la clase del héroe publicado (con 0x513100 si cambia) y, por grupos (la cabeza
// con el cuerpo, las manos, las alas y la mascota), lo que difiere: del héroe tal cual con +0x3e3 en 0, o del registro
// propio con +0x3e3 distinto de 0 (con su acceso y los 3.848 rand() de su cierre al comparar y en cada grupo que
// copia). Con la mascota distinta, 0x495a20(0x7a, 0) y `action` (+0x6a4) en 0x7a; si no, 0x495a20 con la que mostró.
// Con la mascota o las manos distintas, el ayudante de nuevo. Devuelve si algo difería.
[[nodiscard]] Core::Expected<bool, std::string> CopyTheHeroToThePortrait(LetterPortrait& portrait, std::int32_t& action,
                                                                         const WorldEntryResources& resources);

// 0x495a20(acción, corrimiento): la acción de la lista (la del ayudante montado si lo hay, la de los gestos si no) que
// queda a `delta` de `action`, puesta en el personaje (0x4f2250, 0x4f4a10 o 0x4f37c0 con el mundo en 0, o 0x4ea030 con
// la tabla de los gestos), y 0x4fd5f0 y 0x505de0 con el mundo en curso. Devuelve la acción y la deja en +0x6a0.
[[nodiscard]] Core::Expected<std::int32_t, std::string> ShowThePortraitAction(LetterPortrait& portrait,
                                                                              std::int32_t action, std::int32_t delta,
                                                                              const WorldEntryResources& resources);

// La parte del personaje de 0x497890 (la vtable + 0x40), armado: los ángulos (+0x1c..+0x24) en (−20, 5, 60).
void TurnThePortrait(LetterPortrait& portrait);

// La vista del retrato de 0x495760 (0x4957a4..0x4958c6) en una pantalla de `screen` píxeles: el viewport de 0x620d10
// con el lugar y el ancho de `base` y 141 (0x6ba004) de alto, cada uno por W / 640 o H / 480 en float (0x5762ea8 y
// 0x5762eac) y truncado; gluPerspective(1, el ancho sobre el alto sin truncar, 2000, 20000) (0x6203c0, sin el × 1,4
// de 0x620db0); y glRotatef(−90) en X, glRotatef(−90) en Z, glTranslatef(−10000, 0, −75) y glTranslatef(−X, −Y,
// −Z), con 50 menos en Z con el caballo oscuro (0x1c16) en +0x318.
[[nodiscard]] Render::Pass3D PortraitPass(App::PixelSize screen, const UI::OldWindowBase& base,
                                          const ActorState& actor);

// 0x4957a4..0x4959bf, lo de 0x495760 después del paso: el pase de PortraitPass, los ángulos en (0, 0, +0x6b0), la
// escala por el zoom (+0x6bc · 0,7 y la del ayudante +0x688 · +0x6bc, que pisa 0x445260: Q-C1010), las luces en 1,
// +0x3e4 en 1 y la zona segura (+0x3e2) con alas y +0x6a4 pasada de 0x82. Sin rand().
[[nodiscard]] Render::Pass3D PlaceThePortrait(LetterPortrait& portrait, const UI::OldPortraitPart& part,
                                              App::PixelSize screen);

// 0x497c10 (la vtable + 0x14) sin lo de 2D, con `part` sin estar en blanco (+0x6c0): el frame (+0x114) por debajo de
// +0x6a8 elige la acción que sigue (quieto, 1 de cada 4 con rand() % 4 la de +0x6a4; si no, la 0x7a si +0x6a4 no está
// entre 0x7a y 0x82) y si no, +0x6a8 sigue al frame; con +0x360 por debajo de 4, 0x414340; y 0x495760 hasta su
// dibujo: 0x4fd5f0, 0x505de0 y 0x444010(ayudante, 1) (StepTheHelperOutsideTheTable) con el mundo en 0, y
// PlaceThePortrait. Devuelve su pase; el dibujo del ayudante (0x445260) y del personaje (0x50ba30) en él es
// DrawThePortraitModel.
[[nodiscard]] Core::Expected<Render::Pass3D, std::string> PrepareThePortraitDraw(LetterPortrait& portrait,
                                                                                 UI::OldPortraitPart& part,
                                                                                 App::PixelSize screen,
                                                                                 const WorldEntryResources& resources);

// Lo que el dibujo del retrato usa del dibujo del juego (el 2D de 0x637673, después del HUD).
struct LetterPortraitDraw
{
    // El lote 2D, los textos, Text.bmd y la caja de los tooltips (0x5af090); su rand() no lo usa el retrato.
    GameHudContext& hud;
    // El lote de los modelos y los fines de los dos lotes (SceneCanvas): el pase del retrato cierra el 2D de antes y
    // queda debajo del de después.
    Render::ModelBatch& batch;
    std::function<void()> flushSprites{};
    std::function<void()> flushModels{};
    // Lo que lee 0x50ba30 (DrawCharacterOutsideThePool), con el rand() del frame en `clothWorld`.
    GameCharacterPass characters{};
    // Los modelos de los ayudantes (los del mundo, como DrawHelpers) y lo que lee 0x445260: la pose al buffer global
    // (0x68b181c), sus mallas posadas, timeGetTime() y el buffer global de la pose.
    const IModelLibrary* helperModels = nullptr;
    std::function<Core::Expected<void, std::string>(const DrawnObject&)> onHelperPose{};
    std::function<std::span<const Render::Models::SkinnedMesh>()> posedMeshes{};
    std::function<std::uint32_t()> systemTime{};
    std::function<std::span<const Render::Models::BoneTransform>()> globalPose{};
    // Un frame del original: el paso y el dibujo sobre el retrato. Un dibujo intermedio de MuRet dibuja una copia,
    // sin el paso (lo hace el frame siguiente del original), y con el rand() de `characters`, que es su copia.
    bool originalFrame = true;
};

// 0x4958e2..0x495a0c sobre el pase de PrepareThePortraitDraw: cierra el 2D, empieza el pase con el material de
// 0x620db0 y la profundidad borrada (glDisable(GL_ALPHA_TEST), glEnable(GL_TEXTURE_2D), 0x620850, 0x6208d0, 0x620890,
// GL_LEQUAL, glAlphaFunc(GL_GREATER, 0,25), sin niebla y glClear(GL_DEPTH_BUFFER_BIT)), 0x445260(ayudante, 1)
// (DrawTheHelperOutsideTheTable) y 0x50ba30(personaje, personaje, 0) (DrawCharacterOutsideThePool), y cierra el pase
// aunque corte. Las matrices y el viewport de la pantalla (0x4959dd..0x495a0c) los vuelve a poner el lote 2D.
[[nodiscard]] Core::Expected<void, std::string> DrawThePortraitModel(LetterPortrait& portrait,
                                                                     const Render::Pass3D& pass,
                                                                     const LetterPortraitDraw& draw,
                                                                     const WorldEntryResources& resources);

// 0x497c10 entera (la vtable + 0x14, desde 0x49a1d0 y 0x498800): en blanco (+0x6c0 en 1), el cuadro negro de
// 119 × 141 y Local\Webzenlogo.jpg (0x653) de 80 × 62 en (x + 0x14; y + 0x26) con la v en 0,76171875; si no,
// PrepareThePortraitDraw y DrawThePortraitModel, y con +0x4c & 1 el ícono de la ayuda (0x651): de 16 × 16 en
// (x + 1; y + alto − 0x11), o con la ayuda a la vista (+0x690) gris (0,6) de 15 × 15 en (x + 2; y + alto − 0x10) con la
// u y la v en 0,9375, y los textos 997, 998 y 999 de Text.bmd en la caja de 0x5af090 (modo 1, con marco), centrada en
// x + ancho / 2 y subida (cy("Z") / ry + 2) · 3 desde abajo. Sin el personaje de `part`, corta.
[[nodiscard]] Core::Expected<void, std::string>
DrawTheLetterPortrait(UI::OldPortraitPart& part, const LetterPortraitDraw& draw, const WorldEntryResources& resources);

// El gancho de UI::OldWindowStepView::portrait con el juego de `resources`, que vive más que él: el primer pedido arma
// el LetterPortrait de UI::OldPortraitPart::actor. Build (0x4961d0) pone +0x68c en 1; SetClass, SetEquipment,
// CopyTheHero y SetAngle no hacen nada sin armar (0x4977d9, 0x49781e, 0x496548, 0x49789b); SetAction (0x4978f0) y
// TurnAction (0x497910: la acción que da 0x495a20 en +0x6a4, +0x6a8 en 0 y +0x698 en 1) no lo miran; Step copia +0x360
// del héroe (0x4979c0).
[[nodiscard]] UI::OldPortraitHook LetterPortraitHook(const WorldEntryResources& resources);
} // namespace Client::Scenes
