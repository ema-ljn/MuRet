#include "Scenes/Screenshots.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Core/LocalTime.h"
#include "Data/Text/TextTable.h"
#include "Data/Textures/JpegWriter.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Scenes/GameMessages.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameText.h"
#include "UI/GameUiState.h"
#include "UI/ServerListModel.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Client::Scenes
{
namespace
{
// 0x637cfd: la tecla virtual de Impr Pant (VK_SNAPSHOT), que indexa su latch de 0x77f36d4.
constexpr std::uint8_t PrintScreenVirtualKey = 0x2c;
// 0x63821e: el texto de Text.bmd que da el formato del mensaje (0x76debc0 = 0x76bd1dc + 459 · 300).
constexpr std::size_t ScreenshotText = 459;
// 0x638347 y 0x6385c4: el tipo de 0x555190.
constexpr std::int32_t ScreenshotMessageType = 1;
// 0x620330: la calidad que 0x6202e0 le pasa a 0x63b150.
constexpr int ScreenshotQuality = 100;
// 0x62034b: el contador va de 0 a 9.999.
constexpr std::int32_t CounterModulus = 10000;

// 0x553520(0x2c): con la tecla abajo y el latch en 0, lo pone en 1 y da verdadero; con la tecla arriba, lo vuelve a 0
// (como KeyEdge de GameUi.cpp). La tecla abajo es también el apretón del frame que llegó con su soltada (Q-C630).
bool PrintScreenEdge(UI::GameUiState& ui, const Input::InputState& input)
{
    std::int32_t& latch = ui.keyLatches[PrintScreenVirtualKey];
    if (!input.IsDown(Input::Key::PrintScreen) && !input.WasPressed(Input::Key::PrintScreen))
    {
        latch = 0;
        return false;
    }
    if (latch != 0)
        return false;
    latch = 1;
    return true;
}

// 0x638232..0x6382ae: 0x7d36bb0 + 0x21e · 0x6d5ec4 (el lugar del grupo elegido, recortado entre 0 y 30), donde
// 0x4c1860 copió el nombre del grupo: el texto del grupo del servidor elegido. Sin servidor elegido, vacío (Q-C632).
std::string_view GroupNameOf(const Data::Text::TextTable* texts, std::optional<std::uint16_t> chosenServer)
{
    if (!chosenServer)
        return {};
    return UI::TextAt(texts, UI::GroupNameText(UI::GroupOf(*chosenServer))).value_or(std::string_view{});
}
} // namespace

std::string ScreenshotFileName(const Core::LocalTime& time, std::int32_t counter)
{
    return std::format("Screen({:02}_{:02}-{:02}_{:02})-{:04}.jpg", time.month, time.day, time.hour, time.minute,
                       counter);
}

std::string ScreenshotMessage(std::string_view format, std::string_view fileName, std::string_view group,
                              std::string_view hero)
{
    const std::array<std::string_view, 1> arguments{fileName};
    std::string message = UI::PlaceArguments(format, arguments);
    message += std::format(" [{} / {}]", group, hero);
    return message;
}

Screenshots::Screenshots(const ScreenshotState& state) : m_state(state) {}

Core::Expected<void, std::string> Screenshots::StepTheKey(UI::GameUiState& ui, const Input::InputState& input)
{
    if (m_heldFrame)
        return Core::Unexpected(*m_heldFrame);
    // 0x637d10..0x637d25: un segundo flanco antes del frame deshace el pedido (Q-C634).
    if (PrintScreenEdge(ui, input))
        m_state.requested = !m_state.requested;
    return {};
}

Core::Expected<void, std::string> Screenshots::BeforeTheDraw(const ScreenshotFrameContext& context)
{
    m_drawn = true;
    if (m_heldFrame)
        return Core::Unexpected(*m_heldFrame);
    m_pending.reset();
    if (!m_state.requested)
        return {};
    // 0x6381bb: 0x41ef00, el sonido 3D, es de la CL10.
    if (!context.localTime)
        return Core::Unexpected(std::string("0x6381c7: la captura necesita la hora local (GetLocalTime)"));
    const auto time = context.localTime();
    if (!time.HasValue())
        return Core::Unexpected(std::format("0x6381c7: {}", time.Error()));
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    const std::string_view format = UI::TextAt(context.texts, ScreenshotText).value_or(std::string_view{});
    std::string fileName = ScreenshotFileName(time.Value(), m_state.counter);
    std::string message = ScreenshotMessage(format, fileName, GroupNameOf(context.texts, context.chosenServer),
                                            HeroNameOf(world.Actors()[runtime.hero]));
    // 0x6382f9..0x63835a: sin Shift, el mensaje va antes del dibujo y sale en la captura.
    if (!context.shiftDown)
    {
        if (auto added = AddGameMessage(runtime, context.measure, context.screen, {}, message, ScreenshotMessageType);
            !added.HasValue())
            return added;
    }
    m_pending = Pending{.fileName = std::move(fileName), .message = std::move(message), .shiftDown = context.shiftDown};
    return {};
}

Core::Expected<void, std::string> Screenshots::AfterTheDraw(const ScreenshotFrameContext& context, SceneCanvas& canvas,
                                                            IScreenReader& reader)
{
    if (!m_state.requested || !m_pending)
        return {};
    const Pending pending = *std::exchange(m_pending, std::nullopt);
    // 0x638592..0x6385a7: con 0x7d366bc, el modo del editor que nadie prende, 0x517570 escribiría
    // Data2\MonsterSetBase2.txt.
    // 0x6385aa → 0x6202e0: lo dibujado, con lo que todavía junta el lienzo, como en el Present (el 3D antes del 2D).
    canvas.flushModels();
    canvas.flushSprites();
    m_state.taken = true;
    const std::vector<std::uint8_t> pixels = reader.ReadTheScreen(canvas.screen);
    const auto written = Data::Textures::WriteJpegFile(context.folder / pending.fileName, canvas.screen.width,
                                                       canvas.screen.height, pixels, ScreenshotQuality);
    // Un archivo que no abre (Data::Textures::JpegWrite::NotOpened) sigue igual, con el contador y el mensaje (Q-C633).
    if (!written.HasValue())
        return Core::Unexpected(written.Error());
    m_state.counter = (m_state.counter + 1) % CounterModulus;
    // 0x6385af..0x6385d7: con Shift, el mensaje va después de la lectura.
    if (pending.shiftDown)
    {
        if (auto added = AddGameMessage(context.world.Runtime(), context.measure, context.screen, {}, pending.message,
                                        ScreenshotMessageType);
            !added.HasValue())
            return added;
    }
    // 0x6385da.
    m_state.requested = false;
    return {};
}

void Screenshots::FinishTheLoop(bool connecting)
{
    const bool drawn = std::exchange(m_drawn, false);
    if (drawn || connecting || !m_state.requested || m_heldFrame)
        return;
    m_heldFrame = std::string("0x6385aa: la captura de un frame que la escena no presenta (0x6385e1), con el glClear y "
                              "lo que dibujó la escena antes de salir con 0, es de P-C110");
}

const ScreenshotState& Screenshots::State() const
{
    return m_state;
}
} // namespace Client::Scenes
