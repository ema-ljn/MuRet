#include "Scenes/SceneManager.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/Log.h"
#include "Input/InputState.h"
#include "Scenes/IFrameTarget.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneTransitions.h"

#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace Client::Scenes
{
namespace
{
// "Login (2)": el nombre y el número de DAT_006d5dc8.
std::string Describe(SceneId scene)
{
    return std::format("{} ({})", SceneName(scene), static_cast<int>(scene));
}
} // namespace

SceneManager::SceneManager(SceneFactory factory, IFrameTarget& frame, Core::IClock& clock,
                           std::function<Core::Expected<void, std::string>()> pollNetwork, Core::Log& log,
                           std::function<std::optional<SceneId>()> takeSceneOrder,
                           std::function<void(Core::Duration)> beforeSceneFrame,
                           std::function<void(Core::Duration)> afterSceneFrame)
    : m_factory(std::move(factory)), m_frame(frame), m_clock(clock), m_pollNetwork(std::move(pollNetwork)), m_log(log),
      m_takeSceneOrder(std::move(takeSceneOrder)), m_beforeSceneFrame(std::move(beforeSceneFrame)),
      m_afterSceneFrame(std::move(afterSceneFrame))
{
}

// La escena que queda suelta lo suyo, como al cambiar: sus texturas no pasan al que cierra.
SceneManager::~SceneManager()
{
    if (m_scene)
    {
        m_scene->Leave();
    }
}

Core::Expected<void, std::string> SceneManager::Start(SceneId first)
{
    if (m_pollFailed)
    {
        return Core::Unexpected(*m_failure);
    }
    if (m_current)
    {
        std::string error = std::format("el SceneManager ya está en la escena {}: Start no entra a la {}",
                                        Describe(*m_current), Describe(first));
        m_log.Error("{}", error);
        return Core::Unexpected(std::move(error));
    }
    m_failure.reset();
    m_log.Info("escena {}", Describe(first));
    return Enter(first);
}

void SceneManager::Poll()
{
    // El GameLoop empieza cada frame con Poll: lo que trae la red puede cambiar lo que lee el dibujo.
    m_framePrepared = false;
    if (m_pollFailed || !m_pollNetwork)
    {
        return;
    }
    const auto polled = m_pollNetwork();
    if (!polled.HasValue())
    {
        m_failure = polled.Error();
        m_pollFailed = true;
        m_log.Error("no se pudo aplicar la recepción de la sesión: {}", *m_failure);
        return;
    }
    if (!m_takeSceneOrder)
    {
        return;
    }
    if (const std::optional<SceneId> order = m_takeSceneOrder())
    {
        // Dos órdenes antes de un paso: el original solo ve la última escritura.
        m_networkOrder = order;
    }
}

void SceneManager::Step(const Input::InputState& input)
{
    if (m_failure || !m_scene)
    {
        return;
    }
    m_framePrepared = false;
    // El original escribió la escena mientras leía la red: este paso ya es el de la nueva.
    if (const std::optional<SceneId> order = std::exchange(m_networkOrder, std::nullopt))
    {
        Request(*order);
        if (m_failure || !m_scene)
        {
            return;
        }
    }
    m_scene->Step(input);
    if (TakeTheSceneFailure("avanzar"))
    {
        return;
    }
    const std::optional<SceneId> next = m_scene->Next();
    if (!next)
    {
        // La escena dejó de pedir: si el pedido rechazado vuelve, se avisa otra vez.
        m_rejected.reset();
        return;
    }
    Request(*next);
}

void SceneManager::Render(float alpha)
{
    if (m_failure || !m_scene)
    {
        return;
    }
    // Lo que el dibujo lee al empezar (IScene::PrepareFrame), si nadie preguntó antes en este frame.
    PrepareFrame();
    SceneCanvas& canvas = m_frame.BeginFrame(m_clock.Now());
    m_scene->Render(canvas, alpha);
    if (TakeTheSceneFailure("dibujar"))
    {
        return;
    }
    m_frame.Present();
    // El frame terminó: el próximo vuelve a decidir.
    m_framePrepared = false;
}

void SceneManager::Request(SceneId next)
{
    if (m_failure || !m_current)
    {
        return;
    }
    const SceneId from = *m_current;
    if (!IsOriginalTransition(from, next))
    {
        if (m_rejected != next)
        {
            m_log.Error("de la escena {} no se va a la {}: no es una transición del original", Describe(from),
                        Describe(next));
            m_rejected = next;
        }
        return;
    }
    m_rejected.reset();
    m_log.Info("escena {} a {}", Describe(from), Describe(next));
    m_scene->Leave();
    m_scene.reset();
    m_current.reset();
    // El error ya quedó en Failure y en el log. Lo que tardó, salga o no, no es tiempo del juego.
    static_cast<void>(Enter(next));
    m_sceneChanged = true;
}

std::optional<SceneId> SceneManager::Current() const
{
    return m_current;
}

const std::optional<std::string>& SceneManager::Failure() const
{
    return m_failure;
}

bool SceneManager::WantsToQuit() const
{
    return m_scene && m_scene->WantsToQuit();
}

bool SceneManager::WantsTextInput() const
{
    return m_scene && m_scene->WantsTextInput();
}

void SceneManager::FinishTheLoop()
{
    if (m_failure || !m_scene || !m_afterSceneFrame)
    {
        return;
    }
    m_afterSceneFrame(m_clock.Now());
}

bool SceneManager::HoldsTheLastFrame() const
{
    if (m_failure)
    {
        return true;
    }
    PrepareFrame();
    if (!m_scene || !m_scene->HoldsTheLastFrame())
    {
        return false;
    }
    // Consultar y obtener true también notifica la retención; repetirlo es un aviso idempotente.
    m_scene->OnFrameHeld();
    return true;
}

void SceneManager::PrepareFrame() const
{
    if (m_framePrepared || !m_scene)
    {
        return;
    }
    if (m_beforeSceneFrame)
        m_beforeSceneFrame(m_clock.Now());
    m_scene->PrepareFrame();
    m_framePrepared = true;
}

bool SceneManager::TakeTheSceneFailure(const char* where)
{
    std::optional<std::string> failure = m_scene->Failure();
    if (!failure)
    {
        return false;
    }
    m_failure = std::move(failure);
    m_log.Error("no se pudo {} la escena {}: {}", where, Describe(*m_current), *m_failure);
    return true;
}

Core::Expected<void, std::string> SceneManager::Enter(SceneId id)
{
    std::unique_ptr<IScene> scene = m_factory(id);
    if (!scene)
    {
        m_failure = std::format("no hay escena para la {}", Describe(id));
        m_log.Error("{}", *m_failure);
        return Core::Unexpected(*m_failure);
    }
    const auto entered = scene->Enter();
    if (!entered.HasValue())
    {
        // Lo que alcanzó a subir antes del error se suelta igual: soltar un slot vacío no hace nada
        // (Render::ITextureStore).
        scene->Leave();
        m_failure = entered.Error();
        m_log.Error("la escena {} no pudo entrar: {}", Describe(id), *m_failure);
        return Core::Unexpected(*m_failure);
    }
    m_scene = std::move(scene);
    m_current = id;
    m_framePrepared = false;
    return {};
}

bool SceneManager::TakeTheSceneChange()
{
    const bool changed = m_sceneChanged;
    m_sceneChanged = false;
    return changed;
}
} // namespace Client::Scenes
