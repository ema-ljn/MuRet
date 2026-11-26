#include "Selection.h"

#include <string.h>

namespace MyDll
{
namespace Protocol
{
namespace
{
enum
{
    FrameOffset = 0,
    SizeOffset = 1,
    HeadOffset = 2,
    SubcodeOffset = 3,
    HeaderBytes = 3,
    SubheaderBytes = 4,
    ClientCheckRequestBytes = 8,
    ClientCheckReplyBytes = 6,
    KeepaliveBytes = 12,
    LoginBytes = 49,
    GreetingBytes = 12,
    AccountResultBytes = 5,
    SecurityReportBytes = 6,
    CreateRequestName = 4,
    CreateSelector = 14,
    HighestSelector = 0x40,
    SelectorStep = 0x10,
    CreateResult = 4,
    CreateReplyName = 5,
    StockCreateEquipment = 16,
    ClientCreateLevel = 16,
    ClientCreateClass = 18,
    ClientCreateEquipment = 19,
    CreateEquipmentBytes = 24,
    Success = 1,
    NoticeExtraBytes = 9,
    WireXor = 0xaa
};

bool HasShortHeader(const unsigned char* packet, unsigned length)
{
    return packet && length >= HeaderBytes && length <= ShortFrameLimit && packet[FrameOffset] == ShortFrame;
}

bool IsCreateSelector(unsigned char value)
{
    return value <= HighestSelector && value % SelectorStep == 0;
}

Route AccountReceive(unsigned subcode, unsigned available, unsigned encrypted)
{
    switch (subcode)
    {
    case Login:
        return encrypted && available == LoginBytes ? Stock : Closed;
    case Close:
        return available == AccountResultBytes ? Stock : Closed;
    case SecurityReport:
        return available == SecurityReportBytes ? Stock : Closed;
    default:
        return Closed;
    }
}
} // namespace

Route ReceiveRoute(unsigned proto, const unsigned char* packet, unsigned length, unsigned encrypted)
{
    if (!HasShortHeader(packet, length) || encrypted > 1 || packet[SizeOffset] != length + 2 * encrypted ||
        static_cast<unsigned char>(proto) != packet[HeadOffset])
        return Closed;
    const unsigned available = length + encrypted;
    switch (packet[HeadOffset])
    {
    case ClientCheck:
        return available == ClientCheckRequestBytes ? Stock : Closed;
    case Keepalive:
        return available == KeepaliveBytes ? Stock : Closed;
    case Account:
        return available >= SubheaderBytes ? AccountReceive(packet[SubcodeOffset], available, encrypted) : Closed;
    case Character:
        if (available < SubheaderBytes)
            return Closed;
        if (available == SubheaderBytes)
        {
            if (packet[SubcodeOffset] == List)
                return RequestList;
            if (packet[SubcodeOffset] == LoginCompanion)
                return Stock;
        }
        if (!encrypted && length == CreateRequestBytes && packet[SubcodeOffset] == Create &&
            IsCreateSelector(packet[CreateSelector]))
            return RequestCreate;
        return Closed;
    default:
        return Closed;
    }
}

Route SendRoute(const unsigned char* packet, unsigned length)
{
    if (!HasShortHeader(packet, length) || packet[SizeOffset] != length)
        return Closed;
    switch (packet[HeadOffset])
    {
    case ClientCheck:
        return length == ClientCheckReplyBytes ? Stock : Closed;
    case Keepalive:
        return Stock;
    case Notice:
        return length >= SubheaderBytes && length <= ShortFrameLimit - NoticeExtraBytes ? SendNotice : Closed;
    case Account:
        if (length < SubheaderBytes)
            return Closed;
        if (packet[SubcodeOffset] == Greeting && length == GreetingBytes)
            return ResetAndSend;
        if ((packet[SubcodeOffset] == Login || packet[SubcodeOffset] == Close) && length == AccountResultBytes)
            return Stock;
        return Closed;
    case Character:
        if (length < SubheaderBytes)
            return Closed;
        if (packet[SubcodeOffset] == Create && length == StockCreateBytes)
            return SendCreate;
        if (packet[SubcodeOffset] == List && length >= StockListHeader && packet[StockListCount] <= MaxCharacters &&
            length == StockListHeader + static_cast<unsigned>(packet[StockListCount]) * StockListRow)
            return SendList;
        return Closed;
    default:
        return Closed;
    }
}

bool BeginCreate(CreateRequest& pending, const unsigned char* packet)
{
    if (pending.pending)
        return false;
    memcpy(pending.name, packet + CreateRequestName, NameBytes);
    pending.clientClass = static_cast<unsigned char>(packet[CreateSelector] * 2);
    pending.pending = true;
    return true;
}

bool ExpandCreate(CreateRequest& pending, const unsigned char* stock, unsigned char (&out)[ClientCreateBytes])
{
    if (!pending.pending || memcmp(pending.name, stock + CreateReplyName, NameBytes))
        return false;
    memcpy(out, stock, StockCreateEquipment);
    out[SizeOffset] = ClientCreateBytes;
    out[ClientCreateLevel] = 1;
    out[ClientCreateLevel + 1] = 0;
    out[ClientCreateClass] = pending.clientClass;
    if (stock[CreateResult] == Success)
        memcpy(out + ClientCreateEquipment, stock + StockCreateEquipment, CreateEquipmentBytes);
    else
        memset(out + ClientCreateEquipment, 0xff, CreateEquipmentBytes);
    out[ClientCreateBytes - 1] = 0;
    memset(&pending, 0, sizeof(pending));
    return true;
}

unsigned ExpandNotice(const unsigned char* stock, unsigned size, unsigned char (&out)[ShortFrameLimit])
{
    memcpy(out, stock, SubheaderBytes);
    const unsigned result = size + NoticeExtraBytes;
    out[SizeOffset] = static_cast<unsigned char>(result);
    memset(out + SubheaderBytes, 0, NoticeExtraBytes);
    memcpy(out + SubheaderBytes + NoticeExtraBytes, stock + SubheaderBytes, size - SubheaderBytes);
    return result;
}

void __stdcall XorWire(unsigned char* bytes, int length)
{
    for (int i = 0; i < length; ++i)
        bytes[i] ^= WireXor;
}
} // namespace Protocol
} // namespace MyDll
