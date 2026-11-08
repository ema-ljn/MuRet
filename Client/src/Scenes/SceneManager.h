#pragma once

#include "App/ILoopClient.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/Log.h"
#include "Input/InputState.h"
#include "Scenes/IFrameTarget.h"
#include "Scenes/IScene.h"
#include "Scenes/SceneId.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

// La máquina de escenas (CL5.4): el cliente del bucle de Main, que tiene la escena de ahora y la
// cambia solo por una transición de la tabla del original (SceneTransitions.h). Cambiar es Leave de
// la que sale, la fábrica arma la nueva y su Enter; un Enter que falla deja el SceneManager sin
// escena, con el error en Failure, para que main.cpp cierre, y la escena que falló hace su Leave,
// que suelta lo que alcanzó a subir. Al destruirse, la escena de ahora hace su Leave.
//
// La red no es de una escena: la ServerSession sigue viva del login a la selección y al juego. El
// SceneManager la corre en cada Poll, también minimizada, con `pollNetwork`. Un error de aplicación
// conserva el primer diagnóstico y bloquea red, pasos, transiciones y dibujo hasta el cierre normal.
namespace Client::Scenes
{
// Arma la escena de un SceneId: main.cpp con las de verdad, los tests con dobles. nullptr si no
// tiene esa escena.
using SceneFactory = std::function<std::unique_ptr<IScene>(SceneId)>;

class SceneManager final : public App::ILoopClient
{
public:
    // `pollNetwork` corre en cada Poll: main.cpp propaga Online::Poll y, sin servidor, devuelve
    // éxito sin red. El callback puede estar vacío. La fábrica, el frame, el reloj y el log viven
    // más que el SceneManager.
    SceneManager(SceneFactory factory, IFrameTarget& frame, Core::IClock& clock,
                 std::function<Core::Expected<void, std::string>()> pollNetwork, Core::Log& log);
    ~SceneManager() override;

    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

    // Entra a `first` (main.cpp pasa FirstScene, la carga). Si su Enter falla, el error, que queda
    // también en Failure. Con una escena ya entrada es un error y la de ahora sigue: para cambiar
    // está Request. Un error anterior de Poll es terminal, incluso antes de la primera escena:
    // devuelve el primer diagnóstico y no entra. Un Enter fallido sí permite reintentar Start.
    Core::Expected<void, std::string> Start(SceneId first);
    // La red, también minimizada y antes de Start; sólo un fallo de Poll la detiene.
    // Un Enter fallido no corta la red mientras el host decide reintentar Start.
    void Poll() override;
    // El paso de la escena y, si pide otra (Next), Request. Un paso sin pedido corta la seguidilla
    // de un pedido rechazado: si vuelve, se avisa otra vez.
    void Step(const Input::InputState& input) override;
    // BeginFrame con el reloj, el Render de la escena y Present. Sin escena, nada.
    void Render(float alpha) override;

    // Cambia a `next` si de la escena de ahora se puede ir (IsOriginalTransition): Leave, la
    // fábrica y Enter. Si no se puede, un error en el log (una vez por pedido seguido igual) y nada
    // cambia. No se llama desde adentro de una escena: la escena lo pide con Next.
    void Request(SceneId next);

    // La escena de ahora; vacío antes de Start o después de un Enter que falló.
    [[nodiscard]] std::optional<SceneId> Current() const;
    // El error vigente de Enter o el primer fallo terminal de Poll (que gana al de Enter), o vacío.
    [[nodiscard]] const std::optional<std::string>& Failure() const;
    // Si la escena de ahora pide cerrar el juego (IScene::WantsToQuit); sin escena, false.
    [[nodiscard]] bool WantsToQuit() const;
    // Si la escena de ahora pide el texto tecleado (IScene::WantsTextInput); sin escena, false.
    [[nodiscard]] bool WantsTextInput() const;
    // Con Failure retiene sin notificar la escena; de otro modo consulta IScene::HoldsTheLastFrame.
    [[nodiscard]] bool HoldsTheLastFrame() const override;
    // Si un Step entró a otra escena desde la última vez (Request); el Start no cuenta, porque va antes
    // del bucle.
    [[nodiscard]] bool TakeTheSceneChange() override;

private:
    // La fábrica y el Enter de `id`; si falla, queda sin escena y con el error.
    Core::Expected<void, std::string> Enter(SceneId id);

    SceneFactory m_factory;
    IFrameTarget& m_frame;
    Core::IClock& m_clock;
    std::function<Core::Expected<void, std::string>()> m_pollNetwork;
    Core::Log& m_log;
    std::unique_ptr<IScene> m_scene;
    std::optional<SceneId> m_current;
    std::optional<std::string> m_failure;
    // Distingue el aborto fatal de Poll del Enter fallido que Start puede reintentar.
    bool m_pollFailed = false;
    // El último pedido rechazado, para no repetir el aviso en cada paso mientras se pida seguido.
    std::optional<SceneId> m_rejected;
    bool m_sceneChanged = false;
};
} // namespace Client::Scenes
