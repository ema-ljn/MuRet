#include "Data/Textures/JpegImage.h"

#include <algorithm>
#include <csetjmp>
#include <cstddef>
#include <cstdio> // jpeglib.h usa FILE y no incluye su header
#include <initializer_list>
#include <limits>
#include <span>

#include <jpeglib.h>

namespace Client::Data::Textures
{

// La API de la libjpeg 6b, con la que el original crea su descompresor (jpeg_CreateDecompress con
// la versión 62). libjpeg-turbo la da salvo que se compile para emular la 7 o la 8.
static_assert(JPEG_LIB_VERSION == 62, "la API de la libjpeg 6b, la del original (0063b870-ozj-loader.md)");

// La versión de libjpeg-turbo que se comparó con la 6b en los 2.380 JPEG del laboratorio
// (GoldenJpegImageTest y lab-6b.txt), fijada en vcpkg.json. Otra puede dar otros bytes: subirla es
// un PR propio que corre los golden (CODING_RULES.md §10), y cambia este número.
static_assert(LIBJPEG_TURBO_VERSION_NUMBER == 3002000, "libjpeg-turbo 3.2.0, la comparada con la 6b");

namespace
{

// La precisión de las muestras que lee la 6b del original: se compila con BITS_IN_JSAMPLE en 8 y
// corta con cualquier otra (JERR_BAD_PRECISION, en su jdinput.c). Es la que da jpeg_read_scanlines.
constexpr int SixBSampleBits = 8;
static_assert(SixBSampleBits == BITS_IN_JSAMPLE, "jpeg_read_scanlines da muestras de 8 bits");

} // namespace

// libjpeg llama a estas dos como funciones de C, así que lo son: una función de C y una de C++ son
// de tipos distintos ([dcl.link]). static: no salen de este archivo.
extern "C"
{
    // error_exit: libjpeg lo llama ante un error y no puede volver. Salta al setjmp de Decompress,
    // con el jmp_buf que dejó el Decompressor en client_data.
    [[noreturn]] static void JumpOnError(j_common_ptr info)
    {
        std::longjmp(*static_cast<std::jmp_buf*>(info->client_data), 1);
    }

    // output_message: los mensajes no se muestran (JpegImage.h).
    static void DiscardMessage(j_common_ptr /*info*/) {}
}

namespace
{

// El descompresor de libjpeg, que se libera al salir por cualquier camino (regla 8). Liberar uno
// que no llegó a crearse, o uno que no terminó, no hace nada de más: libjpeg mira si tiene memoria
// pedida y la suelta.
//
// El original arma el manejador de errores de libjpeg y adónde volver cuando corta como un struct:
// los 0x84 bytes de jpeg_error_mgr y el jmp_buf después (0x63b850 salta a err + 0x84,
// 0063b490-terrainlight-ozj-loader.md §3). Acá el jmp_buf es de quien crea el Decompressor: en
// MSVC de 64 bits va alineado a 16, así que un struct con jpeg_error_mgr y el jmp_buf queda con
// relleno, y MSVC lo avisa (C4324, que con los warnings como errores corta el build).
struct Decompressor
{
    jpeg_decompress_struct info{};
    jpeg_error_mgr errors{};
    std::jmp_buf& jump;

    explicit Decompressor(std::jmp_buf& target) : jump(target)
    {
        info.err = jpeg_std_error(&errors);
        errors.error_exit = JumpOnError;
        errors.output_message = DiscardMessage;
        info.client_data = &jump;
    }
    ~Decompressor()
    {
        jpeg_destroy_decompress(&info);
    }
    Decompressor(const Decompressor&) = delete;
    Decompressor& operator=(const Decompressor&) = delete;
};

// Si la 6b del original puede leer `info`, con la cabecera ya leída. libjpeg-turbo lee dos cosas
// más, y la 6b las rechaza (Q-C33):
//   - la codificación aritmética: la 6b no trae ese decodificador y jinit_master_decompress corta
//     con JERR_ARITH_NOTIMPL;
//   - el JPEG sin pérdida (SOF3): la 6b corta al leer la cabecera con JERR_SOF_UNSUPPORTED. turbo
//     no lo dice en un campo público, pero en ese modo cada unidad de datos es una muestra y no un
//     bloque de 8 × 8: deja min_DCT_scaled_size en 1, y en un JPEG con DCT, en DCTSIZE (su
//     jdinput.c).
// Las muestras de otra precisión que 8 bits las rechazan las dos: la 6b al leer la cabecera
// (JERR_BAD_PRECISION) y turbo, con la API de 8 bits, recién en jpeg_read_scanlines (su
// jdapistd.c). Acá se rechazan antes de decodificar.
bool SixBCanRead(const jpeg_decompress_struct& info)
{
    return info.data_precision == SixBSampleBits && !info.arith_code && info.min_DCT_scaled_size == DCTSIZE;
}

// Las tablas de Huffman que turbo llena con las estándar si faltan cuando empieza a decodificar un
// JPEG secuencial: la 0 y la 1, de DC y de AC, como para Motion JPEG, que no las trae
// (std_huff_tables, que llama jinit_huff_decoder en su jdhuff.c).
constexpr std::size_t StandardHuffmanTables = 2;

// Una tabla de Huffman que no se puede armar: tres códigos de 1 bit, cuando con 1 bit hay dos.
constexpr UINT8 ImpossibleOneBitCodes = 3;

// Deja como faltan las tablas de Huffman que turbo llenaría (Q-C33). La 6b no las llena: si un scan
// pide una que no está, jpeg_make_d_derived_tbl corta con JERR_NO_HUFF_TABLE. turbo solo llena las
// que están en NULL (add_huff_table, en su jstdhuff.c), así que acá van en una tabla imposible: un
// DHT que llegue después la pisa como a cualquier otra, y si un scan la pide sin que llegue,
// jpeg_make_d_derived_tbl corta con JERR_BAD_HUFF_TABLE, cuando la 6b cortaría. En un progresivo
// turbo no llena nada y una tabla imposible corta igual que una que falta.
void KeepMissingHuffmanTablesMissing(jpeg_decompress_struct& info)
{
    for (std::size_t number = 0; number < StandardHuffmanTables; ++number)
    {
        for (JHUFF_TBL** table : {&info.dc_huff_tbl_ptrs[number], &info.ac_huff_tbl_ptrs[number]})
        {
            if (*table != nullptr)
            {
                continue;
            }
            // libjpeg pide el descompresor como j_common_ptr, el principio que comparte con el
            // compresor (jpeglib.h), y así lo pasa ella misma (jpeg_destroy_decompress, en
            // jdapimin.c). La tabla es de libjpeg: la libera jpeg_destroy_decompress.
            *table = jpeg_alloc_huff_table(reinterpret_cast<j_common_ptr>(&info));
            std::ranges::fill((*table)->bits, UINT8{0});
            std::ranges::fill((*table)->huffval, UINT8{0});
            (*table)->bits[1] = ImpossibleOneBitCodes;
        }
    }
}

// Las muestras que necesita el filtro de agrandar al doble de ancho: con 2 o menos, las dos
// libjpeg repiten muestras aunque el filtro esté prendido (downsampled_width > 2, en jdsample.c).
constexpr JDIMENSION NarrowestFancyWidth = 2;

// Elige cómo agrandar los componentes submuestreados como la 6b (Q-C33). La 6b usa el filtro
// "fancy" (una interpolación) cuando un componente va al doble de ancho (h2v1 y h2v2) y tiene más
// de NarrowestFancyWidth muestras, y repite muestras en los demás casos (int_upsample, en su
// jdsample.c). libjpeg-turbo usa además el filtro cuando va solo al doble de alto (h1v2, el 4:4:0).
// Si ninguno usa el filtro al doble de ancho, apagarlo deja a turbo como la 6b: el h1v2 pasa a
// repetir filas y nada más lo usaba. Si hay de los dos, no hay cómo: queda el de turbo.
void UpsampleLikeThe6b(jpeg_decompress_struct& info)
{
    bool doubleHeightOnly = false;
    bool doubleWidth = false;
    for (int index = 0; index < info.num_components; ++index)
    {
        const jpeg_component_info& component = info.comp_info[index];
        const bool sameWidth = component.h_samp_factor == info.max_h_samp_factor;
        const bool halfWidth = component.h_samp_factor * 2 == info.max_h_samp_factor;
        const bool sameHeight = component.v_samp_factor == info.max_v_samp_factor;
        const bool halfHeight = component.v_samp_factor * 2 == info.max_v_samp_factor;
        doubleHeightOnly = doubleHeightOnly || (sameWidth && halfHeight);
        const bool wideEnough = component.downsampled_width > NarrowestFancyWidth;
        doubleWidth = doubleWidth || (halfWidth && (sameHeight || halfHeight) && wideEnough);
    }
    if (doubleHeightOnly && !doubleWidth)
    {
        info.do_fancy_upsampling = FALSE;
    }
}

// Decodifica `jpeg` en `image`, con las llamadas del original. Devuelve false si libjpeg cortó con
// un error antes de la última fila, si la 6b no lo puede leer o si la imagen pasa de `maxSide`.
//
// longjmp vuelve al setjmp sin correr destructores, y si entre los dos quedara vivo un objeto que
// lo tiene, sería comportamiento indefinido (cppreference, std::longjmp). Por eso acá no vive
// ninguno: el descompresor, su jmp_buf y la imagen son de quien llama, y entre el setjmp y libjpeg
// solo hay referencias y números. Los que cambian después del setjmp, como `row`, quedan
// indeterminados después del salto (cppreference, std::longjmp), pero nadie los lee.
bool Decompress(Decompressor& decompressor, std::span<const std::byte> jpeg, int maxSide, JpegImage& image)
{
    jpeg_decompress_struct& info = decompressor.info;
    if (setjmp(decompressor.jump) != 0)
    {
        return false;
    }
    jpeg_CreateDecompress(&info, JPEG_LIB_VERSION, sizeof(info));
    jpeg_mem_src(&info, reinterpret_cast<const unsigned char*>(jpeg.data()), static_cast<unsigned long>(jpeg.size()));
    jpeg_read_header(&info, TRUE);
    if (!SixBCanRead(info))
    {
        return false;
    }
    KeepMissingHuffmanTablesMissing(info);
    UpsampleLikeThe6b(info);

    // El tamaño, antes de decodificar: con un JPEG progresivo, jpeg_start_decompress ya lee todos
    // sus scans a un buffer del tamaño de la imagen, y la cabecera puede decir 65.500 × 65.500 con
    // unos pocos bytes. El original lo mira después, con el mismo resultado: false. Con los
    // parámetros por defecto, jpeg_calc_output_dimensions da el tamaño que daría start_decompress.
    jpeg_calc_output_dimensions(&info);
    const JDIMENSION limit = maxSide > 0 ? static_cast<JDIMENSION>(maxSide) : 0;
    if (info.output_width > limit || info.output_height > limit)
    {
        return false;
    }

    jpeg_start_decompress(&info);
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

    // Acá el original llama a jpeg_finish_decompress, que lee lo que queda hasta el final y corta
    // si encuentra algo que no espera, como un segundo SOI o un scan de más. Pero ya usó la imagen:
    // la textura subió (glTexImage2D en 0x63bc89, finish en 0x63bcd6) y la luz se escribió
    // (0x63b7a2 a 0x63b80c, finish en 0x63b81b), y nadie mira el false (Q-C32). Lo que viene
    // después de la última fila no cambia nada, así que no se lee: el destructor libera igual. En
    // un JPEG de un solo scan, eso es todo lo que sigue a sus datos. En uno de varios (un
    // progresivo, por ejemplo) no queda nada: las dos libjpeg ya leyeron hasta el final en
    // jpeg_start_decompress, y ahí cortan por lo mismo que cortaría finish.
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

    std::jmp_buf jump{};
    Decompressor decompressor(jump);
    JpegImage image;
    if (!Decompress(decompressor, jpeg, maxSide, image))
    {
        return Core::Unexpected(DataError::InvalidData);
    }
    return image;
}

} // namespace Client::Data::Textures
