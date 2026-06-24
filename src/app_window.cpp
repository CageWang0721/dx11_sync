// ─── DX11 多窗口同步器 — Direct2D 自绘窗口实现 ──────────
//
//  使用 Direct2D + DirectWrite 渲染现代化 UI，
//  支持 Win11 Mica 材质、深色/浅色自适应。
//  完全无第三方依赖 — 仅 Windows SDK 即可编译。

#include "app_window.h"
#include "key_blacklist.h"
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <commctrl.h>
#include <windowsx.h>
#include "resource.h"

// ═══════════════════════════════════════════════════════════
//  常量
// ═══════════════════════════════════════════════════════════
//  前向声明
static bool PtInD2DRect(const D2D1_RECT_F& r, int x, int y);

static constexpr int  FILTER_H     = 50;
static constexpr int  LIST_HEAD_H  = 24;
static constexpr int  LIST_ITEM_H  = 36;
static constexpr int  LIST_PAD     = 4;
static constexpr int  INFO_H       = 46;
static constexpr int  ACTION_H     = 48;
static constexpr int  STATUS_H     = 32;
static constexpr int  MARGIN       = 14;
static constexpr int  GAP          = 10;
static constexpr int  CORNER_R     = 8;
static constexpr int  BTN_W        = 130;
static constexpr int  BTN_H        = 36;

static constexpr UINT_PTR TIMER_FILTER   = 1;
static constexpr UINT     FILTER_DELAY   = 300;  // ms
static constexpr UINT_PTR TIMER_STATUS   = 2;
static constexpr UINT     STATUS_INTERVAL = 500; // ms

static constexpr ULONG_PTR EDIT_TITLE_ID   = 1001;
static constexpr ULONG_PTR EDIT_PROCESS_ID = 1002;

// DWM 常量（Win10 SDK 可能没有，Win11 SDK 已内置）
#ifndef DWMWA_BACKDROP
#define DWMWA_BACKDROP         38
#endif
#ifndef DWMSBT_MICA
#define DWMSBT_MICA            2
#endif
#ifndef DWMWA_MICA
#define DWMWA_MICA             1029
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND           2
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE  20
#endif

// ═══════════════════════════════════════════════════════════
//  构造 / 析构
// ═══════════════════════════════════════════════════════════

AppWindow::AppWindow(HINSTANCE hInst) : m_hInst(hInst) {
    ReadSystemTheme();
    UpdateColors();

    m_engine.SetStatusCallback([this](const wchar_t*) {
        // 状态变化 → 刷新状态栏 + 按钮区
        auto sr = StatusBarRect();
        auto ar = ActionBarRect();
        RECT rc = { (LONG)sr.left, (LONG)ar.top, (LONG)sr.right, (LONG)sr.bottom };
        InvalidateRect(m_hWnd, &rc, FALSE);
    });
}

AppWindow::~AppWindow() {
    if (m_pBrush)    m_pBrush->Release();
    if (m_pRT)       m_pRT->Release();
    if (m_pBtnFormat)  m_pBtnFormat->Release();
    if (m_pTitleFormat) m_pTitleFormat->Release();
    if (m_pBoldFormat)  m_pBoldFormat->Release();
    if (m_pSmallFormat) m_pSmallFormat->Release();
    if (m_pTextFormat)  m_pTextFormat->Release();
    if (m_pDWriteFactory) m_pDWriteFactory->Release();
    if (m_pD2DFactory)    m_pD2DFactory->Release();
}

// ═══════════════════════════════════════════════════════════
//  窗口创建
// ═══════════════════════════════════════════════════════════

bool AppWindow::Create(int nCmdShow) {
    // ── 注册窗口类 ────────────────────────────────────
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = m_hInst;
    wc.hIcon         = LoadIconW(m_hInst, MAKEINTRESOURCEW(IDI_ICON1));
    wc.hIconSm       = LoadIconW(m_hInst, MAKEINTRESOURCEW(IDI_ICON1));
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"DX11SyncWindow";

    RegisterClassExW(&wc);

    // ── 创建窗口 ──────────────────────────────────────
    m_hWnd = CreateWindowExW(
        0, L"DX11SyncWindow",
        L"DX11 多窗口同步器 v3.0",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, m_width, m_height,
        nullptr, nullptr, m_hInst, this);

    if (!m_hWnd) return false;

    // ── Win11 Mica 材质 ──────────────────────────────
    //  先尝试 Win11 22H2+ (DWMSBT_MAINWINDOW = 2)
    DWORD micaType = DWMSBT_MICA;
    HRESULT hr = DwmSetWindowAttribute(m_hWnd, DWMWA_BACKDROP,
                                       &micaType, sizeof(DWORD));
    if (FAILED(hr)) {
        // 回退 Win11 21H2
        BOOL enable = TRUE;
        hr = DwmSetWindowAttribute(m_hWnd, DWMWA_MICA,
                                   &enable, sizeof(BOOL));
    }
    m_micaEnabled = SUCCEEDED(hr);

    //  深色模式标题栏
    BOOL darkTitle = m_darkMode ? TRUE : FALSE;
    DwmSetWindowAttribute(m_hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE,
                          &darkTitle, sizeof(BOOL));

    //  圆角
    DWORD cornerRound = DWMWCP_ROUND;
    DwmSetWindowAttribute(m_hWnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                          &cornerRound, sizeof(DWORD));

    ShowWindow(m_hWnd, nCmdShow);
    UpdateWindow(m_hWnd);

    return true;
}

int AppWindow::Run() {
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

// ═══════════════════════════════════════════════════════════
//  静态窗口过程 → 实例方法
// ═══════════════════════════════════════════════════════════

LRESULT CALLBACK AppWindow::WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    AppWindow* pThis = nullptr;

    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        pThis = static_cast<AppWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        pThis->m_hWnd = hWnd;
        pThis->OnCreate();
        return 0;
    }

    pThis = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (pThis)
        return pThis->HandleMessage(msg, wp, lp);

    return DefWindowProcW(hWnd, msg, wp, lp);
}

LRESULT AppWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_DESTROY:
        OnDestroy();
        return 0;

    case WM_SIZE: {
        int w = LOWORD(lp), h = HIWORD(lp);
        if (w > 0 && h > 0) { m_width = w; m_height = h; }
        OnSize(w, h);
        return 0;
    }

    case WM_PAINT:
        OnPaint();
        return 0;

    case WM_MOUSEMOVE:
        OnMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_LBUTTONDOWN:
        OnLButtonDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_LBUTTONUP:
        OnLButtonUp(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_MOUSEWHEEL:
        OnMouseWheel(GET_X_LPARAM(lp), GET_Y_LPARAM(lp),
                     GET_WHEEL_DELTA_WPARAM(wp));
        return 0;

    case WM_TIMER:
        OnTimer(static_cast<UINT_PTR>(wp));
        return 0;

    case WM_COMMAND:
        OnCommand(wp);
        return 0;

    case WM_SETTINGCHANGE:
        OnSettingChange();
        return 0;

    case WM_MOUSELEAVE:
        //  鼠标离开窗口 → 清除所有 hover 状态
        m_hoverZone  = HitZone::None;
        m_hoverIdx   = -1;
        m_hoverStart = false;
        m_hoverStop  = false;
        InvalidateContent();
        return 0;

    case WM_ERASEBKGND:
        return 1;  // 禁止默认擦除（避免闪烁）
    }

    return DefWindowProcW(m_hWnd, msg, wp, lp);
}

// ═══════════════════════════════════════════════════════════
//  消息处理
// ═══════════════════════════════════════════════════════════

void AppWindow::OnCreate() {
    // ── Direct2D 工厂 ────────────────────────────────
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                      IID_ID2D1Factory, nullptr,
                      reinterpret_cast<void**>(&m_pD2DFactory));

    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                        __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown**>(&m_pDWriteFactory));

    //  字体
    const wchar_t* fontFamily = L"Segoe UI";
    m_pDWriteFactory->CreateTextFormat(fontFamily, nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"", &m_pTextFormat);
    m_pDWriteFactory->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"", &m_pSmallFormat);
    m_pDWriteFactory->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"", &m_pBoldFormat);
    m_pDWriteFactory->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 15.0f, L"", &m_pTitleFormat);

    // 按钮专用: 13pt 粗体 + 水平垂直居中
    m_pDWriteFactory->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"", &m_pBtnFormat);
    m_pBtnFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_pBtnFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    m_pBtnFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    CreateDeviceResources();

    // ── 原生 Edit 控件 ────────────────────────────────
    int filterY = 4;
    int editW = (m_width - MARGIN * 2 - GAP * 2 - BTN_W - GAP) / 2;

    m_hEditTitle = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        MARGIN, filterY, editW, 32,
        m_hWnd, reinterpret_cast<HMENU>(EDIT_TITLE_ID), m_hInst, nullptr);

    m_hEditProcess = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        MARGIN + editW + GAP, filterY, editW, 32,
        m_hWnd, reinterpret_cast<HMENU>(EDIT_PROCESS_ID), m_hInst, nullptr);

    //  字体: Segoe UI 14pt
    HFONT hEditFont = CreateFontW(19, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH, L"Segoe UI");
    SendMessageW(m_hEditTitle,   WM_SETFONT, reinterpret_cast<WPARAM>(hEditFont), TRUE);
    SendMessageW(m_hEditProcess, WM_SETFONT, reinterpret_cast<WPARAM>(hEditFont), TRUE);

    SendMessageW(m_hEditTitle,   EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(L"输入关键字过滤窗口..."));
    SendMessageW(m_hEditProcess, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(L"进程名过滤, 如: game.exe"));

    //  强制刷新列表
    ApplyFilter();

    // ── 定时器 ────────────────────────────────────────
    SetTimer(m_hWnd, TIMER_STATUS, STATUS_INTERVAL, nullptr);
}

void AppWindow::OnDestroy() {
    m_engine.Stop();
    KillTimer(m_hWnd, TIMER_STATUS);
    KillTimer(m_hWnd, TIMER_FILTER);
    DiscardDeviceResources();
    PostQuitMessage(0);
}

void AppWindow::OnSize(int w, int h) {
    if (m_pRT) {
        m_pRT->Resize(D2D1::SizeU(static_cast<UINT32>(w), static_cast<UINT32>(h)));
    }
    //  调整 Edit 控件位置
    if (m_hEditTitle) {
        int filterY = 4;
        int editW = (w - MARGIN * 2 - GAP * 2 - BTN_W - GAP) / 2;
        if (editW < 80) editW = 80;
        SetWindowPos(m_hEditTitle, nullptr,
            MARGIN, filterY, editW, 32, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(m_hEditProcess, nullptr,
            MARGIN + editW + GAP, filterY, editW, 32, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    InvalidateContent();
}

void AppWindow::OnPaint() {
    PAINTSTRUCT ps;
    BeginPaint(m_hWnd, &ps);

    if (!m_pRT) {
        CreateDeviceResources();
    }

    if (m_pRT) {
        Render();
    }

    EndPaint(m_hWnd, &ps);
}

void AppWindow::OnMouseMove(int x, int y) {
    HitZone oldZone = m_hoverZone;
    int oldIdx = m_hoverIdx;
    bool oldHoverStart = m_hoverStart;
    bool oldHoverStop  = m_hoverStop;
    bool oldHoverBlacklist = m_hoverBlacklist;

    //  重置
    m_hoverZone  = HitZone::None;
    m_hoverIdx   = -1;
    m_hoverStart = false;
    m_hoverStop  = false;
    m_hoverBlacklist = false;

    //  命中检测
    if (PtInD2DRect(StartBtnRect(), x, y)) {
        m_hoverZone = HitZone::StartBtn;
        m_hoverStart = true;
    } else if (PtInD2DRect(StopBtnRect(), x, y)) {
        m_hoverZone = HitZone::StopBtn;
        m_hoverStop = true;
    } else if (PtInD2DRect(BlacklistBtnRect(), x, y)) {
        m_hoverZone = HitZone::BlacklistBtn;
        m_hoverBlacklist = true;
    } else if (PtInD2DRect(RefreshBtnRect(), x, y)) {
        m_hoverZone = HitZone::FilterRefresh;
    } else {
        int idx;
        if (PointInList(ParentListRect(), x, y, idx)) {
            m_hoverZone = HitZone::ParentList;
            m_hoverIdx = idx;
        } else if (PointInList(ChildListRect(), x, y, idx)) {
            m_hoverZone = HitZone::ChildList;
            m_hoverIdx = idx;
        }
    }

    //  仅变化时重绘
    if (m_hoverZone != oldZone || m_hoverIdx != oldIdx ||
        m_hoverStart != oldHoverStart || m_hoverStop != oldHoverStop ||
        m_hoverBlacklist != oldHoverBlacklist) {
        InvalidateContent();
    }

    //  跟踪鼠标离开
    TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
    TrackMouseEvent(&tme);
}

void AppWindow::OnLButtonDown(int x, int y) {
    m_activeZone = m_hoverZone;

    if (m_activeZone == HitZone::StartBtn) {
        if (m_engine.GetParent() && !m_engine.GetChildren().empty()) {
            m_engine.Start();
        }
        InvalidateContent();
        return;
    }

    if (m_activeZone == HitZone::StopBtn) {
        m_engine.Stop();
        InvalidateContent();
        return;
    }

    if (m_activeZone == HitZone::BlacklistBtn) {
        ShowKeyBlacklistDialog(m_hInst, m_hWnd, m_engine);
        InvalidateContent();
        return;
    }

    if (m_activeZone == HitZone::FilterRefresh) {
        // 重新枚举窗口并刷新列表
        m_childHwnds.clear();
        m_engine.ClearChildren();
        m_parentHwnd = nullptr;
        m_engine.SetParent(nullptr);
        ApplyFilter();
        return;
    }

    if (m_activeZone == HitZone::ParentList && m_hoverIdx >= 0 &&
        static_cast<size_t>(m_hoverIdx) < m_filtered.size()) {
        m_parentHwnd = m_filtered[m_hoverIdx].hWnd;
        m_engine.SetParent(m_parentHwnd);
        InvalidateContent();
        return;
    }

    if (m_activeZone == HitZone::ChildList && m_hoverIdx >= 0 &&
        static_cast<size_t>(m_hoverIdx) < m_filtered.size()) {
        HWND hwnd = m_filtered[m_hoverIdx].hWnd;
        if (hwnd == m_parentHwnd) return; // 不能选父窗口为子窗口

        auto it = m_childHwnds.find(hwnd);
        if (it != m_childHwnds.end()) {
            m_childHwnds.erase(it);
            m_engine.RemoveChild(hwnd);
        } else {
            m_childHwnds.insert(hwnd);
            m_engine.AddChild(hwnd);
        }
        InvalidateContent();
        return;
    }
}

void AppWindow::OnLButtonUp(int, int) {
    m_activeZone = HitZone::None;
}

void AppWindow::OnMouseWheel(int x, int y, int delta) {
    //  WM_MOUSEWHEEL 的 x,y 是屏幕坐标，转为客户区坐标
    POINT pt = { x, y };
    ScreenToClient(m_hWnd, &pt);

    //  滚轮滚动列表
    int* pScroll = nullptr;
    int itemCount = static_cast<int>(m_filtered.size());
    D2D1_RECT_F listRect;

    if (PtInD2DRect(ParentListRect(), pt.x, pt.y)) {
        pScroll = &m_parentScroll;
        listRect = ParentListRect();
    } else if (PtInD2DRect(ChildListRect(), pt.x, pt.y)) {
        pScroll = &m_childScroll;
        listRect = ChildListRect();
    }

    if (pScroll) {
        int maxScroll = ListScrollMax(listRect, itemCount);
        *pScroll -= delta / 4;  //  滚动速度
        if (*pScroll < 0) *pScroll = 0;
        if (*pScroll > maxScroll) *pScroll = maxScroll;
        ClampScroll();
        InvalidateContent();
    }
}

void AppWindow::OnTimer(UINT_PTR id) {
    if (id == TIMER_FILTER) {
        KillTimer(m_hWnd, TIMER_FILTER);
        ApplyFilter();
    } else if (id == TIMER_STATUS) {
        // 周期性刷新状态栏+按钮区（仅该区域，避免影响 Edit 控件）
        auto sr = StatusBarRect();
        auto ar = ActionBarRect();
        RECT rc = { (LONG)sr.left, (LONG)ar.top, (LONG)sr.right, (LONG)sr.bottom };
        InvalidateRect(m_hWnd, &rc, FALSE);
    }
}

void AppWindow::OnCommand(WPARAM wp) {
    WORD code = HIWORD(wp);
    WORD ctrlId = LOWORD(wp);

    if (code == EN_CHANGE) {
        wchar_t buf[256] = {};
        if (ctrlId == EDIT_TITLE_ID) {
            GetWindowTextW(m_hEditTitle, buf, 255);
            m_filterTitle = buf;
        } else if (ctrlId == EDIT_PROCESS_ID) {
            GetWindowTextW(m_hEditProcess, buf, 255);
            m_filterProcess = buf;
        }
        // 立即刷新 + 防抖: 300ms 内新输入重置定时器
        ApplyFilter();
        SetTimer(m_hWnd, TIMER_FILTER, FILTER_DELAY, nullptr);
    }
}

void AppWindow::OnSettingChange() {
    ReadSystemTheme();
    UpdateColors();
    //  更新 Edit 控件颜色
    if (m_hEditTitle) {
        InvalidateContent();
    }
}

// ═══════════════════════════════════════════════════════════
//  Direct2D 资源
// ═══════════════════════════════════════════════════════════

bool AppWindow::CreateDeviceResources() {
    if (m_pRT) return true;

    RECT rc;
    GetClientRect(m_hWnd, &rc);

    D2D1_RENDER_TARGET_PROPERTIES props =
        D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                              D2D1_ALPHA_MODE_PREMULTIPLIED));

    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps =
        D2D1::HwndRenderTargetProperties(m_hWnd,
            D2D1::SizeU(static_cast<UINT32>(rc.right - rc.left),
                        static_cast<UINT32>(rc.bottom - rc.top)));

    HRESULT hr = m_pD2DFactory->CreateHwndRenderTarget(
        props, hwndProps, &m_pRT);

    if (SUCCEEDED(hr) && m_pRT) {
        CreateBrushes();
    }

    return SUCCEEDED(hr);
}

void AppWindow::DiscardDeviceResources() {
    if (m_pBrush) { m_pBrush->Release(); m_pBrush = nullptr; }
    if (m_pRT)    { m_pRT->Release();    m_pRT    = nullptr; }
}

void AppWindow::CreateBrushes() {
    if (m_pBrush) { m_pBrush->Release(); m_pBrush = nullptr; }
    //  创建一个默认画笔，后续绘制时按需更换颜色
    m_pRT->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &m_pBrush);
}

// ═══════════════════════════════════════════════════════════
//  渲染主循环
// ═══════════════════════════════════════════════════════════

void AppWindow::Render() {
    if (!m_pRT) return;

    m_pRT->BeginDraw();
    m_pRT->SetTransform(D2D1::Matrix3x2F::Identity());

    //  全窗口 Mica 覆层（半透明 Acrylic 效果）
    D2D1_RECT_F full = { 0, 0, static_cast<float>(m_width), static_cast<float>(m_height) };
    m_pBrush->SetColor(m_clrMicaOverlay);
    m_pRT->FillRectangle(&full, m_pBrush);

    DrawFilterBar();
    DrawWindowLists();
    DrawActionBar();
    DrawStatusBar();

    m_pRT->EndDraw();
}

// ═══════════════════════════════════════════════════════════
//  绘制各部分
// ═══════════════════════════════════════════════════════════

void AppWindow::DrawFilterBar() {
    //  只画刷新按钮，不画背景（避免覆盖原生 Edit 控件导致闪烁）
    auto btnR = RefreshBtnRect();
    bool hoverRefresh = (m_hoverZone == HitZone::FilterRefresh);
    DrawButton(m_pRT, btnR, L"\U0001F504 刷新列表", true, hoverRefresh, false, true);
}

void AppWindow::DrawWindowLists() {
    auto parentListR = ParentListRect();
    auto childListR  = ChildListRect();
    auto parentInfoR = ParentInfoRect();
    auto childInfoR  = ChildInfoRect();

    // ── "父窗口 (单选)" 标签 ──────────────────────────
    {
        D2D1_RECT_F labelR = { parentListR.left, parentListR.top - LIST_HEAD_H,
                               parentListR.right, parentListR.top };
        m_pBrush->SetColor(m_clrTextSecondary);
        m_pRT->DrawText(L"\U0001F464 父窗口 (单选)", 12,
                         m_pBoldFormat, labelR, m_pBrush,
                         D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    // ── "子窗口 (多选)" 标签 ──────────────────────────
    {
        D2D1_RECT_F labelR = { childListR.left, childListR.top - LIST_HEAD_H,
                               childListR.right, childListR.top };
        m_pBrush->SetColor(m_clrTextSecondary);
        m_pRT->DrawText(L"\U0001F3AE 子窗口 (多选)", 12,
                         m_pBoldFormat, labelR, m_pBrush,
                         D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    // ── 父窗口列表背景 ────────────────────────────────
    m_pBrush->SetColor(m_clrSurface);
    m_pRT->FillRoundedRectangle(
        D2D1::RoundedRect(parentListR, static_cast<float>(CORNER_R), static_cast<float>(CORNER_R)),
        m_pBrush);
    m_pBrush->SetColor(m_clrBorder);
    m_pRT->DrawRoundedRectangle(
        D2D1::RoundedRect(parentListR, static_cast<float>(CORNER_R), static_cast<float>(CORNER_R)),
        m_pBrush, 1.0f);

    // ── 父窗口列表项 ──────────────────────────────────
    {
        int visCount = VisibleItemsCount(parentListR);
        int maxScroll = ListScrollMax(parentListR, static_cast<int>(m_filtered.size()));
        if (maxScroll > 0 && m_parentScroll > maxScroll) m_parentScroll = maxScroll;

        D2D1_RECT_F clipR = { parentListR.left + LIST_PAD, parentListR.top + LIST_PAD,
                              parentListR.right - LIST_PAD, parentListR.bottom - LIST_PAD };
        m_pRT->PushAxisAlignedClip(clipR, D2D1_ANTIALIAS_MODE_ALIASED);

        for (int i = 0; i < visCount + 1; i++) {
            int dataIdx = i + m_parentScroll;
            if (dataIdx >= static_cast<int>(m_filtered.size())) break;
            float y = clipR.top + i * LIST_ITEM_H;
            D2D1_RECT_F itemR = { clipR.left, y, clipR.right, y + LIST_ITEM_H };
            bool selected = (m_filtered[dataIdx].hWnd == m_parentHwnd);
            bool hovered = (m_hoverZone == HitZone::ParentList && m_hoverIdx == dataIdx);
            DrawListItem(m_pRT, itemR, m_filtered[dataIdx], selected, hovered, false);
        }

        m_pRT->PopAxisAlignedClip();
    }

    // ── 子窗口列表背景 ────────────────────────────────
    m_pBrush->SetColor(m_clrSurface);
    m_pRT->FillRoundedRectangle(
        D2D1::RoundedRect(childListR, static_cast<float>(CORNER_R), static_cast<float>(CORNER_R)),
        m_pBrush);
    m_pBrush->SetColor(m_clrBorder);
    m_pRT->DrawRoundedRectangle(
        D2D1::RoundedRect(childListR, static_cast<float>(CORNER_R), static_cast<float>(CORNER_R)),
        m_pBrush, 1.0f);

    // ── 子窗口列表项 ──────────────────────────────────
    {
        int maxScroll = ListScrollMax(childListR, static_cast<int>(m_filtered.size()));
        if (maxScroll > 0 && m_childScroll > maxScroll) m_childScroll = maxScroll;

        D2D1_RECT_F clipR = { childListR.left + LIST_PAD, childListR.top + LIST_PAD,
                              childListR.right - LIST_PAD, childListR.bottom - LIST_PAD };
        m_pRT->PushAxisAlignedClip(clipR, D2D1_ANTIALIAS_MODE_ALIASED);

        int visCount = VisibleItemsCount(childListR);
        for (int i = 0; i < visCount + 1; i++) {
            int dataIdx = i + m_childScroll;
            if (dataIdx >= static_cast<int>(m_filtered.size())) break;
            float y = clipR.top + i * LIST_ITEM_H;
            D2D1_RECT_F itemR = { clipR.left, y, clipR.right, y + LIST_ITEM_H };
            bool selected = (m_childHwnds.find(m_filtered[dataIdx].hWnd) != m_childHwnds.end());
            bool hovered = (m_hoverZone == HitZone::ChildList && m_hoverIdx == dataIdx);
            DrawListItem(m_pRT, itemR, m_filtered[dataIdx], selected, hovered, true);
        }

        m_pRT->PopAxisAlignedClip();
    }

    // ── 父窗口信息面板 ────────────────────────────────
    {
        m_pBrush->SetColor(m_clrSurface);
        m_pRT->FillRoundedRectangle(
            D2D1::RoundedRect(parentInfoR, static_cast<float>(CORNER_R), static_cast<float>(CORNER_R)),
            m_pBrush);

        std::wstring info = L"选择父窗口";
        if (m_parentHwnd && IsWindow(m_parentHwnd)) {
            for (const auto& w : m_filtered) {
                if (w.hWnd == m_parentHwnd) {
                    int ww = w.rect.right - w.rect.left;
                    int wh = w.rect.bottom - w.rect.top;
                    wchar_t buf[512];
                    swprintf_s(buf, L"已选: %s\n类名: %s | %d×%d  位置: (%d, %d)",
                               w.title.c_str(), w.className.c_str(),
                               ww, wh, w.rect.left, w.rect.top);
                    info = buf;
                    break;
                }
            }
            if (info == L"选择父窗口") info = L"父窗口已失效";
        }

        D2D1_RECT_F padR = { parentInfoR.left + 10, parentInfoR.top + 6,
                             parentInfoR.right - 10, parentInfoR.bottom - 6 };
        m_pBrush->SetColor(m_clrTextSecondary);
        m_pRT->DrawText(info.c_str(), static_cast<UINT32>(info.length()),
                         m_pSmallFormat, padR, m_pBrush,
                         D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    // ── 子窗口信息面板 ────────────────────────────────
    {
        m_pBrush->SetColor(m_clrSurface);
        m_pRT->FillRoundedRectangle(
            D2D1::RoundedRect(childInfoR, static_cast<float>(CORNER_R), static_cast<float>(CORNER_R)),
            m_pBrush);

        std::wstring info = L"选择子窗口";
        if (!m_childHwnds.empty()) {
            wchar_t buf[128];
            swprintf_s(buf, L"已选 %zu 个子窗口", m_childHwnds.size());
            info = buf;
        }

        D2D1_RECT_F padR = { childInfoR.left + 10, childInfoR.top + 6,
                             childInfoR.right - 10, childInfoR.bottom - 6 };
        m_pBrush->SetColor(m_clrTextSecondary);
        m_pRT->DrawText(info.c_str(), static_cast<UINT32>(info.length()),
                         m_pSmallFormat, padR, m_pBrush,
                         D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
}

void AppWindow::DrawListItem(ID2D1RenderTarget* rt,
                              const D2D1_RECT_F& r, const WindowInfo& w,
                              bool selected, bool hovered, bool childList) {
    //  项背景 — hover 优先于 selected
    if (selected) {
        m_pBrush->SetColor(m_clrAccentDim);
        rt->FillRoundedRectangle(
            D2D1::RoundedRect(r, 5.0f, 5.0f), m_pBrush);
        m_pBrush->SetColor(m_clrAccent);
        rt->DrawRoundedRectangle(
            D2D1::RoundedRect(r, 5.0f, 5.0f), m_pBrush, 1.0f);
    } else if (hovered) {
        m_pBrush->SetColor(m_clrSurfaceHover);
        rt->FillRoundedRectangle(
            D2D1::RoundedRect(r, 5.0f, 5.0f), m_pBrush);
    }

    //  子窗口列表：复选框指示器
    float textX = r.left + 8;
    if (childList) {
        D2D1_RECT_F cbR = { r.left + 6, r.top + 9, r.left + 24, r.top + 27 };
        m_pBrush->SetColor(selected ? m_clrAccent : D2D1::ColorF(m_clrBorder.r, m_clrBorder.g, m_clrBorder.b, 0.3f));
        rt->FillRoundedRectangle(D2D1::RoundedRect(cbR, 4.0f, 4.0f), m_pBrush);
        if (selected) {
            m_pBrush->SetColor(D2D1::ColorF(1, 1, 1));
            rt->DrawText(L"\u2713", 1, m_pBoldFormat,
                          D2D1_RECT_F{ cbR.left, cbR.top, cbR.right, cbR.bottom },
                          m_pBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        textX = r.left + 32;
    }

    //  标题文字 "[process.exe] 窗口标题"
    std::wstring display = L"[" + w.processName + L"] " + w.title;
    D2D1_RECT_F textR = { textX, r.top + 8, r.right - 60, r.bottom - 4 };

    //  省略过长文字
    m_pBrush->SetColor(m_clrText);
    rt->DrawText(display.c_str(), static_cast<UINT32>(display.length()),
                  m_pTextFormat, textR, m_pBrush,
                  D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_NO_SNAP);

    //  PID 标签
    std::wstring pidStr = L"PID:" + std::to_wstring(w.processId);
    D2D1_RECT_F pidR = { r.right - 58, r.top + 10, r.right - 6, r.bottom - 4 };
    m_pBrush->SetColor(selected ? m_clrAccent : m_clrTextDim);
    rt->DrawText(pidStr.c_str(), static_cast<UINT32>(pidStr.length()),
                  m_pSmallFormat, pidR, m_pBrush,
                  D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void AppWindow::DrawActionBar() {
    auto r = ActionBarRect();

    bool canStart = m_engine.GetParent() != nullptr &&
                    !m_engine.GetChildren().empty() &&
                    !m_engine.IsRunning();
    bool canStop  = m_engine.IsRunning();

    DrawButton(m_pRT, StartBtnRect(), L"\u25B6 开始同步",
               canStart, m_hoverStart, m_activeZone == HitZone::StartBtn, true);
    DrawButton(m_pRT, StopBtnRect(), L"\u23F9 终止同步",
               canStop, m_hoverStop, m_activeZone == HitZone::StopBtn, false);
    DrawButton(m_pRT, BlacklistBtnRect(), L"\U0001F512 热键黑名单",
               true, m_hoverBlacklist, m_activeZone == HitZone::BlacklistBtn, true);
}

void AppWindow::DrawButton(ID2D1RenderTarget* rt,
                            const D2D1_RECT_F& r, const wchar_t* text,
                            bool enabled, bool hover, bool pressed,
                            bool isPrimary) {
    //  按钮背景色
    if (!enabled) {
        m_pBrush->SetColor(m_clrBtnDisabled);
    } else if (pressed) {
        m_pBrush->SetColor(isPrimary ? D2D1::ColorF(0.08f, 0.45f, 0.20f)
                                     : D2D1::ColorF(0.55f, 0.12f, 0.08f));
    } else if (hover) {
        m_pBrush->SetColor(isPrimary ? m_clrBtnStartHover : m_clrBtnStopHover);
    } else {
        m_pBrush->SetColor(isPrimary ? m_clrBtnStart : m_clrBtnStop);
    }
    rt->FillRoundedRectangle(D2D1::RoundedRect(r, 7.0f, 7.0f), m_pBrush);

    //  按钮文字
    m_pBrush->SetColor(enabled ? D2D1::ColorF(1, 1, 1) : m_clrTextDim);
    rt->DrawText(text, static_cast<UINT32>(wcslen(text)),
                  m_pBtnFormat, r, m_pBrush,
                  D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void AppWindow::DrawStatusBar() {
    auto r = StatusBarRect();
    bool running = m_engine.IsRunning();

    m_pBrush->SetColor(running ? m_clrStatusGreenBg : m_clrSurface);
    m_pRT->FillRoundedRectangle(D2D1::RoundedRect(r, static_cast<float>(CORNER_R), static_cast<float>(CORNER_R)), m_pBrush);

    //  状态指示灯
    D2D1_ELLIPSE dot = { { r.left + 15, r.top + 16 }, 5, 5 };
    m_pBrush->SetColor(running ? m_clrStatusGreen
                                : D2D1::ColorF(m_clrTextDim.r, m_clrTextDim.g, m_clrTextDim.b, 0.3f));
    m_pRT->FillEllipse(&dot, m_pBrush);

    //  状态文字
    D2D1_RECT_F textR = { r.left + 30, r.top + 6, r.right - 10, r.bottom - 6 };
    m_pBrush->SetColor(running ? m_clrStatusText : m_clrTextSecondary);

    const wchar_t* status;
    if (running)
        status = L"\U0001F7E2 同步中 | 切到父窗口操作";
    else
        status = L"\u23F8 同步已停止 | 就绪";

    m_pRT->DrawText(status, static_cast<UINT32>(wcslen(status)),
                  m_pSmallFormat, textR, m_pBrush,
                  D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

// ═══════════════════════════════════════════════════════════
//  布局计算
// ═══════════════════════════════════════════════════════════

D2D1_RECT_F AppWindow::RefreshBtnRect() const {
    auto r = FilterBgRect();
    float x = r.right - BTN_W - GAP;
    float y = r.top + 2;
    return { x, y, x + BTN_W, y + BTN_H };
}

D2D1_RECT_F AppWindow::FilterBgRect() const {
    float y = 0;
    return { static_cast<float>(MARGIN), y,
             static_cast<float>(m_width - MARGIN), y + static_cast<float>(FILTER_H) };
}

D2D1_RECT_F AppWindow::ParentListRect() const {
    float y = static_cast<float>(FILTER_H + GAP + LIST_HEAD_H);
    float halfW = (m_width - MARGIN * 2 - GAP) / 2.0f;
    float bottom = static_cast<float>(m_height) - MARGIN - INFO_H - GAP * 2 - ACTION_H - STATUS_H - GAP;
    return { static_cast<float>(MARGIN), y,
             static_cast<float>(MARGIN) + halfW, bottom };
}

D2D1_RECT_F AppWindow::ChildListRect() const {
    float y = static_cast<float>(FILTER_H + GAP + LIST_HEAD_H);
    float halfW = (m_width - MARGIN * 2 - GAP) / 2.0f;
    float bottom = static_cast<float>(m_height) - MARGIN - INFO_H - GAP * 2 - ACTION_H - STATUS_H - GAP;
    return { static_cast<float>(MARGIN) + halfW + GAP, y,
             static_cast<float>(m_width - MARGIN), bottom };
}

D2D1_RECT_F AppWindow::ParentInfoRect() const {
    float y = ParentListRect().bottom + GAP;
    float halfW = (m_width - MARGIN * 2 - GAP) / 2.0f;
    return { static_cast<float>(MARGIN), y,
             static_cast<float>(MARGIN) + halfW, y + INFO_H };
}

D2D1_RECT_F AppWindow::ChildInfoRect() const {
    float y = ChildListRect().bottom + GAP;
    float halfW = (m_width - MARGIN * 2 - GAP) / 2.0f;
    return { static_cast<float>(MARGIN) + halfW + GAP, y,
             static_cast<float>(m_width - MARGIN), y + INFO_H };
}

D2D1_RECT_F AppWindow::ActionBarRect() const {
    float y = ParentInfoRect().bottom + GAP;
    return { static_cast<float>(MARGIN), y,
             static_cast<float>(m_width - MARGIN), y + ACTION_H };
}

D2D1_RECT_F AppWindow::StartBtnRect() const {
    float y = ActionBarRect().top + 6;
    return { static_cast<float>(MARGIN), y,
             static_cast<float>(MARGIN + BTN_W), y + BTN_H };
}

D2D1_RECT_F AppWindow::StopBtnRect() const {
    float y = ActionBarRect().top + 6;
    return { static_cast<float>(MARGIN + BTN_W + GAP), y,
             static_cast<float>(MARGIN + BTN_W * 2 + GAP), y + BTN_H };
}

D2D1_RECT_F AppWindow::BlacklistBtnRect() const {
    float y = ActionBarRect().top + 6;
    float left = MARGIN + BTN_W * 2 + GAP * 2;
    return { static_cast<float>(left), y,
             static_cast<float>(left + 150), y + BTN_H };
}

D2D1_RECT_F AppWindow::StatusBarRect() const {
    float y = ActionBarRect().bottom + GAP;
    return { static_cast<float>(MARGIN), y,
             static_cast<float>(m_width - MARGIN),
             static_cast<float>(m_height) - GAP };
}

// ═══════════════════════════════════════════════════════════
//  列表辅助
// ═══════════════════════════════════════════════════════════

int AppWindow::VisibleItemsCount(const D2D1_RECT_F& listRect) const {
    float h = listRect.bottom - listRect.top - LIST_PAD * 2;
    return static_cast<int>(h / LIST_ITEM_H);
}

void AppWindow::ClampScroll() {
    int maxP = ListScrollMax(ParentListRect(), static_cast<int>(m_filtered.size()));
    int maxC = ListScrollMax(ChildListRect(), static_cast<int>(m_filtered.size()));
    if (m_parentScroll < 0) m_parentScroll = 0;
    if (m_parentScroll > maxP) m_parentScroll = maxP > 0 ? maxP : 0;
    if (m_childScroll < 0) m_childScroll = 0;
    if (m_childScroll > maxC) m_childScroll = maxC > 0 ? maxC : 0;
}

bool AppWindow::PointInList(const D2D1_RECT_F& listRect, int mx, int my,
                             int& idx) const {
    float padL = listRect.left + LIST_PAD;
    float padT = listRect.top + LIST_PAD;
    float padR = listRect.right - LIST_PAD;
    float padB = listRect.bottom - LIST_PAD;

    if (mx < padL || mx > padR || my < padT || my > padB) return false;

    const int* pScroll = (fabs(listRect.left - ParentListRect().left) < 1)
                       ? &m_parentScroll : &m_childScroll;
    int relY = static_cast<int>(my - padT);
    int dataIdx = *pScroll + relY / LIST_ITEM_H;

    if (dataIdx >= 0 && dataIdx < static_cast<int>(m_filtered.size())) {
        idx = dataIdx;
        return true;
    }
    return false;
}

int AppWindow::ListScrollMax(const D2D1_RECT_F& listRect, int itemCount) const {
    int vis = VisibleItemsCount(listRect);
    int maxVal = itemCount - vis;
    return maxVal > 0 ? maxVal : 0;
}

// ═══════════════════════════════════════════════════════════
//  过滤
// ═══════════════════════════════════════════════════════════

void AppWindow::ApplyFilter() {
    m_windowMgr.Refresh();
    const auto& all = m_windowMgr.GetWindows();

    m_filtered.clear();
    m_filtered.reserve(all.size());

    for (const auto& w : all) {
        //  标题过滤
        if (!m_filterTitle.empty()) {
            std::wstring titleLow(w.title);
            std::transform(titleLow.begin(), titleLow.end(), titleLow.begin(), ::towlower);
            std::wstring filterLow(m_filterTitle);
            std::transform(filterLow.begin(), filterLow.end(), filterLow.begin(), ::towlower);
            if (titleLow.find(filterLow) == std::wstring::npos) continue;
        }

        //  进程名过滤
        if (!m_filterProcess.empty()) {
            std::wstring procLow(w.processName);
            std::transform(procLow.begin(), procLow.end(), procLow.begin(), ::towlower);
            std::wstring filterLow(m_filterProcess);
            std::transform(filterLow.begin(), filterLow.end(), filterLow.begin(), ::towlower);
            if (procLow.find(filterLow) == std::wstring::npos) continue;
        }

        m_filtered.push_back(w);
    }

    //  清理无效的选中
    if (m_parentHwnd && !IsWindow(m_parentHwnd)) {
        m_parentHwnd = nullptr;
        m_engine.SetParent(nullptr);
    }
    for (auto it = m_childHwnds.begin(); it != m_childHwnds.end(); ) {
        if (!IsWindow(*it)) {
            m_engine.RemoveChild(*it);
            it = m_childHwnds.erase(it);
        } else {
            ++it;
        }
    }

    ClampScroll();
    // 只刷新列表和状态区域（避开 Edit 控件所在区域）
    auto lr = ParentListRect();
    auto sr = StatusBarRect();
    RECT rc = { (LONG)lr.left, (LONG)(lr.top - LIST_HEAD_H),
                (LONG)sr.right, (LONG)sr.bottom };
    InvalidateRect(m_hWnd, &rc, FALSE);
}

// ═══════════════════════════════════════════════════════════
//  系统主题
// ═══════════════════════════════════════════════════════════

void AppWindow::ReadSystemTheme() {
    //  读取 AppsUseLightTheme
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {

        DWORD appsUseLight = 1;
        DWORD size = sizeof(appsUseLight);
        RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(&appsUseLight), &size);
        m_darkMode = (appsUseLight == 0);

        RegCloseKey(hKey);
    }

    //  读取系统主题色
    m_accentColor = D2D1::ColorF(0.0f, 0.37f, 0.72f, 1.0f); // 默认蓝色
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\DWM",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {

        DWORD color = 0;
        DWORD size = sizeof(color);
        if (RegQueryValueExW(hKey, L"AccentColor", nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(&color), &size) == ERROR_SUCCESS) {
            //  DWM AccentColor = BBGGRR (无 alpha)
            m_accentColor = D2D1::ColorF(
                ((color >> 16) & 0xFF) / 255.0f,
                ((color >> 8)  & 0xFF) / 255.0f,
                (color & 0xFF) / 255.0f,
                1.0f);
        }
        RegCloseKey(hKey);
    }
}

void AppWindow::UpdateColors() {
    if (m_darkMode) {
        m_clrText          = D2D1::ColorF(0.95f, 0.95f, 0.95f);
        m_clrTextSecondary = D2D1::ColorF(0.65f, 0.65f, 0.65f);
        m_clrTextDim       = D2D1::ColorF(0.40f, 0.40f, 0.40f);
        m_clrSurface       = D2D1::ColorF(0.12f, 0.12f, 0.14f, 0.72f);
        m_clrSurfaceHover  = D2D1::ColorF(0.18f, 0.18f, 0.20f, 0.85f);
        m_clrMicaOverlay   = D2D1::ColorF(0.08f, 0.08f, 0.10f, 0.78f);
        m_clrBorder        = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.06f);
        m_clrBtnDisabled   = D2D1::ColorF(0.08f, 0.08f, 0.10f, 0.50f);
        m_clrStatusGreenBg = D2D1::ColorF(0.06f, 0.55f, 0.24f, 0.15f);
        m_clrStatusText    = D2D1::ColorF(0.30f, 0.80f, 0.45f);
        m_clrStatusBg      = m_clrSurface;
    } else {
        m_clrText          = D2D1::ColorF(0.10f, 0.10f, 0.10f);
        m_clrTextSecondary = D2D1::ColorF(0.33f, 0.33f, 0.33f);
        m_clrTextDim       = D2D1::ColorF(0.60f, 0.60f, 0.60f);
        m_clrSurface       = D2D1::ColorF(0.98f, 0.98f, 0.99f, 0.72f);
        m_clrSurfaceHover  = D2D1::ColorF(0.94f, 0.94f, 0.96f, 0.85f);
        m_clrMicaOverlay   = D2D1::ColorF(0.95f, 0.95f, 0.97f, 0.78f);
        m_clrBorder        = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.07f);
        m_clrBtnDisabled   = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.04f);
        m_clrStatusGreenBg = D2D1::ColorF(0.06f, 0.55f, 0.24f, 0.12f);
        m_clrStatusText    = D2D1::ColorF(0.04f, 0.37f, 0.15f);
        m_clrStatusBg      = m_clrSurface;
    }

    m_clrAccent     = m_accentColor;
    m_clrAccentDim  = D2D1::ColorF(m_accentColor.r, m_accentColor.g, m_accentColor.b, 0.12f);
    m_clrBtnStart       = D2D1::ColorF(0.10f, 0.48f, 0.23f);
    m_clrBtnStartHover  = D2D1::ColorF(0.18f, 0.55f, 0.34f);
    m_clrBtnStop        = D2D1::ColorF(0.63f, 0.13f, 0.13f);
    m_clrBtnStopHover   = D2D1::ColorF(0.75f, 0.18f, 0.18f);
    m_clrStatusGreen    = D2D1::ColorF(0.06f, 0.55f, 0.24f);
}

// ═══════════════════════════════════════════════════════════
//  辅助
// ═══════════════════════════════════════════════════════════

void AppWindow::InvalidateContent() {
    //  仅刷新标题栏以下、Edit 控件以下的区域（避开原生控件）
    auto sr = StatusBarRect();
    RECT rc = { 0, (LONG)FilterBgRect().bottom,
                (LONG)sr.right, (LONG)sr.bottom };
    InvalidateRect(m_hWnd, &rc, FALSE);
}

static bool PtInD2DRect(const D2D1_RECT_F& r, int x, int y) {
    return x >= static_cast<int>(r.left)  && x < static_cast<int>(r.right) &&
           y >= static_cast<int>(r.top)   && y < static_cast<int>(r.bottom);
}
