#include "Data/Textures/JpegImage.h"

#include <csetjmp>
#include <cstddef>
#include <cstdio> // jpeglib.h usa FILE y no incluye su header
#include <limits>

#include <jpeglib.h>

namespace Client::Data::Textures
{

// La API de la libjpeg 6b, con la que el original crea su descompresor (jpeg_CreateDecompress con
// la versión 62). libjpeg-turbo la da salvo que se compile para emular la 7 o la 8.
static_assert(JPEG_LIB_VERSION == 62, "la API de la libjpeg 6b, la del original (0063b870-ozj-loader.md)");

namespace
{

// El manejador de errores de libjpeg y adónde volver cuando corta, como los arma el original: un
// struct de error de 0x84 bytes y un jmp_buf (0x63b850, 0063b490-terrainlight-ozj-loader.md §3).
struct JpegErrors
{
    jpeg_error_mgr manager{};
    std::jmp_buf jump{};
};

// error_exit: libjpeg lo llama ante un error y no puede volver. Salta al setjmp de Decompress.
[[noreturn]] void JumpOnError(j_common_ptr info)
{
    std::longjmp(static_cast<JpegErrors*>(info->client_data)->jump, 1);
}

// output_message: los mensajes no se muestran (JpegImage.h).
void DiscardMessage(j_common_ptr /*info*/) {}

// El descompresor de libjpeg, que se libera al salir por cualquier camino (regla 8). Liberar uno
// que no llegó a crearse no hace nada: libjpeg mira si tiene memoria pedida.
struct Decompressor
{
    jpeg_decompress_struct info{};
    JpegErrors errors{};

    Decompressor()
    {
        info.err = jpeg_std_error(&errors.manager);
        errors.manager.error_exit = JumpOnError;
        errors.manager.output_message = DiscardMessage;
        info.client_data = &errors;
    }
    ~Decompressor()
    {
        jpeg_destroy_decompress(&info);
    }
    Decompressor(const Decompressor&) = delete;
    Decompressor& operator=(const Decompressor&) = delete;
};

// Decodifica `jpeg` en `image`, con las llamadas del original. Devuelve false si libjpeg cortó con
// un error o si la imagen pasa de `maxSide`.
//
// longjmp vuelve al setjmp sin correr destructores, y si entre los dos quedara vivo un objeto que
// lo tiene, sería comportamiento indefinido (cppreference, std::longjmp). Por eso acá no vive
// ninguno: el descompresor y la imagen son de quien llama, y entre el setjmp y libjpeg solo hay
// referencias y números.
bool Decompress(Decompressor& decompressor, std::span<const std::byte> jpeg, int maxSide, JpegImage& image)
{
    jpeg_decompress_struct& info = decompressor.info;
    if (setjmp(decompressor.errors.jump) != 0)
    {
        return false;
    }
    jpeg_CreateDecompress(&info, JPEG_LIB_VERSION, sizeof(info));
    jpeg_mem_src(&info, reinterpret_cast<const unsigned char*>(jpeg.data()), static_cast<unsigned long>(jpeg.size()));
    jpeg_read_header(&info, TRUE);
    jpeg_start_decompress(&info);
    const auto limit = static_cast<JDIMENSION>(maxSide);
    if (info.output_width > limit || info.output_height > limit)
    {
        return false;
    }

    image.width = static_cast<int>(info.output_width);
    image.height = static_cast<int>(info.output_height);
    image.components = info.output_components;
    const std::size_t rowSize = std::size_t{info.output_width} * static_cast<std::size_t>(info.output_components);
    image.pixels.resize(rowSize * info.output_height);
    while (info.output_scanline < info.output_height)
    {
        JSAMPROW row = image.pixels.data() + std::size_t{info.output_scanline} * rowSize;
        jpeg_read_scanlines(&info, &row, 1);
    }
    jpeg_finish_decompress(&info);
    return true;
}

} // namespace

Core::Expected<JpegImage, DataError> DecodeJpeg(std::span<const std::byte> jpeg, int maxSide)
{
    // jpeg_mem_src recibe el tamaño como unsigned long, que en Windows es de 32 bits.
    if constexpr (sizeof(std::size_t) > sizeof(unsigned long))
    {
        if (jpeg.size() > std::numeric_limits<unsigned long>::max())
        {
            return Core::Unexpected(DataError::InvalidData);
        }
    }

    Decompressor decompressor;
    JpegImage image;
    if (!Decompress(decompressor, jpeg, maxSide, image))
    {
        return Core::Unexpected(DataError::InvalidData);
    }
    return image;
}

} // namespace Client::Data::Textures
