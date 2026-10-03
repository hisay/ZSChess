// AutoTrainer.cpp
#include "AutoTrainer.h"
#include "CZSAIPlayer.h"
#include "RepetitionGuard.h"
#include "ZSSync.h"
#include <cstdlib>
#include <ctime>
#include <climits>
#include <io.h>
#include <sys/stat.h>
#include "CPieceMng.h"
static std::wstring ExeDirW() {
    wchar_t buf[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    size_t s = p.find_last_of(L"/\\");
    if (s != std::wstring::npos) p = p.substr(0, s);
    return p;
}

AutoTrainer& AutoTrainer::instance() {
    static AutoTrainer t;
    return t;
}

std::wstring AutoTrainer::LastResult() const {
    std::lock_guard<std::mutex> lk(m_lastMutex);
    return m_lastResult;
}

// 引擎棋子 -> 中文名（与界面记法一致）
void AutoTrainer::PieceLabel(zschess::Piece p, wchar_t* out) {
    if (!out) return;
    wcscpy_s(out, 8, L"");
    if (p == zschess::EMPTY) return;
    zschess::PieceType pt = zschess::piece_type(p);
    zschess::Color c = zschess::piece_color(p);
    const wchar_t* n = L"?";
    switch (pt) {
    case zschess::JIANG: n = (c == zschess::RED) ? L"帅" : L"将"; break;
    case zschess::DU:    n = L"督"; break;
    case zschess::CHE:   n = (c == zschess::RED) ? L"車" : L"俥"; break;
    case zschess::MA:    n = (c == zschess::RED) ? L"馬" : L"傌"; break;
    case zschess::XIANG: n = (c == zschess::RED) ? L"相" : L"像"; break;
    case zschess::SHI:   n = (c == zschess::RED) ? L"士" : L"仕"; break;
    case zschess::HOU:   n = L"后"; break;
    case zschess::JUN:   n = L"军"; break;
    case zschess::PAO:   n = L"炮"; break;
    case zschess::BING:  n = (c == zschess::RED) ? L"兵" : L"卒"; break;
    default: break;
    }
    wcscpy_s(out, 8, n);
}

// 初始 UI 棋盘（与引擎 set_initial_position 同布局）
std::vector<CChessPiece> AutoTrainer::MakeInitialUI() const {
    std::vector<CChessPiece> v;
    auto add = [&v](int x, int y, const wchar_t* label, PieceColor color, int type) {
        v.emplace_back(x, y, label, color, type);
    };
    extern PieceData initialPieces[];
    extern const int initialPiecesCount;
    for (int i = 0; i < initialPiecesCount; i++) {
        const auto& pd = initialPieces[i];
        add(pd.x, pd.y, pd.label, pd.color, (int)pd.type);
    }
    return v;
}

void AutoTrainer::Start(const Config& cfg) {
    if (m_running.load()) return;
	  Stop();
    m_cfg = cfg;
    m_currentGame = 0;
    m_gamesPlayed = 0;
    m_targetGames = cfg.games;
    m_redWins = 0;
    m_blackWins = 0;
    m_draws = 0;
    {
        std::lock_guard<std::mutex> lk(m_lastMutex);
        m_lastResult = L"训练启动";
    }
    srand((unsigned)time(nullptr));
    m_running = true;
    m_pThread = new std::thread([this] { Loop(); });
}

void AutoTrainer::RequestStop() {
    m_running = false;                 // 训练线程在下一步循环/等待点立即退出当前局
    if (m_stepDone) SetEvent(m_stepDone); // 释放正在 INFINITE 等待界面动画的训练线程
}

void AutoTrainer::SetTargetGames(int n) {
    if (n < 0) n = 0;
    m_targetGames = n;
}

void AutoTrainer::Stop() {
    RequestStop();
    //if (!m_running.load()) return;
    m_running = false;             // 置位后循环在当前局结束时退出
    // 释放正在 INFINITE 等待界面动画的训练线程，
    // 否则 Stop() 的 join 会在关窗口/退出时永久卡死
    if (m_stepDone) SetEvent(m_stepDone);
    zschess::ThreadPool::instance().stop_all();
    if (m_pThread && m_pThread->joinable()) {
        m_pThread->join();
        delete m_pThread;
        m_pThread = nullptr;
    }
}

// 低等级：以概率选"非最佳"着法（静态评估+噪声），模拟人类不总走最优
zschess::Move AutoTrainer::NoisyMove(zschess::Position& pos, zschess::Move avoid, int noiseCenti) {
    zschess::Move ml[256];
    int n = pos.generate_legal_moves(ml);
    if (n <= 1) return zschess::MOVE_NONE;
    int bestIdx = -1;
    int bestVal = INT_MIN;
    for (int i = 0; i < n; i++) {
        if (ml[i] == avoid) continue;
        zschess::Position cp = pos;
        if (!cp.do_move(ml[i])) continue;
        int v = zschess::Eval::evaluate(cp);           // cp.stm 视角
        if (cp.side_to_move() == zschess::BLACK) v = -v; // 统一红方视角
        if (noiseCenti > 0) v += (rand() % (2 * noiseCenti + 1)) - noiseCenti;
        if (v > bestVal) { bestVal = v; bestIdx = i; }
    }
    return bestIdx >= 0 ? ml[bestIdx] : zschess::MOVE_NONE;
}

// --show --train 实时演示设置
void AutoTrainer::SetShowMode(bool show, HWND hwnd) {
    m_showMode = show;
    m_hwnd = hwnd;
    if (m_stepDone) { CloseHandle(m_stepDone); m_stepDone = nullptr; }
    if (show) m_stepDone = CreateEventW(nullptr, TRUE, TRUE, nullptr);
}

// 每步决策：神经网络优先 -> 搜索 -> 在线学习 -> 低等级扰动
zschess::Move AutoTrainer::PickMove(zschess::Position& pos, AITier tier) {
    int lv = (int)tier; if (lv < 0) lv = 0; if (lv > 5) lv = 5;
    TierConfig tc = TierToConfig(tier, m_cfg.cfgDepth, m_cfg.cfgTimeMs, m_cfg.cfgThreads);

    // 1) 精确着法库优先：相同局面【命中本级库】直接出棋，微秒~毫秒级
    if (m_cfg.trainBook) {
        zschess::Move bm; int bsc = 0, bdep = 0;
        if (zschess::Book::instance().probe_exact(pos.hash(), lv, bm, bsc, bdep)
            && pos.is_legal(bm)
            && !CZSAIPlayer::IsLongCheckBlunder(pos, bm)
            && !CZSAIPlayer::IsLongChaseBlunder(pos, bm)) {
            // 命中也给本级网络补一个样本（红方视角）
            float red = (pos.side_to_move() == zschess::RED) ? (float)bsc : -(float)bsc;
            zschess::NNLib::instance().TrainSample(lv, pos, red);
            return bm;
        }
    }

    // 2) 搜索
    zschess::SearchLimits lim;
    lim.depth = tc.depth;
    lim.movetime = tc.timeMs;
    zschess::ThreadPool::instance().set_threads(tc.threads);
    zschess::Move best = zschess::ThreadPool::instance().start_search(pos, lim);
    if (best == zschess::MOVE_NONE) return zschess::MOVE_NONE;
    // 长将/长捉防护：搜索最优若构成连将循环或单子长捉无根子，强制变招
    if (CZSAIPlayer::IsLongCheckBlunder(pos, best) || CZSAIPlayer::IsLongChaseBlunder(pos, best)) {
        zschess::Move alt = CZSAIPlayer::FindNonCycleMove(pos);
        if (alt != zschess::MOVE_NONE) best = alt;
    }

    // 3) 在线学习：把 (局面, 红方视角分值) 喂给对应等级网络，并写入精确着法库
    if (m_cfg.trainBook) {
        int sc = zschess::ThreadPool::instance().last_score(); // stm 视角
        if (pos.side_to_move() == zschess::BLACK) sc = -sc;    // 转红方视角
        zschess::NNLib::instance().TrainSample(lv, pos, (float)sc); // 只写当前走棋方等级，各级独立
        // 补喂“最优走法执行后的局面”（目标=当前搜索分，让网络对优势后续局面评估更准）
        zschess::Position aft = pos;
        if (aft.do_move(best)) {
            zschess::NNLib::instance().TrainSample(lv, aft, (float)sc);
        }
        // 精确着法库只写当前走棋方等级（红方视角分、请求深度）
        zschess::Book::instance().add(pos.hash(), lv, best, sc, lim.depth);
    }

	//这里不做低等级扰动了，避免同等级 AI 互相对弈时开局随机扰动导致棋谱不稳定
	// 上面引擎已经走过棋局了，返回错误的走法会导致棋谱和引擎不同步，所以这里不做低等级扰动了
    //// 4) 低等级扰动
    //if (tc.noiseProb > 0 && (rand() % 100) < tc.noiseProb) {
    //    zschess::Move alt = NoisyMove(pos, best, tc.noiseCenti);
    //    if (alt != zschess::MOVE_NONE) return alt;
    //}
    return best;
}
void AutoTrainer::OnRandomSeedStep(zschess::Position& pos,  std::vector<CChessPiece>& uiBoard,   std::vector<RecordStep>& steps) {
    //#define RND_BEGIN 0
#ifdef RND_BEGIN
    // 开局随机扰动 0-2 步（防同质：同强度 AI 不总是同一开局）
     
    //这里做判断，只有相同等级的 AI 才会做开局随机扰动，否则直接走正常决策
    if (m_cfg.blackTier == m_cfg.redTier)
        for (int i = 0; i < 1; i++) {
            if (!m_running.load()) break;
            zschess::Move ml[256];
            int n = pos.generate_legal_moves(ml);
            if (n == 0) break;
            zschess::Move m = ml[rand() % n];
            if (!pos.is_legal(m)) break;
            zschess::Color stm = pos.side_to_move();
            RecordStep rs;
            rs.number = (int)steps.size() + 1;
            rs.side = (stm == zschess::RED) ? RED : BLACK;
            rs.fromCol = zschess::square_x(zschess::move_from(m));
            rs.fromRow = zschess::square_y(zschess::move_from(m));
            rs.toCol = zschess::square_x(zschess::move_to(m));
            rs.toRow = zschess::square_y(zschess::move_to(m));
            zschess::Piece cap = pos.piece_on(zschess::move_to(m));
            rs.wasCapture = cap != zschess::EMPTY;
            if (rs.wasCapture) PieceLabel(cap, rs.capturedLabel);
            std::wstring nota = CGameRecord::MakeNotation(uiBoard, rs.side, rs.fromCol, rs.fromRow,
                rs.toCol, rs.toRow, rs.wasCapture ? rs.capturedLabel : L"");
            wcscpy_s(rs.notation, nota.c_str());
            pos.do_move(m);
            rs.inCheck = pos.in_check();
            RepetitionGuard::instance().PushPosition(pos); // 记录走后局面（含长捉分析），防长将/长捉循环
            int e = zschess::Eval::evaluate(pos);
            rs.evalScore = (pos.side_to_move() == zschess::RED) ? e : -e;

            // --show 模式：通知主窗口展示此步落子，
            // 等待界面处理完毕后才继续下一步（以界面显示为准）
            if (m_showMode && m_hwnd && m_stepDone) {
                if (!m_running.load()) return;
                ResetEvent(m_stepDone);
                PostMessageW(m_hwnd, WM_APP + 102,
                    MAKEWPARAM((short)rs.toCol, (short)rs.toRow),
                    MAKEWPARAM((short)rs.fromCol, (short)rs.fromRow));
                WaitForSingleObject(m_stepDone, INFINITE);

            }


            CGameRecord::ApplyStep(uiBoard, rs);
            steps.push_back(rs);
            {
                std::wstring rep;
                if (ZSSync::VerifyBoardSync(pos, uiBoard, rep) != 0) {
                    ZSSync::LogSyncError(L"训练随机步后", rep);
                    m_running = false; // 严重不同步：终止训练防扩散
                    return;
                }
            }
        }
 
#endif 
}
// 一局对弈
void AutoTrainer::PlayOneGame() {
    zschess::Position pos;
    pos.set_initial_position();
    RepetitionGuard::instance().Reset(); // 新局清空长将历史
    // --show 模式：每局开始先通知界面重置棋盘（等界面完成再开走，不然下一局走子校验失败）
    if (m_showMode && m_hwnd && m_stepDone) {
        ResetEvent(m_stepDone);
        PostMessageW(m_hwnd, WM_APP + 103, 0, 0);
        WaitForSingleObject(m_stepDone, 4000);
        Sleep(80);
    }
    std::vector<CChessPiece> uiBoard = MakeInitialUI();
    {
        std::wstring rep;
        int e1 = ZSSync::VerifyUiBoardSelf(uiBoard, rep);
        int e2 = ZSSync::VerifyBoardSync(pos, uiBoard, rep);
        if (e1 != 0 || e2 != 0)
            ZSSync::LogSyncError(L"训练开局", rep);
    }

    std::vector<RecordStep> steps;



    // 主循环
    int ply = 0;
    while (ply < 320) {
        if (!m_running.load()) break;

		//////这里加一个随机性，在相同AI等级，第7，8步之后随机走一手，避免同质化开局
  //      if (m_cfg.blackTier == m_cfg.redTier && ply >= 6 && ply <= 7) {
  //          //这里做判断，只有相同等级的 AI 才会做开局随机扰动，否则直接走正常决策
  //          if (m_cfg.blackTier == m_cfg.redTier)
  //              for (int i = 0; i < 1; i++) {
  //                  if (!m_running.load()) break;
  //                  zschess::Move ml[256];
  //                  int n = pos.generate_legal_moves(ml);
  //                  if (n == 0) break;
  //                  zschess::Move m = ml[rand() % n];
  //                  if (!pos.is_legal(m)) break;
  //                  zschess::Color stm = pos.side_to_move();
  //                  RecordStep rs;
  //                  rs.number = (int)steps.size() + 1;
  //                  rs.side = (stm == zschess::RED) ? RED : BLACK;
  //                  rs.fromCol = zschess::square_x(zschess::move_from(m));
  //                  rs.fromRow = zschess::square_y(zschess::move_from(m));
  //                  rs.toCol = zschess::square_x(zschess::move_to(m));
  //                  rs.toRow = zschess::square_y(zschess::move_to(m));
  //                  zschess::Piece cap = pos.piece_on(zschess::move_to(m));
  //                  rs.wasCapture = cap != zschess::EMPTY;
  //                  if (rs.wasCapture) PieceLabel(cap, rs.capturedLabel);
  //                  std::wstring nota = CGameRecord::MakeNotation(uiBoard, rs.side, rs.fromCol, rs.fromRow,
  //                      rs.toCol, rs.toRow, rs.wasCapture ? rs.capturedLabel : L"");
  //                  wcscpy_s(rs.notation, nota.c_str());
  //                  pos.do_move(m);
  //                  rs.inCheck = pos.in_check();
  //                  RepetitionGuard::instance().PushPosition(pos); // 记录走后局面（含长捉分析），防长将/长捉循环
  //                  int e = zschess::Eval::evaluate(pos);
  //                  rs.evalScore = (pos.side_to_move() == zschess::RED) ? e : -e;

  //                  // --show 模式：通知主窗口展示此步落子，
  //                  // 等待界面处理完毕后才继续下一步（以界面显示为准）
  //                  if (m_showMode && m_hwnd && m_stepDone) {
  //                      if (!m_running.load()) return;
  //                      ResetEvent(m_stepDone);
  //                      PostMessageW(m_hwnd, WM_APP + 102,
  //                          MAKEWPARAM((short)rs.toCol, (short)rs.toRow),
  //                          MAKEWPARAM((short)rs.fromCol, (short)rs.fromRow));
  //                      WaitForSingleObject(m_stepDone, INFINITE);

  //                  }


  //                  CGameRecord::ApplyStep(uiBoard, rs);
  //                  steps.push_back(rs);
  //                  {
  //                      std::wstring rep;
  //                      if (ZSSync::VerifyBoardSync(pos, uiBoard, rep) != 0) {
  //                          ZSSync::LogSyncError(L"训练随机步后", rep);
  //                          m_running = false; // 严重不同步：终止训练防扩散
  //                          return;
  //                      }
  //                  }
  //              }
  //      }

        zschess::Move ml[256];
        int n = pos.generate_legal_moves(ml);
        if (n == 0) break; // 终局

        zschess::Color stm = pos.side_to_move();
        AITier tier = (stm == zschess::RED) ? m_cfg.redTier : m_cfg.blackTier;
        uint64_t h = pos.hash();
        zschess::Move m = PickMove(pos, tier);
        if (m == zschess::MOVE_NONE) break;

        RecordStep rs;
        rs.number = (int)steps.size() + 1;
        rs.side = (stm == zschess::RED) ? RED : BLACK;
        rs.fromCol = zschess::square_x(zschess::move_from(m));
        rs.fromRow = zschess::square_y(zschess::move_from(m));
        rs.toCol = zschess::square_x(zschess::move_to(m));
        rs.toRow = zschess::square_y(zschess::move_to(m));
        zschess::Piece cap = pos.piece_on(zschess::move_to(m));
        rs.wasCapture = cap != zschess::EMPTY;
        if (rs.wasCapture) PieceLabel(cap, rs.capturedLabel);
        std::wstring nota = CGameRecord::MakeNotation(uiBoard, rs.side, rs.fromCol, rs.fromRow,
            rs.toCol, rs.toRow, rs.wasCapture ? rs.capturedLabel : L"");
        wcscpy_s(rs.notation, nota.c_str());

        pos.do_move(m);
        rs.inCheck = pos.in_check();
        // 后台训练：训练线程维护重复历史（含长捉分析）；--show 模式由界面记账块统一维护，避免双重记录
        if (!m_showMode) RepetitionGuard::instance().PushPosition(pos);
        // 走后对方是否无步可走（将杀或困毙）
        zschess::Move ml2[256];
        rs.mate = pos.generate_legal_moves(ml2) == 0;
        int e = zschess::Eval::evaluate(pos);
        rs.evalScore = (pos.side_to_move() == zschess::RED) ? e : -e;

        // --show 模式：通知主窗口展示此步落子，
        // 等待界面处理完毕后才继续下一步（以界面显示为准）
        if (m_showMode && m_hwnd && m_stepDone) {
			if (!m_running.load()) return; // 退出时避免卡死
            ResetEvent(m_stepDone);
            PostMessageW(m_hwnd, WM_APP + 102,
                MAKEWPARAM((short)rs.toCol, (short)rs.toRow),
                MAKEWPARAM((short)rs.fromCol, (short)rs.fromRow));
            WaitForSingleObject(m_stepDone, INFINITE);
			
        }

        CGameRecord::ApplyStep(uiBoard, rs);
        steps.push_back(rs);
        {
            std::wstring rep;
            if (ZSSync::VerifyBoardSync(pos, uiBoard, rep) != 0) {
                ZSSync::LogSyncError(L"训练每步后", rep);
                m_running = false; // 严重不同步：终止训练防扩散
                return;
            }
        }
        ply++;
        // 合法重复（一将一捉 / 双方都不愿变招，且非长将长捉）同一局面第3次形成 → 和棋
        if (RepetitionGuard::instance().IsThreefoldDraw(pos.hash())) break;
    }

    // ---- 终局判定 ----
    std::wstring result = L"和棋";
    PieceColor winner = RED;
    bool hasWinner = false;
    {
        zschess::Move ml[256];
        int n = pos.generate_legal_moves(ml);
        if (n == 0 && pos.side_to_move() == zschess::RED) { result = L"黑胜"; winner = BLACK; hasWinner = true; }
        else if (n == 0 && pos.side_to_move() == zschess::BLACK) { result = L"红胜"; winner = RED; hasWinner = true; }
        // 超过 320 步判和
    }

    // ---- 保存棋谱（文件名含日期+胜负）----
    if (m_cfg.saveRecords) {
        CGameRecord rec;
        for (auto& s : steps) rec.Append(s);
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t ev[48];
        swprintf_s(ev, L"%04d-%02d-%02d_%02d-%02d-%02d",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        std::wstring path = CGameRecord::DefaultRecordDir() + L"\\auto_" +
            CGameRecord::MakeTimestampFilename(result);
        rec.SaveToFile(path, ev, result);
    }

    // ---- 每局训练网络：红黑各自等级网络用本局样本训练并落盘 ----
    if (m_cfg.trainBook) {
        zschess::NNLib::instance().FlushTrain((int)m_cfg.redTier, 5);
        zschess::NNLib::instance().FlushTrain((int)m_cfg.blackTier, 5);
        zschess::NNLib::instance().SaveAll();
        // 精确着法库按本局【实际参与等级】落盘（新手vs新手只存 tier0；新手vs特级存 tier0+tier5）
        zschess::Book::instance().save_tier((int)m_cfg.redTier);
        zschess::Book::instance().save_tier((int)m_cfg.blackTier);
    }

    // ---- 统计 ----
    m_gamesPlayed++;
    if (result == L"红胜") m_redWins++;
    else if (result == L"黑胜") m_blackWins++;
    else m_draws++;
    {
        std::lock_guard<std::mutex> lk(m_lastMutex);
        wchar_t buf[128];
        int ns = zschess::NNLib::instance().TotalSamples();
        swprintf_s(buf, L"第%d局 %s（%d步） 网络样本%d", m_gamesPlayed.load(), result.c_str(), ply, ns);
        m_lastResult = buf;
    }
}

void AutoTrainer::Loop() {
    m_currentGame = 0;
    while (m_running.load()) {
        m_currentGame++;
        PlayOneGame();
        if (m_targetGames.load() > 0 && m_gamesPlayed.load() >= m_targetGames.load()) break;
    }
    m_running = false;
    // 最后训练+落盘一次
    if (m_cfg.trainBook) {
        zschess::NNLib::instance().FlushTrain((int)m_cfg.redTier, 5);
        zschess::NNLib::instance().FlushTrain((int)m_cfg.blackTier, 5);
        zschess::NNLib::instance().SaveAll();
    }
}
