#pragma once

#include "Core/ILogSink.h"

#include <cstdio>
#include <mutex>
#include <string_view>

// Cada línea a stderr, con su fin de línea, en un solo fwrite: el lock del FILE (POSIX y el CRT de
// MSVC) la deja entera también contra otro que escriba a stderr sin pasar por acá (un fprintf, la
// salida por defecto de SDL). El std::mutex ordena las de esta salida. stderr no tiene buffer: la
// línea sale al escribirla.
namespace Client::Core
{
class StderrSink final : public ILogSink
{
public:
    // `output` es stderr; un test pasa un std::tmpfile() para leer lo que se escribió. No es
    // dueña: no lo cierra.
    explicit StderrSink(std::FILE* output = stderr);

    void Write(LogLevel level, std::string_view line) override;

private:
    std::FILE* m_output;
    std::mutex m_mutex;
};
} // namespace Client::Core
