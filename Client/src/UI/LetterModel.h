#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Las cartas y las salas del chat con un amigo (CL9 hito 10; 06 § HUD y ventanas, "Las cartas y el chat con un
// amigo"): el objeto de [0x576351c], que llenan el C6 (0x49eb60) y el C8 (0x49ee20) y que la pestaña de las cartas
// copia a su lista (0x4a0960); y el de [0x5763520], las salas, que arma 0x49de50 al llegar un CA. Los std::deque y
// std::map de la STL de MSVC6 son los de MuRet, con el mismo orden.
namespace Client::UI
{
// 0x49f870: por el tipo (0), el que la manda (1), la fecha y la hora (2) o el asunto (3); −1, el que estaba.
inline constexpr std::int32_t ByTypeLetters = 0;
inline constexpr std::int32_t BySenderLetters = 1;
inline constexpr std::int32_t ByDateLetters = 2;
inline constexpr std::int32_t BySubjectLetters = 3;
inline constexpr std::int32_t SameLetters = -1;
// Los largos de los campos de una carta (0x49eb60: strncpy de 10, 0x20, 0x10 y 0x10).
inline constexpr std::size_t LetterNameLength = 10;
inline constexpr std::size_t LetterSubjectLength = 0x20;
inline constexpr std::size_t LetterStampLength = 0x10;

// Una carta, 0x138 bytes.
struct LetterEntry
{
    // +0: lo da vuelta el clic sobre su renglón; en el modelo queda en 0 (la plantilla de 0x5762f18, *estático*).
    std::int32_t selected = 0;
    // +4: el número del servidor.
    std::uint16_t index = 0;
    // +8, +0x13, +0x113 y +0x123.
    std::string name;
    std::string subject;
    std::string date;
    std::string time;
    // +0x134: 0 sin leer... como la marca el servidor (el C6), y 1 después de abrirla (0x4dcb70).
    std::int32_t type = 0;

    friend bool operator==(const LetterEntry&, const LetterEntry&) = default;
};

// [0x576351c].
struct LetterModel
{
    // +0: el orden del último 0x49f870.
    std::int32_t sortMode = ByTypeLetters;
    // +8..+0x30.
    std::deque<LetterEntry> letters;
    // +0x44: el C7 de cada carta ya leída (0x4a03c0), 0x404 bytes desde pkt[0].
    std::map<std::uint16_t, std::vector<std::byte>> copies;
};

// 0x49c190: hasta el largo del más corto, en el primer char distinto (con signo) da si `a` es el más grande. Con el
// principio igual pone `same` en 1 (si lo hay) y da que no.
[[nodiscard]] bool TextComesAfter(std::string_view a, std::string_view b, bool* same = nullptr);

// 0x49eb60: si ya hay una con el número, nada; si no, push_back con los textos cortados.
void AddALetter(LetterModel& model, std::uint16_t index, std::string_view name, std::string_view subject,
                std::string_view date, std::string_view time, std::int32_t type);

// 0x49ee20: borra la primera con el número, si hay.
void RemoveALetter(LetterModel& model, std::uint16_t index);

// 0x4a01a0: la primera con el número, o ninguna.
[[nodiscard]] LetterEntry* FindALetter(LetterModel& model, std::uint16_t index);

// 0x49f870: con un modo que no es −1 lo guarda, y ordena con el sort de MSVC6 según el guardado.
void SortTheLetters(LetterModel& model, std::int32_t mode);

// 0x4a03c0: guarda la copia si no había una para el número (std::map::insert).
void KeepTheLetter(LetterModel& model, std::uint16_t index, std::vector<std::byte> copy);
// 0x4a04a0: la copia guardada, o ninguna.
[[nodiscard]] const std::vector<std::byte>* KeptLetter(const LetterModel& model, std::uint16_t index);
// 0x4a0500.
void ForgetTheLetter(LetterModel& model, std::uint16_t index);
// 0x49f260 (el primer paso del juego): sin cartas (0x49f2c0..) y sin copias (0x4a0560).
void ClearTheLetters(LetterModel& model);

// [0x5763520], las salas del chat con un amigo.
inline constexpr std::size_t ChatSlots = 0x100;
// 0x49de50: el mensaje de la conexión de la sala es 0x500 más su lugar.
inline constexpr std::uint32_t ChatSocketMessage = 0x500;
struct ChatRoom
{
    // +0 y +0x4028 del objeto de 0x402c bytes: la sala y la ventana del chat.
    std::uint32_t room = 0;
    std::int32_t window = 0;
};
struct ChatRoomOrder
{
    enum class Kind : std::uint8_t
    {
        // C1 04 con `text` (0x4944c0 con el 7).
        Message,
        // C1 03 01 (0x493a70) y el cierre de la conexión (0x49dfd0).
        Leave,
        // C1 03 05 (0x4a6550).
        Ping,
    };
    Kind kind = Kind::Message;
    std::uint32_t room = 0;
    std::string text;
    friend bool operator==(const ChatRoomOrder&, const ChatRoomOrder&) = default;
};
struct ChatRooms
{
    // +0: dónde empieza a buscar 0x49e280; +1..+0x100, los lugares ocupados.
    std::uint8_t cursor = 0;
    std::array<std::uint8_t, ChatSlots> used{};
    // +0x108: cada lugar con su sala.
    std::map<std::uint32_t, std::uint32_t> slots;
    // +0x118: cada sala con su conexión (la de la sesión, App::IChatRoomLinks), desde que 0x49de50 la abre.
    std::map<std::uint32_t, ChatRoom> rooms;
    // Lo que las ventanas le mandan a la conexión de su sala (el mensaje de 0x4944c0 con el 7, la salida de 0x493a70
    // con el cierre de 0x49dfd0 y el de 0x4a6550), en orden: UI no ve la sesión, y Scenes lo saca
    // (Scenes::FlushTheChatRoomOrders).
    std::deque<ChatRoomOrder> orders;
};

// 0x49e280: desde el cursor, el primer lugar libre (corre el cursor con vuelta en 0xff); lo marca y lo asocia a la
// sala. Después de 0x101 ocupados seguidos, ninguno: quien llama escribe "max chat count over".
[[nodiscard]] std::optional<std::uint8_t> TakeAChatSlot(ChatRooms& rooms, std::uint32_t room);

// 0x49e420: la conexión de la sala, o ninguna.
[[nodiscard]] const ChatRoom* FindTheChatRoom(const ChatRooms& rooms, std::uint32_t room);

// 0x49dfd0: sin la sala, nada; con ella, la saca y libera el lugar que la tenga.
void DropTheChatRoom(ChatRooms& rooms, std::uint32_t room);
// 0x49de50 con el connect que falla: la sala no se anota y el lugar que tomó 0x49e280 queda tomado (Q-C1005).
void ForgetTheChatRoom(ChatRooms& rooms, std::uint32_t room);
} // namespace Client::UI
