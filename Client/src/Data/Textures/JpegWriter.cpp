#include "Data/Textures/JpegWriter.h"

#include "Core/Expected.h"
#include "Core/PathText.h"
#include "Data/Textures/JpegErrors.h"

#include <array>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <cstdio> // jpeglib.h usa FILE y no incluye su header
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
// La API de la libjpeg 6b, con la que el original crea su compresor (jpeg_CreateCompress con la versión 62, 0x63b17a).
static_assert(JPEG_LIB_VERSION == 62, "la API de la libjpeg 6b, la del original (0x63b150)");

// La versión de libjpeg-turbo que se comparó con la 6b en los vectores de la captura (JpegWriter.h), fijada en
// vcpkg.json, como en JpegImage.cpp.
static_assert(LIBJPEG_TURBO_VERSION_NUMBER == 3002000, "libjpeg-turbo 3.2.0, la comparada con la 6b");

namespace
{
// OUTPUT_BUF_SIZE del destino de stdio de la 6b (jdatadst.c): escribe de a 4.096 bytes.
constexpr std::size_t OutputBufferSize = 4096;
// 0x63b1c8: input_components, con in_color_space en JCS_RGB (0x63b1d2).
constexpr int RgbComponents = 3;

// El destino de jpeg_stdio_dest (jdatadst.c de la 6b) sobre un std::ofstream. libjpeg ve solo `manager`, el primer
// miembro: el struct es standard-layout, así que desde su dirección se vuelve al struct entero.
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

// ERREXIT(info, JERR_FILE_WRITE) de jdatadst.c, sin el cast de C de la macro.
void ExitOnWriteError(j_compress_ptr info)
{
    info->err->msg_code = JERR_FILE_WRITE;
    info->err->error_exit(reinterpret_cast<j_common_ptr>(info));
}

// fwrite de `size` bytes del buffer: si no salen todos, el error de escritura.
void WriteOrExit(j_compress_ptr info, std::size_t size)
{
    StreamDestination& destination = DestinationOf(info);
    destination.stream->write(reinterpret_cast<const char*>(destination.buffer.data()),
                              static_cast<std::streamsize>(size));
    if (!*destination.stream)
        ExitOnWriteError(info);
}
} // namespace

// libjpeg llama a estas tres como funciones de C, así que lo son (JpegErrors.h). static: no salen de este archivo.
extern "C"
{
    // init_destination: el buffer entero, libre.
    static void InitDestination(j_compress_ptr info)
    {
        StreamDestination& destination = DestinationOf(info);
        destination.manager.next_output_byte = destination.buffer.data();
        destination.manager.free_in_buffer = destination.buffer.size();
    }

    // empty_output_buffer: el buffer lleno, al archivo, y otra vez libre.
    static boolean EmptyOutputBuffer(j_compress_ptr info)
    {
        WriteOrExit(info, OutputBufferSize);
        InitDestination(info);
        return TRUE;
    }

    // term_destination: lo que quedó, al archivo, y el fflush; con el archivo en error, el error de escritura.
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
// El compresor de libjpeg, que se libera al salir por cualquier camino (regla 8), como el Decompressor de
// JpegImage.cpp: liberar uno que no llegó a crearse no hace nada de más.
struct Compressor
{
    jpeg_compress_struct info{};
    jpeg_error_mgr errors{};
    StreamDestination destination;
    std::jmp_buf& jump;

    Compressor(std::jmp_buf& target, std::ofstream& stream) : jump(target)
    {
        // 0x63b161: jpeg_std_error, con el error que salta al setjmp de Compress (JpegErrors.h).
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

// Comprime en el archivo de `compressor` con las llamadas de 0x63b150. Devuelve false si libjpeg cortó.
//
// Como en Decompress (JpegImage.cpp), entre el setjmp y libjpeg no vive ningún objeto con destructor: el compresor,
// su jmp_buf y el archivo son de quien llama. Lo que cambia después del setjmp nadie lo lee después del salto.
bool Compress(Compressor& compressor, int width, int height, std::span<const std::uint8_t> bottomUpRgb, int quality)
{
    jpeg_compress_struct& info = compressor.info;
    if (setjmp(compressor.jump) != 0)
    {
        return false;
    }
    // 0x63b166..0x63b17a: jpeg_CreateCompress, que vuelve a poner en cero todo menos err y client_data; después, el
    // destino (0x63b1aa, jpeg_stdio_dest).
    jpeg_CreateCompress(&info, JPEG_LIB_VERSION, sizeof(info));
    info.dest = &compressor.destination.manager;
    // 0x63b1af..0x63b1d2.
    info.image_width = static_cast<JDIMENSION>(width);
    info.image_height = static_cast<JDIMENSION>(height);
    info.input_components = RgbComponents;
    info.in_color_space = JCS_RGB;
    // 0x63b1dc, 0x63b1ee y 0x63b1fc.
    jpeg_set_defaults(&info);
    jpeg_set_quality(&info, quality, TRUE);
    jpeg_start_compress(&info, TRUE);
    // 0x63b210..0x63b250: la fila n del JPEG es la (alto − n − 1) del buffer, de ancho · 3 bytes.
    const std::size_t rowSize = static_cast<std::size_t>(width) * RgbComponents;
    while (info.next_scanline < info.image_height)
    {
        const std::size_t source = std::size_t{info.image_height - info.next_scanline - 1};
        // libjpeg pide filas sin const y no las escribe.
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
    // 0x63b182..0x63b19f: fopen(nombre, "wb"), que crea el archivo o lo vacía; sin él, 0 sin escribir nada.
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
    // 0x63b262: fclose, sin mirar lo que da; el destino ya hizo el fflush y miró el error.
    return JpegWrite::Written;
}
} // namespace Client::Data::Textures
