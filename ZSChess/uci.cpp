#include "position.h"
#include "search.h"
#include "tt.h"
#include "zobrist.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <signal.h>
#include <cstdlib>
#include <stdio.h>
#include <iostream>
#include <sstream>
using namespace zschess;

// UCI 调试协议入口（工程内未主动调用，仅保证可编译、可复用）
static Position pos;
static Searcher* g_searcher = nullptr;
static std::atomic<bool> search_running{ false };
static std::mutex g_posMutex;

static void stop_current_search() {
    if (g_searcher) g_searcher->stop();
}

void uci_loop() {
    std::string line, token;
    Zobrist::init();
    {
        std::lock_guard<std::mutex> lock(g_posMutex);
        pos.set_initial_position();
    }
    g_searcher = new Searcher();

    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        token.clear();
        iss >> std::skipws >> token;

        if (token == "uci") {
            std::cout << "id name ZSChess 1.0" << std::endl;
            std::cout << "id author ZSChess Dev" << std::endl;
            std::cout << "uciok" << std::endl;
        }
        else if (token == "isready") {
            std::cout << "readyok" << std::endl;
        }
        else if (token == "ucinewgame") {
            TT.clear();
            std::lock_guard<std::mutex> lock(g_posMutex);
            pos.set_initial_position();
        }
        else if (token == "position") {
            if (search_running.load()) {
                stop_current_search();
                while (search_running.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            std::string type;
            iss >> type;
            if (type == "startpos") {
                std::lock_guard<std::mutex> lock(g_posMutex);
                pos.set_initial_position();
                std::string moves;
                iss >> moves; // "moves"
                while (iss >> moves) {
                    Move m = pos.move_from_string(moves);
                    if (is_move_ok(m)) pos.do_move(m);
                }
            }
        }
        else if (token == "d") {
            std::lock_guard<std::mutex> lock(g_posMutex);
            std::cout << pos.to_string() << std::endl;
        }
        else if (token == "go") {
            if (search_running.load()) {
                stop_current_search();
                while (search_running.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            SearchLimits limits;
            std::string param;
            while (iss >> param) {
                if (param == "depth") iss >> limits.depth;
                else if (param == "movetime") iss >> limits.movetime;
                else if (param == "wtime") iss >> limits.wtime;
                else if (param == "btime") iss >> limits.btime;
                else if (param == "winc") iss >> limits.winc;
                else if (param == "binc") iss >> limits.binc;
                else if (param == "infinite") limits.infinite = true;
            }

            // 默认深度6
            if (limits.movetime == 0 && limits.depth == MAX_PLY) {
                limits.depth = 6;
            }

            // 复制当前局面到独立副本，搜索在后台线程进行，避免与 position 命令竞争
            Position posCopy;
            {
                std::lock_guard<std::mutex> lock(g_posMutex);
                posCopy = pos;
            }

            search_running = true;
            std::thread t([posCopy, limits]() mutable {
                Move best = g_searcher->think(posCopy, limits);
                std::cout << "bestmove " << Position::move_to_string(best) << std::endl;
                std::cout.flush();
                search_running = false;
                });
            t.detach();
        }
        else if (token == "stop") {
            if (search_running.load()) {
                stop_current_search();
            }
        }
        else if (token == "quit") {
            if (search_running.load()) {
                stop_current_search();
                while (search_running.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            break;
        }
        else if (token == "perft") {
            // 性能测试：走法生成计数（调试用）
            int depth;
            iss >> depth;
            if (depth < 1) depth = 1;
            std::lock_guard<std::mutex> lock(g_posMutex);
            uint64_t nodes = 0;
            Move temp[MAX_MOVES];
            int count = pos.generate_legal_moves(temp);
            for (int i = 0; i < count; i++) {
                pos.do_move(temp[i]);
                nodes += pos.perft(depth - 1);
                pos.undo_move();
            }
            std::cout << "perft(" << depth << ") = " << nodes << std::endl;
        }

        std::cout.flush();
    }

    delete g_searcher;
    g_searcher = nullptr;
}
