#include "CAICalcAnimal.h"
#include <math.h>
#include <algorithm>
void CAICalcAnimal::Draw(HDC dc, int x, int y) {
	// 背景圆圈半径
	int r = this->m_banJin;
	// 画完整背景圆（灰色细线）
	HPEN hBgPen = CreatePen(PS_SOLID, 2, RGB(200, 200, 200));
	HBRUSH hOldBrush = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
	HGDIOBJ hOldPen = SelectObject(dc, hBgPen);
	Ellipse(dc, x - r, y - r, x + r, y + r);

	// 计算红色弧的起止角度（度数）
	const int segLen = 60; // 弧长（度）
	// 更新角度并保持在 [0,360)
	this->m_jidu = (this->m_jidu + 15) % 360;
	int startDeg = (int)(this->m_jidu);
	int endDeg = (startDeg + segLen) % 360;

	// 由角度计算弧线端点坐标（屏幕坐标 y 向下为正）
	const double PI = 3.14159265358979323846;
	double startRad = startDeg * PI / 180.0;
	double endRad = endDeg * PI / 180.0;
	int xStart = x + (int)(cos(startRad) * r);
	int yStart = y - (int)(sin(startRad) * r);
	int xEnd = x + (int)(cos(endRad) * r);
	int yEnd = y - (int)(sin(endRad) * r);

	// 画红色弧段（稍粗）
	HPEN hArcPen = CreatePen(PS_SOLID, 4, RGB(220, 50, 50));
	SelectObject(dc, hArcPen);
	// Arc 使用的是椭圆的边界以及起止点坐标
	Arc(dc, x - r, y - r, x + r, y + r, xStart, yStart, xEnd, yEnd);

	// 恢复 GDI 对象并释放
	SelectObject(dc, hOldPen);
	SelectObject(dc, hOldBrush);
	DeleteObject(hBgPen);
	DeleteObject(hArcPen);
}