#include "search.h"
#include "evaluate.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <iomanip>

using namespace zschess;
using namespace zschess::Eval;

Searcher::Searcher() {
    searching_ = false;
    stop_ = false;
    helper_ = false;
    bestMove_ = MOVE_NONE;
    bestValue_ = 0;
    nodes_ = 0;
    timeLimit_ = 1000;
    std::memset(historyTable_, 0, sizeof(historyTable_));
    std::memset(killerMoves_, 0, sizeof(killerMoves_));
}

Searcher::~Searcher() {}

void Searcher::stop() {
    stop_ = true;
}

void Searcher::set_time(int timeMs) {
    timeLimit_ = timeMs;
}

void Searcher::check_time() {
    if (nodes_ % 4096 == 0) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime_).count();
        if (elapsed >= timeLimit_) {
            stop_ = true;
        }
    }
}

bool Searcher::time_up() {
    return stop_;
}

// ==================== 走法排序评分 ====================
// 排序依据（参照 Fairy-Stockfish 的 MovePicker 优先级）：
// 1. 置换表走法（最高）
// 2. 吃子走法：MVV-LVA（被吃子价值大、吃子棋子价值小优先）
// 3. 杀手走法（同级无吃子的剪枝走法）
// 4. 历史启发分数
void Searcher::score_moves(MoveList& moves, Move ttMove, Stack* ss) {
    int ply = ss->ply;
    moves.scores.resize(moves.size());

    for (size_t i = 0; i < moves.size(); i++) {
        Move m = moves[i];
        int score = 0;

        if (m == ttMove) {
            score = 1000000;
        }
        else if (move_captured(m) != EMPTY) {
            // MVV-LVA：被吃子价值 * 16 - 吃子棋子价值
            PieceType victim = piece_type(move_captured(m));
            PieceType attacker = piece_type(move_piece(m));
            score = 500000 + piece_value[victim] * 16 - piece_value[attacker];
        }
        else if (m == killerMoves_[ply][0] || m == killerMoves_[ply][1]) {
            score = 250000;
        }
        else {
            // 历史启发：走子方 + 移动棋子类型 + 目标格
            PieceType pt = piece_type(move_piece(m));
            Square to = move_to(m);
            // score_moves 阶段无法确定走子方，历史分数在 do_move 前补正：
            // 这里只取一个近似值（两种颜色取较大者），仅用于排序，不影响正确性
            int h = std::max(historyTable_[RED][pt][to], historyTable_[BLACK][pt][to]);
            score = h;
        }

        moves.scores[i] = score;
    }
}

Move Searcher::pick_best(MoveList& moves, size_t idx) {
    size_t bestIdx = idx;
    int bestScore = -1;
    for (size_t i = idx; i < moves.size(); i++) {
        if (moves.scores[i] > bestScore) {
            bestScore = moves.scores[i];
            bestIdx = i;
        }
    }
    std::swap(moves.moves[idx], moves.moves[bestIdx]);
    std::swap(moves.scores[idx], moves.scores[bestIdx]);
    return moves[idx];
}

// ==================== 静态交换评估 SEE ====================
// 模拟"吃 → 最小价值攻击者回吃 → 再吃 …"的交换链，返回吃子方视角的净得失。
// Fairy-Stockfish 的 SEE 是 qsearch 剪枝的核心：SEE 明显为负的吃子（吃到被
// 保护格、会被更便宜的子回吃）直接跳过，避免在静态搜索里模拟已知的送子。
int zschess::see_value(const Position& pos, Move m) {
    if (move_captured(m) == EMPTY) return 0;
    Square to = move_to(m);

    // 在拷贝局面上执行第一次吃子，避免污染主局面
    Position copy(pos);
    if (!copy.do_move(m)) return 0;

    int gain[16];
    int d = 0;
    gain[0] = piece_value[piece_type(move_captured(m))];

    Color side = copy.side_to_move(); // 下一轮回吃方
    Move tmp[MAX_MOVES];

    // 交换链：每轮找出 side 方能吃到 to 的最小价值攻击者（LVA，王除外）
    while (d < 15) {
        int cnt = copy.generate_pseudolegal_moves(tmp);
        Move bestAtk = MOVE_NONE;
        int bestVal = 1000000;
        for (int i = 0; i < cnt; i++) {
            if (move_to(tmp[i]) == to) {
                PieceType atkPt = piece_type(move_piece(tmp[i]));
                if (atkPt == JIANG) continue; // 将/帅不进入静态交换链
                int v = piece_value[atkPt];
                if (v < bestVal) { bestVal = v; bestAtk = tmp[i]; }
            }
        }
        if (bestAtk == MOVE_NONE) break;
        // 攻击者必须吃到子（to 上有子），否则链结束
        if (move_captured(bestAtk) == EMPTY) break;

        gain[d + 1] = piece_value[piece_type(move_captured(bestAtk))];
        copy.do_move(bestAtk);
        side = copy.side_to_move();
        d++;
    }

    // 负向回溯：每一方都可以选择"不再吃"止损，
    // gain[i] = max(吃掉当前子的所得, -(对手后续交换的净得))
    while (d > 0) {
        gain[d - 1] = std::max(gain[d - 1], -gain[d]);
        d--;
    }
    return gain[0];
}

int Searcher::see(const Position& pos, Move m) {
    return see_value(pos, m);
}

// ==================== 静态搜索 ====================
// 只搜索吃子走法（被将军时搜索全部解将走法），
// 防止搜索在叶子节点因“刚吃完子又立刻被吃回”产生水平效应。
// 参照 Fairy-Stockfish：
//   - 被将军时保留全部解将走法（不做 SEE 过滤，强制序列必须完整）
//   - 未受将时，用 SEE 跳过明显亏的进攻性吃子（吃到被保护格）
Value Searcher::qsearch(Position& pos, Stack* ss, Value alpha, Value beta) {
    nodes_++;
    check_time();
    if (time_up()) return VALUE_DRAW;
    if (ss->ply >= MAX_PLY) return Eval::evaluate(pos);

    bool inCheck = pos.in_check();

    // 静态评估（stand pat）：被将军时不能 stand-pat，必须搜索解将走法
    if (!inCheck) {
        Value eval = Eval::evaluate(pos);
        if (eval >= beta) return beta;
        if (eval > alpha) alpha = eval;
    }

    // 生成所有合法走法：被将军时保留全部（含非吃子解将），否则只保留吃子
    MoveList caps;
    {
        Move temp[MAX_MOVES];
        int count = pos.generate_legal_moves(temp);
        for (int i = 0; i < count; i++) {
            if (inCheck || move_captured(temp[i]) != EMPTY) {
                caps.push_back(temp[i]);
            }
        }
    }

    // 被将军且无解将走法 = 被将死（必须把杀棋分数传回上层，否则漏杀）
    if (caps.empty()) {
        if (inCheck) return -VALUE_MATE + ss->ply;
        return alpha; // 无可吃走法：stand-pat 结果（!inCheck 时 alpha 已提升到 eval）
    }

    // SEE 过滤（仅未受将时）：跳过净亏超过 80 分的进攻性吃子，
    // 这些走法在交换链模拟下明显送子（如车吃被马保护的兵）
    if (!inCheck) {
        size_t writeIdx = 0;
        for (size_t i = 0; i < caps.size(); i++) {
            Move m = caps[i];
            if (move_captured(m) != EMPTY && see(pos, m) < -80) {
                continue;
            }
            caps.moves[writeIdx++] = m;
        }
        caps.moves.resize(writeIdx);
        caps.scores.resize(writeIdx);
        if (caps.empty()) return alpha;
    }

    score_moves(caps, MOVE_NONE, ss);

    for (size_t i = 0; i < caps.size(); i++) {
        Move m = pick_best(caps, i);

        pos.do_move(m);
        (ss + 1)->ply = ss->ply + 1;
        Value score = -qsearch(pos, ss + 1, -beta, -alpha);
        pos.undo_move();

        if (time_up()) return VALUE_DRAW;

        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }

    return alpha;
}

// ==================== Alpha-Beta主搜索 ====================
// 结构：置换表裁剪 → 空着裁剪 → 走法生成 → PVS 主变例搜索 →
//      杀手/历史启发更新 → 置换表回写
Value Searcher::alpha_beta(Position& pos, Stack* ss, Value alpha, Value beta, Depth depth, bool cutNode, bool pvNode) {
    nodes_++;
    check_time();

    if (time_up()) return VALUE_DRAW;

    // 叶子节点：进入静态搜索
    if (depth <= 0) {
        return qsearch(pos, ss, alpha, beta);
    }
    if (ss->ply >= MAX_PLY) return Eval::evaluate(pos);

    // PV 节点维护：本层主变化线先清空（beta 截断时也保持为空）
    if (pvNode) pvLen_[ss->ply] = 0;

    // 被将则加深一层（应对将军局面下的强制走法）
    bool inCheck = pos.in_check();
    if (inCheck) depth++;

    // 置换表查询
    Key key = pos.hash();
    bool ttFound;
    TTEntry* tte = TT.probe(key, ttFound);
    Move ttMove = MOVE_NONE;
    if (ttFound) {
        ttMove = tte->move;
        // PV 节点不做 bound 提前返回（否则 pvLen_ 残留 0，PV 线断裂）
        if (!pvNode && tte->depth >= depth) {
            if (tte->bound == BOUND_EXACT) return tte->score;
            if (tte->bound == BOUND_LOWER && tte->score >= beta) return tte->score;
            if (tte->bound == BOUND_UPPER && tte->score <= alpha) return tte->score;
        }
    }

    ss->staticEval = Eval::evaluate(pos);

    // 空着裁剪 (Null Move Pruning)：
    // 如果静态评估已超过 beta（当前方优势很大），先让对手走一手（空着），
    // 若仍超过 beta，说明本层走法可以安全剪掉
    if (!pvNode && depth >= 3 && !inCheck && ss->staticEval >= beta) {
        pos.do_null_move();
        (ss + 1)->ply = ss->ply + 1;
        Value nullScore = -alpha_beta(pos, ss + 1, -beta, -beta + 1, depth - 3 - 1, !cutNode, false);
        pos.undo_null_move();

        if (time_up()) return VALUE_DRAW;
        if (nullScore >= beta) {
            // 防止空着把“杀棋分数”带入置换表造成误判
            if (nullScore >= VALUE_MATE - MAX_PLY) nullScore = beta;
            return nullScore;
        }
    }

    // 生成走法
    MoveList moves;
    {
        Move temp[MAX_MOVES];
        int count = pos.generate_legal_moves(temp);
        for (int i = 0; i < count; i++) {
            moves.push_back(temp[i]);
        }
    }

    if (moves.empty()) {
        if (inCheck) return -VALUE_MATE + ss->ply; // 被将死
        return VALUE_DRAW; // 困毙
    }

    score_moves(moves, ttMove, ss);

    Value bestValue = -VALUE_INFINITE;
    Move bestMove = MOVE_NONE;
    size_t moveCount = 0;
    Color us = pos.side_to_move();

    // PVS (Principal Variation Search)
    for (size_t i = 0; i < moves.size(); i++) {
        Move m = pick_best(moves, i);

        pos.do_move(m);
        (ss + 1)->ply = ss->ply + 1;
        (ss + 1)->currentMove = m;

        Value score;
        if (moveCount == 0) {
            // 第一个走法全窗口搜索（继承 PV 节点属性）
            score = -alpha_beta(pos, ss + 1, -beta, -alpha, depth - 1, false, pvNode);
        }
        else {
            // 后续走法先零窗口搜索验证（非 PV 节点）
            score = -alpha_beta(pos, ss + 1, -alpha - 1, -alpha, depth - 1, true, false);
            if (score > alpha && score < beta) {
                // 提升：全窗口重新搜索（成为新的 PV 候选）
                score = -alpha_beta(pos, ss + 1, -beta, -alpha, depth - 1, false, pvNode);
            }
        }

        pos.undo_move();
        moveCount++;

        if (time_up()) return VALUE_DRAW;

        if (score > bestValue) {
            bestValue = score;
            bestMove = m;

            if (score > alpha) {
                alpha = score;
                // 成为新的主变化线：本层着法 + 子节点主变化线
                if (pvNode) {
                    pvTable_[ss->ply][0] = m;
                    int childLen = pvLen_[ss->ply + 1];
                    for (int k = 0; k < childLen && k + 1 < MAX_PLY; k++)
                        pvTable_[ss->ply][k + 1] = pvTable_[ss->ply + 1][k];
                    pvLen_[ss->ply] = childLen + 1;
                }
            }
        }

        // Beta剪枝：更新杀手走法与历史启发
        // 注意：不在 fail-high 时清空 pvLen_（Fairy-Stockfish 语义）——
        // pvLen_ 只在"score > alpha 更新"时写入，进入节点时已清空，
        // 若这里再清空，父节点拼接子节点 PV 时读到 0，主变化线断裂。
        if (alpha >= beta) {
            if (move_captured(m) == EMPTY) {
                if (killerMoves_[ss->ply][0] != m) {
                    killerMoves_[ss->ply][1] = killerMoves_[ss->ply][0];
                    killerMoves_[ss->ply][0] = m;
                }
                historyTable_[us][piece_type(move_piece(m))][move_to(m)] += depth * depth;
            }
            break;
        }
    }

    // 存入置换表
    Bound b;
    if (bestValue >= beta) b = BOUND_LOWER;
    else if (bestMove != MOVE_NONE) b = BOUND_EXACT;
    else b = BOUND_UPPER;

    TT.store(key, bestMove, bestValue, ss->staticEval, depth, b);

    return bestValue;
}

// ==================== 迭代加深搜索 ====================
void Searcher::iterative_deepening(Position& pos, const SearchLimits& limits) {
    bestMove_ = MOVE_NONE;
    bestValue_ = 0;
    nodes_ = 0;
    stop_ = false;
    startTime_ = std::chrono::steady_clock::now();

    // 清空杀手走法与历史启发表
    std::memset(killerMoves_, 0, sizeof(killerMoves_));
    std::memset(historyTable_, 0, sizeof(historyTable_));

    Stack stack[MAX_PLY + 10];
    std::memset(stack, 0, sizeof(stack));
    for (int i = 0; i < MAX_PLY + 10; i++) stack[i].ply = i;

    TT.new_search();

    // 先取一个兜底走法以防搜索被立即打断
    MoveList legalMoves;
    {
        Move temp[MAX_MOVES];
        int count = pos.generate_legal_moves(temp);
        for (int i = 0; i < count; i++) {
            legalMoves.push_back(temp[i]);
        }
    }
    if (!legalMoves.empty()) {
        bestMove_ = legalMoves[0];
    }

    // 计算时间分配。Lazy SMP 辅助线程不做时间管理（由主线程统一停止），
    // 这里把预算设为极大，仅靠外部 stop_ 结束。
    int timeMs;
    if (helper_) {
        timeMs = 1000000000;
    }
    else if (limits.movetime > 0) {
        timeMs = limits.movetime;
    }
    else {
        int ourTime = (pos.side_to_move() == RED) ? limits.wtime : limits.btime;
        int ourInc = (pos.side_to_move() == RED) ? limits.winc : limits.binc;
        if (ourTime <= 0) ourTime = 5000;
        timeMs = ourTime / 30 + ourInc;
        if (ourTime > 100) timeMs = std::min(timeMs, ourTime - 100);
    }
    timeLimit_ = std::max(timeMs, 50);

    if (!helper_) {
        std::cout << "info string time allocation " << timeLimit_ << "ms\n";
        std::cout.flush();
    }

    int maxDepth = limits.depth > 0 ? limits.depth : MAX_DEPTH;
    maxDepth = std::min(maxDepth, MAX_DEPTH);

    // 迭代加深
    for (int depth = 1; depth <= maxDepth && !stop_; depth++) {
        Value alpha = -VALUE_INFINITE;
        Value beta = VALUE_INFINITE;

        // Aspiration Window：以上一次迭代结果为中心的小窗口，加速收敛
        if (depth >= 5
            && bestValue_ > -VALUE_INFINITE + 200
            && bestValue_ < VALUE_INFINITE - 200) {
            alpha = bestValue_ - 50;
            beta = bestValue_ + 50;
        }

        Value value = alpha_beta(pos, &stack[0], alpha, beta, depth, false, true);
        if (stop_) break;
        lastDepth_ = depth; // 该层搜索成功完成，记录供缓存使用

        // 窗口不够大时重新搜索（fail-low / fail-high）
        while (!stop_ && (value <= alpha || value >= beta)) {
            if (value <= alpha) alpha = -VALUE_INFINITE;
            else beta = VALUE_INFINITE;
            value = alpha_beta(pos, &stack[0], alpha, beta, depth, false, true);
        }
        if (stop_) break;

        // 固化本层根 PV（aspiration 重搜完成后；防未完成层清空 pvTable_）
        bestPvLen_ = pvLen_[0];
        for (int k = 0; k < pvLen_[0] && k < MAX_PLY; k++)
            bestPv_[k] = pvTable_[0][k];

        // 从置换表取根节点最佳走法
        bool ttFound;
        TTEntry* tte = TT.probe(pos.hash(), ttFound);
        if (ttFound && tte->move != MOVE_NONE) {
            bestMove_ = tte->move;
        }
        bestValue_ = value;

        // 输出信息（辅助线程跳过：多线程下 cout 全局锁会拖慢停止）
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime_).count();
        if (!helper_) {
            long long nps = (elapsed > 0) ? (nodes_ * 1000 / elapsed) : nodes_;

            std::cout << "info depth " << depth << " score cp " << bestValue_
                << " nodes " << nodes_ << " time " << elapsed << " nps " << nps
                << " pv " << Position::move_to_string(bestMove_) << "\n";
            std::cout.flush();
        }

        // 检查是否已经找到将杀
        if (bestValue_ >= VALUE_MATE - MAX_PLY || bestValue_ <= -VALUE_MATE + MAX_PLY) {
            break;
        }

        if (elapsed > timeLimit_ * 8 / 10) break;
    }
}

Move Searcher::think(Position& pos, const SearchLimits& limits) {
    searching_ = true;
    iterative_deepening(pos, limits);
    searching_ = false;
    return bestMove_;
}

// 变招精算：在一个不受时间限制的临时上下文里跑 quiescence（只展开吃子），
// 返回 stm 视角真实分。强制变招时对候选着法逐一调用，可识别"走后大子被白吃"
// 的送子着法，从而选出仅次于将军的、损失最小的有效着法。
Value Searcher::qsearch_value(Position& pos) {
    stop_ = false;                                       // 新上下文，清除可能的停标记
    startTime_ = std::chrono::steady_clock::now();       // 防止 check_time 读到未初始化时间误停
    timeLimit_ = 100000;
    Stack stack[MAX_PLY + 10];
    std::memset(stack, 0, sizeof(stack));
    for (int i = 0; i < MAX_PLY + 10; i++) stack[i].ply = i;
    return qsearch(pos, stack, -VALUE_MATE, VALUE_MATE);
}
