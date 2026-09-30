#ifndef ZSCHESS_TT_H
#define ZSCHESS_TT_H

#include "types.h"
#include <vector>
#include <cstdint>
#include <atomic>

namespace zschess {

    // ============================================================
    // 置换表 TT —— 参考 Fairy-Stockfish src/tt.h 设计升级：
    //   1) Cluster 桶：每桶 4 个条目，冲突在桶内线性查找（原单槽 → 4 槽），
    //      减少同哈希槽互相覆盖，缓存友好（80B 对齐 8）。
    //   2) 世代机制：每次 new_search 世代 +8（8bit 循环，低 3 位留用），
    //      probe 命中条目刷新世代防老化；替换候选按
    //      "深度 - 8×代数差" 选价值最低者，保留深搜索结论、淘汰旧世代。
    //   3) save 智能：同 key 保留已有 move；BOUND_EXACT / 新 key / 深度远超
    //      才覆盖 key 字段（避免浅层结果冲掉深层结论）。
    //   4) Lazy SMP 并发：字段先写 + release 屏障 + key 最后写；读方先校验
    //      key（对齐 8 字节原子读），匹配后 acquire 屏障再读字段。
    //   接口（probe/store/new_search/hashfull）保持原签名，search.cpp 仅
    //   probe 返回值去 const。
    // ============================================================

    struct TTEntry {
        uint64_t key;       // 完整哈希键（并发下最后写入）
        Move move;          // 最佳走法
        int16_t score;      // 评估分数
        int16_t static_eval;
        uint8_t depth;      // 搜索深度
        uint8_t bound;      // 边界类型
        uint8_t generation; // 世代（8bit 循环，每次 new_search += GEN_DELTA）
        uint8_t padding;    // 对齐
    };

    enum Bound : uint8_t {
        BOUND_NONE = 0,
        BOUND_UPPER = 1,
        BOUND_LOWER = 2,
        BOUND_EXACT = 3
    };

    class TranspositionTable {
        static constexpr int CLUSTER_SIZE = 4;   // 每桶条目数
        static constexpr uint8_t GEN_DELTA = 8;  // 世代增量（低 3 位留用）

        struct Cluster {
            TTEntry entry[CLUSTER_SIZE];
        };

    public:
        TranspositionTable(size_t mb_size = 64);
        ~TranspositionTable();

        void resize(size_t mb_size);
        void clear();
        // 世代自增（8bit 循环）。每次新搜索开始时调用一次。
        void new_search() { generation_.fetch_add(GEN_DELTA, std::memory_order_relaxed); }

        // 查找 key：命中返回条目并刷新其世代；未命中返回桶内"价值最低"的
        // 替换候选（found=false），调用方（store）直接写入该槽。
        TTEntry* probe(uint64_t key, bool& found);
        void store(uint64_t key, Move m, int score, int static_eval, int depth, Bound bound);
        int hashfull() const;

    private:
        // 返回条目的替换价值：深度 - 8×代数差（越大越值得保留）
        static int replace_value(const TTEntry& e, uint8_t cur_gen);

        std::vector<Cluster> table_;
        size_t clusterCount_;
        std::atomic<uint8_t> generation_;
    };

    extern TranspositionTable TT;

} // namespace zschess

#endif
