#include "sync_engine.h"
#include <algorithm>

SyncEngine* SyncEngine::s_instance = nullptr;

SyncEngine::SyncEngine() {
    s_instance = this;
    m_lastMouseTime = 0;
}

SyncEngine::~SyncEngine() {
    Stop();
    s_instance = nullptr;
}

void SyncEngine::SetParent(HWND hWnd)    { m_parent = hWnd; }

void SyncEngine::AddChild(HWND hWnd) {
    for (auto h : m_children) if (h == hWnd) return;
    if (hWnd == m_parent) return;
    m_children.push_back(hWnd);
}

void SyncEngine::RemoveChild(HWND hWnd) {
    m_children.erase(
        std::remove(m_children.begin(), m_children.end(), hWnd),
        m_children.end());
}

void SyncEngine::ClearChildren()         { m_children.clear(); }
void SyncEngine::AddBlacklistKey(DWORD k)   { m_keyBlacklist.insert(k); }
void SyncEngine::RemoveBlacklistKey(DWORD k) { m_keyBlacklist.erase(k); }

// ─── 启动 / 停止 ─────────────────────────────────────────

bool SyncEngine::Start() {
    if (m_running) return true;
    if (!m_parent || m_children.empty()) {
        if (m_statusCallback)
            m_statusCallback(L"错误: 请先选择父窗口和至少一个子窗口");
        return false;
    }

    m_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL,
        LowLevelKeyboardProc, GetModuleHandleW(nullptr), 0);
    m_mouseHook = SetWindowsHookExW(WH_MOUSE_LL,
        LowLevelMouseProc, GetModuleHandleW(nullptr), 0);

    if (!m_keyboardHook || !m_mouseHook) {
        Stop();
        if (m_statusCallback)
            m_statusCallback(L"错误: 安装钩子失败 (需管理员权限)");
        return false;
    }

    m_running = true;
    m_lastMouseTime = 0;
    if (m_statusCallback)
        m_statusCallback(L"🟢 同步中 | 切到父窗口操作");
    return true;
}

void SyncEngine::Stop() {
    if (m_keyboardHook) { UnhookWindowsHookEx(m_keyboardHook); m_keyboardHook = nullptr; }
    if (m_mouseHook)    { UnhookWindowsHookEx(m_mouseHook);    m_mouseHook    = nullptr; }
    m_running = false;
    if (m_statusCallback)
        m_statusCallback(L"⏸ 同步已停止");
}

// ─── 键盘钩子 ────────────────────────────────────────────

LRESULT CALLBACK SyncEngine::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode != HC_ACTION || !s_instance)
        return CallNextHookEx(nullptr, nCode, wParam, lParam);

    auto* pThis = s_instance;
    if (pThis->m_inInject)
        return CallNextHookEx(nullptr, nCode, wParam, lParam);

    auto* pKbd = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
    bool keyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);

    if (pThis->m_running && pThis->ShouldForward()) {
        if (!pThis->IsKeyBlacklisted(pKbd->vkCode)) {
            pThis->InjectKeyEvent(pKbd->vkCode, keyDown,
                                  pKbd->scanCode, pKbd->flags);
        }
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// ─── 鼠标钩子 ────────────────────────────────────────────

LRESULT CALLBACK SyncEngine::LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode != HC_ACTION || !s_instance)
        return CallNextHookEx(nullptr, nCode, wParam, lParam);

    auto* pThis = s_instance;
    if (pThis->m_inInject)
        return CallNextHookEx(nullptr, nCode, wParam, lParam);

    auto* pMouse = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);

    if (pThis->m_running && pThis->ShouldForward()) {
        int sx = pMouse->pt.x;
        int sy = pMouse->pt.y;

        switch (wParam) {
        case WM_MOUSEMOVE:
            // 节流：最多 120 次/秒 (≈8ms 间隔)
            {
                DWORD now = GetTickCount();
                if (now - pThis->m_lastMouseTime >= 8) {
                    pThis->m_lastMouseTime = now;
                    pThis->InjectMouseMove(sx, sy);
                }
            }
            break;
        case WM_LBUTTONDOWN: case WM_LBUTTONUP:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP:
            pThis->InjectMouseButton((UINT)wParam, sx, sy);
            break;
        case WM_MOUSEWHEEL:
            pThis->InjectMouseWheel(
                GET_WHEEL_DELTA_WPARAM(pMouse->mouseData), sx, sy);
            break;
        case WM_XBUTTONDOWN: case WM_XBUTTONUP:
            pThis->InjectMouseButton((UINT)wParam, sx, sy);
            break;
        }
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// ═══════════════════════════════════════════════════════════
//  注入实现 — 纯 PostMessage（异步，低延迟）
//
//  说明：物理键盘/鼠标已更新系统异步键态表，子进程的
//  GetAsyncKeyState 本就能读到。我们只需投递窗口消息。
// ═══════════════════════════════════════════════════════════

void SyncEngine::InjectKeyEvent(DWORD vkCode, bool keyDown,
                                 DWORD scanCode, DWORD flags) {
    m_inInject = true;

    UINT msg = keyDown ? WM_KEYDOWN : WM_KEYUP;

    // 构造标准 lParam
    LPARAM lParam = (scanCode & 0xFF) << 16;
    if (flags & LLKHF_EXTENDED) lParam |= (1 << 24);
    if (flags & LLKHF_ALTDOWN)  lParam |= (1 << 29);
    if (!keyDown) {
        lParam |= (1 << 30);
        lParam |= (1 << 31);
    }

    for (HWND hChild : m_children) {
        if (!IsWindow(hChild)) continue;
        PostMessageW(hChild, msg, vkCode, lParam);
        if (flags & LLKHF_ALTDOWN) {
            PostMessageW(hChild, keyDown ? WM_SYSKEYDOWN : WM_SYSKEYUP,
                         vkCode, lParam);
        }
    }

    m_inInject = false;
}

void SyncEngine::InjectMouseMove(int screenX, int screenY) {
    m_inInject = true;

    for (HWND hChild : m_children) {
        if (!IsWindow(hChild)) continue;
        LPARAM lp = ScreenToClientLParam(hChild, screenX, screenY);

        // 更新鼠标悬停状态（wParam = 虚拟键状态）
        WPARAM wp = 0;
        if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) wp |= MK_LBUTTON;
        if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) wp |= MK_RBUTTON;
        if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) wp |= MK_MBUTTON;
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) wp |= MK_CONTROL;
        if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) wp |= MK_SHIFT;

        PostMessageW(hChild, WM_MOUSEMOVE, wp, lp);
    }

    m_inInject = false;
}

void SyncEngine::InjectMouseButton(UINT msg, int screenX, int screenY) {
    m_inInject = true;

    WPARAM wp = 0;
    switch (msg) {
    case WM_LBUTTONDOWN: wp = MK_LBUTTON; break;
    case WM_LBUTTONUP:   wp = 0;          break;
    case WM_RBUTTONDOWN: wp = MK_RBUTTON; break;
    case WM_RBUTTONUP:   wp = 0;          break;
    case WM_MBUTTONDOWN: wp = MK_MBUTTON; break;
    case WM_MBUTTONUP:   wp = 0;          break;
    }
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) wp |= MK_CONTROL;
    if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) wp |= MK_SHIFT;

    for (HWND hChild : m_children) {
        if (!IsWindow(hChild)) continue;
        LPARAM lp = ScreenToClientLParam(hChild, screenX, screenY);
        PostMessageW(hChild, msg, wp, lp);
    }

    m_inInject = false;
}

void SyncEngine::InjectMouseWheel(int delta, int screenX, int screenY) {
    m_inInject = true;

    WPARAM wp = MAKEWPARAM(
        (GetAsyncKeyState(VK_CONTROL) & 0x8000 ? MK_CONTROL : 0) |
        (GetAsyncKeyState(VK_SHIFT)   & 0x8000 ? MK_SHIFT   : 0),
        delta);

    for (HWND hChild : m_children) {
        if (!IsWindow(hChild)) continue;
        LPARAM lp = ScreenToClientLParam(hChild, screenX, screenY);
        PostMessageW(hChild, WM_MOUSEWHEEL, wp, lp);
    }

    m_inInject = false;
}

// ─── 辅助 ────────────────────────────────────────────────

LPARAM SyncEngine::ScreenToClientLParam(HWND hWnd, int screenX, int screenY) {
    POINT pt = { screenX, screenY };
    ScreenToClient(hWnd, &pt);
    return MAKELPARAM((WORD)pt.x, (WORD)pt.y);
}

bool SyncEngine::ShouldForward() const {
    if (!m_parent || !IsWindow(m_parent)) return false;
    return GetForegroundWindow() == m_parent;
}

bool SyncEngine::IsKeyBlacklisted(DWORD vkCode) const {
    return m_keyBlacklist.find(vkCode) != m_keyBlacklist.end();
}
