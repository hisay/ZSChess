// NNModel.cpp
// NNUE 风格实现：稀疏特征嵌入 + 累积器 + 微型网络（ClippedReLU），SGD 在线训练。
#include "NNModel.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <algorithm>

namespace zschess {

static inline float clip01(float v, float hi) { return v < 0.f ? 0.f : (v > hi ? hi : v); }

void NNModel::Init() {
    std::mt19937 rng(0x5EED);
    std::normal_distribution<float> nd(0.f, 0.1f);
    embed_.resize((size_t)NN_FEATURE_NB * NN_EMBED);
    for (size_t i = 0; i < embed_.size(); i++)
        embed_[i] = nd(rng) * 0.5f / (float)std::sqrt((double)NN_EMBED);
    for (int i = 0; i < NN_EMBED; i++) bias_[i] = 0.f;
    W1_.resize((size_t)NN_HID1 * NN_INPUT);
    b1_.assign(NN_HID1, 0.f);
    W2_.resize((size_t)NN_HID2 * NN_HID1);
    b2_.assign(NN_HID2, 0.f);
    W3_.assign(NN_HID2, 0.f);
    b3_.assign(1, 0.f);
    for (size_t i = 0; i < W1_.size(); i++) W1_[i] = nd(rng) * 0.5f / (float)std::sqrt((double)NN_INPUT);
    for (size_t i = 0; i < W2_.size(); i++) W2_[i] = nd(rng) * 0.5f / (float)std::sqrt((double)NN_HID1);
    for (int i = 0; i < NN_HID2; i++) W3_[i] = nd(rng) * 0.1f;
    samples_ = 0;
}

float NNModel::Forward(const float acc[2][NN_EMBED], const float kingFeat[NN_KING_FEAT]) const {
    thread_local std::vector<float> in(NN_INPUT);
    for (int i = 0; i < NN_EMBED; i++) in[i] = acc[0][i];
    for (int i = 0; i < NN_EMBED; i++) in[NN_EMBED + i] = acc[1][i];
    for (int i = 0; i < NN_KING_FEAT; i++) in[2 * NN_EMBED + i] = kingFeat[i];

    thread_local std::vector<float> h1(NN_HID1), h2(NN_HID2);
    for (int j = 0; j < NN_HID1; j++) {
        float s = b1_[j];
        const float* w = &W1_[(size_t)j * NN_INPUT];
        for (int i = 0; i < NN_INPUT; i++) s += w[i] * in[i];
        h1[j] = clip01(s, 127.f);
    }
    for (int j = 0; j < NN_HID2; j++) {
        float s = b2_[j];
        const float* w = &W2_[(size_t)j * NN_HID1];
        for (int i = 0; i < NN_HID1; i++) s += w[i] * h1[i];
        h2[j] = clip01(s, 127.f);
    }
    float s = b3_[0];
    for (int i = 0; i < NN_HID2; i++) s += W3_[i] * h2[i];
    return s;
}

void NNModel::AccumulateFull(int p, const int* active, int len, float out[NN_EMBED]) const {
    for (int i = 0; i < NN_EMBED; i++) out[i] = bias_[i];
    for (int k = 0; k < len; k++) {
        int f = active[k];
        if (f < 0 || f >= NN_FEATURE_NB) continue;
        const float* col = &embed_[(size_t)f * NN_EMBED];
        for (int i = 0; i < NN_EMBED; i++) out[i] += col[i];
    }
}

void NNModel::TrainSample(const int* actRed, int nRed, const int* actBlack, int nBlack,
                          const float* kingFeat, float target, float lr) {
    // ---- 前向（含中间保存）----
    std::vector<float> accRed(NN_EMBED), accBlack(NN_EMBED);
    for (int i = 0; i < NN_EMBED; i++) { accRed[i] = bias_[i]; accBlack[i] = bias_[i]; }
    for (int k = 0; k < nRed; k++) {
        int f = actRed[k];
        if (f < 0 || f >= NN_FEATURE_NB) continue;
        const float* col = &embed_[(size_t)f * NN_EMBED];
        for (int i = 0; i < NN_EMBED; i++) accRed[i] += col[i];
    }
    for (int k = 0; k < nBlack; k++) {
        int f = actBlack[k];
        if (f < 0 || f >= NN_FEATURE_NB) continue;
        const float* col = &embed_[(size_t)f * NN_EMBED];
        for (int i = 0; i < NN_EMBED; i++) accBlack[i] += col[i];
    }
    std::vector<float> in(NN_INPUT), z1(NN_HID1), h1(NN_HID1), z2(NN_HID2), h2(NN_HID2);
    for (int i = 0; i < NN_EMBED; i++) in[i] = accRed[i];
    for (int i = 0; i < NN_EMBED; i++) in[NN_EMBED + i] = accBlack[i];
    for (int i = 0; i < NN_KING_FEAT; i++) in[2 * NN_EMBED + i] = kingFeat[i];

    for (int j = 0; j < NN_HID1; j++) {
        float s = b1_[j];
        const float* w = &W1_[(size_t)j * NN_INPUT];
        for (int i = 0; i < NN_INPUT; i++) s += w[i] * in[i];
        z1[j] = s; h1[j] = clip01(s, 127.f);
    }
    for (int j = 0; j < NN_HID2; j++) {
        float s = b2_[j];
        const float* w = &W2_[(size_t)j * NN_HID1];
        for (int i = 0; i < NN_HID1; i++) s += w[i] * h1[i];
        z2[j] = s; h2[j] = clip01(s, 127.f);
    }
    float out = b3_[0];
    for (int i = 0; i < NN_HID2; i++) out += W3_[i] * h2[i];

    float err = (out - target);
    float g3 = 2.f * err;

    // ---- 输出层 ----
    for (int i = 0; i < NN_HID2; i++) W3_[i] -= lr * g3 * h2[i];
    b3_[0] -= lr * g3;

    // ---- 隐藏层 2 ----
    std::vector<float> g2(NN_HID2);
    for (int j = 0; j < NN_HID2; j++) {
        float g = g3 * W3_[j] * ((z2[j] > 0.f && z2[j] < 127.f) ? 1.f : 0.f);
        g2[j] = g;
        b2_[j] -= lr * g;
    }
    for (int j = 0; j < NN_HID2; j++) {
        float g = g2[j];
        if (g == 0.f) continue;
        float* w = &W2_[(size_t)j * NN_HID1];
        for (int i = 0; i < NN_HID1; i++) w[i] -= lr * g * h1[i];
    }

    // ---- 隐藏层 1 ----
    std::vector<float> g1(NN_HID1);
    for (int i = 0; i < NN_HID1; i++) {
        float g = 0.f;
        for (int j = 0; j < NN_HID2; j++) g += g2[j] * W2_[(size_t)j * NN_HID1 + i];
        g *= ((z1[i] > 0.f && z1[i] < 127.f) ? 1.f : 0.f);
        g1[i] = g;
        b1_[i] -= lr * g;
    }
    // 输入层梯度（含嵌入列梯度来源）
    std::vector<float> gIn(NN_INPUT, 0.f);
    for (int i = 0; i < NN_HID1; i++) {
        float g = g1[i];
        if (g == 0.f) continue;
        float* w = &W1_[(size_t)i * NN_INPUT];
        for (int k = 0; k < NN_INPUT; k++) {
            w[k] -= lr * g * in[k];
            gIn[k] += g * (w[k] + lr * g * in[k]); // 更新后权重近似 = 原权重；此处用当前 w（已减）近似梯度传递
        }
    }

    // ---- 嵌入列梯度 ----
    // in[0..128) ← 红累积；in[128..256) ← 黑累积
    for (int k = 0; k < nRed; k++) {
        int f = actRed[k];
        if (f < 0 || f >= NN_FEATURE_NB) continue;
        float* col = &embed_[(size_t)f * NN_EMBED];
        for (int i = 0; i < NN_EMBED; i++) col[i] -= lr * gIn[i];
    }
    for (int k = 0; k < nBlack; k++) {
        int f = actBlack[k];
        if (f < 0 || f >= NN_FEATURE_NB) continue;
        float* col = &embed_[(size_t)f * NN_EMBED];
        for (int i = 0; i < NN_EMBED; i++) col[i] -= lr * gIn[NN_EMBED + i];
    }

    samples_++;
}

size_t NNModel::WeightCount() {
    return (size_t)NN_FEATURE_NB * NN_EMBED + NN_EMBED
         + (size_t)NN_INPUT * NN_HID1 + NN_HID1
         + (size_t)NN_HID1 * NN_HID2 + NN_HID2
         + NN_HID2 + 1;
}

bool NNModel::Save(const std::string& path) const {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    char magic[8] = { 'Z','S','N','N','0','3','\0','\0' };
    fwrite(magic, 1, 8, f);
    int samples = samples_;
    fwrite(&samples, sizeof(int), 1, f);
    int featNb = NN_FEATURE_NB, embed = NN_EMBED, kf = NN_KING_FEAT;
    int in = NN_INPUT, h1 = NN_HID1, h2 = NN_HID2;
    fwrite(&featNb, sizeof(int), 1, f);
    fwrite(&embed, sizeof(int), 1, f);
    fwrite(&kf, sizeof(int), 1, f);
    fwrite(&in, sizeof(int), 1, f);
    fwrite(&h1, sizeof(int), 1, f);
    fwrite(&h2, sizeof(int), 1, f);
    size_t wc = WeightCount();
    uint64_t wc64 = (uint64_t)wc;
    fwrite(&wc64, sizeof(uint64_t), 1, f);
    auto wr = [&](const std::vector<float>& v) { fwrite(v.data(), sizeof(float), v.size(), f); };
    wr(embed_);
    fwrite(bias_, sizeof(float), NN_EMBED, f);
    wr(W1_); wr(b1_); wr(W2_); wr(b2_); wr(W3_); wr(b3_);
    fclose(f);
    return true;
}

bool NNModel::Load(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char magic[8];
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "ZSNN03", 6) != 0) {
        fclose(f); return false;
    }
    int samples, featNb, embed, kf, in, h1, h2; uint64_t wc;
    if (fread(&samples, sizeof(int), 1, f) != 1 ||
        fread(&featNb, sizeof(int), 1, f) != 1 ||
        fread(&embed, sizeof(int), 1, f) != 1 ||
        fread(&kf, sizeof(int), 1, f) != 1 ||
        fread(&in, sizeof(int), 1, f) != 1 ||
        fread(&h1, sizeof(int), 1, f) != 1 ||
        fread(&h2, sizeof(int), 1, f) != 1 ||
        fread(&wc, sizeof(uint64_t), 1, f) != 1) { fclose(f); return false; }
    if (featNb != NN_FEATURE_NB || embed != NN_EMBED || kf != NN_KING_FEAT ||
        in != NN_INPUT || h1 != NN_HID1 || h2 != NN_HID2 || wc != WeightCount()) {
        fclose(f); return false;
    }
    auto rd = [&](std::vector<float>& v, size_t n) {
        v.resize(n);
        return fread(v.data(), sizeof(float), n, f) == n;
    };
    bool ok = rd(embed_, (size_t)NN_FEATURE_NB * NN_EMBED);
    if (ok) ok = fread(bias_, sizeof(float), NN_EMBED, f) == NN_EMBED;
    ok = ok && rd(W1_, (size_t)NN_INPUT * NN_HID1) &&
         rd(b1_, NN_HID1) &&
         rd(W2_, (size_t)NN_HID1 * NN_HID2) &&
         rd(b2_, NN_HID2) &&
         rd(W3_, NN_HID2) &&
         rd(b3_, 1);
    fclose(f);
    if (!ok) return false;
    samples_ = samples;
    return true;
}

} // namespace zschess
