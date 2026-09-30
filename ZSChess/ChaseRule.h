#pragma once
// ChaseRule.h
// "禁止一子长捉"中"捉"的集中判定（标准A口径）：
//   走完本步后，若本步移动子攻击某敌子，且该敌子"无根"
//   （移动子吃它不会被对方吃回），则构成一次"捉"。
// 与"将"互斥分流：本步是将军时返回 0，交给长将规则处理。
// 判定结果哈希为 chaseKey：同一局面 + 同一捉对集合 重现即判"长捉循环"。
//
// 实现说明：
//   引擎的 is_square_attacked / is_square_protected 对滑行子（车/督/后/士）
//   存在"棋子在自己所在格 dx=dy=0 也算攻击/保护"的自匹配缺陷，
//   因此这里不用 is_square_protected 判根，改用"模拟吃回"：
//   把移动子摆到被捉格（移除被捉子）后，is_square_attacked(格, 对方)
//   为目标格是敌色子、扫描的是对方子，不触发自匹配，几何判断准确。
//   攻击归属用"移除移动子后不再被本方攻击"判定（若本方其他子同攻该格，
//   保守跳过——只会漏判不会误禁）。
#include "position.h"
#include <vector>
#include <algorithm>

namespace zschess {
namespace chase {

    // 用棋盘数组微调后重建局面（set_board_state 自动重算将位/哈希/历史栈）
    inline Position RebuildWithEdits(const Position& base,
                                     const Piece* edits, const Square* editSq, int n,
                                     Color stm) {
        Piece arr[BOARD_SIZE];
        for (int s = 0; s < BOARD_SIZE; s++) arr[s] = base.piece_on((Square)s);
        for (int i = 0; i < n; i++) arr[editSq[i]] = edits[i];
        Position p;
        p.set_board_state(arr, stm);
        return p;
    }

    // 计算"走完本步后"移动子构成的捉集合哈希。
    // after   ：走完后的局面（stm = 对方走子权）
    // moverSq ：移动子走完后的所在格
    // 返回 0 = 本步不构成捉（或本步是将军，让位长将规则）
    inline uint64_t ComputeChases(const Position& after, Square moverSq) {
        if (!zschess::is_valid_square(moverSq)) return 0;
        if (after.in_check()) return 0;                        // 将/捉互斥分流
        Color mover = ~after.side_to_move();
        Piece mp = after.piece_on(moverSq);
        if (mp == EMPTY || piece_color(mp) != mover) return 0; // 防御：格上不是移动方棋子

        // 移除移动子后的局面：用于判定"该格攻击是否由移动子构成"
        {
            Square rmSq[1] = { moverSq };
            Piece  rmEd[1] = { EMPTY };
            Position noMover = RebuildWithEdits(after, rmEd, rmSq, 1, after.side_to_move());

            Color opp = ~mover;
            std::vector<Square> victims;
            for (int s = 0; s < BOARD_SIZE; s++) {
                Piece v = after.piece_on((Square)s);
                if (v == EMPTY || piece_color(v) != opp) continue;
                if (piece_type(v) == JIANG) continue;          // 攻将属将军语义，走长将分支
                if (!after.is_square_attacked((Square)s, mover)) continue;
                if (noMover.is_square_attacked((Square)s, mover)) continue; // 非移动子单独构成攻击 → 保守跳过

                // 有根判定：模拟移动子吃掉该子（摆上目标格、腾空原格），
                // 对方若能"吃回"该格（真实攻击语义：炮需恰一炮架等），即为有根，不算捉
                Piece  ed[2] = { mp, EMPTY };
                Square sq[2] = { (Square)s, moverSq };
                Position ex = RebuildWithEdits(after, ed, sq, 2, mover);
                if (ex.is_square_attacked((Square)s, opp)) continue; // 有根

                victims.push_back((Square)s);
            }
            if (victims.empty()) return 0;

            // 全部捉对（移动子格 + 按格序的被捉格集合）混列为一个 key，
            // 防止"改捉另一个子"绕过检测
            std::sort(victims.begin(), victims.end());
            uint64_t key = 1469598103934665603ull;              // FNV-1a offset
            key = (key ^ (uint64_t)(moverSq + 1)) * 1099511628211ull;
            for (size_t i = 0; i < victims.size(); i++)
                key = (key ^ (uint64_t)(victims[i] + 1)) * 1099511628211ull;
            return key ? key : 1ull;
        }
    }

} // namespace chase
} // namespace zschess
