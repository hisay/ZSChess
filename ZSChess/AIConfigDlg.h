#pragma once
#include <windows.h>
#include "Resource.h"

class AIConfigDlg {
public:
	static INT_PTR CALLBACK DlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
	static void Show(HWND parent); // show configuration dialog
	static int aiDepth;
	static int maxThreads;
	static bool enableHeuristics;
	static bool preferMate;
	static int aiTimeSec; // think time per move in seconds (0 = use depth only)
	static int aiLevel;   // 0=新手 1=入门 2=中级 3=高级 4=大师 5=特级（默认特级=用满配置）
	static bool useAiTier; // 勾选"使用AI等级参数"：等级参数直接生效
	// 自动训练模式红/黑方 AI 等级（持久化于 ai_config.ini，可加载可保存）
	static int trainRedTier;   // 0=新手 1=入门 2=中级 3=高级 4=大师 5=特级
	static int trainBlackTier;
	static int trainGames;   // 自动训练目标局数（持久化，可实时传给运行中实例）
	static void LoadConfig();
	static void SaveConfig();
};
