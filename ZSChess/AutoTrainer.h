// AutoTrainer.h
// 全自动 AI 对弈训练器：双 AI 按指定等级循环对弈，自动检测将杀/困毙/超长和棋，
// 保存棋谱（文件名含日期+胜负）、把每步"局面→评分"训练进神经网络库（nn_lib/tier0..5.bin），
// 自动开始下一局。后台线程运行，界面可随时查看进度/停止。
#pragma once
#include <Windows.h>
#include <atomic>
#include <thread>
#include <vector>
#include <string>
#include "AITier.h"
#include "CChessPiece.h"
#include "CGameRecord.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "evaluate.h"
#include "NNLib.h"

class AutoTrainer {
public:
    struct Config {
        AITier redTier = AITier::INTERMEDIATE;   // 红方等级
        AITier blackTier = AITier::INTERMEDIATE; // 黑方等级
        int games = 0;          // 目标局数（0 = 无限，直到手动停止）
        bool saveRecords = true; // 每局保存棋谱
        bool trainBook = true;   // 训练神经网络库
        int cfgDepth = 20;       // 用户配置深度上限（特级用满）
        int cfgTimeMs = 5000;    // 用户配置时限上限（特级用满）
        int cfgThreads = 8;      // 用户配置线程上限（特级用满）
    };

    static AutoTrainer& instance();

    void Start(const Config& cfg);
    void Stop();                 // 安全停止（当前局结束后退出）
    void RequestStop();          // 立即请求停止（不等待线程，训练线程尽快中止当前局）
    void SetTargetGames(int n);  // 训练运行中实时更新目标局数
    bool Running() const { return m_running.load(); }

    // --show --train 实时演示：训练线程每走一步通过 WM_APP+102 通知主窗口
    // 展示落子，并等待界面处理完（以界面显示为准，训练不会跑在界面前面）。
    void SetShowMode(bool show, HWND hwnd);
    // 界面处理完当前步后 SetEvent，训练线程据此继续下一步
    HANDLE StepDoneEvent() const { return m_stepDone; }
    void OnRandomSeedStep(zschess::Position& pos,  std::vector<CChessPiece>& uiBoard,   std::vector<RecordStep>& steps);
    // ---- 实时统计（供界面显示）----
    int GamesPlayed() const { return m_gamesPlayed.load(); }
    int RedWins() const { return m_redWins.load(); }
    int BlackWins() const { return m_blackWins.load(); }
    int Draws() const { return m_draws.load(); }
    int CurrentGame() const { return m_currentGame.load(); }
    int TargetGames() const { return m_cfg.games; }
    AITier RedTier() const { return m_cfg.redTier; }
    AITier BlackTier() const { return m_cfg.blackTier; }
    // 最近一局结果（"红胜" / "黑胜" / "和棋"）
    std::wstring LastResult() const;

private:
    AutoTrainer() = default;
    void Loop();
    void PlayOneGame();
    zschess::Move PickMove(zschess::Position& pos, AITier tier);
    zschess::Move NoisyMove(zschess::Position& pos, zschess::Move avoid, int noiseCenti);
    std::vector<CChessPiece> MakeInitialUI() const;
    static void PieceLabel(zschess::Piece p, wchar_t* out);

    Config m_cfg;
    std::atomic<bool> m_running{ false };
    std::thread* m_pThread  = nullptr;
    std::atomic<int> m_currentGame{ 0 };
    std::atomic<int> m_gamesPlayed{ 0 };
	std::atomic<int> m_targetGames{ 0 };   // 目标局数（可被对话框实时更新）
    std::atomic<int> m_redWins{ 0 };
    std::atomic<int> m_blackWins{ 0 };
    std::atomic<int> m_draws{ 0 };
    mutable std::mutex m_lastMutex;
    std::wstring m_lastResult;
    bool m_showMode = false;     // 实时演示模式（--show --train）
    HWND m_hwnd = nullptr;       // 主窗口句柄
    HANDLE m_stepDone = nullptr; // 每步屏幕展示完成事件
};

// 导出单例访问（供 ZSChess.cpp 菜单/退出处理调用）
inline AutoTrainer& GetAutoTrainer() { return AutoTrainer::instance(); }
