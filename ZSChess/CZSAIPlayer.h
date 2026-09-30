// CZSAIPlayer.h
// Next-gen AI player: keeps the same UI-facing interface as the old CAIPlayer,
// but internally uses the fixed zschess engine (Position / Searcher / TT),
// with iterative deepening, Alpha-Beta/PVS, transposition table, killer/history
// heuristics, quiescence search and null-move pruning.
#pragma once
#define NOMINMAX
#include "CChessPiece.h"
#include "AIConfigBridge.h"
#include "AITier.h"
#include "position.h"
#include "search.h"
#include "evaluate.h"
#include "tt.h"
#include "zobrist.h"
#include "thread.h"
#include "NNLib.h"
#include "RepetitionGuard.h"
#include <vector>
#include <functional>
#include <future>
#include <thread>
#include <atomic>
#include <memory>
#include <cstring>
#include <cstdlib>
#include <algorithm>

class CZSAIPlayer {
public:
    CZSAIPlayer() {
        m_cancelToken = std::make_shared<std::atomic<bool>>(false);
    }
    ~CZSAIPlayer() { Cancel(); }

    // ==================== Interface compatible with CAIPlayer ====================

    // Synchronous search: returns best move as (from, to); (-1,-1) on no move / cancelled
    std::pair<PSF::POS, PSF::POS> FindBestMoveCoords(PieceColor color, const std::vector<CChessPiece>& board, int depth) {
        if (m_cancelToken && m_cancelToken->load()) return { PSF::POS(-1, -1), PSF::POS(-1, -1) };

        zschess::Position pos = BoardToPosition(board, color);
        TierConfig tc = EffectiveTier();
        int tier = GetConfiguredAITier();
        if (tier < 0) tier = 0; if (tier > 5) tier = 5;

        // 神经网络优先：先查最高等级（特级）网络库，命中直接出棋（毫秒级，等级再低也能走出最高水平）
        {
            float nsc = 0;
            zschess::Move nbm = zschess::NNLib::instance().PickMove(pos, tier, true, nsc);
            if (nbm != zschess::MOVE_NONE && pos.is_legal(nbm) && !IsLongCheckBlunder(pos, nbm) && !IsLongChaseBlunder(pos, nbm)) {
                // 局面和分值入库（红方视角）：只写当前选择的等级，各级独立
                float red = (pos.side_to_move() == zschess::RED) ? nsc : -nsc;
                zschess::NNLib::instance().TrainSample(tier, pos, red);
                return { ToPos(zschess::move_from(nbm)), ToPos(zschess::move_to(nbm)) };
            }
            // 长将命中：不能走连将循环，近落到引擎搜索
        }

        zschess::SearchLimits limits = MakeLimitsFromTier(tc);

        EnsureThreads(tc);

        zschess::Move best = zschess::ThreadPool::instance().start_search(pos, limits);
        // 长将/长捉防护：搜索最优若构成连将循环或单子长捉无根子，强制变招
        if (best != zschess::MOVE_NONE && (IsLongCheckBlunder(pos, best) || IsLongChaseBlunder(pos, best))) {
            zschess::Move alt = FindNonCycleMove(pos);
            if (alt != zschess::MOVE_NONE) best = alt;
        }

        // 搜索后在线学习：把 (u5c40面, 红方视角分值) 喂给当前等级网络
        if (best != zschess::MOVE_NONE) {
            int sc = zschess::ThreadPool::instance().last_score();
            if (color == BLACK) sc = -sc;
            zschess::NNLib::instance().TrainSample(tier, pos, (float)sc); // 只写当前等级
            zschess::Position aft = pos;
            if (aft.do_move(best)) {
                zschess::NNLib::instance().TrainSample(tier, aft, (float)sc);
            }
        }

        // 低等级扰动：模拟人类失误（按概率随机合法走法）
        if (best != zschess::MOVE_NONE && tc.noiseProb > 0 && (rand() % 100) < tc.noiseProb) {
            zschess::Move ml[256];
            int cnt = pos.generate_legal_moves(ml);
            if (cnt > 0) best = ml[rand() % cnt];
        }

        if (best == zschess::MOVE_NONE) return { PSF::POS(-1, -1), PSF::POS(-1, -1) };
        return { ToPos(zschess::move_from(best)), ToPos(zschess::move_to(best)) };
    }

    // Async search: runs on a background thread, callback fires on a worker thread.
    // Returns a shared_future that can also be waited on synchronously.
    std::shared_future<std::pair<PSF::POS, PSF::POS>> FindBestMoveCoordsAsync(
        
        PieceColor color, const std::vector<CChessPiece>& board, int depth,
        std::function<void(std::pair<PSF::POS, PSF::POS>)> callback = nullptr) {
        srand(static_cast<unsigned int>(time(NULL)));
        // Fresh cancel token per task so an old cancellation cannot kill a new search
        m_cancelToken = std::make_shared<std::atomic<bool>>(false);
        auto token = m_cancelToken;

        zschess::Position pos = BoardToPosition(board, color);
        TierConfig tc = EffectiveTier();
        int tier = GetConfiguredAITier();
        if (tier < 0) tier = 0; if (tier > 5) tier = 5;
        zschess::SearchLimits limits = MakeLimitsFromTier(tc);

        EnsureThreads(tc);

        auto fut = std::async(std::launch::async, [token, pos, limits, tc, tier]() mutable -> std::pair<PSF::POS, PSF::POS> {
            if (token && token->load()) return { PSF::POS(-1, -1), PSF::POS(-1, -1) };
            // 神经网络优先：先查最高等级（特级）网络库
            {
                float nsc = 0;
                zschess::Move nbm = zschess::NNLib::instance().PickMove(pos, tier, true, nsc);
                if (nbm != zschess::MOVE_NONE && pos.is_legal(nbm) && !IsLongCheckBlunder(pos, nbm) && !IsLongChaseBlunder(pos, nbm)) {
                    float red = (pos.side_to_move() == zschess::RED) ? nsc : -nsc;
                    zschess::NNLib::instance().TrainSample(tier, pos, red);
                    return { ToPos(zschess::move_from(nbm)), ToPos(zschess::move_to(nbm)) };
                }
                // 长将命中：落到引擎
            }
            zschess::Move best = zschess::ThreadPool::instance().start_search(pos, limits);
            // 长将防护：搜索最优若构成连将循环，强制变招
            if (best != zschess::MOVE_NONE && (IsLongCheckBlunder(pos, best) || IsLongChaseBlunder(pos,best))) {
                zschess::Move alt = FindNonCycleMove(pos);
                if (alt != zschess::MOVE_NONE) best = alt;
            }
            if (best == zschess::MOVE_NONE) return { PSF::POS(-1, -1), PSF::POS(-1, -1) };
            // 搜索后在线学习
            {
                int sc = zschess::ThreadPool::instance().last_score();
                if (pos.side_to_move() == zschess::BLACK) sc = -sc;
                zschess::NNLib::instance().TrainSample(tier, pos, (float)sc); // 只写当前等级
                zschess::Position aft = pos;
                if (aft.do_move(best)) {
                    zschess::NNLib::instance().TrainSample(tier, aft, (float)sc);
                }
            }
            // 低等级扰动
            if (best != zschess::MOVE_NONE && tc.noiseProb > 0 && (rand() % 100) < tc.noiseProb) {
                zschess::Move ml[256];
                int cnt = pos.generate_legal_moves(ml);
                if (cnt > 0) best = ml[rand() % cnt];
            }
            return { ToPos(zschess::move_from(best)), ToPos(zschess::move_to(best)) };
        });

        auto shared = fut.share();
        if (callback) {
            // 回调线程同样捕获取消标记：搜索被 Cancel 后返回的结果必须丢弃，
            // 否则取消后（如复盘回退）仍会落子到已变化的局面，导致崩溃。
            auto t = token;
            std::thread([shared, callback, t]() mutable {
                if (t && t->load()) return; // 已取消：不执行回调（不落子）
                auto res = shared.get();
                
                callback(res);
            }).detach();
        }
        return shared;
    }

    // Cancel current search (thread-safe): makes the running search stop soon
    void Cancel() {
        if (m_cancelToken) m_cancelToken->store(true);
        zschess::ThreadPool::instance().stop_all();
    }

    // Clear the cancel flag (call before a new search)
    void ClearCancel() {
        m_cancelToken.reset();
    }

    // Compatible legacy API: returns the piece pointer of the best move plus target
    std::pair<CChessPiece*, PSF::POS> GetBestMove(PieceColor color, std::vector<CChessPiece>& board, int depth) {
        auto res = FindBestMoveCoords(color, board, depth);
        if (res.first.col < 0 || res.first.row < 0) return { nullptr, PSF::POS(0, 0) };
        for (auto& p : board) {
            if (p.GetX() == res.first.col && p.GetY() == res.first.row) {
                return { &p, res.second };
            }
        }
        return { nullptr, PSF::POS(0, 0) };
    }

    // 评估给定局面，返回红方视角分数（正=红优，负=黑优）——棋谱运行时每步得分显示用
    // 只做静态评估，快速同步；不参与搜索、不写入棋谱文件
    static int EvaluateBoardScore(const std::vector<CChessPiece>& board, PieceColor side) {
        zschess::Position pos = BoardToPosition(board, side);
        int e = zschess::Eval::evaluate(pos);
        return (side == RED) ? e : -e;
    }

    // 长将防护：检查 pos 走 m 是否构成"连将循环"（走子后直接将死对方则允许，杀棋优先）。
    // 满足则禁止该着法（强制变招）：无论人类还是 AI 都不允许长将。
    static bool IsLongCheckBlunder(const zschess::Position& pos, zschess::Move m) {
        zschess::Position p2 = pos;
        if (!p2.do_move(m)) return false;
        // 例外：将死对方（杀棋）→ 允许
        zschess::Move ml[256];
        if (p2.generate_legal_moves(ml) == 0) return false;
        // 长将循环：走子后对方被将（m 是将军）且局面重复且中间连将
        return RepetitionGuard::instance().IsLongCheckCycle(p2.hash(), p2.in_check());
    }
    static void ClearPanDing() {
        RepetitionGuard::instance().clear();
    }
    // 长捉防护：检查 pos 走 m 是否构成"单子长捉对方一个无根、不能互吃的子"。
    // 杀棋优先（将死允许）；多子联合捉、被捉子有根、能互吃（兑子）均不算。
    static bool IsLongChaseBlunder(const zschess::Position& pos, zschess::Move m) {
        zschess::Position p2 = pos;
        if (!p2.do_move(m)) return false;
        zschess::Move ml[256];
        if (p2.generate_legal_moves(ml) == 0) return false;   // 杀棋允许
        RepetitionGuard::ChaseInfo ci = RepetitionGuard::AnalyzeChase(p2);
        if (!ci.chase) return false;
        return RepetitionGuard::instance().IsLongChaseCycle(
            p2.hash(), true, ci.attType, ci.tgtType);
    }

    // 变招：在"不构成长将/长捉循环"的合法着法里，选出对己方损失最小的有效着法。
    // 流程：杀棋直接返回；候选按静态分粗排取前 K，再用 quiescence（能看出下一步
    // 被白吃）逐一精算，选真实分最高——绝不从合法着法里随机挑（随机极易送子）。
    // 返回 MOVE_NONE 表示无可用着法（极罕见，由调用方保留原着法）。
    static zschess::Move FindNonCycleMove(const zschess::Position& pos) {
        zschess::Move ml[256];
        int n = pos.generate_legal_moves(ml);
        if (n <= 0) return zschess::MOVE_NONE;

        struct Cand { zschess::Move m; int sv; };
        std::vector<Cand> cands;
        zschess::Move staticBest = zschess::MOVE_NONE;
        int staticBestV = -1000000000;

        for (int i = 0; i < n; i++) {
            zschess::Position p2 = pos;
            if (!p2.do_move(ml[i])) continue;
            zschess::Move ml2[256];
            if (p2.generate_legal_moves(ml2) == 0) return ml[i];   // 杀棋，最高优先
            if (RepetitionGuard::instance().IsLongCheckCycle(p2.hash(), p2.in_check())) continue;
            RepetitionGuard::ChaseInfo ci = RepetitionGuard::AnalyzeChase(p2);
            if (ci.chase && RepetitionGuard::instance().IsLongChaseCycle(
                    p2.hash(), true, ci.attType, ci.tgtType)) continue;
            int v = zschess::Eval::evaluate(p2);                   // p2.stm（对方）视角
            if (p2.side_to_move() == zschess::BLACK) v = -v;       // 转红方视角
            cands.push_back(Cand{ ml[i], v });
            if (v > staticBestV) { staticBestV = v; staticBest = ml[i]; }
        }
        if (cands.empty())
            return (staticBest != zschess::MOVE_NONE) ? staticBest : ml[0];

        std::sort(cands.begin(), cands.end(),
            [](const Cand& a, const Cand& b) { return a.sv > b.sv; });
        const int K = 8;
        int lim = (int)cands.size() < K ? (int)cands.size() : K;

        zschess::Move best = staticBest;
        int bestReal = -1000000000;
        for (int i = 0; i < lim; i++) {
            zschess::Position p2 = pos;
            if (!p2.do_move(cands[i].m)) continue;
            zschess::Searcher s;
            int qv = s.qsearch_value(p2);                          // 对方视角真实分
            int red = (p2.side_to_move() == zschess::RED) ? qv : -qv;
            if (red > bestReal) { bestReal = red; best = cands[i].m; }
        }
        return best;
    }

private:
    std::shared_ptr<std::atomic<bool>> m_cancelToken;

    // 等级映射：由 AI 设置对话框的等级 + 配置上限决定搜索资源
    static TierConfig EffectiveTier() {
        int lv = 5, d = 0, ts = 0, th = 0;
        try {
            lv = GetConfiguredAITier();
            d = GetConfiguredAIDepth();
            ts = GetConfiguredAITimeSec();
            th = GetConfiguredAIMaxThreads();
        } catch (...) {}
        if (lv < 0) lv = 0;
        if (lv > 5) lv = 5;
        int timeMs = ts > 0 ? ts * 1000 : 0;
        return TierToConfig((AITier)lv, d, timeMs, th);
    }

    // 按等级生成搜索限定：深度/时限都来自等级配置
    static zschess::SearchLimits MakeLimitsFromTier(const TierConfig& tc) {
        zschess::SearchLimits limits;
        limits.depth = (tc.depth > 0) ? tc.depth : 4;
        limits.movetime = (tc.timeMs > 0) ? tc.timeMs : 1500;

        if (limits.depth > 4) {
            int i = rand() % 4;
            int s = rand() % 100 < 80 ? 1 : -1;
            i = i * s;
            if (rand() % 100 < 60) {
                limits.depth += i;
            }
        }
        int rndTime = 0;
        if (rand() % 80 < 40) {
            rndTime += rand() % 6000;
        }
        limits.movetime += rndTime;

        return limits;
    }

    // Lazy SMP 线程数由等级决定，上限 min(核数,8)
    static void EnsureThreads(const TierConfig& tc) {
        static int s_threads = -1;
        unsigned hw = std::thread::hardware_concurrency();
        if (hw < 1) hw = 4;
        unsigned cap = hw < 8 ? hw : 8;
        int n = (tc.threads > 0) ? tc.threads : 4;
        if (n > (int)cap) n = (int)cap;
        if (n == s_threads) return;
        s_threads = n;
        zschess::ThreadPool::instance().set_threads(n);
    }

    // ==================== Engine bridge helpers (public: 界面/训练器共用) ====================

public:
    // UI piece type -> engine piece type
    static zschess::PieceType ToEngineType(E_PieceType t) {
        switch (t) {
        case PT_JIANG: return zschess::JIANG;
        case PT_DU:    return zschess::DU;
        case PT_JU:    return zschess::CHE;
        case PT_SHI:   return zschess::SHI;
        case PT_HOU:   return zschess::HOU;
        case PT_JUN:   return zschess::JUN;
        case PT_PAO:   return zschess::PAO;
        case PT_MA:    return zschess::MA;
        case PT_XIANG: return zschess::XIANG;
        case PT_BING:  return zschess::BING;
        default:       return zschess::NO_PIECE_TYPE;
        }
    }

    // UI board -> engine position
    static zschess::Position BoardToPosition(const std::vector<CChessPiece>& board, PieceColor color) {
        zschess::Piece arr[zschess::BOARD_SIZE];
        std::memset(arr, 0, sizeof(arr));
        for (const auto& p : board) {
            int x = p.GetX(), y = p.GetY();
            if (x < 0 || x >= 11 || y < 0 || y >= 11) continue;
            zschess::PieceType pt = ToEngineType(p.GetType());
            if (pt == zschess::NO_PIECE_TYPE) continue;
            arr[zschess::make_square(x, y)] = zschess::make_piece((zschess::Color)p.GetColor(), pt);
        }
        zschess::Position pos;
        pos.set_board_state(arr, (zschess::Color)color);
        return pos;
    }

    // Engine square -> UI coordinate
    static PSF::POS ToPos(zschess::Square s) {
        return PSF::POS(zschess::square_x(s), zschess::square_y(s));
    }

    // Search limits: time (sec, from AIConfigDlg) wins when configured;
    // otherwise fall back to depth + a 1.5s safety cap to keep the UI responsive.
    static zschess::SearchLimits MakeLimits(int depth) {
        (void)depth;
        return MakeLimitsFromTier(EffectiveTier());
    }
};
