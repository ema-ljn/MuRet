#include "Selection.h"

#include <string.h>

namespace MyDll
{
namespace Protocol
{
namespace
{
// 04-character-selection § List bridge
enum
{
    DbListHead = 1,
    DbHeadOffset = 3,
    DbSlotOffset = 4,
    DbCountOffset = 6,
    DbEquipmentOffset = 16,
    DbItems = 12,
    DbItemBytes = 5,
    StockItemBytes = 3,
    DbTrailerBytes = 2,
    StockLevel = 12,
    ClientListHeader = 7,
    ClientListRow = 34,
    FirstWeapon = 16,
    SecondWeapon = 17,
    WingPreview = 20,
    ExtraWingPreview = 24,
    EmptyPreview = 0xff,
    NoItem = 0xffff,
    ExtendedItem = 0x200,
    SpecialWing = 0x1be,
    FirstSpecialPet = 0x1a4,
    SecondSpecialPet = 0x3a5
};

unsigned LittleWord(const unsigned char* address)
{
    return address[0] | (static_cast<unsigned>(address[1]) << 8);
}

void CompactRow(const unsigned char* source, unsigned char* out)
{
    memcpy(out, source, DbEquipmentOffset);
    for (unsigned i = 0; i < DbItems; ++i)
        memcpy(out + DbEquipmentOffset + i * StockItemBytes, source + DbEquipmentOffset + i * DbItemBytes,
               StockItemBytes);
    memcpy(out + StockDbRowBytes - DbTrailerBytes, source + DbRowBytes - DbTrailerBytes, DbTrailerBytes);
}

const unsigned char* ItemAt(const unsigned char* dbRow, unsigned slot)
{
    return dbRow + DbEquipmentOffset + DbItemBytes * slot;
}

unsigned ItemId(const unsigned char* dbRow, unsigned slot)
{
    const unsigned char* item = ItemAt(dbRow, slot);
    if (item[0] == EmptyPreview && item[2] == EmptyPreview && item[4] == EmptyPreview)
        return NoItem;
    return item[0] + (item[2] & 0x80) * 2 + ((item[4] & 0xf0) == 0x80 ? ExtendedItem : 0);
}

unsigned ItemExtension(const unsigned char* dbRow, unsigned slot)
{
    const unsigned item = ItemId(dbRow, slot);
    return item == NoItem ? 0x0f : (item >= ExtendedItem ? 1 : 0);
}

unsigned char SplitWeapon(unsigned char& weapon, unsigned char emptyLow)
{
    if (weapon == EmptyPreview)
    {
        weapon = emptyLow;
        return 0xf0;
    }
    const unsigned char high = static_cast<unsigned char>(weapon & 0xe0);
    weapon &= 0x1f;
    return high;
}

void ExpandPreview(const unsigned char* dbRow, unsigned char* row)
{
    if (ItemId(dbRow, 7) == SpecialWing)
    {
        row[ExtraWingPreview] |= 0x05;
        row[WingPreview] |= 0x0f;
    }
    const unsigned pet = ItemId(dbRow, 8);
    unsigned char* extra = row + StockListRow;
    extra[0] = 0;
    extra[1] = SplitWeapon(row[FirstWeapon], EmptyPreview);
    if (pet == FirstSpecialPet || pet == SecondSpecialPet)
        extra[1] = static_cast<unsigned char>((extra[1] & 0xf0) | (pet == FirstSpecialPet ? 1 : 4));
    extra[2] = static_cast<unsigned char>(SplitWeapon(row[SecondWeapon], 0xf0) | ItemExtension(dbRow, 2));
    extra[3] = static_cast<unsigned char>((ItemExtension(dbRow, 3) << 4) | ItemExtension(dbRow, 4));
    extra[4] = static_cast<unsigned char>((ItemExtension(dbRow, 5) << 4) | ItemExtension(dbRow, 6));
    unsigned petVariant = pet == SecondSpecialPet ? ItemAt(dbRow, 8)[2] & 0x0f : 0;
    if (petVariant > 2)
        petVariant = 0;
    extra[5] = static_cast<unsigned char>((ItemExtension(dbRow, 1) << 4) | petVariant);
    extra[6] = 0;
    extra[7] = EmptyPreview;
}
} // namespace

unsigned DbListSize(const unsigned char* header)
{
    if (header[0] != WideFrame || header[DbHeadOffset] != DbListHead || header[DbCountOffset] > MaxCharacters)
        return 0;
    const unsigned size = DbListHeaderBytes + static_cast<unsigned>(header[DbCountOffset]) * DbRowBytes;
    return size == (static_cast<unsigned>(header[1]) << 8) + header[2] ? size : 0;
}

unsigned DbListSlot(const unsigned char* header)
{
    return LittleWord(header + DbSlotOffset);
}

void CompactDbList(const unsigned char* source, unsigned char (&out)[MaxStockDbListBytes])
{
    const unsigned count = source[DbCountOffset];
    const unsigned size = DbListHeaderBytes + count * StockDbRowBytes;
    memcpy(out, source, DbListHeaderBytes);
    out[1] = static_cast<unsigned char>(size >> 8);
    out[2] = static_cast<unsigned char>(size);
    for (unsigned i = 0; i < count; ++i)
        CompactRow(source + DbListHeaderBytes + i * DbRowBytes, out + DbListHeaderBytes + i * StockDbRowBytes);
}

unsigned ExpandList(const unsigned char* stock, const unsigned char* source, unsigned mgLevel, unsigned dlLevel,
                    unsigned char (&out)[ShortFrameLimit])
{
    const unsigned count = stock[StockListCount];
    if (count != source[DbCountOffset])
        return 0;
    unsigned best = 0;
    for (unsigned i = 0; i < count; ++i)
    {
        unsigned char* row = out + ClientListHeader + i * ClientListRow;
        memcpy(row, stock + StockListHeader + i * StockListRow, StockListRow);
        const unsigned level = LittleWord(row + StockLevel);
        if (level > best)
            best = level;
        ExpandPreview(source + DbListHeaderBytes + i * DbRowBytes, row);
    }
    const unsigned size = ClientListHeader + count * ClientListRow;
    memcpy(out, stock, StockListCount);
    out[1] = static_cast<unsigned char>(size);
    out[4] = static_cast<unsigned char>(best >= dlLevel ? 4 : (best >= mgLevel ? 3 : 2));
    out[5] = 0;
    out[6] = static_cast<unsigned char>(count);
    return size;
}
} // namespace Protocol
} // namespace MyDll
