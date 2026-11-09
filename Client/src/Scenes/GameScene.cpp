#include "Scenes/GameScene.h"

#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "Core/Expected.h"
#include "Input/InputState.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"

#include <cstdint>
#include <optional>
#include <string>

namespace Client::Scenes
{
namespace
{
// 0x636838: el yaw de la cámara del juego en el primer paso, 0xc2340000.
constexpr float EntryYaw = -45.0f;
} // namespace

GameScene::GameScene(const SceneContext& context, App::ServerSession* session) : m_context(context), m_session(session)
{
}

Core::Expected<void, std::string> GameScene::Enter()
{
    if (m_context.world == nullptr || m_context.cloth == nullptr)
    {
        return Core::Unexpected(std::string("el juego no entra sin el WorldState y el rand() de la CRT: el contador "
                                            "de la espera vive ahí y sus cierres gastan rand()"));
    }
    m_firstStep = true;
    m_holdFrame = true;
    return {};
}

// 0x636290 y, después, lo de 0x637700 para la tela: el viento (0x637cec) y el contador de pasos
// (0x637f8c) van detrás de la lógica de la escena (02, "Qué corre en cada paso").
void GameScene::Step(const Input::InputState& /*input*/)
{
    StepEntry();
    StepWorld();
    if (m_context.cloth != nullptr)
    {
        Render::Models::StepTheClothWorld(*m_context.cloth);
    }
}

void GameScene::StepEntry()
{
    // 0x636838: solo el primer paso de la escena 5 escribe el yaw, sin reiniciar los otros globales.
    if (m_firstStep)
    {
        if (m_context.camera != nullptr)
            m_context.camera->yaw = EntryYaw;
        m_firstStep = false;
    }
    if (m_sent || m_session == nullptr)
    {
        return;
    }
    m_sent = true;
    m_session->EnterWorld();
}

void GameScene::StepWorld()
{
    // Enter no deja entrar sin los dos.
    if (m_context.world == nullptr || m_context.cloth == nullptr)
    {
        return;
    }
    WorldRuntime& runtime = m_context.world->Runtime();
    // 0x636da3: el estado 0x3d marca el mundo cargado; sin él y con el mundo sin cargar, el paso
    // termina acá (0x636dbc).
    if (m_session != nullptr && m_session->State() == App::SessionState::InWorld)
    {
        runtime.drawingEnabled = true;
    }
    else if (!runtime.drawingEnabled)
    {
        return;
    }
    // Frontera (T8): lo que va de 0x636dc2 a 0x637105, antes de MoveHero (el temblor, 0x5fbf60, la UI,
    // los objetos, las hojas, los pájaros, los peces y el caballo), y lo de 0x63710f en adelante.
    // Frontera (P-C28 b): MoveHero (0x59adf0) sale sin tocar el contador con el recorrido de la cámara
    // (0x4033d0, que además para al héroe con 0x4f2250), con un personaje bajo el mouse y +0x7c &
    // 0x200000 del héroe, con +0x389 o con +0x78 & 0x1000 (0x59ae24..0x59ae87). MuRet no tiene todavía
    // esos campos del héroe ni el recorrido, y baja el contador en cada paso.
    if (StepWaitCounter() > WaitDrawThreshold)
    {
        return;
    }
    // Frontera (T7, T8 y CL7.4): el resto de MoveHero, desde 0x59afdf (el clic, caminar, los gates,
    // 0x57cfc0 con el registro propio y la capa 1 de la celda en 0x77f36d0).
}

std::int32_t GameScene::StepWaitCounter()
{
    WorldRuntime& runtime = m_context.world->Runtime();
    Render::Models::CrtRandom& random = m_context.cloth->random;
    {
        // 0x59ae8d..0x59af5a: lo abre, le resta 1 si es positivo (0x59af01) y lo cierra (0x412890).
        const Render::Models::LogicalAccess access(runtime.warpDepth, random, Render::Models::SmallLogicalCloseRandoms);
        if (runtime.warpCounter > 0)
        {
            --runtime.warpCounter;
        }
    }
    // 0x59af5f..0x59afd1: otro acceso para leerlo, con su propio cierre (0x40b800).
    const Render::Models::LogicalAccess access(runtime.warpDepth, random, Render::Models::SmallLogicalCloseRandoms);
    return runtime.warpCounter;
}

void GameScene::PrepareFrame()
{
    m_holdFrame = true;
    if (m_context.world == nullptr || m_context.cloth == nullptr)
    {
        return;
    }
    WorldRuntime& runtime = m_context.world->Runtime();
    // 0x6371a6: sin el mundo cargado devuelve 0 sin leer el contador.
    if (!runtime.drawingEnabled)
    {
        return;
    }
    std::int32_t counter = 0;
    {
        // 0x6371b7..0x637229: lo lee con un acceso que se cierra (0x40b800) aunque después no dibuje.
        const Render::Models::LogicalAccess access(runtime.warpDepth, m_context.cloth->random,
                                                   Render::Models::SmallLogicalCloseRandoms);
        counter = runtime.warpCounter;
    }
    // 0x63722e: cmp esi, 0x1e y jle, con signo.
    m_holdFrame = counter > WaitDrawThreshold;
}

void GameScene::Render(SceneCanvas& /*canvas*/, float /*alpha*/) {}

void GameScene::Leave() {}

// Q-C70: sin conexión, el juego sigue; el texto 3 del área de mensajes es de la CL9.
std::optional<SceneId> GameScene::Next() const
{
    return std::nullopt;
}

bool GameScene::WantsToQuit() const
{
    return false;
}

bool GameScene::WantsTextInput() const
{
    return false;
}

bool GameScene::HoldsTheLastFrame() const
{
    return m_holdFrame;
}
} // namespace Client::Scenes
