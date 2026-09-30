#include "CChessPiece.h"
#include "CQiPan.h"
#include <math.h>

CChessPiece::CChessPiece(int x, int y, const wchar_t* label, PieceColor color, int type)
	: m_x(x), m_y(y), m_color(color), m_type(type)
{
	wcscpy_s(m_label, label);
}

#ifdef MYSOURCEVER
// 在指定中心和半径绘制棋子（用于动画）
void CChessPiece::DrawAt(HDC hdc, int centerX, int centerY, int radius)
{
	// 状态边框（选中 / 可吃 / 已移动）
	if (m_isSelected)
	{
		HPEN hSelectPen = CreatePen(PS_SOLID, 3, RGB(0, 255, 0));
		HPEN hOldSelectPen = (HPEN)SelectObject(hdc, hSelectPen);
		Ellipse(hdc, centerX - radius - 2, centerY - radius - 2, centerX + radius + 2, centerY + radius + 2);
		SelectObject(hdc, hOldSelectPen);
		DeleteObject(hSelectPen);
	}

	if (IsKilling()) {
		HPEN hMovedPen = CreatePen(PS_SOLID, 3, RGB(255, 0, 0));
		HPEN hOldMovedPen = (HPEN)SelectObject(hdc, hMovedPen);
		Ellipse(hdc, centerX - radius - 5, centerY - radius - 5, centerX + radius + 5, centerY + radius + 5);
		SelectObject(hdc, hOldMovedPen);
		DeleteObject(hMovedPen);
	}

	if (m_isMoved) {
		HPEN hMovedPen = CreatePen(PS_SOLID, 3, RGB(255, 255, 0));
		HPEN hOldMovedPen = (HPEN)SelectObject(hdc, hMovedPen);
		Ellipse(hdc, centerX - radius - 3, centerY - radius - 3, centerX + radius + 3, centerY + radius + 3);
		SelectObject(hdc, hOldMovedPen);
		DeleteObject(hMovedPen);
	}

	// 阴影
	int shadowOffset = ((radius / 6) > 2 ? (radius / 6) : 2);
	HBRUSH hShadowBrush = CreateSolidBrush(RGB(60, 60, 60));
	auto  hOldPenForShadow = (HPEN)SelectObject(hdc, GetStockObject(NULL_PEN));

	HBRUSH hOldShadow = (HBRUSH)SelectObject(hdc, hShadowBrush);
	Ellipse(hdc, centerX - radius + shadowOffset, centerY - radius + shadowOffset, centerX + radius + shadowOffset, centerY + radius + shadowOffset);
	SelectObject(hdc, hOldShadow);
	DeleteObject(hShadowBrush);
	SelectObject(hdc, hOldPenForShadow);

	// 径向渐变：多层同心圆
	int steps = ((radius / 6) > 4 ? (radius / 6) : 4);
	int outerR = (m_color == RED) ? 120 : 20;
	int outerG = (m_color == RED) ? 30 : 20;
	int outerB = (m_color == RED) ? 30 : 20;
	int innerR = (m_color == RED) ? 255 : 86;
	int innerG = (m_color == RED) ? 0 : 80;
	int innerB = (m_color == RED) ? 0 : 80;
	int denom = (steps > 1) ? (steps - 1) : 1;
	for (int i = 0; i < steps; ++i) {
		float t = (float)i / (float)denom;
		int cr = (int)(outerR + (innerR - outerR) * t);
		int cg = (int)(outerG + (innerG - outerG) * t);
		int cb = (int)(outerB + (innerB - outerB) * t);
		int r = radius - (radius * i) / steps;
		HBRUSH hBrush = CreateSolidBrush(RGB(cr, cg, cb));
		auto hPen = (HPEN)SelectObject(hdc, GetStockObject(NULL_PEN));
		HBRUSH hOld = (HBRUSH)SelectObject(hdc, hBrush);
		Ellipse(hdc, centerX - r, centerY - r, centerX + r, centerY + r);
		SelectObject(hdc, hOld);
		DeleteObject(hBrush);
		SelectObject(hdc, hPen);
	}

	// 高光（无边框填充椭圆）
	HPEN hOldPenForHighlight = (HPEN)SelectObject(hdc, GetStockObject(NULL_PEN));
	HBRUSH hHighlight = CreateSolidBrush(m_color == RED ? RGB(255, 100, 100) : RGB(80, 80, 80));
	HBRUSH hOldH = (HBRUSH)SelectObject(hdc, hHighlight);
	int hx = centerX - radius / 3;
	int hy = centerY - radius / 2;
	int hw = radius * 2 / 3; int hh = radius / 3;
	Ellipse(hdc, hx, hy, hx + hw, hy + hh);
	SelectObject(hdc, hOldH);
	SelectObject(hdc, hOldPenForHighlight);
	DeleteObject(hHighlight);

	// 外边描边（只描边）
	HPEN hPen = CreatePen(PS_SOLID, 2, m_color == RED ? RGB(245, 0, 0) : RGB(40, 0, 0));
	HGDIOBJ hOldBrushForPen = SelectObject(hdc, GetStockObject(NULL_BRUSH));
	HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
	Ellipse(hdc, centerX - radius, centerY - radius, centerX + radius, centerY + radius);
	SelectObject(hdc, hOldPen);
	SelectObject(hdc, hOldBrushForPen);
	DeleteObject(hPen);

	// 文本
	SetBkMode(hdc, TRANSPARENT);
	SetTextColor(hdc, RGB(255, 255, 255));
	HFONT hFont = CreateFontW((int)(radius * 1.4), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"楷体");
	HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
	SIZE textSize;
	GetTextExtentPoint32W(hdc, m_label, wcslen(m_label), &textSize);
	int textX = centerX - textSize.cx / 2;
	int textY = centerY - textSize.cy / 2;
	TextOutW(hdc, textX, textY, m_label, wcslen(m_label));
	SelectObject(hdc, hOldFont);
	DeleteObject(hFont);
}
#endif
void CChessPiece::DrawAt(HDC hdc, int centerX, int centerY, int radius)
{
	if (radius < 4) return;

	// 1. 软阴影
	DrawSoftShadow(hdc, centerX, centerY, radius);

	// 2. 棋子底色 + 径向渐变
	DrawPieceBody(hdc, centerX, centerY, radius);

	//// 3. 木纹 / 颗粒质感
	//DrawWoodTexture(hdc, centerX, centerY, radius);

	// 4. 边缘倒角和高光
	DrawEdgeHighlight(hdc, centerX, centerY, radius);

	// 5. 状态边框
	DrawStateBorder(hdc, centerX, centerY, radius);

	// 6. 文字
	DrawPieceText(hdc, centerX, centerY, radius);
}

#define AIDRAW
#ifdef AIDRAW
void CChessPiece::DrawSoftShadow(HDC hdc, int cx, int cy, int r)
{
	int shadowOffset = r / 15;
	if (shadowOffset < 2) shadowOffset = 2;

	for (int i = 0; i < 2; ++i)
	{
		int alpha = 40 - i * 10;
		if (alpha < 0) alpha = 0;

		int expand = i * 2;
		COLORREF c = RGB(30 + alpha, 30 + alpha, 30 + alpha);

		HBRUSH brush = CreateSolidBrush(c);
		HPEN pen = (HPEN)SelectObject(hdc, GetStockObject(NULL_PEN));
		HBRUSH old = (HBRUSH)SelectObject(hdc, brush);

		Ellipse(hdc,
			cx - r + shadowOffset - expand,
			cy - r + shadowOffset - expand,
			cx + r + shadowOffset + expand,
			cy + r + shadowOffset + expand);

		SelectObject(hdc, old);
		SelectObject(hdc, pen);
		DeleteObject(brush);
	}
}

void CChessPiece::DrawPieceBody(HDC hdc, int cx, int cy, int r)
{
	int steps = std::max(r / 4, 12);

	int outerR, outerG, outerB;
	int innerR, innerG, innerB;

	if (m_color == RED)
	{
		outerR = 250; outerG = 20; outerB = 20;
		innerR = 220; innerG = 0; innerB = 0;
	}
	else
	{
		outerR = 10; outerG = 10; outerB = 10;
		innerR = 60; innerG = 60; innerB = 60;
	}

	for (int i = 0; i < steps; ++i)
	{
		float t = (float)i / (float)(steps - 1);

		int cr = (int)(outerR + (innerR - outerR) * t);
		int cg = (int)(outerG + (innerG - outerG) * t);
		int cb = (int)(outerB + (innerB - outerB) * t);

		int curR = (int)(r * (1.0f - t * 0.15f));
		if (curR < 1) curR = 1;

		HBRUSH brush = CreateSolidBrush(RGB(cr, cg, cb));
		HPEN pen = (HPEN)SelectObject(hdc, GetStockObject(NULL_PEN));
		HBRUSH old = (HBRUSH)SelectObject(hdc, brush);

		Ellipse(hdc, cx - curR, cy - curR, cx + curR, cy + curR);

		SelectObject(hdc, old);
		SelectObject(hdc, pen);
		DeleteObject(brush);
	}
}

void CChessPiece::DrawWoodTexture(HDC hdc, int cx, int cy, int r)
{
	int inner = r - 3;
	if (inner < 4) return;

	RECT rc;
	rc.left = cx - inner;
	rc.top = cy - inner;
	rc.right = cx + inner;
	rc.bottom = cy + inner;

	for (int y = rc.top; y < rc.bottom; ++y)
	{
		for (int x = rc.left; x < rc.right; ++x)
		{
			int dx = x - cx;
			int dy = y - cy;
			if (dx * dx + dy * dy > inner * inner) continue;

			int noise = (rand() % 11) - 5;

			COLORREF old = GetPixel(hdc, x, y);
			int rr = GetRValue(old) + noise;
			int gg = GetGValue(old) + noise;
			int bb = GetBValue(old) + noise;

			rr = std::max(0, std::min(255, rr));
			gg = std::max(0, std::min(255, gg));
			bb = std::max(0, std::min(255, bb));

			SetPixel(hdc, x, y, RGB(rr, gg, bb));
		}
	}
}

void CChessPiece::DrawEdgeHighlight(HDC hdc, int cx, int cy, int r)
{
	// 外圈暗边
	HPEN darkPen = CreatePen(PS_SOLID, 2, RGB(60, 10, 5));
	HPEN oldPen = (HPEN)SelectObject(hdc, darkPen);
	HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));

	Ellipse(hdc, cx - r, cy - r, cx + r, cy + r);

	SelectObject(hdc, oldPen);
	DeleteObject(darkPen);

	// 内圈亮边
	HPEN lightPen = CreatePen(PS_SOLID, 1, RGB(255, 180, 160));
	oldPen = (HPEN)SelectObject(hdc, lightPen);

	Ellipse(hdc, cx - r + 2, cy - r + 2, cx + r - 2, cy + r - 2);

	SelectObject(hdc, oldPen);
	SelectObject(hdc, oldBrush);
	DeleteObject(lightPen);
}
void CChessPiece::DrawEillFrame(HDC hdc, DWORD clr, int width, int x, int y, int x2, int y2) {
	HPEN pen = CreatePen(PS_SOLID, width, clr);
	HPEN oldPen = (HPEN)SelectObject(hdc, pen);
	HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));

	Ellipse(hdc, x,y,x2,y2);

	SelectObject(hdc, oldPen);
	SelectObject(hdc, oldBrush);
	DeleteObject(pen);
}
void CChessPiece::DrawStateBorder(HDC hdc, int cx, int cy, int r)
{
	COLORREF color = RGB(0, 0, 0);
	int offset = 0;


	if (IsKilling())
	{
		color =  RGB(220, 40, 40);
		offset = 3;
		DrawEillFrame(hdc, color, 4, cx - r - offset, cy - r - offset, cx + r + offset, cy + r + offset);
	}
	
	if (m_isMoved)
	{
		color = RGB(255, 255, 255);
		offset = 2;
		DrawEillFrame(hdc, color,5,cx - r - offset, cy - r - offset, cx + r + offset, cy + r + offset);
	}
	if (m_isSelected)
	{
		color = RGB(0, 200, 60);
		offset = 3;
		DrawEillFrame(hdc, color, 3, cx - r - offset, cy - r - offset, cx + r + offset, cy + r + offset);
	}

	//if (offset <= 0) return;

	//HPEN pen = CreatePen(PS_SOLID, 3, color);
	//HPEN oldPen = (HPEN)SelectObject(hdc, pen);
	//HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));

	//Ellipse(hdc, cx - r - offset, cy - r - offset, cx + r + offset, cy + r + offset);

	//SelectObject(hdc, oldPen);
	//SelectObject(hdc, oldBrush);
	//DeleteObject(pen);
}

void CChessPiece::DrawPieceText(HDC hdc, int cx, int cy, int r)
{
	SetBkMode(hdc, TRANSPARENT);

	COLORREF textColor = (m_color == RED) ? RGB(255, 255, 255) : RGB(20, 20, 20);
	COLORREF shadowColor = (m_color == RED) ? RGB(255, 255, 255) : RGB(180, 180, 180);
	textColor = RGB(255, 255, 255);
	shadowColor = RGB(20, 20, 30);
	int fontSize = (int)(r * 1.3);
	if (fontSize < 12) fontSize = 12;

	HFONT font = CreateFontW(fontSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"楷体");

	HFONT oldFont = (HFONT)SelectObject(hdc, font);

	SIZE sz;
	GetTextExtentPoint32W(hdc, m_label, wcslen(m_label), &sz);

	int tx = cx - sz.cx / 2;
	int ty = cy - sz.cy / 2;

	// 凹陷阴影
	SetTextColor(hdc, shadowColor);
	TextOutW(hdc, tx + 1, ty + 1, m_label, (int)wcslen(m_label));

	// 主文字
	SetTextColor(hdc, textColor);
	TextOutW(hdc, tx, ty, m_label, (int)wcslen(m_label));

	SelectObject(hdc, oldFont);
	DeleteObject(font);
}
#endif


void CChessPiece::Draw(HDC hdc, int startX, int startY, int cellSize)
{
	int boardStartX = startX + m_x * cellSize - cellSize / 2;
	int boardStartY = startY + m_y * cellSize - cellSize / 2;
	int centerX = boardStartX + cellSize / 2;
	int centerY = boardStartY + cellSize / 2;
	int radius = cellSize / 2 - 3;
	DrawAt(hdc, centerX, centerY, radius);
}

// 辅助函数：用于规则引擎查询棋盘上是否有棋子
static CChessPiece* CheckBoardForPiece(int col, int row, void* userData) {
	std::vector<CChessPiece>* pieces = static_cast<std::vector<CChessPiece>*>(userData);
	if (!pieces) return nullptr;
	for (auto& p : *pieces) {
		if (p.GetX() == col && p.GetY() == row) {
			return &p;
		}
	}
	return nullptr;
}

bool CChessPiece::IsKilling() {
	PieceColor myColor = this->GetColor();
	int myCol = this->GetX();
	int myRow = this->GetY();
	auto allPieces = g_qiPan->GetAllPiece();
	for (auto& piece : allPieces) {
		if (piece.GetColor() != myColor) {
			bool canKillMe = PSF::CChessRule::CanGotoPos(
				myCol,
				myRow,
				&piece,
				CheckBoardForPiece,
				&allPieces
			);
			if (canKillMe) return true;
		}
	}
	return false;
}
