#pragma once
#include <windows.h>
#include <string>

class CTextAnim {
public:
	CTextAnim();
	void Start(const std::wstring& text, int durationMs = 1000);
	void Draw(HDC hdc, int centerX, int centerY);
	bool IsActive() const { return m_active; }
private:
	bool m_active;
	unsigned long long m_startTime;
	int m_duration;
	std::wstring m_text;
};
