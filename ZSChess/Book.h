// Book.h
// 局面经验库（开局库/定式库）：把"被 AI 确认的最佳着法"按局面哈希入库，
// 相同局面直接查库出棋，无需重算。文件为可读文本，支持合并统计与胜负学习。
#pragma once
#include "types.h"
#include <string>
#include <unordered_map>
#include <mutex>

namespace zschess {

    class Book {
    public:
        struct Entry {
            Move move = MOVE_NONE;
            int score = 0;      // 搜索/评估分（红方视角）
            int depth = 0;      // 入库时的搜索深度
            int visits = 0;     // 被采用次数
            int wins = 0;       // 由此走法最终获胜的局数（红胜算红方走法，黑胜算黑方走法）
        };

        static Book& instance();

        // 加载库文件（不存在则静默返回）
        void load(const std::string& path);
        // 保存库文件（目录不存在则创建）
        void save(const std::string& path) const;

        // 查库：命中返回 true，并输出该局面的记录
        bool probe(uint64_t hash, Move& best, int& score, int& depth) const;

        // 入库/合并：同 hash 同 move 则累计 visits；不同 move 则取深度更大的
        void add(uint64_t hash, Move m, int score, int depth);

        // 终局学习：moveSideWon 表示走这步的一方最终获胜，累加其胜场
        void learn(uint64_t hash, Move m, bool moveSideWon);

        size_t size() const { std::lock_guard<std::mutex> lk(m_mutex); return m_entries.size(); }

        // 命中质量门槛：库深度不足请求深度 70% 时不使用
        bool usable(uint64_t hash, int reqDepth) const;

    private:
        Book() = default;
        std::unordered_map<uint64_t, Entry> m_entries;
        mutable std::mutex m_mutex;
    };

} // namespace zschess
