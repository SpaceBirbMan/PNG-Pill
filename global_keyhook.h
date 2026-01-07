#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static bool g_globalRunning = true;
static HHOOK g_keyboardHook = nullptr;

// todo: Надо передавать в хук клавиши, которые надо читать
// todo: Тут должны быть атомики, также надо решить вопросы с антивирусами

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        KBDLLHOOKSTRUCT* p = (KBDLLHOOKSTRUCT*)lParam;
        DWORD vkCode = p->vkCode;

    }
    return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
}

static void InstallGlobalKeyboardHook() {
    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandleW(nullptr), 0);
    // ifdef сюда
    if (!g_keyboardHook) {

    }
}

static void UninstallGlobalKeyboardHook() {
    if (g_keyboardHook) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
    }
}

#endif // _WIN32
