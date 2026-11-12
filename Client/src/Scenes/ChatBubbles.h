#pragma once

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Render/Text/IGlyphRasterizer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// Los globos sobre las cabezas (CL8 hito 3; 06 § El chat, "Los globos"): la tabla de la .bss en 0x77e1be0, 100
// entradas de 0x294 bytes, que llenan los mensajes que llegan (0x557170, por el 00 y el 01, y 0x557560 por nombre), que
// el paso del juego envejece (0x557650, 0x6370f9) y que el HUD dibuja (0x5a9050, el 12). El hover de los personajes
// del hito 6 usa la misma tabla. Los textos se guardan hasta su '\0', como los leen strcpy y strlen.
namespace Client::Scenes
{
class WorldState;
struct ActorState;

inline constexpr std::size_t ChatBubbleCount = 100;
// 0x557170: con 20 bytes o más, el texto va en dos renglones (0x5549a0).
inline constexpr std::size_t ChatBubbleSplitBytes = 20;
// 0x557170: cuánto dura un renglón del modo 0 (2 · largo + 0xa0 pasos) y uno del 1 (1.000); el nombre solo, con el
// texto vacío, 100 pasos en una entrada nueva y 10 en la del mismo personaje.
inline constexpr std::int32_t ChatBubbleLogSteps = 1000;
inline constexpr std::int32_t ChatBubbleBaseSteps = 0xa0;
inline constexpr std::int32_t ChatBubbleNewNameSteps = 100;
inline constexpr std::int32_t ChatBubbleNameSteps = 10;

struct ChatBubble
{
    // +0: el nombre, strcpy del de quien habla.
    std::string name;
    // +0x18 y +0x36: el gremio y el puesto, que escribe 0x556dd0 con la tabla de los gremios (CL9.4). Sin gremio,
    // vacíos.
    std::string guild;
    std::string guildPost;
    // +0x54: el título de la tienda personal (0x7245c8, CL9.4). Sin tienda, vacío.
    std::string shopTitle;
    // +0x64: el color, el PK de quien habla (+0x375) o 0 con +0x88 en 4.
    std::uint8_t color = 0;
    // +0x65: con gremio, el +0x374 de quien habla; sin él, 0.
    std::uint8_t guildByte = 0;
    // +0x68: cuántos pasos se ve el nombre solo.
    std::int32_t nameTime = 0;
    // +0x6c y +0x16c: los renglones, el de abajo y el de arriba, de 0x100 bytes.
    std::string line1;
    std::string line2;
    // +0x26c y +0x270: cuántos pasos se ve cada uno.
    std::int32_t time1 = 0;
    std::int32_t time2 = 0;
    // +0x274: de quién es, como lugar del pool. Vacío, el NULL de la .bss.
    std::optional<std::size_t> owner{};
    // +0x278, +0x27c, +0x280 y +0x284: dónde quedó y cuánto mide en el último dibujo (0x5a9050, 0x555840).
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
};

using ChatBubbleTable = std::array<ChatBubble, ChatBubbleCount>;

// +0x1d9 como lo leen strcmp y strcpy: con los diez bytes llenos sigue +0x1e3. Lo que sigue a +0x1e3 MuRet no lo tiene
// y lo toma en 0.
[[nodiscard]] std::string ChatNameOf(const ActorState& actor);

// El modo de 0x557170: 0 el chat (el globo dura según el largo) y 1 también al registro del chat (1.000 pasos).
// Ningún llamador de MuRet pasa otro: el original toma de duración un puntero (el texto o el personaje).
enum class ChatBubbleMode : std::uint8_t
{
    Chat = 0,
    Log = 1,
};

// Lo que mide el registro del chat en el modo 1 (0x45bb20).
struct ChatBubbleMeasure
{
    const Render::Text::ITextMeasure& measure;
    App::PixelSize screen{};
};

// 0x557170(nombre, texto, personaje, modo, color): con el personaje vivo y a la vista (+0x16c), la entrada que ya es
// suya o la primera libre (el nombre y los dos renglones sin tiempo); sin ninguna, nada. Copia el nombre, el color (sin
// él, el PK, o 0 con +0x88 en 4) y el gremio y la tienda (0x556dd0). Con el texto vacío solo prende el nombre. En la
// entrada suya, el renglón vivo sube al de arriba. Con 20 bytes o más, el texto se corta en dos (0x5549a0): la cabeza
// arriba y la cola abajo, con el mismo tiempo. El modo 1 lo agrega también al registro (0x45bb20 con el tipo 3) y el
// sonido que pida queda en WorldRuntime::lastSoundId. Un personaje con gremio corta: la tabla es de la CL9.4.
[[nodiscard]] Core::Expected<void, std::string> AddChatBubble(WorldState& world, const ChatBubbleMeasure& measure,
                                                              std::string_view name, std::string_view text,
                                                              std::size_t owner, ChatBubbleMode mode,
                                                              std::optional<std::uint8_t> color = std::nullopt);

// 0x557560(nombre, texto, modo): el primer personaje vivo con +0x88 en 1 y ese nombre (strcmp de +0x1d9) y, si no
// hay, el primero con +0x88 en 2; con uno, 0x557170 sin color. Sin ninguno, nada.
[[nodiscard]] Core::Expected<void, std::string> AddChatBubbleByName(WorldState& world, const ChatBubbleMeasure& measure,
                                                                    std::string_view name, std::string_view text,
                                                                    ChatBubbleMode mode);

// Lo que lee 0x557650 del paso.
struct ChatBubbleStepInput
{
    // 0x7d3682c y 0x7d36828: el mouse en 640 × 480.
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    App::PixelSize screen{};
};

// 0x557650, en cada paso del juego (0x6370f9, después de los ayudantes y antes de MoveHero, también en la espera):
// baja los tres relojes de cada entrada y los pone en 0 si su personaje murió o no se ve; las tiendas (0x451720,
// 0x4516d0) no tienen ninguna. El clic derecho sobre un globo ajeno con el campo del chat abierto en la zona segura
// llena el destino del susurro (0x7798cf8), que es de escribir: corta.
[[nodiscard]] Core::Expected<void, std::string> StepTheChatBubbles(WorldState& world, const ChatBubbleStepInput& input);
} // namespace Client::Scenes
