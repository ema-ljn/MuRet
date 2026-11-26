#include "Diagnostics/CrashTexts.h"

#include "Core/Ascii.h"
#include "Diagnostics/CrashReport.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Client::Diagnostics
{
namespace
{
// Cut at 127 bytes, like the lstrcpynA that copies it (0x67f955, 0x67f99b, 0x67ff3c). Numbers go through to_chars: no
// locale and no memory, so the handler can use it after the crash.
class TextBuilder
{
public:
    void Append(std::string_view text)
    {
        const std::size_t count = std::min(MaxLength - m_length, text.size());
        std::copy_n(text.begin(), count, m_text.begin() + static_cast<std::ptrdiff_t>(m_length));
        m_length += count;
    }

    // Replaces the contents, like the original's std::string assign.
    void Assign(std::string_view text)
    {
        m_text.fill('\0');
        m_length = 0;
        Append(text);
    }

    // %d.
    void AppendDecimal(std::int64_t value)
    {
        std::array<char, DecimalDigits> digits{};
        const auto written = std::to_chars(digits.data(), digits.data() + digits.size(), value);
        Append(std::string_view(digits.data(), static_cast<std::size_t>(written.ptr - digits.data())));
    }

    // %08X: uppercase, at least 8 digits.
    void AppendHex(std::uint64_t value)
    {
        constexpr std::string_view HexDigits = "0123456789ABCDEF";
        constexpr std::size_t MinimumHexDigits = 8;
        constexpr unsigned BitsPerHexDigit = 4;
        std::array<char, HexDigitsOf64Bits> digits{};
        std::size_t count = 0;
        do
        {
            digits[count++] = HexDigits[static_cast<std::size_t>(value & 0xF)];
            value >>= BitsPerHexDigit;
        } while (value != 0);
        for (std::size_t padding = count; padding < MinimumHexDigits; ++padding)
        {
            Append("0");
        }
        for (std::size_t i = count; i > 0; --i)
        {
            Append(std::string_view(&digits[i - 1], 1));
        }
    }

    // %.2f.
    void AppendFixed2(double value)
    {
        constexpr int Decimals = 2;
        std::array<char, FixedDigits> digits{};
        const auto written =
            std::to_chars(digits.data(), digits.data() + digits.size(), value, std::chars_format::fixed, Decimals);
        Append(std::string_view(digits.data(), static_cast<std::size_t>(written.ptr - digits.data())));
    }

    [[nodiscard]] const CrashText& Text() const
    {
        return m_text;
    }

private:
    static constexpr std::size_t MaxLength = CrashTextSize - 1;
    static constexpr std::size_t DecimalDigits = 24;
    static constexpr std::size_t HexDigitsOf64Bits = 16;
    // A double in %.2f with up to 308 integer digits.
    static constexpr std::size_t FixedDigits = 320;

    CrashText m_text{};
    std::size_t m_length = 0;
};

// ---- The system, 0x683ed0 ----

// OSVERSIONINFO dwPlatformId: VER_PLATFORM_WIN32_WINDOWS (95, 98, Me) and VER_PLATFORM_WIN32_NT.
constexpr std::uint32_t PlatformWin32Windows = 1;
constexpr std::uint32_t PlatformWin32Nt = 2;

// The szCSDVersion[1] letter that names the edition: 'B' or 'C' on 95 (0x6844b0) and 'A' on 98 (0x6843f5).
char ServicePackLetter(std::string_view servicePack)
{
    return servicePack.size() > 1 ? servicePack[1] : '\0';
}

// The Windows 2000 ProductType (0x684220..0x6842ab), with _stricmp.
std::string_view ProductSuffix(std::string_view productType)
{
    if (Core::EqualsIgnoringAsciiCase(productType, "WINNT"))
    {
        return " Professional";
    }
    if (Core::EqualsIgnoringAsciiCase(productType, "LANMANNT"))
    {
        return " Server";
    }
    if (Core::EqualsIgnoringAsciiCase(productType, "SERVERNT"))
    {
        return " Advanced Server";
    }
    return {};
}

void AppendUnknownSystem(TextBuilder& text, const OsVersion& version)
{
    text.Append("Unknown ");
    text.AppendDecimal(static_cast<std::int32_t>(version.major));
    text.Append(".");
    text.AppendDecimal(static_cast<std::int32_t>(version.minor));
}

// Windows 95, 98 and Me: true, because their build has another format.
bool AppendVersion4(TextBuilder& text, const OsVersion& version)
{
    constexpr std::uint32_t Minor95 = 0;
    constexpr std::uint32_t Minor98 = 10;
    constexpr std::uint32_t MinorMe = 90;
    const char letter = ServicePackLetter(version.servicePack);
    if (version.minor == Minor95 && version.platformId == PlatformWin32Windows)
    {
        text.Append("Windows 95");
        text.Append(letter == 'B' || letter == 'C' ? " OSR2" : "");
        return true;
    }
    if (version.minor == Minor95 && version.platformId == PlatformWin32Nt)
    {
        text.Append("Windows NT 4.0");
        return false;
    }
    if (version.minor == Minor98)
    {
        text.Append("Windows 98");
        text.Append(letter == 'A' ? " SE" : "");
        return true;
    }
    if (version.minor == MinorMe)
    {
        text.Append("Windows Me");
        return true;
    }
    AppendUnknownSystem(text, version);
    return false;
}

void AppendVersion5(TextBuilder& text, const OsVersion& version)
{
    constexpr std::uint32_t Minor2000 = 0;
    constexpr std::uint32_t MinorXp = 1;
    constexpr std::uint32_t Minor2003 = 2;
    switch (version.minor)
    {
    case Minor2000:
        text.Append("Windows 2000");
        text.Append(ProductSuffix(version.productType));
        return;
    case MinorXp:
        text.Append("Windows XP");
        return;
    case Minor2003:
        text.Append("Windows 2003 family");
        return;
    default:
        AppendUnknownSystem(text, version);
        return;
    }
}

// The name by version (the switch at 0x683f4a). true for 95, 98 and Me.
bool AppendSystemName(TextBuilder& text, const OsVersion& version)
{
    constexpr std::uint32_t MajorNt3 = 3;
    constexpr std::uint32_t MinorNt351 = 51;
    constexpr std::uint32_t Major4 = 4;
    constexpr std::uint32_t Major5 = 5;
    if (version.major == MajorNt3 && version.minor == MinorNt351)
    {
        text.Append("Windows NT 3.51");
        return false;
    }
    if (version.major == Major4)
    {
        return AppendVersion4(text, version);
    }
    if (version.major == Major5)
    {
        AppendVersion5(text, version);
        return false;
    }
    AppendUnknownSystem(text, version);
    return false;
}

// " Build %d.%d.%d " with the build split on 95, 98 and Me, and " Build %d" with the whole one on the others
// (0x6845a6..0x6845f1).
void AppendBuild(TextBuilder& text, std::uint32_t build, bool windows9x)
{
    text.Append(" Build ");
    if (!windows9x)
    {
        text.AppendDecimal(static_cast<std::int32_t>(build));
        return;
    }
    constexpr unsigned MajorShift = 24;
    constexpr unsigned MinorShift = 16;
    constexpr std::uint32_t ByteMask = 0xFF;
    constexpr std::uint32_t WordMask = 0xFFFF;
    text.AppendDecimal((build >> MajorShift) & ByteMask);
    text.Append(".");
    text.AppendDecimal((build >> MinorShift) & ByteMask);
    text.Append(".");
    text.AppendDecimal(build & WordMask);
    text.Append(" ");
}

// ---- The processor, 0x684730 ----

// Leaf 0 EBX: "Genu" of GenuineIntel and "Auth" of AuthenticAMD (0x6847df, 0x6847eb).
constexpr std::uint32_t IntelVendorEbx = 0x756e6547;
constexpr std::uint32_t AmdVendorEbx = 0x68747541;

// Family and model from the signature, 4 bits each, without the extended ones (0x6847d1..0x6847dc).
constexpr unsigned FamilyShift = 8;
constexpr unsigned ModelShift = 4;
constexpr std::uint32_t NibbleMask = 0xF;

// The 12 vendor bytes, EBX, EDX and ECX, up to the first 0 (0x684753..0x684791).
void AppendVendor(TextBuilder& text, const CpuId& cpu)
{
    constexpr std::size_t VendorBytes = 12;
    constexpr unsigned BitsPerByte = 8;
    constexpr unsigned BitsPerWord = 32;
    std::array<char, VendorBytes> vendor{};
    const std::array words{cpu.vendorEbx, cpu.vendorEdx, cpu.vendorEcx};
    std::size_t length = 0;
    for (const std::uint32_t word : words)
    {
        for (unsigned shift = 0; shift < BitsPerWord; shift += BitsPerByte)
        {
            vendor[length++] = static_cast<char>((word >> shift) & 0xFF);
        }
    }
    const std::string_view all(vendor.data(), vendor.size());
    text.Append(all.substr(0, all.find('\0')));
    text.Append(" - ");
}

// Intel's table (0x684840): family 6 by model, 15 is the Pentium 4, and 3 to 5 say nothing.
std::string_view IntelModel(std::uint32_t family, std::uint32_t model)
{
    constexpr std::uint32_t FamilyP6 = 6;
    constexpr std::uint32_t FamilyNetBurst = 15;
    constexpr std::uint32_t FirstSilentFamily = 3;
    constexpr std::uint32_t LastSilentFamily = 5;
    if (family >= FirstSilentFamily && family <= LastSilentFamily)
    {
        return {};
    }
    if (family == FamilyNetBurst)
    {
        return "Pentium 4";
    }
    if (family != FamilyP6)
    {
        return "Unknown";
    }
    switch (model)
    {
    case 3:
    case 5:
        return "Pentium 2";
    case 6:
        return "Pentium Celeron";
    case 7:
    case 8:
    case 10:
    case 11:
        return "Pentium 3";
    default:
        return "Pentium Pro";
    }
}

std::string_view AmdFamily4(std::uint32_t model)
{
    switch (model)
    {
    case 3:
    case 7:
    case 8:
    case 9:
        return "AMD 486";
    case 14:
    case 15:
        return "AMD 586";
    default:
        return "AMD Unknown (486 or 586)";
    }
}

std::string_view AmdFamily5(std::uint32_t model)
{
    switch (model)
    {
    case 0:
    case 1:
    case 2:
    case 3:
        return "AMD K5 5k86";
    case 6:
    case 7:
        return "AMD K6";
    case 8:
        return "AMD K6-2";
    case 9:
        return "AMD K6-3";
    case 13:
        return "AMD K6-2+ or K6-3+";
    default:
        return "AMD Unknown (K5 or K6)";
    }
}

std::string_view AmdFamily6(std::uint32_t model)
{
    switch (model)
    {
    case 1:
    case 2:
    case 4:
    case 6:
        return "AMD K-7 Athlon";
    case 3:
    case 7:
        return "AMD K-7 Duron";
    default:
        return "AMD K-7 Unknown";
    }
}

// AMD's table (0x684930).
std::string_view AmdModel(std::uint32_t family, std::uint32_t model)
{
    constexpr std::uint32_t Family486 = 4;
    constexpr std::uint32_t FamilyK5K6 = 5;
    constexpr std::uint32_t FamilyK7 = 6;
    switch (family)
    {
    case Family486:
        return AmdFamily4(model);
    case FamilyK5K6:
        return AmdFamily5(model);
    case FamilyK7:
        return AmdFamily6(model);
    default:
        return "AMD Unknown";
    }
}

// " CPU %.2fGHz" above 1000 MHz and " CPU %dMHz" otherwise, with the low 32 bits (0x684d77..0x684db3).
void AppendSpeed(TextBuilder& text, std::int64_t megahertz)
{
    constexpr std::int64_t GigahertzFrom = 1000;
    constexpr double GigahertzPerMegahertz = 0.001;
    text.Append(" CPU ");
    if (megahertz > GigahertzFrom)
    {
        text.AppendFixed2(static_cast<double>(megahertz) * GigahertzPerMegahertz);
        text.Append("GHz");
        return;
    }
    text.AppendDecimal(static_cast<std::int32_t>(static_cast<std::uint32_t>(megahertz)));
    text.Append("MHz");
}

// ---- The exception, 0x680260 ----

struct ExceptionName
{
    std::uint32_t code = 0;
    std::string_view text;
};

// The access violation, which adds the address.
constexpr std::uint32_t AccessViolationCode = 0xC0000005;

// The codes the switch at 0x680260 and its two tables know (0x6807b4 and 0x680834).
constexpr std::array ExceptionNames{
    ExceptionName{.code = 0x80000002, .text = "Data type misalignment"},
    ExceptionName{.code = 0x80000003, .text = "Break point"},
    ExceptionName{.code = 0x80000004, .text = "Single step"},
    ExceptionName{.code = AccessViolationCode, .text = "Access violation"},
    ExceptionName{.code = 0xC0000006, .text = "In page error"},
    ExceptionName{.code = 0xC000001D, .text = "Illegal instruction"},
    ExceptionName{.code = 0xC0000025, .text = "Noncontinuable exception"},
    ExceptionName{.code = 0xC0000026, .text = "Invalid disposition"},
    ExceptionName{.code = 0xC000008C, .text = "Bounds exceeded"},
    ExceptionName{.code = 0xC000008D, .text = "Float denormal operand"},
    ExceptionName{.code = 0xC000008E, .text = "Float divide by zero"},
    ExceptionName{.code = 0xC000008F, .text = "Float inexact result"},
    ExceptionName{.code = 0xC0000090, .text = "Float invalid operation"},
    ExceptionName{.code = 0xC0000091, .text = "Float overflow"},
    ExceptionName{.code = 0xC0000092, .text = "Float stack check"},
    ExceptionName{.code = 0xC0000093, .text = "Float underflow"},
    ExceptionName{.code = 0xC0000094, .text = "Int divide by zero"},
    ExceptionName{.code = 0xC0000095, .text = "Int overflow"},
    ExceptionName{.code = 0xC0000096, .text = "Priv instruction"},
    ExceptionName{.code = 0xC00000FD, .text = "Stack overflow"},
};
} // namespace

CrashText SystemText(const OsVersion& version)
{
    TextBuilder text;
    const bool windows9x = AppendSystemName(text, version);
    AppendBuild(text, version.build, windows9x);
    if (!version.servicePack.empty())
    {
        text.Append("(");
        text.Append(version.servicePack);
        text.Append(")");
    }
    return text.Text();
}

CrashText PosixSystemText(std::string_view systemName, std::string_view release)
{
    TextBuilder text;
    text.Append(systemName);
    text.Append(" ");
    text.Append(release);
    return text.Text();
}

CrashText ProcessorText(const CpuId& cpu, std::int64_t megahertz)
{
    TextBuilder text;
    if (cpu.vendorEbx != 0)
    {
        AppendVendor(text, cpu);
    }
    const std::uint32_t family = (cpu.signature >> FamilyShift) & NibbleMask;
    const std::uint32_t model = (cpu.signature >> ModelShift) & NibbleMask;
    if (cpu.vendorEbx == AmdVendorEbx)
    {
        text.Append(AmdModel(family, model));
    }
    else if (cpu.vendorEbx == IntelVendorEbx)
    {
        text.Append(IntelModel(family, model));
    }
    else
    {
        // Another vendor: the whole text becomes "Unknown", without the vendor (0x6847f3).
        text.Assign("Unknown");
    }
    AppendSpeed(text, megahertz);
    return text.Text();
}

std::int64_t MegahertzFromSample(std::uint64_t cycles, std::int64_t ticks, std::int64_t ticksPerSecond)
{
    constexpr std::int64_t HertzPerMegahertz = 1000000;
    // What fits in an int64: __ftol (0x69fd3c) gives no useful value outside it.
    constexpr double Int64Limit = 9.2e18;
    if (ticks <= 0 || ticksPerSecond <= 0)
    {
        return 0;
    }
    // The two x87 divisions of 0x684fdc, with the cycles signed, as fild loads them.
    const double seconds = static_cast<double>(ticks) / static_cast<double>(ticksPerSecond);
    const double hertz = static_cast<double>(static_cast<std::int64_t>(cycles)) / seconds;
    if (!(std::fabs(hertz) < Int64Limit))
    {
        return 0;
    }
    return static_cast<std::int64_t>(hertz) / HertzPerMegahertz;
}

CrashText ExceptionText(std::uint32_t code, std::uint64_t accessKind, std::uint64_t accessAddress)
{
    TextBuilder text;
    const auto found = std::find_if(ExceptionNames.begin(), ExceptionNames.end(),
                                    [code](const ExceptionName& name) { return name.code == code; });
    text.Append(found != ExceptionNames.end() ? found->text : "Unknown exception");
    if (code == AccessViolationCode)
    {
        // 0 is a read; any other value, a write (0x6803a5).
        text.Append(accessKind == 0 ? " : reading inaccessible 0x" : " : writing inaccessible 0x");
        text.AppendHex(accessAddress);
    }
    return text.Text();
}

std::string CrashLogLine()
{
    return std::string(Core::LevelPrefix(Core::LogLevel::Error)).append(DumpSavedMessage);
}
} // namespace Client::Diagnostics
