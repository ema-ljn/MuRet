#pragma once

#include <chrono>

// El reloj del bucle (CL3.2), como interfaz para inyectarlo: Main le pasa el de SDL
// (Platform/SdlClock) y los tests, uno falso que avanza lo que el test dice
// (tests/support/FakeClock).
namespace Client::Core
{
// La unidad de tiempo del bucle: nanosegundos enteros, sin redondeos de float.
using Duration = std::chrono::nanoseconds;

// Now() es monótono en el real; el falso puede ir para atrás a propósito.
class IClock
{
public:
    virtual ~IClock() = default;
    virtual Duration Now() = 0;
    virtual void SleepFor(Duration duration) = 0;
};
} // namespace Client::Core
