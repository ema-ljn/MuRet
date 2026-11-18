#pragma once

#include <cstdint>

// La tabla de protección del original (0x57630c8; 06 § La tabla de protección, Q-C125 y Q-C183), sin
// portar el cifrado: el valor lógico vive en claro en su dueño, como la variable viva del original, y
// de la entrada queda solo el byte de cuántos accesos la tienen abierta. Un LogicalAccess es uno de
// esos accesos: abrir le suma 1 al byte y no gasta; cerrar le resta 1 y, si lo deja en 0, saca del
// rand() de la CRT los que el original gasta al volver a cifrar la entrada, uno por byte. El byte
// envuelve (255 + 1 = 0) y gasta el cierre que lo deja en 0, aunque no sea el de más afuera.
// No hace el alta ni la búsqueda de la entrada (0x40ad90, 0x40b240), ni deja en la variable los
// rand() & 0xff del último cierre (Q-C125), salvo que el dueño pida esos bytes (`left`, hasta 4). La
// cuenta con la que arranca cada entrada la pone el dueño del byte: la del original no está leída
// (CL7.P, P-C22 c).
namespace Client::Render::Models
{
class CrtRandom;

// 0x412890, y en línea en 0x40b570 y 0x40b800: una variable de 4 bytes, un rand() por byte (Q-C125).
inline constexpr std::uint16_t SmallLogicalCloseRandoms = 4;
// 0x647f60: el serial de los C3 (0x5767913), de un byte, un rand() (CL8, hito 8).
inline constexpr std::uint16_t SerialCloseRandoms = 1;
// 0x40b4d0: el registro propio de 0x7681eb4, de 0xf08 bytes, un rand() por byte: 3.848 (Q-C183).
inline constexpr std::uint16_t OwnRecordCloseRandoms = 0xf08;

class LogicalAccess
{
public:
    // Abre el acceso. depth y random viven más que él; closeRandoms es lo que gasta la entrada al
    // volver a cifrarse (SmallLogicalCloseRandoms u OwnRecordCloseRandoms). Un temporal suelto
    // abriría y cerraría en la misma sentencia: nodiscard lo avisa.
    [[nodiscard]] LogicalAccess(std::uint8_t& depth, CrtRandom& random, std::uint16_t closeRandoms);
    // Lo mismo, y el cierre que deja la cuenta en 0 escribe en `left` lo que 0x412890 deja en la
    // variable (0x4128e2..0x4128f9): el rand() & 0xff de cada byte, el primero abajo. Con otra
    // cuenta, `left` queda como estaba (la variable sigue en claro). Solo con closeRandoms de 1 a 4.
    [[nodiscard]] LogicalAccess(std::uint8_t& depth, CrtRandom& random, std::uint16_t closeRandoms,
                                std::uint32_t& left);
    LogicalAccess(const LogicalAccess&) = delete;
    LogicalAccess& operator=(const LogicalAccess&) = delete;
    // Cierra el acceso.
    ~LogicalAccess();

private:
    std::uint8_t& m_depth;
    CrtRandom& m_random;
    std::uint16_t m_closeRandoms;
    std::uint32_t* m_left = nullptr;
};
} // namespace Client::Render::Models
