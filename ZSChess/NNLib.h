// NNLib.h
// 按 AI 等级区分的 NNUE 风格训练库：
//   - 6 个等级各自一套网络（嵌入累积 + 微型网络），二进制文件 nn_lib/tier{i}.bin
//   - 稀疏特征：每子 (颜色,类型,格) → 128 维嵌入列；每局面 ~22 个活跃特征
//   - 查询着法：玩家对局优先特级网络 1-ply 贪心；训练对局用该方等级网络；
//     网络未成熟（样本不足 NN_MIN_QUERY_SAMPLES）返回 MOVE_NONE 由引擎兜底
//   - 在线学习：每步样本（自动镜像 ×4）喂给对应等级网络，批量 SGD，定期落盘
#pragma once
#include <vector>
#include <string>
#include <mutex>
#include <cstdint>
#include "NNModel.h"
#include "types.h"
#include "position.h"

namespace zschess {

// 启用网络查询的最低样本数（网络成熟后才优先于引擎）
constexpr int NN_MIN_QUERY_SAMPLES = 2000;
// 训练缓冲大小（达到后批量梯度下降）
constexpr int NN_BATCH_SIZE = 64;
// 网络文件目录（相对 exe 工作目录）
constexpr const char* NN_LIB_DIR = "nn_lib";

class NNLib {
public:
    // 首次访问时自动加载全部已有网络权重（nn_lib/tier*.bin）
    static NNLib& instance() {
        static NNLib* p = nullptr;
        static std::mutex m;
        if (!p) {
            std::lock_guard<std::mutex> lk(m);
            if (!p) {
                p = new NNLib();
                p->LoadAll();
            }
        }
        return *p;
    }

    // 构建局面活跃特征（每方最多 32 个）+ 王位编码
    // actRed/actBlack 缓冲建议 >=64；nRed/nBlack 为输出数量
    static void BuildFeatures(const Position& pos, int* actRed, int& nRed,
                              int* actBlack, int& nBlack,
                              float kingFeat[NN_KING_FEAT]);

    // 特征索引（颜色, 类型 1..10, 格 x,y）
    static int FeatureIndex(Color c, PieceType pt, int x, int y);

    // 网络评估（红方视角真实分值；未加载/未成熟返回 0 并置 ok=false）
    bool Evaluate(int tier, const Position& pos, float& score, bool& ok);

    // 查询着法：
    //   myTier   = 当前 AI 等级
    //   preferTop= true 时先查特级（玩家对局）；false 时只查本级（训练对局）
    //   score    = 输出该着法的评估分（红方视角）
    // 未命中返回 MOVE_NONE
    Move PickMove(const Position& pos, int myTier, bool preferTop, float& score);

    // 在线学习：把 (局面, 红方视角分值) 加入 tier 网络训练缓冲（自动镜像 ×4）
    void TrainSample(int tier, const Position& pos, float redScore);
    // 用缓冲样本批量训练一次（可多轮）；返回该网络累计样本数
    int FlushTrain(int tier, int epochs = 1);
    // 保存/加载
    bool Save(int tier);
    bool SaveAll();
    bool Load(int tier);
    bool LoadAll();

    // 统计信息（UI 状态条用）
    int Samples(int tier) const;
    int TotalSamples() const;
    bool FileExists(int tier) const;

private:
    NNLib();
    NNModel models_[6];

    // 待训练样本（镜像展开后存特征列表，训练时即时前向/反传）
    struct PendingSample {
        std::vector<int> actRed, actBlack;
        float kingFeat[NN_KING_FEAT];
        float target;
    };
    std::vector<PendingSample> pending_[6];
    mutable std::mutex mu_;
    std::string libDir_;

    std::string Path(int tier) const;
    // 单网络 1-ply 贪心（bs=走棋方视角，sBest/sOfBest=红方视角）
    Move Greedy1Ply(const Position& pos, const NNModel& m, float& bestScore, int& staticBest, int& staticOfBest) const;
    // 学习率按累计样本衰减（防后期震荡）
    float DecayLr(int tier);
};

} // namespace zschess
