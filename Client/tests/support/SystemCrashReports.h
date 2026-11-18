#pragma once

// Sólo en el hijo de un death test: Linux no debe enviar su fallo deliberado a systemd-coredump/DrKonqi.
// El manejador propio de MuRet sigue escribiendo su dump. No cambia la configuración del escritorio.
namespace Client::Tests
{
[[nodiscard]] bool SuppressSystemCrashReports();
} // namespace Client::Tests
