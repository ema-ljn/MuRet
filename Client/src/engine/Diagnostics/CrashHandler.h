#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

// El manejador de caídas del original (02 § El manejador de caídas): lo primero que hace WinMain
// es 0x67f4f0(0x4b9a70, 0), que instala el filtro 0x680180 con SetUnhandledExceptionFilter. Si el
// cliente se cae, el filtro junta el estado del proceso y la cadena de frames, 0x4b9a70 escribe
// MuError.dmp (0x67f840), deja "Save DumpFile complete - MuError.dmp" en el log y devuelve
// EXCEPTION_EXECUTE_HANDLER: el proceso termina sin el diálogo del sistema.
//
// MuRet hace lo mismo, con el formato de CrashDump.h:
//   - en Windows, con SetUnhandledExceptionFilter, y el filtro devuelve EXCEPTION_EXECUTE_HANDLER;
//   - en Linux, con sigaction para SIGSEGV, SIGBUS, SIGILL, SIGFPE y SIGTRAP, en una pila aparte
//     (sigaltstack) para que un desborde de la pila también llegue. Al terminar vuelve a la acción
//     por defecto y levanta la señal otra vez: el proceso muere por ella, como sin manejador;
//   - en otro sistema, Install devuelve un error y el cliente sigue sin manejador.
// Las diferencias con el original, una por una, en Q-C241..Q-C245 de 07.
//
// Lo que corre después de la caída no pide memoria ni toma locks: todo vive en un estado estático
// (un buffer para el archivo, otro para leer /proc, las rutas ya copiadas) que se arma al instalar.
// En Linux usa solo funciones de la lista de POSIX de las seguras dentro de una señal (open, read,
// write, close, readlink, getpid, time, clock_gettime, nanosleep, pause, sigaction, raise) y
// sysinfo, que en glibc es la llamada al sistema sin más.
//
// Es la excepción a D-C1 (sin globales): un manejador de señales no recibe nada más que la señal,
// así que lo que necesita tiene que estar en una variable estática. Por eso hay uno solo: Install
// falla si ya hay otro instalado.
//
// No restaura la resolución: el original llama a ChangeDisplaySettingsA(NULL, 0) (0x4b9b66)
// porque cambia el modo del escritorio para su pantalla completa, y MuRet no lo cambia nunca (su
// ventana es una ventana, Platform/Window.h). No hay nada que deshacer (Q-C244).
namespace Client::Core::Crash
{
// El nombre de 0x6c80dc. Relativo a la carpeta actual, como el fopen(..., "wb") de 0x67f840.
inline constexpr std::string_view CrashDumpFileName = "MuError.dmp";

// Lo más largo que puede ser una ruta del manejador, en caracteres: se copian a un buffer fijo al
// instalar, para no pedir memoria después de la caída.
inline constexpr std::size_t MaxCrashPathLength = 4096;

class CrashHandler
{
public:
    // Instala el manejador, que escribe el volcado en `dumpPath` (main pasa CrashDumpFileName). Un
    // error si ya hay uno instalado, si la ruta es demasiado larga o si el sistema no lo deja.
    [[nodiscard]] static Expected<std::unique_ptr<CrashHandler>, std::string>
    Install(const std::filesystem::path& dumpPath);

    // Desinstala y deja lo que había antes: el filtro anterior, como 0x67f740, o las acciones de
    // las señales y la pila aparte de antes.
    ~CrashHandler();

    CrashHandler(const CrashHandler&) = delete;
    CrashHandler& operator=(const CrashHandler&) = delete;
    CrashHandler(CrashHandler&&) = delete;
    CrashHandler& operator=(CrashHandler&&) = delete;

    // El archivo del log adonde va la línea de 0x4b9b3e cuando el volcado se escribió: el del
    // FileSink de main, que se abre después de instalar. Sin llamarla no se escribe ninguna, como
    // el original con el log cerrado (0x41f9ec). La línea es la de CrashLogLine con el fin de
    // línea del FileSink. false si la ruta es demasiado larga, y entonces no hay línea.
    [[nodiscard]] bool SetLogFile(const std::filesystem::path& logPath);

    // Si hay un manejador instalado ahora.
    [[nodiscard]] static bool IsInstalled();

private:
    CrashHandler() = default;
};
} // namespace Client::Core::Crash
