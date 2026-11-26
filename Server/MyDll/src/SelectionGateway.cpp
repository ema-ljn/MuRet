#include "SelectionGateway.h"
#include "ExecutableDirectory.h"
#include "HostBoot.h"
#include "Observer.h"
#include "Protocol/Selection.h"

#include <excpt.h>
#include <string.h>

namespace MyDll
{
namespace Boot
{
namespace
{
using namespace Protocol;
const int FirstPlayer = 0x12c0;
const int EndPlayer = 0x15de;
const unsigned ObjectStride = 0xd0c;
const unsigned ObjectIndex = 0;
const unsigned ObjectConnected = 4;
const unsigned ObjectSocket = 0x10;
const DWORD OnSelector = 2;
const DWORD NoSocket = MAXDWORD;
const DWORD ReceiveClosed = 0x80004005UL;
const DWORD SendConsumed = 1;
const char IniName[] = "mx_config.ini";
const unsigned DefaultMgLevel = 220;
const unsigned DefaultDlLevel = 300;

struct CreateSlot
{
    CreateRequest request;
    DWORD socket;
};

struct ListContext
{
    const unsigned char* source;
    int slot;
};

SelectionHost s_stockHost;
CreateSlot s_creates[EndPlayer - FirstPlayer];
CRITICAL_SECTION s_createLock;
DWORD s_listTls = TLS_OUT_OF_INDEXES;
unsigned s_mgLevel;
unsigned s_dlLevel;

class CreateLock
{
public:
    CreateLock()
    {
        EnterCriticalSection(&s_createLock);
    }
    ~CreateLock()
    {
        LeaveCriticalSection(&s_createLock);
    }

private:
    CreateLock(const CreateLock&);
    CreateLock& operator=(const CreateLock&);
};

bool IsPlayer(int index)
{
    return index >= FirstPlayer && index < EndPlayer;
}

DWORD ObjectField(const unsigned char* object, unsigned offset)
{
    DWORD value;
    memcpy(&value, object + offset, sizeof(value));
    return value;
}

DWORD SelectorSocket(int index)
{
    const unsigned char* object = s_stockHost.objects + static_cast<unsigned>(index) * ObjectStride;
    if (ObjectField(object, ObjectIndex) != static_cast<DWORD>(index) ||
        ObjectField(object, ObjectConnected) != OnSelector)
        return NoSocket;
    return ObjectField(object, ObjectSocket);
}

bool SaveCreate(int index, const unsigned char* packet)
{
    CreateLock lock;
    const DWORD socket = SelectorSocket(index);
    if (socket == NoSocket)
        return false;
    CreateSlot& slot = s_creates[index - FirstPlayer];
    if (slot.socket != socket)
        memset(&slot, 0, sizeof(slot));
    if (!BeginCreate(slot.request, packet))
        return false;
    slot.socket = socket;
    return true;
}

bool TakeCreate(int index, const unsigned char* packet, unsigned char (&out)[ClientCreateBytes])
{
    CreateLock lock;
    const DWORD socket = SelectorSocket(index);
    CreateSlot& slot = s_creates[index - FirstPlayer];
    if (socket == NoSocket || slot.socket != socket)
        return false;
    return ExpandCreate(slot.request, packet, out);
}

bool MayReceive(Route route, int index, const unsigned char* packet)
{
    switch (route)
    {
    case Stock:
        return true;
    case RequestList:
        return SelectorSocket(index) != NoSocket;
    case RequestCreate:
        return SaveCreate(index, packet);
    default:
        return false;
    }
}

DWORD __cdecl ReceiveSelection(DWORD proto, DWORD packet, DWORD length, int index, DWORD encrypted, DWORD serial)
{
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(packet);
    const bool delegate = IsPlayer(index) && MayReceive(ReceiveRoute(proto, bytes, length, encrypted), index, bytes);
    RecordObservation(true, packet, length, delegate ? Delegated : BlockedByProfile);
    if (!delegate)
        return ReceiveClosed;
    s_stockHost.receive(static_cast<unsigned char>(proto), packet, length, index, encrypted, serial);
    return 0;
}

ListContext* CurrentList()
{
    const DWORD lastError = GetLastError();
    ListContext* const context = static_cast<ListContext*>(TlsGetValue(s_listTls));
    SetLastError(lastError);
    return context;
}

unsigned MakeList(int index, const unsigned char* packet, unsigned char (&out)[ShortFrameLimit])
{
    const ListContext* context = CurrentList();
    if (!context || context->slot != index || SelectorSocket(index) == NoSocket)
        return 0;
    return ExpandList(packet, context->source, s_mgLevel, s_dlLevel, out);
}

DWORD Deliver(int index, char* packet, DWORD length)
{
    RecordObservation(false, reinterpret_cast<DWORD>(packet), length, Delegated);
    return s_stockHost.send(index, packet, length);
}

DWORD __cdecl SendSelection(int index, char* packet, DWORD length)
{
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(packet);
    const Route route = IsPlayer(index) ? SendRoute(bytes, length) : Closed;
    if (route == ResetAndSend)
    {
        CreateLock lock;
        CreateSlot& slot = s_creates[index - FirstPlayer];
        memset(&slot, 0, sizeof(slot));
    }
    if (route == Stock || route == ResetAndSend)
        return Deliver(index, packet, length);
    if (route == SendCreate)
    {
        unsigned char reply[ClientCreateBytes];
        if (TakeCreate(index, bytes, reply))
            return Deliver(index, reinterpret_cast<char*>(reply), sizeof(reply));
    }
    if (route == SendList || route == SendNotice)
    {
        unsigned char out[ShortFrameLimit];
        const unsigned size = route == SendList ? MakeList(index, bytes, out) : ExpandNotice(bytes, length, out);
        if (size)
            return Deliver(index, reinterpret_cast<char*>(out), size);
    }
    RecordObservation(false, reinterpret_cast<DWORD>(packet), length, BlockedByProfile);
    return SendConsumed;
}

int ReadFault(DWORD code)
{
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR || code == EXCEPTION_GUARD_PAGE
               ? EXCEPTION_EXECUTE_HANDLER
               : EXCEPTION_CONTINUE_SEARCH;
}

bool CopyIncomingList(DWORD address, unsigned char (&copy)[MaxDbListBytes])
{
    if (!address || address > MAXDWORD - (MaxDbListBytes - 1))
        return false;
    __try
    {
        const unsigned char* source = reinterpret_cast<const unsigned char*>(address);
        memcpy(copy, source, DbListHeaderBytes);
        const unsigned size = DbListSize(copy);
        if (!size)
            return false;
        memcpy(copy + DbListHeaderBytes, source + DbListHeaderBytes, size - DbListHeaderBytes);
    }
    __except (ReadFault(GetExceptionCode()))
    {
        return false;
    }
    return true;
}

void __stdcall ReceiveList(DWORD address)
{
    unsigned char source[MaxDbListBytes];
    if (!CopyIncomingList(address, source))
        return;
    const int slot = static_cast<int>(DbListSlot(source));
    if (!IsPlayer(slot) || SelectorSocket(slot) == NoSocket)
        return;
    unsigned char compact[MaxStockDbListBytes];
    CompactDbList(source, compact);
    ListContext context = {source, slot};
    ListContext* const prior = CurrentList();
    if (!TlsSetValue(s_listTls, &context))
        return;
    __try
    {
        s_stockHost.list(compact);
    }
    __finally
    {
        if (!TlsSetValue(s_listTls, prior))
            ExitProcess(2);
    }
}

} // namespace

bool BindSelection(BootSession& session, const SelectionHost& host, unsigned mgLevel, unsigned dlLevel)
{
    if (!host.receive || !host.send || !host.list || !host.objects || s_listTls != TLS_OUT_OF_INDEXES)
        return false;
    const DWORD tls = TlsAlloc();
    if (tls == TLS_OUT_OF_INDEXES)
        return false;
    if (!InitializeCriticalSectionAndSpinCount(&s_createLock, 0))
    {
        TlsFree(tls);
        return false;
    }
    s_listTls = tls;
    s_stockHost = host;
    s_mgLevel = mgLevel;
    s_dlLevel = dlLevel;
    session.receive = &ReceiveSelection;
    session.send = &SendSelection;
    session.characterList = &ReceiveList;
    session.wire = &XorWire;
    return true;
}

bool ConfigureSelection(BootSession& session)
{
    char path[MAX_PATH];
    DWORD directory;
    if (!ExecutableDirectory(path, directory) || sizeof(IniName) > sizeof(path) - directory)
        return false;
    memcpy(path + directory, IniName, sizeof(IniName));
    const unsigned mgLevel = static_cast<WORD>(GetPrivateProfileIntA("Common", "Lv01", DefaultMgLevel, path));
    const unsigned dlLevel = static_cast<WORD>(GetPrivateProfileIntA("Common", "Lv02", DefaultDlLevel, path));
    const SelectionHost stock = {reinterpret_cast<NativeReceive>(0x00414180), reinterpret_cast<SendHook>(0x00440240),
                                 reinterpret_cast<NativeList>(0x0040f6c0), GameServer102iObjects()};
    return BindSelection(session, stock, mgLevel, dlLevel);
}
} // namespace Boot
} // namespace MyDll
