#pragma once

// La hora local, lo que el original lee de GetLocalTime (el SYSTEMTIME) y usa: el nombre de la captura de pantalla
// (0x6381c7). La lee Platform (Platform/LocalTime.h).
namespace Client::Core
{
struct LocalTime
{
    // wMonth, de 1 a 12.
    int month = 1;
    // wDay, de 1 a 31.
    int day = 1;
    // wHour, de 0 a 23.
    int hour = 0;
    // wMinute, de 0 a 59.
    int minute = 0;
};
} // namespace Client::Core
