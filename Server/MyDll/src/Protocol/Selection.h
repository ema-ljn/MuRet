#pragma once

namespace MyDll
{
namespace Protocol
{
// 04-character-selection § Packet contracts
enum
{
    ShortFrame = 0xc1,
    WideFrame = 0xc2,
    ClientCheck = 0x03,
    Notice = 0x0d,
    Keepalive = 0x0e,
    Account = 0xf1,
    Character = 0xf3,
    List = 0,
    Create = 1,
    Greeting = 0,
    Login = 1,
    Close = 2,
    SecurityReport = 3,
    LoginCompanion = 0x7a,
    NameBytes = 10,
    CreateRequestBytes = 15,
    StockCreateBytes = 40,
    ClientCreateBytes = 44,
    ShortFrameLimit = 255,
    MaxCharacters = 5,
    StockListHeader = 5,
    StockListCount = 4,
    StockListRow = 26,
    DbListHeaderBytes = 24,
    DbRowBytes = 78,
    StockDbRowBytes = 54,
    MaxDbListBytes = DbListHeaderBytes + MaxCharacters * DbRowBytes,
    MaxStockDbListBytes = DbListHeaderBytes + MaxCharacters * StockDbRowBytes
};

enum Route
{
    Closed,
    Stock,
    ResetAndSend,
    RequestList,
    RequestCreate,
    SendList,
    SendCreate,
    SendNotice
};

struct CreateRequest
{
    unsigned char name[NameBytes];
    unsigned char clientClass;
    bool pending;
};

Route ReceiveRoute(unsigned proto, const unsigned char* packet, unsigned length, unsigned encrypted);
Route SendRoute(const unsigned char* packet, unsigned length);
// Call transforms only after admitting the source frame.
bool BeginCreate(CreateRequest& pending, const unsigned char* packet);
bool ExpandCreate(CreateRequest& pending, const unsigned char* stock, unsigned char (&out)[ClientCreateBytes]);
unsigned DbListSize(const unsigned char* header);
unsigned DbListSlot(const unsigned char* header);
void CompactDbList(const unsigned char* source, unsigned char (&out)[MaxStockDbListBytes]);
unsigned ExpandList(const unsigned char* stock, const unsigned char* source, unsigned mgLevel, unsigned dlLevel,
                    unsigned char (&out)[ShortFrameLimit]);
unsigned ExpandNotice(const unsigned char* stock, unsigned size, unsigned char (&out)[ShortFrameLimit]);
void __stdcall XorWire(unsigned char* bytes, int length);
} // namespace Protocol
} // namespace MyDll
