#pragma once
// ─── DX11 多窗口同步器 — Direct2D 自绘窗口 ──────────────
//
//  纯 Win32 API + Direct2D/DirectWrite 渲染，零第三方依赖。
//  支持 Win11 Mica 材质、深色/浅色自适应、自绘列表与按钮。

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <vector>
#include <set>
#include <string>

#include "sync_engine.h"
#include "window_manager.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")

class AppWindow {
public:
    AppWindow(HINSTANCE hInst);
    ~AppWindow();

    bool Create(int nCmdShow);
    int  Run();          // 消息循环

private:
    // ── 窗口过程 ───────────────────────────────────────
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    // ── 消息处理 ───────────────────────────────────────
    void OnCreate();
    void OnDestroy();
    void OnSize(int w, int h);
    void OnPaint();
    void OnMouseMove(int x, int y);
    void OnLButtonDown(int x, int y);
    void OnLButtonUp(int x, int y);
    void OnMouseWheel(int x, int y, int delta);
    void OnTimer(UINT_PTR id);
    void OnCommand(WPARAM wp);
    void OnSettingChange();

    // ── Direct2D 资源 ──────────────────────────────────
    bool CreateDeviceResources();
    void DiscardDeviceResources();
    void CreateBrushes();

    // ── 渲染 ───────────────────────────────────────────
    void Render();
    void DrawFilterBar();
    void DrawWindowLists();
    void DrawActionBar();
    void DrawStatusBar();
    void DrawListItem(ID2D1RenderTarget* rt,
                      const D2D1_RECT_F& r, const WindowInfo& w,
                      bool selected, bool hovered, bool childList);
    void DrawButton(ID2D1RenderTarget* rt,
                    const D2D1_RECT_F& r, const wchar_t* text,
                    bool enabled, bool hover, bool pressed,
                    bool isPrimary);

    // ── 布局计算 ───────────────────────────────────────
    D2D1_RECT_F FilterBgRect() const;
    D2D1_RECT_F ParentListRect() const;
    D2D1_RECT_F ChildListRect() const;
    D2D1_RECT_F ParentInfoRect() const;
    D2D1_RECT_F ChildInfoRect() const;
    D2D1_RECT_F ActionBarRect() const;
    D2D1_RECT_F StatusBarRect() const;
    D2D1_RECT_F StartBtnRect() const;
    D2D1_RECT_F StopBtnRect() const;
    D2D1_RECT_F RefreshBtnRect() const;
    D2D1_RECT_F BlacklistBtnRect() const;

    // ── 列表计算 ───────────────────────────────────────
    int  VisibleItemsCount(const D2D1_RECT_F& listRect) const;
    void ClampScroll();
    bool PointInList(const D2D1_RECT_F& listRect, int mx, int my, int& idx) const;
    int  ListScrollMax(const D2D1_RECT_F& listRect, int itemCount) const;

    // ── 过滤 ───────────────────────────────────────────
    void ApplyFilter();

    // ── 主题 ───────────────────────────────────────────
    void ReadSystemTheme();
    void UpdateColors();
    void InvalidateContent();  // 仅刷新 Edit 控件以下区域

    // ═══════════════════════════════════════════════════════
    //  窗口
    // ═══════════════════════════════════════════════════════
    HINSTANCE m_hInst = nullptr;
    HWND      m_hWnd  = nullptr;
    int       m_width  = 860;
    int       m_height = 620;
    bool      m_micaEnabled = false;

    // ═══════════════════════════════════════════════════════
    //  Direct2D 资源（设备无关）
    // ═══════════════════════════════════════════════════════
    ID2D1Factory*      m_pD2DFactory    = nullptr;
    IDWriteFactory*    m_pDWriteFactory  = nullptr;
    IDWriteTextFormat* m_pTextFormat     = nullptr;  // 13pt 正文
    IDWriteTextFormat* m_pSmallFormat    = nullptr;  // 11pt 辅助文字
    IDWriteTextFormat* m_pBoldFormat     = nullptr;  // 13pt 粗体
    IDWriteTextFormat* m_pTitleFormat    = nullptr;  // 15pt 标题
    IDWriteTextFormat* m_pBtnFormat      = nullptr;  // 13pt 按钮(居中)

    //  设备相关（随 render target 重建）
    ID2D1HwndRenderTarget* m_pRT       = nullptr;
    ID2D1SolidColorBrush*  m_pBrush    = nullptr;  // 复用

    // ═══════════════════════════════════════════════════════
    //  Edit 控件（原生输入框，避免自绘文本输入的复杂度）
    // ═══════════════════════════════════════════════════════
    HWND m_hEditTitle   = nullptr;
    HWND m_hEditProcess = nullptr;
    std::wstring m_filterTitle;
    std::wstring m_filterProcess;

    // ═══════════════════════════════════════════════════════
    //  核心引擎（不变）
    // ═══════════════════════════════════════════════════════
    SyncEngine    m_engine;
    WindowManager m_windowMgr;

    // ═══════════════════════════════════════════════════════
    //  UI 状态
    // ═══════════════════════════════════════════════════════
    std::vector<WindowInfo> m_filtered;   // 过滤后的窗口列表
    HWND m_parentHwnd = nullptr;          // 当前选中的父窗口
    std::set<HWND> m_childHwnds;          // 已选的子窗口集合

    int m_parentScroll = 0;               // 父窗口列表滚动
    int m_childScroll  = 0;               // 子窗口列表滚动

    //  鼠标交互
    enum class HitZone {
        None,
        TitleBar,
        FilterTitle, FilterProcess, FilterRefresh,
        ParentList, ChildList,
        ParentScrollTrack, ChildScrollTrack,
        StartBtn, StopBtn, BlacklistBtn,
    };
    HitZone m_hoverZone  = HitZone::None;
    HitZone m_activeZone = HitZone::None; // 鼠标按下时的区域
    int     m_hoverIdx   = -1;
    int     m_scrollDragY = 0;
    int     m_scrollDragStartVal = 0;

    bool m_hoverStart = false;
    bool m_hoverStop  = false;
    bool m_hoverBlacklist = false;

    // ═══════════════════════════════════════════════════════
    //  主题颜色
    // ═══════════════════════════════════════════════════════
    bool m_darkMode = false;
    D2D1_COLOR_F m_accentColor;           // 系统主题色

    //  文本色
    D2D1_COLOR_F m_clrText;
    D2D1_COLOR_F m_clrTextSecondary;
    D2D1_COLOR_F m_clrTextDim;
    //  表面色
    D2D1_COLOR_F m_clrSurface;
    D2D1_COLOR_F m_clrSurfaceHover;
    D2D1_COLOR_F m_clrMicaOverlay;
    //  边框
    D2D1_COLOR_F m_clrBorder;
    //  强调色
    D2D1_COLOR_F m_clrAccent;
    D2D1_COLOR_F m_clrAccentDim;
    //  按钮
    D2D1_COLOR_F m_clrBtnStart;
    D2D1_COLOR_F m_clrBtnStartHover;
    D2D1_COLOR_F m_clrBtnStop;
    D2D1_COLOR_F m_clrBtnStopHover;
    D2D1_COLOR_F m_clrBtnDisabled;
    //  状态
    D2D1_COLOR_F m_clrStatusGreen;
    D2D1_COLOR_F m_clrStatusGreenBg;
    D2D1_COLOR_F m_clrStatusText;
    D2D1_COLOR_F m_clrStatusBg;
};
