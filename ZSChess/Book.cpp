// Book.cpp — 二进制分级着法库实现
#include "Book.h"
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <direct.h>
#include <sys/stat.h>

namespace zschess {

    static_assert(sizeof(Book::Entry) == 8, "Book::Entry must be 8 bytes");

    namespace {
        constexpr char MAGIC[4] = { 'Z', 'S', 'B', 'K' };
        constexpr uint16_t VERSION = 1;

        int clampTier(int t) { return t < 0 ? 0 : (t > 5 ? 5 : t); }
        int16_t clampScore(int s) {
            if (s > 32000) return 32000;
            if (s < -32000) return -32000;
            return (int16_t)s;
        }
        int8_t clampDepth(int d) {
            if (d < 0) return 0;
            if (d > 127) return 127;
            return (int8_t)d;
        }
        uint8_t satVisits(uint8_t v) { return v < 255 ? (uint8_t)(v + 1) : 255; }
    }

    Book& Book::instance() {
        static Book* p = nullptr;
        static std::mutex gate;
        if (!p) {
            std::lock_guard<std::mutex> lk(gate);
            if (!p) {
                Book* q = new Book();
                q->load_all();   // 首次访问自动加载已有二进制着法库
                p = q;
            }
        }
        return *p;
    }

    std::string Book::Dir() {
        char buf[512] = { 0 };
        GetModuleFileNameA(nullptr, buf, 512);
        std::string p(buf);
        size_t slash = p.find_last_of("/\\");
        if (slash != std::string::npos) p = p.substr(0, slash);
        return p + "\\book";
    }

    std::string Book::Path(int tier) {
        return Dir() + "\\book_tier" + std::to_string(clampTier(tier)) + ".bin";
    }

    void Book::load_all() {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (int t = 0; t < TIERS; t++) {
            FILE* f = nullptr;
            if (fopen_s(&f, Path(t).c_str(), "rb") != 0 || !f) continue;
            char magic[4] = { 0 };
            uint16_t version = 0;
            uint32_t count = 0;
            if (fread(magic, 1, 4, f) == 4 && memcmp(magic, MAGIC, 4) == 0 &&
                fread(&version, sizeof(version), 1, f) == 1 && version == VERSION &&
                fread(&count, sizeof(count), 1, f) == 1) {
                auto& map = m_tiers[t];
                map.reserve((size_t)count + map.size());
                for (uint32_t i = 0; i < count; i++) {
                    uint64_t key = 0;
                    Entry e;
                    if (fread(&key, sizeof(key), 1, f) != 1 ||
                        fread(&e, sizeof(e), 1, f) != 1) break;
                    map[key] = e;
                }
            }
            fclose(f);
        }
    }

    void Book::save_tier(int tier) const {
        std::lock_guard<std::mutex> lk(m_mutex);
        int t = clampTier(tier);
        std::string dir = Dir();
        _mkdir(dir.c_str());   // 已存在返回 -1，忽略
        FILE* f = nullptr;
        if (fopen_s(&f, Path(t).c_str(), "wb") != 0 || !f) return;
        const auto& map = m_tiers[t];
        uint32_t count = (uint32_t)map.size();
        fwrite(MAGIC, 1, 4, f);
        fwrite(&VERSION, sizeof(VERSION), 1, f);
        fwrite(&count, sizeof(count), 1, f);
        for (const auto& kv : map) {
            fwrite(&kv.first, sizeof(kv.first), 1, f);
            fwrite(&kv.second, sizeof(kv.second), 1, f);
        }
        fclose(f);
    }

    void Book::save_all() const {
        for (int t = 0; t < TIERS; t++) {
            bool nonempty;
            { std::lock_guard<std::mutex> lk(m_mutex); nonempty = !m_tiers[t].empty(); }
            if (nonempty) save_tier(t);
        }
    }

    bool Book::probe(uint64_t key, int fromTier, Move& move, int& score, int& depth, int& tierFound) const {
        std::lock_guard<std::mutex> lk(m_mutex);
        int lo = clampTier(fromTier);
        for (int t = TIERS - 1; t >= lo; t--) {
            auto it = m_tiers[t].find(key);
            if (it != m_tiers[t].end()) {
                move = (Move)it->second.move;
                score = it->second.score;
                depth = it->second.depth;
                tierFound = t;
                return true;
            }
        }
        return false;
    }

    bool Book::probe_exact(uint64_t key, int tier, Move& move, int& score, int& depth) const {
        std::lock_guard<std::mutex> lk(m_mutex);
        auto it = m_tiers[clampTier(tier)].find(key);
        if (it == m_tiers[clampTier(tier)].end()) return false;
        move = (Move)it->second.move;
        score = it->second.score;
        depth = it->second.depth;
        return true;
    }

    void Book::add(uint64_t key, int tier, Move move, int score, int depth) {
        if (move == MOVE_NONE) return;
        std::lock_guard<std::mutex> lk(m_mutex);
        auto& map = m_tiers[clampTier(tier)];
        auto it = map.find(key);
        if (it == map.end()) {
            Entry e; e.move = (uint32_t)move; e.score = clampScore(score);
            e.depth = clampDepth(depth); e.visits = 1;
            map[key] = e;
        } else {
            Entry& e = it->second;
            if (e.move == (uint32_t)move) {
                e.visits = satVisits(e.visits);
                if (depth > (int)e.depth) { e.depth = clampDepth(depth); e.score = clampScore(score); }
            } else if (depth >= (int)e.depth) {
                uint8_t keepV = e.visits;
                e.move = (uint32_t)move; e.score = clampScore(score); e.depth = clampDepth(depth);
                e.visits = satVisits(keepV - 1);
            } else {
                e.visits = satVisits(e.visits);
            }
        }
    }

    size_t Book::size(int tier) const {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_tiers[clampTier(tier)].size();
    }

    size_t Book::size_all() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        size_t n = 0;
        for (const auto& m : m_tiers) n += m.size();
        return n;
    }

} // namespace zschess
