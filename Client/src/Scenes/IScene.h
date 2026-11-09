#pragma once

#include "Core/Expected.h"
#include "Input/InputState.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneId.h"

#include <optional>
#include <string>

// Una escena (CL5.4): lo que en el original es un número en DAT_006d5dc8 y un `if` por escena en
// la lógica de cada paso y en el dibujo, acá es un objeto con entrar, paso, dibujo y salir. Lo que
// sube al entrar (sus texturas, sus ventanas) vive lo que ella: lo suelta en Leave. La arma una
// SceneFactory (SceneManager.h) cada vez que se entra, así que entrar otra vez es empezar de cero,
// como el original con sus banderas de "ya entré" en 0.
namespace Client::Scenes
{
class IScene
{
public:
    virtual ~IScene() = default;
    // Lo que el original hace al entrar (0x6349a0 en el login). Un error, como una textura que
    // falta, corta: el SceneManager no sigue y main.cpp cierra, con el error en el log.
    virtual Core::Expected<void, std::string> Enter() = 0;
    // Un paso de la lógica, 25 por segundo (02 § El ritmo).
    virtual void Step(const Input::InputState& input) = 0;
    // El dibujo del frame en `canvas`. alpha en [0, 1), como el de ILoopClient::Render.
    virtual void Render(SceneCanvas& canvas, float alpha) = 0;
    // Una decisión de dibujo por frame, antes de retener o presentar. Holds sólo consulta el
    // resultado; P puede cerrar aquí su lectura aunque se omita Render.
    virtual void PrepareFrame() {}
    // Suelta lo que subió en Enter. Corre antes del Enter de la que sigue.
    virtual void Leave() = 0;
    // A qué escena quiere ir; vacío si sigue en esta. El SceneManager lo mira después de cada
    // Step.
    [[nodiscard]] virtual std::optional<SceneId> Next() const = 0;
    // Si el jugador cerró el juego desde la escena, como el OK del aviso de la conexión del login,
    // que en el original postea WM_CLOSE (Q-C69, CL5.5). main.cpp lo mira después de cada frame.
    [[nodiscard]] virtual bool WantsToQuit() const = 0;
    // Si la escena tiene arriba un campo de texto que lee lo tecleado, como la ventana de la cuenta
    // del login (CL5.5). main.cpp prende el texto de SDL solo mientras tanto
    // (SDL_StartTextInput) y lo apaga cuando deja de pedirlo, después de cada frame.
    [[nodiscard]] virtual bool WantsTextInput() const = 0;
    // Si el frame de ahora no se dibuja y queda el anterior (ILoopClient::HoldsTheLastFrame): el
    // login mientras conecta, que en el original es un connect bloqueante (Q-C85).
    [[nodiscard]] virtual bool HoldsTheLastFrame() const = 0;
    // SceneManager avisa cuando su consulta HoldsTheLastFrame devuelve true. El aviso puede repetirse
    // y debe ser idempotente. Por defecto, nada.
    virtual void OnFrameHeld() {}
    // Fallo de una frontera productiva durante Step; el manager lo conserva y main cierra con error.
    [[nodiscard]] virtual std::optional<std::string> Failure() const
    {
        return std::nullopt;
    }
};
} // namespace Client::Scenes
