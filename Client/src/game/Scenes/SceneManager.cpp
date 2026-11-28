#include "Scenes/SceneManager.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/Log.h"
#include "Input/InputState.h"
#include "Render/Frame/IFrameTarget.h"
#include "Render/Frame/SceneCanvas.h"
#include "Kernel/SceneId.h"
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
// "Login (2)": the name and the number of DAT_006d5dc8.
std::string Describe(Kernel::SceneId scene)
{
    return std::format("{} ({})", Kernel::SceneName(scene), static_cast<int>(scene));
}
} // namespace

SceneManager::SceneManager(SceneFactory factory, Render::Frame::IFrameTarget& frame, Core::IClock& clock,
                           std::function<Core::Expected<void, std::string>()> pollNetwork, Core::Log& log,
                           std::function<std::optional<Kernel::SceneId>()> takeSceneOrder,
                           std::function<void(Core::Duration)> beforeSceneFrame,
                           std::function<void(Core::Duration)> afterSceneFrame,
                           std::function<Core::Expected<void, std::string>()> afterSceneStep,
                           std::function<Core::Expected<void, std::string>()> beforeSceneDraw,
                           std::function<Core::Expected<void, std::string>(Render::Frame::SceneCanvas&)> afterSceneDraw)
    : m_factory(std::move(factory)), m_frame(frame), m_clock(clock), m_pollNetwork(std::move(pollNetwork)), m_log(log),
      m_takeSceneOrder(std::move(takeSceneOrder)), m_beforeSceneFrame(std::move(beforeSceneFrame)),
      m_afterSceneFrame(std::move(afterSceneFrame)), m_afterSceneStep(std::move(afterSceneStep)),
      m_beforeSceneDraw(std::move(beforeSceneDraw)), m_afterSceneDraw(std::move(afterSceneDraw))
{
}

// The scene that remains releases its own, as when changing: its textures do not pass on to whoever closes.
SceneManager::~SceneManager()
{
    if (m_scene)
    {
        m_scene->Leave();
    }
}

Core::Expected<void, std::string> SceneManager::Start(Kernel::SceneId first)
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
    // The GameLoop starts each frame with Poll: what the network brings can change what the draw reads.
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
    if (const std::optional<Kernel::SceneId> order = m_takeSceneOrder())
    {
        // Two orders before a step: the original only sees the last write.
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
    // The original wrote the scene while reading the network: this step is already the new one's.
    if (const std::optional<Kernel::SceneId> order = std::exchange(m_networkOrder, std::nullopt))
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
    if (m_afterSceneStep)
    {
        if (auto after = m_afterSceneStep(); !after.HasValue())
        {
            m_failure = after.Error();
            m_log.Error("no se pudo terminar el paso de la escena {}: {}", Describe(*m_current), *m_failure);
            return;
        }
    }
    const std::optional<Kernel::SceneId> next = m_scene->Next();
    if (!next)
    {
        // The scene stopped asking: if the rejected request comes back, it is reported again.
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
    // What the draw reads at its start (IScene::PrepareFrame), if nobody asked before in this frame.
    PrepareFrame();
    if (m_beforeSceneDraw && TakeTheDrawFailure(m_beforeSceneDraw()))
    {
        return;
    }
    Render::Frame::SceneCanvas& canvas = m_frame.BeginFrame(m_clock.Now());
    m_scene->Render(canvas, alpha);
    if (TakeTheSceneFailure("dibujar"))
    {
        return;
    }
    if (m_afterSceneDraw && TakeTheDrawFailure(m_afterSceneDraw(canvas)))
    {
        return;
    }
    m_frame.Present();
    // The frame ended: the next one decides again.
    m_framePrepared = false;
}

void SceneManager::Request(Kernel::SceneId next)
{
    if (m_failure || !m_current)
    {
        return;
    }
    const Kernel::SceneId from = *m_current;
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
    // The error is already in Failure and in the log. Its time, whether it succeeds or not, is not game time.
    static_cast<void>(Enter(next));
    m_sceneChanged = true;
}

std::optional<Kernel::SceneId> SceneManager::Current() const
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
    // Querying and getting true also notifies the hold; repeating it is an idempotent notice.
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

bool SceneManager::TakeTheDrawFailure(const Core::Expected<void, std::string>& done)
{
    if (done.HasValue())
    {
        return false;
    }
    m_failure = done.Error();
    m_log.Error("no se pudo terminar el dibujo de la escena {}: {}", Describe(*m_current), *m_failure);
    return true;
}

Core::Expected<void, std::string> SceneManager::Enter(Kernel::SceneId id)
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
        // What managed to load before the error is released anyway: releasing an empty slot does nothing
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
