// TrainConfigDlg.cpp
// 自动训练模式配置对话框：选择红/黑方 AI 等级、局数，启动后台自动对弈训练。
#include "TrainConfigDlg.h"
#include "AIConfigBridge.h"
#include <cstdlib>
#include "CZSChessWin.h"
void ShowTrainConfig(HWND parent) { TrainConfigDlg::Show(parent); }

void TrainConfigDlg::Show(HWND parent) {
    // 允许训练运行中打开：修改的局数会实时传给运行中的训练实例，
    // 修改的等级会持久化并在下次启动训练时生效。
    DialogBox(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDD_TRAIN_CONFIG), parent, DlgProc);
}

INT_PTR CALLBACK TrainConfigDlg::DlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_INITDIALOG: {
        // 填充红/黑方等级下拉框
        const wchar_t* tiers[] = { L"新手", L"入门", L"中级", L"高级", L"大师", L"特级" };
        HWND hRed = GetDlgItem(hwndDlg, IDC_TRAIN_RED_TIER);
        HWND hBlack = GetDlgItem(hwndDlg, IDC_TRAIN_BLACK_TIER);
        for (int i = 0; i < 6; i++) {
            SendMessageW(hRed, CB_ADDSTRING, 0, (LPARAM)tiers[i]);
            SendMessageW(hBlack, CB_ADDSTRING, 0, (LPARAM)tiers[i]);
        }
        // 回填持久化的红/黑方等级（ai_config.ini 的 trainRedTier/trainBlackTier）
        // 未配置时默认：红方 中级、黑方 高级（一强一弱更易产生多样局面）
        int redDef = GetTrainRedTier();
        int blackDef = GetTrainBlackTier();
        SendMessageW(hRed, CB_SETCURSEL, (WPARAM)redDef, 0);
        SendMessageW(hBlack, CB_SETCURSEL, (WPARAM)blackDef, 0);
        SetDlgItemInt(hwndDlg, IDC_TRAIN_GAMES, GetTrainGames(), FALSE); // 回填持久化局数
        return (INT_PTR)TRUE;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == IDOK) {
            int redIdx = (int)SendMessageW(GetDlgItem(hwndDlg, IDC_TRAIN_RED_TIER), CB_GETCURSEL, 0, 0);
            int blackIdx = (int)SendMessageW(GetDlgItem(hwndDlg, IDC_TRAIN_BLACK_TIER), CB_GETCURSEL, 0, 0);
            if (redIdx < 0) redIdx = GetTrainRedTier();
            if (blackIdx < 0) blackIdx = GetTrainBlackTier();
            // 保存红/黑方等级到 ai_config.ini（持久化，--train 命令行也读这个）
            SetTrainTiers(redIdx, blackIdx);
            BOOL ok = FALSE;
            int games = (int)GetDlgItemInt(hwndDlg, IDC_TRAIN_GAMES, &ok, FALSE);
            if (!ok || games < 0) games = 50;
            SetTrainGames(games); // 永久化目标局数到 ai_config.ini

            if (AutoTrainer::instance().Running()) {
                // 训练运行中：仅实时更新目标局数（到达后自动停止），不重启训练
                AutoTrainer::instance().SetTargetGames(games);
                EndDialog(hwndDlg, IDOK);
                return (INT_PTR)TRUE;
            }

            AutoTrainer::Config cfg;
            cfg.redTier = (AITier)redIdx;
            cfg.blackTier = (AITier)blackIdx;
            cfg.games = games;
            cfg.saveRecords = true;
            cfg.trainBook = true;
            // 用满当前 AI 设置作为特级上限
            int d = GetConfiguredAIDepth();
            if (d <= 0) d = 20;
            cfg.cfgDepth = d;
            cfg.cfgTimeMs = GetConfiguredAITimeSec() > 0 ? GetConfiguredAITimeSec() * 1000 : 5000;
            cfg.cfgThreads = GetConfiguredAIMaxThreads() > 0 ? GetConfiguredAIMaxThreads() : 8;
            extern CZSChessWin g_ZSChessWin;
            g_ZSChessWin.SetTrainShowMode(true);
            AutoTrainer::instance().SetShowMode(true, GetParent(hwndDlg));
            AutoTrainer::instance().Start(cfg);
            
            EndDialog(hwndDlg, IDOK);
            return (INT_PTR)TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hwndDlg, IDCANCEL);
            return (INT_PTR)TRUE;
        }
        break;
    }
    case WM_CLOSE:
        EndDialog(hwndDlg, IDCANCEL);
        return (INT_PTR)TRUE;
    }
    return (INT_PTR)FALSE;
}
