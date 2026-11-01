// Lo que puede fallar al leer un archivo de Data\. Cada sub-ola de la CL1 suma lo suyo.

#pragma once

namespace Client::Data
{

enum class DataError
{
    FileTooShort,     // el archivo tiene menos bytes de los que pide su formato
    ChecksumMismatch, // el checksum que guarda el archivo no es el de sus registros
    WrongWorld,       // el archivo dice ser de otro mundo que el que se pidió
    UnexpectedSize,   // el archivo no tiene ninguno de los tamaños que acepta su formato
    InvalidData,      // un valor del archivo no es uno de los que acepta su formato
};

} // namespace Client::Data
