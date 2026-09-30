#ifndef ZSCHESS_TYPES_H
#define ZSCHESS_TYPES_H

#include <vector>
#include <string>
#include <stdlib.h>
#include <stddef.h>

namespace zschess {

    // 棋盘大小：11x11
    constexpr int BOARD_WIDTH = 11;
    constexpr int BOARD_HEIGHT = 11;
    constexpr int BOARD_SIZE = BOARD_WIDTH * BOARD_HEIGHT;
    constexpr int MAX_MOVES = 256;       // 单局面最大走法数
    constexpr int MAX_PLY = 128;         // 最大搜索深度
    constexpr int MAX_DEPTH = MAX_PLY;
    constexpr int MAX_GAME_MOVES = 1024; // 最多游戏步数

    // 颜色定义
    enum Color : int {
        RED = 0,
        BLACK = 1,
        NO_COLOR = 2
    };

    constexpr Color operator~(Color c) { return Color(c ^ 1); }

    // 棋子类型定义
    enum PieceType : int {
        NO_PIECE_TYPE = 0,
        BING = 1,    // 兵/卒：原规则，过河可横走
        JUN = 2,     // 军：新棋子，横竖斜走一格，可后退
        XIANG = 3,   // 相/象：田字格，可过河
        SHI = 4,     // 士/仕：斜线不限步数，可过河（象走法）
        MA = 5,      // 马：日字格，蹩马腿
        PAO = 6,     // 炮：直线，打子需炮架
        HOU = 7,     // 后：新棋子，只能在已方九宫内活动，横竖斜不限步数
        CHE = 8,     // 车：直线不限步数
        DU = 9,      // 督：新棋子，横竖斜不限步数（后）
        JIANG = 10,  // 将/帅：九宫走一格
        PIECE_TYPE_NB = 11
    };

    // 棋子：包含颜色和类型，最高位区分颜色
    using Piece = int;
    constexpr Piece make_piece(Color c, PieceType pt) { return pt | (c << 5); }
    constexpr Color piece_color(Piece p) { return Color(p >> 5); }
    constexpr PieceType piece_type(Piece p) { return PieceType(p & 31); }

    constexpr Piece EMPTY = 0;
    // 红方棋子
    constexpr Piece R_BING = make_piece(RED, BING);
    constexpr Piece R_JUN = make_piece(RED, JUN);
    constexpr Piece R_XIANG = make_piece(RED, XIANG);
    constexpr Piece R_SHI = make_piece(RED, SHI);
    constexpr Piece R_MA = make_piece(RED, MA);
    constexpr Piece R_PAO = make_piece(RED, PAO);
    constexpr Piece R_HOU = make_piece(RED, HOU);
    constexpr Piece R_CHE = make_piece(RED, CHE);
    constexpr Piece R_DU = make_piece(RED, DU);
    constexpr Piece R_JIANG = make_piece(RED, JIANG);
    // 黑方棋子
    constexpr Piece B_BING = make_piece(BLACK, BING);
    constexpr Piece B_JUN = make_piece(BLACK, JUN);
    constexpr Piece B_XIANG = make_piece(BLACK, XIANG);
    constexpr Piece B_SHI = make_piece(BLACK, SHI);
    constexpr Piece B_MA = make_piece(BLACK, MA);
    constexpr Piece B_PAO = make_piece(BLACK, PAO);
    constexpr Piece B_HOU = make_piece(BLACK, HOU);
    constexpr Piece B_CHE = make_piece(BLACK, CHE);
    constexpr Piece B_DU = make_piece(BLACK, DU);
    constexpr Piece B_JIANG = make_piece(BLACK, JIANG);

    // 棋盘坐标，0~120（使用0x88风格但简化为数组索引）
    using Square = int;
    constexpr Square NO_SQUARE = -1;

    // 引擎搜索基础类型（参照 Fairy-Stockfish 的类型体系）
    using Value = int;      // 局面评估分数（分）
    using Depth = int;      // 搜索深度
    using Key = uint64_t;   // 局面哈希键

    constexpr Square make_square(int x, int y) { return y * BOARD_WIDTH + x; }
    constexpr int square_x(Square s) { return s % BOARD_WIDTH; }
    constexpr int square_y(Square s) { return s / BOARD_WIDTH; }
    constexpr bool is_valid_square(int x, int y) { return x >= 0 && x < BOARD_WIDTH && y >= 0 && y < BOARD_HEIGHT; }
    constexpr bool is_valid_square(Square s) { return s >= 0 && s < BOARD_SIZE; }

    // 方向偏移量（一格）
    enum Direction : int {
        DIR_N = -BOARD_WIDTH,     // 上（y减小）
        DIR_S = BOARD_WIDTH,     // 下（y增大）
        DIR_W = -1,               // 左
        DIR_E = 1,               // 右
        DIR_NW = -BOARD_WIDTH - 1, // 左上
        DIR_NE = -BOARD_WIDTH + 1, // 右上
        DIR_SW = BOARD_WIDTH - 1, // 左下
        DIR_SE = BOARD_WIDTH + 1  // 右下
    };

    constexpr Direction rook_dirs[4] = { DIR_N, DIR_S, DIR_W, DIR_E };
    constexpr Direction bishop_dirs[4] = { DIR_NW, DIR_NE, DIR_SW, DIR_SE };
    constexpr Direction queen_dirs[8] = { DIR_N, DIR_S, DIR_W, DIR_E, DIR_NW, DIR_NE, DIR_SW, DIR_SE };
    constexpr Direction king_dirs[8] = { DIR_N, DIR_S, DIR_W, DIR_E, DIR_NW, DIR_NE, DIR_SW, DIR_SE };

    // 走法表示：32位整数
    // bit 0-6   : 起始格
    // bit 7-13  : 目标格
    // bit 14-19 : 被吃子
    // bit 20-25 : 移动的棋子
    // bit 26-31: 特殊标记
    using Move = uint32_t;

    constexpr Move MOVE_NONE = 0;
    constexpr Move make_move(Square from, Square to) { return from | (to << 7); }
    constexpr Move make_move(Square from, Square to, Piece captured, Piece piece) {
        return from | (to << 7) | (captured << 14) | (piece << 20);
    }
    constexpr Square move_from(Move m) { return m & 0x7F; }
    constexpr Square move_to(Move m) { return (m >> 7) & 0x7F; }
    constexpr Piece move_captured(Move m) { return (m >> 14) & 0x3F; }
    constexpr Piece move_piece(Move m) { return (m >> 20) & 0x3F; }
    constexpr bool is_move_ok(Move m) { return m != MOVE_NONE; }

    // 走法列表：moves 与 scores 并行存储（score 用于走法排序，
    // 不再把分数塞进 Move 的位域，避免破坏 from/to/captured/piece 编码）
    struct MoveList {
        std::vector<Move> moves;
        std::vector<int> scores;

        size_t size() const { return moves.size(); }
        bool empty() const { return moves.empty(); }
        void clear() { moves.clear(); scores.clear(); }
        void push_back(Move m) { moves.push_back(m); }
        Move& operator[](size_t i) { return moves[i]; }
        const Move& operator[](size_t i) const { return moves[i]; }
    };

    // 搜索栈帧：ply 为从根起的深度，currentMove 为到达本层的走法，
    // staticEval 为本局面的静态评估（用于空着裁剪等）
    struct Stack {
        int ply;
        Move currentMove;
        Value staticEval;

        Stack() : ply(0), currentMove(MOVE_NONE), staticEval(0) {}
    };

    // 棋子基础价值（单位：分，可自行调整平衡）
    // 子力定位（用户棋风校准，参照 Fairy-Stockfish 的 pieceValue 理念）：
    //   - PAO(炮) 550：进攻主力，价值上调；后(HOU) 400：九宫防守子，价值下调。
    //     炮换后若被回吃即净亏 → AI 不会轻易用进攻主力换防守子；
    //     后换炮赚 → 符合"进攻优先、防守可舍"的棋风。
    constexpr int piece_value[PIECE_TYPE_NB] = {
        0,      // NO_PIECE_TYPE
        100,    // BING  兵
        150,    // JUN   军
        200,    // XIANG 相
        250,    // SHI   士
        400,    // MA    马
        550,    // PAO   炮（进攻主力）
        400,    // HOU   后（九宫防守子）
        950,    // CHE   车
        1200,   // DU    督（全图皇后，价值高于车）
        10000   // JIANG 将
    };

    // 棋子中文名称（用于显示）
    // 把原来的 piece_char 函数替换为：
    inline const char* piece_char(Piece p) {
        if (p == EMPTY) return ".";
        PieceType pt = piece_type(p);
        Color c = piece_color(p);
        if (c == RED) {
            switch (pt) {
            case BING:  return "兵";
            case JUN:   return "军";
            case XIANG: return "相";
            case SHI:   return "仕";
            case MA:    return "馬";
            case PAO:   return "炮";
            case HOU:   return "后";
            case CHE:   return "車";
            case DU:    return "督";
            case JIANG: return "帅";
            default: return "?";
            }
        }
        else {
            switch (pt) {
            case BING:  return "卒";
            case JUN:   return "军";
            case XIANG: return "象";
            case SHI:   return "士";
            case MA:    return "傌";
            case PAO:   return "炮";
            case HOU:   return "后";
            case CHE:   return "俥";
            case DU:    return "督";
            case JIANG: return "将";
            default: return "?";
            }
        }
    }


    inline std::string square_name(Square s) {
        if (!is_valid_square(s)) return "-";
        char file = 'a' + square_x(s);
        // 行号 0~9 用 '0'~'9'，10 用 'a'（避免 '0'+10 输出 ':'）
        char rank = square_y(s) < 10 ? char('0' + square_y(s)) : char('a' + square_y(s) - 10);
        return std::string(1, file) + std::string(1, rank);
    }

    inline Square square_from_name(const std::string& name) {
        if (name.size() < 2) return NO_SQUARE;
        int x = name[0] - 'a';
        int y;
        if (name[1] >= '0' && name[1] <= '9') y = name[1] - '0';
        else if (name[1] >= 'a' && name[1] <= 'k') y = name[1] - 'a' + 10;
        else return NO_SQUARE;
        if (!is_valid_square(x, y)) return NO_SQUARE;
        return make_square(x, y);
    }

    // 过河判断（与界面 CChessPiece.h::HasCrossedRiver 保持一致）
    // 河界在 y=4/5/6 附近：红方从底线 y=10 向上推进，y<=4 视为过河；
    // 黑方从底线 y=0 向下推进，y>=6 视为过河
    inline bool is_crossed_river(Color c, int y) {
        if (c == RED) return y <= 4;
        else return y >= 6;
    }

    // 九宫判断
    inline bool is_in_palace(Color c, int x, int y) {
        // 红方九宫：x=4~6, y=8~10（因为11x11，红方底线在y=10）
        // 黑方九宫：x=4~6, y=0~2（黑方底线在y=0）
        // 注：坐标可以根据你的游戏调整，这里是对称设置
        if (x < 4 || x > 6) return false;
        if (c == RED) return y >= 8 && y <= 10;
        else return y >= 0 && y <= 2;
    }

    inline bool is_in_palace(Color c, Square s) {
        return is_in_palace(c, square_x(s), square_y(s));
    }

} // namespace zschess

#endif // ZSCHESS_TYPES_H
