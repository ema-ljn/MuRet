#pragma once

#include "Render/Models/Animation.h"
#include "World/GameCamera.h"

#include "App/ServerSession.h"
#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Data/Tables/ItemTable.h"
#include "Scenes/CharacterStep.h"
#include "Data/Text/TextTable.h"
#include "GameLogic/CharacterAppearance.h"
#include "Input/DoubleClick.h"
#include "Input/InputState.h"
#include "Protocol/Character/CharacterList.h"
#include "Render/Models/ModelTextureSlots.h"
#include "Render/Models/Pose.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterCloth.h"
#include "Scenes/CharacterAnimation.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/CreatePreview.h"
#include "Scenes/FullSetScan.h"
#include "Scenes/GameData.h"
#include "Scenes/IScene.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneTextures.h"
#include "Scenes/SelectionWorld.h"
#include "UI/CreateCharacterWindow.h"
#include "UI/EscapeWindow.h"
#include "UI/NameChecks.h"
#include "UI/NewUi.h"
#include "UI/NoticeWindow.h"
#include "UI/SelectionBar.h"
#include "World/TerrainCells.h"
#include "World/TerrainCulling.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// La escena 4, la selección de personaje (0x633f20 al entrar, 0x6342c0 la lógica y 0x634680 el
// dibujo): los personajes de la lista en sus lugares, animados, el clic que elige el que está bajo el
// mouse, y Enter o el doble clic que entra al mundo con el elegido. Arriba, la barra de la selección
// (UI::SelectionBar) con conectar, crear y borrar, la ventana de crear (UI::CreateCharacterWindow) y
// los avisos. Con el paso común del pool y del NewFace y el mundo 54 (SelectionWorld: el terreno, los
// objetos y sus efectos, la luz y las dos 0x729 del elegido); sin el menú y las opciones (CL8), los
// carteles de los personajes ni el área de mensajes.
// Contrato: Client/docs/research/06-render-escenas-y-ui.md, "La selección de personaje", y el plan de
// la CL6 (CL6.3, "Contrato").
namespace Client::Scenes
{
struct FaunaDrawCarry;
struct ActorState;

class CharacterSelectScene final : public IScene
{
public:
    // `data`, `models` y `session` tienen que vivir más. `session` es nullptr sin servidor: la escena
    // dibuja los fondos y no pide nada.
    // `gameSlots` es la tabla de los slots de la carga (GameModels::TextureSlots), que la vista previa
    // de la ventana de crear sigue desde 0x6f8; nullptr, una tabla vacía. Tiene que vivir más.
    CharacterSelectScene(const SceneContext& context, const GameData& data, const IModelLibrary& models,
                         App::ServerSession* session, const Render::Models::ModelTextureSlots* gameSlots = nullptr);

    // Como 0x633f20: su juego de texturas y sus modelos (0x61ca30: Logo05 y los NewFace de la vista
    // previa, SelectionModels), el de las ventanas, ninguno elegido, y el F3 00
    // (ServerSession::RequestCharacterList).
    Core::Expected<void, std::string> Enter() override;
    // 0x6342c0: nada hasta la lista; después, los personajes de la lista y su avance, las respuestas
    // del borrado y de crear, y la UI de arriba hacia abajo: el aviso si está abierto (con la ventana
    // de crear debajo, que sigue tomando lo que se escribe); si no, la ventana de crear, que es modal;
    // si no, la barra, el doble clic que entra, el clic que elige y Enter. Con el mouse sobre la
    // barra o una ventana abierta (+0x8de8), ni el clic ni el doble clic, y el contorno se queda en el
    // último; un aviso que se abre en el paso se atiende desde el que sigue.
    void Step(const Input::InputState& input) override;
    // 0x634680 sin el terreno, los objetos ni los efectos: con sesión, nada hasta la lista; el de bajo
    // el mouse, con el contorno. Después, la UI: la barra, la ventana de crear (su base, la vista
    // previa en su propio pase 3D, que no se oscurece, y lo demás) y el aviso. En cada frame del original,
    // la cámara y el trapecio de 0x5fbb00 (0x63475c), que deja en WorldRuntime para el primer paso del
    // juego; si el trapecio da error, el frame se corta y la escena queda en Failure.
    void Render(SceneCanvas& canvas, float alpha) override;
    // Suelta sus slots.
    void Leave() override;
    // WorldLoading después de entrar. Si la sesión falla, la escena no se va: abre el aviso 0x70.
    [[nodiscard]] std::optional<SceneId> Next() const override;
    // Con el OK del aviso 0x70, que cierra el juego (Q-C69).
    [[nodiscard]] bool WantsToQuit() const override;
    [[nodiscard]] bool WantsTextInput() const override;
    [[nodiscard]] bool HoldsTheLastFrame() const override;
    // El error de 0x5fb570 en el dibujo (ADR 0009), con la dirección; vacío mientras no corte.
    [[nodiscard]] std::optional<std::string> Failure() const override;
    [[nodiscard]] const ActorState& PreviewActor() const;

private:
    // Un personaje de la lista en su lugar, con la entrada de la que salió.
    struct Character
    {
        std::uint8_t slot = 0;
        std::uint64_t revision = 0;
        Protocol::Character::CharacterEntry entry;
        GameLogic::CharacterAppearance appearance;
        CharacterAnimation animation;
        // Su tela (0x50ba30 la arma desde el primer frame en que se dibuja).
        CharacterCloth cloth;
    };

    // Si la escena todavía espera la lista: con sesión, hasta ChoosingCharacter.
    [[nodiscard]] bool WaitingForTheList() const;
    // La lista de la sesión a los personajes: la primera vez, todos, y el +0x543 de la barra; después,
    // cada construcción recibida recrea su visual, aunque tenga los mismos bytes que el anterior.
    void SyncTheCharacters();
    [[nodiscard]] Character MakeCharacter(const Protocol::Character::CharacterEntry& entry) const;
    [[nodiscard]] bool PrepareTheCharacters();
    // 0x63420a: con alguno de los dos números de pkt[5] de la lista, el 1240 en el área de mensajes.
    [[nodiscard]] Core::Expected<void, std::string> AddTheAccountNotice();
    [[nodiscard]] CharacterStepContext CharacterContext();
    void StepCharacters();
    void StepTheScene(const Input::InputState& input);
    // Si abrió el aviso 0x70 en este paso.
    bool ShowTheFailure();
    // Con la respuesta del F3 01 (0x4c2290): con 1 se cierra el 0x5d; con 0 o 2, la ventana de crear
    // vuelve a abrirse con su aviso arriba. Si abrió uno en este paso.
    bool TakeTheCreationResult();
    // 0x63d880 con la ventana abierta y sin aviso arriba.
    void StepTheCreateWindow(const Input::InputState& input, UI::NewUiTurn turn);
    // 0x65f690: abre la ventana con lo suyo y la pasa arriba con el foco.
    void OpenTheCreateWindow();
    void OpenNotice(UI::NoticeId notice, std::string_view argument = {});
    void OpenTheEscapeWindow();
    // 0x65fc30 al empezar el paso, como 0x637700 antes de 0x6342c0: cada ventana con su turno.
    void StepTheWindows(const Input::InputState& input);
    // 0x647d40: lo que pasó en la de Esc.
    void OnEscapeEvent(const UI::EscapeWindowEvent& event);
    // 0x647ef0: el 380 con lo que falta, a los mensajes.
    void AddTheCountdown(int seconds);
    // El 1 de la de Esc (0x647e08): el C3 F1 02 02 con sus dos rand() y se cierran ella y la barra.
    void JoinAnotherServer();
    // 0x63d990: los chequeos del nombre, y el F3 01 y el 0x5d, o el aviso del nombre.
    void SubmitTheCreation();
    // 0x63e430 al final del paso de la ventana, que solo corre con ella arriba: la clase de la ventana
    // y el avance adicional: con un aviso arriba solo avanza desde 0x508ea0.
    void StepThePreview();
    void DrawTheCreateWindow(SceneCanvas& canvas, float alpha);
    // 0x505820 con un personaje que se dibuja, `frames` veces (Scenes/FullSetScan.h), sobre WorldState: lo que
    // devuelve el último, que lee el conjunto completo de la cola de efectos.
    FullSetScan ScanTheCharacter(const GameLogic::CharacterAppearance& appearance, int frames);
    // 0x635ae0 (0x6346ca) y 0x5fbb00 (0x63475c) en cada frame del original que cuenta el reloj, uno detrás
    // del otro; un dibujo intermedio solo consulta la cámara. Con un error del trapecio deja m_failure.
    [[nodiscard]] Render::Camera3D StepTheCameraAndTheTrapezoid(const World::GameCameraInput& input,
                                                                Core::Duration clock);
    // Si el mundo 54 corre: con WorldState, la tela, la selección inicializada y el mapa 54 en curso.
    [[nodiscard]] bool HasTheWorld() const;
    // La altura del terreno 54 (0x5f93c0) para la mariposa y las partículas; vacía sin terreno.
    [[nodiscard]] std::function<double(float, float)> TerrainHeight();
    // 0x634680 desde 0x6347e9 hasta 0x6213f0: la luz del elegido, el mundo, los personajes, la cola, las dos
    // 0x729 del elegido y el pase de ojo. Sin el mundo, solo los personajes.
    void DrawTheWorldAndTheCharacters(SceneCanvas& canvas, float alpha, const Render::Pass3D& pass,
                                      const Render::Camera3D& camera, std::uint8_t map, FaunaDrawCarry* poseCarry,
                                      int scanFrames);
    void DrawTheCharacters(SceneCanvas& canvas, float alpha, std::uint8_t map, FaunaDrawCarry* poseCarry,
                           int scanFrames);
    // Si Player.bmd tiene su esqueleto con acciones: sin ellas, 0x50ba30 sale antes (0x50ba87).
    [[nodiscard]] bool HasTheSkeleton() const;
    // 0x50db57..0x510a7a con un personaje de la lista, después de su cuerpo; un error queda en m_failure.
    void DrawTheTail(SceneCanvas& canvas, ActorState& actor, std::size_t slot, const DrawnCharacter& drawn,
                     FaunaDrawCarry& poseCarry, const FullSetScan& scan, bool originalFrame);
    // El cursor del juego (0x5ac2d0) al final de la UI, con el aviso de antes de la lista también.
    void DrawTheCursor(SceneCanvas& canvas) const;
    // 0x634949..0x63495f: los avisos y el área vieja, antes de la UI.
    void DrawTheMessages(SceneCanvas& canvas);
    // Con la respuesta del F3 02, su aviso en lugar del 0x5d (0x4e2fe9), salvo que la sesión haya
    // caído. Si abrió uno en este paso.
    bool TakeTheDeletionResult();
    void StepTheNotice(const Input::InputState& input, UI::NewUiTurn turn);
    // 0x63eb40: crear con un lugar libre de los cinco; conectar y borrar con uno elegido.
    void EnableTheBar();
    void OnTheBar(UI::SelectionBarClick click);
    // 0x63edf0: las revisiones del borrado con el elegido, y su aviso.
    void AskToDelete();
    // 0x643740: el F3 02 con el código del aviso 0x5f, y el 0x5d.
    void SendTheDeletion();
    // El lugar del personaje bajo el mouse, con el rayo del último pase dibujado.
    [[nodiscard]] std::optional<std::uint8_t> UnderTheMouse(const Input::InputState& input) const;
    void Choose(std::optional<std::uint8_t> slot);
    // Enter con uno elegido: el aviso 0x84 si está bloqueado, o el mundo.
    void TryToEnter();
    [[nodiscard]] Render::Models::ObjectTransform ObjectOf(const Character& character) const;
    // El buffer de un slot de las ventanas o de la escena.
    [[nodiscard]] TextureBufferSize BufferSize(Render::TextureSlot slot) const;

    [[nodiscard]] World::GameCamera& CameraState();
    [[nodiscard]] World::GameCameraInput& CameraInputState();
    [[nodiscard]] CreatePreview& Preview();
    [[nodiscard]] const CreatePreview& Preview() const;

    SceneContext m_context;
    // Fallback explícito para tests sin contexto completo; main siempre aporta ambos globales.
    World::GameCamera m_localCamera;
    World::GameCameraInput m_localCameraInput;
    Render::Models::OriginalFrameCarry m_cameraFrames;
    // Los frames del original de 0x505820 desde el dibujo (0x50c91e), como los de la cámara.
    Render::Models::OriginalFrameCarry m_fullSetScanFrames;
    Render::Models::OriginalFrameCarry m_previewDrawFrames;
    // Los frames del original del mundo 54 desde el dibujo: con cero, el dibujo va sobre copias.
    Render::Models::OriginalFrameCarry m_worldFrames;
    // Los de los avisos y el área vieja (0x554810 y 0x5554e0), lo mismo.
    Render::Models::OriginalFrameCarry m_messageFrames;
    Render::Camera3D m_previewCamera;
    const Data::Text::TextTable* m_texts;
    const Data::Tables::ItemTable* m_items;
    const IModelLibrary& m_models;
    App::ServerSession* m_session;
    SceneTextures m_textures;
    SceneTextures m_windowTextures;
    // El mundo 54 (P-C50): los tiles, los modelos de Object55, los sprites y las copias de los dibujos
    // intermedios.
    SelectionWorld m_world;
    // El trapecio y el rango del último frame, los del pase del terreno y los objetos.
    World::TerrainPolygon m_polygon{};
    World::TerrainRange m_terrainRange{};
    UI::NoticeWindow m_notice;
    UI::SelectionBar m_bar;
    // Las palabras de FilterName.bmd y Filter.bmd de `data`, y los textos 457 y 458.
    UI::NameFilters m_nameFilters;
    UI::CreateCharacterWindow m_create;
    UI::EscapeWindow m_escape;
    // El administrador de la UI nueva (CL8, hito 8): el orden, el foco y el mouse de las ventanas.
    UI::NewUi m_ui;
    // Si ya se abrió la barra con la lista (0x65f4b0).
    bool m_openedTheBar = false;
    const Render::Models::ModelTextureSlots* m_gameSlots;
    SelectionModels m_selectionModels;
    // Adaptador sin SelectionState: main conserva el NewFace en el estado durable de selección.
    CreatePreview m_preview;
    bool m_previewAdvanced = false;
    bool m_charactersPrepared = false;
    Input::DoubleClick m_doubleClick;
    std::vector<Character> m_characters;
    bool m_tookTheList = false;
    // Desde que se mandó el F3 02 (o el F3 01) hasta su respuesta.
    bool m_waitingForTheDeletion = false;
    bool m_waitingForTheCreation = false;
    std::optional<Render::Pass3D> m_lastPass;
    // El que estaba bajo el mouse en el último paso, que lleva el contorno: como el DAT_006c8e30
    // que 0x59ec80 deja para el frame siguiente.
    std::optional<std::uint8_t> m_hovered;
    App::PixelSize m_screen{640, 480};
    // Los pasos desde que se entró: el reloj del doble clic.
    std::uint64_t m_steps = 0;
    std::optional<SceneId> m_next;
    bool m_failureShown = false;
    bool m_quit = false;
    std::optional<std::string> m_failure;
};
} // namespace Client::Scenes
