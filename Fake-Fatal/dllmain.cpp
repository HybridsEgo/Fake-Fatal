#include "pch.h"
#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <random>
#include <chrono>
#include <thread>

static HANDLE g_hThread = NULL;
static DWORD  g_threadId = 0;
static HMODULE g_hModule = NULL;

#define HOTKEY_ID 1

static HWND CreateTopmostOwnerWindow()
{
    static const wchar_t CLASS_NAME[] = L"TopmostWindow";

    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = g_hModule;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        CLASS_NAME,
        L"",
        WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT, 1, 1,
        NULL,
        NULL,
        g_hModule,
        NULL
    );

    if (hwnd) {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ShowWindow(hwnd, SW_HIDE);
    }

    return hwnd;
}

static std::vector<HANDLE> SuspendOtherThreads()
{
    std::vector<HANDLE> suspendedThreadHandles;
    DWORD currentPid = GetCurrentProcessId();
    DWORD currentTid = GetCurrentThreadId();

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return suspendedThreadHandles;

    THREADENTRY32 te;
    te.dwSize = sizeof(te);
    if (Thread32First(hSnap, &te)) {
        do {
            if (te.th32OwnerProcessID == currentPid && te.th32ThreadID != currentTid) {
                HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION | THREAD_GET_CONTEXT, FALSE, te.th32ThreadID);
                if (hThread) {
                    DWORD prevCount = SuspendThread(hThread);
                    if (prevCount != (DWORD)-1) {
                        suspendedThreadHandles.push_back(hThread);
                    }
                    else {
                        CloseHandle(hThread);
                    }
                }
            }
            te.dwSize = sizeof(te);
        } while (Thread32Next(hSnap, &te));
    }

    CloseHandle(hSnap);
    return suspendedThreadHandles;
}

static void ResumeAndCloseThreads(std::vector<HANDLE>& handles)
{
    for (auto it = handles.rbegin(); it != handles.rend(); ++it) {
        HANDLE hThread = *it;
        if (hThread) {
            DWORD suspendCount;
            do {
                suspendCount = ResumeThread(hThread);
            } while (suspendCount > 1);

            CloseHandle(hThread);
        }
    }
    handles.clear();
}

static void ShowMessageBox(LPCWSTR text, LPCWSTR title)
{
    std::vector<HANDLE> suspended = SuspendOtherThreads();

    HWND owner = CreateTopmostOwnerWindow();
    if (owner) {
        SetForegroundWindow(owner);
        MessageBoxW(owner, text, title, MB_OK | MB_SETFOREGROUND);
        DestroyWindow(owner);
    }
    else {
        MessageBoxW(NULL, text, title, MB_OK | MB_SETFOREGROUND);
    }

    ResumeAndCloseThreads(suspended);
}

DWORD WINAPI HotkeyThreadProc(LPVOID lpParam)
{
    if (!RegisterHotKey(NULL, HOTKEY_ID, MOD_SHIFT, 'P')) {
        return 1;
    }

    MSG msg;
    BOOL bRet;

    while ((bRet = GetMessage(&msg, NULL, 0, 0)) != 0) {
        if (bRet == -1) {
            break;
        }
        else {
            if (msg.message == WM_HOTKEY && msg.wParam == HOTKEY_ID) {
                ShowMessageBox(L"Fatal error!", L"The UE4-MCC Game has crashed and will close");
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    UnregisterHotKey(NULL, HOTKEY_ID);
    return 0;
}

BOOL StartHotkeyThread()
{
    if (g_hThread != NULL) return TRUE;

    g_hThread = CreateThread(
        NULL,
        0,
        HotkeyThreadProc,
        NULL,
        0,
        &g_threadId
    );

    if (g_hThread == NULL) {
        return FALSE;
    }

    return TRUE;
}

void StopHotkeyThread()
{
    if (g_threadId != 0) {
        PostThreadMessage(g_threadId, WM_QUIT, 0, 0);
        if (g_hThread) {
            WaitForSingleObject(g_hThread, 3000);
            CloseHandle(g_hThread);
        }
    }
    g_hThread = NULL;
    g_threadId = 0;
}

static DWORD GenerateRandomInterval()
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<DWORD> dist(5 * 60 * 1000, 4 * 60 * 60 * 1000);
    return dist(gen);
}

DWORD WINAPI RandomPopupThreadProc(LPVOID lpParam)
{
    while (true)
    {
        DWORD interval = GenerateRandomInterval();
        std::this_thread::sleep_for(std::chrono::milliseconds(interval));

        ShowMessageBox(L"Fatal error!", L"The UE4-MCC Game has crashed and will close");
    }
    return 0;
}

BOOL StartRandomPopupThread()
{
    static HANDLE hRandomPopupThread = NULL;
    if (hRandomPopupThread != NULL) return TRUE;

    hRandomPopupThread = CreateThread(
        NULL,
        0,
        RandomPopupThreadProc,
        NULL,
        0,
        NULL
    );

    if (hRandomPopupThread == NULL) {
        return FALSE;
    }

    return TRUE;
}

BOOL APIENTRY DllMain(HMODULE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved
)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        StartHotkeyThread();
        StartRandomPopupThread();
        break;

    case DLL_PROCESS_DETACH:
        StopHotkeyThread();
        g_hModule = NULL;
        break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }
    return TRUE;
}
