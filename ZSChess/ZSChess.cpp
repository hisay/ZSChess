// ZSChess.cpp : 定义应用程序的入口点。
//

#include "framework.h"
#include "ZSChess.h"
#include "CZSChessWin.h"
#include "AutoTrainer.h"
#define MAX_LOADSTRING 100

// 全局变量:
HINSTANCE hInst;                                // 当前实例
WCHAR szTitle[MAX_LOADSTRING];                  // 标题栏文本
WCHAR szWindowClass[MAX_LOADSTRING];            // 主窗口类名

// 此代码模块中包含的函数的前向声明:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

CZSChessWin g_ZSChessWin;

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
	srand((unsigned)time(nullptr));
    // TODO: 在此处放置代码。

    // 初始化全局字符串
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_ZSCHESS, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // 执行应用程序初始化:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_ZSCHESS));

    MSG msg;

    // 主消息循环:
    while (true) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
				if (msg.message == WM_QUIT)
					break;
            }
        }
        else {
			InvalidateRect(g_ZSChessWin.GetHWND(), nullptr, TRUE);
            Sleep(1);
        }
    }
	g_ZSChessWin.ShutdownGDIPlus();
    return (int) msg.wParam;
}



//
//  函数: MyRegisterClass()
//
//  目标: 注册窗口类。
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ZSCHESS));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_ZSCHESS);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   函数: InitInstance(HINSTANCE, int)
//
//   目标: 保存实例句柄并创建主窗口
//
//   注释:
//
//        在此函数中，我们在全局变量中保存实例句柄并
//        创建和显示主程序窗口。
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // 将实例句柄存储在全局变量中

   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }
   
   g_ZSChessWin.SetHWND(hWnd);
   g_ZSChessWin.InitGDIPlus();
   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   // 命令行 --train：无对话框直接自动启动训练（红中级／黑高级／50局）
   //   加 --show 则在主界面实时演示每一步落子；
   //   不加 --show 则隐藏窗口、无界面纯后台训练。
   {
       LPWSTR cmd = GetCommandLineW();
       if (cmd && wcsstr(cmd, L"--train")) {
           bool show = (wcsstr(cmd, L"--show") != nullptr);
           extern int GetConfiguredAIDepth();
           extern int GetConfiguredAITimeSec();
           extern int GetConfiguredAIMaxThreads();
           extern int GetTrainRedTier();
           extern int GetTrainBlackTier();
           extern int GetTrainGames();
           AutoTrainer::Config cfg;
           // 红/黑方等级读取持久化配置（训练对话框中设置并保存到 ai_config.ini），
           // 不再代码硬编码；未配置时用默认（红中级/黑高级）
           cfg.redTier = (AITier)GetTrainRedTier();
           cfg.blackTier = (AITier)GetTrainBlackTier();
           cfg.games = GetTrainGames(); // 持久化的目标局数（训练对话框保存到 ai_config.ini）
           cfg.saveRecords = true;
           cfg.trainBook = true;
           cfg.cfgDepth = GetConfiguredAIDepth() > 0 ? GetConfiguredAIDepth() : 20;
           cfg.cfgTimeMs = GetConfiguredAITimeSec() > 0 ? GetConfiguredAITimeSec() * 1000 : 5000;
           cfg.cfgThreads = GetConfiguredAIMaxThreads() > 0 ? GetConfiguredAIMaxThreads() : 8;
           if (show) {
  
               g_ZSChessWin.NewGame();
               g_ZSChessWin.SetTrainShowMode(true);
               GetAutoTrainer().SetShowMode(true, hWnd);
           } else {
               // 无界面后台训练：隐藏窗口，全速运行（保留用户配置的深度搜索）
               ShowWindow(hWnd, SW_HIDE);
               GetAutoTrainer().SetShowMode(false, nullptr);
           }
           AutoTrainer::instance().Start(cfg);
       }
   }
   // 初始化菜单项勾选状态，根据默认 AI 开关
   {
       HMENU hMenu = GetMenu(hWnd);
       if (hMenu) {
           CheckMenuItem(hMenu, ID_BLACKAI, MF_BYCOMMAND | (g_ZSChessWin.IsAutoAI(BLACK) ? MF_CHECKED : MF_UNCHECKED));
           CheckMenuItem(hMenu, ID_REDAI, MF_BYCOMMAND | (g_ZSChessWin.IsAutoAI(RED) ? MF_CHECKED : MF_UNCHECKED));
       }
   }
   

   return TRUE;
}

//
//  函数: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  目标: 处理主窗口的消息。
//
//  WM_COMMAND  - 处理应用程序菜单
//  WM_PAINT    - 绘制主窗口
//  WM_DESTROY  - 发送退出消息并返回
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_APP + 103:
    {
        // --show --train ：训练开始新一局，重置界面棋盘
        g_ZSChessWin.NewGame();
        {
            extern AutoTrainer& GetAutoTrainer();
            HANDLE hEv = GetAutoTrainer().StepDoneEvent();
            if (hEv) SetEvent(hEv);
        }
        return 0;
    }

    case WM_APP + 102:
    {
        // --show --train 实时展示步：wParam=to, lParam=from （与 101 同编码）
        short tfcol = (short)LOWORD(lParam);
        short tfrow = (short)HIWORD(lParam);
        short ttcol = (short)LOWORD(wParam);
        short ttrow = (short)HIWORD(wParam);
        
 
                extern AutoTrainer& GetAutoTrainer();
                HANDLE hEv = GetAutoTrainer().StepDoneEvent();
                g_ZSChessWin.SetTrainStepDoneHandle(hEv);
       

        if (tfcol >= 0 && tfrow >= 0 && ttcol >= 0 && ttrow >= 0
            && tfcol <= 10 && tfrow <= 10 && ttcol <= 10 && ttrow <= 10) {
            PSF::POS tfrom{ tfcol, tfrow }, tto{ ttcol, ttrow };
            

            g_ZSChessWin.ApplyAIMove(tfrom, tto);
        }
        else {
			SetEvent(GetAutoTrainer().StepDoneEvent()); // 训练线程不能干等)
        }

        return 0;
    }

    case WM_APP + 101:
    {
        // wParam: to(col,row) packed, lParam: from(col,row) packed
        short fcol = (short)LOWORD(lParam);
        short frow = (short)HIWORD(lParam);
        short tcol = (short)LOWORD(wParam);
        short trow = (short)HIWORD(wParam);
		if (fcol == tcol && fcol == 0 && frow == trow && frow == 0) {
			//MessageBoxW(hWnd, L"AI 无可解将走法，该方被判负或需人工处理", L"提示", MB_OK | MB_ICONINFORMATION);
            g_ZSChessWin.PlayWuJie();
			return 0;
		}
        // sentinel (-1,-1) indicates no-solution
        if (fcol == -1 && frow == -1 && tcol == -1 && trow == -1) {
            g_ZSChessWin.PlayWuJie();
            //MessageBoxW(hWnd, L"AI 无可解将走法，该方被判负或需人工处理", L"提示", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        PSF::POS from, to;
        from.col = fcol; from.row = frow;
        to.col = tcol; to.row = trow;
        g_ZSChessWin.ApplyAIMove(from, to);
        return 0;
    }

    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // 分析菜单选择:
            switch (wmId)
            {
            case ID_CONTINUEQIJU:
                g_ZSChessWin.ContinueQiJu();
                break;
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
				g_ZSChessWin.StopAI(); // 退出前取消任何正在进行的 AI 计算
				{
				    // 停止后台自动训练（当前局结束后退出）
				    extern AutoTrainer& GetAutoTrainer();
				    GetAutoTrainer().Stop();
				}
                DestroyWindow(hWnd);
                break;
            case ID_NEWGAME:
                g_ZSChessWin.NewGame();
                break;
            case ID_BACKSTEP:
                g_ZSChessWin.BackStep();
                break;
            case ID_RECORD_STEPBACK:
                g_ZSChessWin.StepBackRecord();
                break;
            case ID_RECORD_STEPFWD:
                g_ZSChessWin.StepForwardRecord();
                break;
            case ID_RECORD_SAVE:
                g_ZSChessWin.SaveRecord();
                break;
            case ID_RECORD_LOAD:
                g_ZSChessWin.LoadRecord();
                break;
            case ID_RECORD_COPY:
                g_ZSChessWin.CopyPosition();
                break;
            case ID_BLACKAI:
                g_ZSChessWin.SetAutoAI(BLACK, !g_ZSChessWin.IsAutoAI(BLACK));
                // 更新菜单勾选
                {
                    HMENU hm = GetMenu(hWnd);
                    if (hm) CheckMenuItem(hm, ID_BLACKAI, MF_BYCOMMAND | (g_ZSChessWin.IsAutoAI(BLACK) ? MF_CHECKED : MF_UNCHECKED));
                }
                break;
            case ID_REDAI:
                g_ZSChessWin.SetAutoAI(RED, !g_ZSChessWin.IsAutoAI(RED));
                // 更新菜单勾选
                {
                    HMENU hm = GetMenu(hWnd);
                    if (hm) CheckMenuItem(hm, ID_REDAI, MF_BYCOMMAND | (g_ZSChessWin.IsAutoAI(RED) ? MF_CHECKED : MF_UNCHECKED));
                }
                break;
            case ID_AI_CONFIG:
                {
                    extern void ShowAIConfig(HWND parent);
                    ShowAIConfig(hWnd);
                }
                break;
            case ID_TRAIN_MODE:
                {
                    extern void ShowTrainConfig(HWND parent);
                    ShowTrainConfig(hWnd);
                }
                break;
            case ID_STOP_TRAIN:
                {
                    AutoTrainer::instance().RequestStop();
                    MessageBoxW(hWnd, L"已请求停止训练：当前步完成后自动中止，不再开新局。",
                        L"自动训练模式", MB_OK | MB_ICONINFORMATION);
                }
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_ERASEBKGND:
        return TRUE;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            BeginPaint(hWnd, &ps);
			g_ZSChessWin.OnDraw();
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_DESTROY:
        GetAutoTrainer().Stop(); // 关闭窗口时停止后台训练（保存当前库）
        PostQuitMessage(0);
        break;
	case WM_LBUTTONDOWN:
	{
		int mouseX = LOWORD(lParam);
		int mouseY = HIWORD(lParam);
	 
        g_ZSChessWin.OnMouseClick(mouseX, mouseY);
		// 若按在棋谱滚动条滑块上，捕获鼠标以便拖动
		if (g_ZSChessWin.IsScrollDragging()) SetCapture(hWnd);
	}
    break;
    case WM_MOUSEMOVE:
    {
        int mouseX = LOWORD(lParam);
        int mouseY = HIWORD(lParam);
        g_ZSChessWin.MouseMove(mouseX, mouseY);
    }
    break;
    case WM_LBUTTONUP:
    {
        int mouseX = LOWORD(lParam);
        int mouseY = HIWORD(lParam);
        g_ZSChessWin.MouseUp(mouseX, mouseY);
        if (GetCapture() == hWnd) ReleaseCapture();
    }
    break;
    case WM_MOUSEWHEEL:
    {
        // 滚轮滚动棋谱列表（仅鼠标位于面板区域时生效）
        int delta = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        POINT pt;
        pt.x = (short)LOWORD(lParam);
        pt.y = (short)HIWORD(lParam);
        ScreenToClient(hWnd, &pt);
        if (g_ZSChessWin.ScrollRecord(delta, pt.x, pt.y)) return 0;
        break;
    }
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// “关于”框的消息处理程序。
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

void CZSChessWin::StopAI()
{
    // 关闭两方的自动 AI；若你有具体的枚举名（如 PieceColor::Black / ::White），可替换 (PieceColor)0/(PieceColor)1
    SetAutoAI((PieceColor)0, false);
    SetAutoAI((PieceColor)1, false);
}
