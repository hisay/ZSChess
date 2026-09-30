#pragma once
// RepetitionGuard.h
// 统一的"重复局面"规则引擎（参照中国象棋 / Fairy-Stockfish 规则精神）：
//   1) 长将禁招：同一方连续将军导致局面重复（杀棋除外）→ 必须变招；
//   2) 长捉禁招：一方用【同一个子】长捉对方【一个无根、又不能互吃】的子，
//      局面重复 → 必须变招；车捉车（可互吃/兑子）、多子联合捉一子、
//      被捉子有根 均【不算】长捉；
//   3) 一将一捉 / 一捉一闲 / 双方闲 等合法重复，同一局面第 3 次形成 → 判和。
//
// 每步走完记录：局面哈希、是否将军、是否"单子捉无根子"以及攻击/被捉子类型。
#include <cstdint>
#include <vector>
#include "position.h"

class RepetitionGuard {
public:
    struct ChaseInfo {
        bool chase;
        int  attType;   // 攻击子 PieceType
        int  tgtType;   // 被捉子 PieceType
        int  attSq;
        int  tgtSq;
        ChaseInfo() : chase(false), attType(-1), tgtType(-1), attSq(-1), tgtSq(-1) {}
    };

    static RepetitionGuard& instance() {
        static RepetitionGuard g;
        return g;
    }

    // 分析 after 局面：刚走完方(~stm) 是否用"唯一一个子"捉对方一个无根、
    // 不能互吃的子。多子联合捉一子不算；将军局面不标 chase（将/捉在重复
    // 判定中分开，从而支持"一将一捉允许、最终判和"）。
    static ChaseInfo AnalyzeChase(const zschess::Position& after) {
        ChaseInfo result;
        if (after.in_check()) return result;                 // 将军步只算将，不算捉

        zschess::Color defender = after.side_to_move();      // 被走方
        zschess::Color mover = static_cast<zschess::Color>(defender ^ 1); // 刚走完方

        // mover 方伪合法着法：用 null move 翻转 stm 生成（棋子布局不变）
        zschess::Position mp = after;
        mp.do_null_move();                                  // stm -> mover
        zschess::Move pmv[zschess::MAX_MOVES];
        int pn = mp.generate_pseudolegal_moves(pmv);

        // defender 方伪合法着法（after.stm 即 defender）
        zschess::Move dmv[zschess::MAX_MOVES];
        int dn = after.generate_pseudolegal_moves(dmv);

        // 遍历 defender 每个子 Q，判断是否被 mover 唯一子 P 长捉
        for (int s = 0; s < zschess::BOARD_SIZE; s++) {
            zschess::Piece qp = after.piece_on(s);
            if (qp == zschess::EMPTY || zschess::piece_color(qp) != defender) continue;
            int qsq = s;

            // 收集能吃 Q 的 mover 攻击子（按 from 去重）
            std::vector<int> attFrom;
            std::vector<zschess::Move> attMove;
            for (int k = 0; k < pn; k++) {
                if (zschess::move_to(pmv[k]) != qsq) continue;
                if (zschess::move_captured(pmv[k]) == zschess::EMPTY) continue;
                int f = zschess::move_from(pmv[k]);
                bool seen = false;
                for (size_t z = 0; z < attFrom.size(); z++)
                    if (attFrom[z] == f) { seen = true; break; }
                if (!seen) { attFrom.push_back(f); attMove.push_back(pmv[k]); }
            }
            if (attFrom.size() != 1) continue;  // 0=没捉；>1=多子联合捉（不算单子长捉）

            int from = attFrom[0];
            zschess::Move cm = attMove[0];

            // 条件1：Q 无根（不被 defender 方其他子保护）
            if (after.is_square_protected(static_cast<zschess::Square>(qsq), defender)) continue;

            // 条件2：Q 不能直接吃 P（互吃/兑子，如车捉车对望，Q 能走到 P 格）
            bool mutual = false;
            for (int k = 0; k < dn; k++)
                if (zschess::move_from(dmv[k]) == qsq && zschess::move_to(dmv[k]) == from) { mutual = true; break; }
            if (mutual) continue;

            // 条件3：P 吃 Q 后落点不被 defender 攻击（吃子安全，非交换陷阱）
            {
                zschess::Position sim = after;
                sim.do_null_move();                         // stm -> mover
                if (!sim.do_move(cm)) continue;
                if (sim.is_square_attacked(static_cast<zschess::Square>(qsq), defender)) continue;
            }

            // 构成"单子捉无根子"：一个循环里通常只有一个活跃捉对，取首个
            ChaseInfo ci;
            ci.chase = true;
            ci.attType = zschess::piece_type(after.piece_on(from));
            ci.tgtType = zschess::piece_type(qp);
            ci.attSq = from;
            ci.tgtSq = qsq;
            return ci;
        }
        return result;
    }

    // 记录一步走完后的局面（自动做长捉分析）
    void PushPosition(const zschess::Position& after) {
        bool chk = after.in_check();
        ChaseInfo ci = chk ? ChaseInfo() : AnalyzeChase(after);
        m_hist.push_back(Entry{ after.hash(), chk, ci.chase, ci.attType, ci.tgtType });
    }

    // 兼容旧接口（仅 hash + 将军，无长捉信息）
    void Push(uint64_t hash, bool check) {
        m_hist.push_back(Entry{ hash, check, false, -1, -1 });
    }

    // 截断到前 keep 步（悔棋/复盘回退：棋谱截断后历史必须同步）
    void Truncate(int keep) {
        if (keep <= 0) { m_hist.clear(); return; }
        if ((int)m_hist.size() > keep) m_hist.resize(keep);
    }

    // 清空（新局/加载棋谱）
    void Reset() { m_hist.clear(); }

    int Count() const { return (int)m_hist.size(); }

    // 长将循环：待走着法将军，且 newHash 上次出现至今同色每步都是将军
    bool IsLongCheckCycle(uint64_t newHash, bool newCheck) const {
        if (!newCheck) return false;
        for (int i = (int)m_hist.size() - 1; i >= 0; i--) {
            if (m_hist[i].hash != newHash) continue;
            for (int j = i + 2; j < (int)m_hist.size(); j += 2)
                if (!m_hist[j].check) return false;
            return true;
        }
        return false;
    }
    void clear() {
        m_hist.clear();
    }
    // 长捉循环：待走着法是"单子捉无根子"，且 newHash 上次出现至今同色每步
    // 都是同一攻击子类型捉同一被捉子类型（中间若将军/闲则为一将一捉/一捉一闲，允许）
    bool IsLongChaseCycle(uint64_t newHash, bool newChase, int newAtt, int newTgt) const {
        if (!newChase) return false;
        for (int i = (int)m_hist.size() - 1; i >= 0; i--) {
            if (m_hist[i].hash != newHash) continue;
            for (int j = i + 2; j < (int)m_hist.size(); j += 2) {
                if (m_hist[j].check || !m_hist[j].chase) return false;
                if (m_hist[j].attType != newAtt || m_hist[j].tgtType != newTgt) return false;
            }
            return true;
        }
        return false;
    }

    // 合法重复（不犯长将/长捉）同一局面第 3 次形成 → 和棋。
    // 在 PushPosition 之后调用：数历史中 newHash（含刚记录的当前局面）出现次数。
    bool IsThreefoldDraw(uint64_t newHash) const {
        int c = 0;
        for (size_t i = 0; i < m_hist.size(); i++)
            if (m_hist[i].hash == newHash) c++;
        return c >= 3;
    }

private:
    RepetitionGuard() {}
    struct Entry {
        uint64_t hash;
        bool check;
        bool chase;
        int  attType;
        int  tgtType;
    };
    std::vector<Entry> m_hist;
};
