#include "Data/Textures/JpegWriter.h"

#include "Core/Expected.h"
#include "Core/PathText.h"
#include "Data/Textures/JpegErrors.h"

#include <array>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <cstdio> // jpeglib.h uses FILE without including its header
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <span>
#include <string>

#include <jerror.h>
#include <jpeglib.h>

namespace Client::Data::Textures
{
// The libjpeg 6b API the original creates its compressor with (jpeg_CreateCompress with version 62, 0x63b17a).
static_assert(JPEG_LIB_VERSION == 62, "la API de la libjpeg 6b, la del original (0x63b150)");

// The libjpeg-turbo version compared with 6b on the screenshot vectors (JpegWriter.h), pinned in vcpkg.json, as in
// JpegImage.cpp.
static_assert(LIBJPEG_TURBO_VERSION_NUMBER == 3002000, "libjpeg-turbo 3.2.0, la comparada con la 6b");

namespace
{
// 6b's stdio destination OUTPUT_BUF_SIZE (jdatadst.c): it writes 4,096 bytes at a time.
constexpr std::size_t OutputBufferSize = 4096;
// 0x63b1c8: input_components, with in_color_space JCS_RGB (0x63b1d2).
constexpr int RgbComponents = 3;

// jpeg_stdio_dest (6b's jdatadst.c) over a std::ofstream. libjpeg sees only `manager`, the first member: the struct
// is standard-layout, so its address leads back to the whole struct.
struct StreamDestination
{
    jpeg_destination_mgr manager{};
    std::ofstream* stream = nullptr;
    std::array<JOCTET, OutputBufferSize> buffer{};
};

StreamDestination& DestinationOf(j_compress_ptr info)
{
    return *reinterpret_cast<StreamDestination*>(info->dest);
}

// ERREXIT(info, JERR_FILE_WRITE) of jdatadst.c, without the macro's C cast.
void ExitOnWriteError(j_compress_ptr info)
{
    info->err->msg_code = JERR_FILE_WRITE;
    info->err->error_exit(reinterpret_cast<j_common_ptr>(info));
}

void WriteOrExit(j_compress_ptr info, std::size_t size)
{
    StreamDestination& destination = DestinationOf(info);
    destination.stream->write(reinterpret_cast<const char*>(destination.buffer.data()),
                              static_cast<std::streamsize>(size));
    if (!*destination.stream)
        ExitOnWriteError(info);
}
} // namespace

// libjpeg calls these three as C functions, so they are (JpegErrors.h).
extern "C"
{
    static void InitDestination(j_compress_ptr info)
    {
        StreamDestination& destination = DestinationOf(info);
        destination.manager.next_output_byte = destination.buffer.data();
        destination.manager.free_in_buffer = destination.buffer.size();
    }

    static boolean EmptyOutputBuffer(j_compress_ptr info)
    {
        WriteOrExit(info, OutputBufferSize);
        InitDestination(info);
        return TRUE;
    }

    // With the file in error, the write error.
    static void TermDestination(j_compress_ptr info)
    {
        StreamDestination& destination = DestinationOf(info);
        const std::size_t remaining = OutputBufferSize - destination.manager.free_in_buffer;
        if (remaining > 0)
            WriteOrExit(info, remaining);
        destination.stream->flush();
        if (!*destination.stream)
            ExitOnWriteError(info);
    }
}

namespace
{
// Freeing one that was never created does nothing extra, like JpegImage.cpp's Decompressor.
struct Compressor
{
    jpeg_compress_struct info{};
    jpeg_error_mgr errors{};
    StreamDestination destination;
    std::jmp_buf& jump;

    Compressor(std::jmp_buf& target, std::ofstream& stream) : jump(target)
    {
        // 0x63b161: jpeg_std_error, with the error that jumps to Compress's setjmp (JpegErrors.h).
        info.err = jpeg_std_error(&errors);
        errors.error_exit = JpegJumpOnError;
        errors.output_message = JpegDiscardMessage;
        info.client_data = &jump;
        destination.manager.init_destination = InitDestination;
        destination.manager.empty_output_buffer = EmptyOutputBuffer;
        destination.manager.term_destination = TermDestination;
        destination.stream = &stream;
    }
    ~Compressor()
    {
        jpeg_destroy_compress(&info);
    }
    Compressor(const Compressor&) = delete;
    Compressor& operator=(const Compressor&) = delete;
};

// With 0x63b150's calls. As in Decompress (JpegImage.cpp), no object with a destructor lives between setjmp and
// libjpeg: the compressor, its jmp_buf and the file are the caller's, and nobody reads what changes after setjmp.
bool Compress(Compressor& compressor, int width, int height, std::span<const std::uint8_t> bottomUpRgb, int quality)
{
    jpeg_compress_struct& info = compressor.info;
    if (setjmp(compressor.jump) != 0)
    {
        return false;
    }
    // 0x63b166..0x63b17a: jpeg_CreateCompress, which zeroes everything but err and client_data; then the destination
    // (0x63b1aa, jpeg_stdio_dest).
    jpeg_CreateCompress(&info, JPEG_LIB_VERSION, sizeof(info));
    info.dest = &compressor.destination.manager;
    // 0x63b1af..0x63b1d2.
    info.image_width = static_cast<JDIMENSION>(width);
    info.image_height = static_cast<JDIMENSION>(height);
    info.input_components = RgbComponents;
    info.in_color_space = JCS_RGB;
    // 0x63b1dc, 0x63b1ee and 0x63b1fc.
    jpeg_set_defaults(&info);
    jpeg_set_quality(&info, quality, TRUE);
    jpeg_start_compress(&info, TRUE);
    // 0x63b210..0x63b250: JPEG row n is buffer row (height − n − 1), width · 3 bytes wide.
    const std::size_t rowSize = static_cast<std::size_t>(width) * RgbComponents;
    while (info.next_scanline < info.image_height)
    {
        const std::size_t source = std::size_t{info.image_height - info.next_scanline - 1};
        // libjpeg takes non-const rows and does not write them.
        JSAMPROW row = const_cast<JSAMPLE*>(bottomUpRgb.data() + source * rowSize);
        jpeg_write_scanlines(&info, &row, 1);
    }
    // 0x63b25c.
    jpeg_finish_compress(&info);
    return true;
}
} // namespace

Core::Expected<JpegWrite, std::string> WriteJpegFile(const std::filesystem::path& file, int width, int height,
                                                     std::span<const std::uint8_t> bottomUpRgb, int quality)
{
    if (width <= 0 || height <= 0 ||
        bottomUpRgb.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * RgbComponents)
    {
        return Core::Unexpected(std::format("0x63b150: {} × {} no es el tamaño de una pantalla de {} bytes", width,
                                            height, bottomUpRgb.size()));
    }
    // 0x63b182..0x63b19f: fopen(name, "wb"), which creates or empties the file; without it, 0 and nothing written.
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    if (!stream)
    {
        return JpegWrite::NotOpened;
    }
    std::jmp_buf jump{};
    Compressor compressor(jump, stream);
    if (!Compress(compressor, width, height, bottomUpRgb, quality))
    {
        return Core::Unexpected(std::format("0x63b150: libjpeg cortó la captura {} con el error {} (el original sale "
                                            "del juego con exit(1), el error_exit de jpeg_std_error)",
                                            Core::PathText(file), compressor.errors.msg_code));
    }
    // 0x63b262: fclose, ignoring its result; the destination already flushed and checked the error.
    return JpegWrite::Written;
}
} // namespace Client::Data::Textures
