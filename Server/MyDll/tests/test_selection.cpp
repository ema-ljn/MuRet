#include "Protocol/Selection.h"

#include <assert.h>
#include <string.h>

using namespace MyDll::Protocol;

namespace
{
void AuthUsesNativeFrameShape()
{
    unsigned char login[49] = {0xc1, 50, 0xf1, 1};
    assert(ReceiveRoute(0x12f1, login, 48, 1) == Stock);
    assert(ReceiveRoute(0xf1, login, 48, 0) == Closed);
    assert(ReceiveRoute(0xf1, login, 47, 1) == Closed);
    login[1] = 49;
    assert(ReceiveRoute(0xf1, login, sizeof(login), 0) == Closed);
    const unsigned char authReply[] = {0xc1, 5, 0xf1, 1, 0};
    assert(SendRoute(authReply, sizeof(authReply)) == Stock);
}

void OnlySelectorOperationsAreOpened()
{
    const unsigned char list[] = {0xc1, 4, 0xf3, 0};
    const unsigned char companion[] = {0xc1, 4, 0xf3, 0x7a};
    const unsigned char join[] = {0xc1, 14, 0xf3, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const unsigned char keepalive[12] = {0xc1, 12, 0x0e, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    assert(ReceiveRoute(0x13f3, list, sizeof(list), 0) == RequestList);
    assert(ReceiveRoute(0xf3, companion, sizeof(companion), 0) == Stock);
    assert(ReceiveRoute(0xf3, join, sizeof(join), 0) == Closed);
    assert(ReceiveRoute(0x0e, keepalive, sizeof(keepalive), 0) == Stock);
    assert(ReceiveRoute(0x0e, keepalive, 2, 0) == Closed);
    assert(ReceiveRoute(0xf3, 0, sizeof(list), 0) == Closed);
    assert(ReceiveRoute(0xf2, list, sizeof(list), 0) == Closed);
}

void CreateCorrelatesAndConsumesExactlyOneReply()
{
    unsigned char request[15] = {0xc1, 15, 0xf3, 1, 'T', 'e', 's', 't'};
    unsigned char stock[40] = {0xc1, 40, 0xf3, 1, 1, 'T', 'e', 's', 't'};
    unsigned char out[44];
    memset(stock + 16, 0x67, 24);
    for (unsigned selector = 0; selector <= 0x40; selector += 0x10)
    {
        CreateRequest pending = {{0}, 0, false};
        request[14] = static_cast<unsigned char>(selector);
        unsigned char original[15];
        memcpy(original, request, sizeof(original));
        assert(ReceiveRoute(0x12f3, request, sizeof(request), 0) == RequestCreate);
        assert(BeginCreate(pending, request));
        assert(!BeginCreate(pending, request));
        assert(!memcmp(request, original, sizeof(original)));
        assert(SendRoute(stock, sizeof(stock)) == SendCreate);
        stock[5] = 'X';
        memset(out, 0xac, sizeof(out));
        assert(!ExpandCreate(pending, stock, out));
        assert(out[0] == 0xac && pending.pending);
        stock[5] = 'T';
        assert(ExpandCreate(pending, stock, out));
        assert(out[1] == 44 && out[16] == 1 && out[17] == 0 && out[18] == selector * 2);
        assert(!memcmp(out + 19, stock + 16, 24) && out[43] == 0);
        assert(!ExpandCreate(pending, stock, out));
    }
    request[14] = 0x31;
    assert(ReceiveRoute(0xf3, request, sizeof(request), 0) == Closed);
    request[14] = 0x30;
    request[1] = 16;
    assert(ReceiveRoute(0xf3, request, 14, 1) == Closed);
}

void FailureDoesNotCopyUninitializedEquipment()
{
    const unsigned char request[15] = {0xc1, 15, 0xf3, 1, 'X', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    unsigned char stock[40] = {0xc1, 40, 0xf3, 1, 0, 'X'};
    memset(stock + 16, 0x67, 24);
    CreateRequest pending = {{0}, 0, false};
    unsigned char out[44];
    assert(BeginCreate(pending, request));
    assert(ExpandCreate(pending, stock, out) && out[4] == 0);
    for (unsigned i = 19; i < 43; ++i)
        assert(out[i] == 0xff);
}

void DataServerListIsCompactedBeforeNativeList()
{
    unsigned char source[102] = {0xc2, 0, 102, 1, 0xc1, 0x12, 1};
    unsigned char compact[MaxStockDbListBytes];
    memset(compact, 0xcc, sizeof(compact));
    for (unsigned i = 24; i < sizeof(source); ++i)
        source[i] = static_cast<unsigned char>(i);
    assert(DbListSize(source) == sizeof(source) && DbListSlot(source) == 0x12c1);
    CompactDbList(source, compact);
    assert(compact[1] == 0 && compact[2] == 78);
    assert(!memcmp(compact + 24, source + 24, 16));
    for (unsigned item = 0; item < 12; ++item)
        assert(!memcmp(compact + 40 + 3 * item, source + 40 + 5 * item, 3));
    assert(compact[76] == source[100] && compact[77] == source[101] && compact[78] == 0xcc);
    source[6] = 6;
    assert(DbListSize(source) == 0);
    source[6] = 1;
    source[2] = 101;
    assert(DbListSize(source) == 0);
}

void CharacterListPreservesStockDataAndExtendsPreview()
{
    unsigned char source[102] = {0xc2, 0, 102, 1, 0xc0, 0x12, 1};
    unsigned char list[31] = {0xc1, 31, 0xf3, 0, 1};
    unsigned char out[ShortFrameLimit];
    memset(source + 40, 0xff, 60);
    list[5 + 16] = 0xff;
    list[5 + 17] = 0xff;
    assert(SendRoute(list, sizeof(list)) == SendList);
    assert(ExpandList(list, source, 220, 300, out) == 41);
    const unsigned char emptyPreview[] = {0, 0xf0, 0xff, 0xff, 0xff, 0xf0, 0, 0xff};
    assert(!memcmp(out + 33, emptyPreview, sizeof(emptyPreview)));
    assert(out[4] == 2 && out[6] == 1);
    list[5 + 12] = 220;
    assert(ExpandList(list, source, 220, 300, out) == 41 && out[4] == 3);
    list[5 + 12] = 44;
    list[5 + 13] = 1;
    assert(ExpandList(list, source, 220, 300, out) == 41 && out[4] == 4);
    const unsigned char emptyDb[] = {0xc2, 0, 24, 1, 0xc0, 0x12, 0};
    const unsigned char emptyList[] = {0xc1, 5, 0xf3, 0, 0};
    assert(ExpandList(emptyList, emptyDb, 220, 300, out) == 7 && out[6] == 0);
    assert(ExpandList(list, emptyDb, 220, 300, out) == 0);
}

void NoticeAndWireTransformHaveGoldenBytes()
{
    const unsigned char notice[] = {0xc1, 6, 0x0d, 0x01, '!', 0};
    const unsigned char expected[] = {0x6b, 0xa5, 0xa7, 0xab, 0xaa, 0xaa, 0xaa, 0xaa,
                                      0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0x8b, 0xaa};
    unsigned char out[ShortFrameLimit];
    assert(SendRoute(notice, sizeof(notice)) == SendNotice);
    const unsigned length = ExpandNotice(notice, sizeof(notice), out);
    assert(length == sizeof(expected));
    XorWire(out, static_cast<int>(length));
    assert(!memcmp(out, expected, sizeof(expected)));
    XorWire(out, -1);
    XorWire(0, 0);
    assert(!memcmp(out, expected, sizeof(expected)));
}
} // namespace

int main()
{
    AuthUsesNativeFrameShape();
    OnlySelectorOperationsAreOpened();
    CreateCorrelatesAndConsumesExactlyOneReply();
    FailureDoesNotCopyUninitializedEquipment();
    DataServerListIsCompactedBeforeNativeList();
    CharacterListPreservesStockDataAndExtendsPreview();
    NoticeAndWireTransformHaveGoldenBytes();
    return 0;
}
