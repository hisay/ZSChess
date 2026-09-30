// TrainConfigDlg.h
#pragma once
#include <Windows.h>
#include "Resource.h"
#include "AITier.h"
#include "AutoTrainer.h"

class TrainConfigDlg {
public:
    static void Show(HWND parent);
private:
    static INT_PTR CALLBACK DlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
};

void ShowTrainConfig(HWND parent);
