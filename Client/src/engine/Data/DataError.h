#pragma once

namespace Client::Data
{
enum class DataError
{
    FileTooShort,
    ChecksumMismatch, // the checksum stored in the file does not match its records
    WrongWorld,       // the file belongs to a world other than the one requested
    UnexpectedSize,   // none of the sizes its format accepts
    InvalidData,      // a value its format does not accept
};
} // namespace Client::Data
