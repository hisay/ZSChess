#include "TextAnim.h"
#include <gdiplus.h>
#include <objidl.h>
#include <cmath>
using namespace Gdiplus;

CTextAnim::CTextAnim()
	: m_active(false), m_startTime(0), m_duration(1000)
{
}

void CTextAnim::Start(const std::wstring& text, int durationMs)
{
	m_text = text;
	m_duration = durationMs;
	m_startTime = GetTickCount64();
	m_active = true;
}

void CTextAnim::Draw(HDC hdc, int centerX, int centerY)
{
	if (!m_active) return;
	unsigned long long now = GetTickCount64();
	unsigned long long elapsed = now - m_startTime;
	float t = (float)elapsed / (float)m_duration;
	if (t >= 1.0f) {
		m_active = false;
		return;
	}

	// ease out for scale and fade
	float ease = 1.0f - powf(1.0f - t, 2.0f);
	float scale = 1.0f + 0.6f * (1.0f - ease); // start bigger then settle
	float alpha = 1.0f - ease; // fade out

	// Use GDI+ to draw gradient outlined text
	Graphics graphics(hdc);
	graphics.SetSmoothingMode(SmoothingModeHighQuality);

	// Choose font size relative to device DPI and desired scale
	REAL fontSize = 36.0f * scale;
	FontFamily fontFamily(L"SimSun");
	Font font(&fontFamily, fontSize, FontStyleBold, UnitPixel);

	// Create path for text
	GraphicsPath path;
	StringFormat sf;
	sf.SetAlignment(StringAlignmentCenter);
	sf.SetLineAlignment(StringAlignmentCenter);
	RectF layout((REAL)centerX - 300.0f, (REAL)centerY - 80.0f, 600.0f, 160.0f);
	path.AddString(m_text.c_str(), -1, &fontFamily, FontStyleBold, fontSize, layout, &sf);

	// Outline pen (slightly thicker) drawn with solid color and alpha
	Color outlineColor((BYTE)(alpha * 255), 0, 0, 0); // black outline
	Pen pen(outlineColor, 4.0f + 2.0f * (1.0f - t));
	pen.SetLineJoin(LineJoinRound);

	// Gradient fill brush
	RectF bounds;
	path.GetBounds(&bounds);
	Color c1((BYTE)(alpha * 255), 255, 215, 0); // gold-ish
	Color c2((BYTE)(alpha * 255), 255, 69, 0);  // orange-red
	LinearGradientBrush brush(bounds, c1, c2, LinearGradientModeHorizontal);

	// Apply transform for scaling centered on centerX,centerY
	Matrix m;
	m.Translate(centerX, centerY);
	m.Scale(scale, scale);
	m.Translate(-centerX, -centerY);
	graphics.SetTransform(&m);

	// Draw outline then fill
	graphics.DrawPath(&pen, &path);
	graphics.FillPath(&brush, &path);

	// reset transform
	graphics.ResetTransform();
}
