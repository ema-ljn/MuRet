#pragma once

#include <cstdint>

// Misión de la CL3.0 (docs/learning/ola-cl3.0-el-bucle-de-juego.md, actividad 6): el reloj de
// GetTickCount, que no avanza de a 1 ms sino de a un tick del reloj de Windows.
// Contrato: Client/docs/research/02-arranque-y-ciclo-de-vida.md, "El ritmo".
//
// El tiempo del modelo va en microsegundos enteros, para que 15,625 ms sea exacto.
namespace Labs
{
// El tick por defecto de Windows, el de la VM XP: 15,625 ms, 64 por segundo (02, "El ritmo").
inline constexpr std::int64_t XpTickUs = 15'625;

// El tick de otro HAL, 10 ms: con él la espera de 40 ms termina justo a los 40 (02).
inline constexpr std::int64_t TenMsTickUs = 10'000;

class TickGrid
{
public:
    // `tickUs`, en µs, tiene que ser mayor que 0: es el divisor de Ms.
    explicit TickGrid(std::int64_t tickUs);

    // Lo que leería GetTickCount en el microsegundo `nowUs` (desde 0): el último tick entero, en ms
    // truncados. Es un DWORD, como el del original, y da la vuelta a los 2^32 ms (49,7 días).
    [[nodiscard]] std::uint32_t Ms(std::int64_t nowUs) const;

    [[nodiscard]] std::int64_t TickUs() const;

private:
    std::int64_t m_tickUs;
};
} // namespace Labs
