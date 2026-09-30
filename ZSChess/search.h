#ifndef SEARCH_H
#define SEARCH_H

#include "types.h"
#include "position.h"
#include "tt.h"
#include "evaluate.h"
#include <atomic>
#include <chrono>

namespace zschess {
using namespace zschess::Eval;

// Search parameters
struct SearchLimits {
    int depth;
    int movetime;
    int wtime;
    int btime;
    int winc;
    int binc;
    int movestogo;
    bool infinite;

    SearchLimits() {
        depth = MAX_DEPTH;
        movetime = 0;
        wtime = btime = winc = binc = 0;
        movestogo = 0;
        infinite = false;
    }
};

// 全局静态交换评估 SEE：模拟" 吃 → 最小价值攻击者回吃 → 再吃 …"
// 的交换链，返回吃子方视角的净得失（百分子）。
// 用于 qsearch 剪枝与 NN 1-ply 吃子过滤（阻止"吃大子被回吃"的送吃）。
int see_value(const Position& pos, Move m);

// 全局静态交换评估 SEE：模拟" 吃 → 最小价值攻击者回吃 → 再吃 …"
// 的交换链，返回吃子方视角的净得失（百分子）。
// 用于 qsearch 剪枝与 NN 1-ply 吃子过滤（阻止"吃大子被回吃"的送吃）。
int see_value(const Position& pos, Move m);

// Searcher: iterative deepening + Alpha-Beta/PVS + TT + killer/history +
// quiescence search + null-move pruning (modeled after Fairy-Stockfish)
class Searcher {
public:
    Searcher();
    ~Searcher();

    // Run a search (modifies the passed position but restores it), returns best move
    Move think(Position& pos, const SearchLimits& limits);

    // Request the search to stop
    void stop();

    // Set an explicit time budget in ms
    void set_time(int timeMs);

    // Best move found so far
    Move best_move() const { return bestMove_; }
    // Best value found so far（stm 视角 centi-pawn；供训练入库）
    Value best_value() const { return bestValue_; }
    // 最近一次搜索实际完成的迭代深度（供根着法缓存记录）
    int last_depth() const { return lastDepth_; }
    // 根主变化线（PV）：每层迭代成功后立即固化到 bestPv_，
    // 避免"最后一层未完成（时间到）清空 pvTable_"导致 PV 丢失。
    int pv_len() const { return bestPvLen_; }
    Move pv_move(int i) const { return bestPv_[i]; }

    // Lazy SMP 辅助线程模式：不自己做时间管理（timeLimit 视为无限），
    // 仅由 ThreadPool 统一 stop 停止；迭代深度上限 +1，借共享 TT 并行提速。
    void set_helper(bool b) { helper_ = b; }
    bool helper() const { return helper_; }

    // 变招精算用：返回 pos 当前 stm 视角的静态搜索（quiescence）分值。
    // 能识别"走完立即被吃"的 hanging 子，供强制变招时选损失最小的有效着法，
    // 而不是从合法着法里随手挑一步送子。
    Value qsearch_value(Position& pos);

private:
    // Alpha-Beta search with PVS (zero-window verification for non-first moves)
    // pvNode：该节点是否为主变化线节点（用于维护 pvTable_ 主变化线，
    // 供"PV 线预缓存"——红方搜索时算出整条双方最佳应对链，黑/红方沿线取棋）
    Value alpha_beta(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth, bool cutNode, bool pvNode);

    // Quiescence search: only capture moves, avoids the horizon effect
    Value qsearch(Position& pos, Stack* ss, Value alpha, Value beta);

    // Move ordering: TT move > captures (MVV-LVA) > killers > history
    void score_moves(MoveList& moves, Move ttMove, Stack* ss);
    Move pick_best(MoveList& moves, size_t idx);

    // Static Exchange Evaluation (SEE): simulates the capture-exchange sequence
    // on the destination square and returns the net gain (in centi-pawns) from
    // the mover's perspective. Modeled after Fairy-Stockfish's SEE, which is
    // used to prune obviously losing captures in the quiescence search.
    int see(const Position& pos, Move m);

    // Time management
    void check_time();
    bool time_up();

    // Root iterative-deepening loop
    void iterative_deepening(Position& pos, const SearchLimits& limits);

    // Search state
    std::atomic<bool> searching_;
    std::atomic<bool> stop_;
    bool helper_;   // 辅助线程模式（Lazy SMP）
    std::chrono::steady_clock::time_point startTime_;
    int timeLimit_;

    Move bestMove_;
    Value bestValue_;
    int nodes_;
    int lastDepth_ = 0; // 最近一次成功完成的迭代深度
    // 主变化线表（Fairy-Stockfish 标准结构）：
    // pvTable_[ply][0..pvLen_[ply]-1] = 从第 ply 层起的双方最佳应对链
    Move pvTable_[MAX_PLY][MAX_PLY];
    int pvLen_[MAX_PLY];
    // 最近一次成功完成迭代的根 PV 固化副本（供 ThreadPool PV 线预缓存）
    Move bestPv_[MAX_PLY];
    int bestPvLen_ = 0;

    // History heuristic table: [side to move][piece type][target square]
    int historyTable_[2][PIECE_TYPE_NB][BOARD_SIZE];
    // Killer moves: [ply][0/1]
    Move killerMoves_[MAX_PLY][2];
};

} // namespace zschess

#endif // SEARCH_H
