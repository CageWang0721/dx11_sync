// ─── 热键黑名单弹窗实现 ─────────────────────────────────
//
//  使用原生 BS_AUTOCHECKBOX 控件 + 分组标签 + 滚动区域。
//  彻底避免 ListView 分组渲染问题。

#include "key_blacklist.h"
#include <vector>

#pragma comment(lib, "comctl32.lib")

// ═══════════════════════════════════════════════════════════
//  常量
// ═══════════════════════════════════════════════════════════

static constexpr const wchar_t* kDlgClass    = L"KeyBlacklistDlgW";
static constexpr const wchar_t* kScrollClass = L"KeyBlacklistScrollW";

static constexpr int kDlgW      = 640;
static constexpr int kDlgH      = 530;
static constexpr int kBottomBar = 56;   // 底部按钮栏高度
static constexpr int kMargin    = 8;
static constexpr int kChkCols   = 4;    // 每行4个checkbox
static constexpr int kChkColW   = 155;  // 每列宽度
static constexpr int kChkH      = 24;   // checkbox高度
static constexpr int kChkRowH   = 28;   // 行高（含间距）
static constexpr int kLabelH    = 30;   // 分组标签高度

static constexpr int kChkBaseID = 1000;  // checkbox ID = 1000 + index
static constexpr int kLabelBaseID = 2000; // 标签 ID = 2000 + groupId

static constexpr int kBtnOK     = 3001;
static constexpr int kBtnCancel = 3002;
static constexpr int kBtnAllOn  = 3003;
static constexpr int kBtnAllOff = 3004;

// ═══════════════════════════════════════════════════════════
//  弹窗数据
// ═══════════════════════════════════════════════════════════

struct DlgData {
    SyncEngine* engine;
    HWND        hScroll;          // 滚动容器
    HWND        hContent;         // 内容窗口（所有checkbox的父窗口）
    int         contentHeight;    // 内容总高度
    int         visibleHeight;    // 可见区域高度
    int         scrollY;          // 当前滚动位置
    HFONT       hFont;
    HFONT       hFontBold;
};

// ═══════════════════════════════════════════════════════════
//  布局计算：填充内容窗口
// ═══════════════════════════════════════════════════════════

static int LayoutContent(HWND hContent, HINSTANCE hInst,
                          HFONT hFont, HFONT hFontBold,
                          SyncEngine& engine) {
    int y = 4;
    auto& bl = engine.GetBlacklist();

    for (int g = 0; g < g_groupCount; ++g) {
        // ── 分组标签 ──────────────────────────────────
        CreateWindowExW(0, L"STATIC", g_groupNames[g],
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            kMargin, y, 600, kLabelH,
            hContent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kLabelBaseID + g)),
            hInst, nullptr);
        SendMessageW(GetDlgItem(hContent, kLabelBaseID + g),
                     WM_SETFONT, reinterpret_cast<WPARAM>(hFontBold), TRUE);
        y += kLabelH;

        // ── 收集该组的键 ──────────────────────────────
        struct Idx { int keyIdx; };
        std::vector<Idx> groupItems;
        for (int i = 0; i < g_blacklistKeyCount; ++i) {
            if (g_blacklistKeys[i].groupId == g)
                groupItems.push_back({ i });
        }

        // ── 创建 checkbox（4列流式布局） ─────────────
        for (size_t j = 0; j < groupItems.size(); ++j) {
            int idx = groupItems[j].keyIdx;
            const auto& key = g_blacklistKeys[idx];

            int col = static_cast<int>(j) % kChkCols;
            int row = static_cast<int>(j) / kChkCols;
            int cx  = kMargin + col * kChkColW;
            int cy  = y + row * kChkRowH;

            HWND hChk = CreateWindowExW(0, L"BUTTON", key.name,
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                cx, cy, kChkColW - 4, kChkH,
                hContent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kChkBaseID + idx)),
                hInst, nullptr);
            SendMessageW(hChk, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

            // 预设勾选
            if (bl.find(key.vkPrimary) != bl.end()) {
                SendMessageW(hChk, BM_SETCHECK, BST_CHECKED, 0);
            }
        }

        int rows = (static_cast<int>(groupItems.size()) + kChkCols - 1) / kChkCols;
        if (rows == 0) rows = 1;
        y += rows * kChkRowH + 6;  // 组间距
    }

    return y + 4;  // 底部留白
}

// ═══════════════════════════════════════════════════════════
//  滚动处理
// ═══════════════════════════════════════════════════════════

static void UpdateScrollbar(DlgData* dd) {
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin   = 0;
    si.nMax   = dd->contentHeight - 1;
    si.nPage  = dd->visibleHeight;
    si.nPos   = dd->scrollY;
    SetScrollInfo(dd->hScroll, SB_VERT, &si, TRUE);
}

static void DoScroll(DlgData* dd, int newY) {
    if (newY < 0) newY = 0;
    int maxY = dd->contentHeight - dd->visibleHeight;
    if (maxY < 0) maxY = 0;
    if (newY > maxY) newY = maxY;

    dd->scrollY = newY;
    SetWindowPos(dd->hContent, nullptr, 0, -newY, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    UpdateScrollbar(dd);
}

// ═══════════════════════════════════════════════════════════
//  全选 / 全不选
// ═══════════════════════════════════════════════════════════

static void CheckAll(DlgData* dd, BOOL check) {
    for (int i = 0; i < g_blacklistKeyCount; ++i) {
        HWND hChk = GetDlgItem(dd->hContent, kChkBaseID + i);
        if (hChk) SendMessageW(hChk, BM_SETCHECK, check ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

// ═══════════════════════════════════════════════════════════
//  应用黑名单
// ═══════════════════════════════════════════════════════════

static void ApplyBlacklist(DlgData* dd) {
    auto& bl = dd->engine->GetBlacklist();
    // 清空
    std::vector<DWORD> toRemove;
    for (DWORD vk : bl) toRemove.push_back(vk);
    for (DWORD vk : toRemove) dd->engine->RemoveBlacklistKey(vk);

    // 根据勾选重新添加
    for (int i = 0; i < g_blacklistKeyCount; ++i) {
        HWND hChk = GetDlgItem(dd->hContent, kChkBaseID + i);
        if (hChk && SendMessageW(hChk, BM_GETCHECK, 0, 0) == BST_CHECKED) {
            const auto& key = g_blacklistKeys[i];
            dd->engine->AddBlacklistKey(key.vkPrimary);
            if (key.vkSecondary != 0) {
                dd->engine->AddBlacklistKey(key.vkSecondary);
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════
//  滚动容器窗口过程
// ═══════════════════════════════════════════════════════════

static LRESULT CALLBACK ScrollWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* dd = reinterpret_cast<DlgData*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        dd = static_cast<DlgData*>(cs->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dd));
        return 0;
    }

    case WM_SIZE: {
        if (!dd) break;
        dd->visibleHeight = HIWORD(lp);
        UpdateScrollbar(dd);
        // 如果内容比可见区短，调整滚动位置
        if (dd->scrollY > dd->contentHeight - dd->visibleHeight) {
            DoScroll(dd, max(0, dd->contentHeight - dd->visibleHeight));
        }
        return 0;
    }

    case WM_VSCROLL: {
        if (!dd) break;
        int newY = dd->scrollY;
        switch (LOWORD(wp)) {
        case SB_LINEUP:       newY -= 20; break;
        case SB_LINEDOWN:     newY += 20; break;
        case SB_PAGEUP:       newY -= dd->visibleHeight; break;
        case SB_PAGEDOWN:     newY += dd->visibleHeight; break;
        case SB_THUMBTRACK:   newY = HIWORD(wp); break;
        default: return 0;
        }
        DoScroll(dd, newY);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        if (!dd) break;
        int delta = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
        DoScroll(dd, dd->scrollY - delta * 40);
        return 0;
    }
    }

    return DefWindowProcW(hWnd, msg, wp, lp);
}

// ═══════════════════════════════════════════════════════════
//  弹窗主窗口过程
// ═══════════════════════════════════════════════════════════

static LRESULT CALLBACK DlgWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* dd = reinterpret_cast<DlgData*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        dd = static_cast<DlgData*>(cs->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dd));

        HINSTANCE hInst = cs->hInstance;

        // ── 字体 ─────────────────────────────────────
        dd->hFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        dd->hFontBold = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

        // ── 滚动容器 ─────────────────────────────────
        RECT rc;
        GetClientRect(hWnd, &rc);
        dd->visibleHeight = rc.bottom - rc.top - kBottomBar;

        dd->hScroll = CreateWindowExW(0, kScrollClass, L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL,
            0, 0, rc.right, dd->visibleHeight,
            hWnd, nullptr, hInst, dd);

        //  注册滚动容器的窗口类（首次）
        //  已在 ShowKeyBlacklistDialog 中注册

        // ── 内容窗口 ─────────────────────────────────
        dd->hContent = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            0, 0, rc.right - GetSystemMetrics(SM_CXVSCROLL), 2000,
            dd->hScroll, nullptr, hInst, nullptr);

        dd->contentHeight = LayoutContent(dd->hContent, hInst,
                                          dd->hFont, dd->hFontBold, *dd->engine);

        // 调整内容窗口高度
        SetWindowPos(dd->hContent, nullptr, 0, 0,
                     rc.right - GetSystemMetrics(SM_CXVSCROLL),
                     dd->contentHeight, SWP_NOMOVE | SWP_NOZORDER);

        UpdateScrollbar(dd);

        // ── 底部按钮 ─────────────────────────────────
        int btnY = rc.bottom - kBottomBar + 12;
        CreateWindowExW(0, L"BUTTON", L"全选",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            kMargin, btnY, 70, 30,
            hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBtnAllOn)),
            hInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"取消全选",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            kMargin + 76, btnY, 80, 30,
            hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBtnAllOff)),
            hInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"确定",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            kDlgW - kMargin - 176, btnY, 80, 30,
            hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBtnOK)),
            hInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"取消",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            kDlgW - kMargin - 86, btnY, 80, 30,
            hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBtnCancel)),
            hInst, nullptr);

        // 按钮字体
        for (int id : { kBtnAllOn, kBtnAllOff, kBtnOK, kBtnCancel }) {
            SendMessageW(GetDlgItem(hWnd, id), WM_SETFONT,
                        reinterpret_cast<WPARAM>(dd->hFont), TRUE);
        }

        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case kBtnOK:
            ApplyBlacklist(dd);
            DestroyWindow(hWnd);
            return 0;
        case kBtnCancel:
            DestroyWindow(hWnd);
            return 0;
        case kBtnAllOn:
            CheckAll(dd, TRUE);
            return 0;
        case kBtnAllOff:
            CheckAll(dd, FALSE);
            return 0;
        }
        break;

    case WM_DESTROY:
        if (dd) {
            if (dd->hFont)     DeleteObject(dd->hFont);
            if (dd->hFontBold) DeleteObject(dd->hFontBold);
        }
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wp, lp);
}

// ═══════════════════════════════════════════════════════════
//  公开接口
// ═══════════════════════════════════════════════════════════

bool ShowKeyBlacklistDialog(HINSTANCE hInst, HWND hParent, SyncEngine& engine) {
    // 注册窗口类（仅一次）
    static bool s_registered = false;
    if (!s_registered) {
        // 弹窗主窗口
        WNDCLASSEXW wcDlg = {};
        wcDlg.cbSize        = sizeof(wcDlg);
        wcDlg.style         = CS_HREDRAW | CS_VREDRAW;
        wcDlg.lpfnWndProc   = DlgWndProc;
        wcDlg.hInstance     = hInst;
        wcDlg.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wcDlg.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wcDlg.lpszClassName = kDlgClass;
        RegisterClassExW(&wcDlg);

        // 滚动容器
        WNDCLASSEXW wcScroll = {};
        wcScroll.cbSize        = sizeof(wcScroll);
        wcScroll.style         = CS_HREDRAW | CS_VREDRAW;
        wcScroll.lpfnWndProc   = ScrollWndProc;
        wcScroll.hInstance     = hInst;
        wcScroll.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wcScroll.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wcScroll.lpszClassName = kScrollClass;
        RegisterClassExW(&wcScroll);

        s_registered = true;
    }

    DlgData dd = {};
    dd.engine = &engine;

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        kDlgClass,
        L"热键黑名单 — 勾选 = 不转发该键到子窗口",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        kDlgW, kDlgH,
        hParent, nullptr, hInst, &dd);

    if (!hDlg) return false;

    // 居中于父窗口
    RECT rParent, rDlg;
    GetWindowRect(hParent, &rParent);
    GetWindowRect(hDlg, &rDlg);
    int x = rParent.left + ((rParent.right - rParent.left) - (rDlg.right - rDlg.left)) / 2;
    int y = rParent.top  + ((rParent.bottom - rParent.top) - (rDlg.bottom - rDlg.top)) / 2;
    SetWindowPos(hDlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    // 模态消息循环
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_CLOSE && msg.hwnd == hDlg) {
            DestroyWindow(hDlg);
            continue;
        }
        if (!IsWindow(hDlg)) break;

        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    return true;
}
