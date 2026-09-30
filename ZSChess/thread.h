// thread.h
#ifndef ZSCHESS_THREAD_H
#define ZSCHESS_THREAD_H

#include "types.h"
#include "position.h"
#include "search.h"
#include <vector>
#include <thread>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>

namespace zschess {

    // 线程池：每个线程一个独立的 Position + Searcher 副本，
    // 并行搜索同一局面，主线程（id=0）的搜索结果作为最终结果
    class ThreadPool {
    public:
        static ThreadPool& instance() {
            static ThreadPool pool;
            return pool;
        }

        // 设置线程数
        void set_threads(int n);

        // 获取线程数
        int thread_count() const { return (int)searchers.size(); }

        // 开始搜索，阻塞直到搜索完成，返回主线程的最佳走法
        Move start_search(Position& root_pos, const SearchLimits& limits);

        // 最近一次 start_search 的最佳评估分（stm 视角 centi-pawn；训练入库用）
        int last_score() const { return m_lastScore; }

        // 停止所有线程
        void stop_all();

        // 辅助线程入口（Lazy SMP：helper 模式 + 无限时，由 stop_all 统一停止）
        void run_helper(size_t i, const SearchLimits& lim) {
            searchers[i]->set_helper(true);
            searchers[i]->think(*positions[i], lim);
        }

        // 当前搜索的主变化线（PV）：仅用于界面显示思考线/着法排序，
        // 不用于自动落子——后续着法必须重新完整搜索以保证质量。
        int pv_len() const { return searchers.empty() ? 0 : searchers[0]->pv_len(); }
        Move pv_move(int i) const {
            if (searchers.empty() || i < 0 || i >= searchers[0]->pv_len()) return MOVE_NONE;
            return searchers[0]->pv_move(i);
        }

    private:
        ThreadPool() { set_threads(1); }
        ~ThreadPool() { stop_all(); }

        // 在持锁状态下回收已结束的辅助线程（避免并发 join 同一 std::thread 的 UB）
        void join_helpers_locked();

        std::mutex m_mutex;                 // 保护 threads 容器与创建段
        std::condition_variable m_cv;
        int m_activeSearches = 0;           // 正在执行搜索（主 think 段）的调用数
        int m_lastScore = 0;                // 最近一次主线程搜索结果（stm 视角）

        std::vector<std::unique_ptr<Position>> positions;
        std::vector<std::unique_ptr<Searcher>> searchers;
        std::vector<std::thread> threads;
    };

} // namespace zschess

#endif // ZSCHESS_THREAD_H
