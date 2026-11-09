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
                           std::function<Core::Expected<void, std::string>()> pollNetwork, Core::Log& log)
    : m_factory(std::move(factory)), m_frame(frame), m_clock(clock), m_pollNetwork(std::move(pollNetwork)), m_log(log)
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
    m_framePrepared = false;
    if (m_pollFailed || (m_failure && m_scene) || !m_pollNetwork)
    {
        return;
    }
    const auto polled = m_pollNetwork();
    if (!polled.HasValue())
    {
        m_failure = polled.Error();
        m_pollFailed = true;
        m_log.Error("no se pudo aplicar la recepción de la sesión: {}", *m_failure);
    }
}

void SceneManager::Step(const Input::InputState& input)
{
    if (m_failure || !m_scene)
    {
        return;
    }
    m_framePrepared = false;
    m_scene->Step(input);
    if (const auto failure = m_scene->Failure())
    {
        m_failure = *failure;
        m_log.Error("no se pudo avanzar la escena: {}", *m_failure);
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
    if (HoldsTheLastFrame())
        return;
    SceneCanvas& canvas = m_frame.BeginFrame(m_clock.Now());
    m_scene->Render(canvas, alpha);
    if (const auto failure = m_scene->Failure())
    {
        m_failure = *failure;
        m_log.Error("no se pudo dibujar la escena: {}", *m_failure);
        return;
    }
    m_frame.Present();
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
    if (!m_framePrepared && m_scene)
    {
        m_scene->PrepareFrame();
        m_framePrepared = true;
    }
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
    m_framePrepared = false;
    m_scene = std::move(scene);
    m_current = id;
    return {};
}

bool SceneManager::TakeTheSceneChange()
{
    const bool changed = m_sceneChanged;
    m_sceneChanged = false;
    return changed;
}
} // namespace Client::Scenes
