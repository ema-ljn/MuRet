/* Laboratorio propio de aprendizaje: observa el EDIT del sistema, no ejecuta ni carga MU.
 * Contrato del cliente: c32, 467fe0 / 467b10 / 4682a0 / 4a35a0; P-C181.
 * Compilar x86 permite repetirlo en XP. No confundir Windows del CI con XP.
 * Las pruebas de paste reemplazan el portapapeles por textos sintéticos. Usar la VM del lab.
 */
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static HWND s_parent;
static HWND s_edit;
static BYTE s_keyboard[256];
static int s_failed;

static void selection(int anchor, int cursor)
{
    SendMessageA(s_edit, EM_SETSEL, (WPARAM)(intptr_t)anchor, (LPARAM)cursor);
}

static void reset(const char* text, int cursor)
{
    BYTE clear[256] = {0};
    SetKeyboardState(clear);
    SetWindowTextA(s_edit, text);
    selection(cursor, cursor);
    SendMessageA(s_edit, EM_EMPTYUNDOBUFFER, 0, 0);
}

static void key(UINT vk, int shift, int ctrl, int alt)
{
    BYTE state[256] = {0};
    state[VK_SHIFT] = shift ? 0x80 : 0;
    state[VK_CONTROL] = ctrl ? 0x80 : 0;
    state[VK_MENU] = alt ? 0x80 : 0;
    state[vk] = 0x80;
    if (!SetKeyboardState(state))
        s_failed = 1;
    SendMessageA(s_edit, alt ? WM_SYSKEYDOWN : WM_KEYDOWN, vk, alt ? (LPARAM)0x20000001 : 1);
    state[vk] = 0;
    SetKeyboardState(state);
}

static void character(UINT ch)
{
    SendMessageA(s_edit, WM_CHAR, ch, 1);
}

static void type(const char* text)
{
    const unsigned char* at = (const unsigned char*)text;
    BYTE clear[256] = {0};
    SetKeyboardState(clear);
    while (*at)
        character(*at++);
}

static void dump(const char* label)
{
    char text[512] = {0};
    DWORD first = 0, last = 0;
    int i;
    const int length = GetWindowTextA(s_edit, text, (int)sizeof(text));
    SendMessageA(s_edit, EM_GETSEL, (WPARAM)&first, (LPARAM)&last);
    printf("%s\t", label);
    for (i = 0; i < length; ++i)
        printf("%02x", (unsigned char)text[i]);
    printf("\t%lu\t%lu\t%ld\t%ld\n", first, last, (long)SendMessageA(s_edit, EM_CANUNDO, 0, 0),
           (long)SendMessageA(s_edit, EM_GETFIRSTVISIBLELINE, 0, 0));
}

static void paste(const char* label, const char* text)
{
    HGLOBAL data;
    void* bytes;
    const SIZE_T size = strlen(text) + 1;
    reset("abcdef", 0);
    selection(1, 4);
    data = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!data)
    {
        s_failed = 1;
        return;
    }
    bytes = GlobalLock(data);
    if (!bytes)
    {
        GlobalFree(data);
        s_failed = 1;
        return;
    }
    memcpy(bytes, text, size);
    GlobalUnlock(data);
    if (!OpenClipboard(s_parent))
    {
        GlobalFree(data);
        s_failed = 1;
        return;
    }
    if (!EmptyClipboard() || !SetClipboardData(CF_TEXT, data))
    {
        CloseClipboard();
        GlobalFree(data);
        s_failed = 1;
        return;
    }
    CloseClipboard();
    SendMessageA(s_edit, WM_PASTE, 0, 0);
    dump(label);
}

static void navigation(void)
{
    reset("abc def", 7);
    key(VK_LEFT, 0, 1, 0);
    dump("ctrl_left");
    key(VK_LEFT, 0, 1, 0);
    dump("ctrl_left_again");
    key(VK_RIGHT, 1, 1, 0);
    dump("ctrl_shift_right");
    reset("abc.def-ghi", 10);
    key(VK_LEFT, 0, 1, 0);
    dump("ctrl_left_punctuation");
    reset("abc def", 7);
    key(VK_BACK, 0, 1, 0);
    dump("ctrl_backspace_keydown");
    character(0x7f); /* Observar el WM_CHAR DEL por separado; no deducir el traductor del guest. */
    dump("char_del_with_ctrl");
    reset("abc def", 2);
    key(VK_DELETE, 0, 1, 0);
    dump("ctrl_delete");
    reset("abc def", 0);
    selection(2, 4);
    key(VK_DELETE, 0, 1, 0);
    dump("ctrl_delete_selection");
    reset("abc", 3);
    character(1);
    dump("char_ctrl_a");
    reset("abcdef", 0);
    selection(1, 4);
    selection(-2, -1);
    dump("setsel_minus2_minus1");
    reset("ABCDEFGHIJKLMNO", 14);
    selection(10, 14);
    type("Q");
    dump("settext_over_limit_replace");
}

static void translated_key(const char* label, UINT vk, int ctrl, int alt)
{
    MSG input = {0}, posted;
    BYTE state[256] = {0};
    state[VK_CONTROL] = ctrl ? 0x80 : 0;
    state[VK_MENU] = alt ? 0x80 : 0;
    state[vk] = 0x80;
    SetKeyboardState(state);
    input.hwnd = s_edit;
    input.message = alt ? WM_SYSKEYDOWN : WM_KEYDOWN;
    input.wParam = vk;
    input.lParam = alt ? (LPARAM)0x20000001 : 1;
    TranslateMessage(&input);
    DispatchMessageA(&input);
    while (PeekMessageA(&posted, s_edit, WM_CHAR, WM_SYSDEADCHAR, PM_REMOVE))
    {
        printf("message_%s\t%04x\t%04lx\n", label, posted.message, (unsigned long)posted.wParam);
        DispatchMessageA(&posted);
    }
    dump(label);
}

static void word_breaks(void)
{
    unsigned code;
    char name[64];
    char text[6] = {'a', 'b', 0, 'c', 'd', 0};
    for (code = 0x20; code <= 0xff; ++code)
    {
        if (code >= 0x7f && code < 0xa0)
            continue;
        text[2] = (char)code;
        reset(text, 5);
        key(VK_LEFT, 0, 1, 0);
        sprintf_s(name, sizeof(name), "word_%02x_left", code);
        dump(name);
        reset(text, 0);
        key(VK_RIGHT, 0, 1, 0);
        sprintf_s(name, sizeof(name), "word_%02x_right", code);
        dump(name);
        reset(text, 2);
        key(VK_RIGHT, 0, 1, 0);
        sprintf_s(name, sizeof(name), "word_%02x_right_from_separator", code);
        dump(name);
        reset(text, 3);
        key(VK_LEFT, 0, 1, 0);
        sprintf_s(name, sizeof(name), "word_%02x_left_from_word", code);
        dump(name);
    }
}

static void horizontal_scroll(void)
{
    RECT format;
    SIZE size;
    HDC dc;
    HGDIOBJ previous;
    HFONT font = (HFONT)GetStockObject(ANSI_FIXED_FONT);
    SendMessageA(s_edit, WM_SETFONT, (WPARAM)font, FALSE);
    SetWindowPos(s_edit, NULL, 200, 100, 20, 14, SWP_NOZORDER | SWP_NOACTIVATE);
    SendMessageA(s_edit, EM_GETRECT, 0, (LPARAM)&format);
    dc = GetDC(s_edit);
    previous = SelectObject(dc, font);
    GetTextExtentPoint32A(dc, "W", 1, &size);
    SelectObject(dc, previous);
    ReleaseDC(s_edit, dc);
    printf("metrics_fixed\t%ld\t%ld\t%ld\t%ld\n", size.cx, size.cy, format.left, format.right);
    reset("", 0);
    type("WWWWWWWWWW");
    dump("scroll_ten_w");
    key(VK_HOME, 0, 0, 0);
    dump("scroll_home");
    key(VK_END, 0, 0, 0);
    dump("scroll_end");
    key(VK_LEFT, 0, 0, 0);
    dump("scroll_left_one");
    key(VK_LEFT, 0, 0, 0);
    dump("scroll_left_two");
    key(VK_LEFT, 0, 0, 0);
    dump("scroll_left_three");
}

static void undo(void)
{
    reset("", 0);
    type("abc");
    SendMessageA(s_edit, WM_UNDO, 0, 0);
    dump("undo_typing");
    SendMessageA(s_edit, WM_UNDO, 0, 0);
    dump("redo_typing");
    reset("abcd", 2);
    character(8);
    character(8);
    SendMessageA(s_edit, WM_UNDO, 0, 0);
    dump("undo_backspaces");
    reset("abcd", 1);
    key(VK_DELETE, 0, 0, 0);
    key(VK_DELETE, 0, 0, 0);
    SendMessageA(s_edit, WM_UNDO, 0, 0);
    dump("undo_deletes");
    reset("abcd", 0);
    selection(1, 3);
    type("XY");
    SendMessageA(s_edit, WM_UNDO, 0, 0);
    dump("undo_replacement");
    SendMessageA(s_edit, WM_UNDO, 0, 0);
    dump("redo_replacement");
    reset("", 0);
    type("abc");
    character(8);
    SendMessageA(s_edit, WM_UNDO, 0, 0);
    dump("undo_typing_then_backspace");
    reset("abc", 3);
    type("X");
    key(VK_HOME, 0, 0, 0);
    type("Y");
    type("Z");
    SendMessageA(s_edit, WM_UNDO, 0, 0);
    dump("undo_typing_after_home");
    reset("", 0);
    type("abc");
    key(VK_BACK, 0, 0, 1);
    dump("alt_backspace_keydown");
    SendMessageA(s_edit, WM_SYSCHAR, 8, (LPARAM)0x20000001);
    dump("alt_backspace_syschar");
}

int main(void)
{
    if (!GetKeyboardState(s_keyboard))
        return 2;
    s_parent = CreateWindowExA(0, "STATIC", "MuRet EDIT contract", WS_OVERLAPPED, 0, 0, 200, 100, NULL, NULL,
                               GetModuleHandleA(NULL), NULL);
    if (!s_parent)
        return 2;
    s_edit = CreateWindowExA(0, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 200, 100, 80, 14, s_parent,
                             (HMENU)(intptr_t)1, GetModuleHandleA(NULL), NULL);
    if (!s_edit)
    {
        DestroyWindow(s_parent);
        return 2;
    }
    SendMessageA(s_edit, EM_LIMITTEXT, 10, 0);
    printf("native-edit-contract-v1\tbits=%u\tACP=%u\n", (unsigned)(sizeof(void*) * 8), GetACP());
    puts("case\thex_text\tselection_begin\tselection_end\tcan_undo\tfirst_visible_char");
    navigation();
    undo();
    reset("abc def", 7);
    translated_key("translated_ctrl_backspace", VK_BACK, 1, 0);
    reset("", 0);
    type("abc");
    translated_key("translated_alt_backspace", VK_BACK, 0, 1);
    word_breaks();
    paste("paste_empty", "");
    paste("paste_cr", "\r");
    paste("paste_lf", "\n");
    paste("paste_crlf", "\r\n");
    paste("paste_cr_middle", "X\rY");
    paste("paste_lf_middle", "X\nY");
    paste("paste_crlf_middle", "X\r\nY");
    paste("paste_controls", "X\tY\b\x1a");
    horizontal_scroll();
    SetKeyboardState(s_keyboard);
    DestroyWindow(s_edit);
    DestroyWindow(s_parent);
    if (s_failed)
        fputs("No se completaron todas las llamadas al control/portapapeles.\n", stderr);
    return s_failed ? 2 : 0;
}
