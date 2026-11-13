#include "Data/Textures/JpegErrors.h"

#include <csetjmp>
#include <cstdio> // jpeglib.h usa FILE y no incluye su header

#include <jpeglib.h>

extern "C"
{
    void JpegJumpOnError(j_common_ptr info)
    {
        std::longjmp(*static_cast<std::jmp_buf*>(info->client_data), 1);
    }

    void JpegDiscardMessage(j_common_ptr /*info*/) {}
}
