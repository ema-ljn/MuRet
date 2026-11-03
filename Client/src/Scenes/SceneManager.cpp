#include "Scenes/SceneManager.h"

#include "Scenes/SceneTransitions.h"

#include <format>
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
                           std::function<void()> pollNetwork, Core::Log& log)
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
    if (m_pollNetwork)
    {
        m_pollNetwork();
    }
}

void SceneManager::Step(const Input::InputState& input)
{
    if (!m_scene)
    {
        return;
    }
    m_scene->Step(input);
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
    if (!m_scene)
    {
        return;
    }
    SceneCanvas& canvas = m_frame.BeginFrame(m_clock.Now());
    m_scene->Render(canvas, alpha);
    m_frame.Present();
}

void SceneManager::Request(SceneId next)
{
    if (!m_current)
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
    // El error ya quedó en Failure y en el log.
    static_cast<void>(Enter(next));
}

std::optional<SceneId> SceneManager::Current() const
{
    return m_current;
}

const std::optional<std::string>& SceneManager::Failure() const
{
    return m_failure;
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
    return {};
}
} // namespace Client::Scenes
