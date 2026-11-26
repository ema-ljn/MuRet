#include "HostBoot.h"
#include "Observer.h"
#include "Protocol/Selection.h"
#include "SelectionGateway.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

using namespace MyDll::Boot;

namespace
{
const int Slot = 0x12c0;
const unsigned Stride = 0xd0c;
const DWORD NativeSendResult = 0x1284;
const DWORD RxBlocked = 0x80004005UL;
const DWORD TxBlocked = 1;

BootSession s_boot;
unsigned char s_objects[(Slot + 1) * Stride];
unsigned s_listCalls;
unsigned s_sendCalls;
unsigned s_receiveCalls;
DWORD s_lastLength;
DWORD s_lastProto;
DWORD s_lastPacket;
DWORD s_lastEncrypted;
DWORD s_lastSerial;
unsigned char s_sent[255];

void SetObject(DWORD connected, DWORD socket)
{
    const DWORD fields[] = {Slot, connected, 0, 0, socket};
    memcpy(s_objects + Slot * Stride, fields, sizeof(fields));
}

void __cdecl Receive(DWORD proto, DWORD packet, DWORD length, int index, DWORD encrypted, DWORD serial)
{
    assert(index == Slot);
    ++s_receiveCalls;
    s_lastProto = proto;
    s_lastPacket = packet;
    s_lastLength = length;
    s_lastEncrypted = encrypted;
    s_lastSerial = serial;
}

DWORD __cdecl Send(int index, char* packet, DWORD size)
{
    assert(index == Slot && size <= sizeof(s_sent));
    ++s_sendCalls;
    s_lastLength = size;
    memcpy(s_sent, packet, size);
    return NativeSendResult;
}

DWORD SendPacket(int index, unsigned char* packet, DWORD size)
{
    return s_boot.send(index, reinterpret_cast<char*>(packet), size);
}

void __cdecl ServeList(unsigned char* compact)
{
    ++s_listCalls;
    assert(compact[0] == 0xc2 && compact[2] == 78 && compact[6] == 1);
    assert(compact[40] == 0x83 && compact[44] == 0xff);
    unsigned char stock[31] = {0xc1, 31, 0xf3, 0, 1};
    stock[5 + 12] = 220;
    assert(SendPacket(Slot + 1, stock, sizeof(stock)) == TxBlocked);
    assert(SendPacket(Slot, stock, sizeof(stock)) == NativeSendResult);
}

DWORD ReceivePacket(DWORD proto, unsigned char* packet, DWORD length, DWORD encrypt, DWORD serial)
{
    return s_boot.receive(proto, reinterpret_cast<DWORD>(packet), length, Slot, encrypt, serial);
}

void LoginDelegatesWithoutRewritingArguments()
{
    unsigned char login[49] = {0xc1, 50, 0xf1, 1};
    assert(ReceivePacket(0x12f1, login, 48, 1, 0x43) == 0);
    assert(s_receiveCalls == 1 && s_lastProto == 0xf1 && s_lastLength == 48);
    assert(s_lastEncrypted == 1 && s_lastSerial == 0x43 && s_lastPacket == reinterpret_cast<DWORD>(login));
    assert(ReceivePacket(0x12f1, login, 48, 0, 0x43) == RxBlocked && s_receiveCalls == 1);
    assert(s_boot.receive(0xf1, reinterpret_cast<DWORD>(login), 48, Slot + 0x10000, 1, 0) == RxBlocked);
}

void ListContextOnlyLivesDuringNativeCall()
{
    unsigned char reply[102] = {0xc2, 0, 102, 1, 0xc0, 0x12, 1};
    memset(reply + 40, 0xff, 60);
    reply[40] = 0x83;
    unsigned char list[31] = {0xc1, 31, 0xf3, 0, 1};
    s_boot.characterList(1);
    s_boot.characterList(reinterpret_cast<DWORD>(reply));
    assert(s_listCalls == 0);
    SetObject(2, 5);
    assert(SendPacket(Slot, list, sizeof(list)) == TxBlocked);
    s_boot.characterList(reinterpret_cast<DWORD>(reply));
    assert(s_listCalls == 1 && s_lastLength == 41 && s_sent[4] == 3 && s_sent[6] == 1);
    assert(SendPacket(Slot, list, sizeof(list)) == TxBlocked);
    reply[2] = 101;
    s_boot.characterList(reinterpret_cast<DWORD>(reply));
    assert(s_listCalls == 1);
}

void CreationRequiresNativeSelectorAndCorrelatesSocket()
{
    unsigned char create[15] = {0xc1, 15, 0xf3, 1, 'X'};
    unsigned char reply[40] = {0xc1, 40, 0xf3, 1, 1, 'X'};
    unsigned char greeting[12] = {0xc1, 12, 0xf1, 0};
    create[14] = 0x40;
    const unsigned calls = s_receiveCalls;
    SetObject(1, 5);
    assert(ReceivePacket(0x12f3, create, sizeof(create), 0, MAXDWORD) == RxBlocked);
    SetObject(2, 5);
    assert(ReceivePacket(0x12f3, create, sizeof(create), 0, MAXDWORD) == 0);
    assert(s_lastPacket == reinterpret_cast<DWORD>(create) && s_receiveCalls == calls + 1);
    assert(ReceivePacket(0x12f3, create, sizeof(create), 0, MAXDWORD) == RxBlocked);
    SetObject(2, 6);
    assert(SendPacket(Slot, reply, sizeof(reply)) == TxBlocked);
    assert(SendPacket(Slot, greeting, sizeof(greeting)) == NativeSendResult);
    assert(ReceivePacket(0x12f3, create, sizeof(create), 0, MAXDWORD) == 0);
    reply[5] = 'Y';
    assert(SendPacket(Slot, reply, sizeof(reply)) == TxBlocked);
    reply[5] = 'X';
    assert(SendPacket(Slot, reply, sizeof(reply)) == NativeSendResult);
    assert(s_lastLength == 44 && s_sent[18] == 0x80 && s_sent[4] == 1);
    assert(SendPacket(Slot, reply, sizeof(reply)) == TxBlocked);
    assert(ReceivePacket(0x12f3, create, sizeof(create), 0, MAXDWORD) == 0);
    assert(SendPacket(Slot, greeting, sizeof(greeting)) == NativeSendResult);
    assert(SendPacket(Slot, reply, sizeof(reply)) == TxBlocked);
}

void ProfileObservationAndWireAreSeparate()
{
    char file[MAX_PATH];
    sprintf(file, "selection-fixture-%lu.tsv", GetCurrentProcessId());
    const ObserverOptions options = {true, true, true, true, true, 10};
    assert(OpenObserver(file, options));
    unsigned char join[14] = {0xc1, 14, 0xf3, 3};
    unsigned char result[5] = {0xc1, 5, 0xf1, 1, 1};
    assert(ReceivePacket(0xf3, join, sizeof(join), 0, MAXDWORD) == RxBlocked);
    assert(SendPacket(Slot, result, sizeof(result)) == NativeSendResult);
    assert(s_lastLength == sizeof(result) && !memcmp(s_sent, result, sizeof(result)));
    s_boot.wire(s_sent, sizeof(result));
    assert(s_sent[0] == 0x6b && s_sent[1] == 0xaf);
    CloseObserver();
    FILE* const log = fopen(file, "rb");
    assert(log);
    char text[1024];
    const size_t bytes = fread(text, 1, sizeof(text) - 1, log);
    fclose(log);
    assert(DeleteFileA(file));
    text[bytes] = '\0';
    assert(strstr(text, "\tRX\tBLOCKED_BY_PROFILE\tC1\tF3\t03\t14\tok\t0"));
    assert(strstr(text, "\tTX\tDELEGATED_TO_GS\tC1\tF1\t01\t5\tok\t0"));
}

void PublishesTheNativeWireBodyDirectly()
{
    assert(s_boot.wire == &MyDll::Protocol::XorWire);
}
} // namespace

int main()
{
    const SelectionHost host = {&Receive, &Send, &ServeList, s_objects};
    assert(BindSelection(s_boot, host, 220, 300));
    LoginDelegatesWithoutRewritingArguments();
    ListContextOnlyLivesDuringNativeCall();
    CreationRequiresNativeSelectorAndCorrelatesSocket();
    PublishesTheNativeWireBodyDirectly();
    ProfileObservationAndWireAreSeparate();
    return 0;
}
