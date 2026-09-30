#include "tt.h"
#include <cstring>
#include <algorithm>

namespace zschess {

    TranspositionTable TT(64);

    TranspositionTable::TranspositionTable(size_t mb_size) : generation_(0) {
        resize(mb_size);
    }

    TranspositionTable::~TranspositionTable() {}

    void TranspositionTable::resize(size_t mb_size) {
        clusterCount_ = (mb_size * 1024 * 1024) / sizeof(Cluster);
        clusterCount_ = std::max(clusterCount_, size_t(1024));
        table_.resize(clusterCount_);
        clear();
    }

    void TranspositionTable::clear() {
        memset(table_.data(), 0, clusterCount_ * sizeof(Cluster));
        generation_.store(0, std::memory_order_relaxed);
    }

    int TranspositionTable::replace_value(const TTEntry& e, uint8_t cur_gen) {
        // 代数差 = (cur - entry) 在 8bit 循环下的差值；GEN_DELTA=8 → >>3 得代数
        uint8_t diff = (uint8_t)(cur_gen - e.generation);
        int age = diff >> 3; // 代数差
        return (int)e.depth - 8 * age; // 深度 - 8×年龄（SF 同款策略）
    }

    TTEntry* TranspositionTable::probe(uint64_t key, bool& found) {
        Cluster& c = table_[key % clusterCount_];
        uint8_t cur_gen = generation_.load(std::memory_order_relaxed);

        // 1) 桶内查找：完整 key 匹配 → 命中
        for (int i = 0; i < CLUSTER_SIZE; i++) {
            TTEntry& e = c.entry[i];
            uint64_t k = e.key; // 对齐 8 字节原子读
            std::atomic_thread_fence(std::memory_order_acquire);
            if (k == key) {
                e.generation = cur_gen; // 刷新世代防老化（单字节原子写）
                found = true;
                return &e;
            }
        }

        // 2) 未命中：返回"价值最低"的替换候选（空槽 depth=0 天然价值最低）
        TTEntry* replace = &c.entry[0];
        for (int i = 1; i < CLUSTER_SIZE; i++) {
            if (replace_value(c.entry[i], cur_gen) < replace_value(*replace, cur_gen))
                replace = &c.entry[i];
        }
        found = false;
        return replace;
    }

    void TranspositionTable::store(uint64_t key, Move m, int score, int static_eval, int depth, Bound bound) {
        bool found = false;
        TTEntry* tte = probe(key, found);
        uint8_t cur_gen = generation_.load(std::memory_order_relaxed);

        // 同 key：保留已有 move（SF 策略：新 move 非空或 key 变化时才替换 move）
        if (m != MOVE_NONE || tte->key != key)
            tte->move = m;

        // 覆盖条件（SF save）：BOUND_EXACT / 新 key / 深度远超（>= +2 层）
        bool overwrite = (bound == BOUND_EXACT)
            || tte->key != key
            || depth + 2 >= tte->depth;

        if (overwrite) {
            // 数据字段先写，release 屏障后再写 key：读方见 key 匹配即可安全读字段
            tte->score = (int16_t)score;
            tte->static_eval = (int16_t)static_eval;
            tte->depth = (uint8_t)depth;
            tte->bound = bound;
            tte->generation = cur_gen;
            std::atomic_thread_fence(std::memory_order_release);
            tte->key = key;
        }
    }

    int TranspositionTable::hashfull() const {
        int cnt = 0;
        uint8_t cur_gen = generation_.load(std::memory_order_relaxed);
        for (size_t i = 0; i < 1000 && i < clusterCount_; i++) {
            for (int j = 0; j < CLUSTER_SIZE; j++) {
                const TTEntry& e = table_[i].entry[j];
                if (e.depth != 0 && e.generation == cur_gen)
                    cnt++;
            }
        }
        return cnt / CLUSTER_SIZE;
    }

} // namespace zschess
