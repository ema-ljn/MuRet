#pragma once

#include <cstdio> // jpeglib.h uses FILE without including its header

#include <jpeglib.h>

// The original keeps jpeg_std_error's error_exit, which shows the message and exits with exit(1); MuRet longjmps to
// the caller's setjmp, which returns the error. libjpeg calls these as C functions, so they are: a C function and a
// C++ one have different types ([dcl.link]).
extern "C"
{
    // error_exit: libjpeg calls it on an error and it cannot return. Jumps to the jmp_buf the caller left in
    // client_data.
    [[noreturn]] void JpegJumpOnError(j_common_ptr info);

    // output_message: messages are not shown. In the original they go to a Windows program's stderr, which nobody sees.
    void JpegDiscardMessage(j_common_ptr info);
}
