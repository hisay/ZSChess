#pragma once
#include <Windows.h>
#include "CPieceMng.h"
#include "TextAnim.h"
#include "CGameRecord.h"
#include <atomic>
#include <mutex>
#include <memory>
#include "CAICalcAnimal.h"
#include "RepetitionGuard.h"
class CZSAIPlayer; // 前向声明：在 CQiPan 中以智能指针引用 CZSAIPlayer

// 三区布局（按窗口比例计算，随窗口缩放自适应）：
//   [左栏-状态卡] [棋盘区] [右栏-棋谱面板]
struct BoardLayout {
    int finalStartX = 0, finalStartY = 0;  // 棋盘左上角
    int cellSize = 0;
    int boardW = 0, boardH = 0;
    int leftX = 0, leftW = 0;              // 左栏（AI/玩家状态卡）
    int panelX = 0, panelY = 0, panelW = 0, panelH = 0; // 右栏（棋谱面板）
};

 
class CQiPan
{
private:
	int m_rows;
	int m_cols;
	bool m_aiMoving = false;
	int m_winWidth;
	int m_winHeight;
 
	CAICalcAnimal m_aica;

	CPieceMng m_pieceMng;
	// 自动 AI 标志
	bool m_bAutoAIBlack = true; // 默认黑方启用 AI
	bool m_bAutoAIRed = false;
	// AI 正在计算标志
	std::atomic<bool> m_bAIThinking{ false };
	// 待执行的 AI 结果（主线程周期内应用）
	std::mutex m_pendingAIMutex;
	bool m_hasPendingAI = false;
	PSF::POS m_pendingAIFrom{0,0};
	PSF::POS m_pendingAITo{0,0};
	// 辅助函数声明
	void DrawCrossMark(HDC hdc, int x, int y, int size,bool noLeft=false,bool noRight=false);
	void OnClickCell(int row, int col);
	bool PieceCanGoto(CChessPiece* ptr, int x, int y);
	void SetLastMovePos(int col, int row) { m_lastMoveCol = col; m_lastMoveRow = row; }
	void OnPieceMove(CChessPiece* piece, int newCol, int newRow);
	void OnCheckAIGo();
	void CheckAndStartAI();
	void OnCheckJiangJue();

	int m_lastMoveCol = -1;
	int m_lastMoveRow = -1;
	bool m_isMoveedPiece = false;
	PieceColor m_currentTurn = RED; // 当前轮到的玩家，初始为红方

	// ---- 左侧信息栏：每方思考计时（比赛时钟风格）----
	ULONGLONG m_blackTurnStart = 0;   // 黑方当前回合开始思考的时刻 (GetTickCount64)
	ULONGLONG m_redTurnStart = 0;     // 红方当前回合开始思考的时刻
	ULONGLONG m_blackTotalMs = 0;     // 黑方累计思考时间
	ULONGLONG m_redTotalMs = 0;       // 红方累计思考时间
	// 回合开始时调用（color 为即将走棋的一方）
	void BeginTurnTimer(PieceColor color) {
		ULONGLONG nowTick = GetTickCount64();
		if (color == PieceColor::BLACK) m_blackTurnStart = nowTick;
		else m_redTurnStart = nowTick;
	}
	// 回合切换时调用：oldSide 刚走完，newSide 即将走
	void SwitchTurnTimer(PieceColor oldSide, PieceColor newSide) {
		ULONGLONG nowTick = GetTickCount64();
		if (oldSide == PieceColor::BLACK) m_blackTotalMs += nowTick - m_blackTurnStart;
		else m_redTotalMs += nowTick - m_redTurnStart;
		BeginTurnTimer(newSide);
	}
	// 绘制左侧信息栏（头像 / 思考动画 / 计时）
	void DrawSidePanel(HDC hdc, const BoardLayout& L);
	void DrawSideCard(HDC hdc, PieceColor color, bool isAI, bool active, bool inCheck, bool mate,
		int x0, int y0, int w, int h, ULONGLONG nowTick);
	void DrawAvatar(HDC hdc, int cx, int cy, int r, bool isAI, bool active, ULONGLONG nowTick);
	void FormatClock(ULONGLONG ms, wchar_t* buf, int bufLen) const;

	// ---- 三区布局（窗口比例自适应）----
	void ComputeLayout(int w, int h, BoardLayout& L) const;
	void OnMoveOneStep();	//当一步已走完，计算是否将军或将死，切换回合，计时器切换，棋谱记录等
	// ---- 棋谱：记录 / 复盘 / 保存 / 加载 / 复制 ----
	CGameRecord m_record;                 // 棋谱步序列
	int m_reviewIdx = 0;                  // 当前显示步数（0=初始局面，Count=最新一步）
	std::vector<CChessPiece> m_initialBoard; // 初始局面快照（复盘重放基准）
	std::wstring m_recordFile;            // 当前棋谱文件名（自动保存/加载后）
	bool m_recordAutoSaved = false;       // 本局是否已自动保存（终局只保存一次）
	bool m_gameDrawn = false;             // 三活判和：和棋结束（阻止继续走子/AI）
	bool m_trainShowMode = false;         // 训练实时演示模式（--show --train）
	HANDLE m_trainStepDone = nullptr;     // 当前训练步动画完成事件（等待放行）
	// 面板按钮/列表矩形（Draw 时更新，点击时判定）
	RECT m_panelListRect = { 0,0,0,0 };
	RECT m_btnPrev = { 0,0,0,0 }, m_btnNext = { 0,0,0,0 };
	RECT m_btnSave = { 0,0,0,0 }, m_btnLoad = { 0,0,0,0 }, m_btnCopy = { 0,0,0,0 };
	// ---- 棋谱列表滚动：记录超出一屏时，以当前步为准显示有效窗口 ----
	int m_recordTop = 0;          // 列表顶部显示的第几条（0-based 索引）
	bool m_recordFollowBottom = true; // 是否自动跟随最新步（手动滚动时解除，可在全部记录间自由滚动）
	RECT m_scrollTrack = { 0,0,0,0 }; // 滚动条轨道（Draw 时更新）
	int m_recordScrollDrag = -1;  // -1 未拖动；0 拖动中
	int m_recordDragGrabY = 0;    // 拖动起点时鼠标相对滑块顶部的偏移
	void ClampRecordTop(int maxRows); // 约束 m_recordTop 范围，并保证当前步可见
	int RecordMaxRows(const BoardLayout& L) const; // 面板列表可显示行数
	bool HitScrollThumb(int mouseX, int mouseY) const; // 命中滑块（按下开始拖动）
	bool IsReviewing() const { return m_reviewIdx < m_record.Count(); } // 是否处于复盘（非最新步）
	void StepBack();      // 复盘上一步
	void StepForward();   // 复盘下一步
	void JumpToStep(int stepNo);          // 跳转到第 stepNo 步（1-based；0=初始）
	void ApplyReplay();                   // 用棋谱重放到 m_reviewIdx 局面
	void SaveRecordDialog();              // 手动保存（文件对话框）
	void SaveRecordAuto(const std::wstring& result); // 终局自动保存（红胜/黑胜）
	void LoadRecordDialog();              // 加载棋谱（文件对话框）
	void CopyPosition();                  // 复制当前局面文本到剪贴板
	void DrawRecordPanel(HDC hdc, const BoardLayout& L); // 右侧棋谱面板
	bool HitRecordPanel(int mouseX, int mouseY);         // 面板点击处理，返回是否已消费

	// 落子时（动画前）暂存的记法信息，动画完成回合切换时写入棋谱
	std::wstring m_pendingNotation;
	wchar_t m_pendingCaptured[8] = { 0 };

public:
	// --show --train 实时演示：界面只展示训练步，不触发界面 AI/重复保存
	void SetTrainShowMode(bool on) { m_trainShowMode = on; }
	// 训练步的“动画完成”事件句柄：由 WM_APP+102 处理时设置，
	// 在 OnMoveOneStep（真正走完一步）时 SetEvent 放行训练线程。
	void SetTrainStepDoneHandle(HANDLE h) { m_trainStepDone = h; }
	void NotifyTrainStepDone() {
		if (m_trainStepDone) { SetEvent(m_trainStepDone); m_trainStepDone = nullptr; }
	}
	PieceColor CurTurn() {
		return m_currentTurn			;
	}
	void ContinueQiJu();
	bool BackStep() {
		// 复盘模式（不在最新步）：回退为棋谱上一步（重放）
		if (IsReviewing()) {
			StepBack();
			return true;
		}
		// 正常对弈悔棋：快照回退 + 同步删除棋谱最后一步
		if (m_pieceMng.BackStep(m_currentTurn,m_lastMoveCol,m_lastMoveRow))
		{
						m_pieceMng.ClearSelection();
			m_record.Truncate(m_record.Count() - 1);
			RepetitionGuard::instance().Truncate(m_record.Count()); // 长将历史同步截断
			m_reviewIdx = m_record.Count();
			m_recordFollowBottom = true;
			m_recordAutoSaved = false; // 悔棋后局面变化，允许再次自动保存
			// 回退后仍是当前方走棋：重置其本回合计时
			BeginTurnTimer(m_currentTurn);
			return true;
		}
		return false;
	}
	std::vector<CChessPiece>& GetAllPiece(){ return m_pieceMng.AllPieces(); }
	CQiPan( int winWidth, int winHeight)
		: m_rows(MAX_ROWS), m_cols(MAX_COLS),  m_winWidth(winWidth), m_winHeight(winHeight) {
		m_pieceMng.InitPieces();
		m_initialBoard = m_pieceMng.AllPieces();
		extern CQiPan* g_qiPan;
		g_qiPan = this;
		// 红方先手：开启红方计时
		BeginTurnTimer(RED);
		//m_pieceMng.SaveRec(m_currentTurn);
	}
	void NewPlay() {
		// 先取消任何正在进行的 AI 计算
		StopAI();
		m_pieceMng.InitPieces();
		m_initialBoard = m_pieceMng.AllPieces();
		m_lastMoveCol = -1;
		m_lastMoveRow = -1;
		m_currentTurn = RED; // 重置为红方先手
		// 重置双方计时并开启红方计时
		m_blackTotalMs = 0;
		m_redTotalMs = 0;
		BeginTurnTimer(RED);
		// 重置棋谱（必须清空步列表：训练模式每局都是独立新棋局，不能把多局记到同一列表）
		m_record.Clear();
		m_reviewIdx = 0;
		RepetitionGuard::instance().Reset(); // 新局清空长将历史
		m_recordFollowBottom = true;
		m_recordScrollDrag = -1;
		m_recordFile.clear();
		m_recordAutoSaved = false;
		m_gameDrawn = false;
		SetAutoAI(BLACK, true)	; // 默认黑方启用 AI
	}
	// 人类走子前检查：是否构成"长将循环"（杀棋除外）。true=禁止此着法
	bool IsHumanLongCheck(CChessPiece* selectedPiece, int col, int row);
	CChessPiece* GetPieceAtFuncImp(int col, int row, void* userData) {
		return m_pieceMng.FindPiece(col, row);
	}
	CPieceMng& PMng() { return m_pieceMng; }
	static CChessPiece* GetPieceAtFunc(int col, int row, void* userData) {
		if (userData) {
			CQiPan* pQiPan = static_cast<CQiPan*>(userData);
			return pQiPan->GetPieceAtFuncImp(col, row, userData);
		}
		return nullptr;
	}
	void PlayWuJie();
	void Draw(HDC hdc);
	void OnMouseClick(int mouseX, int mouseY);
	void SetSize(int width, int height);
	// 控制自动 AI 开关（公开以供窗口或菜单调用）
	void SetAutoAI(PieceColor color, bool enable);
	bool IsAutoAI(PieceColor color) const;
	// 直接在主线程应用 AI 结果（由消息或主循环调用）
	void ApplyAIMove(const PSF::POS& from, const PSF::POS& to);
	// 取消正在进行的 AI 计算（如果有）
	void StopAI();
	// ---- 公开入口：棋谱控制（供窗口菜单 / 快捷键调用） ----
	void RecordStepBack() { StepBack(); }
	void RecordStepForward() { StepForward(); }
	void RecordSaveDialog() { SaveRecordDialog(); }
	void RecordLoadDialog() { LoadRecordDialog(); }
	void RecordCopyPosition() { CopyPosition(); }
	// ---- 棋谱滚动（供窗口滚轮 / 鼠标拖动调用） ----
	bool ScrollRecord(int deltaLines, int mouseX, int mouseY); // 滚轮：delta 行数（正=向下），仅鼠标在列表/轨道区生效
	bool IsScrollDragging() const { return m_recordScrollDrag >= 0; }
	void OnMouseMove(int mouseX, int mouseY);                  // 拖动滚动条
	void OnMouseUp(int mouseX, int mouseY);                    // 结束拖动
	// Text animation effect
	CTextAnim m_textAnim;
	// 当前正在运行的 AI 对象（智能指针管理生命周期：任务自身持有引用，取消/新局时安全释放）
	std::shared_ptr<CZSAIPlayer> m_pCurrentAI;
};

