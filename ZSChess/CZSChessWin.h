#pragma once
#include "CQiPan.h"
#include <windows.h>


class CZSChessWin
{
public:
	CZSChessWin();
	virtual ~CZSChessWin();
	void SetHWND(HWND hWnd) { m_hWnd = hWnd; }
	void OnDraw();
	HWND GetHWND() const { return m_hWnd; }
	bool InitGDIPlus();
	void ShutdownGDIPlus();
	void OnMouseClick(int mouseX, int mouseY);	
	void NewGame() { m_qipan.NewPlay();} // 重置棋盘
	bool BackStep() { return m_qipan.BackStep(); }
	void StopAI();
	// 棋谱：复盘 / 保存 / 加载 / 复制（转发到 CQiPan 公开入口）
	void StepBackRecord() { m_qipan.RecordStepBack(); }
	void StepForwardRecord() { m_qipan.RecordStepForward(); }
	void SaveRecord() { m_qipan.RecordSaveDialog(); }
	void LoadRecord() { m_qipan.RecordLoadDialog(); }
	void CopyPosition() { m_qipan.RecordCopyPosition(); }
	void ContinueQiJu() {
		m_qipan.ContinueQiJu();
	}
	// 棋谱滚动：滚轮 / 拖动
	bool ScrollRecord(int delta, int x, int y) { return m_qipan.ScrollRecord(delta, x, y); }
	void MouseMove(int x, int y) { m_qipan.OnMouseMove(x, y); }
	void MouseUp(int x, int y) { m_qipan.OnMouseUp(x, y); }
	bool IsScrollDragging() { return m_qipan.IsScrollDragging(); }
	// 控制和查询自动 AI 开关
	void SetAutoAI(PieceColor color, bool enable) { m_qipan.SetAutoAI(color, enable); }
	bool IsAutoAI(PieceColor color) const { return m_qipan.IsAutoAI(color); }
	// 转发：在主线程应用 AI 走法
	void ApplyAIMove(const PSF::POS& from, const PSF::POS& to) { m_qipan.ApplyAIMove(from, to); }
	void SetTrainShowMode(bool on) { m_qipan.SetTrainShowMode(on); }
	void SetTrainStepDoneHandle(HANDLE h) { m_qipan.SetTrainStepDoneHandle(h); }
	void PlayWuJie() { m_qipan.PlayWuJie(); } // 播放五节棍动画
private:
	HWND m_hWnd = nullptr;
	ULONG_PTR m_gdiplusToken = 0;
	CQiPan m_qipan{ 800, 600 }; // 默认棋盘大小，可根据窗口大小调整
};


 
