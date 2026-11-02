#pragma once

#include "Core/Expected.h"
#include "Core/ILogSink.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

// Cada línea a un archivo de texto, con su fin de línea, en una sola escritura y con un flush: si
// el cliente se cae, lo que se logueó hasta ahí ya está en el disco.
//
// Agrega al final, como el original: 0x41f7a0 abre MuError.log con OPEN_ALWAYS y se para en
// FILE_END. Así el log de una caída sigue ahí al volver a abrir el cliente. Dos clientes abiertos a
// la vez (en el MU es normal) escriben los dos: en modo agregar cada escritura va al final, y las
// líneas quedan intercaladas pero enteras. El original lo abre compartido solo para leer, y el
// segundo cliente se queda sin log (Q-C48 de 07). El original lo cifra con un XOR; MuRet lo
// escribe en claro, a propósito. Para que no crezca sin fin, al abrir se vacía si ya pasa de
// MaxLogFileBytes.
namespace Client::Core
{
// El tope del log al abrir. El original no vacía: al arrancar, 0x41f830 lee hasta 0x1ffff bytes
// y, si leyó 0x7fff o más, se queda con lo que va desde el byte 0x7fff; si no, con las cuatro
// últimas sesiones ("###### Log Begin ######"). Después borra el archivo y lo reescribe (Q-C48,
// estático, verificado con objdump). MuRet loguea más por corrida (el arranque, los avisos del bucle, SDL),
// y vaciarlo entero al pasar 1 MiB es más simple que cortar el principio y deja varias corridas.
// Se mira solo al abrir: así una corrida no pierde lo suyo a la mitad.
inline constexpr std::uintmax_t MaxLogFileBytes = 1024 * 1024;

class FileSink final : public ILogSink
{
public:
    // Si no se puede abrir (la carpeta no existe, no hay permiso), un error que nombra la ruta:
    // main lo avisa por stderr y sigue sin archivo. Va en un unique_ptr porque el mutex no se
    // mueve, y el Expected guarda su valor moviéndolo.
    [[nodiscard]] static Expected<std::unique_ptr<FileSink>, std::string> Open(const std::filesystem::path& path);

    void Write(LogLevel level, std::string_view line) override;

private:
    // Privado: solo Open, que mira que el archivo haya abierto, arma un FileSink.
    explicit FileSink(std::ofstream file);

    std::ofstream m_file;
    std::mutex m_mutex;
};
} // namespace Client::Core
