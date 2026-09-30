// NNLib.cpp
#include "NNLib.h"
#include "evaluate.h"
#include "search.h"
#include <cstdio>
#include <cstring>
#include <direct.h>
#include <sys/stat.h>
#include <windows.h>

namespace zschess {

static inline bool NNFileExists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

// 行/列粗分桶（王位 4 位 one-hot 编码用）
static inline int bucket4(int v) { return v < 3 ? 0 : v < 6 ? 1 : v < 9 ? 2 : 3; }

NNLib::NNLib() {
    char buf[512];
    GetCurrentDirectoryA(512, buf);
    libDir_ = std::string(buf) + "\\" + NN_LIB_DIR;
}

std::string NNLib::Path(int tier) const {
    char buf[64];
    snprintf(buf, sizeof(buf), "\\tier%d.bin", tier);
    return libDir_ + buf;
}

int NNLib::FeatureIndex(Color c, PieceType pt, int x, int y) {
    int t = (int)pt;
    if (t < 1 || t > 10) return -1;
    if (x < 0 || x > 10 || y < 0 || y > 10) return -1;
    return (int)c * 1210 + (t - 1) * 121 + (y * 11 + x);
}

void NNLib::BuildFeatures(const Position& pos, int* actRed, int& nRed,
                          int* actBlack, int& nBlack,
                          float kingFeat[NN_KING_FEAT]) {
    nRed = 0; nBlack = 0;
    for (int y = 0; y < BOARD_HEIGHT; y++) {
        for (int x = 0; x < BOARD_WIDTH; x++) {
            Piece p = pos.piece_on(x, y);
            if (p == EMPTY) continue;
            PieceType pt = piece_type(p);
            Color c = piece_color(p);
            if (c == RED) {
                int f = FeatureIndex(RED, pt, x, y);
                if (f >= 0 && nRed < 64) actRed[nRed++] = f;
            } else {
                int f = FeatureIndex(BLACK, pt, x, y);
                if (f >= 0 && nBlack < 64) actBlack[nBlack++] = f;
            }
        }
    }
    // 王位显式编码：行 4 桶 + 列 4 桶（红帅、黑将各 8 位）
    for (int i = 0; i < NN_KING_FEAT; i++) kingFeat[i] = 0.f;
    Square rs = pos.king_square(RED);
    Square bs = pos.king_square(BLACK);
    if (rs >= 0 && rs < BOARD_SIZE) {
        kingFeat[bucket4(square_y(rs))] = 1.f;
        kingFeat[4 + bucket4(square_x(rs))] = 1.f;
    }
    if (bs >= 0 && bs < BOARD_SIZE) {
        kingFeat[8 + bucket4(square_y(bs))] = 1.f;
        kingFeat[12 + bucket4(square_x(bs))] = 1.f;
    }
}

float NNLib::DecayLr(int tier) {
    int s = models_[tier].Samples();
    return 0.05f / (1.0f + (float)s * 0.00002f);
}

bool NNLib::Evaluate(int tier, const Position& pos, float& score, bool& ok) {
    if (tier < 0) tier = 0;
    if (tier > 5) tier = 5;
    std::lock_guard<std::mutex> lk(mu_);
    if (models_[tier].Samples() < NN_MIN_QUERY_SAMPLES) { ok = false; score = 0; return false; }
    int actR[64], actB[64]; int nr = 0, nb = 0; float kf[NN_KING_FEAT];
    BuildFeatures(pos, actR, nr, actB, nb, kf);
    NNAccumulator acc;
    models_[tier].AccumulateFull(0, actR, nr, acc.v[0]);
    models_[tier].AccumulateFull(1, actB, nb, acc.v[1]);
    score = models_[tier].Forward(acc.v, kf) * NN_SCALE; // 红方视角
    ok = true;
    return true;
}

Move NNLib::Greedy1Ply(const Position& pos, const NNModel& m, float& bestScore, int& staticBest, int& staticOfBest) const {
    Move legal[256];
    int n = pos.generate_legal_moves(legal);
    if (n == 0) { bestScore = 0; staticBest = 0; staticOfBest = 0; return MOVE_NONE; }
    Move best = MOVE_NONE;
    float bestV = -1e30f;
    int bestS = -1000000000;
    int bestMoveStatic = -1000000000;
    Position p2 = pos;
    for (int i = 0; i < n; i++) {
        if (!p2.do_move(legal[i])) continue;
        // 吃子着法：SEE 拦截"吃大子却被更便宜的子回吃"的送吃
        if (move_captured(legal[i]) != EMPTY && see_value(pos, legal[i]) < -120) { p2.undo_move(); continue; }
        int actR[64], actB[64]; int nr = 0, nb = 0; float kf[NN_KING_FEAT];
        BuildFeatures(p2, actR, nr, actB, nb, kf);
        NNAccumulator acc;
        m.AccumulateFull(0, actR, nr, acc.v[0]);
        m.AccumulateFull(1, actB, nb, acc.v[1]);
        float v = m.Forward(acc.v, kf); // 网络输出 = 红方视角
        // 我方视角：走棋方是红 → 直接 v；黑 → -v
        float mine = (pos.side_to_move() == RED) ? v : -v;
        // 静态评估（红方视角）
        int es = Eval::evaluate(p2);                 // p2.stm（走子后对方）视角
        if (p2.side_to_move() == BLACK) es = -es;    // 转红方视角
        // 开局平局打破：网络分相近时用静态评估倾向出大子而非动将
        if (mine > bestV + 8.0f || (mine > bestV - 8.0f && es > bestMoveStatic)) {
            bestV = mine; best = legal[i]; bestMoveStatic = es;
        }
        if (es > bestS) bestS = es;
        p2.undo_move();
    }
    bestScore = bestV;
    staticBest = bestS;
    staticOfBest = bestMoveStatic;
    return best;
}

Move NNLib::PickMove(const Position& pos, int myTier, bool preferTop, float& score) {
    if (myTier < 0) myTier = 0;
    if (myTier > 5) myTier = 5;
    std::lock_guard<std::mutex> lk(mu_);

    int order[6];
    int cnt = 0;
    if (preferTop && myTier != 5) order[cnt++] = 5;   // 玩家对局：特级最优先
    order[cnt++] = myTier;
    for (int t = 5; t >= 0; t--) {
        bool dup = false;
        for (int i = 0; i < cnt; i++) if (order[i] == t) dup = true;
        if (!dup) order[cnt++] = t;
    }

    for (int i = 0; i < cnt; i++) {
        int t = order[i];
        if (models_[t].Samples() < NN_MIN_QUERY_SAMPLES) continue;
        float bs = 0;
        int sBest = 0, sOfBest = 0;
        Move m = Greedy1Ply(pos, models_[t], bs, sBest, sOfBest);
        if (m != MOVE_NONE) {
            // 双保险门槛：bs 是走棋方视角，sBest/sOfBest 是红方视角 → bs 转红方视角再比较
            float bsRed = (pos.side_to_move() == RED) ? bs : -bs;
            if (bsRed >= sBest - 150.0f && sOfBest >= sBest - 150) { score = bs; return m; }
        }
    }
    score = 0;
    return MOVE_NONE;
}

// 构造对称镜像棋盘（左右镜像 + 可选红黑翻转），供训练样本扩增
static void BuildMirrorBoard(const Position& pos, bool flipColor, Piece out[BOARD_SIZE]) {
    for (int y = 0; y < BOARD_HEIGHT; y++) {
        for (int x = 0; x < BOARD_WIDTH; x++) {
            int nx = BOARD_WIDTH - 1 - x;                    // 左右镜像
            int ny = flipColor ? (BOARD_HEIGHT - 1 - y) : y; // 红黑翻转：上下对调
            Piece p = pos.piece_on(x, y);
            if (p == EMPTY) { out[ny * BOARD_WIDTH + nx] = EMPTY; }
            else out[ny * BOARD_WIDTH + nx] = make_piece(flipColor ? ~piece_color(p) : piece_color(p), piece_type(p));
        }
    }
}

void NNLib::TrainSample(int tier, const Position& pos, float redScore) {
    if (tier < 0) tier = 0;
    if (tier > 5) tier = 5;
    std::lock_guard<std::mutex> lk(mu_);
    // 镜像扩增 ×4：原局面、左右镜像、红黑翻转、双镜像（目标分同步变换）
    Piece mirrored[BOARD_SIZE];
    Color stm = pos.side_to_move();
    int ar[64], ab[64]; int nr = 0, nb = 0; float kf[NN_KING_FEAT];
    for (int v = 0; v < 4; v++) {
        PendingSample ps;
        if (v == 0) {
            BuildFeatures(pos, ar, nr, ab, nb, kf);
            ps.target = redScore;
        } else {
            bool flipColor = (v == 2 || v == 3);
            BuildMirrorBoard(pos, flipColor, mirrored);
            Position mp;
            mp.set_board_state(mirrored, flipColor ? ~stm : stm);
            BuildFeatures(mp, ar, nr, ab, nb, kf);
            ps.target = flipColor ? -redScore : redScore; // 红黑翻转：红方视角 → 黑方视角
        }
        ps.actRed.assign(ar, ar + nr);
        ps.actBlack.assign(ab, ab + nb);
        for (int i = 0; i < NN_KING_FEAT; i++) ps.kingFeat[i] = kf[i];
        pending_[tier].push_back(std::move(ps));
    }
    if ((int)pending_[tier].size() >= NN_BATCH_SIZE) {
        float lr = DecayLr(tier);
        for (const auto& ps : pending_[tier])
            models_[tier].TrainSample(ps.actRed.data(), (int)ps.actRed.size(),
                                      ps.actBlack.data(), (int)ps.actBlack.size(),
                                      ps.kingFeat, ps.target / NN_SCALE, lr);
        pending_[tier].clear();
    }
}

int NNLib::FlushTrain(int tier, int epochs) {
    if (tier < 0) tier = 0;
    if (tier > 5) tier = 5;
    std::lock_guard<std::mutex> lk(mu_);
    int before = models_[tier].Samples();
    if (pending_[tier].empty()) return before;
    float lr = DecayLr(tier);
    for (int e = 0; e < epochs; e++) {
        for (const auto& ps : pending_[tier])
            models_[tier].TrainSample(ps.actRed.data(), (int)ps.actRed.size(),
                                      ps.actBlack.data(), (int)ps.actBlack.size(),
                                      ps.kingFeat, ps.target / NN_SCALE, lr);
    }
    pending_[tier].clear();
    return models_[tier].Samples();
}

bool NNLib::Save(int tier) {
    if (tier < 0 || tier > 5) return false;
    std::lock_guard<std::mutex> lk(mu_);
    if (_mkdir(libDir_.c_str()) != 0 && !NNFileExists(libDir_ + "\\")) {}
    return models_[tier].Save(Path(tier));
}

bool NNLib::SaveAll() {
    bool ok = true;
    for (int i = 0; i < 6; i++) if (!Save(i)) ok = false;
    return ok;
}

bool NNLib::Load(int tier) {
    if (tier < 0 || tier > 5) return false;
    std::lock_guard<std::mutex> lk(mu_);
    return models_[tier].Load(Path(tier));
}

bool NNLib::LoadAll() {
    bool ok = true;
    for (int i = 0; i < 6; i++) {
        if (NNFileExists(Path(i))) ok = Load(i) && ok;
    }
    return ok;
}

int NNLib::Samples(int tier) const {
    if (tier < 0 || tier > 5) return 0;
    std::lock_guard<std::mutex> lk(mu_);
    return models_[tier].Samples();
}

int NNLib::TotalSamples() const {
    std::lock_guard<std::mutex> lk(mu_);
    int s = 0;
    for (int i = 0; i < 6; i++) s += models_[i].Samples();
    return s;
}

bool NNLib::FileExists(int tier) const {
    if (tier < 0 || tier > 5) return false;
    return NNFileExists(Path(tier));
}

} // namespace zschess
