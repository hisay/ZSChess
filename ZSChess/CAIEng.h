#pragma once
#include "ai/src/position.h"
#include "ai/src/search.h"
#include "ai/src/variant.h"
#include "AI/src/thread.h"
using namespace Stockfish;

class CustomXiangqiEngine {
private:
    Position pos;
    Search::Stack searchState;
    // 用于存储引擎计算出的最佳走法
    Move bestMove;

public:
    CustomXiangqiEngine() {
        // 初始化引擎，加载你自定义的象棋变体规则
        // 假设你已经在 variant.cpp 中注册了名为 "my_xiangqi" 的变体
       // const Variant* v = Variant::ZSInit();
       // pos.set().set(v, false); // 设置初始局面
   
    }

    //// 设置局面（例如通过 FEN 串）
    //void setPosition(const std::string& fen) {
    //    Stockfish::StateInfo si;
    //    
    //    //pos.set(fen, false, Variant::ZSInit());
    //}

    //// 执行搜索并获取最佳走法
    //std::string getBestMove(int depth = 20, int moveTime = 1000) {
    //    // 调用底层搜索接口，这里简化了 Search::think 的参数
    //    // 实际使用时需根据你的需求配置 LimitsType (时间、深度等)
    //    Search::LimitsType limits;
    //    limits.depth = depth;
    //    limits.movetime = moveTime;

    //    // 启动搜索线程
    //    Threads.start_thinking(pos, searchState, limits);
    //    Threads.wait_for_search_finished();

    //    // 获取搜索结果中的最佳走法
    //    bestMove = Threads.main()->bestMove;
    //    return UCI::move(bestMove, pos.is_chess960());
    //}

    //// 执行一步走法，更新内部局面
    //void makeMove(const std::string& moveStr) {
    //    Move m = UCI::to_move(pos, moveStr);
    //    if (m != MOVE_NONE) {
    //        pos.do_move(m, searchState.st);
    //    }
    //}
};