#include "evaluate.h"
#include "position.h"

namespace zschess {
    namespace Eval {

        // ============================================================
        // Piece-square tables (PST). All tables are written from RED's
        // viewpoint: row index 0 = BLACK back rank (y=0), row 10 = RED
        // back rank (y=10). BLACK pieces use mirrored row (10 - y).
        // Every table is horizontally symmetric around x=5, so the same
        // table works for both colors without x mirroring.
        // Values are bonus points added to piece_value (centi-pawns).
        // ============================================================

        // BING (pawn): the deeper it pushes, the more valuable.
        static const int bing_pst[BOARD_HEIGHT][BOARD_WIDTH] = {
            {100,100,105,110,110,110,110,110,105,100,100}, // y0: enemy back rank
            {95, 95, 100,105,105,105,105,105,100,95, 95},  // y1
            {90, 90, 95, 100,100,100,100,100,95, 90, 90},  // y2
            {80, 80, 85, 90, 90, 90, 90, 90, 85, 80, 80},  // y3
            {70, 70, 75, 80, 80, 80, 80, 80, 75, 70, 70},  // y4: crossed river
            {50, 50, 55, 60, 60, 60, 60, 60, 55, 50, 50},  // y5: before river
            {30, 30, 35, 40, 40, 40, 40, 40, 35, 30, 30},  // y6
            {10, 10, 15, 20, 20, 20, 20, 20, 15, 10, 10},  // y7: pawn start
            {5,  5,  8,  10, 10, 10, 10, 10, 8,  5,  5},   // y8
            {2,  2,  3,  4,  4,  4,  4,  4,  3,  2,  2},   // y9
            {0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0}    // y10: own back rank
        };

        // CHE (rook): open center / deep penetration.
        static const int che_pst[BOARD_HEIGHT][BOARD_WIDTH] = {
            {0,0,5,10,10,10,10,10,5,0,0},
            {0,0,5,10,15,15,15,15,5,0,0},
            {0,0,10,15,20,20,20,15,10,0,0},
            {0,5,15,20,25,25,25,20,15,5,0},
            {5,10,20,25,30,30,30,25,20,10,5},
            {5,10,20,25,30,30,30,25,20,10,5},
            {5,10,20,25,30,30,30,25,20,10,5},
            {0,5,15,20,25,25,25,20,15,5,0},
            {0,0,10,15,20,20,20,15,10,0,0},
            {0,0,5,10,15,15,15,15,5,0,0},
            {0,0,5,10,10,10,10,10,5,0,0}
        };

        // MA (horse): center strong, edges/back ranks weak.
        static const int ma_pst[BOARD_HEIGHT][BOARD_WIDTH] = {
            {0, 0, 5, 8, 10, 10, 10, 8, 5, 0, 0},
            {0, 5, 10, 15, 18, 20, 18, 15, 10, 5, 0},
            {5, 10, 18, 25, 30, 32, 30, 25, 18, 10, 5},
            {8, 15, 25, 32, 38, 40, 38, 32, 25, 15, 8},
            {10, 18, 30, 38, 45, 48, 45, 38, 30, 18, 10},
            {10, 18, 30, 38, 45, 48, 45, 38, 30, 18, 10},
            {10, 18, 30, 38, 45, 48, 45, 38, 30, 18, 10},
            {8, 15, 25, 32, 38, 40, 38, 32, 25, 15, 8},
            {5, 10, 18, 25, 30, 32, 30, 25, 18, 10, 5},
            {0, 5, 10, 15, 18, 20, 18, 15, 10, 5, 0},
            {0, 0, 5, 8, 10, 10, 10, 8, 5, 0, 0}
        };

        // PAO (cannon): central cannon strong, sides and back weak.
        static const int pao_pst[BOARD_HEIGHT][BOARD_WIDTH] = {
            {0, 0, 0, 5, 10, 10, 10, 5, 0, 0, 0},
            {0, 5, 10, 15, 20, 20, 20, 15, 10, 5, 0},
            {5, 10, 15, 20, 25, 25, 25, 20, 15, 10, 5},
            {8, 15, 20, 25, 30, 30, 30, 25, 20, 15, 8},
            {10, 18, 25, 30, 35, 35, 35, 30, 25, 18, 10},
            {10, 18, 25, 30, 35, 35, 35, 30, 25, 18, 10},
            {10, 18, 25, 30, 35, 35, 35, 30, 25, 18, 10},
            {8, 15, 20, 25, 30, 30, 30, 25, 20, 15, 8},
            {5, 10, 15, 20, 25, 25, 25, 20, 15, 10, 5},
            {0, 5, 10, 15, 20, 20, 20, 15, 10, 5, 0},
            {0, 0, 0, 5, 10, 10, 10, 5, 0, 0, 0}
        };

        // XIANG (elephant, may cross river in this game): home half strong.
        static const int xiang_pst[BOARD_HEIGHT][BOARD_WIDTH] = {
            {2, 2, 3, 4, 4, 4, 4, 4, 3, 2, 2},   // y0
            {3, 3, 4, 5, 5, 5, 5, 5, 4, 3, 3},   // y1
            {4, 4, 5, 6, 6, 6, 6, 6, 5, 4, 4},   // y2
            {5, 5, 6, 7, 7, 7, 7, 7, 6, 5, 5},   // y3
            {6, 6, 8, 9, 10, 10, 10, 9, 8, 6, 6},// y4
            {8, 8, 10, 12, 14, 14, 14, 12, 10, 8, 8}, // y5: river
            {10, 10, 12, 14, 16, 16, 16, 14, 12, 10, 10}, // y6
            {12, 12, 14, 16, 18, 18, 18, 16, 14, 12, 12}, // y7
            {14, 14, 16, 18, 20, 20, 20, 18, 16, 14, 14}, // y8
            {15, 15, 17, 19, 22, 22, 22, 19, 17, 15, 15}, // y9: start
            {14, 14, 16, 18, 20, 20, 20, 18, 16, 14, 14}  // y10
        };

        // SHI (advisor, diagonal unlimited, may cross river): palace guard.
        static const int shi_pst[BOARD_HEIGHT][BOARD_WIDTH] = {
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},   // y0
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},   // y1
            {0, 0, 0, 0, 2, 2, 2, 0, 0, 0, 0},   // y2
            {0, 0, 0, 2, 4, 4, 4, 2, 0, 0, 0},   // y3
            {0, 0, 2, 4, 6, 6, 6, 4, 2, 0, 0},   // y4
            {2, 2, 4, 6, 8, 8, 8, 6, 4, 2, 2},   // y5
            {4, 4, 6, 8, 10, 10, 10, 8, 6, 4, 4},// y6
            {6, 6, 8, 12, 14, 14, 14, 12, 8, 6, 6},// y7
            {10, 10, 12, 18, 22, 22, 22, 18, 12, 10, 10}, // y8 palace
            {12, 12, 14, 20, 26, 26, 26, 20, 14, 12, 12}, // y9 start
            {12, 12, 14, 20, 26, 26, 26, 20, 14, 12, 12}  // y10
        };

        // HOU (queen confined to palace): strong in/near palace.
        static const int hou_pst[BOARD_HEIGHT][BOARD_WIDTH] = {
            {0, 0, 0, 0, 8, 10, 8, 0, 0, 0, 0},   // y0
            {0, 0, 0, 0, 12, 14, 12, 0, 0, 0, 0}, // y1
            {0, 0, 0, 0, 16, 18, 16, 0, 0, 0, 0}, // y2
            {0, 0, 2, 4, 6, 8, 6, 4, 2, 0, 0},    // y3
            {0, 2, 4, 6, 8, 10, 8, 6, 4, 2, 0},   // y4
            {0, 2, 4, 6, 8, 10, 8, 6, 4, 2, 0},   // y5
            {0, 2, 4, 6, 8, 10, 8, 6, 4, 2, 0},   // y6
            {0, 0, 2, 4, 6, 8, 6, 4, 2, 0, 0},    // y7
            {0, 0, 0, 0, 16, 18, 16, 0, 0, 0, 0}, // y8 palace
            {0, 0, 0, 0, 20, 22, 20, 0, 0, 0, 0}, // y9 start
            {0, 0, 0, 0, 18, 20, 18, 0, 0, 0, 0}  // y10
        };

        // DU (full-board queen): strong center, weaker edges.
        static const int du_pst[BOARD_HEIGHT][BOARD_WIDTH] = {
            {5, 5, 8, 10, 12, 12, 12, 10, 8, 5, 5},   // y0
            {5, 8, 12, 15, 18, 18, 18, 15, 12, 8, 5}, // y1
            {8, 12, 18, 22, 25, 25, 25, 22, 18, 12, 8}, // y2
            {10, 15, 22, 28, 32, 34, 32, 28, 22, 15, 10}, // y3
            {12, 18, 25, 32, 38, 40, 38, 32, 25, 18, 12}, // y4
            {12, 18, 25, 32, 38, 40, 38, 32, 25, 18, 12}, // y5
            {12, 18, 25, 32, 38, 40, 38, 32, 25, 18, 12}, // y6
            {10, 15, 22, 28, 32, 34, 32, 28, 22, 15, 10}, // y7
            {8, 12, 18, 22, 25, 25, 25, 22, 18, 12, 8},   // y8
            {5, 8, 12, 15, 18, 18, 18, 15, 12, 8, 5},     // y9
            {5, 5, 8, 10, 12, 12, 12, 10, 8, 5, 5}        // y10
        };

        // JUN (one-step king-like piece): center slightly better.
        static const int jun_pst[BOARD_HEIGHT][BOARD_WIDTH] = {
            {0, 0, 2, 4, 6, 6, 6, 4, 2, 0, 0},   // y0
            {0, 2, 4, 6, 8, 8, 8, 6, 4, 2, 0},   // y1
            {2, 4, 6, 8, 10, 10, 10, 8, 6, 4, 2},// y2
            {4, 6, 8, 10, 12, 12, 12, 10, 8, 6, 4}, // y3
            {4, 6, 10, 12, 14, 14, 14, 12, 10, 6, 4}, // y4
            {4, 6, 10, 12, 14, 14, 14, 12, 10, 6, 4}, // y5
            {4, 6, 10, 12, 14, 14, 14, 12, 10, 6, 4}, // y6
            {4, 6, 8, 10, 12, 12, 12, 10, 8, 6, 4},   // y7
            {2, 4, 6, 8, 10, 10, 10, 8, 6, 4, 2},     // y8
            {0, 2, 4, 6, 8, 8, 8, 6, 4, 2, 0},        // y9
            {0, 0, 2, 4, 6, 6, 6, 4, 2, 0, 0}         // y10
        };

        // JIANG (king): stay safe in palace center; higher when defending.
        // 将帅：开局动王是废着，底线安全份很高，离开底线剧减
        // （开局时所有着法静态分接近，若帅离底线只损 4 分，
        // 搜索/move-ordering 会随机命中“帅六进一”等废着）
        static const int jiang_pst[BOARD_HEIGHT][BOARD_WIDTH] = {
            {0, 0, 0, 0, 26, 30, 26, 0, 0, 0, 0},   // y0
            {0, 0, 0, 0, 18, 22, 18, 0, 0, 0, 0},   // y1
            {0, 0, 0, 0, 12, 14, 12, 0, 0, 0, 0},   // y2
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},      // y3
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},      // y4
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},      // y5
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},      // y6
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},      // y7
            {0, 0, 0, 0, 4, 5, 4, 0, 0, 0, 0},      // y8
            {0, 0, 0, 0, 7, 8, 7, 0, 0, 0, 0},      // y9
            {0, 0, 0, 0, 26, 30, 26, 0, 0, 0, 0}    // y10 start
        };

        // Table lookup helper: BLACK mirrors the row (10 - y).
        static inline int pst_lookup(const int table[BOARD_HEIGHT][BOARD_WIDTH], Color c, int x, int y) {
            int row = (c == RED) ? y : (BOARD_HEIGHT - 1 - y);
            return table[row][x];
        }

        int evaluate(const Position& pos) {
            Color stm = pos.side_to_move();

            int red_score = 0;
            int black_score = 0;

            // 威胁惩罚（Threats，参照 Fairy-Stockfish 评估项）：
            // Fairy-Stockfish 对"被攻击且无根"的大子（hanging piece）按子力
            // 价值比例重罚（约 1/3~1/2），对"有根被攻"只轻微扣分；因为无根
            // 大子被便宜子攻击意味着下回合白送，有根大子只是处于子力争夺。
            // 之前的统一 -100 太轻：督(1200)/车(950) 送掉的代价远高于 100，
            // 浅层搜索（15 秒限时下通常只能到深度 6~8）看不到交换链时，
            // 静态评估不足以拦住"车送兵口 / 督退挡送吃 / 督被捉不逃"。
            constexpr int THREAT_PENALTY_PROTECTED = 40;  // 有根被攻：轻微
            constexpr int THREAT_PENALTY_MAX = 400;       // 无根被攻：按比例上限

            for (int y = 0; y < BOARD_HEIGHT; y++) {
                for (int x = 0; x < BOARD_WIDTH; x++) {
                    Piece p = pos.piece_on(make_square(x, y));
                    if (p == EMPTY) continue;

                    Color c = piece_color(p);
                    PieceType pt = piece_type(p);
                    int val = piece_value[pt];
                    int pst = 0;

                    switch (pt) {
                    case BING:  pst = pst_lookup(bing_pst, c, x, y); break;
                    case CHE:   pst = pst_lookup(che_pst, c, x, y); break;
                    case MA:    pst = pst_lookup(ma_pst, c, x, y); break;
                    case PAO:   pst = pst_lookup(pao_pst, c, x, y); break;
                    case XIANG: pst = pst_lookup(xiang_pst, c, x, y); break;
                    case SHI:   pst = pst_lookup(shi_pst, c, x, y); break;
                    case HOU:   pst = pst_lookup(hou_pst, c, x, y); break;
                    case DU:    pst = pst_lookup(du_pst, c, x, y); break;
                    case JUN:   pst = pst_lookup(jun_pst, c, x, y); break;
                    case JIANG: pst = pst_lookup(jiang_pst, c, x, y); break;
                    default: break;
                    }

                    int pieceScore = val + pst;

                    // 威胁检测：只对"值得保护"的大子做（将帅由将军惩罚单独处理）
                    // Fairy-Stockfish 的 hanging 判定：被对方攻击且无己方保护 → 重罚；
                    // 有己方保护 → 仅轻微扣分（子力争夺，胜负交由搜索判定）。
                    if (pt == CHE || pt == PAO || pt == MA || pt == HOU || pt == DU) {
                        if (pos.is_square_attacked(make_square(x, y), ~c)) {
                            // 保护判定用真实能吃语义（王不含照面、炮需炮架），避免假保护
                            bool defended = pos.is_square_protected(make_square(x, y), c);
                            if (defended) {
                                pieceScore -= THREAT_PENALTY_PROTECTED;
                            } else {
                                // 无根大子被攻击：按子力价值比例重罚（上限 400）
                                int hang = val * 2 / 5;
                                if (hang > THREAT_PENALTY_MAX) hang = THREAT_PENALTY_MAX;
                                pieceScore -= hang;
                            }
                        }
                    }

                    if (c == RED) red_score += pieceScore;
                    else black_score += pieceScore;
                }
            }

            // Penalty for being in check: the side to move is threatened.
            if (pos.in_check()) {
                if (stm == RED) red_score -= 80;
                else black_score -= 80;
            }

            int score = red_score - black_score;
            // Return score from the side-to-move viewpoint:
            // positive means the side to move is better.
            return (stm == RED) ? score : -score;
        }

    } // namespace Eval
} // namespace zschess
