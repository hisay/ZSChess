#include "position.h"
#include "zobrist.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>

namespace zschess {

    Position::Position() : stm(RED), zobrist_hash(0), fifty_move(0) {
        memset(board, 0, sizeof(board));
        king_sq[0] = king_sq[1] = NO_SQUARE;
        Zobrist::init();
    }

    void Position::set_initial_position() {
        while (!history.empty()) history.pop();
        memset(board, 0, sizeof(board));
        stm = RED;
        zobrist_hash = 0;
        fifty_move = 0;

        // 初始布局必须与界面 CPieceMng::initialPieces 完全一致，
        // 否则引擎计算的局面与界面实际玩的局面对不上。
        // 关键：将/帅同在 x=5 列，中间有 y=3 / y=7 的军遮挡，避免开局即"白脸将"。
        // 黑方（上方，y=0 为底线）
        board[make_square(0, 0)] = B_CHE;
        board[make_square(1, 0)] = B_MA;
        board[make_square(2, 0)] = B_XIANG;
        board[make_square(3, 0)] = B_SHI;
        board[make_square(4, 0)] = B_HOU;
        board[make_square(5, 0)] = B_JIANG;
        board[make_square(6, 0)] = B_HOU;
        board[make_square(7, 0)] = B_SHI;
        board[make_square(8, 0)] = B_XIANG;
        board[make_square(9, 0)] = B_MA;
        board[make_square(10, 0)] = B_CHE;

        // 黑方 y=2：炮 x1/x9，督 x5
        board[make_square(1, 2)] = B_PAO;
        board[make_square(9, 2)] = B_PAO;
        board[make_square(5, 2)] = B_DU;

        // 黑方 y=3：卒 x0/x2/x8/x10，军 x4/x5/x6
        board[make_square(0, 3)] = B_BING;
        board[make_square(2, 3)] = B_BING;
        board[make_square(4, 3)] = B_JUN;
        board[make_square(5, 3)] = B_JUN;
        board[make_square(6, 3)] = B_JUN;
        board[make_square(8, 3)] = B_BING;
        board[make_square(10, 3)] = B_BING;

        // 红方（下方，y=10 为底线）
        board[make_square(0, 10)] = R_CHE;
        board[make_square(1, 10)] = R_MA;
        board[make_square(2, 10)] = R_XIANG;
        board[make_square(3, 10)] = R_SHI;
        board[make_square(4, 10)] = R_HOU;
        board[make_square(5, 10)] = R_JIANG;
        board[make_square(6, 10)] = R_HOU;
        board[make_square(7, 10)] = R_SHI;
        board[make_square(8, 10)] = R_XIANG;
        board[make_square(9, 10)] = R_MA;
        board[make_square(10, 10)] = R_CHE;

        // 红方 y=8：炮 x1/x9，督 x5
        board[make_square(1, 8)] = R_PAO;
        board[make_square(9, 8)] = R_PAO;
        board[make_square(5, 8)] = R_DU;

        // 红方 y=7：兵 x0/x2/x8/x10，军 x4/x5/x6
        board[make_square(0, 7)] = R_BING;
        board[make_square(2, 7)] = R_BING;
        board[make_square(4, 7)] = R_JUN;
        board[make_square(5, 7)] = R_JUN;
        board[make_square(6, 7)] = R_JUN;
        board[make_square(8, 7)] = R_BING;
        board[make_square(10, 7)] = R_BING;

        // 记录将/帅位置
        king_sq[BLACK] = make_square(5, 0);
        king_sq[RED] = make_square(5, 10);

        // 计算哈希
        zobrist_hash = 0;
        for (int s = 0; s < BOARD_SIZE; s++) {
            Piece p = board[s];
            if (p != EMPTY) {
                zobrist_hash ^= Zobrist::psq[piece_color(p)][piece_type(p)][s];
            }
        }
    }

    std::string Position::to_string() const {
        std::ostringstream oss;
        oss << "\n   a b c d e f g h i j k\n";
        for (int y = 0; y < BOARD_HEIGHT; y++) {
            oss << " " << y << " ";
            for (int x = 0; x < BOARD_WIDTH; x++) {
                Piece p = piece_on(x, y);
                oss << piece_char(p) << " ";
            }
            oss << y;
            // 标注河道
            if (y == 3 || y == 6) oss << "  <-- 楚河汉界";
            oss << "\n";
        }
        oss << "   a b c d e f g h i j k\n";
        oss << "走方: " << (stm == RED ? "红方" : "黑方") << "\n";
        oss << "哈希: " << std::hex << zobrist_hash << std::dec << "\n";
        return oss.str();
    }

    std::string Position::move_to_string(Move m) {
        if (!is_move_ok(m)) return "0000";
        return square_name(move_from(m)) + square_name(move_to(m));
    }

    Move Position::move_from_string(const std::string& str) const {
        if (str.size() < 4) return MOVE_NONE;
        Square from = square_from_name(str.substr(0, 2));
        Square to = square_from_name(str.substr(2, 2));
        if (!is_valid_square(from) || !is_valid_square(to)) return MOVE_NONE;
        Piece p = piece_on(from);
        if (p == EMPTY) return MOVE_NONE;
        Piece captured = piece_on(to);
        return make_move(from, to, captured, p);
    }

    bool Position::is_square_attacked(Square s, Color by) const {
        int sx = square_x(s), sy = square_y(s);

        // 检查所有对方棋子
        for (int y = 0; y < BOARD_HEIGHT; y++) {
            for (int x = 0; x < BOARD_WIDTH; x++) {
                Square from = make_square(x, y);
                Piece p = board[from];
                if (p == EMPTY || piece_color(p) != by) continue;
                PieceType pt = piece_type(p);
                int dx = abs(sx - x);
                int dy = abs(sy - y);

                switch (pt) {
                case CHE: {
                    // 车：直线，中间无障碍
                    if (dx == 0 || dy == 0) {
                        bool blocked = false;
                        int step_x = (sx > x) ? 1 : (sx < x) ? -1 : 0;
                        int step_y = (sy > y) ? 1 : (sy < y) ? -1 : 0;
                        int cx = x + step_x, cy = y + step_y;
                        while (cx != sx || cy != sy) {
                            if (piece_on(cx, cy) != EMPTY) { blocked = true; break; }
                            cx += step_x; cy += step_y;
                        }
                        if (!blocked) return true;
                    }
                    break;
                }
                case PAO: {
                    // 炮：直线，吃子需要一个炮架
                    if (dx == 0 || dy == 0) {
                        int cnt = 0;
                        int step_x = (sx > x) ? 1 : (sx < x) ? -1 : 0;
                        int step_y = (sy > y) ? 1 : (sy < y) ? -1 : 0;
                        int cx = x + step_x, cy = y + step_y;
                        while (cx != sx || cy != sy) {
                            if (piece_on(cx, cy) != EMPTY) cnt++;
                            cx += step_x; cy += step_y;
                        }
                        // 炮攻击需要恰好一个炮架
                        if (piece_on(s) != EMPTY && cnt == 1) return true;
                        // 炮不吃子时是直线无阻挡
                        if (piece_on(s) == EMPTY && cnt == 0) return true;
                    }
                    break;
                }
                case SHI:
                case DU: {
                    // 士/督：斜线（士）+横竖斜（督），滑动无障碍
                    if (pt == DU && (dx == 0 || dy == 0)) {
                        // 横竖和车一样
                        bool blocked = false;
                        int step_x = (sx > x) ? 1 : (sx < x) ? -1 : 0;
                        int step_y = (sy > y) ? 1 : (sy < y) ? -1 : 0;
                        int cx = x + step_x, cy = y + step_y;
                        while (cx != sx || cy != sy) {
                            if (piece_on(cx, cy) != EMPTY) { blocked = true; break; }
                            cx += step_x; cy += step_y;
                        }
                        if (!blocked) return true;
                    }
                    if (dx == dy && dx > 0) {
                        // 对角线
                        bool blocked = false;
                        int step_x = (sx > x) ? 1 : -1;
                        int step_y = (sy > y) ? 1 : -1;
                        int cx = x + step_x, cy = y + step_y;
                        while (cx != sx || cy != sy) {
                            if (piece_on(cx, cy) != EMPTY) { blocked = true; break; }
                            cx += step_x; cy += step_y;
                        }
                        if (!blocked) return true;
                    }
                    break;
                }
                case HOU: {
                    // 后：仅限己方九宫内横竖斜（界面规则：后=九宫内的督，
                    // 只在自己九宫活动，不能攻击九宫外的棋子）
                    if (!is_in_palace(by, sx, sy)) break; // 目标格必须在己方九宫（检查目标格，非攻击者位置）
                    if (dx == 0 || dy == 0) {
                        bool blocked = false;
                        int step_x = (sx > x) ? 1 : (sx < x) ? -1 : 0;
                        int step_y = (sy > y) ? 1 : (sy < y) ? -1 : 0;
                        int cx = x + step_x, cy = y + step_y;
                        while (cx != sx || cy != sy) {
                            if (piece_on(cx, cy) != EMPTY) { blocked = true; break; }
                            cx += step_x; cy += step_y;
                        }
                        if (!blocked) return true;
                    }
                    if (dx == dy && dx > 0) {
                        bool blocked = false;
                        int step_x = (sx > x) ? 1 : -1;
                        int step_y = (sy > y) ? 1 : -1;
                        int cx = x + step_x, cy = y + step_y;
                        while (cx != sx || cy != sy) {
                            if (piece_on(cx, cy) != EMPTY) { blocked = true; break; }
                            cx += step_x; cy += step_y;
                        }
                        if (!blocked) return true;
                    }
                    break;
                }
                case XIANG: {
                    // 相：田字格，塞象眼
                    if (dx == 2 && dy == 2) {
                        // 象眼在中间
                        if (piece_on((x + sx) / 2, (y + sy) / 2) == EMPTY) return true;
                    }
                    break;
                }
                case MA: {
                    // 马：日字格，蹩马腿
                    if ((dx == 2 && dy == 1) || (dx == 1 && dy == 2)) {
                        // 马腿位置
                        int leg_x, leg_y;
                        if (dx == 2) { leg_x = x + (sx > x ? 1 : -1); leg_y = y; }
                        else { leg_x = x; leg_y = y + (sy > y ? 1 : -1); }
                        if (piece_on(leg_x, leg_y) == EMPTY) return true;
                    }
                    break;
                }
                case JUN: {
                    // 军：横竖斜走一格（国王走法）
                    if (dx <= 1 && dy <= 1) return true;
                    break;
                }
                case BING: {
                    // 兵
                    if (by == RED) {
                        // 红兵：y从大到小为前进方向（y减小）
                        if (sy == y - 1 && sx == x) return true; // 前进
                        if (is_crossed_river(RED, y) && sy == y && abs(sx - x) == 1) return true; // 过河横走
                    }
                    else {
                        // 黑卒：y从小到大前进
                        if (sy == y + 1 && sx == x) return true;
                        if (is_crossed_river(BLACK, y) && sy == y && abs(sx - x) == 1) return true;
                    }
                    break;
                }
                case JIANG: {
                    // 将：走一格，九宫
                    if (is_in_palace(by, sx, sy) && dx <= 1 && dy <= 1 && dx + dy > 0) return true;
                    // 将对将（白脸将）
                    if (pt == JIANG && dx == 0) {
                        bool blocked = false;
                        int my = std::min(y, sy), My = std::max(y, sy);
                        for (int cy = my + 1; cy < My; cy++) {
                            if (piece_on(x, cy) != EMPTY) { blocked = true; break; }
                        }
                        if (!blocked) return true;
                    }
                    break;
                }
                default: break;
                }
            }
        }
        return false;
    }

    bool Position::is_square_protected(Square s, Color by) const {
        int sx = square_x(s), sy = square_y(s);
        for (int y = 0; y < BOARD_HEIGHT; y++) {
            for (int x = 0; x < BOARD_WIDTH; x++) {
                Square from = make_square(x, y);
                Piece p = board[from];
                if (p == EMPTY || piece_color(p) != by) continue;
                PieceType pt = piece_type(p);
                int dx = abs(sx - x);
                int dy = abs(sy - y);
                switch (pt) {
                case CHE: {
                    if (dx == 0 || dy == 0) {
                        bool blocked = false;
                        int step_x = (sx > x) ? 1 : (sx < x) ? -1 : 0;
                        int step_y = (sy > y) ? 1 : (sy < y) ? -1 : 0;
                        int cx = x + step_x, cy = y + step_y;
                        while (cx != sx || cy != sy) {
                            if (piece_on(cx, cy) != EMPTY) { blocked = true; break; }
                            cx += step_x; cy += step_y;
                        }
                        if (!blocked) return true;
                    }
                    break;
                }
                case PAO: {
                    // 保护语义：炮必须"能吃"该格（恰好一个炮架，且目标格有子）
                    if (dx == 0 || dy == 0) {
                        int cnt = 0;
                        int step_x = (sx > x) ? 1 : (sx < x) ? -1 : 0;
                        int step_y = (sy > y) ? 1 : (sy < y) ? -1 : 0;
                        int cx = x + step_x, cy = y + step_y;
                        while (cx != sx || cy != sy) {
                            if (piece_on(cx, cy) != EMPTY) cnt++;
                            cx += step_x; cy += step_y;
                        }
                        if (piece_on(s) != EMPTY && cnt == 1) return true;
                    }
                    break;
                }
                case SHI:
                case DU: {
                    if (pt == DU && (dx == 0 || dy == 0)) {
                        bool blocked = false;
                        int step_x = (sx > x) ? 1 : (sx < x) ? -1 : 0;
                        int step_y = (sy > y) ? 1 : (sy < y) ? -1 : 0;
                        int cx = x + step_x, cy = y + step_y;
                        while (cx != sx || cy != sy) {
                            if (piece_on(cx, cy) != EMPTY) { blocked = true; break; }
                            cx += step_x; cy += step_y;
                        }
                        if (!blocked) return true;
                    }
                    if (dx == dy && dx > 0) {
                        bool blocked = false;
                        int step_x = (sx > x) ? 1 : -1;
                        int step_y = (sy > y) ? 1 : -1;
                        int cx = x + step_x, cy = y + step_y;
                        while (cx != sx || cy != sy) {
                            if (piece_on(cx, cy) != EMPTY) { blocked = true; break; }
                            cx += step_x; cy += step_y;
                        }
                        if (!blocked) return true;
                    }
                    break;
                }
                case HOU: {
                    if (!is_in_palace(by, x, y)) break; // 后在九宫外无法走/保护
                    if (dx == 0 || dy == 0) {
                        bool blocked = false;
                        int step_x = (sx > x) ? 1 : (sx < x) ? -1 : 0;
                        int step_y = (sy > y) ? 1 : (sy < y) ? -1 : 0;
                        int cx = x + step_x, cy = y + step_y;
                        while (cx != sx || cy != sy) {
                            if (piece_on(cx, cy) != EMPTY) { blocked = true; break; }
                            cx += step_x; cy += step_y;
                        }
                        if (!blocked && is_in_palace(by, sx, sy)) return true;
                    }
                    if (dx == dy && dx > 0) {
                        bool blocked = false;
                        int step_x = (sx > x) ? 1 : -1;
                        int step_y = (sy > y) ? 1 : -1;
                        int cx = x + step_x, cy = y + step_y;
                        while (cx != sx || cy != sy) {
                            if (piece_on(cx, cy) != EMPTY) { blocked = true; break; }
                            cx += step_x; cy += step_y;
                        }
                        if (!blocked && is_in_palace(by, sx, sy)) return true;
                    }
                    break;
                }
                case XIANG: {
                    if (dx == 2 && dy == 2) {
                        if (piece_on((x + sx) / 2, (y + sy) / 2) == EMPTY) return true;
                    }
                    break;
                }
                case MA: {
                    if ((dx == 2 && dy == 1) || (dx == 1 && dy == 2)) {
                        int leg_x, leg_y;
                        if (dx == 2) { leg_x = x + (sx > x ? 1 : -1); leg_y = y; }
                        else { leg_x = x; leg_y = y + (sy > y ? 1 : -1); }
                        if (piece_on(leg_x, leg_y) == EMPTY) return true;
                    }
                    break;
                }
                case JUN: {
                    if (dx <= 1 && dy <= 1 && dx + dy > 0) return true;
                    break;
                }
                case BING: {
                    if (by == RED) {
                        if (sy == y - 1 && sx == x) return true;
                        if (is_crossed_river(RED, y) && sy == y && abs(sx - x) == 1) return true;
                    }
                    else {
                        if (sy == y + 1 && sx == x) return true;
                        if (is_crossed_river(BLACK, y) && sy == y && abs(sx - x) == 1) return true;
                    }
                    break;
                }
                case JIANG: {
                    // 保护语义：王只保护九宫内邻格（不含"白脸将"照面——王吃不到远处）
                    if (is_in_palace(by, sx, sy) && dx <= 1 && dy <= 1 && dx + dy > 0) return true;
                    break;
                }
                default: break;
                }
            }
        }
        return false;
    }

    bool Position::in_check() const {
        return is_square_attacked(king_sq[stm], ~stm);
    }

    // 滑动走法生成模板
    template<Color C>
    void Position::gen_sliding_moves(Move* ml, int& idx, Square from,const Direction* Dirs, int NDirs) const {
        Piece p = board[from];
        for (int d = 0; d < NDirs; d++) {
            Direction dir = Dirs[d];
            Square to = from + dir;
            while (is_valid_square(to)) {
                Piece target = board[to];
                int tx = square_x(to), ty = square_y(to);
                // 边界检查：防止横向绕回
                if (abs(tx - square_x(to - dir)) > 1) break;

                if (target == EMPTY) {
                    ml[idx++] = make_move(from, to, EMPTY, p);
                }
                else {
                    if (piece_color(target) != C) {
                        ml[idx++] = make_move(from, to, target, p);
                    }
                    break; // 被棋子挡住
                }
                to += dir;
            }
        }
    }

    // 兵走法生成
    template<Color C>
    void Position::gen_bing_moves(Move* ml, int& idx, Square from) const {
        int x = square_x(from), y = square_y(from);
        Piece p = board[from];
        constexpr int forward = (C == RED) ? -1 : 1;

        // 前进一格（可吃子：目标为空或为对方棋子——传统兵直前进吃子）
        int ny = y + forward;
        if (is_valid_square(x, ny)) {
            Square to = make_square(x, ny);
            Piece target = board[to];
            if (target == EMPTY || piece_color(target) != C) {
                ml[idx++] = make_move(from, to, target, p);
            }
        }
        // 过河后可横走
        if (is_crossed_river(C, y)) {
            for (int dx : {-1, 1}) {
                int nx = x + dx;
                if (is_valid_square(nx, y)) {
                    Square to = make_square(nx, y);
                    Piece target = board[to];
                    if (target == EMPTY || piece_color(target) != C) {
                        ml[idx++] = make_move(from, to, target, p);
                    }
                }
            }
        }
    }

    // 军走法：横竖斜走一格（国王走法，不限九宫）
    template<Color C>
    void Position::gen_jun_moves(Move* ml, int& idx, Square from) const {
        int x = square_x(from), y = square_y(from);
        Piece p = board[from];
        for (int dx : {-1, 0, 1}) {
            for (int dy : {-1, 0, 1}) {
                if (dx == 0 && dy == 0) continue;
                int nx = x + dx, ny = y + dy;
                if (!is_valid_square(nx, ny)) continue;
                Square to = make_square(nx, ny);
                Piece target = board[to];
                if (target == EMPTY || piece_color(target) != C) {
                    ml[idx++] = make_move(from, to, target, p);
                }
            }
        }
    }

    // 相：田字格，可过河，塞象眼
    template<Color C>
    void Position::gen_xiang_moves(Move* ml, int& idx, Square from) const {
        int x = square_x(from), y = square_y(from);
        Piece p = board[from];
        // 4个田字方向
        const int dxy[4][2] = { {2,2}, {2,-2}, {-2,2}, {-2,-2} };
        for (int d = 0; d < 4; d++) {
            int nx = x + dxy[d][0];
            int ny = y + dxy[d][1];
            if (!is_valid_square(nx, ny)) continue;
            // 象眼位置
            int ex = x + dxy[d][0] / 2;
            int ey = y + dxy[d][1] / 2;
            if (board[make_square(ex, ey)] != EMPTY) continue; // 塞象眼
            Square to = make_square(nx, ny);
            Piece target = board[to];
            if (target == EMPTY || piece_color(target) != C) {
                ml[idx++] = make_move(from, to, target, p);
            }
        }
    }

    // 士：斜线不限步数，可过河（象走法）
    template<Color C>
    void Position::gen_shi_moves(Move* ml, int& idx, Square from) const {
        gen_sliding_moves<C>(ml, idx, from, bishop_dirs, 4);
    }

    // 马：日字格，蹩马腿
    template<Color C>
    void Position::gen_ma_moves(Move* ml, int& idx, Square from) const {
        int x = square_x(from), y = square_y(from);
        Piece p = board[from];
        // 8个日字方向：dx, dy, 马腿dx, 马腿dy
        const int dirs[8][4] = {
            {2, 1, 1, 0}, {-2, 1, -1, 0}, {2, -1, 1, 0}, {-2, -1, -1, 0},
            {1, 2, 0, 1}, {1, -2, 0, -1}, {-1, 2, 0, 1}, {-1, -2, 0, -1}
        };
        for (int d = 0; d < 8; d++) {
            int nx = x + dirs[d][0];
            int ny = y + dirs[d][1];
            if (!is_valid_square(nx, ny)) continue;
            int lx = x + dirs[d][2];
            int ly = y + dirs[d][3];
            if (board[make_square(lx, ly)] != EMPTY) continue; // 蹩马腿
            Square to = make_square(nx, ny);
            Piece target = board[to];
            if (target == EMPTY || piece_color(target) != C) {
                ml[idx++] = make_move(from, to, target, p);
            }
        }
    }

    // 炮：直线走，吃子需炮架
    template<Color C>
    void Position::gen_pao_moves(Move* ml, int& idx, Square from) const {
        int x = square_x(from), y = square_y(from);
        Piece p = board[from];
        // 四个直线方向
        const int dirs[4][2] = { {0,1}, {0,-1}, {1,0}, {-1,0} };
        for (int d = 0; d < 4; d++) {
            int nx = x, ny = y;
            bool jumped = false; // 是否已跳过一个炮架
            while (true) {
                nx += dirs[d][0];
                ny += dirs[d][1];
                if (!is_valid_square(nx, ny)) break;
                Square to = make_square(nx, ny);
                Piece target = board[to];
                if (!jumped) {
                    if (target == EMPTY) {
                        ml[idx++] = make_move(from, to, EMPTY, p);
                    }
                    else {
                        jumped = true; // 遇到炮架
                    }
                }
                else {
                    if (target != EMPTY) {
                        if (piece_color(target) != C) {
                            ml[idx++] = make_move(from, to, target, p);
                        }
                        break; // 第二个棋子挡住
                    }
                }
            }
        }
    }

    // 后：九宫内横竖斜不限步数
    template<Color C>
    void Position::gen_hou_moves(Move* ml, int& idx, Square from) const {
        int x = square_x(from), y = square_y(from);
        Piece p = board[from];
        // 8个方向
        const int dirs[8][2] = {
            {0,1}, {0,-1}, {1,0}, {-1,0},
            {1,1}, {1,-1}, {-1,1}, {-1,-1}
        };
        for (int d = 0; d < 8; d++) {
            int nx = x, ny = y;
            while (true) {
                nx += dirs[d][0];
                ny += dirs[d][1];
                if (!is_valid_square(nx, ny)) break;
                if (!is_in_palace(C, nx, ny)) break; // 不能出九宫！
                Square to = make_square(nx, ny);
                Piece target = board[to];
                if (target == EMPTY) {
                    ml[idx++] = make_move(from, to, EMPTY, p);
                }
                else {
                    if (piece_color(target) != C) {
                        ml[idx++] = make_move(from, to, target, p);
                    }
                    break;
                }
            }
        }
    }

    // 车：直线不限步数
    template<Color C>
    void Position::gen_che_moves(Move* ml, int& idx, Square from) const {
        gen_sliding_moves<C>(ml, idx, from, rook_dirs, 4);
    }

    // 督：横竖斜不限步数（国际象棋后）
    template<Color C>
    void Position::gen_du_moves(Move* ml, int& idx, Square from) const {
        gen_sliding_moves<C>(ml, idx, from , queen_dirs, 8  );
    }

    // 将：九宫走一格
    template<Color C>
    void Position::gen_jiang_moves(Move* ml, int& idx, Square from) const {
        int x = square_x(from), y = square_y(from);
        Piece p = board[from];
        // 8个方向走一格，但必须在九宫内
        for (int dx : {-1, 0, 1}) {
            for (int dy : {-1, 0, 1}) {
                if (dx == 0 && dy == 0) continue;
                // 将不能斜走！！中国象棋将只能横竖走一格！
                if (dx != 0 && dy != 0) continue;
                int nx = x + dx, ny = y + dy;
                if (!is_valid_square(nx, ny)) continue;
                if (!is_in_palace(C, nx, ny)) continue;
                Square to = make_square(nx, ny);
                Piece target = board[to];
                if (target == EMPTY || piece_color(target) != C) {
                    ml[idx++] = make_move(from, to, target, p);
                }
            }
        }
        // 注意：与界面规则一致，将/帅只能九宫内横竖一格走，
        // 不能"飞将"直线吃对方将（"白脸将"仅作为将军判定使用，
        // 走成照面会因 is_legal 检查而被判非法）
    }

    int Position::generate_pseudolegal_moves(Move* movelist) const {
        int idx = 0;
        for (Square s = 0; s < BOARD_SIZE; s++) {
            Piece p = board[s];
            if (p == EMPTY || piece_color(p) != stm) continue;
            PieceType pt = piece_type(p);

            if (stm == RED) {
                switch (pt) {
                case BING:  gen_bing_moves<RED>(movelist, idx, s); break;
                case JUN:   gen_jun_moves<RED>(movelist, idx, s); break;
                case XIANG: gen_xiang_moves<RED>(movelist, idx, s); break;
                case SHI:   gen_shi_moves<RED>(movelist, idx, s); break;
                case MA:    gen_ma_moves<RED>(movelist, idx, s); break;
                case PAO:   gen_pao_moves<RED>(movelist, idx, s); break;
                case HOU:   gen_hou_moves<RED>(movelist, idx, s); break;
                case CHE:   gen_che_moves<RED>(movelist, idx, s); break;
                case DU:    gen_du_moves<RED>(movelist, idx, s); break;
                case JIANG: gen_jiang_moves<RED>(movelist, idx, s); break;
                default: break;
                }
            }
            else {
                switch (pt) {
                case BING:  gen_bing_moves<BLACK>   (movelist, idx, s); break;
                case JUN:   gen_jun_moves<BLACK>(movelist, idx, s); break;
                case XIANG: gen_xiang_moves<BLACK>(movelist, idx, s); break;
                case SHI:   gen_shi_moves<BLACK>(movelist, idx, s); break;
                case MA:    gen_ma_moves<BLACK>(movelist, idx, s); break;
                case PAO:   gen_pao_moves<BLACK>(movelist, idx, s); break;
                case HOU:   gen_hou_moves<BLACK>(movelist, idx, s); break;
                case CHE:   gen_che_moves<BLACK>(movelist, idx, s); break;
                case DU:    gen_du_moves<BLACK>(movelist, idx, s); break;
                case JIANG: gen_jiang_moves<BLACK>(movelist, idx, s); break;
                default: break;
                }
            }
        }
        return idx;
    }


    bool Position::legal_after_move(Move m) const {
        // 走一步后看自己是否被将军（假设 m 已是伪合法着法）
        // （在 const 副本语义下通过 const_cast 临时修改并恢复，避免拷贝整个棋盘）
        Square from = move_from(m);
        Square to = move_to(m);
        Piece p = board[from];
        Piece captured = board[to];

        const_cast<Position*>(this)->board[from] = EMPTY;
        const_cast<Position*>(this)->board[to] = p;

        Square old_king = king_sq[stm];
        if (piece_type(p) == JIANG) {
            const_cast<Position*>(this)->king_sq[stm] = to;
        }

        bool legal = !is_square_attacked(king_sq[stm], ~stm);

        // 恢复
        const_cast<Position*>(this)->board[to] = captured;
        const_cast<Position*>(this)->board[from] = p;
        const_cast<Position*>(this)->king_sq[stm] = old_king;

        return legal;
    }

    bool Position::is_legal(Move m) const {
        // 严格校验（外部历史/网络着法必须用）：
        if (m == MOVE_NONE) return false;
        Square from = move_from(m);
        Square to = move_to(m);
        // 1) 来源格必须有当前走方的棋子
        Piece p = board[from];
        if (p == EMPTY || piece_color(p) != stm) return false;
        if (to == from) return false;
        // 2) m 必须是该棋子在当前局面真实可达的伪合法着法（关键：防瞬移到走不到的格）
        {
            Move pseudo[MAX_MOVES];
            int cnt = generate_pseudolegal_moves(pseudo);
            bool reachable = false;
            for (int i = 0; i < cnt; i++)
                if (pseudo[i] == m) { reachable = true; break; }
            if (!reachable) return false;
        }
        // 3) 走完后将/帅不被攻击
        return legal_after_move(m);
    }

    int Position::generate_legal_moves(Move* movelist) const {
        Move temp[MAX_MOVES];
        int count = generate_pseudolegal_moves(temp);
        int legal_count = 0;
        for (int i = 0; i < count; i++) {
            if (legal_after_move(temp[i])) {
                movelist[legal_count++] = temp[i];
            }
        }
        return legal_count;
    }

    // perft：走法生成正确性验证（Fairy-Stockfish 调试标配）
    uint64_t Position::perft(int depth) const {
        Move temp[MAX_MOVES];
        int count = generate_legal_moves(temp);
        if (depth <= 1) return (uint64_t)count;

        uint64_t nodes = 0;
        Position copy(*this); // 在副本上递归，避免修改当前局面
        for (int i = 0; i < count; i++) {
            copy.do_move(temp[i]);
            nodes += copy.perft(depth - 1);
            copy.undo_move();
        }
        return nodes;
    }

    bool Position::do_move(Move m) {
        Square from = move_from(m);
        Square to = move_to(m);
        if (!is_valid_square(from) || !is_valid_square(to)) return false;
        Piece p = board[from];
        if (p == EMPTY || piece_color(p) != stm) return false;
        Piece captured = board[to];

        // 保存历史
        MoveInfo mi;
        mi.move = m;
        mi.captured = captured;
        mi.hash = zobrist_hash;
        mi.fifty_move = fifty_move;
        history.push(mi);

        // 移走棋子
        board[from] = EMPTY;
        zobrist_hash ^= Zobrist::psq[stm][piece_type(p)][from];

        // 吃子
        if (captured != EMPTY) {
            zobrist_hash ^= Zobrist::psq[~stm][piece_type(captured)][to];
            fifty_move = 0;
        }
        else {
            fifty_move++;
        }

        // 放置棋子
        board[to] = p;
        zobrist_hash ^= Zobrist::psq[stm][piece_type(p)][to];

        // 更新将/帅位置
        if (piece_type(p) == JIANG) {
            king_sq[stm] = to;
        }
        // 吃将（胜负已分）
        if (captured != EMPTY && piece_type(captured) == JIANG) {
            king_sq[~stm] = NO_SQUARE;
        }

        // 交换走方
        stm = ~stm;
        zobrist_hash ^= Zobrist::side;

        return true;
    }

    void Position::undo_move() {
        if (history.empty()) return;
        MoveInfo mi = history.top();
        history.pop();

        Move m = mi.move;
        Square from = move_from(m);
        Square to = move_to(m);
        Piece p = board[to];
        Piece captured = mi.captured;

        // 还原走方
        stm = ~stm;
        zobrist_hash = mi.hash;
        fifty_move = mi.fifty_move;

        // 移回棋子
        board[to] = captured;
        board[from] = p;

        // 更新将/帅位置
        if (piece_type(p) == JIANG) {
            king_sq[stm] = from;
        }
        if (captured != EMPTY && piece_type(captured) == JIANG) {
            king_sq[~stm] = to;
        }
    }

    // 从外部棋盘数组设置局面：重算将/帅位置、走方与哈希
    void Position::set_board_state(const Piece boardArr[BOARD_SIZE], Color sideToMove) {
        while (!history.empty()) history.pop();
        memcpy(board, boardArr, sizeof(board));
        stm = sideToMove;
        fifty_move = 0;

        king_sq[RED] = NO_SQUARE;
        king_sq[BLACK] = NO_SQUARE;
        for (int s = 0; s < BOARD_SIZE; s++) {
            Piece p = board[s];
            if (p != EMPTY && piece_type(p) == JIANG) {
                king_sq[piece_color(p)] = s;
            }
        }

        zobrist_hash = 0;
        for (int s = 0; s < BOARD_SIZE; s++) {
            Piece p = board[s];
            if (p != EMPTY) {
                zobrist_hash ^= Zobrist::psq[piece_color(p)][piece_type(p)][s];
            }
        }
        // 走子方哈希位：与 do_move/undo_move 的约定一致（黑方走子时置位）
        if (stm == BLACK) zobrist_hash ^= Zobrist::side;
    }

    // 空着走法：不移动棋子，仅交换走方（Fairy-Stockfish 空着裁剪的标准做法）
    void Position::do_null_move() {
        stm = ~stm;
        zobrist_hash ^= Zobrist::side;
        // 注意：不压入 history 栈，必须与 undo_null_move 成对使用
    }

    void Position::undo_null_move() {
        stm = ~stm;
        zobrist_hash ^= Zobrist::side;
    }

    void Position::set_from_fen(const std::string& fen) {
        // 简化版FEN解析，如需可自行扩展完整FEN支持
        set_initial_position();
    }

} // namespace zschess
