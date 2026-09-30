// thread.cpp
// windows.h 最先包含：SEH（__try/__except）需要，且避免 min/max 宏污染 STL
#include <windows.h>
#include "thread.h"
#include "tt.h"
#include "search.h"
#include <atomic>

namespace zschess {

    namespace {
        // SEH 包装：辅助线程崩溃时打印现场而不是静默挂死（崩溃现场定位用）。
        // 注意 __try 块内不能有需要析构的 C++ 局部对象（/EHsc 限制），
        // 因此只做外部函数调用。
        static void run_helper_safe(ThreadPool* pool, size_t i, const SearchLimits& lim) {
            __try {
                pool->run_helper(i, lim);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                DWORD code = GetExceptionCode();
                printf("[HELPER %zu] CRASH code=0x%X\n", i, (unsigned)code);
                fflush(stdout);
            }
        }
    }

    // 并发安全说明：
    //   - stop_all() 会被多个线程同时调用（UI 线程 Cancel、start_search 开头/尾部、
    //     set_threads、析构）。若不对 join 加锁，两个线程并发 join 同一个
    //     std::thread 是数据竞争/UB，Release 下必然崩溃（表现为取消/回退时崩溃）。
    //   - start_search 的线程创建段（emplace_back 到 threads、拷贝根局面）与
    //     stop_all 的 join/clear 也必须互斥，否则 vector 并发修改同样是 UB。
    //   - set_threads 重建 positions/searchers 前必须等待所有在跑搜索结束，
    //     否则正在搜索的线程会访问已释放的 Position。

    void ThreadPool::join_helpers_locked() {
        for (auto& t : threads) {
            if (t.joinable()) t.join();
        }
        threads.clear();
    }

    void ThreadPool::set_threads(int n) {
        if (n < 1) n = 1;
        std::unique_lock<std::mutex> lk(m_mutex);
        // 等待正在执行的搜索全部结束（调用方通常已先 Cancel，搜索会很快收尾）
        m_cv.wait(lk, [this] { return m_activeSearches == 0; });
        for (auto& s : searchers) {
            if (s) s->stop();
        }
        join_helpers_locked();
        positions.clear();
        searchers.clear();

        for (int i = 0; i < n; i++) {
            positions.emplace_back(std::make_unique<Position>());
            searchers.emplace_back(std::make_unique<Searcher>());
        }
    }

    void ThreadPool::stop_all() {
            std::lock_guard<std::mutex> lk(m_mutex);
            for (auto& s : searchers) {
                if (s) s->stop();
            }
        join_helpers_locked();
    }

    Move ThreadPool::start_search(Position& root_pos, const SearchLimits& limits) {
        // 创建段：与其它 stop_all / start_search / set_threads 互斥，
        // 等待上一局辅助线程回收后再复用容器。
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            join_helpers_locked();

            // 把根局面复制给所有线程
            for (size_t i = 0; i < positions.size(); i++) {
                *positions[i] = root_pos;
            }

            // 主线程：正常时间管理；辅助线程：Lazy SMP（延迟启动、加深一层、
            // 不做时间管理，只由 stop_all 统一停止）。共享全局 TT 使辅助线程
            // 立即受益于主线程已填入的置换表（走法排序/裁剪），并行提速。
            SearchLimits helperLimits = limits;
            if (limits.depth > 0 && limits.depth < MAX_DEPTH)
                helperLimits.depth = limits.depth + 1;

            // 启动工作线程（id >= 1），延迟 1~N ms 启动，避免与主线程同频争抢。
            // helperLimits 按值捕获，避免引用捕获在并发/异常路径下的生命周期问题。
            for (size_t i = 1; i < searchers.size(); i++) {
                threads.emplace_back([this, i, helperLimits]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1 + (int)(i % 3)));
                    run_helper_safe(this, i, helperLimits);
                });
            }
            m_activeSearches++;
        }

        // 主线程（id = 0）同步搜索：由它管理时间，其结果为最终结果
        searchers[0]->set_helper(false);
        Move best = searchers[0]->think(*positions[0], limits);
        m_lastScore = searchers[0]->best_value();

        // 停止所有辅助线程并回收（持锁，与其它 stop_all 互日）
        // 关键：主线程时限到期后必须统一停表，否则辅助线程演练到 depth+1
        // （时限设为 10^9 不停），join 会等它们搜完整层，导致每步超出时限数十秒。
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_activeSearches--;
            for (auto& s : searchers) if (s) s->stop(); // 通知辅助线程立即停止
            join_helpers_locked();
            if (m_activeSearches == 0) m_cv.notify_all();
        }

        return best;
    }

} // namespace zschess
