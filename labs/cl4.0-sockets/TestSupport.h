#pragma once

#include "DrainQueue.h"
#include "LabSocket.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <deque>
#include <optional>
#include <ostream>
#include <span>
#include <thread>
#include <vector>

// Lo que comparten los tests del lab: cómo imprime GoogleTest un IoStatus, una espera con tope en
// tiempo real para lo que hace el sistema en el fondo (un accept, un rechazo), y un send que sigue
// un guion para la cola y el jefe.
namespace Labs
{
// Sin esto, GoogleTest muestra un IoStatus como un byte suelto. Va en el namespace del tipo, donde
// GoogleTest lo busca.
inline void PrintTo(IoStatus status, std::ostream* out)
{
    switch (status)
    {
    case IoStatus::Done:
        *out << "Done";
        return;
    case IoStatus::TryLater:
        *out << "TryLater";
        return;
    case IoStatus::InProgress:
        *out << "InProgress";
        return;
    case IoStatus::Refused:
        *out << "Refused";
        return;
    case IoStatus::PeerClosed:
        *out << "PeerClosed";
        return;
    case IoStatus::Failed:
        *out << "Failed";
        return;
    }
    *out << "IoStatus fuera del enum";
}
} // namespace Labs

namespace Labs::Tests
{
// Lo que el sistema hace en el fondo (terminar un connect, mandar un RST) no se puede apurar desde
// el test: se pregunta hasta que pasa o hasta `limit` de reloj real. Un rechazo en Windows tarda
// unos 2 s por los reintentos del connect, así que el tope es de 5.
inline constexpr std::chrono::seconds RealTimeLimit{5};
inline constexpr std::chrono::milliseconds PollInterval{1};

template <typename Condition> bool WaitUntil(Condition condition, std::chrono::seconds limit = RealTimeLimit)
{
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (!condition())
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }
        std::this_thread::sleep_for(PollInterval);
    }
    return true;
}

// Los bytes 00, 01, 02, ... hasta `count`, como el buffer del paso 5 del plan.
inline std::vector<std::byte> Sequence(std::size_t count, std::size_t first = 0)
{
    std::vector<std::byte> bytes;
    bytes.reserve(count);
    for (std::size_t value = first; value < first + count; ++value)
    {
        bytes.push_back(static_cast<std::byte>(value));
    }
    return bytes;
}

// Un send falso que sigue un guion: cada entrada dice cuánto toma esa llamada, como mucho lo
// pedido, o std::nullopt para "probá más tarde". Sin guion, toma todo. `failWith` hace fallar la
// próxima llamada con ese estado. Anota lo que tomó, en orden, como saldría al cable, y cuánto le
// pidió cada llamada.
struct ScriptedSend
{
    std::deque<std::optional<std::size_t>> script;
    std::optional<Labs::IoStatus> failWith;
    std::vector<std::byte> wire;
    std::vector<std::size_t> asked;

    Labs::IoResult operator()(std::span<const std::byte> bytes)
    {
        asked.push_back(bytes.size());
        if (failWith)
        {
            const Labs::IoStatus status = *failWith;
            failWith.reset();
            return {status, 0, 0};
        }
        std::size_t take = bytes.size();
        if (!script.empty())
        {
            const std::optional<std::size_t> next = script.front();
            script.pop_front();
            if (!next)
            {
                return {Labs::IoStatus::TryLater, 0, 0};
            }
            take = std::min(*next, bytes.size());
        }
        wire.insert(wire.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(take));
        return {Labs::IoStatus::Done, take, 0};
    }

    // Para DrainQueue::Flush, que copia lo que recibe: una función que llama a este, no a una
    // copia.
    Labs::SendFunction Function()
    {
        return [this](std::span<const std::byte> bytes) { return (*this)(bytes); };
    }
};
} // namespace Labs::Tests
