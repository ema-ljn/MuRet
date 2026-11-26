#include "Data/Textures/JpegImage.h"

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Textures/JpegErrors.h"

#include <algorithm>
#include <csetjmp>
#include <cstddef>
#include <cstdio> // jpeglib.h uses FILE without including its header
#include <initializer_list>
#include <limits>
#include <span>

#include <jpeglib.h>

namespace Client::Data::Textures
{
// The libjpeg 6b API the original creates its decompressor with (jpeg_CreateDecompress with version 62).
// libjpeg-turbo gives it unless built to emulate 7 or 8.
static_assert(JPEG_LIB_VERSION == 62, "la API de la libjpeg 6b, la del original (0063b870-ozj-loader.md)");

// The libjpeg-turbo version compared with 6b on the lab's 2,380 JPEGs (test_golden_jpeg_image.cpp and lab-6b.txt),
// pinned in vcpkg.json. Another may give other bytes: raising it is its own PR that runs the goldens (CODING_RULES.md
// §10), and changes this number.
static_assert(LIBJPEG_TURBO_VERSION_NUMBER == 3002000, "libjpeg-turbo 3.2.0, la comparada con la 6b");

namespace
{
// The original's 6b is built with BITS_IN_JSAMPLE 8 and stops on any other precision (JERR_BAD_PRECISION, its
// jdinput.c).
constexpr int SixBSampleBits = 8;
static_assert(SixBSampleBits == BITS_IN_JSAMPLE, "jpeg_read_scanlines da muestras de 8 bits");
} // namespace

namespace
{
// Freeing one that was never created, or never finished, does nothing extra: libjpeg checks whether it holds memory.
// The original builds libjpeg's error manager and its jump target as one struct: jpeg_error_mgr's 0x84 bytes and the
// jmp_buf after them (0x63b850 jumps to err + 0x84, 0063b490-terrainlight-ozj-loader.md §3). Here the jmp_buf is the
// creator's: 64-bit MSVC aligns it to 16, so that struct would get padding and MSVC warns (C4324, an error here).
struct Decompressor
{
    jpeg_decompress_struct info{};
    jpeg_error_mgr errors{};
    std::jmp_buf& jump;

    explicit Decompressor(std::jmp_buf& target) : jump(target)
    {
        // The error jumps to Decompress's setjmp, through the jmp_buf left in client_data (JpegErrors.h).
        info.err = jpeg_std_error(&errors);
        errors.error_exit = JpegJumpOnError;
        errors.output_message = JpegDiscardMessage;
        info.client_data = &jump;
    }
    ~Decompressor()
    {
        jpeg_destroy_decompress(&info);
    }
    Decompressor(const Decompressor&) = delete;
    Decompressor& operator=(const Decompressor&) = delete;
};

// Whether the original's 6b can read `info`, header already read. libjpeg-turbo reads two more things that 6b
// rejects (Q-C33): arithmetic coding (6b has no such decoder and jinit_master_decompress stops with
// JERR_ARITH_NOTIMPL), and lossless JPEG (SOF3; 6b stops reading the header with JERR_SOF_UNSUPPORTED). turbo has no
// public field for lossless, but there each data unit is one sample, not an 8 × 8 block: min_DCT_scaled_size is 1,
// and DCTSIZE in a DCT JPEG (its jdinput.c). Both reject samples of a precision other than 8 bits: 6b while reading
// the header (JERR_BAD_PRECISION), turbo with the 8-bit API only in jpeg_read_scanlines (its jdapistd.c). Here they
// are rejected before decoding.
bool SixBCanRead(const jpeg_decompress_struct& info)
{
    return info.data_precision == SixBSampleBits && !info.arith_code && info.min_DCT_scaled_size == DCTSIZE;
}

// The Huffman tables turbo fills with the standard ones if missing when it starts a sequential JPEG: 0 and 1, DC and
// AC, as for Motion JPEG, which lacks them (std_huff_tables, called by jinit_huff_decoder in its jdhuff.c).
constexpr std::size_t StandardHuffmanTables = 2;

// A Huffman table that cannot be built: three 1-bit codes, when 1 bit has two.
constexpr UINT8 ImpossibleOneBitCodes = 3;

// Keeps missing the Huffman tables turbo would fill (Q-C33). 6b does not fill them: a scan that asks for a missing
// one stops jpeg_make_d_derived_tbl with JERR_NO_HUFF_TABLE. turbo fills only NULL ones (add_huff_table, its
// jstdhuff.c), so here they get an impossible table: a later DHT overwrites it like any other, and a scan that asks
// for it without one stops jpeg_make_d_derived_tbl with JERR_BAD_HUFF_TABLE, where 6b would stop. In a progressive
// JPEG turbo fills nothing, and an impossible table stops just like a missing one.
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
            // libjpeg takes the decompressor as j_common_ptr, the start it shares with the compressor (jpeglib.h), and
            // passes it that way itself (jpeg_destroy_decompress, jdapimin.c). The table is libjpeg's:
            // jpeg_destroy_decompress frees it.
            *table = jpeg_alloc_huff_table(reinterpret_cast<j_common_ptr>(&info));
            std::ranges::fill((*table)->bits, UINT8{0});
            std::ranges::fill((*table)->huffval, UINT8{0});
            (*table)->bits[1] = ImpossibleOneBitCodes;
        }
    }
}

// With 2 samples or fewer, both libjpegs repeat samples even with the filter on (downsampled_width > 2, jdsample.c).
constexpr JDIMENSION NarrowestFancyWidth = 2;

// 6b uses the "fancy" filter (an interpolation) when a component doubles its width (h2v1 and h2v2) and has more than
// NarrowestFancyWidth samples, and repeats samples otherwise (int_upsample, its jdsample.c). libjpeg-turbo also uses
// it when only the height doubles (h1v2, 4:4:0). If no component doubles its width, turning the filter off makes
// turbo match 6b: h1v2 repeats rows and nothing else used it (Q-C33). With both kinds there is no way: turbo's stays.
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

// false if libjpeg stopped with an error before the last row, if 6b cannot read it or if the image exceeds `maxSide`.
// longjmp returns to setjmp without running destructors, and a live object with one between them would be undefined
// behavior (cppreference, std::longjmp). So none lives here: the decompressor, its jmp_buf and the image are the
// caller's, and only references and numbers sit between setjmp and libjpeg. Those that change after setjmp, like
// `row`, are indeterminate after the jump, but nobody reads them.
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

    // The size, before decoding: a progressive JPEG's jpeg_start_decompress already reads all its scans into an
    // image-sized buffer, and the header can claim 65,500 × 65,500 with a few bytes. The original checks it later, with
    // the same result: false. With the default parameters, jpeg_calc_output_dimensions gives start_decompress's size.
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

    // Here the original calls jpeg_finish_decompress, which reads to the end and stops on anything unexpected, like a
    // second SOI or an extra scan. But the image is already used: the texture is uploaded (glTexImage2D at 0x63bc89,
    // finish at 0x63bcd6) and the light written (0x63b7a2 to 0x63b80c, finish at 0x63b81b), and nobody checks the false
    // (Q-C32). What follows the last row changes nothing, so it is not read. In a multi-scan JPEG (a progressive one)
    // nothing is left: both libjpegs read to the end in jpeg_start_decompress and stop there for what finish would.
    return true;
}
} // namespace

Core::Expected<JpegImage, DataError> DecodeJpeg(std::span<const std::byte> jpeg, int maxSide)
{
    // jpeg_mem_src takes the size as unsigned long, 32 bits on Windows.
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
