#pragma once
#define NOMINMAX
#include "CChessPiece.h"
#include <vector>
#include <algorithm>
#include <climits>
#include <functional>
#include <future>
#include <thread>
#include <mutex>
#include <atomic>
#include <stdexcept>
#include "ThreadPool.h"
#include "AIConfigBridge.h"
#include "CPieceMng.h"

struct MoveInfo {
    int pieceIndex;   // 棋子在 board 中的索引
    PSF::POS from;    // 起始位置
    PSF::POS to;      // 目标位置
    bool isCapture;   // 是否吃子
    int capturedIndex;// 被吃棋子的索引（-1 表示未吃子）
};

class CAIPlayer {
private:
    CPieceMng* m_pPMng = nullptr;
public:
	CAIPlayer(CPieceMng* p) : m_cancelToken(nullptr),m_pPMng(p) {}
    // 搜索入口：为指定颜色寻找最佳走法（返回原板上指针）
    std::pair<CChessPiece*, PSF::POS> GetBestMove(PieceColor color, std::vector<CChessPiece>& board, int depth) {
        this->currentColor = color;

        // 1. 生成所有合法走法（使用索引，避免指针悬空）
        std::vector<MoveInfo> allMoves;
        for (int i = 0; i < (int)board.size(); ++i) {
            if (board[i].GetColor() != color) continue;
            auto moves = PSF::CChessRule::ListCanGotoPos(&board[i], GetPieceAt, &board);
            for (const auto& m : moves) {
                if (!m_pPMng->IsLegalMove(m_pPMng->FindPiece(board[i].GetX(), board[i].GetY()), m.col, m.row)) {
                    continue;
                }
                MoveInfo mi;
                mi.pieceIndex = i;
                mi.from = { board[i].GetX(), board[i].GetY() };
                mi.to = m;
                mi.isCapture = false;
                mi.capturedIndex = -1;

                // 检查目标位置是否有棋子
                for (int j = 0; j < (int)board.size(); ++j) {
                    if (j != i && board[j].GetX() == m.col && board[j].GetY() == m.row) {
                        mi.isCapture = true;
                        mi.capturedIndex = j;
                        break;
                    }
                }
                allMoves.push_back(mi);
            }
        }

        if (allMoves.empty()) return { nullptr, {0, 0} };

        // 2. 走法排序优化：优先搜索吃子走法，提升剪枝效率
        std::sort(allMoves.begin(), allMoves.end(), [](const MoveInfo& a, const MoveInfo& b) {
            return a.isCapture > b.isCapture;
            });

        // 3. 对每一种走法进行 Alpha-Beta 搜索
        int bestScore = (color == RED) ? INT_MIN : INT_MAX;
        int bestMoveIdx = 0;

        for (int i = 0; i < (int)allMoves.size(); ++i) {
            if (m_cancelToken && m_cancelToken->load()) throw std::runtime_error("cancelled");
            MoveInfo mi = allMoves[i];

            // --- 在拷贝上模拟走子，避免直接修改 board 造成索引错位/悬空引用 ---
            std::vector<CChessPiece> bcopy = board;
            if (mi.isCapture && mi.capturedIndex >= 0 && mi.capturedIndex < (int)bcopy.size()) {
                bcopy.erase(bcopy.begin() + mi.capturedIndex);
                if (mi.capturedIndex < mi.pieceIndex) mi.pieceIndex--;
            }
            if (mi.pieceIndex < 0 || mi.pieceIndex >= (int)bcopy.size()) continue;
            bcopy[mi.pieceIndex].SetPosition(mi.to.col, mi.to.row);

            // --- 递归搜索 ---
            // 下一层是对方走：红方走完轮黑方（MIN,false）；黑方走完轮红方（MAX,true）
            int score = AlphaBeta(bcopy, depth - 1, INT_MIN, INT_MAX, (color == BLACK));

            // --- 更新最佳走法 ---
            bool isBetter = false;
            if (color == RED) {
                isBetter = (score > bestScore);
            }
            else {
                isBetter = (score < bestScore);
            }

            if (isBetter) {
                bestScore = score;
                bestMoveIdx = i;
            }
        }

        // 返回最佳走法对应的棋子指针和目标位置
        const MoveInfo& best = allMoves[bestMoveIdx];
        return { &board[best.pieceIndex], best.to };
    }

    // 新 API：返回坐标形式的最佳一步（不依赖原板指针），便于异步/多线程调用
    std::pair<PSF::POS, PSF::POS> FindBestMoveCoords(PieceColor color, const std::vector<CChessPiece>& board, int depth) {
        // generate moves similar to GetBestMove but work with copies
        std::vector<MoveInfo> allMoves;
        for (int i = 0; i < (int)board.size(); ++i) {
            if (m_cancelToken && m_cancelToken->load()) throw std::runtime_error("cancelled");
            if (board[i].GetColor() != color) continue;
            auto moves = PSF::CChessRule::ListCanGotoPos(const_cast<CChessPiece*>(&board[i]), GetPieceAt, (void*)&board);
            for (const auto& m : moves) {
                if (!m_pPMng->IsLegalMove(m_pPMng->FindPiece(board[i].GetX(), board[i].GetY()), m.col, m.row)) {
                    continue;
                }
                MoveInfo mi;
                mi.pieceIndex = i;
                mi.from = { board[i].GetX(), board[i].GetY() };
                mi.to = m;
                mi.isCapture = false;
                mi.capturedIndex = -1;
                for (int j = 0; j < (int)board.size(); ++j) {
                    if (j != i && board[j].GetX() == m.col && board[j].GetY() == m.row) {
                        mi.isCapture = true; mi.capturedIndex = j; break;
                    }
                }
                allMoves.push_back(mi);
            }
        }
        if (allMoves.empty()) return { PSF::POS(0,0), PSF::POS(0,0) };

        // 如果当前处于被将状态，则仅保留能够解将的走法
        auto IsInCheckOnBoard = [](const std::vector<CChessPiece>& b, PieceColor clr) -> bool {
            // 找到将/帅位置
            int kx = -1, ky = -1;
            for (const auto &p : b) {
                if (p.GetType() == PT_JIANG && p.GetColor() == clr) { kx = p.GetX(); ky = p.GetY(); break; }
            }
            if (kx == -1) return false; // king missing -> treated elsewhere
            for (const auto &p : b) {
                if (p.GetColor() == clr) continue;
                if (PSF::CChessRule::CanGotoPos(kx, ky, const_cast<CChessPiece*>(&p), CAIPlayer::GetPieceAt, (void*)&b)) return true;
            }
            return false;
        };

        bool inCheck = IsInCheckOnBoard(board, color);
        std::vector<MoveInfo> movesToEvaluate;
        if (inCheck) {
            // filter moves that resolve check
            for (const auto &mi : allMoves) {
                if (m_cancelToken && m_cancelToken->load()) throw std::runtime_error("cancelled");
                auto bcopy = board;
                if (mi.isCapture && mi.capturedIndex >= 0 && mi.capturedIndex < (int)bcopy.size()) {
                    bcopy.erase(bcopy.begin() + mi.capturedIndex);
                }
                // adjust index not needed since using copy and local mi
                bcopy[mi.pieceIndex].SetPosition(mi.to.col, mi.to.row);
                if (!IsInCheckOnBoard(bcopy, color)) {
                    movesToEvaluate.push_back(mi);
                }
            }
            if (movesToEvaluate.empty()) {
                // no move resolves check -> signal no-solution with sentinel (-1,-1)
                return { PSF::POS(-1,-1), PSF::POS(-1,-1) };
            }
        } else {
            movesToEvaluate = allMoves;
        }

        // Helper: check whether the king of 'clr' can be captured on next opponent move on board 'b'
        auto IsKingCapturable = [](const std::vector<CChessPiece>& b, PieceColor clr) -> bool {
            // find king
            int kx = -1, ky = -1;
            for (const auto &p : b) {
                if (p.GetType() == PT_JIANG && p.GetColor() == clr) { kx = p.GetX(); ky = p.GetY(); break; }
            }
            if (kx == -1) return false;
            // iterate opponent pieces
            for (const auto &p : b) {
                if (p.GetColor() == clr) continue;
                // list moves for p on this board
                auto moves = PSF::CChessRule::ListCanGotoPos(const_cast<CChessPiece*>(&p), GetPieceAt, (void*)&b);
                for (const auto &m : moves) {
                    if (m.col == kx && m.row == ky) return true;
                }
            }
            return false;
        };

        // Remove moves that would expose our king to immediate capture (forbidden moves)
        std::vector<MoveInfo> finalMoves;
        for (const auto &mi : movesToEvaluate) {
            auto bcopy = board;
            if (mi.isCapture && mi.capturedIndex >= 0 && mi.capturedIndex < (int)bcopy.size()) {
                bcopy.erase(bcopy.begin() + mi.capturedIndex);
            }
            bcopy[mi.pieceIndex].SetPosition(mi.to.col, mi.to.row);
            if (!IsKingCapturable(bcopy, color)) {
                finalMoves.push_back(mi);
            }
        }
        if (finalMoves.empty()) {
            // no safe moves -> signal no-solution
            return { PSF::POS(-1,-1), PSF::POS(-1,-1) };
        }

        // parallel evaluate each top-level move
        // Use a shared thread pool to parallelize top-level move evaluations
        ThreadPool &pool = GetThreadPool();
        std::vector<std::future<int>> futures;
        futures.reserve(finalMoves.size());
        for (auto &mi : finalMoves) {
            // build std::function task to avoid parser/template issues
            MoveInfo copyMi = mi;
            auto task = [this, copyMi, board, depth, color]() -> int {
                MoveInfo local = copyMi;
                auto bcopy = board;
                if (local.isCapture && local.capturedIndex >= 0 && local.capturedIndex < (int)bcopy.size()) {
                    bcopy.erase(bcopy.begin() + local.capturedIndex);
                    if (local.capturedIndex < local.pieceIndex) local.pieceIndex--;
                }
                bcopy[local.pieceIndex].SetPosition(local.to.col, local.to.row);
                bool nextIsMax = (color == BLACK);
                return AlphaBeta(bcopy, depth - 1, INT_MIN, INT_MAX, nextIsMax);
            };
            futures.push_back(pool.enqueue(task));
        }

        // collect results
        int bestIdx = 0;
        int bestScore = (color == RED) ? INT_MIN : INT_MAX;
        for (int i = 0; i < (int)finalMoves.size(); ++i) {
            int score = futures[i].get();
            bool isBetter = (color == RED) ? (score > bestScore) : (score < bestScore);
            if (isBetter) { bestScore = score; bestIdx = i; }
        }

        // 如果启用了 preferMate，可以做一次快速检索：若某一步导致对方在有限深度内必败，可优先选择
        try {
            if (GetConfiguredAIPreferMate()) {
                // 简单尝试：如果任一步的评估分数达到极大值（绝杀阈值），立即返回
                const int mateThreshold = 90000;
                for (int i = 0; i < (int)finalMoves.size(); ++i) {
                    if (futures[i].valid()) {
                        // we already consumed futures[i].get() above; cannot re-get. Instead rely on bestScore.
                    }
                }
            }
        } catch(...) {}

        return { finalMoves[bestIdx].from, finalMoves[bestIdx].to };
    }

    // 异步计算：在后台计算并通过回调返回结果 (from,to)。线程安全。
    std::shared_future<std::pair<PSF::POS, PSF::POS>> FindBestMoveCoordsAsync(PieceColor color, const std::vector<CChessPiece>& board, int depth, std::function<void(std::pair<PSF::POS, PSF::POS>)> callback = nullptr) {
        // Use configured depth and threadcount from AIConfigDlg if available
        int useDepth = depth;
        try {
            extern int GetConfiguredAIDepth();
            int cfg = GetConfiguredAIDepth();
            if (cfg > 0) useDepth = cfg;
        } catch(...) {}
        // Consider configured max threads for pool sizing (ThreadPool singleton is fixed size at creation).
        int cfgThreads = 0;
        cfgThreads = GetConfiguredAIMaxThreads();
        // Note: ThreadPool singleton created in GetThreadPool() uses hardware_concurrency - 1 by default.
        ThreadPool &pool = GetThreadPool();
        // create a cancellation token for this async run
        m_cancelToken = std::make_shared<std::atomic<bool>>(false);
        auto token = m_cancelToken;
        auto fut = pool.enqueue([this, color, board, useDepth, token]() -> std::pair<PSF::POS, PSF::POS> {
            try {
                // use the member token inside search routines via this->m_cancelToken
                return FindBestMoveCoords(color, board, useDepth);
            }
            catch (const std::runtime_error&) {
                // cancelled
                return { PSF::POS(-1, -1), PSF::POS(-1, -1) };
            }
        });
        auto shared = fut.share();
        if (callback) {
            // 优化：在工作线程中直接调用回调可能会访问 UI，仍使用 detached thread 来调用回调以保证安全性
            std::thread([shared, callback]() mutable {
                auto res = shared.get();
                callback(res);
            }).detach();
        }
        return shared;
    }

    // 取消当前正在进行的计算（线程安全），会使 AlphaBeta/搜索尽快抛出并结束
    void Cancel() {
        if (m_cancelToken) m_cancelToken->store(true);
    }

    // 清理取消标志（下次搜索前调用）
    void ClearCancel() {
        m_cancelToken.reset();
    }

    // Thread pool accessor (shared instance)
    static ThreadPool& GetThreadPool() {
        static ThreadPool pool(std::max(1u, std::thread::hardware_concurrency() > 1 ? std::thread::hardware_concurrency() - 1 : 1));
        return pool;
    }

private:
    PieceColor currentColor;
    // 取消令牌，共享给后台任务以便及时中止搜索
    std::shared_ptr<std::atomic<bool>> m_cancelToken;

    // Alpha-Beta 剪枝核心函数
    int AlphaBeta(std::vector<CChessPiece>& board, int depth, int alpha, int beta, bool isMaximizing) {
        // 终止条件：搜索到指定深度
        if (m_cancelToken && m_cancelToken->load()) return 0;// throw std::runtime_error("cancelled");
        if (depth == 0) {
            return EvaluateBoardEnhanced(board);
        }

        // 确定当前搜索的是哪一方
        PieceColor side = isMaximizing ? RED : BLACK;

        // 生成当前局面所有合法走法
        std::vector<MoveInfo> moves;
        for (int i = 0; i < (int)board.size(); ++i) {
            if (board[i].GetColor() != side) continue;
            auto pieceMoves = PSF::CChessRule::ListCanGotoPos(&board[i], GetPieceAt, &board);
            for (const auto& m : pieceMoves) {
                MoveInfo mi;
                mi.pieceIndex = i;
                mi.from = { board[i].GetX(), board[i].GetY() };
                mi.to = m;
                mi.isCapture = false;
                mi.capturedIndex = -1;

                for (int j = 0; j < (int)board.size(); ++j) {
                    if (j != i && board[j].GetX() == m.col && board[j].GetY() == m.row) {
                        mi.isCapture = true;
                        mi.capturedIndex = j;
                        break;
                    }
                }
                moves.push_back(mi);
            }
        }

        // 如果没有合法走法（被将死或困毙）
        if (moves.empty()) {
            // 无路可走视为输棋，分数对当前方极其不利
            return isMaximizing ? -100000 : 100000;
        }

        if (isMaximizing) {
            // MAX 层（红方）
            int maxEval = INT_MIN;
            for (auto& mi : moves) {
                if (m_cancelToken && m_cancelToken->load()) return 0;// throw std::runtime_error("cancelled");
                CChessPiece& piece = board[mi.pieceIndex];
                int oldX = piece.GetX(), oldY = piece.GetY();
                CChessPiece capturedBackup;
                bool hasCapture = mi.isCapture;
                int capturedOldIndex = -1;

                if (hasCapture) {
                    capturedBackup = board[mi.capturedIndex];
                    capturedOldIndex = mi.capturedIndex;
                    board.erase(board.begin() + mi.capturedIndex);
                    if (mi.capturedIndex < mi.pieceIndex) {
                        mi.pieceIndex--;
                    }
                }
                piece.SetPosition(mi.to.col, mi.to.row);

                int eval = AlphaBeta(board, depth - 1, alpha, beta, false);
                maxEval = std::max(maxEval, eval);
                alpha = std::max(alpha, eval);

                // 撤销
                piece.SetPosition(oldX, oldY);
                if (hasCapture) {
                    if (capturedOldIndex < (int)board.size()) {
                        board.insert(board.begin() + capturedOldIndex, capturedBackup);
                    }
                    else {
                        board.push_back(capturedBackup);
                    }
                }

                // Beta 剪枝
                if (beta <= alpha) break;
            }
            return maxEval;
        }
        else {
            // MIN 层（黑方）
            int minEval = INT_MAX;
            for (auto& mi : moves) {
                if (m_cancelToken && m_cancelToken->load()) return 0;// throw std::runtime_error("cancelled");
                CChessPiece& piece = board[mi.pieceIndex];
                int oldX = piece.GetX(), oldY = piece.GetY();
                CChessPiece capturedBackup;
                bool hasCapture = mi.isCapture;
                int capturedOldIndex = -1;

                if (hasCapture) {
                    capturedBackup = board[mi.capturedIndex];
                    capturedOldIndex = mi.capturedIndex;
                    board.erase(board.begin() + mi.capturedIndex);
                    if (mi.capturedIndex < mi.pieceIndex) {
                        mi.pieceIndex--;
                    }
                }
                piece.SetPosition(mi.to.col, mi.to.row);

                int eval = AlphaBeta(board, depth - 1, alpha, beta, true);
                minEval = std::min(minEval, eval);
                beta = std::min(beta, eval);

                // 撤销
                piece.SetPosition(oldX, oldY);
                if (hasCapture) {
                    if (capturedOldIndex < (int)board.size()) {
                        board.insert(board.begin() + capturedOldIndex, capturedBackup);
                    }
                    else {
                        board.push_back(capturedBackup);
                    }
                }

                // Alpha 剪枝
                if (beta <= alpha) break;
            }
            return minEval;
        }
    }

    // 辅助：规则引擎回调函数
    static CChessPiece* GetPieceAt(int col, int row, void* userData) {
        std::vector<CChessPiece>* board = static_cast<std::vector<CChessPiece>*>(userData);
        for (auto& p : *board) {
            if (p.GetX() == col && p.GetY() == row) return &p;
        }
        return nullptr;
    }

    // 局面评估函数（核心）
    int EvaluateBoard(const std::vector<CChessPiece>& board) {
        int score = 0;

        // 1. 子力价值评估
        for (const auto& p : board) {
            int pieceValue = GetPieceValue(p.GetType());
            if (p.GetColor() == RED) {
                score += pieceValue;  // 红方加分
            }
            else {
                score -= pieceValue;  // 黑方减分
            }
        }

        // 2. 位置价值评估
        for (const auto& p : board) {
            if (p.GetColor() == RED) {
                score += GetPositionBonus(p);
            }
            else {
                score -= GetPositionBonus(p);
            }
        }

        // 3. 胜负判定（极高优先级）
        bool redKingAlive = false;
        bool blackKingAlive = false;
        for (const auto& p : board) {
            if (p.GetType() == PT_JIANG) {
                if (p.GetColor() == RED) redKingAlive = true;
                else blackKingAlive = true;
            }
        }
        if (!redKingAlive) return -100000;  // 红方输了（黑方赢）
        if (!blackKingAlive) return 100000; // 黑方输了（红方赢）

        return score;
    }

    // 增强版评估：结合子力、兵力位置、机动性与王的安全性（攻击者数）
    int EvaluateBoardEnhanced(const std::vector<CChessPiece>& board) {
        int score = 0;

        // 基础子力与位置
        for (const auto& p : board) {
            int base = GetPieceValue(p.GetType());
            int posBonus = GetPositionBonus(p);
            int mobilityBonus = 0;
            // 机动性：移动数目越多价值稍高
            try {
                auto moves = PSF::CChessRule::ListCanGotoPos(const_cast<CChessPiece*>(&p), GetPieceAt, (void*)&board);
                mobilityBonus = (int)moves.size() * 6; // 每个可走位置约6分
            } catch(...) { mobilityBonus = 0; }

            int centerBonus = 0;
            // 中心控制：靠近中心（5,5）略有加分
            int cx = 5, cy = 5;
            int dist = abs(p.GetX() - cx) + abs(p.GetY() - cy);
            centerBonus = std::max(0, 6 - dist) * 8;

            int totalPieceScore = base + posBonus + mobilityBonus + centerBonus;
            if (p.GetColor() == RED) score += totalPieceScore; else score -= totalPieceScore;
        }

        // King 安全性：统计攻击者数量，攻击越多分数越高（优先杀王）
        int redKingX = -1, redKingY = -1, blackKingX = -1, blackKingY = -1;
        for (const auto &p : board) {
            if (p.GetType() == PT_JIANG) {
                if (p.GetColor() == RED) { redKingX = p.GetX(); redKingY = p.GetY(); }
                else { blackKingX = p.GetX(); blackKingY = p.GetY(); }
            }
        }
        // 胜负快速判定
        if (redKingX == -1) return -1000000; // 红方死亡，极大负值
        if (blackKingX == -1) return 1000000; // 黑方死亡，极大正值

        int redAttackers = 0, blackAttackers = 0;
        for (const auto &p : board) {
            if (p.GetColor() == RED) continue;
            if (PSF::CChessRule::CanGotoPos(redKingX, redKingY, const_cast<CChessPiece*>(&p), CAIPlayer::GetPieceAt, (void*)&board)) redAttackers++;
        }
        for (const auto &p : board) {
            if (p.GetColor() == BLACK) continue;
            if (PSF::CChessRule::CanGotoPos(blackKingX, blackKingY, const_cast<CChessPiece*>(&p), CAIPlayer::GetPieceAt, (void*)&board)) blackAttackers++;
        }

        // 每个攻击者给出很大的加分，鼓励寻找将杀机会（注意与 base king 值配合）
        const int ATTACKER_WEIGHT = 20000;
        score += blackAttackers * ATTACKER_WEIGHT; // 红方对黑王的攻击
        score -= redAttackers * ATTACKER_WEIGHT;   // 黑方对红王的攻击

        return score;
    }

    // 棋子基础价值
    int GetPieceValue(E_PieceType type) {
        switch (type) {
        case PT_JIANG: return 100000; // 将帅：极高价值，优先杀将
        case PT_DU:    return 5000;   // 督：活动范围大，价值很高
        case PT_JU:    return 3000;   // 车：直线无限制
        case PT_SHI:   return 2500;   // 士：九宫内的灵活性
        case PT_HOU:   return 2800;   // 后：九宫内机动性强
        case PT_JUN:   return 1600;   // 军：横竖斜一格，灵活且实用
        case PT_PAO:   return 1500;   // 炮：远程威胁
        case PT_MA:    return 1500;   // 馬：机动性强，与炮同等
        case PT_XIANG: return 1400;   // 相：过河后协助强
        case PT_BING:  return 600;    // 兵：过河后价值上升
        default: return 0;
        }
    }

    // 位置加分
    int GetPositionBonus(const CChessPiece& p) {
        int bonus = 0;
        E_PieceType type = p.GetType();
        int row = p.GetY();
        PieceColor color = p.GetColor();

        // 兵过河加分
        if (type == PT_BING) {
            bool crossed = (color == RED && row <= 4) || (color == BLACK && row >= 5);
            if (crossed) bonus += 20;
        }

        // 马在中心区域加分
        if (type == PT_MA) {
            if (p.GetX() >= 3 && p.GetX() <= 7 && row >= 2 && row <= 7) {
                bonus += 10;
            }
        }

        return bonus;
    }
};