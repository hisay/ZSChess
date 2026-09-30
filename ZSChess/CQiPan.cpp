#include "framework.h"
#include "CQiPan.h"
#include <tchar.h>
#pragma once
#include "CPieceMng.h"
#include "CZSAIPlayer.h"
#include "RepetitionGuard.h"
#include "AutoTrainer.h"
#include "AITier.h"
#include "ZSSync.h"
#include <commdlg.h>
CQiPan* g_qiPan = nullptr; // 全局棋盘指针，供规则判断使用
#include "CZSChessWin.h"

#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

// 打开并播放 MP3
void PlayMP3(const wchar_t* filePath) {
    wchar_t cmd[512],cmdPath[512];
    
    GetCurrentDirectory(512, cmd);
    _tcscat_s(cmd, 512, L"\\audio\\");
    _tcscat_s(cmd, 512, filePath);
    mciSendString(((L"close mymp3") ), NULL, 0, NULL);
    _stprintf_s(cmdPath, L"open \"%s\" type mpegvideo alias mymp3", cmd);
    mciSendString(cmdPath, NULL, 0, NULL);       // 打开文件
    mciSendString(L"play mymp3", NULL, 0, NULL); // 开始播放
}
void CQiPan::CheckAndStartAI() {
	// 训练模式下不触发自动 AI
    if (m_trainShowMode) return;
    // 如果正在动画或 AI 正在计算，跳过
    if (m_pieceMng.IsAnimating() || m_bAIThinking.load()) return;

    // 根据当前轮到的玩家，判断是否启用了自动AI
    if (m_currentTurn == PieceColor::BLACK && m_bAutoAIBlack) {
        m_bAIThinking.store(true);
        auto ai = std::make_shared<CZSAIPlayer>();
        // 保存当前 AI 指针以便外部可取消（shared_ptr 管理，任务自身持有引用，安全）
        this->m_pCurrentAI = ai;
        std::vector<CChessPiece> allb = this->GetAllPiece();
        {
            std::wstring rep;
            if (ZSSync::VerifyUiBoardSelf(allb, rep) != 0) {
                ZSSync::LogSyncError(L"玩家对局-黑AI取子", rep);
            }
        }
        ai->FindBestMoveCoordsAsync(BLACK, allb, 3, [this, ai](std::pair<PSF::POS, PSF::POS> res) {
            //// 直接通过 PostMessage 将结果发回主窗口，主线程会处理
            //// 如果搜索被取消，返回的结果为 (-1,-1)，此时不做任何动作
            //if (res.first.col == -1 && res.first.row == -1 && res.second.col == -1 && res.second.row == -1) {
            //    this->m_bAIThinking.store(false);
            //    delete ai;
            //    return;
            //}
            if (g_qiPan) {
                HWND hw = nullptr;
                // 尝试通过窗口类单例获取 HWND
                extern CZSChessWin g_ZSChessWin;
                hw = g_ZSChessWin.GetHWND();
                if (hw) {
                    // pack positions into WPARAM/LPARAM (use MAKELONG for each POS)
                    LPARAM lParam = (LPARAM)MAKELONG(res.first.col, res.first.row);
                    WPARAM wParam = (WPARAM)MAKELONG(res.second.col, res.second.row);
                    PostMessage(hw, WM_APP + 101, wParam, lParam);
                }
            }
            this->m_bAIThinking.store(false);
            //// 清理当前 AI 指针
            //if (this->m_pCurrentAI == ai) this->m_pCurrentAI = nullptr;
            //delete ai;
        });
    }
    else if (m_currentTurn == PieceColor::RED && m_bAutoAIRed) {
        m_bAIThinking.store(true);
        auto ai = std::make_shared<CZSAIPlayer>();
        this->m_pCurrentAI = ai;
        std::vector<CChessPiece> allb = this->GetAllPiece();
        {
            std::wstring rep;
            if (ZSSync::VerifyUiBoardSelf(allb, rep) != 0) {
                ZSSync::LogSyncError(L"玩家对局-红AI取子", rep);
            }
        }
        ai->FindBestMoveCoordsAsync(RED, allb, 3, [this, ai](std::pair<PSF::POS, PSF::POS> res) {
            //if (res.first.col == -1 && res.first.row == -1 && res.second.col == -1 && res.second.row == -1) {
            //    this->m_bAIThinking.store(false);
            //    delete ai;
            //    return;
            //}
            if (g_qiPan) {
                extern CZSChessWin g_ZSChessWin;
                HWND hw = g_ZSChessWin.GetHWND();
                if (hw) {
                    LPARAM lParam = (LPARAM)MAKELONG(res.first.col, res.first.row);
                    WPARAM wParam = (WPARAM)MAKELONG(res.second.col, res.second.row);
                    PostMessage(hw, WM_APP + 101, wParam, lParam);
                }
            }
            this->m_bAIThinking.store(false);
            //if (this->m_pCurrentAI == ai) this->m_pCurrentAI = nullptr;
            //delete ai;
        });
    }
}

void CQiPan::SetAutoAI(PieceColor color, bool enable) {
    if (color == PieceColor::BLACK) m_bAutoAIBlack = enable;
    else m_bAutoAIRed = enable;
    // 如果用户关闭了自动 AI，立即取消正在运行的 AI 计算
    if (!enable) StopAI();
}

void CQiPan::StopAI() {
    // 若有正在运行的 AI，发送取消信号
    if (m_pCurrentAI) {
        m_pCurrentAI->Cancel();
        m_pCurrentAI = nullptr;
    }
    // 立即复位思考标志，不依赖回调（取消后回调会被跳过，若不在此复位会永久卡 true）
    m_bAIThinking.store(false);
}

bool CQiPan::IsAutoAI(PieceColor color) const {
    if (color == PieceColor::BLACK) return m_bAutoAIBlack;
    return m_bAutoAIRed;
}

void CQiPan::ApplyAIMove(const PSF::POS& from, const PSF::POS& to) {
    // 校验着法在棋盘内且来源有子且属于当前走方（双保险：
    // 防止被取消的 AI 残留落子在回退/新局面上触发越界与状态破坏）
    if (from.col < 0 || from.row < 0 || to.col < 0 || to.row < 0 ||
        from.col > 10 || from.row > 10 || to.col > 10 || to.row > 10) {
        if (m_trainShowMode) NotifyTrainStepDone(); // 训练模式不能让训练线程干等
        m_bAIThinking.store(false);
        return;
    }
    CChessPiece* fp = m_pieceMng.FindPiece(from.col, from.row);
    if (!fp || fp->GetColor() != m_currentTurn) {
        if (m_trainShowMode) NotifyTrainStepDone();
        m_bAIThinking.store(false);
        return;
    }
    // 在主线程直接模拟点击以走子
    m_aiMoving = true;
    OnClickCell(from.row, from.col);
    OnClickCell(to.row, to.col);
    m_aiMoving = false;
}

enum E_AudioType {
    AT_MOVE = 0,
    AT_JIANGJUN,
    AT_JIANGSHI,
    AT_CHIZHI
};

void PlayAudio(E_AudioType at) {
    switch (at) {
    case AT_MOVE:
        PlayMP3(L"move.mp3");
        break;
    case AT_JIANGJUN:
        PlayMP3(L"jiangjun.mp3");
        break;
    case AT_JIANGSHI:
        PlayMP3(L"jiangshi.mp3");
        break;
    case AT_CHIZHI:
        PlayMP3(L"eat.mp3");
        break;
    }
}
 
void CQiPan::SetSize(int width, int height) {
	m_winWidth = width;
	m_winHeight = height;
}

// ==================== 三区布局（窗口比例自适应） ====================
// 布局： [左栏-状态卡] [棋盘区] [右栏-棋谱面板]
// 左栏/右栏宽度按窗口宽度比例预留，棋盘在中间区域按格子数取整并居中，
// 因此窗口任意缩放（最大化/拖动）都不会错位或重叠。
void CQiPan::ComputeLayout(int w, int h, BoardLayout& L) const {
	double padX = w * 0.11;
	double padY = h * 0.11;
	int gap = 14;

	int leftW = (int)(w * 0.16);
	if (leftW < 150) leftW = 150;
	if (leftW > 260) leftW = 260;
	int rightW = (int)(w * 0.17);
	if (rightW < 185) rightW = 185;
	if (rightW > 300) rightW = 300;

	int boardAreaX = (int)padX + leftW + gap;
	int boardAreaW = w - (int)padX - leftW - gap - rightW - gap;
	int boardAreaH = h - (int)padY * 2;
	if (boardAreaW < 60) boardAreaW = 60;
	if (boardAreaH < 60) boardAreaH = 60;

	int cell = (boardAreaW / 10 < boardAreaH / 9) ? boardAreaW / 10 : boardAreaH / 9;
	// 面板间距约束：棋盘右缘 + 至少一个 cell 的空隙，棋子不碰右侧面板
	int panelX0 = w - rightW - gap;
	int maxCellByPanel = (panelX0 - boardAreaX - boardAreaW / 2) / 6;
	if (maxCellByPanel < cell) cell = maxCellByPanel;
	if (cell < 12) cell = 12;

	L.cellSize = cell;
	L.boardW = cell * 10;
	L.boardH = cell * 10;
	L.finalStartX = boardAreaX + (boardAreaW - L.boardW) / 2;
	L.finalStartY = (int)padY + (boardAreaH - L.boardH) / 2;

	L.leftX = (int)padX;
	L.leftW = leftW;
	L.panelX = w   - rightW - gap;
	L.panelY = (int)padY;
	L.panelW = rightW;
	L.panelH = boardAreaH;
}
// 开始新局：先取消正在进行的 AI 计算以便立即响应用户操作，然后初始化棋子并重置状态
// NewPlay 已在头文件中内联定义，这里移除重复的实现以避免重复定义。

bool CQiPan::PieceCanGoto(CChessPiece* ptr, int x, int y) {
    if (ptr) {
        int px = ptr->GetX();
        int py = ptr->GetY();
        auto clr = ptr->GetColor();
        E_PieceType pt = ptr->GetType();
        PSF::CChessRule rule;
        if (!m_pieceMng.IsLegalMove(ptr, x, y)) return false;

		return rule.CanGotoPos(x,y, ptr, PSF::CChessRule::GetPieceAtFunc([](int col, int row, void* userData) -> CChessPiece* {
			CQiPan* pThis = static_cast<CQiPan*>(userData);
			return pThis->m_pieceMng.FindPiece(col, row);
			}), this);
    }
    return false;
}
void CQiPan::PlayWuJie() {
	this->m_textAnim.Start(L"无解局面", 2000);
	m_pCurrentAI.reset();
	// 无解 = 当前方已无棋可走（困毙/将死），该方判负 -> 自动保存棋谱（命名含胜负）
	if (!m_recordAutoSaved && m_record.Count() > 0) {
		m_recordAutoSaved = true;
		std::wstring result = (m_currentTurn == RED) ? L"黑胜" : L"红胜";
		SaveRecordAuto(result);
	}
}
void CQiPan::OnClickCell(int row, int col)
{
    if (m_gameDrawn) {
        return;
    }

    if (m_currentTurn == PieceColor::RED && m_pieceMng.IsCheckmate(PieceColor::RED)) {
        return;
    }

    if (m_currentTurn == PieceColor::BLACK && m_pieceMng.IsCheckmate(PieceColor::BLACK)) {
        return;
    }


    auto x = m_pieceMng.FindPiece(col, row);
    if (x) {
		auto selectedPiece = m_pieceMng.GetSelectedPiece();
		if (selectedPiece) {
			if (selectedPiece == x) {
				// 如果点击的是已经选中的棋子，取消选择
				selectedPiece->SetSelected(false);
				m_pieceMng.ClearSelection();
				return;
			}
			if (x->GetColor() == selectedPiece->GetColor()) {
				// 如果点击的是同一颜色的棋子，切换选中状态
				m_pieceMng.ClearSelection();
				x->SetSelected(true);
				return;
			}
            // 训练展示模式：不走棋
            if (m_trainShowMode && !m_aiMoving) return;
            // 界面不再做第二次禁招校验，防止规则判定差异导致落子被拒、动画无法推进
            if (!this->PieceCanGoto(selectedPiece, col, row)) return;
            // 长将循环禁止（人类同样遵守：连将导致局面重复必须变招；杀棋除外）
            if (!m_trainShowMode && IsHumanLongCheck(selectedPiece, col, row)) {
                m_textAnim.Start(L"长将循环，禁止此着法", 1600);
                return;
            }
            // 发起移动动画，目标棋子保留到动画结束时再移除
            m_pieceMng.SaveRec(m_currentTurn,m_lastMoveCol,m_lastMoveRow);
            m_pieceMng.SetPieceMoved(selectedPiece, true);
            SetLastMovePos(selectedPiece->GetX(), selectedPiece->GetY());
            // 落子前生成棋谱记法（走子前局面可用）
            m_pendingCaptured[0] = 0;
            if (x && x->GetColor() != selectedPiece->GetColor()) {
                const wchar_t* cl = x->GetLabel();
                if (cl) wcsncpy_s(m_pendingCaptured, cl, 7);
            }
            m_pendingNotation = CGameRecord::MakeNotation(m_pieceMng.AllPieces(), m_currentTurn,
                selectedPiece->GetX(), selectedPiece->GetY(), col, row, m_pendingCaptured);
            m_pieceMng.StartMoveAnimation(selectedPiece, col, row);
            m_pieceMng.ClearSelection();
            PlayAudio(AT_CHIZHI);
		}
		else {
			if (m_currentTurn != x->GetColor()) return;
			// 没有选中的棋子，选择当前点击的棋子
			m_pieceMng.ClearSelection(); // 清除之前的选择
			x->SetSelected(true);
		}
		//wchar_t buffer[100];
		//swprintf_s(buffer, L"Clicked on piece: (%d, %d)[%d,%d] Label: %s", col, row, x->GetX(), x->GetY(),x->GetLabel());
		//MessageBoxW(nullptr, buffer, L"Piece Clicked", MB_OK);

    }
    else {
		auto selectedPiece = m_pieceMng.GetSelectedPiece();
        if (selectedPiece) {
			if (x == selectedPiece) {
				// 如果点击的是已经选中的棋子，取消选择
				selectedPiece->SetSelected(false);
				m_pieceMng.ClearSelection();
				return;
			}

            if(m_trainShowMode && !m_aiMoving) return;

            if (!this->PieceCanGoto(selectedPiece, col, row)) return;
            // 长将循环禁止（人类同样遵守）
            if (!m_trainShowMode && IsHumanLongCheck(selectedPiece, col, row)) {
                m_textAnim.Start(L"长将循环，禁止此着法", 1600);
                return;
            }
            m_pieceMng.SaveRec(m_currentTurn,m_lastMoveCol,m_lastMoveRow);
            m_pieceMng.SetPieceMoved(selectedPiece, true);
            SetLastMovePos(selectedPiece->GetX(), selectedPiece->GetY());
            // 落子前生成棋谱记法（走子前局面可用）
            m_pendingCaptured[0] = 0;
            m_pendingNotation = CGameRecord::MakeNotation(m_pieceMng.AllPieces(), m_currentTurn,
                selectedPiece->GetX(), selectedPiece->GetY(), col, row, nullptr);
            m_pieceMng.StartMoveAnimation(selectedPiece, col, row);
            m_pieceMng.ClearSelection();
            PlayAudio(AT_MOVE);
        }
    }
 
}
bool CQiPan::IsHumanLongCheck(CChessPiece* selectedPiece, int col, int row) {
    if (!selectedPiece) return false;
    // 模拟走子：移除目标、移动源
    std::vector<CChessPiece> tmp = m_pieceMng.AllPieces();
    for (auto it = tmp.begin(); it != tmp.end();) {
        if (it->GetX() == col && it->GetY() == row) { it = tmp.erase(it); break; }
        else ++it;
    }
    for (auto& q : tmp) {
        if (q.GetX() == selectedPiece->GetX() && q.GetY() == selectedPiece->GetY()
            && q.GetColor() == selectedPiece->GetColor()) {
            q.SetX(col); q.SetY(row); break;
        }
    }
    PieceColor after = (PieceColor)(1 - m_currentTurn);
    zschess::Position rp = CZSAIPlayer::BoardToPosition(tmp, after);
    zschess::Move rml[256];
    if (rp.generate_legal_moves(rml) == 0) return false; // 将死对方→允许（杀棋优先）
    return RepetitionGuard::instance().IsLongCheckCycle(rp.hash(), rp.in_check());
}

void CQiPan::OnPieceMove(CChessPiece* piece, int newCol, int newRow) {
    // 使用动画形式完成移动，最终变更在动画完成后提交
    m_pieceMng.SaveRec(m_currentTurn,m_lastMoveCol,m_lastMoveRow);
    m_pieceMng.SetPieceMoved(piece, true);
    SetLastMovePos(piece->GetX(), piece->GetY());
    m_pieceMng.StartMoveAnimation(piece, newCol, newRow);
    

}
void CQiPan::OnCheckJiangJue() {
    if (m_isMoveedPiece) {
        if (m_pieceMng.IsCheckmate(m_currentTurn)) {
            PlayAudio(AT_JIANGSHI);
        }
        else if (m_pieceMng.IsInCheck(m_currentTurn)) {
            PlayAudio(AT_JIANGJUN);
        }
    }
}
void CQiPan::OnCheckAIGo() {
    // 在每帧末尾检查并启动 AI（如果轮到 AI 且未在动画/计算）
    CheckAndStartAI();
}
void CQiPan::OnMouseClick(int mouseX, int mouseY) {
	// 玩家点击棋盘不马上打断 AI：选子/试走不应中断其思考。
	// 仅当人类替 AI 真正完成一步走子（回合切换）时才取消，详见下文格子命中处。
	// 0. 三区布局（窗口比例自适应）
	BoardLayout L;
	ComputeLayout(m_winWidth, m_winHeight, L);
	int finalStartX = L.finalStartX;
	int finalStartY = L.finalStartY;
	int m_cellSize = L.cellSize;

	// 0.5 右侧棋谱面板优先响应（按钮/列表点击）
	if (HitRecordPanel(mouseX, mouseY)) return;

	int radius = m_cellSize / 2 - 3;

	for (int r = 0; r <= m_rows; r++) {
		for (int c = 0; c <= m_cols; c++) {
			int cellCenterX = finalStartX + c * m_cellSize;
			int cellCenterY = finalStartY + r * m_cellSize;

			if (mouseX >= cellCenterX - radius && mouseX <= cellCenterX + radius &&
				mouseY >= cellCenterY - radius && mouseY <= cellCenterY + radius) {
                m_isMoveedPiece = false;
                // 人类替 AI 走棋：仅当 AI 正在计算且本次点击真正完成一步走子（回合切换）时
                // 才取消 AI ——选子/试探点击不打断 AI 思考。
                bool aiThinkingBefore = m_bAIThinking.load();
                PieceColor turnBefore = m_currentTurn;
                OnClickCell(r, c);
                if (m_currentTurn != turnBefore && aiThinkingBefore) {
                    // 玩家替 AI 完成了走子：旧 AI 基于旧局面，其结果必须作废
                    // （Cancel 后回调被 token 检查丢弃，不会再落子）
                    StopAI();
                    // 取消后若新回合轮到 AI 且已开启自动 AI，则启动新的 AI 计算
                    CheckAndStartAI();
                }
                OnCheckJiangJue();
                //OnCheckAIGo();
				//// 点击在当前格子范围内
				//wchar_t buffer[100];
				//swprintf_s(buffer, L"Clicked on cell: (%d, %d)", c, r);
				//MessageBoxW(nullptr, buffer, L"Cell Clicked", MB_OK);
				return; // 找到后直接返回
			}


			// 这里可以添加调试输出，查看每个格子的中心坐标
			// wprintf(L"Cell (%d, %d) center: (%d, %d)\n", c, r, cellCenterX, cellCenterY);
		}
	}

 //   
	//// 4. 将鼠标点击坐标转换为棋盘格子索引
	//if (mouseX < finalStartX - radius || mouseX > finalStartX + boardTotalWidth + radius ||
	//	mouseY < finalStartY - radius || mouseY > finalStartY + boardTotalHeight + radius) {
	//	// 点击在棋盘外部，忽略
	//	return;
	//}
	//int colIndex = (mouseX - finalStartX) / m_cellSize;
	//int rowIndex = (mouseY - finalStartY) / m_cellSize;
	//// 输出点击的格子索引
	//wchar_t buffer[100];
	//swprintf_s(buffer, L"Clicked on cell: (%d, %d)", colIndex, rowIndex);
	//MessageBoxW(nullptr, buffer, L"Cell Clicked", MB_OK);
}

void CQiPan::ContinueQiJu() {
    this->m_gameDrawn = false;
    RepetitionGuard::instance().clear();
}
void CQiPan::Draw(HDC hdc)
{
    // 0. 三区布局：左栏状态卡 / 棋盘区 / 右栏棋谱面板（窗口比例自适应）
    BoardLayout L;
    ComputeLayout(m_winWidth, m_winHeight, L);
    int finalStartX = L.finalStartX;
    int finalStartY = L.finalStartY;
    int m_cellSize = L.cellSize;

    // 1. 创建黑色画笔
    HPEN hPen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

    int boardTotalWidth = m_cellSize * m_cols;
    int boardTotalHeight = m_cellSize * m_rows;

    //棋盘最外边框绘制成双层边框
    {
        RECT rect = { finalStartX -3, finalStartY - 3, finalStartX + boardTotalWidth + 3, finalStartY + boardTotalHeight + 3 };
        FrameRect(hdc, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
    }

    // 5. 绘制横线 (共 rows + 1 = 11 条)
    for (int i = 0; i <= m_rows; ++i)
    {
        int y = finalStartY + i * m_cellSize;
        MoveToEx(hdc, finalStartX, y, nullptr);
        LineTo(hdc, finalStartX + boardTotalWidth, y);
    }

    // 6. 绘制竖线 (共 cols + 1 = 11 条)
    // 修复：上半部分画到河界上边，下半部分从河界下边开始
    for (int j = 0; j <= m_cols; ++j)
    {
        int x = finalStartX + j * m_cellSize;

        // 上半部分竖线：从第0行画到第4行
        MoveToEx(hdc, x, finalStartY, nullptr);
        LineTo(hdc, x, finalStartY + (m_rows / 2-1) * m_cellSize);

        // 下半部分竖线：从第5行画到第9行
        MoveToEx(hdc, x, finalStartY + (m_rows / 2 + 1) * m_cellSize, nullptr);
        LineTo(hdc, x, finalStartY + boardTotalHeight);
    }

    // 7. 绘制河界两侧的封口线 (连接第0列和第10列的河界缺口)
    MoveToEx(hdc, finalStartX, finalStartY + 4 * m_cellSize, nullptr);
    LineTo(hdc, finalStartX, finalStartY + 6 * m_cellSize);

    MoveToEx(hdc, finalStartX + boardTotalWidth, finalStartY + 4 * m_cellSize, nullptr);
    LineTo(hdc, finalStartX + boardTotalWidth, finalStartY + 6 * m_cellSize);

    // 8. 绘制"楚河汉界"文字
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 0, 0));

    HFONT hFont = CreateFontW(
        int(m_cellSize * 0.6), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"SimSun");
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

    TEXTMETRIC tm;
    GetTextMetrics(hdc, &tm);
    int fontHeight = tm.tmHeight;
    int riverCenterY = finalStartY + (m_rows / 2  ) * m_cellSize;

    // 由于列数变宽，调整文字位置使其居中分布
    TextOutW(hdc, finalStartX + m_cellSize * 2, riverCenterY - fontHeight / 2, L"古今", 2);
    TextOutW(hdc, finalStartX + m_cellSize * 6, riverCenterY - fontHeight / 2, L"河界", 2);

    // 9. 绘制"九宫格"斜线
    // 规则：帅在6路(索引5)，九宫格范围应为 4路-6路(索引3-5)
    // 上方九宫格：(3,0) -> (5,2) 和 (5,0) -> (3,2)
    MoveToEx(hdc, finalStartX + 4 * m_cellSize, finalStartY, nullptr);
    LineTo(hdc, finalStartX + 6 * m_cellSize, finalStartY + 2 * m_cellSize);
    MoveToEx(hdc, finalStartX + 6 * m_cellSize, finalStartY, nullptr);
    LineTo(hdc, finalStartX + 4 * m_cellSize, finalStartY + 2 * m_cellSize);
    
    MoveToEx(hdc, finalStartX + 5 * m_cellSize, finalStartY, nullptr);
    LineTo(hdc, finalStartX + 4 * m_cellSize, finalStartY + 1 * m_cellSize);
    MoveToEx(hdc, finalStartX + 5 * m_cellSize, finalStartY, nullptr);
    LineTo(hdc, finalStartX + 6 * m_cellSize, finalStartY + 1 * m_cellSize);
    MoveToEx(hdc, finalStartX + 4 * m_cellSize, finalStartY +1 * m_cellSize, nullptr);
    LineTo(hdc, finalStartX + 5 * m_cellSize, finalStartY + 2 * m_cellSize);
    MoveToEx(hdc, finalStartX + 6 * m_cellSize, finalStartY + 1 * m_cellSize, nullptr);
    LineTo(hdc, finalStartX + 5 * m_cellSize, finalStartY + 2 * m_cellSize);


    // 下方九宫格：(3,7) -> (5,9) 和 (5,7) -> (3,9)
    MoveToEx(hdc, finalStartX + 4 * m_cellSize, finalStartY + 8 * m_cellSize, nullptr);
    LineTo(hdc, finalStartX + 6 * m_cellSize, finalStartY + 10 * m_cellSize);
    MoveToEx(hdc, finalStartX + 6 * m_cellSize, finalStartY + 8 * m_cellSize, nullptr);
    LineTo(hdc, finalStartX + 4 * m_cellSize, finalStartY + 10 * m_cellSize);

    MoveToEx(hdc, finalStartX + 5 * m_cellSize, finalStartY + 8 * m_cellSize, nullptr);
    LineTo(hdc, finalStartX + 4 * m_cellSize, finalStartY + 9 * m_cellSize);
    MoveToEx(hdc, finalStartX + 5 * m_cellSize, finalStartY + 8 * m_cellSize, nullptr);
    LineTo(hdc, finalStartX + 6 * m_cellSize, finalStartY + 9 * m_cellSize);

    MoveToEx(hdc, finalStartX + 4 * m_cellSize, finalStartY + 9 * m_cellSize, nullptr);
    LineTo(hdc, finalStartX + 5 * m_cellSize, finalStartY + 10 * m_cellSize);
    MoveToEx(hdc, finalStartX + 6 * m_cellSize, finalStartY + 9 * m_cellSize, nullptr);
    LineTo(hdc, finalStartX + 5 * m_cellSize, finalStartY + 10 * m_cellSize);

    // 10. 绘制自定义兵/卒的位置标记 (1,3,5,6,7,9,11线 -> 索引 0,2,4,5,6,8,10)
    // 兵的位置在第3行(索引2)和第8行(索引7)
    int soldierIndices[] = { 0, 2, 4, 5,  6, 8, 10 };
    int soldierCount = 7;
    //int soldierIndices[] = { 0, 2,  5,  8, 10 };
    //int soldierCount = 5;
    // 绘制上方兵的十字标记
    for (int k = 0; k < soldierCount; ++k) {
        int idx = soldierIndices[k];
        int cx = finalStartX + idx * m_cellSize;
        int cy = finalStartY + 3 * m_cellSize;
        DrawCrossMark(hdc, cx, cy, m_cellSize, 0==idx, idx==10);
    }

    // 绘制下方兵的十字标记
    for (int k = 0; k < soldierCount; ++k) {
        int idx = soldierIndices[k];
        int cx = finalStartX + idx * m_cellSize;
        int cy = finalStartY + 7 * m_cellSize;
        DrawCrossMark(hdc, cx, cy, m_cellSize, 0==idx, idx==10);
    }

    // 11. 绘制自定义炮的位置标记 (第6路索引5, 第3线索引2 和 第8线索引7)
    // 上方炮 (5, 2)
   // DrawCrossMark(hdc, finalStartX + 5 * m_cellSize, finalStartY + 2 * m_cellSize, m_cellSize);

    DrawCrossMark(hdc, finalStartX + 1 * m_cellSize, finalStartY + 2 * m_cellSize, m_cellSize);
    DrawCrossMark(hdc, finalStartX + 9 * m_cellSize, finalStartY + 2 * m_cellSize, m_cellSize);
    
    DrawCrossMark(hdc, finalStartX + 5 * m_cellSize, finalStartY + 2 * m_cellSize, m_cellSize);
    
    // 下方炮 (5, 7)
   // DrawCrossMark(hdc, finalStartX + 5 * m_cellSize, finalStartY + 7 * m_cellSize, m_cellSize);
    DrawCrossMark(hdc, finalStartX + 1 * m_cellSize, finalStartY + 8 * m_cellSize, m_cellSize);
    DrawCrossMark(hdc, finalStartX + 5 * m_cellSize, finalStartY + 8 * m_cellSize, m_cellSize);
    DrawCrossMark(hdc, finalStartX + 9 * m_cellSize, finalStartY + 8 * m_cellSize, m_cellSize);

    // 12. 清理 GDI 资源
    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);


      hFont = CreateFontW(
        int(m_cellSize * 0.2), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"楷体");
      hOldFont = (HFONT)SelectObject(hdc, hFont);
      GetTextMetrics(hdc, &tm);
      fontHeight = tm.tmHeight;
    SetTextColor(hdc, RGB(0, 0, 0));

    // 1. 绘制顶部竖线坐标 (从右到左: 一, 二, ..., 十一)
    const wchar_t* colLabels[] = { L"十一", L"十", L"九", L"八", L"七", L"六", L"五", L"四", L"三", L"二", L"一" };
    int labelPadding = 3; // 文字与棋盘线的间距
    for (int i = 0; i <= m_cols; ++i)
    {
        int x = finalStartX + (m_cols - i) * m_cellSize;
        // 文字垂直居中于其所在列
        int textX = x - tm.tmAveCharWidth; // 近似居中，因为汉字宽度约等于两个英文字符
        int textY = finalStartY - fontHeight - labelPadding;

        wchar_t rowLabel[4];
        swprintf_s(rowLabel, _countof(rowLabel), L"%d", m_cols-i+1);

        TextOutW(hdc, textX, textY, rowLabel, (int)wcslen(rowLabel));

        x = finalStartX + i * m_cellSize;
        // 文字垂直居中于其所在列
        textX = x - tm.tmAveCharWidth; // 近似居中，因为汉字宽度约等于两个英文字符
        textY = finalStartY + boardTotalHeight + fontHeight + labelPadding;
        TextOutW(hdc, textX, textY, colLabels[i], (int)wcslen(colLabels[ i]));
    }

    //// 2. 绘制左侧横线坐标 (从上到下: 1, 2, ..., 9)
    //for (int i = 1; i <= m_rows; ++i)
    //{
    //    int y = finalStartY + (i-1) * m_cellSize;
    //    // 文字水平居中对齐其所在行
    //    wchar_t rowLabel[4];
    //    swprintf_s(rowLabel, _countof(rowLabel), L"%d", i);
    //    int textX = finalStartX - tm.tmAveCharWidth - labelPadding;
    //    int textY = y - fontHeight / 2;
    //    TextOutW(hdc, textX, textY, rowLabel, wcslen(rowLabel));
    //}
    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);

    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);





    //左侧信息栏：双方头像（AI/人类）、思考动画、计时器（比赛时钟风格）
    DrawSidePanel(hdc, L);
    // 右侧棋谱面板：步数列表 + 上一步/下一步 + 保存/加载/复制
    DrawRecordPanel(hdc, L);

    // 自动训练状态条（窗口底部）
    {
        AutoTrainer& tr = GetAutoTrainer();
        if (tr.Running()) {
            wchar_t buf[200];
            int ns0 = zschess::NNLib::instance().Samples(0);
            int ns5 = zschess::NNLib::instance().Samples(5);
            swprintf_s(buf, L"自动训练  %s(红) vs %s(黑)  第%d/%d局  红胜%d 黑胜%d 和%d  神经网络样本%d  %s",
                TierName(tr.RedTier()), TierName(tr.BlackTier()),
                tr.GamesPlayed(), tr.TargetGames(),
                tr.RedWins(), tr.BlackWins(), tr.Draws(),
                zschess::NNLib::instance().TotalSamples(), tr.LastResult().c_str());
            HFONT hF = CreateFontW(20, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Microsoft YaHei");
            HFONT hOldF = (HFONT)SelectObject(hdc, hF);
            int oldMode = SetBkMode(hdc, TRANSPARENT);
            COLORREF oldCol = SetTextColor(hdc, RGB(240, 90, 30));
            RECT rc;
            rc.left = L.finalStartX;
            rc.top = m_winHeight - 20;
            rc.right = L.panelX + L.panelW;
            rc.bottom = m_winHeight - 8;
            DrawTextW(hdc, buf, -1, &rc, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
            SetTextColor(hdc, oldCol);
            SetBkMode(hdc, oldMode);
            SelectObject(hdc, hOldF);
            DeleteObject(hF);
        }
    }

    m_pieceMng.DrawPieces(hdc, finalStartX, finalStartY,  m_cellSize);

    // 如果有动画完成，消费并完成回合切换与音效
    int lf, lr, ltc, ltr; bool wasCap; PieceColor movedColor;
    if (m_pieceMng.ConsumeLastMove(lf, lr, ltc, ltr, wasCap, movedColor)) {
        // 复盘节点落子：截断该节点之后的旧棋谱，更新为最新棋谱
        if (m_reviewIdx < m_record.Count()) {
            m_record.Truncate(m_reviewIdx);
            m_recordFile.clear(); // 内容已变化，不再对应已保存文件
        }
        // 完成回合切换
        m_currentTurn = (movedColor == RED) ? BLACK : RED;
        // 计时切换：累计刚走完一方用时，开启新一方计时
        SwitchTurnTimer((movedColor == RED) ? RED : BLACK, m_currentTurn);
        // 播放音效
        if (wasCap) PlayAudio(AT_CHIZHI);
        else PlayAudio(AT_MOVE);
        // 标记已移动，用于提示检查将军/将死
        m_isMoveedPiece = true;
        OnCheckJiangJue();
        // 文本特效：根据本次移动显示提示
        std::wstring msg;
        if (wasCap) {
            const wchar_t* lbl = m_pieceMng.GetLastCapturedLabel();
            if (lbl && lbl[0]) {
                msg = L"吃"; msg += lbl;
            } else {
                msg = L"吃子";
            }
            m_textAnim.Start(msg, 1200);
        } 
        // 将军/将死 检测结果（用于棋谱标记）
        bool mateFlag = false, checkFlag = false;
        {
            // 检查是否将军或将死或双重将
            // 当前轮到的玩家是被将的一方（刚移动的一方是 movedColor）
            PieceColor victim = m_currentTurn;
            if (m_pieceMng.IsCheckmate(victim)) {
                mateFlag = true;
                msg = L"绝杀";
                m_textAnim.Start(msg, 1400);
            } else if (m_pieceMng.IsInCheck(victim)) {
                checkFlag = true;
                // 统计攻击者数量，判断是否双重将
                auto king = m_pieceMng.findKing(victim);
                int attackers = 0;
                if (king) {
                    int kx = king->GetX(); int ky = king->GetY();
                    for (auto& p : m_pieceMng.AllPieces()) {
                        if (p.GetColor() == movedColor) {
                            if (PSF::CChessRule::CanGotoPos(kx, ky, const_cast<CChessPiece*>(&p), CQiPan::GetPieceAtFunc, this)) {
                                attackers++;
                            }
                        }
                    }
                }
                if (attackers >= 2) msg = L"双重将"; else msg = L"将军";
                m_textAnim.Start(msg, 1200);
            }
        }

        // ---- 记入棋谱 ----
        {
            RecordStep rs;
            rs.number = m_record.Count() + 1;
            rs.side = movedColor;
            rs.fromCol = lf; rs.fromRow = lr;
            rs.toCol = ltc; rs.toRow = ltr;
            rs.wasCapture = wasCap;
            if (wasCap) {
                if (m_pendingCaptured[0]) wcsncpy_s(rs.capturedLabel, m_pendingCaptured, 7);
                else {
                    const wchar_t* lbl = m_pieceMng.GetLastCapturedLabel();
                    if (lbl) wcsncpy_s(rs.capturedLabel, lbl, 7);
                }
            }
            rs.inCheck = checkFlag;
            rs.mate = mateFlag;
            // 评估走完后的局面（红方视角：正=红优 负=黑优）——仅运行时显示，不写入棋谱文件
            rs.evalScore = CZSAIPlayer::EvaluateBoardScore(m_pieceMng.AllPieces(), (PieceColor)(1 - movedColor));
            // 中文记法（落子时已按走子前局面生成）
            std::wstring notate = m_pendingNotation;
            if (notate.empty()) {
                swprintf_s(rs.notation, L"%s %d,%d-%d,%d",
                    (movedColor == RED) ? L"红" : L"黑", lf, lr, ltc, ltr);
            } else {
                if (rs.inCheck) notate += L" 将军";
                if (rs.mate) notate += L" 绝杀";
                            wcsncpy_s(rs.notation, notate.c_str(), 47);
            }
            m_record.Append(rs);
            m_reviewIdx = m_record.Count();
            m_recordFollowBottom = true;
            // 长将历史与棋谱同步：先截断旧分支，再记录当前局面
            RepetitionGuard::instance().Truncate(m_record.Count() - 1);
            {
                zschess::Position rp = CZSAIPlayer::BoardToPosition(m_pieceMng.AllPieces(), m_currentTurn);
                RepetitionGuard::instance().PushPosition(rp);
                // 合法重复（一将一捉 / 双方都不愿变招，且非长将长捉）同一局面第3次形成 → 和棋
                if (!m_trainShowMode && RepetitionGuard::instance().IsThreefoldDraw(rp.hash())) {
                    m_gameDrawn = true;
                    this->m_textAnim.Start(L"重复局面判和", 3000);
                    if (!m_recordAutoSaved) {
                        m_recordAutoSaved = true;
                        SaveRecordAuto(L"和棋");
                    }
                }
            }
        }

        // 终局：被将死/困毙（无棋可走）→ 自动保存棋谱（命名含胜负）
        if (mateFlag && !m_recordAutoSaved) {
            m_recordAutoSaved = true;
            std::wstring result = (m_currentTurn == RED) ? L"黑胜" : L"红胜";
            SaveRecordAuto(result);
        }
        OnMoveOneStep();
    }

    // 消费到一次完整的走子（动画）并切换回合后，如果当前轮到的是 AI 并且已开启自动 AI，则触发一次 AI 计算
    // 复盘模式（不在最新步）不触发 AI，等玩家在棋盘上落子接管后自然恢复
    if (!IsReviewing() && !m_gameDrawn && !m_pieceMng.IsCheckmate(m_currentTurn)&& ((m_currentTurn == PieceColor::BLACK && m_bAutoAIBlack) || (m_currentTurn == PieceColor::RED && m_bAutoAIRed))) {
        if(!m_pCurrentAI)
        CheckAndStartAI();
    }



	//// 规则说明：移到棋盘底部（避免与左侧信息栏冲突）不再显示规则说明
    //{
    //    HFONT hRuleFont = CreateFontW(
    //        12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
    //        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
    //        DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"SimSun");
    //    HFONT hOldRuleFont = (HFONT)SelectObject(hdc, hRuleFont);
    //    SetTextColor(hdc, RGB(120, 80, 40));
    //    int ruleY = finalStartY + boardTotalHeight + fontHeight * 2 + labelPadding * 2;
    //    TextOutW(hdc, finalStartX, ruleY, L"仕/士:斜线无距离限制可过河  像/相:行田可过河  后:九宫内横竖斜无限制", 32);
    //    TextOutW(hdc, finalStartX, ruleY + 16, L"军:横竖斜走一格  督:横竖斜无限制  兵:过河后可横走", 26);
    //    SelectObject(hdc, hOldRuleFont);
    //    DeleteObject(hRuleFont);
    //}


    //draw last move rect
	if (m_lastMoveCol != -1) {
        int lastCol = m_lastMoveCol;
        int lastRow = m_lastMoveRow;
		int rectX = finalStartX + lastCol * m_cellSize ; // -2 for a slight offset
		int rectY = finalStartY + lastRow * m_cellSize ; // -2 for a slight offset
        int rectSize = m_cellSize / 4;// +4; // +4 to cover the cell
		//HPEN hRectPen = CreatePen(PS_SOLID, 2, RGB(255, 0, 0)); // Red pen
		//HPEN hOldRectPen = (HPEN)SelectObject(hdc, hRectPen);
        auto hpen = SelectObject(hdc, GetStockObject(NULL_PEN));
        ::Ellipse(hdc, rectX - rectSize / 2, rectY - rectSize / 2, rectX + rectSize / 2, rectY + rectSize / 2);
        //
        SelectObject(hdc, hpen);

        rectSize = m_cellSize / 3;
        auto hbrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            ;
            hpen = SelectObject(hdc, GetStockObject(WHITE_PEN));

        ::Ellipse(hdc, rectX - rectSize / 2, rectY - rectSize / 2, rectX + rectSize / 2, rectY + rectSize / 2);
        SelectObject(hdc, hpen);
        SelectObject(hdc, hbrush);

		//HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(WHITE_PEN)); // No fill
  //      RECT r(rectX - rectSize / 2, rectY - rectSize / 2, rectX + rectSize / 2, rectY + rectSize / 2);
		//FrameRect(hdc,&r, HBRUSH(GetStockObject(WHITE_BRUSH)));

		//SelectObject(hdc, hOldBrush);
		//SelectObject(hdc, hOldRectPen);
		//DeleteObject(hRectPen);
	}
	//根据选中棋子绘制可移动位置标记
    {
		auto curSelectedPiece = m_pieceMng.GetSelectedPiece();
        if (curSelectedPiece)
        {
            std::vector<PSF::POS> possibleMoves;
            PSF::CChessRule rule;
            possibleMoves = rule.ListCanGotoPos(curSelectedPiece, PSF::CChessRule::GetPieceAtFunc([](int col, int row, void* userData) -> CChessPiece* {
                CQiPan* pThis = static_cast<CQiPan*>(userData);
                return pThis->m_pieceMng.FindPiece(col, row);
                }), this);
            for (const auto& move : possibleMoves) {
                int cx = finalStartX + move.col * m_cellSize;
                int cy = finalStartY + move.row * m_cellSize;

                // 在棋盘上绘制一个小圆点，在棋子大小的1/3
                int dotRadius = m_cellSize / 6; // 直径为 cellSize/3，所以半径为 cellSize/6
                if (dotRadius < 2) dotRadius = 2; // 防止格子太小时圆点不可见

                // 创建绿色实心画刷（可移动标记常用绿色）
                HBRUSH hBrush = CreateSolidBrush(RGB(0, 180, 0));
                HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);

                // 创建无边框的画笔（避免圆圈有黑色边框）
                HPEN hPen = CreatePen(PS_NULL, 0, RGB(0, 0, 0));
                HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

                // 绘制实心圆：Ellipse 需要外接矩形的左上角和右下角坐标
                Ellipse(hdc,
                    cx - dotRadius, cy - dotRadius,  // 左上角
                    cx + dotRadius, cy + dotRadius   // 右下角
                );

                // 恢复原来的 GDI 对象并释放资源
                SelectObject(hdc, hOldBrush);
                SelectObject(hdc, hOldPen);
                DeleteObject(hBrush);
                DeleteObject(hPen);
            }
        }
    }

	int xCenter = finalStartX + boardTotalWidth / 2;

    m_textAnim.Draw(hdc, xCenter, m_winHeight / 2);
    if( m_pCurrentAI )
    m_aica.Draw(hdc, xCenter, m_winHeight / 2);

    // 动画完成后消费移动信息时，若切换回合至 AI 且自动 AI 打开，则触发一次 AI 计算。
    // 注意：真正的触发点为上面 ConsumeLastMove 的处理分支内（消费后只触发一次），
    // 这里保持空白以避免每帧重复调用。
}


void CQiPan::OnMoveOneStep() {
	// 每步真正走完后：界面棋盘自检（重复坐标/越界/丢将帅即严重 BUG）
	{
		std::wstring rep;
		if (ZSSync::VerifyUiBoardSelf(m_pieceMng.AllPieces(), rep) != 0) {
			ZSSync::LogSyncError(L"玩家对局-每步后自检", rep);
		}
	}
	// 训练实时演示模式：动画真正结束（已走完一步），
	// 放行训练线程继续下一步；训练线程自己管理终局/保存/继续下局，
	// 界面不触发 AI 也不重复保存棋谱。
	if (m_trainShowMode) { NotifyTrainStepDone(); return; }
	//这里添加逻辑判断 ，是否被将死 或无棋可走，若是，停止所有AI，并保存棋谱
	if (m_pieceMng.IsCheckmate(m_currentTurn)) {
		// 游戏结束，当前轮到的玩家被将死
		std::wstring result = (m_currentTurn == RED) ? L"黑胜" : L"红胜";
		SaveRecordAuto(result);
 
		this->SetAutoAI(BLACK, false); // 停止所有 AI
        this->SetAutoAI(RED, false);
	}
	if (m_pCurrentAI) {
        m_pCurrentAI->Cancel();;  // 停止当前 AI 的计算
	}
	//清除当前 AI 对象，释放资源
    m_pCurrentAI.reset();
}

// 辅助函数：绘制棋子位置的十字标记 (类似传统象棋的炮兵位置标记)
void CQiPan::DrawCrossMark(HDC hdc, int x, int y, int size,bool noLeft,bool noRight)
{
    int offset = size / 9; // 标记的大小
    int len = size / 6;    // 标记线的长度

    HPEN hMarkPen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
    HPEN hOldPen = (HPEN)SelectObject(hdc, hMarkPen);

    if(!noLeft)
    // 左上
    if (x - offset >= 0) {
        MoveToEx(hdc, x - offset, y - offset - len, nullptr);
        LineTo(hdc, x - offset, y - offset);
        LineTo(hdc, x - offset - len, y - offset);
    }

    if(!noRight)
    // 右上
    if (x + offset <= m_winWidth) { // 简单边界检查，实际应基于棋盘边界
        MoveToEx(hdc, x + offset, y - offset - len, nullptr);
        LineTo(hdc, x + offset, y - offset);
        LineTo(hdc, x + offset + len, y - offset);
    }
    if (!noLeft)
    // 左下
    if (x - offset >= 0) {
        MoveToEx(hdc, x - offset, y + offset + len, nullptr);
        LineTo(hdc, x - offset, y + offset);
        LineTo(hdc, x - offset - len, y + offset);
    }

    if (!noRight)
    // 右下
    if (x + offset <= m_winWidth) {
        MoveToEx(hdc, x + offset, y + offset + len, nullptr);
        LineTo(hdc, x + offset, y + offset);
        LineTo(hdc, x + offset + len, y + offset);
    }

    SelectObject(hdc, hOldPen);
    DeleteObject(hMarkPen);
}

// 时间格式化：<60s 显示 "12.3s"；<1h 显示 "m:ss"；否则 "h:mm:ss"
void CQiPan::FormatClock(ULONGLONG ms, wchar_t* buf, int bufLen) const {
    ULONGLONG s = ms / 1000;
    if (s < 60) {
        swprintf_s(buf, bufLen, L"%llu.%1u s", (unsigned long long)s, (unsigned)((ms / 100) % 10));
    } else if (s < 3600) {
        swprintf_s(buf, bufLen, L"%llu:%02llu", (unsigned long long)(s / 60), (unsigned long long)(s % 60));
    } else {
        swprintf_s(buf, bufLen, L"%llu:%02llu:%02llu",
            (unsigned long long)(s / 3600), (unsigned long long)((s % 3600) / 60), (unsigned long long)(s % 60));
    }
}

// 头像：AI 机器人 / 人类。active 时外圈有脉动光环
void CQiPan::DrawAvatar(HDC hdc, int cx, int cy, int r, bool isAI, bool active, ULONGLONG nowTick) {
    // active：脉动光环
    if (active) {
        int pulse = (int)((nowTick / 70) % 3); // 0,1,2 循环扩张
        HPEN hRingPen = CreatePen(PS_SOLID, 2, RGB(255, 180, 0));
        HGDIOBJ hOldRingPen = SelectObject(hdc, hRingPen);
        HGDIOBJ hOldRingBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        int rr = r + 3 + pulse;
        Ellipse(hdc, cx - rr, cy - rr, cx + rr, cy + rr);
        SelectObject(hdc, hOldRingBrush);
        SelectObject(hdc, hOldRingPen);
        DeleteObject(hRingPen);
    }

    COLORREF base = isAI ? RGB(86, 150, 214) : RGB(230, 140, 80);
    if (!active) base = isAI ? RGB(160, 190, 218) : RGB(222, 185, 150);
    HBRUSH hb = CreateSolidBrush(base);
    HPEN hp = CreatePen(PS_SOLID, 1, RGB(70, 70, 70));
    HGDIOBJ ob = SelectObject(hdc, hb);
    HGDIOBJ op = SelectObject(hdc, hp);
    Ellipse(hdc, cx - r, cy - r, cx + r, cy + r);
    SelectObject(hdc, op);
    SelectObject(hdc, ob);
    DeleteObject(hb);
    DeleteObject(hp);

    if (isAI) {
        // 机器人：天线 + 两只眼睛 + 嘴
        HPEN hLine = CreatePen(PS_SOLID, 2, RGB(45, 45, 45));
        HGDIOBJ oLine = SelectObject(hdc, hLine);
        MoveToEx(hdc, cx, cy - r, nullptr);
        LineTo(hdc, cx, cy - r - (int)(r * 0.42));
        SelectObject(hdc, oLine);
        DeleteObject(hLine);
        HBRUSH hDot = CreateSolidBrush(RGB(45, 45, 45));
        HGDIOBJ oDot = SelectObject(hdc, hDot);
        HGDIOBJ oDotPen = SelectObject(hdc, GetStockObject(NULL_PEN));
        Ellipse(hdc, cx - 2, cy - r - (int)(r * 0.42) - 6, cx + 2, cy - r - (int)(r * 0.42) + 2);
        // 眼睛
        int er = (r * 2) / 7;
        Ellipse(hdc, cx - r / 2 - er, cy - er / 2, cx - r / 2 + er, cy + er / 2);
        Ellipse(hdc, cx + r / 2 - er, cy - er / 2, cx + r / 2 + er, cy + er / 2);
        // 嘴
        HPEN hMouth = CreatePen(PS_SOLID, 2, RGB(45, 45, 45));
        HGDIOBJ oMouth = SelectObject(hdc, hMouth);
        MoveToEx(hdc, cx - (int)(r * 0.35), cy + (int)(r * 0.35), nullptr);
        LineTo(hdc, cx + (int)(r * 0.35), cy + (int)(r * 0.35));
        SelectObject(hdc, oMouth);
        DeleteObject(hMouth);
        SelectObject(hdc, oDotPen);
        SelectObject(hdc, oDot);
        DeleteObject(hDot);
    } else {
        // 人类：深色头发（圆帽）+ 肤色脸 + 眼睛 + 微笑
        int fr = (int)(r * 0.72);
        HBRUSH hHair = CreateSolidBrush(RGB(72, 56, 46));
        HGDIOBJ oHair = SelectObject(hdc, hHair);
        HGDIOBJ oHairPen = SelectObject(hdc, GetStockObject(NULL_PEN));
        Ellipse(hdc, cx - (int)(fr * 0.95), cy - (int)(fr * 1.15), cx + (int)(fr * 0.95), cy + (int)(fr * 0.30));
        SelectObject(hdc, oHairPen);
        SelectObject(hdc, oHair);
        DeleteObject(hHair);
        // 脸
        HBRUSH hFace = CreateSolidBrush(RGB(250, 218, 180));
        HGDIOBJ oFace = SelectObject(hdc, hFace);
        HGDIOBJ oFacePen = SelectObject(hdc, GetStockObject(NULL_PEN));
        Ellipse(hdc, cx - fr, cy - (int)(fr * 0.45), cx + fr, cy + (int)(fr * 1.15));
        SelectObject(hdc, oFacePen);
        SelectObject(hdc, oFace);
        DeleteObject(hFace);
        // 眼睛
        HBRUSH hEye = CreateSolidBrush(RGB(50, 45, 40));
        HGDIOBJ oEye = SelectObject(hdc, hEye);
        HGDIOBJ oEyePen = SelectObject(hdc, GetStockObject(NULL_PEN));
        int eyeR = fr / 6;
        Ellipse(hdc, cx - fr / 2 - eyeR, cy - eyeR, cx - fr / 2 + eyeR, cy + eyeR);
        Ellipse(hdc, cx + fr / 2 - eyeR, cy - eyeR, cx + fr / 2 + eyeR, cy + eyeR);
        // 微笑
        HPEN hSmile = CreatePen(PS_SOLID, 2, RGB(120, 70, 50));
        HGDIOBJ oSmile = SelectObject(hdc, hSmile);
        int smR = (int)(fr * 0.45);
        MoveToEx(hdc, cx - smR, cy + (int)(fr * 0.45), nullptr);
        LineTo(hdc, cx - (int)(smR * 0.5), cy + (int)(fr * 0.62));
        LineTo(hdc, cx + (int)(smR * 0.5), cy + (int)(fr * 0.62));
        LineTo(hdc, cx + smR, cy + (int)(fr * 0.45));
        SelectObject(hdc, oSmile);
        DeleteObject(hSmile);
        SelectObject(hdc, oEyePen);
        SelectObject(hdc, oEye);
        DeleteObject(hEye);
    }
}

// 侧边状态卡：背景 + 头像 + 名称 + 计时 + 思考动画
void CQiPan::DrawSideCard(HDC hdc, PieceColor color, bool isAI, bool active, bool inCheck, bool mate,
    int x0, int y0, int w, int h, ULONGLONG nowTick) {
    // 背景圆角矩形
    HBRUSH hBg;
    HPEN hBorder;
    if (mate) {
        hBg = CreateSolidBrush(RGB(255, 245, 205));
        hBorder = CreatePen(PS_SOLID, 4, RGB(205, 155, 40));
    } else if (active && inCheck) {
        hBg = CreateSolidBrush(RGB(255, 232, 220));
        hBorder = CreatePen(PS_SOLID, 4, RGB(220, 40, 40));
    } else if (active) {
        hBg = CreateSolidBrush(RGB(255, 250, 210));
        hBorder = CreatePen(PS_SOLID, 3, RGB(230, 126, 34));
    } else {
        hBg = CreateSolidBrush(RGB(238, 238, 238));
        hBorder = CreatePen(PS_SOLID, 1, RGB(178, 178, 178));
    }
    HGDIOBJ oBg = SelectObject(hdc, hBg);
    HGDIOBJ oBd = SelectObject(hdc, hBorder);
    RoundRect(hdc, x0, y0, x0 + w, y0 + h, 14, 14);
    SelectObject(hdc, oBd);
    SelectObject(hdc, oBg);
    DeleteObject(hBg);
    DeleteObject(hBorder);

    int cxc = x0 + w / 2;
    int avatarR = (w * 3) / 10;
    if (avatarR > h / 3) avatarR = h / 3;
    int avY = y0 + (int)(h * 0.36);

    // 头像
    DrawAvatar(hdc, cxc, avY, avatarR, isAI, active, nowTick);

    // 名称
    HFONT hName = CreateFontW(
        (int)(w * 0.13) > 0 ? (int)(w * 0.13) : 12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"楷体");
    HGDIOBJ oName = SelectObject(hdc, hName);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, active ? RGB(40, 40, 40) : RGB(140, 140, 140));
    wchar_t nameBuf[32];
    if (color == PieceColor::BLACK) swprintf_s(nameBuf, L"黑方 %s", isAI ? L"AI" : L"玩家");
    else swprintf_s(nameBuf, L"红方 %s", isAI ? L"AI" : L"玩家");
    int nameW = (int)wcslen(nameBuf);
    TextOutW(hdc, cxc - nameW * 7-5, y0 + (int)(h * 0.66), nameBuf, nameW);
    SelectObject(hdc, oName);
    DeleteObject(hName);

    // 计时（当前方显示本回合思考时间，非当前方显示累计总用时；将死显示 胜!）
    HFONT hClock = CreateFontW(
        (int)(w * 0.17) > 0 ? (int)(w * 0.17) : 13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Arial");
    HGDIOBJ oClock = SelectObject(hdc, hClock);
    wchar_t clockBuf[48];
    ULONGLONG turnStart = (color == PieceColor::BLACK) ? m_blackTurnStart : m_redTurnStart;
    ULONGLONG totalMs = (color == PieceColor::BLACK) ? m_blackTotalMs : m_redTotalMs;
    if (mate) {
        swprintf_s(clockBuf, L"败！");
        SetTextColor(hdc, RGB(200, 140, 20));
    } else if (active) {
        ULONGLONG el = (turnStart == 0) ? 0 : (nowTick - turnStart);
        FormatClock(el, clockBuf, 48);
        SetTextColor(hdc, inCheck ? RGB(210, 30, 30) : RGB(30, 30, 30));
    } else {
        // 非当前方：显示累计总用时
        wchar_t tmpBuf[40];
        FormatClock(totalMs, tmpBuf, 40);
        swprintf_s(clockBuf, L"总 %s", tmpBuf);
        SetTextColor(hdc, RGB(150, 150, 150));
    }
    int clockLen = (int)wcslen(clockBuf);
    TextOutW(hdc, cxc - clockLen * 5, y0 + (int)(h * 0.84), clockBuf, clockLen);
    SelectObject(hdc, oClock);
    DeleteObject(hClock);

    // 思考动画：三个小点循环点亮（仅当前方且未结束）
    if (active && !mate) {
        int phase = (int)((nowTick / 280) % 3);
        int dotY = avY + avatarR + 10;
        for (int i = 0; i < 3; i++) {
            int dx = cxc - 12 + i * 12;
            bool lit = (i == phase);
            HBRUSH hDot = CreateSolidBrush(lit ? RGB(230, 126, 34) : RGB(200, 200, 200));
            HGDIOBJ oDot = SelectObject(hdc, hDot);
            HGDIOBJ oDotPen = SelectObject(hdc, GetStockObject(NULL_PEN));
            Ellipse(hdc, dx - 3, dotY - 3, dx + 3, dotY + 3);
            SelectObject(hdc, oDotPen);
            SelectObject(hdc, oDot);
            DeleteObject(hDot);
        }
    }
}

// 左侧信息栏：上方黑方（默认 AI）、下方红方（默认人类）
void CQiPan::DrawSidePanel(HDC hdc, const BoardLayout& L) {
    ULONGLONG nowTick = GetTickCount64();
    // 左栏固定区域：卡片尽量占满左栏宽，高度按棋盘比例
    int x0 = L.leftX + 4;
    int cardW = L.leftW - 8;
    if (cardW < 40) cardW = 40;
    int cardH = (int)(L.cellSize * 3.5);

    bool redMate = (m_currentTurn == RED) && m_pieceMng.IsCheckmate(RED);
    bool blackMate = (m_currentTurn == BLACK) && m_pieceMng.IsCheckmate(BLACK);
    bool redCheck = (m_currentTurn == RED) && m_pieceMng.IsInCheck(RED);
    bool blackCheck = (m_currentTurn == BLACK) && m_pieceMng.IsInCheck(BLACK);

    // 上卡片：黑方
    int topY0 = L.finalStartY + (int)(L.cellSize * 0.35);
    DrawSideCard(hdc, BLACK, IsAutoAI(BLACK), (m_currentTurn == BLACK), blackCheck, blackMate,
        x0, topY0, cardW, cardH, nowTick);
    // 下卡片：红方
    int botY0 = L.finalStartY + (int)(L.cellSize * 7.15);
    DrawSideCard(hdc, RED, IsAutoAI(RED), (m_currentTurn == RED), redCheck, redMate,
        x0, botY0, cardW, cardH, nowTick);
}

// ==================== 右侧棋谱面板 ====================
// 简单按钮绘制辅助
static void DrawPanelButton(HDC hdc, const RECT& r, const wchar_t* text, bool enabled) {
    HBRUSH hBg = CreateSolidBrush(enabled ? RGB(240, 238, 228) : RGB(214, 214, 208));
    HPEN hBd = CreatePen(PS_SOLID, 1, RGB(150, 150, 140));
    HGDIOBJ oBg = SelectObject(hdc, hBg);
    HGDIOBJ oBd = SelectObject(hdc, hBd);
    RoundRect(hdc, r.left, r.top, r.right, r.bottom, 8, 8);
    SelectObject(hdc, oBd);
    SelectObject(hdc, oBg);
    DeleteObject(hBg);
    DeleteObject(hBd);
    if (enabled) {
        HFONT hF = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"SimSun");
        HGDIOBJ oF = SelectObject(hdc, hF);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(50, 50, 50));
        int len = (int)wcslen(text);
        int tx = r.left + ((r.right - r.left) - len * 8) / 2;
        int ty = r.top + ((r.bottom - r.top) - 16) / 2;
        TextOutW(hdc, tx, ty, text, len);
        SelectObject(hdc, oF);
        DeleteObject(hF);
    }
}

void CQiPan::DrawRecordPanel(HDC hdc, const BoardLayout& L) {
    int x0 = L.panelX, y0 = L.panelY, w = L.panelW, h = L.panelH;

    // 背景圆角矩形
    HBRUSH hBg = CreateSolidBrush(RGB(252, 251, 246));
    HPEN hBd = CreatePen(PS_SOLID, 1, RGB(180, 178, 165));
    HGDIOBJ oBg = SelectObject(hdc, hBg);
    HGDIOBJ oBd = SelectObject(hdc, hBd);
    RoundRect(hdc, x0, y0, x0 + w, y0 + h, 12, 12);
    SelectObject(hdc, oBd);
    SelectObject(hdc, oBg);
    DeleteObject(hBg);
    DeleteObject(hBd);

    // 标题：棋谱
    HFONT hTitle = CreateFontW(17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"SimSun");
    HGDIOBJ oTitle = SelectObject(hdc, hTitle);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(40, 40, 40));
    TextOutW(hdc, x0 + 10, y0 + 8, L"棋谱", 2);
    SelectObject(hdc, oTitle);
    DeleteObject(hTitle);

    // 文件名（短名：去掉路径）
    if (!m_recordFile.empty()) {
        std::wstring shortName = m_recordFile;
        size_t slash = shortName.find_last_of(L"\\/");
        if (slash != std::wstring::npos) shortName = shortName.substr(slash + 1);
        HFONT hF = CreateFontW(11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"SimSun");
        HGDIOBJ oF = SelectObject(hdc, hF);
        SetTextColor(hdc, RGB(120, 120, 110));
        int len = (int)shortName.size();
        if (len * 7 > w - 20) len = (w - 20) / 7;
        if (len > 0) TextOutW(hdc, x0 + 10, y0 + 30, shortName.c_str(), len);
        SelectObject(hdc, oF);
        DeleteObject(hF);
    }

    // 列表区
    int listY0 = y0 + 46;
    int listH = h - 46 - 84;
    int rowH = 19;
    if (listH > 10) {
        int maxRows = listH / rowH;
        // 滚动条：列表区最右 8px；文字/高亮右边界收窄避免压滚动条
        int sbW = 8;
        int txtRight = x0 + w - 4 - sbW - 2;
        m_scrollTrack = { x0 + w - 4 - sbW, listY0 + 1, x0 + w - 4, listY0 + listH - 1 };
        // 滚动窗口跟随当前步：最新步跟底；复盘时保证当前步可见
        ClampRecordTop(maxRows);
        m_panelListRect = { x0 + 4, listY0, txtRight, listY0 + listH };
        int first = m_recordTop + 1;
        HFONT hRow = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"SimSun");
        HGDIOBJ oRow = SelectObject(hdc, hRow);
        for (int i = 0; i < maxRows; i++) {
            int stepNo = first + i;
            if (stepNo > m_record.Count()) break;
            const RecordStep& rs = m_record.At(stepNo - 1);
            int ry = listY0 + i * rowH;
            // 当前步高亮
            if (stepNo == m_reviewIdx) {
                HBRUSH hHi = CreateSolidBrush(RGB(255, 232, 180));
                HGDIOBJ oHi = SelectObject(hdc, hHi);
                HGDIOBJ oHiPen = SelectObject(hdc, GetStockObject(NULL_PEN));
                Rectangle(hdc, m_panelListRect.left, ry, m_panelListRect.right, ry + rowH);
                SelectObject(hdc, oHiPen);
                SelectObject(hdc, oHi);
                DeleteObject(hHi);
            }
            const wchar_t* sideTxt = (rs.side == RED) ? L"红" : L"黑";
            // 同时显示双方得分（红方视角差值与黑方视角取反），负分也显示，前导空格隔开
            wchar_t scoreTxt[40];
            if (rs.evalScore > 0) swprintf_s(scoreTxt, L" (红:+%d,黑:-%d)", rs.evalScore, rs.evalScore);
            else if (rs.evalScore < 0) swprintf_s(scoreTxt, L" (红:-%d,黑:+%d)", -rs.evalScore, -rs.evalScore);
            else wcscpy_s(scoreTxt, L" (红:0,黑:0)");
            // 拼行：分数后缀优先保留，超长时缩短记法
            wchar_t head[16];
            swprintf_s(head, L"%2d %s ", rs.number, sideTxt);
            std::wstring notate(rs.notation);
            std::wstring tail = scoreTxt;
            int maxChar = (txtRight - x0 - 10) / 8;
            int headLen = (int)wcslen(head);
            int tailLen = (int)tail.size();
            int notateMax = maxChar - headLen - tailLen;
            if (notateMax < 0) notateMax = 0;
            if ((int)notate.size() > notateMax) notate.resize(notateMax);
            std::wstring full0 = head + notate;
            std::wstring full = full0 + tail;
            SetTextColor(hdc, RGB(60, 60, 60));
            int xDraw = x0 + 6;
            if (!full0.empty()) TextOutW(hdc, xDraw, ry + 2, full0.c_str(), (int)full0.size());
            // 分数用颜色区分：红优红字、黑优蓝字
            SIZE sz;
            GetTextExtentPoint32W(hdc, full0.c_str(), (int)full0.size(), &sz);
            if (rs.evalScore > 0) SetTextColor(hdc, RGB(200, 60, 60));
            else if (rs.evalScore < 0) SetTextColor(hdc, RGB(50, 70, 150));
            else SetTextColor(hdc, RGB(150, 150, 140));
            TextOutW(hdc, xDraw + sz.cx, ry + 2, scoreTxt, (int)wcslen(scoreTxt));
        }
        SelectObject(hdc, oRow);
        DeleteObject(hRow);

        // 滚动条（记录数超过一屏才显示）
        if (m_record.Count() > maxRows) {
            int trackX = m_scrollTrack.left, trackY = m_scrollTrack.top;
            int trackH = m_scrollTrack.bottom - m_scrollTrack.top;
            int thumbH = trackH * maxRows / m_record.Count();
            if (thumbH < 20) thumbH = 20;
            if (thumbH > trackH) thumbH = trackH;
            int span = m_record.Count() - maxRows;
            int thumbY = trackY + (trackH - thumbH) * m_recordTop / span;
            // 轨道
            HBRUSH hTr = CreateSolidBrush(RGB(228, 226, 215));
            HGDIOBJ oTr = SelectObject(hdc, hTr);
            HGDIOBJ oTrPen = SelectObject(hdc, GetStockObject(NULL_PEN));
            Rectangle(hdc, trackX, trackY, m_scrollTrack.right, m_scrollTrack.bottom);
            SelectObject(hdc, oTrPen);
            SelectObject(hdc, oTr);
            DeleteObject(hTr);
            // 滑块
            HBRUSH hTh = CreateSolidBrush(RGB(150, 147, 133));
            HGDIOBJ oTh = SelectObject(hdc, hTh);
            RoundRect(hdc, trackX + 1, thumbY, trackX + 6, thumbY + thumbH, 3, 3);
            SelectObject(hdc, oTh);
            DeleteObject(hTh);
        }
    } else {
        m_panelListRect = { 0, 0, 0, 0 };
        m_scrollTrack = { 0, 0, 0, 0 };
    }
    // 底部按钮两行
    int btnY1 = y0 + h - 80;
    int btnH = 26;
    int gap = 6;
    int btnW2 = (w - 12 - gap) / 2;
    m_btnPrev = { x0 + 6, btnY1, x0 + 6 + btnW2, btnY1 + btnH };
    m_btnNext = { x0 + 6 + btnW2 + gap, btnY1, x0 + w - 6, btnY1 + btnH };
    DrawPanelButton(hdc, m_btnPrev, L"◀ 上一步", m_reviewIdx > 0);
    DrawPanelButton(hdc, m_btnNext, L"下一步 ▶", m_reviewIdx < m_record.Count());

    int btnY2 = btnY1 + btnH + 6;
    int btnW3 = (w - 12 - 2 * gap) / 3;
    m_btnSave = { x0 + 6, btnY2, x0 + 6 + btnW3, btnY2 + btnH };
    m_btnLoad = { x0 + 6 + btnW3 + gap, btnY2, x0 + 6 + 2 * (btnW3 + gap), btnY2 + btnH };
    m_btnCopy = { x0 + 6 + 2 * (btnW3 + gap), btnY2, x0 + w - 6, btnY2 + btnH };
    DrawPanelButton(hdc, m_btnSave, L"保存", m_record.Count() > 0);
    DrawPanelButton(hdc, m_btnLoad, L"加载", true);
    DrawPanelButton(hdc, m_btnCopy, L"复制", true);
}

// ==================== 棋谱列表滚动 ====================
int CQiPan::RecordMaxRows(const BoardLayout& L) const {
    int listH = L.panelH - 46 - 84;
    if (listH <= 10) return 0;
    return listH / 19;
}

void CQiPan::ClampRecordTop(int maxRows) {
    if (maxRows <= 0) { m_recordTop = 0; return; }
    int maxTop = m_record.Count() - maxRows;
    if (maxTop < 0) maxTop = 0;
    if (m_recordFollowBottom) {
        // 跟随模式：最新步跟底；复盘时保证当前步可见
        if (m_reviewIdx >= m_record.Count()) m_recordTop = maxTop;
        if (m_reviewIdx < m_recordTop) m_recordTop = m_reviewIdx;
        if (m_reviewIdx >= m_recordTop + maxRows) m_recordTop = m_reviewIdx - maxRows + 1;
    }
    // 手动滚动模式：仅约束合法范围，可在全部记录间自由滚动定位
    if (m_recordTop < 0) m_recordTop = 0;
    if (m_recordTop > maxTop) m_recordTop = maxTop;
}

bool CQiPan::HitScrollThumb(int mouseX, int mouseY) const {
    if (m_scrollTrack.left >= m_scrollTrack.right) return false;
    if (mouseX < m_scrollTrack.left || mouseX >= m_scrollTrack.right) return false;
    int maxRows = m_record.Count() ? 1 : 0;
    return mouseY >= m_scrollTrack.top && mouseY < m_scrollTrack.bottom;
}
bool CQiPan::HitRecordPanel(int mouseX, int mouseY) {
    auto inRect = [&](const RECT& r) {
        return mouseX >= r.left && mouseX < r.right && mouseY >= r.top && mouseY < r.bottom;
    };
    if (inRect(m_btnPrev)) { StepBack(); return true; }
    if (inRect(m_btnNext)) { StepForward(); return true; }
    if (inRect(m_btnSave)) { SaveRecordDialog(); return true; }
    if (inRect(m_btnLoad)) { LoadRecordDialog(); return true; }
    if (inRect(m_btnCopy)) { CopyPosition(); return true; }
    if (m_panelListRect.left < m_panelListRect.right && inRect(m_panelListRect)) {
        int rowH = 19;
        int row = (mouseY - m_panelListRect.top) / rowH;
        int stepNo = m_recordTop + row + 1;
        if (stepNo >= 0 && stepNo <= m_record.Count()) JumpToStep(stepNo);
        return true;
    }
    // 滚动条：点击轨道翻页，点击滑块开始拖动
    if (m_scrollTrack.left < m_scrollTrack.right && inRect(m_scrollTrack)) {
        int rowH = 19;
        int maxRows = (m_panelListRect.bottom - m_panelListRect.top) / rowH;
        int trackH = m_scrollTrack.bottom - m_scrollTrack.top;
        int thumbH = trackH * maxRows / m_record.Count();
        if (thumbH < 20) thumbH = 20;
        if (thumbH > trackH) thumbH = trackH;
        int span = m_record.Count() - maxRows;
        if (span > 0) {
            int thumbY = m_scrollTrack.top + (trackH - thumbH) * m_recordTop / span;
            if (mouseY >= thumbY && mouseY < thumbY + thumbH) {
                // 按下滑块：开始拖动
                
                m_recordFollowBottom = false;
            } else {
                // 点击轨道：翻一页
                int delta = (mouseY < thumbY) ? -maxRows : maxRows;
                
                m_recordFollowBottom = false;
                m_recordScrollDrag = -1;
            }
            return true;
        }
    }
    return false;
}

// 拖动滚动条：根据鼠标位置更新 m_recordTop
void CQiPan::OnMouseMove(int mouseX, int mouseY) {
    if (m_recordScrollDrag < 0) return;
    int maxRows = (m_panelListRect.bottom - m_panelListRect.top) / 19;
    if (maxRows <= 0) return;
    int trackH = m_scrollTrack.bottom - m_scrollTrack.top;
    int thumbH = trackH * maxRows / m_record.Count();
    if (thumbH < 20) thumbH = 20;
    if (thumbH > trackH) thumbH = trackH;
    int span = m_record.Count() - maxRows;
    if (span <= 0) return;
    int posY = mouseY - m_recordDragGrabY - m_scrollTrack.top;
    if (posY < 0) posY = 0;
    int maxY = trackH - thumbH;
    if (posY > maxY) posY = maxY;
    m_recordTop = (span > 0) ? (posY * span / maxY) : 0;
    ClampRecordTop(maxRows);
}

// 结束拖动
void CQiPan::OnMouseUp(int mouseX, int mouseY) {
    m_recordScrollDrag = -1;
}

// 滚轮滚动：deltaLines 为正表示向下滚动（显示更晚的记录）。
// 仅当鼠标位于棋谱面板（列表区或滚动条）内时生效。
bool CQiPan::ScrollRecord(int deltaLines, int mouseX, int mouseY) {
    if (m_panelListRect.left >= m_panelListRect.right) return false;
    bool inList = mouseX >= m_panelListRect.left && mouseX <= m_panelListRect.right
        && mouseY >= m_panelListRect.top && mouseY <= m_panelListRect.bottom;
    bool inTrack = m_scrollTrack.left < m_scrollTrack.right
        && mouseX >= m_scrollTrack.left && mouseX <= m_scrollTrack.right
        && mouseY >= m_scrollTrack.top && mouseY <= m_scrollTrack.bottom;
    if (!inList && !inTrack) return false;
    if (deltaLines == 0) return false;
    int maxRows = (m_panelListRect.bottom - m_panelListRect.top) / 19;
    if (maxRows <= 0) return false;
    // 滚轮向上（delta>0）= 向上滚动 = 显示更早记录；向下 = 显示更晚记录
    m_recordTop -= deltaLines;
    m_recordFollowBottom = false; // 手动滚动：解除跟随，可在所有记录间自由定位
    ClampRecordTop(maxRows);
    return true;
}
// ==================== 复盘控制 ====================
void CQiPan::ApplyReplay() {
    // 从初始局面重放到 m_reviewIdx
    StopAI();
    std::vector<CChessPiece> board = m_initialBoard;
    RepetitionGuard::instance().Reset();
    for (int i = 0; i < m_reviewIdx && i < m_record.Count(); i++) {
        CGameRecord::ApplyStep(board, m_record.At(i));
        // 重建长将历史：每步走完后记录（走完后走棋方 = 1-side）
        {
            PieceColor after = (PieceColor)(1 - m_record.At(i).side);
            zschess::Position rp = CZSAIPlayer::BoardToPosition(board, after);
            RepetitionGuard::instance().PushPosition(rp);
        }
    }
    m_pieceMng.ReplacePieces(board);
    m_lastMoveCol = m_lastMoveRow = -1;
    m_currentTurn = (m_reviewIdx % 2 == 0) ? RED : BLACK;
    m_pieceMng.ClearSelection();
    m_isMoveedPiece = false;
    BeginTurnTimer(m_currentTurn);
}


void CQiPan::StepBack() {
    if (m_reviewIdx <= 0) return;
    m_reviewIdx--;
    m_recordFollowBottom = true;
    ApplyReplay();
}

void CQiPan::StepForward() {
    if (m_reviewIdx >= m_record.Count()) return;
    m_reviewIdx++;
    m_recordFollowBottom = true;
    ApplyReplay();
}

void CQiPan::JumpToStep(int stepNo) {
    if (stepNo < 0 || stepNo > m_record.Count()) return;
    m_reviewIdx = stepNo;
    m_recordFollowBottom = true;
    ApplyReplay();
}

// ==================== 保存 / 加载 / 复制 ====================
void CQiPan::SaveRecordAuto(const std::wstring& result) {
    std::wstring dir = CGameRecord::DefaultRecordDir();
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t name[64];
    swprintf_s(name, L"%04d%02d%02d_%02d%02d%02d_%s.txt",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, result.c_str());
    std::wstring path = dir + L"\\" + name;
    wchar_t ev[40];
    swprintf_s(ev, L"%04d-%02d-%02d_%02d-%02d-%02d",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    if (m_record.SaveToFile(path, ev, result)) {
        m_recordFile = path;
        m_textAnim.Start(L"棋谱已自动保存", 2000);
    }
}

void CQiPan::SaveRecordDialog() {
    if (m_record.Count() <= 0) return;
    std::wstring dir = CGameRecord::DefaultRecordDir();
    std::wstring defName = CGameRecord::MakeTimestampFilename(L"对局");
    wchar_t file[MAX_PATH] = { 0 };
    wcscpy_s(file, defName.c_str());
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"棋谱文件 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = dir.c_str();
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (GetSaveFileNameW(&ofn)) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t ev[40];
        swprintf_s(ev, L"%04d-%02d-%02d_%02d-%02d-%02d",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        if (m_record.SaveToFile(ofn.lpstrFile, ev, L"对局")) {
            m_recordFile = ofn.lpstrFile;
            m_textAnim.Start(L"棋谱已保存", 1500);
        }
    }
}

void CQiPan::LoadRecordDialog() {
    std::wstring dir = CGameRecord::DefaultRecordDir();
    wchar_t file[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"棋谱文件 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = dir.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;

    std::vector<RecordStep> steps;
    std::wstring ev, result;
    if (!CGameRecord::LoadFromFile(ofn.lpstrFile, steps, ev, result)) {
        MessageBoxW(nullptr, L"棋谱文件无法解析", L"加载棋谱", MB_OK | MB_ICONWARNING);
        return;
    }
    // 重置到初始局面，逐步入校验（非法步截断）
    StopAI();
    m_pieceMng.InitPieces();
    m_initialBoard = m_pieceMng.AllPieces();
    m_record.Clear();
    int okCount = 0;
    std::vector<CChessPiece> board = m_initialBoard;
    for (size_t i = 0; i < steps.size(); i++) {
        RecordStep s = steps[i]; // 拷贝：需写入评估分（加载的帧不改变传入的 steps）
        // 校验：from 有 side 棋子，且 to 可达（界面规则）
        CChessPiece* mover = nullptr;
        for (auto& p : board) {
            if (p.GetX() == s.fromCol && p.GetY() == s.fromRow && p.GetColor() == s.side) { mover = &p; break; }
        }
        bool legal = false;
        if (mover) {
			//这里要board作为userData传入，避免使用全局变量,更新可能被 吃掉的棋子
            auto mv = PSF::CChessRule::ListCanGotoPos(mover, [](int col, int row, void* userData)->CChessPiece* {
                std::vector<CChessPiece>* board = static_cast<std::vector<CChessPiece>*>(userData);
                for (auto& p : *board) {
                    if (p.GetX() == col && p.GetY() == row) return &p;
                }
                return nullptr;
            }   , &board);
            for (const auto& t : mv) {
                if (t.col == s.toCol && t.row == s.toRow) { legal = true; break; }
            }
        }
        if (!legal) break;
        CGameRecord::ApplyStep(board, s);
        // 加载时逐步评估走完后的局面（红方视角），与实时对局一致；仅运行时显示
        s.evalScore = CZSAIPlayer::EvaluateBoardScore(board, (PieceColor)(1 - s.side));
        m_record.Append(s);
        okCount++;
    }
    m_recordFile = ofn.lpstrFile;
    if (okCount < (int)steps.size()) {
        wchar_t msg[128];
        swprintf_s(msg, L"第 %d 步起无法复现（棋谱可能来自不同版本），已加载前 %d 步", okCount + 1, okCount);
        MessageBoxW(nullptr, msg, L"加载棋谱", MB_OK | MB_ICONINFORMATION);
    }
    // 显示到最新合法步
    m_reviewIdx = m_record.Count();
    m_recordAutoSaved = false;
    m_recordFollowBottom = true;
    ApplyReplay();
    m_textAnim.Start(L"棋谱已加载", 1500);
}

void CQiPan::CopyPosition() {
    std::wstring txt = CGameRecord::MakePositionText(m_pieceMng.AllPieces(), m_currentTurn);
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    size_t bytes = (txt.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        void* p = GlobalLock(hMem);
        if (p) {
            memcpy(p, txt.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        } else {
            GlobalFree(hMem);
        }
    }
    CloseClipboard();
    m_textAnim.Start(L"局面已复制", 1200);
}
