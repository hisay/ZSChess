#include "AIConfigDlg.h"
#include "CZSChessWin.h"
#include "AITier.h"
#include <commctrl.h>
#include <string>
#include <windows.h>

int AIConfigDlg::aiDepth = 3;
int AIConfigDlg::maxThreads = 0; // 0 => use default (auto)
bool AIConfigDlg::enableHeuristics = true;
bool AIConfigDlg::preferMate = true;
int AIConfigDlg::aiTimeSec = 2; // seconds per move (0 = depth-limited only)
int AIConfigDlg::aiLevel = 5;    // 默认特级（用满配置）
bool AIConfigDlg::useAiTier = false; // 默认不勾选"使用AI等级参数"（手动配置生效）
int AIConfigDlg::trainRedTier = 2;   // 默认红方 中级
int AIConfigDlg::trainBlackTier = 3; // 默认黑方 高级
int AIConfigDlg::trainGames = 50;    // 默认目标 50 局

void AIConfigDlg::LoadConfig() {
    // 用 Windows 内置 INI API：GetPrivateProfileIntW 自带缺省值，无需手写解析
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) *slash = 0;
    wcscat_s(path, L"\\ai_config.ini");

    // 全局键统一放 [config] 段（无段区读取在部分文件上不可靠）
    aiDepth = GetPrivateProfileIntW(L"config", L"depth", aiDepth, path);
    maxThreads = GetPrivateProfileIntW(L"config", L"maxThreads", maxThreads, path);
    enableHeuristics = GetPrivateProfileIntW(L"config", L"enableHeuristics", enableHeuristics ? 1 : 0, path) != 0;
    preferMate = GetPrivateProfileIntW(L"config", L"preferMate", preferMate ? 1 : 0, path) != 0;
    aiTimeSec = GetPrivateProfileIntW(L"config", L"timeSec", aiTimeSec, path);
    aiLevel = GetPrivateProfileIntW(L"config", L"aiLevel", aiLevel, path);
    if (aiLevel < 0) aiLevel = 0;
    if (aiLevel > 5) aiLevel = 5;
    useAiTier = GetPrivateProfileIntW(L"config", L"useAiTier", useAiTier ? 1 : 0, path) != 0;
    TierPresetData().useTier = useAiTier;
    trainRedTier = GetPrivateProfileIntW(L"config", L"trainRedTier", trainRedTier, path);
    if (trainRedTier < 0 || trainRedTier > 5) trainRedTier = 2;
    trainBlackTier = GetPrivateProfileIntW(L"config", L"trainBlackTier", trainBlackTier, path);
    if (trainBlackTier < 0 || trainBlackTier > 5) trainBlackTier = 3;
    trainGames = GetPrivateProfileIntW(L"config", L"trainGames", trainGames, path);
    if (trainGames < 1) trainGames = 1;

    // 六档等级参数 [level0..5]
    wchar_t section[16], buf[32];
    for (int i = 0; i < 6; i++) {
        swprintf_s(section, L"level%d", i);
        int th = GetPrivateProfileIntW(section, L"thread", TierPresetData().threads[i], path);
        int tm = GetPrivateProfileIntW(section, L"time", TierPresetData().timeMs[i], path);
        int dp = GetPrivateProfileIntW(section, L"depth", TierPresetData().depth[i], path);
        if (th > 0) TierPresetData().threads[i] = th;
        if (tm > 0) TierPresetData().timeMs[i] = tm;
        if (dp > 0) TierPresetData().depth[i] = dp;
        (void)buf;
    }
}

void AIConfigDlg::SaveConfig() {
    // 用 Windows 内置 INI API：WritePrivateProfileStringW 自动建文件/覆盖键值
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) *slash = 0;
    wcscat_s(path, L"\\ai_config.ini");

    wchar_t buf[32];
    auto wi = [&](const wchar_t* key, int v) {
        swprintf_s(buf, L"%d", v);
        WritePrivateProfileStringW(L"config", key, buf, path);
    };
    wi(L"depth", aiDepth);
    wi(L"maxThreads", maxThreads);
    wi(L"enableHeuristics", enableHeuristics ? 1 : 0);
    wi(L"preferMate", preferMate ? 1 : 0);
    wi(L"timeSec", aiTimeSec);
    wi(L"aiLevel", aiLevel);
    wi(L"useAiTier", useAiTier ? 1 : 0);
    wi(L"trainRedTier", trainRedTier);
    wi(L"trainBlackTier", trainBlackTier);
    wi(L"trainGames", trainGames);

    // 六档等级参数（深度/时限/线程）写回 [level0..5] 段
    wchar_t section[16];
    for (int i = 0; i < 6; i++) {
        swprintf_s(section, L"level%d", i);
        swprintf_s(buf, L"%d", TierPresetData().threads[i]);
        WritePrivateProfileStringW(section, L"thread", buf, path);
        swprintf_s(buf, L"%d", TierPresetData().timeMs[i]);
        WritePrivateProfileStringW(section, L"time", buf, path);
        swprintf_s(buf, L"%d", TierPresetData().depth[i]);
        WritePrivateProfileStringW(section, L"depth", buf, path);
    }
}

INT_PTR CALLBACK AIConfigDlg::DlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_INITDIALOG: {
        // Initialize depth control
        AIConfigDlg::LoadConfig();
        {
            HWND hLv = GetDlgItem(hwndDlg, IDC_AI_LEVEL);
            if (hLv) {
                const wchar_t* lvs[] = { L"新手", L"入门", L"中级", L"高级", L"大师", L"特级" };
                for (int i = 0; i < 6; i++) SendMessageW(hLv, CB_ADDSTRING, 0, (LPARAM)lvs[i]);
                SendMessageW(hLv, CB_SETCURSEL, (WPARAM)aiLevel, 0);
            }
        }
        HWND hEd = GetDlgItem(hwndDlg, IDC_AI_DEPTH);
        wchar_t buf[16];
        wsprintfW(buf, L"%d", aiDepth);
        SetWindowTextW(hEd, buf);
        // initialize other controls
        HWND hThreads = GetDlgItem(hwndDlg, IDC_AI_MAXTHREADS);
        if (hThreads) {
            wchar_t tbuf[16]; wsprintfW(tbuf, L"%d", maxThreads);
            SetWindowTextW(hThreads, tbuf);
        }
        HWND hHeur = GetDlgItem(hwndDlg, IDC_AI_HEUR);
        if (hHeur) SendMessageW(hHeur, BM_SETCHECK, enableHeuristics ? BST_CHECKED : BST_UNCHECKED, 0);
        HWND hPref = GetDlgItem(hwndDlg, IDC_AI_PREFERMATE);
        if (hPref) SendMessageW(hPref, BM_SETCHECK, preferMate ? BST_CHECKED : BST_UNCHECKED, 0);
        HWND hTime = GetDlgItem(hwndDlg, IDC_AI_TIME);
        if (hTime) {
            wchar_t tbuf[16]; wsprintfW(tbuf, L"%d", aiTimeSec);
            SetWindowTextW(hTime, tbuf);
        }
        HWND hUse = GetDlgItem(hwndDlg, IDC_AI_USE_TIER);
        if (hUse) SendMessageW(hUse, BM_SETCHECK, useAiTier ? BST_CHECKED : BST_UNCHECKED, 0);
        return TRUE;
    }
    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case IDOK: {
            HWND hLv = GetDlgItem(hwndDlg, IDC_AI_LEVEL);
            if (hLv) {
                LRESULT s = SendMessageW(hLv, CB_GETCURSEL, 0, 0);
                if (s >= 0 && s <= 5) aiLevel = (int)s;
            }
            HWND hEd = GetDlgItem(hwndDlg, IDC_AI_DEPTH);
            wchar_t buf[16];
            GetWindowTextW(hEd, buf, _countof(buf));
            int val = _wtoi(buf);
            if (val < 1) val = 1;
            if (val > 64) val = 64; // 引擎 MAX_PLY=128，UI 放宽到 64
            aiDepth = val;
            HWND hThreads = GetDlgItem(hwndDlg, IDC_AI_MAXTHREADS);
            if (hThreads) {
                wchar_t tbuf[16]; GetWindowTextW(hThreads, tbuf, _countof(tbuf));
                int mt = _wtoi(tbuf); if (mt < 0) mt = 0; if (mt > 128) mt = 128; maxThreads = mt;
            }
            HWND hHeur = GetDlgItem(hwndDlg, IDC_AI_HEUR);
            if (hHeur) {
                LRESULT c = SendMessageW(hHeur, BM_GETCHECK, 0, 0);
                enableHeuristics = (c == BST_CHECKED);
            }
            HWND hPref = GetDlgItem(hwndDlg, IDC_AI_PREFERMATE);
            if (hPref) {
                LRESULT c = SendMessageW(hPref, BM_GETCHECK, 0, 0);
                preferMate = (c == BST_CHECKED);
            }
            HWND hTime = GetDlgItem(hwndDlg, IDC_AI_TIME);
            if (hTime) {
                wchar_t tbuf[16]; GetWindowTextW(hTime, tbuf, _countof(tbuf));
                int ts = _wtoi(tbuf);
                if (ts < 0) ts = 0;
                if (ts > 5*60) ts = 5*60;
                aiTimeSec = ts;
            }
            HWND hUse = GetDlgItem(hwndDlg, IDC_AI_USE_TIER);
            if (hUse) {
                LRESULT c = SendMessageW(hUse, BM_GETCHECK, 0, 0);
                useAiTier = (c == BST_CHECKED);
                TierPresetData().useTier = useAiTier; // 立即生效到引擎等级参数源
            }
            // save config
            AIConfigDlg::SaveConfig();
            EndDialog(hwndDlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(hwndDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

void AIConfigDlg::Show(HWND parent) {
    DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_AI_CONFIG), parent, DlgProc);
}

// C-style wrapper used by WndProc
void ShowAIConfig(HWND parent) {
    AIConfigDlg::Show(parent);
}

// Bridge functions used by CAIPlayer
int GetConfiguredAIDepth() { return AIConfigDlg::aiDepth; }
int GetConfiguredAITier() { return AIConfigDlg::aiLevel; }
int GetConfiguredAIMaxThreads() { return AIConfigDlg::maxThreads; }
bool GetConfiguredAIHeuristics() { return AIConfigDlg::enableHeuristics; }
bool GetConfiguredAIPreferMate() { return AIConfigDlg::preferMate; }
int GetConfiguredAITimeSec() { return AIConfigDlg::aiTimeSec; }
bool GetConfiguredUseAiTier() { return AIConfigDlg::useAiTier; }
int GetTrainRedTier() { return AIConfigDlg::trainRedTier; }
int GetTrainBlackTier() { return AIConfigDlg::trainBlackTier; }
int GetTrainGames() { return AIConfigDlg::trainGames; }
void SetTrainGames(int n) {
    if (n < 0) n = 0;
    AIConfigDlg::trainGames = n;
    AIConfigDlg::SaveConfig(); // 立即持久化到 ai_config.ini
}
void SetTrainTiers(int red, int black) {
    if (red < 0) red = 0; if (red > 5) red = 5;
    if (black < 0) black = 0; if (black > 5) black = 5;
    AIConfigDlg::trainRedTier = red;
    AIConfigDlg::trainBlackTier = black;
    AIConfigDlg::SaveConfig(); // 立即持久化到 ai_config.ini
}

// Ensure the bridge functions use saved config by loading once on startup
static struct _AIConfigInit { _AIConfigInit() { AIConfigDlg::LoadConfig(); } } _aiCfgInit;
