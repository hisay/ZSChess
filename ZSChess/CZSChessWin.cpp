#include "CZSChessWin.h"

#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

CZSChessWin::CZSChessWin()
{
	
}

CZSChessWin::~CZSChessWin()
{
	Gdiplus::GdiplusShutdown(m_gdiplusToken);
}

void CZSChessWin::OnMouseClick(int mouseX, int mouseY)
{
 
	m_qipan.OnMouseClick(mouseX, mouseY);
}
void CZSChessWin::OnDraw()
{
	if (!m_hWnd)
		return;
 
	HDC hdc = GetDC(m_hWnd);
	RECT cr;
	GetClientRect(m_hWnd, &cr);
	HDC memdc = CreateCompatibleDC(hdc);
	HBITMAP membmp = CreateCompatibleBitmap(hdc, cr.right, cr.bottom);
	auto memObj = SelectObject(memdc, membmp);


	Gdiplus::Graphics graphics(memdc);
	graphics.Clear(Gdiplus::Color(244, 164, 96));
	
	m_qipan.SetSize(cr.right, cr.bottom);
	m_qipan.Draw(memdc);

	BitBlt(hdc, 0, 0, cr.right, cr.bottom, memdc, 0, 0, SRCCOPY);

	SelectObject(memdc, memObj);
	DeleteObject(membmp);
	DeleteDC(memdc);

	ReleaseDC(m_hWnd, hdc);
	// 如果正在动画中，继续刷新窗口以推进动画帧
	if (m_qipan.PMng().IsAnimating()) {
		InvalidateRect(m_hWnd, nullptr, FALSE);
	}
}

bool CZSChessWin::InitGDIPlus()
{
	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, nullptr);
	return m_gdiplusToken !=0;
}

void CZSChessWin::ShutdownGDIPlus()
{
	Gdiplus::GdiplusShutdown(m_gdiplusToken);
}
