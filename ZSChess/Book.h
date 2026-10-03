// Book.h
// 分级局面经验库（开局库 / 定式库 / 精确着法缓存）：
// 按"局面哈希 → 被强搜索确认的最佳着法"入库，相同局面直接命中出棋（微秒~毫秒级，无需重算）。
// 与 NNUE 的分工（对标 Stockfish）：Book 负责【精确局面命中、秒出棋】；
// NNUE 负责【未见局面的泛化评估】。本库为【二进制】紧凑存储、整块读写、内存哈希，
// 按 AI 等级 0..5 分表，各级独立风格；查询时高等级着法可被低等级采用（特级优先，弱 AI 也不走大漏）。
#pragma once
#include "types.h"
#include <array>
#include <string>
#include <unordered_map>
#include <mutex>

namespace zschess {

    class Book {
    public:
        static constexpr int TIERS = 6;

        // 固定 8 字节紧凑记录（自然对齐，无填充）
        struct Entry {
            uint32_t move = 0;    // Move 编码
            int16_t  score = 0;   // 红方视角 centipawn（裁剪 ±32000；杀棋用 ±32000 标记）
            int8_t   depth = 0;   // 入库搜索深度
            uint8_t  visits = 0;  // 被采用次数（饱和到 255）
        };

        static Book& instance();

        // 加载 <exe目录>/book/book_tierN.bin（文件缺失静默跳过）
        void load_all();
        // 保存某一级 / 全部（目录不存在自动创建）
        void save_tier(int tier) const;
        void save_all() const;

        // 查询：从最高级(5)向下到 fromTier 取第一个命中（特级优先）。
        // 命中输出着法 / 红方视角分 / 深度 / 实际命中级别。
        bool probe(uint64_t key, int fromTier, Move& move, int& score, int& depth, int& tierFound) const;
        // 仅查指定级（同风格训练复用时用）
        bool probe_exact(uint64_t key, int tier, Move& move, int& score, int& depth) const;

        // 入库（仅写指定级）：同 key 同 move 累计 visits；更深搜索可覆盖浅着法
        void add(uint64_t key, int tier, Move move, int score, int depth);

        size_t size(int tier) const;
        size_t size_all() const;

    private:
        Book() = default;
        static std::string Dir();   // <exe目录>/book
        static std::string Path(int tier);
        std::array<std::unordered_map<uint64_t, Entry>, TIERS> m_tiers;
        mutable std::mutex m_mutex;
    };

} // namespace zschess
