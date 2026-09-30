#ifndef ZSCHESS_POSITION_H
#define ZSCHESS_POSITION_H

#include "types.h"
#include <stack>
#include <string>
#include <vector>

namespace zschess {

    struct MoveInfo {
        Move move;
        Piece captured;
        uint64_t hash;
        int fifty_move;
    };

    class Position {
    public:
        Position();

        // 初始化初始局面
        void set_initial_position();

        // 从外部棋盘数组设置局面（用于引擎与界面/其他引擎对接）。
        // boardArr 为 BOARD_SIZE 个 Piece，sideToMove 为当前走方。
        // 内部自动重算将/帅位置、哈希与历史栈。
        void set_board_state(const Piece boardArr[BOARD_SIZE], Color sideToMove);

        // 从FEN字符串设置（简化版）
        void set_from_fen(const std::string& fen);

        // 生成所有合法走法
        int generate_legal_moves(Move* movelist) const;

        // 走子（返回false如果走法不合法）
        bool do_move(Move m);

        // 撤销上一步走法
        void undo_move();

        // 检查当前走方是否被将军
        bool in_check() const;

        // 空着走法（不移动棋子，仅交换走方；用于搜索的空着裁剪）
        // 注意：空着不压入 history 栈，必须与 undo_null_move 成对使用
        void do_null_move();
        void undo_null_move();

        // 检查某格是否为空
        bool is_empty(Square s) const { return board[s] == EMPTY; }

        // 检查走法是否为吃子走法
        bool is_capture(Move m) const { return move_captured(m) != EMPTY; }

        // 检查是否将杀/困毙
        bool is_checkmate() { return generate_legal_moves(temp_moves) == 0 && in_check(); }
        bool is_stalemate() { return generate_legal_moves(temp_moves) == 0 && !in_check(); }

        // 获取当前棋盘上某格棋子
        Piece piece_on(Square s) const { return board[s]; }
        Piece piece_on(int x, int y) const { return board[make_square(x, y)]; }

        // 当前走子方
        Color side_to_move() const { return stm; }

        // 当前局面哈希
        uint64_t hash() const { return zobrist_hash; }

        // 将/帅位置
        Square king_square(Color c) const { return king_sq[c]; }

        // 打印棋盘
        std::string to_string() const;

        // 走法转换为字符串
        static std::string move_to_string(Move m);

        // 从字符串解析走法（如"a7a6"）
        Move move_from_string(const std::string& str) const;

        // 走法生成计数（perft 调试用）：返回 depth 层内的叶子节点总数
        uint64_t perft(int depth) const;

        // 生成所有伪合法走法（不考虑走后自己被将军）
        int generate_pseudolegal_moves(Move* movelist) const;

        // 轻量合法性：假设 m 已是伪合法着法（来自着法生成器），
        // 仅检查走完后自己的将/帅是否被攻击。着法生成/搜索内部高频调用。
        bool legal_after_move(Move m) const;

        // 严格合法性（外部历史/网络着法必须用）：先验证 m 是当前局面
        // 该棋子真实可达的伪合法着法，再检查走完将不被攻击。
        // 防止 NN/PV 返回的历史着法在错位局面下把棋子"瞬移"到走不到的格子。
        bool is_legal(Move m) const;

        // 检查某方格是否被某方攻击
        bool is_square_attacked(Square s, Color by) const;

        // 检查某方格是否被某方"真实保护"（能吃到该格的子）。
        // 与 is_square_attacked 的区别：将/帅只算九宫内一格的真攻击，
        // 不算"白脸将"照面（王实际走不到远处同列子）；炮只算恰好一个
        // 炮架的吃子（无炮架的空线威慑不能保护己方子）。
        // Fairy-Stockfish 的 defended 语义：王仅保护邻格。
        bool is_square_protected(Square s, Color by) const;

    private:
        // 棋盘数组
        Piece board[BOARD_SIZE];

        // 当前走方
        Color stm;

        // 将/帅位置
        Square king_sq[2];

        // Zobrist哈希
        uint64_t zobrist_hash;

        // 五十步计数
        int fifty_move;

        // 历史走法栈
        std::stack<MoveInfo> history;

        // 临时走法数组
        mutable Move temp_moves[MAX_MOVES];

        // 走法生成辅助函数
        template<Color C> void gen_bing_moves(Move* ml, int& idx, Square from) const;
        template<Color C> void gen_jun_moves(Move* ml, int& idx, Square from) const;
        template<Color C> void gen_xiang_moves(Move* ml, int& idx, Square from) const;
        template<Color C> void gen_shi_moves(Move* ml, int& idx, Square from) const;
        template<Color C> void gen_ma_moves(Move* ml, int& idx, Square from) const;
        template<Color C> void gen_pao_moves(Move* ml, int& idx, Square from) const;
        template<Color C> void gen_hou_moves(Move* ml, int& idx, Square from) const;
        template<Color C> void gen_che_moves(Move* ml, int& idx, Square from) const;
        template<Color C> void gen_du_moves(Move* ml, int& idx, Square from) const;
        template<Color C> void gen_jiang_moves(Move* ml, int& idx, Square from) const;

        // 滑动棋子走法生成
// 滑动棋子走法生成：方向数组作为普通参数，兼容MSVC
        template<Color C>
        void gen_sliding_moves(Move* ml, int& idx, Square from, const Direction* dirs, int ndirs) const;

    };

} // namespace zschess

#endif // ZSCHESS_POSITION_H
