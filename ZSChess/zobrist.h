#ifndef ZSCHESS_ZOBRIST_H
#define ZSCHESS_ZOBRIST_H

#include "types.h"

namespace zschess {

    class Zobrist {
    public:
        // 初始化随机数表
        static void init();

        // 棋子在某格的哈希值
        static uint64_t psq[2][PIECE_TYPE_NB][BOARD_SIZE];

        // 走子方哈希
        static uint64_t side;
    };

} // namespace zschess

#endif // ZSCHESS_ZOBRIST_H
