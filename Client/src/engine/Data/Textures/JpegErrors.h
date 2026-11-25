#pragma once

#include <cstdio> // jpeglib.h usa FILE y no incluye su header

#include <jpeglib.h>

// Cómo cortan los errores de libjpeg en MuRet, lo comparten el decodificador (JpegImage.cpp) y el
// compresor de la captura (JpegWriter.cpp). El original deja el error_exit de jpeg_std_error, que
// muestra el mensaje y sale del proceso con exit(1); MuRet salta con longjmp al setjmp de quien
// llamó, que devuelve el error. libjpeg las llama como funciones de C, así que lo son: una función
// de C y una de C++ son de tipos distintos ([dcl.link]).
extern "C"
{
    // error_exit: libjpeg lo llama ante un error y no puede volver. Salta al jmp_buf que quien
    // llama dejó en client_data.
    [[noreturn]] void JpegJumpOnError(j_common_ptr info);

    // output_message: los mensajes no se muestran. En el original van al stderr de un programa de
    // Windows, que no se ve.
    void JpegDiscardMessage(j_common_ptr info);
}
