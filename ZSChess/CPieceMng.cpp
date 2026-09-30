#include "framework.h"
#include "CPieceMng.h"

#include <Windows.h>
#include <algorithm>
#include <chrono>

CChessPiece* CPieceMng::FindPiece(int col, int row) {
	for (auto& p : m_pieces) {
		if (p.GetX() == col && p.GetY() == row) {
			return &p;
		}
	}
	return nullptr;
}

void CPieceMng::DrawPieces(HDC hdc, int startX, int startY, int cellSize)
{
    // 如果有动画正在进行，优先绘制其他棋子，最后绘制移动中的棋子
    if (!m_isAnimating) {
        for (auto& piece : m_pieces) {
            piece.Draw(hdc, startX, startY, cellSize);
        }
        return;
    }

    // 计算移动棋子的像素位置
    unsigned long long now = GetTickCount64();
    unsigned long long elapsed = now - m_animStartTime;
    float t = (float)elapsed / (float)m_animDurationMs;
    if (t > 1.0f) t = 1.0f;

    int fromCenterX = startX + m_animFromCol * cellSize;
    int fromCenterY = startY + m_animFromRow * cellSize;
    int toCenterX = startX + m_animToCol * cellSize;
    int toCenterY = startY + m_animToRow * cellSize;

    // Prepare moving-piece identity (avoid using pointer after potential vector erase)
    if (m_animPieceIdx < 0 || m_animPieceIdx >= (int)m_pieces.size()) return;
    PieceColor movingColor = m_pieces[m_animPieceIdx].GetColor();
    int movingType = (int)m_pieces[m_animPieceIdx].GetType();
    wchar_t movingLabel[8]; wcscpy_s(movingLabel, m_pieces[m_animPieceIdx].GetLabel());

    // draw all except animating piece (by index)
    for (int i = 0; i < (int)m_pieces.size(); ++i) {
        if (i == m_animPieceIdx) continue;
        m_pieces[i].Draw(hdc, startX, startY, cellSize);
    }

    // 插值位置（像素级），使用平滑插值并添加抬高效果
    float ease = t < 0.5f ? (2.0f * t * t) : (-1.0f + (4.0f - 2.0f * t) * t); // smoothstep-ish
    float curX = fromCenterX + (toCenterX - fromCenterX) * ease;
    float curY = fromCenterY + (toCenterY - fromCenterY) * ease;

    // 抬高效果：抬高到最大 lift 然后落下，使用正弦曲线
    float liftHeight = cellSize * 0.35f;
    const float PI_F = 3.14159265358979323846f;
    float lift = sinf(PI_F * t) * liftHeight;

    // DrawAt expects pixel center and radius
    int radius = cellSize / 2 - 3;
    // Draw the moving piece from stored index (it still exists until we possibly erase the captured piece)
    m_pieces[m_animPieceIdx].DrawAt(hdc, (int)curX, (int)(curY - lift), radius);

    // 结束时应用最终状态（移动棋子，移除被吃子）
    if (t >= 1.0f) {
        // if capture exists, remove target piece (the one at toCol,toRow but not animPiece)
        // find target index again (it may have shifted)
        // remove captured piece if exists at target coordinates
        for (auto it = m_pieces.begin(); it != m_pieces.end();) {
            if (it->GetX() == m_animToCol && it->GetY() == m_animToRow) {
                // record captured label before erasing
                wcsncpy_s(m_lastCapturedLabel, it->GetLabel(), _TRUNCATE);
                // remove captured
                it = m_pieces.erase(it);
                break;
            }
            else ++it;
        }

        // find the moving piece by matching original position + identity (in case indices shifted)
        for (auto& p : m_pieces) {
            if (p.GetX() == m_animFromCol && p.GetY() == m_animFromRow && p.GetColor() == movingColor && (int)p.GetType() == movingType && 0 == _wcsicmp(p.GetLabel(), movingLabel)) {
                p.SetX(m_animToCol);
                p.SetY(m_animToRow);
                p.SetMoved(true);
                break;
            }
        }

        // finalize last-move info for consumer
        m_lastMoveCompleted = true;
        m_lastFromCol = m_animFromCol;
        m_lastFromRow = m_animFromRow;
        m_lastToCol = m_animToCol;
        m_lastToRow = m_animToRow;
        m_lastWasCapture = m_animIsCapture;
        m_lastMovedColor = movingColor;
        // clear animation
        m_isAnimating = false;
        m_animPieceIdx = -1;
        m_animCapturedIdx = -1;
    }
}

void CPieceMng::StartMoveAnimation(CChessPiece* piece, int toCol, int toRow)
{
    if (!piece) return;
    // find index
    int idx = -1;
    for (int i = 0; i < (int)m_pieces.size(); ++i) {
        if (&m_pieces[i] == piece) { idx = i; break; }
    }
    if (idx < 0) return;

    m_animPieceIdx = idx;
    m_animFromCol = piece->GetX();
    m_animFromRow = piece->GetY();
    m_animToCol = toCol;
    m_animToRow = toRow;
    m_animStartTime = GetTickCount64();
    m_isAnimating = true;
    // mark whether target occupied
    CChessPiece* target = FindPiece(toCol, toRow);
    m_animIsCapture = (target != nullptr && target != piece);
}

void CPieceMng::UpdateAnimation()
{
    // kept for API completeness; actual progression handled in DrawPieces when painting
}

bool CPieceMng::IsLegalMove(CChessPiece* piece, int toCol, int toRow) {
    if (!piece) return false;

    // 1. 基础走法校验（依然使用原始棋盘数据）
    PSF::CChessRule rule;
    auto moves = rule.ListCanGotoPos(piece, PSF::CChessRule::GetPieceAtFunc(
        [](int col, int row, void* userData) -> CChessPiece* {
            return static_cast<CPieceMng*>(userData)->FindPiece(col, row);
        }), this);

    bool canMoveThere = false;
    for (const auto& m : moves) {
        if (m.col == toCol && m.row == toRow) {
            canMoveThere = true;
            break;
        }
    }
    if (!canMoveThere) return false;

    // 2. 复制一份棋子数据，在副本上模拟走棋
    auto copyPiece = m_pieces;
    PieceColor myColor = piece->GetColor();

    // 找到要移动的棋子在副本中的对应位置（通过坐标匹配）
    CChessPiece* simPiece = nullptr;
    CChessPiece* simTarget = nullptr;
    for (auto& p : copyPiece) {
        if (p.GetX() == piece->GetX() && p.GetY() == piece->GetY()) {
            simPiece = &p;
        }
        if (p.GetX() == toCol && p.GetY() == toRow) {
            simTarget = &p;
        }
    }
    if (!simPiece) return false;

    // 在副本上执行移动
    simPiece->SetX(toCol);
    simPiece->SetY(toRow);
    if (simTarget) {
        // 吃掉目标棋子：从副本中移除
        auto it = std::find_if(copyPiece.begin(), copyPiece.end(),
            [simTarget](const CChessPiece& p) { return &p == simTarget; });
        if (it != copyPiece.end()) {
            copyPiece.erase(it);
        }
    }

    // 3. 在副本上检查是否送将或照面
    // 注意：这里必须传入 copyPiece，不能再用 m_pieces
    bool isCheck = IsInCheckOnBoard(copyPiece, myColor);
    bool isFacing = IsKingsFacingOnBoard(copyPiece);

    // 如果送将或照面，则非法
    return !(isCheck || isFacing);
}

// 2. 判断指定颜色的一方是否处于“被将军”状态
bool CPieceMng::IsInCheck(PieceColor color) {
    // 找到己方的将/帅
    CChessPiece* myKing = nullptr;
    for (auto& p : m_pieces) {
        if (p.GetType() == PT_JIANG && p.GetColor() == color) {
            myKing = &p;
            break;
        }
    }
    if (!myKing) return true; // 将都没了，肯定输了

    // 遍历对方所有棋子，看是否能攻击到己方将/帅
    PieceColor enemyColor = (color == RED) ? BLACK : RED;
    for (auto& p : m_pieces) {
        if (p.GetColor() == enemyColor) {
            // 这里需要复用之前的规则判断：对方棋子能否走到将/帅的位置
            PSF::CChessRule rule;
            auto moves = rule.ListCanGotoPos(&p, PSF::CChessRule::GetPieceAtFunc(
                [](int col, int row, void* userData) -> CChessPiece* {
                    return static_cast<CPieceMng*>(userData)->FindPiece(col, row);
                }), this);

            for (const auto& m : moves) {
                if (m.col == myKing->GetX() && m.row == myKing->GetY()) {
                    return true; // 被将军了
                }
            }
        }
    }
    return false;
}

// 3. 判断将帅是否直接照面（中间无子）
bool CPieceMng::IsKingsFacing() {
    CChessPiece* redKing = nullptr;
    CChessPiece* blackKing = nullptr;
    for (auto& p : m_pieces) {
        if (p.GetType() == PT_JIANG) {
            if (p.GetColor() == RED) redKing = &p;
            else blackKing = &p;
        }
    }
    if (!redKing || !blackKing) return false;

    // 必须在同一列
    if (redKing->GetX() != blackKing->GetX()) return false;

    int col = redKing->GetX();
    int minRow = std::min(redKing->GetY(), blackKing->GetY());
    int maxRow = std::max(redKing->GetY(), blackKing->GetY());

    // 检查中间是否有棋子
    for (int r = minRow + 1; r < maxRow; ++r) {
        if (FindPiece(col, r) != nullptr) {
            return false; // 中间有子，不照面
        }
    }
    return true; // 中间无子，照面了
}

// 4. 判断指定颜色的一方是否被“将死”或“困毙”（无合法走法）
bool CPieceMng::IsCheckmate(PieceColor color) {
    // 遍历该颜色所有棋子，尝试所有可能的走法
    for (auto& p : m_pieces) {
        if (p.GetColor() != color) continue;

        // 获取该棋子的所有候选走法
        PSF::CChessRule rule;
        auto moves = rule.ListCanGotoPos(&p, PSF::CChessRule::GetPieceAtFunc(
            [](int col, int row, void* userData) -> CChessPiece* {
                return static_cast<CPieceMng*>(userData)->FindPiece(col, row);
            }), this);

        // 尝试每一个走法，看是否有合法的（能解将的）
        for (const auto& m : moves) {
            if (IsLegalMove(&p, m.col, m.row)) {
                return false; // 只要有一个合法走法，就没死
            }
        }
    }
    return true; // 所有棋子都没有合法走法，死棋
}

//// 5. 获取指定颜色所有合法的走法列表（用于AI搜索或UI提示）
//std::vector<std::pair<CChessPiece*, PSF::POS>> CPieceMng::GetAllLegalMoves(PieceColor color) {
//    std::vector<std::pair<CChessPiece*, PSF::POS>> legalMoves;
//    for (auto& p : m_pieces) {
//        if (p.GetColor() != color) continue;
//
//        PSF::CChessRule rule;
//        auto moves = rule.ListCanGotoPos(&p, PSF::CChessRule::GetPieceAtFunc(
//            [](int col, int row, void* userData) -> CChessPiece* {
//                return static_cast<CPieceMng*>(userData)->FindPiece(col, row);
//            }), this);
//
//        for (const auto& m : moves) {
//            if (IsLegalMove(&p, m.col, m.row)) {
//                legalMoves.push_back({ &p, m });
//            }
//        }
//    }
//    return legalMoves;
//}

// 辅助函数1：在指定棋盘副本上判断某方是否被将军
bool CPieceMng::IsInCheckOnBoard(const std::vector<CChessPiece>& board, PieceColor color) {
    // 找到己方的将/帅
    const CChessPiece* myKing = nullptr;
    for (const auto& p : board) {
        if (p.GetType() == PT_JIANG && p.GetColor() == color) {
            myKing = &p;
            break;
        }
    }
    if (!myKing) return true;

    // 遍历对方所有棋子，看是否能攻击到己方将/帅
    PieceColor enemyColor = (color == RED) ? BLACK : RED;
    for (const auto& p : board) {
        if (p.GetColor() == enemyColor) {
            // 这里需要复用规则引擎，但 GetPieceAtFunc 要改成查 board 副本
            PSF::CChessRule rule;
            auto moves = rule.ListCanGotoPos(const_cast<CChessPiece*>(&p),
                
                    [](int col, int row, void* userData) -> CChessPiece* {
                        for (const auto& piece : *static_cast<std::vector<CChessPiece>*>(userData)) {
                            if (piece.GetX() == col && piece.GetY() == row) {
                                return const_cast<CChessPiece*>(&piece);
                            }
                        }
                        return nullptr;
                    },
                const_cast<std::vector<CChessPiece>*>(&board));

            for (const auto& m : moves) {
                if (m.col == myKing->GetX() && m.row == myKing->GetY()) {
                    return true;
                }
            }
        }
    }
    return false;
}

// 辅助函数2：在指定棋盘副本上判断将帅是否照面
bool CPieceMng::IsKingsFacingOnBoard(const std::vector<CChessPiece>& board) {
    const CChessPiece* redKing = nullptr;
    const CChessPiece* blackKing = nullptr;
    for (const auto& p : board) {
        if (p.GetType() == PT_JIANG) {
            if (p.GetColor() == RED) redKing = &p;
            else blackKing = &p;
        }
    }
    if (!redKing || !blackKing) return false;
    if (redKing->GetX() != blackKing->GetX()) return false;

    int col = redKing->GetX();
    int minRow = std::min(redKing->GetY(), blackKing->GetY());
    int maxRow = std::max(redKing->GetY(), blackKing->GetY());

    for (int r = minRow + 1; r < maxRow; ++r) {
        for (const auto& p : board) {
            if (p.GetX() == col && p.GetY() == r) {
                return false; // 中间有子，不照面
            }
        }
    }
    return true; // 中间无子，照面
}