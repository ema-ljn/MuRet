#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/PixelSize.h"
#include "Tables/ItemTable.h"
#include "Render/Pass3D.h"
#include "Scenes/GameProjection.h"
#include "Ui/Widget.h"

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_int2.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

// El ítem 3D de la UI (06 § El inventario, "El ítem de la UI"; CL8 hito 4): 0x5e1480 elige un punto de la caja del
// ítem, lo lleva delante de la cámara de la UI (0x620480) y elige el modelo; 0x5e0720 lo acerca a la cámara, le pone
// los ángulos, la escala y la altura de la raíz de su familia, lo posa en el buffer global (0x4eb860) y lo dibuja
// como un ítem del piso (0x607b60 y 0x60cf70). Todo en el pase 3D de 0x5ac120: la proyección de 1° de la pantalla
// entera con la cámara en el origen.
namespace Client::Kernel
{
struct PoseBuffer;
} // namespace Client::Kernel

namespace Client::Render
{
class ModelBatch;
}

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
class IModelLibrary;
} // namespace Client::Models

namespace Client::Actors::Drawing
{
struct ItemDrawContext;
} // namespace Client::Actors::Drawing

namespace Client::Scenes
{
struct GameDrawTailState;
} // namespace Client::Scenes

namespace Client::Items
{

// La caja de un ítem de la UI en 640 × 480: los cuatro floats que reciben 0x5e1480 y 0x5e2d00.
struct UiItemBox
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

// Lo que 0x5e1480 le pasa a 0x5e0720: el modelo y el nivel, el +4 del ítem, o −1 con los modelos del 0x1a13 (0xc1c,
// 0x225 y 0xa24) y con el 0x2246 y el 0x2247.
struct UiItemCall
{
    std::int32_t model = 0;
    std::int32_t level = 0;
};

// 0x5e1d6c..0x5e2267: el modelo y el nivel por el tipo y el +4 (`level`, en 32 bits: el >> 3 con signo y sin
// máscara, el & ~7 sin signo). Sin valor, 0x5e1480 vuelve sin llamar a 0x5e0720 (el 0x1a13 con el paso fuera de 0 a
// 2, el 0x1c17 y el 0x1c18 fuera de 0 a 1 y el 0x1a14 fuera de 0 a 3).
[[nodiscard]] std::optional<UiItemCall> UiItemCallOf(std::int32_t type, std::uint32_t level);

// 0x5e1486..0x5e14e5: el ítem está bajo el mouse (los enteros de 0x7d3682c y 0x7d36828, en 640 × 480) si nadie
// tiene un ítem en el cursor (0x7879fbc en 0) o con `always`, y x ≤ mx < x + w e y ≤ my < y + h en la x87.
[[nodiscard]] bool UiItemHovered(const UiItemBox& box, Ui::Point mouse, std::int32_t cursorItemCount, bool always);

// 0x5e14e9..0x5e1d69: el punto de la caja que 0x5e1480 le pasa a 0x620480: x + w · kx e y + h · ky en la x87 con los
// factores de la tabla del tipo (y del nivel en algunos), truncados con _ftol. Con un paso que la tabla no tiene
// (el 0x1a13, el 0x1a14, el 0x1a0b y el 0x1c15), la esquina.
[[nodiscard]] Core::Expected<glm::ivec2, std::string> UiItemAnchor(std::int32_t type, std::uint32_t level,
                                                                   const UiItemBox& box);

// Lo que 0x5e0720 arma antes de la pose.
struct UiItemPlacement
{
    // El que dibuja (+2 del objeto local, ESI): el del llamado, o el 0x223a y el 0x223b del 0x1e1e con el paso 0 y 2.
    std::uint16_t model = 0;
    // 0x78aadb2: el que posa, el 0x20a (Player) con las armaduras ([0x1012, 0x1a12)).
    std::uint16_t poseModel = 0;
    // El punto acercado a la cámara (0x5fc790) con los corrimientos de algunos modelos.
    glm::vec3 position{};
    // 0x78aadcc..0x78aadd4 después de este ítem.
    glm::vec3 angles{};
    // +0xc del objeto local.
    float scale = 0.0f;
    // +0x84 del modelo que posa: la z de más de la raíz con lock_positions.
    float rootHeight = 0.0f;
    // El nivel que recibe 0x60cf70: el del llamado, o 0 con el 0x225, el 0xc1c y el 0xa24 con uno negativo.
    std::int32_t level = 0;
};

// 0x5e0720 hasta la pose: `anchor` es la salida de 0x620480 y su cámara (0x7d36834); `hovered` gira el ítem con el
// reloj del mundo (0x5767b88, en ms). `angles` son los globales 0x78aadcc..0x78aadd4: el 0x1e1e con un paso que no es
// 0, 1 ni 2 no los escribe y se queda con los del ítem anterior. Con los modelos de 0x212 a 0xe11 lee el byte +0x1e
// (las dos manos) de su registro de Item.bmd; sin él, corta.
[[nodiscard]] Core::Expected<UiItemPlacement, std::string>
PlaceUiItem(const UiItemCall& call, const Scenes::UnprojectedPoint& anchor, bool hovered, bool always, float worldClock,
            const Tables::ItemTable* items, glm::vec3& angles);

// El pase 3D de 0x5ac120: la proyección de 1°, W / H, 20 y 2.000 en la pantalla entera (0x620d10 y 0x6203c0) y la
// vista en la identidad (0x620370 sobre MODELVIEW en la identidad), con su caché de 0x6203c0. El de 0x633110 es igual
// pero con el near y el far de [0x6d5d40] (20) y [0x6d5d44] (el far del mundo de este frame, que escribe 0x635ae0):
// `farPlane`. El caché de 0x6203c0 no depende del far.
struct UiItemPass
{
    Render::Pass3D pass{};
    Scenes::GameProjection projection{};
};

[[nodiscard]] Core::Expected<UiItemPass, std::string> MakeUiItemPass(Core::PixelSize screen, float farPlane = 2000.0f);

// Lo que el dueño del frame le presta al tramo 3D de la UI para dibujar sus ítems: el lote de los modelos y los
// fines de los dos lotes (SceneCanvas), los modelos, el buffer global de la pose (0x68b181c), los contextos de
// 0x60b3d0 y el pase. El primer ítem que dibuja cierra el lote 2D y empieza el pase de la UI, y el final de 0x5ac120
// cierra el de los modelos: los ítems quedan encima del 2D de antes y debajo del de después. Sin ítems, nada.
struct UiItemResources
{
    Render::ModelBatch& batch;
    const Models::IModelLibrary& models;
    Kernel::PoseBuffer& poseCarry;
    const Actors::Drawing::ItemDrawContext& itemContext;
    Core::Duration frameClock{};
    std::function<void()> flushSprites{};
    std::function<void()> flushModels{};
    UiItemPass pass{};
    // Si el lote de los modelos ya va con el pase de la UI.
    bool open = false;
};

// 0x5e13c5..0x5e146d: posa con `placement` en el buffer global, arma el objeto local (0x607b60, con sus rand()) y lo
// dibuja con 0x60cf70(objeto, modelo, 0, (1, 1, 1), 1, nivel, excelente, set, 1, 1, 1, 0, 2): la pose global, sin
// dueño y sin luz por vértice. Abre el pase de la UI si no lo está. Sin el modelo o sin el esqueleto del que posa,
// corta.
[[nodiscard]] Core::Expected<void, std::string> DrawUiItemModel(UiItemResources& resources, Crt::CrtRandom& random,
                                                                const UiItemPlacement& placement,
                                                                std::uint8_t excellent, std::uint8_t set);

// 0x5ac2a2..0x5ac2bb: el fin de 0x5ac120. Si un ítem abrió el pase, el lote de los modelos se dibuja.
void CloseUiItemPass(UiItemResources& resources);

// [ebp − 0x45c] de 0x5e0720: el objeto local que 0x5e0720 arma en la pila para cada ítem de la UI y pasa a 0x607b60 y a
// 0x60cf70. 0x60b3d0 lo da de dueño a las uniones del 0x1a18 (0x60c482..0x60c555): no nulo, así que 0x530860 no escribe
// +0x58, pero nadie lo desreferencia después (07 Q-C1821). Su dirección es la misma en cada llamada; MuRet usa una
// ficha durable que tampoco se lee.
[[nodiscard]] void* UiItemLocalObject();

// Esas uniones nacen en el dibujo de la UI, después del de las uniones (0x637516 y 0x637592), y mueren en el primer
// paso del frame siguiente (la vida en 0, 0x53c3ad..0x53c3bd), antes del dibujo que sigue: el original no las dibuja
// nunca. Los dibujos intermedios de MuRet (GameScene::PrepareDrawProjection) las apagan en su copia de la cola; la
// tabla de verdad las guarda para el paso, con sus rand().
void HideUiItemJoints(Scenes::GameDrawTailState& tail);
} // namespace Client::Items
