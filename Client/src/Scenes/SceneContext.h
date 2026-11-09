#pragma once

#include "App/PacingMode.h"
#include "Core/IClock.h"
#include "Core/Log.h"
#include "Input/InputState.h"
#include "Render/ITextureStore.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"

#include <filesystem>

namespace Client::World
{
struct GameCamera;
struct GameCameraInput;
} // namespace Client::World

// Lo que comparten las escenas (CL5.4). Lo arma main.cpp y vive más que el SceneManager; los tests,
// con un FakeTextureStore (support/). Lo que se leyó antes de la primera escena va aparte, en
// GameData.h.
namespace Client::Scenes
{
class SelectionState;
class WorldState;

// De dónde leen (el Data\ del cliente), adónde suben sus texturas (Render::ITextureStore, CL5.1), el
// log y el reloj del bucle (CL6.4).
struct SceneContext
{
    std::filesystem::path dataRoot;
    Render::ITextureStore& textures;
    Core::Log& log;
    // El de SceneManager: lo que el original lee de DAT_05767b88 en la lógica, como la acción del
    // personaje del login (0x634c60). nullptr sin bucle (los tests que no lo miran): la escena hace como
    // si no lo hubiera.
    Core::IClock* clock = nullptr;
    // Lo global de la tela (Render/Models/Cloth.h): el viento base, el contador de pasos de lógica y el
    // rand() de la CRT, que las escenas 2, 4 y 5 avanzan en cada paso (0x637700). nullptr sin bucle: los
    // personajes van sin tela.
    Render::Models::ClothWorld* cloth = nullptr;
    // El teclado y el mouse del bucle, los de cada paso: lo que el original lee en el dibujo de los
    // globales del WndProc, como el cursor (0x5ac2d0, UI/GameCursor.h). nullptr sin bucle: sin cursor.
    const Input::InputState* input = nullptr;
    // El ritmo del bucle (App/PacingMode.h): en Faithful, lo que el original corre una vez por frame
    // dibujado (la tela y el brillo del cetro) corre uno por dibujo (OriginalFramePaceOf).
    App::PacingMode pacing = App::PacingMode::Decoupled;
    // Estado global de 0x635ae0: vive en main y sobrevive a los cambios de escena (CL7.3 B).
    World::GameCamera* camera = nullptr;
    // Entradas de 0x635ae0, no réplica completa del actor Hero. SelectionState publica la posición
    // construida por cada recepción; el dibujo de selección escribe Hero Z = 163 después de la cámara.
    // L1 aportará XYZ de la respuesta para el juego.
    World::GameCameraInput* cameraInput = nullptr;
    // La selección sobre los cinco primeros registros del pool durable (WorldState) y las tablas del
    // mundo vigentes (CL7.3 B). main es dueño de los dos y viven más que todas las escenas; conserva
    // las tablas del login después de su Leave.
    SelectionState* selection = nullptr;
    // Lo que WinMain deja en cero y dura hasta el cierre (WorldState.h): el registro propio y los
    // globales de la espera de la entrada al mundo (WorldRuntime). Es el mismo que presta `selection`.
    // nullptr en los tests que no lo miran: la selección entra sin preparar el registro propio, y la
    // carga del mundo y el juego no entran (su Enter da el error), porque la espera vive ahí.
    WorldState* world = nullptr;
};

// Cómo cuentan los frames del original la tela y el cetro con el ritmo `pacing`.
[[nodiscard]] constexpr Render::Models::OriginalFramePace OriginalFramePaceOf(App::PacingMode pacing)
{
    return pacing == App::PacingMode::Faithful ? Render::Models::OriginalFramePace::OncePerDraw
                                               : Render::Models::OriginalFramePace::ByTheClock;
}
} // namespace Client::Scenes
