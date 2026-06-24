#pragma once

// ─── DX11 游戏多窗口同步引擎 ─────────────────────────────
//
// 原理：WH_KEYBOARD_LL / WH_MOUSE_LL 低层钩子捕获父窗口输入，
// 通过 PostMessage 异步投递到子窗口消息队列。
//
#include <windows.h>
#include <vector>
#include <functional>
#include <set>

class SyncEngine {
public:
    using StatusCallback = std::function<void(const wchar_t* status)>;

    SyncEngine();
    ~SyncEngine();

    void SetParent(HWND hWnd);
    void AddChild(HWND hWnd);
    void RemoveChild(HWND hWnd);
    void ClearChildren();
    const std::vector<HWND>& GetChildren() const { return m_children; }
    HWND GetParent() const { return m_parent; }

    bool Start();
    void Stop();
    bool IsRunning() const { return m_running; }

    void SetStatusCallback(StatusCallback cb) { m_statusCallback = std::move(cb); }

    void AddBlacklistKey(DWORD vkCode);
    void RemoveBlacklistKey(DWORD vkCode);
    std::set<DWORD>& GetBlacklist() { return m_keyBlacklist; }

private:
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);

    void InjectKeyEvent(DWORD vkCode, bool keyDown, DWORD scanCode, DWORD flags);
    void InjectMouseMove(int screenX, int screenY);
    void InjectMouseButton(UINT msg, int screenX, int screenY);
    void InjectMouseWheel(int delta, int screenX, int screenY);

    LPARAM ScreenToClientLParam(HWND hWnd, int screenX, int screenY);
    bool ShouldForward() const;
    bool IsKeyBlacklisted(DWORD vkCode) const;

    static SyncEngine* s_instance;

    HWND m_parent = nullptr;
    std::vector<HWND> m_children;
    bool m_running = false;

    HHOOK m_keyboardHook = nullptr;
    HHOOK m_mouseHook = nullptr;

    StatusCallback m_statusCallback;
    std::set<DWORD> m_keyBlacklist;
    bool m_inInject = false;
    DWORD m_lastMouseTime = 0;
};
