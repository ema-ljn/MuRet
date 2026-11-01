// Lo que puede fallar al leer un archivo de Data\. Cada sub-ola de la CL1 suma lo suyo.

#pragma once

namespace Client::Data
{

enum class DataError
{
    FileTooShort, // el archivo tiene menos bytes de los que pide su formato
};

} // namespace Client::Data
