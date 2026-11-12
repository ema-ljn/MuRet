#pragma once

#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Pass3D.h"
#include "Scenes/FaunaDrawing.h"
#include "World/GameCamera.h"

#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "GameLogic/CharacterAppearance.h"
#include "Input/InputState.h"
#include "Render/Models/ModelTextureSlots.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterCloth.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/CharacterHead.h"
#include "Scenes/DarkHorse.h"
#include "Scenes/FullSetScan.h"
#include "Scenes/GameData.h"
#include "Scenes/IScene.h"
#include "Scenes/LoadedWorld.h"
#include "World/TerrainCells.h"
#include "World/TerrainCulling.h"
#include "World/TerrainLighting.h"
#include "World/Leaves.h"
#include "World/ParticleDrawing.h"
#include "World/Sprites.h"
#include "World/WorldObjects.h"
#include "Data/Terrain/TerrainObjects.h"
#include "Scenes/LoginModels.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneTextures.h"
#include "Scenes/SceneWorlds.h"
#include "Scenes/WorldModels.h"
#include "Scenes/WorldState.h"
#include "UI/EscapeWindow.h"
#include "UI/LoginWindow.h"
#include "UI/MenuBar.h"
#include "UI/NewUi.h"
#include "UI/NoticeWindow.h"
#include "UI/Notices.h"
#include "UI/ServerListWindow.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
    // personaje (0x634c60) sin el ClothWorld del contexto, que trae el rand() de la CRT y es el que usa
    // Main: los tests la fijan.
    // `gameSlots`, la tabla de slots de la carga (GameModels::TextureSlots), que el mundo sigue desde
    // 0x41c como la global del original; sin ella, una vacía.
    LoginScene(const SceneContext& context, const GameData& data, const IModelLibrary& models,
               App::ServerSession* session, std::string_view version, std::mt19937::result_type seed,
               const Render::Models::ModelTextureSlots* gameSlots = nullptr);

    // Las ventanas guardan this (el tamaño de las texturas): no se copia ni se mueve.
    LoginScene(const LoginScene&) = delete;
    LoginScene& operator=(const LoginScene&) = delete;
    ~LoginScene() override = default;

    // Como 0x6349a0, cada vez que se entra: el terreno del mundo 55 (0x61a560, LoadedWorld: el .map,
    // el .att, las alturas, la luz y los tiles; sin él, el login va sin terreno y la luz del terreno es
    // 0), sus objetos (0x616ec0, WorldModels, y el .obj; sin ellos, solo el logo), LoginTextureSet
    // (0x61c7c0), el logo y sus texturas (desde el próximo slot que deja el mundo),
    // WindowTextureSet (las de las ventanas, de 0x61ccb0), el ConnectServer (0x4c1340,
    // ServerSession::Start), el logo y el personaje en su lugar y la cuenta de las rutas en −1. Una
    // textura que falta corta antes de conectar; un Logo03.bmd que no se lee, no (0x60da30 lo saltea).
    Core::Expected<void, std::string> Enter() override;
    // 0x634c60: la acción del personaje (con el reloj del contexto), los objetos y el logo (0x606560), el personaje
    // (0x508f00) y las rutas (0x633920); después, la UI en el paso (Q-C81): lo que pide la sesión, la
    // ventana de arriba y, sin aviso, la barra. La sesión la corre el SceneManager, antes.
    void Step(const Input::InputState& input) override;
    // 0x6350b0 sin el fundido (Q-C66) ni lo común a las escenas: en el pase 3D, el terreno, el
    // personaje (0x510d00), el caballo y los objetos con el logo (0x6046a0); al final la UI: la lista, la barra de
    // abajo, la cuenta y el aviso, cada uno con la mezcla normal.
    void Render(SceneCanvas& canvas, float alpha) override;
    // Cierra las ventanas (la de la cuenta vacía la clave) y suelta sus 21 slots, los del mundo y los del
    // logo.
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
    // Durante un connect, descarta el tiempo sin dibujo de los efectos de los objetos, de la tela y del cetro:
    // el próximo dibujo corre un frame de cada uno. Mantiene el pool, las muestras, las telas y los demás
    // relojes.
    void OnFrameHeld() override;
    // La cabeza del personaje como la dejó el último paso (0x505de0), la que dibuja.
    [[nodiscard]] const CharacterHead& Head() const;
    [[nodiscard]] std::optional<std::string> Failure() const override;
    // El personaje que dibuja el frame con `alpha` (0x510d00 → 0x50ba30): el cuerpo entre pasos, la cabeza
    // del paso, la luz del terreno en su lugar más la suya, y su tela y la tabla de los sprites de la escena, con
    // el ritmo del contexto. Lo que cuelga (el cetro con su brillo) va aparte, con DrawCharacterTail.
    [[nodiscard]] DrawnCharacter TheCharacterToDraw(float alpha);
    // 0x505820 con el personaje que se dibuja, `frames` veces (Scenes/FullSetScan.h), sobre WorldState: lo que
    // devuelve el último, que lee el conjunto completo de la cola de efectos.
    FullSetScan ScanTheCharacter(const GameLogic::CharacterAppearance& appearance, int frames);

private:
    // 0x60da30 y 0x60d5f0 con el 0xa2 (LoginLogoFile), sin un 0x60d530 antes (Q-C92).
    [[nodiscard]] Core::Expected<void, std::string> LoadTheLogo();
    void LoadTheWorld();
    void LoadTheTerrain();
    void RetainTheTerrain();
    void LoadTheObjects();
    // 0x604ab0 con el 0xa2: la luz de las rutas y el avance a +0xd8.
    void StepTheLogo(World::WorldObject& logo) const;
    [[nodiscard]] Core::Expected<void, std::string> PlaceTheModels();
    [[nodiscard]] Core::Expected<void, std::string> PlaceTheCharacter();
    [[nodiscard]] Core::Expected<void, std::string> StepTheCharacters();
    void StepTheWorld();
    void StepTheScene(const Input::InputState& input, bool connecting);
    // 0x444010 (0x44453f..0x4445d9), después del paso del caballo: el aliento, con `drawn`, el caballo como
    // lo dejó el dibujo anterior (antes de este paso).
    void BreatheTheHorse(const DarkHorse& drawn);
    // Dónde queda `offset` en el hueso `bone` del buffer global de la pose (0x68b181c) al llegar al paso:
    // lo dejó el último modelo posado sin buffer propio en el dibujo anterior que tiene ese hueso y no es
    // dummy (LastPosedBonePoint), en el mundo: el dragón (0x52d620) va después del caballo (0x4453b0), pero
    // su hueso 27 es dummy, y en el 27 queda el del caballo. Antes del primer dibujo, (0, 0, 0).
    [[nodiscard]] glm::vec3 GlobalPosePoint(std::size_t bone, const glm::vec3& offset,
                                            const DarkHorse& drawnHorse) const;
    void DrawTheModels(SceneCanvas& canvas, float alpha);
    // 0x4182f0: el cuervo, si pasa el trapecio (si no, MissTheView), y su pose para la chispa.
    void DrawTheRaven(SceneCanvas& canvas, float alpha, FaunaDrawCarry* carry = nullptr);
    // 0x50db57..0x510a7a con el personaje, después de su cuerpo, `frames` veces (con cero, una entre frames); un
    // error queda en m_failure.
    void DrawTheTail(SceneCanvas& canvas, ActorState& actor, const DrawnCharacter& character, FaunaDrawCarry& carry,
                     const FullSetScan& scan, int frames);
    // El corte del pase de partículas, en el log: una vez por entrada.
    void ReportParticleFailure(const World::ParticleDrawingFailure& failure);
    // La luz del terreno en (x, y), o 0 sin la del mundo.
    [[nodiscard]] glm::vec3 TerrainLightAt(const glm::vec3& position) const;
    void DrawTexts(SceneCanvas& canvas) const;
    // 0x63552a..0x63553f: los avisos y el área vieja, antes de la UI.
    void DrawTheMessages(SceneCanvas& canvas);
    // Lo que cambió en la sesión desde el último paso: abre o cierra las ventanas.
    void FollowSession();
    // La respuesta al F4 03 (0x4e3341), conecte o no: el 481 en el área de mensajes, una vez por dirección.
    void NoticeTheServerAddress();
    // 0x555190 con el texto `index` de Text.bmd como sistema y sin nombre: el de 0x6e57a4 y el de 0x5767974 están
    // en 0. `arguments` van a las marcas del texto, que el original usa como formato (Q-C61).
    void AddTheMessage(std::size_t index, std::span<const std::string_view> arguments = {});
    void OnServerClick(const UI::ServerClick& click);
    void OnLoginRequest(const UI::LoginRequest& request);
    void OnNoticeClosed(UI::NoticeId notice);
    // 0x6411b0: el menú abre la de Esc; los créditos se cortan (P-C76).
    void OnMenuBarClick(UI::MenuBarClick click);
    // 0x647d40: lo que pasó en la de Esc.
    void OnEscapeEvent(const UI::EscapeWindowEvent& event);
    // 0x65fc30 al empezar el paso, como 0x637700 antes de 0x634c60: cada ventana con su turno.
    void StepTheWindows(const Input::InputState& input);
    // 0x65f690: abre la ventana con lo suyo y la pasa arriba con el foco.
    void OpenNotice(UI::NoticeId notice);
    void OpenTheEscapeWindow();
    // La ventana de la cuenta, con el título del servidor elegido.
    void OpenLogin(UI::LoginFocus focus);
    // El aviso de una sesión que falló: 0x6f con otra versión, 0x70 con lo demás.
    void ShowFailure();
    [[nodiscard]] std::string LoginTitle() const;
    [[nodiscard]] Scenes::TextureBufferSize BufferSize(Render::TextureSlot slot) const;

    [[nodiscard]] World::GameCamera& CameraState();
    [[nodiscard]] World::GameCameraInput& CameraInputState();
    // El trapecio global del último 0x5fb570 (WorldRuntime::terrainPolygon), el de cualquier escena.
    [[nodiscard]] World::TerrainPolygon& SharedTerrainPolygon();
    [[nodiscard]] WorldState& CharacterWorld() const;
    [[nodiscard]] Render::Models::ClothWorld& CharacterRandomWorld();

    SceneContext m_context;
    // Los tests de vista sin los globales de main usan el mismo pool y paso, con un dueño local en el heap.
    std::unique_ptr<WorldState> m_localWorld;
    Render::Models::ClothWorld m_localCloth;
    std::optional<std::string> m_failure;
    // Fallback explícito para tests sin contexto completo; main siempre aporta ambos globales.
    World::GameCamera m_localCamera;
    World::GameCameraInput m_localCameraInput;
    Render::Models::OriginalFrameCarry m_cameraFrames;
    // 0x602ed0: un sorteo por objeto y frame original, con replay de la luz del farol entre ellos.
    Render::Models::OriginalFrameCarry m_objectEffectFrames;
    // Los frames del original de 0x505820 desde el dibujo del personaje (0x50c91e).
    Render::Models::OriginalFrameCarry m_fullSetScanFrames;
    // Los de los avisos y el área vieja (0x554810 y 0x5554e0): con cero, el dibujo va sobre copias.
    Render::Models::OriginalFrameCarry m_messageFrames;
    std::vector<std::optional<int>> m_objectEffectRandoms;
    const IModelLibrary& m_models;
    App::ServerSession* m_session;
    const Data::Text::TextTable* m_texts;
    SceneTextures m_textures;
    SceneTextures m_windowTextures;
    // El de SceneWorlds.h: el 55.
    std::optional<SceneWorld> m_world;
    LoadedWorld m_terrain;
    // El rango de bloques del terreno con la cámara del login (0x5fbb00): la carga lo inicializa; cada dibujo
    // lo actualiza para el paso siguiente. El trapecio (0x5fb570) es el global de WorldRuntime.
    World::TerrainRange m_terrainRange;
    Render::Camera3D m_drawnCamera;
    // La luz sola, si el terreno no se pudo leer y su luz sí (World::LoadTerrainLighting).
    std::unique_ptr<World::TerrainLighting> m_lighting;
    const Render::Models::ModelTextureSlots* m_gameSlots;
    // Los modelos de Object56\ y los registros de EncTerrain56.obj, de cada entrada.
    WorldModels m_worldModels;
    std::vector<Data::Terrain::TerrainObject> m_objectRecords;
    // Los objetos del mundo como los crea 0x606660, y el logo al final (0x61c7c0 lo crea después de la
    // carga del mundo).
    std::vector<World::WorldObject> m_objects;
    std::optional<std::size_t> m_logoIndex;
    // Los sprites de un frame (0x760f990): los destellos de la tela y el brillo del cetro.
    World::FrameSprites m_sprites;
    // Las hojas (0x75e34b8): en el mapa 55, las 80 brasas.
    World::Leaves m_leaves;
    // Si ya se dibujó un frame desde que se entró: antes, el buffer global de la pose no tiene nada del
    // login (P-C26).
    bool m_drawnSinceEntering = false;
    // Si ya corrió un paso desde que se entró: antes, 0x6350b0 no dibuja (0x7d3a13d, que prende el paso).
    bool m_steppedSinceEntering = false;
    // Si el pase de partículas ya se cortó (y se dijo en el log) desde que se entró.
    bool m_particleFailureReported = false;
    // Los sprites de lo que cuelga y de los efectos del personaje (el brillo del cetro, 0x509340): los del último
    // frame del original, que un dibujo intermedio repite, como el juego y la selección con toda la tabla.
    World::FrameSprites m_tailSprites;
    World::FrameSprites m_tailSpriteReplay;
    bool m_tailSpriteReplayValid = false;
    // El logo: su modelo, los slots de sus mallas desde donde terminó el mundo y sus texturas.
    SingleModelLibrary m_logo;
    std::optional<Render::Models::ModelTextureSlots> m_logoSlots;
    std::optional<SceneTextures> m_logoTextures;
    LoginRoutes m_routes;
    // El caballo que monta (0x443fa0(0xbf, …) de 0x6349a0): sigue al personaje.
    DarkHorse m_horse;
    // La tela del Dark Lord (0x50ba30), entre frames.
    CharacterCloth m_characterCloth;
    // Los tres textos, o vacíos sin Text.bmd. El de la versión, ya armado.
    std::optional<std::string_view> m_left;
    std::optional<std::string_view> m_right;
    std::optional<std::string> m_version;

    UI::ServerListWindow m_serverList;
    // La abre la lista y queda abierta hasta salir (06 § Las ventanas de la UI nueva).
    UI::MenuBar m_menuBar;
    UI::LoginWindow m_login;
    UI::NoticeWindow m_notice;
    UI::EscapeWindow m_escape;
    // El administrador de la UI nueva (CL8, hito 8): el orden, el foco y el mouse de las ventanas.
    UI::NewUi m_ui;
    // El estado de la sesión que vio el último paso, para abrir cada ventana una vez.
    std::optional<App::SessionState> m_seen;
    // Si el último paso vio la dirección del F4 03 en la sesión.
    bool m_sawTheAddress = false;
    // La pantalla del último frame: las ventanas se ubican y leen el mouse con ella. Hasta el
    // primero, la de referencia.
    App::PixelSize m_screen{640, 480};
    bool m_quit = false;
};
} // namespace Client::Scenes
