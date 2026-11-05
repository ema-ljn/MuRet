#pragma once

#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "GameLogic/CharacterAppearance.h"
#include "Render/Models/Pose.h"
#include "Input/InputState.h"
#include "Render/Models/ModelTextureSlots.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterCloth.h"
#include "Scenes/CharacterAnimation.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/DarkHorse.h"
#include "Scenes/GameData.h"
#include "Scenes/IScene.h"
#include "Scenes/LoadedWorld.h"
#include "World/TerrainLighting.h"
#include "Scenes/LoginModels.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneTextures.h"
#include "Scenes/SceneWorlds.h"
#include "UI/LoginWindow.h"
#include "UI/MenuBar.h"
#include "UI/NoticeWindow.h"
#include "UI/Notices.h"
#include "UI/ServerListWindow.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>

// La escena 2 (CL5.4): el login de 0x634c60, que entra con 0x6349a0 y dibuja con 0x6350b0 (06 § El
// login, al entrar y § El login, el dibujo), con el logo y el personaje (CL6.4, Scenes/LoginModels.h)
// y sin la música (CL10). Con la sesión, su UI (CL5.5): la lista de servidores cuando la sesión espera la elección,
// con la barra de abajo, que se abre con ella y queda hasta salir; la cuenta cuando espera la
// cuenta y la clave, y los avisos de 06 § Los avisos. La sesión dice qué ventana va: la escena la
// mira en cada paso y, con lo que pide el jugador, la llama.
//
// En cada paso se atiende solo la ventana de arriba (el aviso, la cuenta o la lista): así el Enter
// que cierra una no le llega a la que se abre en el mismo paso. Sin aviso, también la barra, cuyos
// botones no se pisan con los de la cuenta ni con los de la lista (Q-C84). El OK de los avisos
// 0x1a, 0x1c, 0x6f y 0x70 cierra el juego (Q-C69): la escena lo dice en WantsToQuit.
namespace Client::Scenes
{
// Los tres textos de abajo, por su índice en Text.bmd.
inline constexpr std::size_t LoginLeftText = 454;
inline constexpr std::size_t LoginRightText = 455;
inline constexpr std::size_t LoginVersionText = 456;

// El 456 con la versión, como el sprintf de 0x635464 pero sin usar el texto como formato (Q-C61):
// el primer "%s" pasa a ser `version` y lo demás queda como está, otros "%" también. Sin "%s", el
// texto tal cual.
[[nodiscard]] std::string PlaceVersion(std::string_view text, std::string_view version);

class LoginScene final : public IScene
{
public:
    // `data`, `models` y `session` tienen que vivir más que la escena. `session` es nullptr sin servidor
    // (Main sin host): la escena dibuja y no conecta. `version` es la de MuRet (MURET_VERSION).
    // `models` tiene los del personaje (GameModels). `seed` es la de los rand() de la acción del
    // personaje (0x634c60): los tests la fijan.
    LoginScene(const SceneContext& context, const GameData& data, const IModelLibrary& models,
               App::ServerSession* session, std::string_view version, std::mt19937::result_type seed);

    // Las ventanas guardan this (el tamaño de las texturas): no se copia ni se mueve.
    LoginScene(const LoginScene&) = delete;
    LoginScene& operator=(const LoginScene&) = delete;
    ~LoginScene() override = default;

    // Como 0x6349a0, cada vez que se entra: el terreno del mundo 55 (0x61a560, LoadedWorld: el .map,
    // el .att, las alturas, la luz y los tiles; sin él, el login va sin terreno y la luz del terreno es
    // 0), LoginTextureSet (0x61c7c0), el logo y sus texturas,
    // WindowTextureSet (las de las ventanas, de 0x61ccb0), el ConnectServer (0x4c1340,
    // ServerSession::Start), el logo y el personaje en su lugar y la cuenta de las rutas en −1. Una
    // textura que falta corta antes de conectar; un Logo03.bmd que no se lee, no (0x60da30 lo saltea).
    Core::Expected<void, std::string> Enter() override;
    // 0x634c60: la acción del personaje (con el reloj del contexto), el logo (0x606560), el personaje
    // (0x508f00) y las rutas (0x633920); después, la UI en el paso (Q-C81): lo que pide la sesión, la
    // ventana de arriba y, sin aviso, la barra. La sesión la corre el SceneManager, antes.
    void Step(const Input::InputState& input) override;
    // 0x6350b0 sin el fundido (Q-C66) ni lo común a las escenas: en el pase 3D, el mundo, el personaje
    // (0x510d00) y el logo (0x6046a0); al final la UI: la lista, la barra de abajo, la cuenta y el
    // aviso, cada uno con la mezcla normal.
    void Render(SceneCanvas& canvas, float alpha) override;
    // Cierra las ventanas (la de la cuenta vacía la clave) y suelta sus 21 slots y los del logo.
    void Leave() override;
    // La selección con el login aceptado (SessionState::LoggedIn), como 0x634dce con el 0x14.
    [[nodiscard]] std::optional<SceneId> Next() const override;
    // Con el OK de un aviso que cierra el juego (Q-C69).
    [[nodiscard]] bool WantsToQuit() const override;
    // Con la ventana de la cuenta abierta: es la de arriba, porque un aviso la cierra al abrirse.
    [[nodiscard]] bool WantsTextInput() const override;
    // Mientras la sesión conecta al CS o al GS, y hasta que un paso de la escena vio el estado de
    // ahora: el original conecta con un connect bloqueante (0x4c1340, que llama a 0x4e8570: al CS
    // desde 0x6349a0, antes del primer frame del login, y desde el cancelar, 0x642837; al GS desde
    // 0x4e3396), y en la pantalla queda el último frame hasta que termina (Q-C85). Así el primer
    // frame que se dibuja después ya trae lo que abrió el paso: el aviso de la falla, o la espera.
    // Hasta el paso y no solo mientras conecta: un connect que falla enseguida sale en el Poll, que
    // puede venir sin un paso antes del dibujo (después de Enter, o en un frame sin pasos).
    [[nodiscard]] bool HoldsTheLastFrame() const override;

private:
    // 0x60da30 y 0x60d5f0 con el 0xa2 (LoginLogoFile), sin un 0x60d530 antes (Q-C92).
    [[nodiscard]] Core::Expected<void, std::string> LoadTheLogo();
    void LoadTheWorld();
    void PlaceTheModels();
    void StepTheWorld();
    void DrawTheModels(SceneCanvas& canvas, float alpha);
    // La luz del terreno en (x, y), o 0 sin la del mundo.
    [[nodiscard]] glm::vec3 TerrainLightAt(const glm::vec3& position) const;
    void DrawTexts(SceneCanvas& canvas) const;
    // Lo que cambió en la sesión desde el último paso: abre o cierra las ventanas.
    void FollowSession();
    void OnServerClick(const UI::ServerClick& click);
    void OnLoginRequest(const UI::LoginRequest& request);
    void OnNoticeClosed(UI::NoticeId notice);
    // Los dos botones de la barra, que todavía no hacen nada (P-C13).
    void OnMenuBarClick(UI::MenuBarClick click);
    // La ventana de la cuenta, con el título del servidor elegido.
    void OpenLogin(UI::LoginFocus focus);
    // El aviso de una sesión que falló: 0x6f con otra versión, 0x70 con lo demás.
    void ShowFailure();
    [[nodiscard]] std::string LoginTitle() const;
    [[nodiscard]] Scenes::TextureBufferSize BufferSize(Render::TextureSlot slot) const;

    SceneContext m_context;
    const IModelLibrary& m_models;
    App::ServerSession* m_session;
    const Data::Text::TextTable* m_texts;
    SceneTextures m_textures;
    SceneTextures m_windowTextures;
    // El de SceneWorlds.h: el 55.
    std::optional<SceneWorld> m_world;
    LoadedWorld m_terrain;
    // La luz sola, si el terreno no se pudo leer y su luz sí (World::LoadTerrainLighting).
    std::unique_ptr<World::TerrainLighting> m_lighting;
    // El logo: su modelo, los slots de sus mallas desde LoginLogoFirstSlot, sus texturas y su objeto.
    SingleModelLibrary m_logo;
    std::optional<Render::Models::ModelTextureSlots> m_logoSlots;
    std::optional<SceneTextures> m_logoTextures;
    ModelObject m_logoObject;
    // El +0x16c del logo: 0x606560 solo corre los objetos que se dibujaron.
    bool m_logoSeen = false;
    Render::Models::AnimationState m_logoAnimation;
    LoginRoutes m_routes;
    // El personaje de 0x517270(2, 4, …).
    GameLogic::CharacterAppearance m_character;
    CharacterAnimation m_characterAnimation;
    // El caballo que monta (0x443fa0(0xbf, …) de 0x6349a0): sigue al personaje.
    DarkHorse m_horse;
    // La tela del Dark Lord (0x50ba30), entre frames.
    CharacterCloth m_characterCloth;
    std::mt19937 m_random;
    // Los tres textos, o vacíos sin Text.bmd. El de la versión, ya armado.
    std::optional<std::string_view> m_left;
    std::optional<std::string_view> m_right;
    std::optional<std::string> m_version;

    UI::ServerListWindow m_serverList;
    // La abre la lista y queda abierta hasta salir (06 § Las ventanas de la UI nueva).
    UI::MenuBar m_menuBar;
    UI::LoginWindow m_login;
    UI::NoticeWindow m_notice;
    // El estado de la sesión que vio el último paso, para abrir cada ventana una vez.
    std::optional<App::SessionState> m_seen;
    // La pantalla del último frame: las ventanas se ubican y leen el mouse con ella. Hasta el
    // primero, la de referencia.
    App::PixelSize m_screen{640, 480};
    bool m_quit = false;
};
} // namespace Client::Scenes
