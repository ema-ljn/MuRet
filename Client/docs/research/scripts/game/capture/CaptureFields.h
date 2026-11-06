#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <span>
#include <type_traits>

// Whitelist del main.exe c32bbd5f...: research 06, cámara y observación CL7.
namespace Client::Capture
{
enum class FieldFormat
{
    Words,
    Bytes
};

struct FieldSpec
{
    const char* name;
    DWORD address;
    SIZE_T size;
    FieldFormat format = FieldFormat::Words;
};

inline constexpr std::size_t MaxFieldBytes = 12;
inline constexpr std::size_t SignatureBytes = 16;
inline constexpr DWORD CameraSignatureAddress = 0x00635ae0;
inline constexpr DWORD InverseSignatureAddress = 0x005fcc30;
inline constexpr DWORD MapAddress = 0x006c9dd0;
inline constexpr DWORD SceneAddress = 0x006d5dc8;
inline constexpr DWORD HeroPointerAddress = 0x0743a170;
inline constexpr DWORD HeroPositionOffset = 0x10;
inline constexpr std::size_t HeroPositionBytes = 12;
inline constexpr std::size_t HeroPathBytes = 33;
inline constexpr std::size_t CameraFieldCount = 15;
inline constexpr std::size_t HeroPositionField = 0;

// Exactamente 16 bytes por función, no una identidad del ejecutable completo.
inline constexpr std::array<unsigned char, SignatureBytes> CameraSignature{
    0x55, 0x8b, 0xec, 0x83, 0xec, 0x58, 0xa0, 0x99, 0x68, 0xd3, 0x07, 0xc7, 0x05, 0x48, 0x5d, 0x6d};
inline constexpr std::array<unsigned char, SignatureBytes> InverseSignature{
    0x55, 0x8b, 0xec, 0x8b, 0x4d, 0x0c, 0x8b, 0x45, 0x08, 0x8b, 0x55, 0x10, 0xd9, 0x41, 0x20, 0xd8};

inline constexpr std::array<FieldSpec, 22> SeriesFields{{
    {"map", MapAddress, 4},         {"scene", SceneAddress, 4},        {"worldTime", 0x05767b88, 4},
    {"angles", 0x07d36868, 12},     {"position", 0x07d36884, 12},      {"zoom", 0x07d3689c, 4},
    {"distance", 0x07d36ba4, 4},    {"targetDistance", 0x006d5ee0, 4}, {"farPlane", 0x006d5d44, 4},
    {"nearPlane", 0x006d5d40, 4},   {"fieldOfView", 0x006d5d48, 4},    {"fixedHeight", 0x006c9d5c, 4},
    {"level", 0x07d3a0ec, 2},       {"effectTarget", 0x00724588, 12},  {"shake", 0x07d325c8, 4},
    {"renderSize", 0x006d5d60, 8},  {"tourState", 0x006e58fc, 4},      {"cullingLevel", 0x07b4ccb8, 4},
    {"mouseUpdate", 0x077f3b10, 4}, {"mouseUpdateMax", 0x006c8dcc, 4}, {"pubLayer", 0x077f36d0, 4},
    {"storm", 0x00723c50, 4},
}};
inline constexpr auto CameraFields = std::span(SeriesFields).first<CameraFieldCount>();

// research 06 entrada/caminar: ángulos +1c/+20/+24; camino cur/sub/n,X[15],Y[15].
// No nombre +1d9 ni celdas protegidas.
inline constexpr std::array<FieldSpec, 7> HeroFields{{
    {"heroPosition", HeroPositionOffset, HeroPositionBytes},
    {"heroAngles", 0x1c, 12},
    {"heroAction", 0x111, 1},
    {"heroSafe", 0x3e2, 1},
    {"heroMovement", 0x378, 1},
    {"heroMovementType", 0x379, 1},
    {"heroPath", 0x3e8, HeroPathBytes, FieldFormat::Bytes},
}};

constexpr bool FieldsFit(std::span<const FieldSpec> fields, std::size_t capacity)
{
    for (const auto& field : fields)
        if (field.size == 0 || field.size > capacity)
            return false;
    return true;
}
static_assert(FieldsFit(SeriesFields, MaxFieldBytes));
static_assert(FieldsFit(HeroFields, HeroPathBytes));
static_assert(HeroFields[HeroPositionField].address == HeroPositionOffset);

inline bool ParsePid(const char* text, DWORD& pid)
{
    DWORD value = 0;
    if (*text == '\0')
        return false;
    for (; *text != '\0'; ++text)
    {
        if (*text < '0' || *text > '9')
            return false;
        const auto digit = static_cast<DWORD>(*text - '0');
        if (value > (MAXDWORD - digit) / 10)
            return false;
        value = value * 10 + digit;
    }
    if (value == 0)
        return false;
    pid = value;
    return true;
}

// La clase MU la registra el cliente (02 § arranque, RegisterClassA). No enumera procesos.
inline bool MuWindowPid(DWORD& pid)
{
    HWND window = FindWindowA("MU", nullptr);
    if (window == nullptr)
    {
        std::puts("error field=FindWindowA reason=mu_window_not_found");
        return false;
    }
    if (GetWindowThreadProcessId(window, &pid) == 0 || pid == 0)
    {
        std::puts("error field=GetWindowThreadProcessId reason=pid_unavailable");
        return false;
    }
    return true;
}

inline bool ReadExact(HANDLE process, DWORD address, std::span<unsigned char> output, const char* field)
{
    SIZE_T bytesRead = 0;
    const BOOL success = ReadProcessMemory(process, reinterpret_cast<LPCVOID>(static_cast<ULONG_PTR>(address)),
                                           output.data(), output.size(), &bytesRead);
    if (!success || bytesRead != output.size())
    {
        const DWORD error = success ? ERROR_PARTIAL_COPY : GetLastError();
        std::printf("error field=%s address=%08lx requested=%08lx read=%08lx win32=%08lx\n", field,
                    static_cast<unsigned long>(address), static_cast<unsigned long>(output.size()),
                    static_cast<unsigned long>(bytesRead), static_cast<unsigned long>(error));
        return false;
    }
    return true;
}

template <typename T> inline bool ReadValue(HANDLE process, DWORD address, T& output, const char* field)
{
    static_assert(std::is_trivially_copyable_v<T>);
    return ReadExact(process, address, {reinterpret_cast<unsigned char*>(&output), sizeof(output)}, field);
}

inline bool MatchSignature(HANDLE process, DWORD address, std::span<const unsigned char, SignatureBytes> expected,
                           const char* field)
{
    std::array<unsigned char, SignatureBytes> actual{};
    if (!ReadExact(process, address, actual, field))
        return false;
    if (std::memcmp(actual.data(), expected.data(), actual.size()) != 0)
    {
        std::printf("error field=%s address=%08lx reason=mismatch\n", field, static_cast<unsigned long>(address));
        return false;
    }
    return true;
}

inline bool MatchCameraSignatures(HANDLE process)
{
    return MatchSignature(process, CameraSignatureAddress, CameraSignature, "camera_signature") &&
           MatchSignature(process, InverseSignatureAddress, InverseSignature, "inverse_signature");
}

// Palabras little-endian impresas como bits numéricos, sin convertir los floats.
inline void PrintField(const char* name, std::span<const unsigned char> bytes)
{
    std::printf("%s=", name);
    constexpr std::size_t WordBytes = 4;
    for (std::size_t offset = 0; offset < bytes.size(); offset += WordBytes)
    {
        const auto width = bytes.size() - offset < WordBytes ? bytes.size() - offset : WordBytes;
        if (offset != 0)
            std::putchar(',');
        for (std::size_t remaining = width; remaining != 0; --remaining)
            std::printf("%02x", static_cast<unsigned int>(bytes[offset + remaining - 1]));
    }
    std::putchar('\n');
}

inline void PrintBytes(const char* name, std::span<const unsigned char> bytes)
{
    std::printf("%s=", name);
    for (std::size_t index = 0; index < bytes.size(); ++index)
        std::printf("%s%02x", index != 0 ? "," : "", static_cast<unsigned int>(bytes[index]));
    std::putchar('\n');
}

inline bool AddressFits(DWORD base, const FieldSpec& field)
{
    return base <= MAXDWORD - field.address - static_cast<DWORD>(field.size - 1);
}

template <std::size_t Count, std::size_t Bytes = MaxFieldBytes>
using FieldValues = std::array<std::array<unsigned char, Bytes>, Count>;

template <std::size_t Count, std::size_t Bytes>
bool ReadFields(HANDLE process, std::span<const FieldSpec> fields, FieldValues<Count, Bytes>& values, DWORD base = 0)
{
    if (fields.size() > values.size() || !FieldsFit(fields, Bytes))
        return false;
    for (std::size_t index = 0; index < fields.size(); ++index)
    {
        const auto& field = fields[index];
        if (!AddressFits(base, field))
        {
            std::printf("error field=%s reason=address_overflow\n", field.name);
            return false;
        }
        if (!ReadExact(process, base + field.address, std::span(values[index]).first(field.size), field.name))
            return false;
    }
    return true;
}

template <std::size_t Count, std::size_t Bytes>
void PrintFields(std::span<const FieldSpec> fields, const FieldValues<Count, Bytes>& values)
{
    for (std::size_t index = 0; index < fields.size(); ++index)
    {
        const auto bytes = std::span(values[index]).first(fields[index].size);
        if (fields[index].format == FieldFormat::Bytes)
            PrintBytes(fields[index].name, bytes);
        else
            PrintField(fields[index].name, bytes);
    }
}

template <typename T, std::size_t Bytes> T FieldValue(const std::array<unsigned char, Bytes>& bytes)
{
    static_assert(std::is_trivially_copyable_v<T> && sizeof(T) <= Bytes);
    T value{};
    std::memcpy(&value, bytes.data(), sizeof(value));
    return value;
}
} // namespace Client::Capture
