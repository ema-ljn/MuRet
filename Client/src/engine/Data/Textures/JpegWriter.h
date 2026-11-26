#pragma once

#include "Core/Expected.h"
#include "EngineApi.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

// The screenshot in JPEG as 0x63b150 (Client/docs/research/06-render-escenas-y-ui.md, "La captura de pantalla"), with
// the original's IJG libjpeg 6b calls: jpeg_CreateCompress with version 62, 3-component RGB, jpeg_set_defaults,
// jpeg_set_quality forcing baseline, jpeg_start_compress(TRUE) and jpeg_write_scanlines one row at a time. With those,
// libjpeg-turbo 3.2.0 gives 6b's bytes, with and without SIMD: CI checks the screenshot-*.6b.jpg vectors 6b wrote with
// the same calls (Client/tests/vectors/jpeg/README.md). The original writes through jpeg_stdio_dest on a
// fopen("wb") FILE; here an own destination on a std::ofstream writes 4,096 bytes at a time like stdio's and leaves
// the same bytes in the file.
namespace Client::Data::Textures
{
enum class JpegWrite
{
    Written,
    // 0x63b188..0x63b19f: fopen(name, "wb") gave NULL. The original returns 0 writing nothing, and its caller
    // (0x6202e0) ignores it.
    NotOpened,
};

// 0x63b150(name, width, height, pixels, quality): `bottomUpRgb` is `height` rows of `width` × 3 bytes (red, green and
// blue) from the bottom one, as glReadPixels leaves them; JPEG row n is buffer row (height − 1 − n). The file opens
// before compressing, like the original's fopen: if it does not open, JpegWrite::NotOpened. A width or height of 0 or
// less, or a buffer of another size, is the error, opening nothing. If libjpeg stops (cannot write, out of memory),
// also the error: the original's jpeg_std_error error_exit quits the game with exit(1), and the file stays as it was.
[[nodiscard]] ENGINE_API Core::Expected<JpegWrite, std::string> WriteJpegFile(const std::filesystem::path& file,
                                                                              int width, int height,
                                                                              std::span<const std::uint8_t> bottomUpRgb,
                                                                              int quality);
} // namespace Client::Data::Textures
