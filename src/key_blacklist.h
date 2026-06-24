#pragma once
// ─── 热键黑名单编辑器 ──────────────────────────────────
//
//  弹出一个带分组的复选框列表，用户勾选需要拦截的键。
//  被勾选的键不会被转发到子窗口。
//
//  128 键英文键盘去重：左右修饰键合并、
//  数字小键盘数字键与主键盘合并等。

#include <windows.h>
#include <vector>

#include "sync_engine.h"

// ── 单个可黑名单键的定义 ──────────────────────────────
struct KeyEntry {
    const wchar_t* name;        // 显示名，如 "Ctrl", "F1"
    const wchar_t* group;       // 分组名（仅用作 ListView group header）
    int            groupId;     // 分组序号（0-based，避免字符串指针比较）
    DWORD          vkPrimary;   // 主 VK 码（用于列表项标识）
    DWORD          vkSecondary; // 次 VK 码（如左右修饰键的另一半），0 表示无
};

// 分组名（按 groupId 索引）
constexpr const wchar_t* g_groupNames[] = {
    L"功能键",       // 0
    L"修饰键",       // 1
    L"导航键",       // 2
    L"特殊键",       // 3
    L"数字键",       // 4
    L"标点符号",     // 5
    L"字母键",       // 6
    L"数字键盘",     // 7
};

constexpr int g_groupCount = sizeof(g_groupNames) / sizeof(g_groupNames[0]);

// ── 所有可黑名单的键 ─────────────────────────────────
//  已按 128 键英文键盘去重：
//    • L/R Ctrl/Alt/Shift/Win → 合并为单条
//    • 数字小键盘 0-9 → 已与主键盘合并
//    • 小键盘 Enter → 已与主键盘合并
const KeyEntry g_blacklistKeys[] = {
    // ── 功能键 (groupId=0) ─────────────────────
    { L"F1",  L"功能键", 0, VK_F1,  0 },
    { L"F2",  L"功能键", 0, VK_F2,  0 },
    { L"F3",  L"功能键", 0, VK_F3,  0 },
    { L"F4",  L"功能键", 0, VK_F4,  0 },
    { L"F5",  L"功能键", 0, VK_F5,  0 },
    { L"F6",  L"功能键", 0, VK_F6,  0 },
    { L"F7",  L"功能键", 0, VK_F7,  0 },
    { L"F8",  L"功能键", 0, VK_F8,  0 },
    { L"F9",  L"功能键", 0, VK_F9,  0 },
    { L"F10", L"功能键", 0, VK_F10, 0 },
    { L"F11", L"功能键", 0, VK_F11, 0 },
    { L"F12", L"功能键", 0, VK_F12, 0 },

    // ── 修饰键 (groupId=1) L/R 合并 ────────────
    { L"Ctrl",  L"修饰键", 1, VK_LCONTROL, VK_RCONTROL },
    { L"Alt",   L"修饰键", 1, VK_LMENU,    VK_RMENU    },
    { L"Shift", L"修饰键", 1, VK_LSHIFT,   VK_RSHIFT   },
    { L"Win",   L"修饰键", 1, VK_LWIN,     VK_RWIN     },

    // ── 导航键 (groupId=2) ─────────────────────
    { L"↑ Up",       L"导航键", 2, VK_UP,     0 },
    { L"↓ Down",     L"导航键", 2, VK_DOWN,   0 },
    { L"← Left",     L"导航键", 2, VK_LEFT,   0 },
    { L"→ Right",    L"导航键", 2, VK_RIGHT,  0 },
    { L"Insert",     L"导航键", 2, VK_INSERT, 0 },
    { L"Delete",     L"导航键", 2, VK_DELETE, 0 },
    { L"Home",       L"导航键", 2, VK_HOME,   0 },
    { L"End",        L"导航键", 2, VK_END,    0 },
    { L"Page Up",    L"导航键", 2, VK_PRIOR,  0 },
    { L"Page Down",  L"导航键", 2, VK_NEXT,   0 },

    // ── 特殊键 (groupId=3) ─────────────────────
    { L"Esc",          L"特殊键", 3, VK_ESCAPE,  0 },
    { L"Tab",          L"特殊键", 3, VK_TAB,     0 },
    { L"Caps Lock",    L"特殊键", 3, VK_CAPITAL, 0 },
    { L"Backspace",    L"特殊键", 3, VK_BACK,    0 },
    { L"Enter",        L"特殊键", 3, VK_RETURN,  0 },
    { L"Space",        L"特殊键", 3, VK_SPACE,   0 },
    { L"Print Screen", L"特殊键", 3, VK_SNAPSHOT,0 },
    { L"Scroll Lock",  L"特殊键", 3, VK_SCROLL,  0 },
    { L"Pause",        L"特殊键", 3, VK_PAUSE,   0 },
    { L"Apps/Menu",    L"特殊键", 3, VK_APPS,    0 },

    // ── 标点符号 + Backtick (groupId=5) ────────
    { L"` (Backtick)", L"标点符号", 5, VK_OEM_3,    0 },

    // ── 数字键 (groupId=4，小键盘0-9已去重) ────
    { L"0",            L"数字键", 4, '0',          0 },
    { L"1",            L"数字键", 4, '1',          0 },
    { L"2",            L"数字键", 4, '2',          0 },
    { L"3",            L"数字键", 4, '3',          0 },
    { L"4",            L"数字键", 4, '4',          0 },
    { L"5",            L"数字键", 4, '5',          0 },
    { L"6",            L"数字键", 4, '6',          0 },
    { L"7",            L"数字键", 4, '7',          0 },
    { L"8",            L"数字键", 4, '8',          0 },
    { L"9",            L"数字键", 4, '9',          0 },

    // ── 标点符号 (groupId=5) ────────────────────
    { L"- (Minus)",    L"标点符号", 5, VK_OEM_MINUS, 0 },
    { L"= (Equals)",   L"标点符号", 5, VK_OEM_PLUS,  0 },
    { L"[ (Bracket)",  L"标点符号", 5, VK_OEM_4,     0 },
    { L"] (Bracket)",  L"标点符号", 5, VK_OEM_6,     0 },
    { L"\\ (Backslash)",L"标点符号",5,VK_OEM_5,     0 },
    { L"; (Semicolon)",L"标点符号", 5, VK_OEM_1,     0 },
    { L"' (Quote)",    L"标点符号", 5, VK_OEM_7,     0 },
    { L", (Comma)",    L"标点符号", 5, VK_OEM_COMMA, 0 },
    { L". (Period)",   L"标点符号", 5, VK_OEM_PERIOD,0 },
    { L"/ (Slash)",    L"标点符号", 5, VK_OEM_2,     0 },

    // ── 字母键 (groupId=6) ─────────────────────
    { L"A", L"字母键", 6, 'A', 0 },
    { L"B", L"字母键", 6, 'B', 0 },
    { L"C", L"字母键", 6, 'C', 0 },
    { L"D", L"字母键", 6, 'D', 0 },
    { L"E", L"字母键", 6, 'E', 0 },
    { L"F", L"字母键", 6, 'F', 0 },
    { L"G", L"字母键", 6, 'G', 0 },
    { L"H", L"字母键", 6, 'H', 0 },
    { L"I", L"字母键", 6, 'I', 0 },
    { L"J", L"字母键", 6, 'J', 0 },
    { L"K", L"字母键", 6, 'K', 0 },
    { L"L", L"字母键", 6, 'L', 0 },
    { L"M", L"字母键", 6, 'M', 0 },
    { L"N", L"字母键", 6, 'N', 0 },
    { L"O", L"字母键", 6, 'O', 0 },
    { L"P", L"字母键", 6, 'P', 0 },
    { L"Q", L"字母键", 6, 'Q', 0 },
    { L"R", L"字母键", 6, 'R', 0 },
    { L"S", L"字母键", 6, 'S', 0 },
    { L"T", L"字母键", 6, 'T', 0 },
    { L"U", L"字母键", 6, 'U', 0 },
    { L"V", L"字母键", 6, 'V', 0 },
    { L"W", L"字母键", 6, 'W', 0 },
    { L"X", L"字母键", 6, 'X', 0 },
    { L"Y", L"字母键", 6, 'Y', 0 },
    { L"Z", L"字母键", 6, 'Z', 0 },

    // ── 数字小键盘 (groupId=7，0-9和Enter已去重)
    { L"Num Lock",     L"数字键盘", 7, VK_NUMLOCK,  0 },
    { L"Num /",        L"数字键盘", 7, VK_DIVIDE,   0 },
    { L"Num *",        L"数字键盘", 7, VK_MULTIPLY, 0 },
    { L"Num -",        L"数字键盘", 7, VK_SUBTRACT, 0 },
    { L"Num +",        L"数字键盘", 7, VK_ADD,      0 },
    { L"Num . (Del)",  L"数字键盘", 7, VK_DECIMAL,  0 },
};

constexpr int g_blacklistKeyCount = sizeof(g_blacklistKeys) / sizeof(g_blacklistKeys[0]);

// ── 弹窗接口 ────────────────────────────────────────────
//  返回 true 表示用户点了确定（黑名单已更新到 engine）
bool ShowKeyBlacklistDialog(HINSTANCE hInst, HWND hParent, SyncEngine& engine);
