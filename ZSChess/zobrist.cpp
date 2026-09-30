#include "zobrist.h"
#include <random>

namespace zschess {

    uint64_t Zobrist::psq[2][PIECE_TYPE_NB][BOARD_SIZE];
    uint64_t Zobrist::side;

    void Zobrist::init() {
        // 只初始化一次：如果多个 Position 各自触发 init 而随机表被重置，
        // 已存在局面的 zobrist_hash 会与新的随机表不一致（多线程时尤其危险）
        static bool initialized = false;
        if (initialized) return;
        initialized = true;

        std::mt19937_64 rng(0x123456789ABCDEFULL);
        for (int c = 0; c < 2; c++) {
            for (int pt = 0; pt < PIECE_TYPE_NB; pt++) {
                for (int s = 0; s < BOARD_SIZE; s++) {
                    psq[c][pt][s] = rng();
                }
            }
        }
        side = rng();
    }

} // namespace zschess
