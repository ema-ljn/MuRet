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
// FILE_END. Así el log de una caída sigue ahí al volver a abrir el cliente, y dos clientes abiertos
// a la vez (en el MU es normal) no se pisan: en modo agregar cada escritura va al final, y las
// líneas de los dos quedan intercaladas pero enteras. Para que no crezca sin fin, al abrir se
// vacía si ya pasa de MaxLogFileBytes.
namespace Client::Core
{
// El tope del log al abrir. El original vacía MuError.log cuando pasa de unos 0x7ffe bytes
// (0x41f830, estático, en el decompilado), unos 32 KB; MuRet loguea más por corrida (el arranque,
// los avisos del bucle, SDL), y 1 MiB guarda varias. Se mira solo al abrir: así una corrida no
// pierde lo suyo a la mitad.
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
