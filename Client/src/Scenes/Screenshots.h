#pragma once

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Core/LocalTime.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Client::Input
{
class InputState;
}

namespace Client::Render::Text
{
class ITextMeasure;
}

namespace Client::Data::Text
{
class TextTable;
}

namespace Client::UI
{
struct GameUiState;
}

// La captura de pantalla de Impr Pant (CL8 hito 6; 06 § La captura de pantalla): lo que 0x637700 hace en todas las
// escenas, detrás del paso (0x637cff) y alrededor del dibujo (0x6381bb..0x6385da). No saca rand(). *Estático*.
namespace Client::Scenes
{
class WorldState;
struct SceneCanvas;

// 0x6202e0: el glReadPixels de la captura, del lado de GL (Render/Gl/GlScreenReader.h).
class IScreenReader
{
public:
    virtual ~IScreenReader() = default;
    // glReadPixels(0, 0, ancho, alto, GL_RGB, GL_UNSIGNED_BYTE) de lo dibujado en el frame: `screen.height` filas de
    // `screen.width` × 3 bytes, desde la de abajo y sin relleno entre filas (Q-C631).
    [[nodiscard]] virtual std::vector<std::uint8_t> ReadTheScreen(App::PixelSize screen) = 0;
};

// Los globales de la captura, en la .bss: los leen y escriben solo 0x637700 y 0x6202e0.
struct ScreenshotState
{
    // 0x7d368a8: la captura pedida. La invierte Impr Pant (0x637d10..0x637d25) y la apaga el frame (0x6385da).
    bool requested = false;
    // 0x7d368ac: el número del nombre, de 0 a 9.999. Lo sube 0x6202e0 (0x620345..0x62035a).
    std::int32_t counter = 0;
    // 0x7d368b0: 0x6202e0 lo pone en 1 (0x6202ed) y nadie lo lee.
    bool taken = false;
};

// Lo que el frame de la captura lee del resto del cliente.
struct ScreenshotFrameContext
{
    // El chat del mensaje (0x555190) y el nombre del héroe (+0x1d9 de 0x743a170).
    WorldState& world;
    // El DC de los textos y la pantalla en píxeles, para medir el mensaje (06 § El chat).
    const Render::Text::ITextMeasure& measure;
    App::PixelSize screen;
    // GetAsyncKeyState(0x10) en 0x638303: Shift abajo.
    bool shiftDown = false;
    // GetLocalTime (0x6381c7). Se lee solo con la captura pedida (ScreenshotFileName).
    std::function<Core::Expected<Core::LocalTime, std::string>()> localTime;
    // Text.bmd, con el 459. Sin tabla, el texto vacío.
    const Data::Text::TextTable* texts = nullptr;
    // El código del servidor elegido (ServerSession::ChosenServer), del que sale el nombre de su grupo (Q-C632).
    std::optional<std::uint16_t> chosenServer;
    // Adónde va el archivo. El original lo abre con el nombre solo, en la carpeta de trabajo: Main pasa la vacía.
    std::filesystem::path folder;
};

// 0x6381c7..0x638211: sprintf("Screen(%02d_%02d-%02d_%02d)-%04d.jpg") con el mes, el día, la hora, el minuto y el
// contador.
[[nodiscard]] std::string ScreenshotFileName(const Core::LocalTime& time, std::int32_t counter);

// 0x638219..0x6382f7: sprintf con el texto 459 de formato y el nombre, y detrás, con strcat, el wsprintfA de
// " [%s / %s]" con el nombre del grupo y el del héroe.
[[nodiscard]] std::string ScreenshotMessage(std::string_view format, std::string_view fileName, std::string_view group,
                                            std::string_view hero);

// La captura entre el paso y el frame: el pedido, lo que el frame arma antes de dibujar para después, y el corte de
// un frame retenido con la captura pedida.
class Screenshots
{
public:
    // Con los globales en cero, como en la .bss; los tests pueden empezar con otros.
    Screenshots() = default;
    explicit Screenshots(const ScreenshotState& state);

    // 0x637cfd..0x637d25, detrás del paso de cualquier escena: con el flanco de Impr Pant (0x553520(0x2c), con su
    // latch en ui.keyLatches), invierte el pedido. Un apretón que llega con su soltada en el mismo frame también es
    // el flanco (Q-C630). Si un frame retenido tenía la captura pedida, su error (FinishTheLoop).
    [[nodiscard]] Core::Expected<void, std::string> StepTheKey(UI::GameUiState& ui, const Input::InputState& input);

    // 0x6381bb..0x63835a, antes del dibujo de la escena: con la captura pedida, el nombre y el mensaje, y sin Shift,
    // el mensaje en el chat (0x555190 con el 0x7d3a190 vacío y el tipo 1), que sale en la captura. El original los
    // arma en cada frame; nadie más los lee (0x7d36724 solo lo leen 0x637700 y 0x6202e0), así que MuRet los arma
    // solo con la captura pedida. Si un frame retenido tenía la captura pedida, su error.
    [[nodiscard]] Core::Expected<void, std::string> BeforeTheDraw(const ScreenshotFrameContext& context);

    // 0x638586..0x6385da, después del dibujo y antes del swap: con la captura pedida, lo que juntó el lienzo a GL,
    // la lectura (0x6202e0), el JPEG con calidad 100 (0x63b150, Data::Textures::WriteJpegFile), el contador, y con
    // Shift, el mensaje (con el 0x7d3a194 vacío), que ya no sale; el pedido, apagado. Un archivo que no abre no es un
    // error (el original no lo mira); que libjpeg corte, sí.
    [[nodiscard]] Core::Expected<void, std::string> AfterTheDraw(const ScreenshotFrameContext& context,
                                                                 SceneCanvas& canvas, IScreenReader& reader);

    // El fin de una vuelta del bucle sin minimizar (SceneManager::FinishTheLoop). Si no hubo dibujo y la captura está
    // pedida, el original la toma igual de un frame que no presenta (0x6385aa con el 0 del dibujo de la escena,
    // 0x6385e1): MuRet no arma ese frame y corta en el próximo StepTheKey o BeforeTheDraw (P-C110). Mientras conecta
    // (`connecting`), el original no corre ningún frame (Q-C85): el pedido espera al próximo dibujo.
    void FinishTheLoop(bool connecting);

    [[nodiscard]] const ScreenshotState& State() const;

private:
    // Lo que BeforeTheDraw armó para AfterTheDraw: el nombre (0x7d36724), el mensaje (ebp − 0x14c) y Shift
    // (ebp − 0x164).
    struct Pending
    {
        std::string fileName;
        std::string message;
        bool shiftDown = false;
    };

    ScreenshotState m_state;
    std::optional<Pending> m_pending;
    // Si BeforeTheDraw corrió desde el último FinishTheLoop.
    bool m_drawn = false;
    // El corte de un frame retenido con la captura pedida.
    std::optional<std::string> m_heldFrame;
};
} // namespace Client::Scenes
