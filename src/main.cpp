// DX11 多窗口同步器 v3.0
//
// 纯 Win32 API + Direct2D 渲染，零第三方依赖。
// 编译: build.bat → 产出单个 dx11_sync.exe

#include "app_window.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    // DPI 感知 (PerMonitorV2 — Win10 1703+)
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    AppWindow app(hInstance);

    if (!app.Create(nCmdShow))
        return -1;

    return app.Run();
}
