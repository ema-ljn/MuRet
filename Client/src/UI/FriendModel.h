#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

// La lista de amigos del juego (CL9 hito 9; 06 § HUD y ventanas, "Los amigos"): el objeto de [0x5763518], que llenan
// los paquetes C0..C4 (0x4dc010..0x4dc720) y que la pestaña de amigos copia a su lista (0x49bd60). Es un std::deque de
// la STL de MSVC6 con elementos de 0x14 bytes y un iterador de trabajo (+0x34) que MuRet no necesita. El std::deque
// de MuRet reemplaza la instancia del original sobre FriendEntry, la del modelo y la de la pestaña (push_back,
// pop_front, sus iteradores y el mapa del deque): 0x472d30, 0x4733c0, 0x473830, 0x473860, 0x473a20, 0x476b20,
// 0x476c70, 0x476cf0, 0x476d10, 0x476d80, 0x4784d0, 0x47a770, 0x4874e0, 0x487560, 0x4875f0, 0x4a76b0, 0x4a7bd0 y
// 0x4a7d20.
namespace Client::UI
{
// El nombre de un amigo: strncpy de 10 con el '\0' en +0xe (0x49b220).
inline constexpr std::size_t FriendNameLength = 10;
// 0x49c260: por nombre (0x49c190), por estado (0x49c240) o con el que estaba (−1).
inline constexpr std::int32_t ByNameSort = 0;
inline constexpr std::int32_t ByStateSort = 1;
inline constexpr std::int32_t SameSort = -1;
// El estado de C4 con todos fuera de línea (0x4dc72b): el servidor de amigos se cayó.
inline constexpr std::uint8_t AllOfflineState = 0xfc;

// Un amigo, 0x14 bytes.
struct FriendEntry
{
    // +0: lo da vuelta el clic sobre su renglón (0x461f00). En el modelo queda en 0: 0x49b220 copia la plantilla de
    // 0x5762ef8, que nadie escribe en +0 (*estático*).
    std::int32_t selected = 0;
    // +4..+0xe.
    std::string name;
    // +0xf: C0, C1 y C4 lo escriben en 0; nadie lo lee en el hito 9.
    std::uint8_t flag = 0;
    // +0x10: el estado (el número del servidor; 0xfc..0xff fuera de línea).
    std::uint8_t state = 0;

    friend bool operator==(const FriendEntry&, const FriendEntry&) = default;
};

// [0x5763518].
struct FriendModel
{
    // +0: el orden del último 0x49c260.
    std::int32_t sortMode = ByNameSort;
    // +8..+0x30.
    std::deque<FriendEntry> entries;
};

// strncpy de 10 hasta el primer '\0'.
[[nodiscard]] std::string FriendName(std::string_view name);

// strncmp(a, b, 10) == 0.
[[nodiscard]] bool SameFriendName(std::string_view a, std::string_view b);

// 0x49c190 (con 0x49c220): hasta el largo del más corto, en el primer char distinto (con signo) va antes el más grande;
// uno que empieza como el otro no va antes. Deja los nombres de la Z a la A.
[[nodiscard]] bool NameComesFirst(const FriendEntry& a, const FriendEntry& b);

// 0x49c240: el estado más alto (sin signo) primero.
[[nodiscard]] bool StateComesFirst(const FriendEntry& a, const FriendEntry& b);

// 0x49bac0: la vacía.
void ClearTheFriends(FriendModel& model);

// 0x49b220: insert(end()) con el nombre, la bandera y el estado (en una vacía, push_front; si no, push_back).
void AddAFriend(FriendModel& model, std::string_view name, std::uint8_t flag, std::uint8_t state);

// 0x49b690: borra el primero con ese nombre (strncmp de 10), si hay.
void RemoveAFriend(FriendModel& model, std::string_view name);

// 0x49c010: la bandera y el estado del primero con ese nombre, si hay.
void SetAFriend(FriendModel& model, std::string_view name, std::uint8_t flag, std::uint8_t state);

// 0x49c0e0: la bandera y el estado de todos.
void SetAllTheFriends(FriendModel& model, std::uint8_t flag, std::uint8_t state);

// 0x49c260: con un modo que no es −1 lo guarda, y ordena con el sort de MSVC6 por nombre (0) o por estado (1); con
// otro modo guardado, nada.
void SortTheFriends(FriendModel& model, std::int32_t mode);
} // namespace Client::UI
