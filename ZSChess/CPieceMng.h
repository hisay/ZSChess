#pragma once
#include "CChessPiece.h"
#include <vector>
#include <utility>
struct PieceData { int x, y; const wchar_t* label; PieceColor color; E_PieceType type; };
static PieceData initialPieces[] = {
    // --- 黑方 (上方) ---
    {0, 0, L"俥", BLACK, PT_JU}, {1, 0, L"傌", BLACK, PT_MA}, {3, 0, L"仕", BLACK, PT_SHI}, {2, 0, L"像", BLACK, PT_XIANG}, {4, 0, L"后", BLACK, PT_HOU},
    {5, 0, L"将", BLACK, PT_JIANG}, {6, 0, L"后", BLACK, PT_HOU}, {8, 0, L"像", BLACK, PT_XIANG}, {7, 0, L"仕", BLACK, PT_SHI}, {9, 0, L"傌", BLACK, PT_MA}, {10, 0, L"俥", BLACK, PT_JU},
    {1, 2, L"炮", BLACK, PT_PAO},   {9, 2, L"炮", BLACK, PT_PAO},
    {0, 3, L"卒", BLACK, PT_BING}, {2, 3, L"卒", BLACK, PT_BING},  {4, 3, L"军", BLACK, PT_JUN},{5, 3, L"军", BLACK, PT_JUN},{6, 3, L"军", BLACK, PT_JUN},
	{8, 3, L"卒", BLACK, PT_BING}, {10, 3, L"卒", BLACK, PT_BING	},
	{5, 2,L"督", BLACK ,PT_DU },


	// --- 红方 (下方) ---
	{0, 10, L"車", RED, PT_JU}, {1, 10, L"馬", RED, PT_MA}, {3, 10, L"士", RED, PT_SHI}, {2, 10, L"相", RED, PT_XIANG}, {4, 10, L"后", RED, PT_HOU},
	{5, 10, L"帅", RED, PT_JIANG}, {6, 10, L"后", RED, PT_HOU}, {8, 10, L"相", RED, PT_XIANG}, {7, 10, L"士", RED, PT_SHI}, {9, 10, L"馬", RED, PT_MA}, {10, 10, L"車", RED, PT_JU},
	{1, 8, L"炮", RED, PT_PAO},   {9, 8, L"炮", RED, PT_PAO},
	{0, 7, L"兵", RED, PT_BING}, {2, 7, L"兵", RED, PT_BING},  {4, 7,L"军", RED ,PT_JUN },  {5, 7,L"军", RED ,PT_JUN },  {6, 7,L"军", RED ,PT_JUN }, 
	{8 ,7 ,L"兵",RED ,PT_BING },{10 ,7 ,L"兵",RED ,PT_BING },

	{5, 8,L"督", RED ,PT_DU }
};
static const int initialPiecesCount = sizeof(initialPieces) / sizeof(initialPieces[0]);

class CPieceMng
{
public:
	void InitPieces()
	{
		m_pieces.clear();
		for (const auto& pd : initialPieces)
		{
			m_pieces.emplace_back(pd.x, pd.y, pd.label, pd.color, pd.type);
		}
		this->ClearRec();
	}
	void DrawPieces(HDC hdc, int startX, int startY, int cellSize);

	// 动画控制接口
	// 发起一个从 piece -> (toCol,toRow) 的行子动画，动画结束后才会真正移除目标棋子
	void StartMoveAnimation(CChessPiece* piece, int toCol, int toRow);
	// 更新动画状态（内部使用）
	void UpdateAnimation();
	bool IsAnimating() const { return m_isAnimating; }
	CChessPiece* FindPiece(int col_X, int row_Y);
	CChessPiece* GetSelectedPiece()
	{
		for (auto& piece : m_pieces)
		{
			if (piece.IsSelected())
				return &piece;
		}
		return nullptr;
	}
	void ClearPiece(CChessPiece* piece)
	{
		if (piece)
		{
			auto it = std::remove_if(m_pieces.begin(), m_pieces.end(),
				[piece](const CChessPiece& p) { return &p == piece; });
			m_pieces.erase(it, m_pieces.end());
		}
	}
	void ClearSelection()
	{
		for (auto& piece : m_pieces)
		{
			piece.SetSelected(false);
		}
	}
	void SelectPiece(int col, int row)
	{
		for (auto& piece : m_pieces)
		{
			if (piece.GetX() == col && piece.GetY() == row)
			{
				piece.SetSelected(true);
				return;
			}
		}
	}
	CChessPiece* findKing(PieceColor color)
	{
		for (auto& piece : m_pieces)
		{
			if (piece.GetType() == PT_JIANG && piece.GetColor() == color)
				return &piece;
		}
		return nullptr;
	}
	void ClearMovedStatus()
	{
		for (auto& piece : m_pieces)
		{
			piece.SetMoved(false);
		}
	}
	void SetPieceMoved(CChessPiece* piece, bool moved)
	{
		ClearMovedStatus();
		if (piece)
		{
			piece->SetMoved(moved);
		}
	}
	bool IsLegalMove(CChessPiece* piece, int toCol, int toRow);

    // 2. 判断指定颜色的一方是否处于“被将军”状态
	bool IsInCheck(PieceColor color);

    // 3. 判断将帅是否直接照面（中间无子）
    bool IsKingsFacing();

    // 4. 判断指定颜色的一方是否被“将死”或“困毙”（无合法走法）
    bool IsCheckmate(PieceColor color);

    // 5. 获取指定颜色所有合法的走法列表（用于AI搜索或UI提示）
    //std::vector<std::pair<CChessPiece*, PSF::POS>> GetAllLegalMoves(PieceColor color);

	std::vector<CChessPiece>& AllPieces() { return m_pieces; }
	// 用给定局面整体替换当前棋子（复盘重放用），同时清空悔棋栈
	void ReplacePieces(const std::vector<CChessPiece>& board) {
		m_pieces = board;
		ClearRec();
	}
	
	void ClearRec() {
		m_rec.clear();
	}
	void SaveRec(PieceColor clr,int col ,int row) {
		m_rec.push_back(std::make_pair(RecS(clr,col,row), m_pieces));
	}
	bool BackStep(PieceColor& clr,int & col,int & row) {
		if (m_rec.size() > 0) {
			auto  x = m_rec.back();
			m_rec.pop_back();
			clr = x.first.clr;
			col = x.first.m_lastCol;
			row = x.first.m_lastRow;
			m_pieces = x.second;
			return true;
		}
		return false;
	}
 
private:
 
	struct RecS {
		PieceColor clr;
		int m_lastCol;
		int m_lastRow;
		RecS(PieceColor clr, int col, int row) {
			this->clr = clr;
			this->m_lastCol = col;
			this->m_lastRow = row;
		}
	};
	std::vector<CChessPiece> m_pieces;
	std::vector< std::pair<RecS, std::vector<CChessPiece>>> m_rec;
	// 动画状态
	bool m_isAnimating = false;
	int m_animPieceIdx = -1; // index in m_pieces
	int m_animFromCol = -1;
	int m_animFromRow = -1;
	int m_animToCol = -1;
	int m_animToRow = -1;
	unsigned long long m_animStartTime = 0; // ms
	int m_animDurationMs = 300;
	// 被吃掉的目标：保留在 m_pieces 直到动画结束
	bool m_animIsCapture = false;
	int m_animCapturedIdx = -1;
	// 上一次完成的动画信息（消费式获取）
	bool m_lastMoveCompleted = false;
	int m_lastFromCol = -1;
	int m_lastFromRow = -1;
	int m_lastToCol = -1;
	int m_lastToRow = -1;
	bool m_lastWasCapture = false;
	PieceColor m_lastMovedColor = RED;
	 wchar_t m_lastCapturedLabel[32] = {0};
 
public:
	// 尝试消费最后一次完成的移动信息，返回 true 并填充参数后清除标记
	bool ConsumeLastMove(int &fromCol, int &fromRow, int &toCol, int &toRow, bool &wasCapture, PieceColor &color) {
		if (!m_lastMoveCompleted) return false;
		fromCol = m_lastFromCol; fromRow = m_lastFromRow; toCol = m_lastToCol; toRow = m_lastToRow; wasCapture = m_lastWasCapture; color = m_lastMovedColor;
		m_lastMoveCompleted = false;
		return true;
	}
	const wchar_t* GetLastCapturedLabel() const { return m_lastCapturedLabel; }
	// 辅助函数1：在指定棋盘副本上判断某方是否被将军
	bool IsInCheckOnBoard(const std::vector<CChessPiece>& board, PieceColor color);

	// 辅助函数2：在指定棋盘副本上判断将帅是否照面
	bool IsKingsFacingOnBoard(const std::vector<CChessPiece>& board);
};

