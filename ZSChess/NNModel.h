// NNModel.h
// NNUE 风格神经网络（Fairy-Stockfish 设计哲学落地中世象棋）：
//   稀疏特征 + 嵌入累积器 + 微型网络（ClippedReLU）
// 结构：
//   特征：每方每子 1 个特征（颜色, 类型, 格）→ 128 维嵌入列（稀疏激活，每局面 ~22 个）
//   累积器：bias + Σ 活跃特征嵌入列（走子后增量更新，仅移动/被吃子列变化）
//   王位：红帅/黑将 (行,列) 各 4+4 one-hot，共 16 维，concat 到累积器后
//   网络：Input(128+16=144) → Affine16(ClippedReLU) → Affine32(ClippedReLU) → Affine1
// 输出：红方视角评估分（NN_SCALE 归一化）
#pragma once
#include <vector>
#include <string>
#include <cstdint>

namespace zschess {

// 特征：2 色 × 10 类型 × 121 格
constexpr int NN_FEATURE_NB = 2 * 10 * 121;
// 嵌入维（累积器维数，每特征一列）
constexpr int NN_EMBED = 128;
// 王位显式编码维（红帅行4+列4 + 黑将行4+列4）
constexpr int NN_KING_FEAT = 16;
// 网络输入维 = 双方累积 concat + 王位
constexpr int NN_INPUT = NN_EMBED + NN_EMBED + NN_KING_FEAT;
// 微型隐藏层（Fairy 风格：窄而快）
constexpr int NN_HID1 = 16;
constexpr int NN_HID2 = 32;
// 分值缩放：网络输出 = 真实分值 / NN_SCALE（红方视角）
constexpr float NN_SCALE = 8000.0f;

// 累积器：双方视角各 NN_EMBED 维（走子后增量更新）
struct NNAccumulator {
    float v[2][NN_EMBED];
    bool computed[2];
};

class NNModel {
public:
    NNModel() { Init(); }

    // 随机初始化（嵌入小随机 + He 缩放）
    void Init();

    // 小网络前向：acc[2][EMBED] + kingFeat[NN_KING_FEAT] → 红方视角归一化分值
    // stm 指定当前走方（输出按走棋方视角由调用方转换）
    float Forward(const float acc[2][NN_EMBED], const float kingFeat[NN_KING_FEAT]) const;

    // 全量累积：bias + Σ 活跃特征列（用于根局面/王移动后刷新）
    // active 为特征索引数组（len 个），视角 p（0=红视角,1=黑视角）
    void AccumulateFull(int p, const int* active, int len, float out[NN_EMBED]) const;

    // 批量训练（SGD）：每样本 (active 列表×2 视角, kingFeat, target 归一化红方视角分值)
    // 训练通过嵌入列与小网络反向传播；lr 可传入衰减值
    void TrainSample(const int* actRed, int nRed, const int* actBlack, int nBlack,
                     const float* kingFeat, float target, float lr);

    // 二进制保存/加载（ZSNN03 格式：含结构元数据，结构不匹配拒绝加载）
    bool Save(const std::string& path) const;
    bool Load(const std::string& path);

    int Samples() const { return samples_; }
    void SetSamples(int n) { samples_ = n; }

    // 权重总大小（文件头校验）
    static size_t WeightCount();

private:
    // 嵌入表 Embed[NN_FEATURE_NB][NN_EMBED]（行主序）
    std::vector<float> embed_;
    // 累积器偏置（每视角一份，初始相同）
    float bias_[NN_EMBED];
    // 小网络：W1[HID1][INPUT], b1[HID1], W2[HID2][HID1], b2[HID2], W3[HID2], b3
    std::vector<float> W1_, b1_, W2_, b2_, W3_, b3_;
    int samples_ = 0;
};

} // namespace zschess
