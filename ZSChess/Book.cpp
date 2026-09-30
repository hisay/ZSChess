// Book.cpp
#include "Book.h"
#include "position.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <io.h>
#include <direct.h>

namespace zschess {

    // 解析 UCI 风格走法字符串（如 "a0b0" = (0,0)->(1,0)）；非法返回 MOVE_NONE
    static Move ParseMoveString(const char* s) {
        if (!s || strlen(s) < 4) return MOVE_NONE;
        auto sq = [](char a, char b) -> int {
            int x = a - 'a';
            int y = b - '0';
            if (x < 0 || x > 10 || y < 0 || y > 10) return -1;
            return make_square(x, y);
        };
        int f = sq(s[0], s[1]);
        int t = sq(s[2], s[3]);
        if (f < 0 || t < 0) return MOVE_NONE;
        return make_move((Square)f, (Square)t);
    }

    Book& Book::instance() {
        static Book b;
        return b;
    }

    void Book::load(const std::string& path) {
        FILE* f = nullptr;
        if (fopen_s(&f, path.c_str(), "r") != 0 || !f) return;
        std::lock_guard<std::mutex> lk(m_mutex);
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
            unsigned long long h = 0;
            char mv[16] = { 0 };
            int score = 0, depth = 0, visits = 0, wins = 0;
            if (sscanf_s(line, "%llu %15s %d %d %d %d", &h, mv, (unsigned)sizeof(mv),
                &score, &depth, &visits, &wins) >= 4) {
                Move m = ParseMoveString(mv);
                if (m == MOVE_NONE) continue;
                Entry e; e.move = m; e.score = score; e.depth = depth;
                e.visits = visits > 0 ? visits : 1; e.wins = wins;
                m_entries[(uint64_t)h] = e;
            }
        }
        fclose(f);
    }

    void Book::save(const std::string& path) const {
        // 确保目录存在
        std::string dir = path;
        size_t slash = dir.find_last_of("/\\");
        if (slash != std::string::npos) {
            std::string d = dir.substr(0, slash);
            if (!d.empty()) _mkdir(d.c_str());
        }
        FILE* f = nullptr;
        if (fopen_s(&f, path.c_str(), "w") != 0 || !f) return;
        std::lock_guard<std::mutex> lk(m_mutex);
        fprintf(f, "# ZSChess book v1  hash move score depth visits wins\n");
        for (auto& kv : m_entries) {
            const Entry& e = kv.second;
            std::string mv = Position::move_to_string(e.move);
            fprintf(f, "%llu %s %d %d %d %d\n", (unsigned long long)kv.first,
                mv.c_str(), e.score, e.depth, e.visits, e.wins);
        }
        fclose(f);
    }

    bool Book::probe(uint64_t hash, Move& best, int& score, int& depth) const {
        std::lock_guard<std::mutex> lk(m_mutex);
        auto it = m_entries.find(hash);
        if (it == m_entries.end()) return false;
        best = it->second.move;
        score = it->second.score;
        depth = it->second.depth;
        return true;
    }

    void Book::add(uint64_t hash, Move m, int score, int depth) {
        if (m == MOVE_NONE) return;
        std::lock_guard<std::mutex> lk(m_mutex);
        auto it = m_entries.find(hash);
        if (it == m_entries.end()) {
            Entry e; e.move = m; e.score = score; e.depth = depth; e.visits = 1; e.wins = 0;
            m_entries[hash] = e;
        } else {
            Entry& e = it->second;
            if (e.move == m) {
                e.visits++;
                if (depth > e.depth) { e.depth = depth; e.score = score; }
            } else if (depth >= e.depth) {
                // 更深搜索覆盖旧走法；浅层则不覆盖（保守）
                e.move = m; e.score = score; e.depth = depth; e.visits++;
            } else {
                e.visits++; // 浅层同局面走不同棋：仅累计该走法次数（不覆盖主条目）
            }
        }
    }

    void Book::learn(uint64_t hash, Move m, bool moveSideWon) {
        if (m == MOVE_NONE || !moveSideWon) return;
        std::lock_guard<std::mutex> lk(m_mutex);
        auto it = m_entries.find(hash);
        if (it == m_entries.end()) return;
        if (it->second.move == m) it->second.wins++;
    }

    bool Book::usable(uint64_t hash, int reqDepth) const {
        std::lock_guard<std::mutex> lk(m_mutex);
        auto it = m_entries.find(hash);
        if (it == m_entries.end()) return false;
        if (reqDepth <= 0) return true;
        return it->second.depth * 10 >= reqDepth * 7; // 库深度 >= 请求深度 70%
    }

} // namespace zschess
