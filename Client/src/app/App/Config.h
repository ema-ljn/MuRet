#pragma once

#include "App/CommandLine.h"
#include "App/ConfigError.h"
#include "Kernel/PacingMode.h"
#include "Session/ServerAddress.h"
#include "Platform/VSyncMode.h"
#include "Core/Expected.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// The client config, muret-client.ini (CL3.4; 02 § Qué implica para MuRet). The original reads the registry
// (HKCU\SOFTWARE\Webzen\Mu\Config) and a config.ini; MuRet reads only this file:
//
//   [Data]
//   Path = C:\MU\Data          ; the data folder (if not given, the one Data::DataPath says)
//   [Server]
//   Host = 10.0.0.1            ; 1 to 255 bytes, no spaces (Q-C37)
//   Port = 44405               ; 1 to 65535
//   [Video]
//   Width = 640
//   Height = 480
//   VSync = on                 ; on, adaptive or off
//   Pacing = decoupled         ; decoupled or faithful
//   MaxFps = 0                 ; FPS cap with VSync off: 0 (no cap), or MinFpsLimit to MaxFpsLimit
//   FontFile = letra.ttf       ; the TrueType font of the text (CL5.3)
//   BoldFontFile = negrita.ttf ; its bold; if not given, FontFile's is widened
//   [Audio]
//   SoundOnOff = 1              ; 1 for effects, 0 to turn them off
//   MusicOnOff = 1              ; 1 for music, 0 to turn it off
//   [Log]
//   Path = muret-client.log
//
// Sections, keys and the VSync and Pacing values are case-insensitive. A `;` at the start of a line, or after a
// space, starts a comment; `#` is not a comment, and a line that starts with `#` is an error. The UTF-8 BOM and the
// Windows line end are accepted. A pair of double quotes around a path or the host is stripped (what Explorer's
// "Copy as path" gives); a lone quote stays part of the value. Errors, naming their line: a line that is neither
// `[Section]` nor `Key = value`, a section or key that does not exist, a repeated key or one without a value, a value
// that cannot be read, a \r without \n, a path with control characters or that is not UTF-8, and a host that is not
// visible ASCII (ParseHost).
//
// The parser is our own, not inih (CODING_RULES §10; step 1 of CL3.4 in the CL3 plan). The vcpkg port (inih 62)
// builds with its meson defaults. What rules it out: the line length is fixed at build time (max_line_length = 200)
// and the portfile does not let it change, so a line over 198 bytes without the line end is an error, and a
// `Host = ` with 255 bytes (Q-C37) fits through neither of its two APIs. Also, an indented line continues the
// previous key's value (multi-line_entries), and the INIReader of the `cpp` feature joins repeated keys with "\n"
// and does not give a bad value's line; those two could be rejected from the C API's handler, but not the first.
namespace Client::App
{
// Not from the original: with 16384, Width * Height fits in an int (2^28), and it is plenty for today's monitors (8K
// is 7680 × 4320).
inline constexpr int MaxWindowSize = 16384;
// Not from the original: a whole config is under 1 KiB, and with the cap a huge file or a device (/dev/zero) is not
// read whole.
inline constexpr std::uintmax_t MaxConfigBytes = 64 * 1024;
inline constexpr std::string_view DefaultConfigFileName = "muret-client.ini";

struct ClientConfig
{
    // [Data] Path. Empty: not given, and the default folder applies.
    std::optional<std::filesystem::path> dataPath;
    // [Server] Host and Port. Empty if neither is there; with only one, the other keeps its default (empty host, port
    // DefaultConnectPort).
    std::optional<Session::ServerAddress> server;
    // [Video] Width and Height, 1 to MaxWindowSize. To open the window they must be a valid resolution
    // (Platform/Resolution.h): main.cpp checks that against the monitor's modes after SDL_Init.
    int width = 640;
    int height = 480;
    // [Video] VSync. Empty: Resolve decides it (step 4) from the Pacing.
    std::optional<Platform::VSyncMode> vsync;
    // [Video] Pacing.
    Kernel::PacingMode pacing = Kernel::PacingMode::Decoupled;
    // [Video] MaxFps, 0 or MinFpsLimit to MaxFpsLimit: the normal mode's FPS cap (MuMain's FpsCap), used only with
    // VSync off (EffectiveMaxFps). 0 means no cap, like MuMain.
    int maxFps = 0;
    // [Video] FontFile and BoldFontFile: the TrueType fonts of the text (question 8 of the CL5 plan). Empty: not given.
    // Without BoldFontFile, the bold is FontFile's widened.
    std::optional<std::filesystem::path> fontFile;
    std::optional<std::filesystem::path> boldFontFile;
    // [Audio] SoundOnOff and MusicOnOff. The official launcher starts them on.
    bool soundOnOff = true;
    bool musicOnOff = true;
    // [Log] Path. Empty: muret-client.log next to the executable.
    std::optional<std::filesystem::path> logPath;
};

// Does not open the file or check that the paths exist: Resolve does. The first error stops it.
Core::Expected<ClientConfig, ConfigError> ParseConfig(std::string_view text);

// The config Main uses, in this order (the first error stops):
//   1. the file: the --config one, which must exist, or DefaultConfigFileName in exeFolder, and if that one is
//      missing, the defaults (so Main --frames 60 works with nothing);
//   2. ParseConfig: an error names the file and the line. A relative path in the config (the data folder, the log
//      and the fonts) is taken from exeFolder, not from the current folder;
//   3. the command line overrides the config (/u /p, --data, --pacing, --vsync, --max-fps);
//   4. in faithful mode, a VSync that is not off is an error (it names Pacing and VSync), and without VSync it stays
//      off; a MaxFps that is not 0 too (it names Pacing and MaxFps): faithful mode waits like the original. In
//      normal mode, without VSync it stays on. --auto-login without a host (HasServerHost) is an error too (CL5.5);
//   5. the data folder, only if given, must exist.
// Without --auto-login, an empty host is not an error here: Main goes on without network.
Core::Expected<ClientConfig, std::string> Resolve(const std::filesystem::path& exeFolder,
                                                  const CommandLine& commandLine);

// A host that is not empty. A [Server] with only the port is not enough, and Main goes on without network (the CL4
// plan, CL4.4).
[[nodiscard]] bool HasServerHost(const ClientConfig& config);

// `maxFps` only when the VSync the window ended up with is off, otherwise 0 (no cap), like MuMain's FpsCap
// (ApplyVSyncPreferenceNow in Winmain.cpp). With VSync the swap already paces to the refresh, and a cap that does
// not divide it (50 at 60 Hz) would draw in jerks.
[[nodiscard]] int EffectiveMaxFps(int maxFps, Platform::VSyncMode vsync);
} // namespace Client::App
