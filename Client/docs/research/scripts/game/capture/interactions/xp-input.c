#define WINVER 0x0501
#define _WIN32_WINNT 0x0501
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Herramienta propia de entrada normal: no abre procesos ni lee/escribe su memoria. */
static POINT text_point;
static const char* text_value;
static int text_matches;
static HWND text_target;
static BOOL CALLBACK set_name(HWND w, LPARAM data)
{
    char cls[64];
    RECT rect;
    (void)data;
    GetClassNameA(w, cls, sizeof cls);
    if (strcmp(cls, "Edit") || !IsWindowVisible(w) || (GetWindowLongA(w, GWL_STYLE) & ES_PASSWORD))
        return TRUE;
    GetWindowRect(w, &rect);
    if (PtInRect(&rect, text_point))
    {
        text_matches++;
        text_target = w;
    }
    return TRUE;
}

static int button(DWORD flags)
{
    INPUT input;
    ZeroMemory(&input, sizeof input);
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flags;
    return SendInput(1, &input, sizeof input) == 1;
}

int main(int argc, char** argv)
{
    HWND game, foreground;
    RECT client;
    POINT origin = {0, 0};
    DWORD pid = 0;
    FILE* out;
    int ok = 1, x = 0, y = 0;
    if (argc != 3 && argc != 5)
        return 2;
    out = fopen(argv[argc - 1], "w");
    if (!out)
        return 3;
    game = FindWindowA("MU", NULL);
    if (!strcmp(argv[1], "up"))
        ok = button(MOUSEEVENTF_LEFTUP);
    else if (!game)
    {
        fprintf(out, "error=MU_window_missing\nend\n");
        fclose(out);
        return 4;
    }
    else if (!strcmp(argv[1], "focus"))
    {
        ShowWindow(game, SW_RESTORE);
        ok = SetForegroundWindow(game) != 0;
    }
    else if (!strcmp(argv[1], "state"))
    { /* Sólo geometría pública de la ventana. */
    }
    /* text <ASCII test name> <x,y>: writes only the visible non-password Edit at that point. */
    else if (argc == 5 && !strcmp(argv[1], "text"))
    {
        int px, py;
        if (GetForegroundWindow() != game || sscanf(argv[3], "%d,%d", &px, &py) != 2)
            ok = 0;
        else
        {
            text_point.x = px;
            text_point.y = py;
            text_value = argv[2];
            text_matches = 0;
            EnumChildWindows(game, set_name, 0);
            ok = text_matches == 1;
            if (ok)
                ok = SetWindowTextA(text_target, text_value) != 0;
            fprintf(out, "matched_visible_name_edits=%d\n", text_matches);
        }
    }
    /* key <VK hex> <hold milliseconds>: ordinary foreground input, then release. */
    else if (argc == 5 && !strcmp(argv[1], "key"))
    {
        INPUT input;
        ZeroMemory(&input, sizeof input);
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = (WORD)strtoul(argv[2], NULL, 16);
        input.ki.wScan = (WORD)MapVirtualKeyA(input.ki.wVk, MAPVK_VK_TO_VSC);
        if (GetForegroundWindow() != game)
            ok = 0;
        else
        {
            ok = SendInput(1, &input, sizeof input) == 1;
            Sleep((DWORD)atoi(argv[3]));
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            ok = (SendInput(1, &input, sizeof input) == 1) && ok;
        }
    }
    else if (argc == 5 && (!strcmp(argv[1], "move") || !strcmp(argv[1], "click")))
    {
        GetClientRect(game, &client);
        x = atoi(argv[2]);
        y = atoi(argv[3]);
        if (GetForegroundWindow() != game || x < 0 || y < 0 || x >= client.right || y >= client.bottom)
            ok = 0;
        else
        {
            origin.x = x;
            origin.y = y;
            ok = ClientToScreen(game, &origin) && SetCursorPos(origin.x, origin.y);
            /* Deja dibujar el rayo de este punto antes del flanco, por Q-C165. */
            Sleep(250);
            if (!strcmp(argv[1], "click"))
            {
                ok = button(MOUSEEVENTF_LEFTDOWN) && ok;
                Sleep(100);
                ok = button(MOUSEEVENTF_LEFTUP) && ok;
            }
        }
    }
    else
        ok = 0;
    foreground = GetForegroundWindow();
    if (game)
    {
        GetWindowThreadProcessId(game, &pid);
        GetClientRect(game, &client);
        origin.x = 0;
        origin.y = 0;
        ClientToScreen(game, &origin);
        fprintf(out, "pid=%lu\nclient_x=%ld\nclient_y=%ld\nclient_width=%ld\nclient_height=%ld\n", (unsigned long)pid,
                origin.x, origin.y, client.right, client.bottom);
    }
    fprintf(out, "action_ok=%d\nforeground_is_game=%d\nleft_down=%d\nend\n", ok, foreground == game,
            (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
    fclose(out);
    return ok ? 0 : 6;
}
