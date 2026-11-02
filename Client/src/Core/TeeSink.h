#pragma once

#include "Core/ILogSink.h"

#include <mutex>
#include <string_view>

// Escribe la misma línea en dos salidas (stderr y el archivo, en main.cpp). Su std::mutex hace que
// las dos reciban las líneas en el mismo orden aunque lleguen de dos hilos: cada salida tiene el
// suyo, pero solo ordena sus propias escrituras. Los mutex se toman siempre en el mismo orden (el
// del Tee y después el de cada salida), así que no se traban.
namespace Client::Core
{
class TeeSink final : public ILogSink
{
public:
    // Las dos salidas tienen que vivir más que el Tee.
    TeeSink(ILogSink& first, ILogSink& second);

    void Write(LogLevel level, std::string_view line) override;

private:
    ILogSink& m_first;
    ILogSink& m_second;
    std::mutex m_mutex;
};
} // namespace Client::Core
