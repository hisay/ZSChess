#pragma once
#include "framework.h"
#include <Windows.h>
#include <vector>
#include <cmath>
#include <tchar.h>
#define SHI_XIANG_GUOHE 1

// 棋子颜色枚举
enum PieceColor
{
    RED,
    BLACK
};
enum E_PieceType
{
	PT_NONE = 0,
	PT_JU, // 车
	PT_MA, // 马
	PT_XIANG, // 象
	PT_SHI, // 士
	PT_JIANG, // 将
	PT_PAO, // 炮
	PT_BING, // 兵
	PT_HOU, // 后
	PT_JUN, // 军
    PT_DU, //督
};

// 棋盘尺寸常量
static const int BOARD_COLS = 11;  // 11列
static const int BOARD_ROWS = 11;  // 11行
static const int RIVER_ROW = 4;    // 河界：黑方 y<=5, 红方 y>=6

static const int MAX_ROWS = 10;
static const int MAX_COLS = 10;

class CQiPan; // 前向声明
extern CQiPan* g_qiPan; // 全局棋盘指针，供规则判断���用
class CChessPiece
{
private:
    int m_x=-1;            // 棋盘格子的列索引 (0-10)
    int m_y=-1;            // 棋盘格子的行索引 (0-10)
    wchar_t m_label[4]{ 0 }; // 棋子上的文字，如 L"帅", L"后"
    PieceColor m_color{ RED }; // 棋子颜色
    int m_type{ 0 };          // 棋子类型 (如车、马、炮等)
	bool m_isSelected=false;   // 是否被选中
	bool m_isMoved = false;     // 是否已经移动过
   
public:
    // 构造函数
    CChessPiece(int x, int y, const wchar_t* label, PieceColor color, int type);
    CChessPiece(){}
private:
    void  DrawSoftShadow(HDC hdc, int cx, int cy, int r);
	void DrawPieceBody(HDC hdc, int cx, int cy, int r);
	void DrawWoodTexture(HDC hdc, int cx, int cy, int r);
	void DrawStateBorder(HDC hdc, int cx, int cy, int r);
	void DrawEdgeHighlight(HDC hdc, int cx, int cy, int r);
    void DrawPieceText(HDC hdc, int cx, int cy, int r);
    void DrawEillFrame(HDC hdc, DWORD clr, int width, int x, int y, int x2, int y2);
    void CopyFrom(const CChessPiece& p) {
        m_x = p.m_x;
        m_y = p.m_y;
        m_type = p.m_type;
        m_isSelected = p.m_isSelected;
        m_isMoved = p.m_isMoved;
        m_color = p.m_color;
        memcpy_s(m_label, sizeof(m_label), p.m_label, sizeof(p.m_label));
    }

public:
    CChessPiece(const CChessPiece& p) noexcept{
        CopyFrom(p);
    }

    CChessPiece(CChessPiece&& p) noexcept{
        CopyFrom(p);
    }
    CChessPiece* operator=(const CChessPiece& p) noexcept {
        CopyFrom(p);
        return this;
    }
    CChessPiece* operator=( CChessPiece&& p) noexcept{
        CopyFrom(p);
        return this;
    }
	int GetX() const { return m_x; }
	int GetY() const { return m_y; }
	void SetX(int x) { m_x = x; }
    void SetY(int y) { m_y = y; }
	bool IsSelected() const { return m_isSelected; }
	bool IsMoved() const { return m_isMoved; }
	void SetMoved(bool moved) { m_isMoved = moved; }
	const wchar_t* GetLabel() const { return m_label; }
	PieceColor GetColor() const { return m_color; }
	void SetPosition(int x, int y) { m_x = x; m_y = y; }
	void SetSelected(bool selected) { m_isSelected = selected; }
    bool IsKilling(); //是否正被 对方吃着的棋子,需要实现这个函数的算法
    E_PieceType GetType() const { return (E_PieceType)m_type; }
    // 绘制函数
    // 需要传入 hdc 以及从 CQiPan 类计算出的棋盘起始坐标和格子大小
    void Draw(HDC hdc, int startX, int startY, int cellSize);
    // 在指定像素中心绘制（用于动画）
    void DrawAt(HDC hdc, int centerX, int centerY, int radius);
    bool operator==(const CChessPiece& p) const noexcept {
        return m_x == p.m_x && m_y == p.m_y && m_color == p.m_color && m_type == p.m_type &&( 0 == _tcscmp(m_label, p.m_label));
    }
    bool operator!=(const CChessPiece& p) const noexcept {
        return !(*this == p);
    }
};
namespace PSF {

    // 坐标结构
    struct POS
    {
        int col;
        int row;
        POS(int c = 0, int r = 0) : col(c), row(r) {}
		POS(const POS& p) : col(p.col), row(p.row) {}
		POS(POS&& p) : col(p.col), row(p.row) {}
		void operator=(const POS& p) { col = p.col; row = p.row; }
		void operator=(POS&& p) { col = p.col; row = p.row; }
    };

    class CChessRule
    {
    public:


        // 判断坐标是否在棋盘内
        static bool IsInBoard(int col, int row)
        {
            return col >= 0 && col < BOARD_COLS && row >= 0 && row < BOARD_ROWS;
        }

        // 判断是否在九宫内（黑方 y:0~2, 红方 y:7~9, x:4~6）
        static bool IsInPalace(int col, int row, PieceColor color)
        {
            if (col < 4 || col > 6) return false;
            if (color == BLACK)
                return row >= 0 && row <= 2;
            else
                return row >= 8 && row <= 10;
        }

        // 判断是否过河：黑方 y<=4 为未过河，红方 y>=5 为未过河
        static bool HasCrossedRiver(int row, PieceColor color)
        {
            if (color == BLACK)
                return row > RIVER_ROW+1;  // 黑方 y>=5 为已过河
            else
                return row <= RIVER_ROW;  // 红方 y<=4 为已过河
        }

        // 获取某个位置上的棋子（通过 CPieceMng 查找）
        // 注意：这里需要外部传入棋子管理器来查询目标位置是否有子
        typedef CChessPiece* (*GetPieceAtFunc)(int col, int row, void* userData);

        // ==================== 核心接口 ====================

        // 判断某棋子能否走到目标位置
        static bool CanGotoPos(int col, int row, CChessPiece* piece,
            GetPieceAtFunc getPieceAt = nullptr, void* userData = nullptr)
        {
            if (!piece) return false;
            if (!IsInBoard(col, row)) return false;

            int fromCol = piece->GetX();
            int fromRow = piece->GetY();

            // 不能原地不动
            if (fromCol == col && fromRow == row) return false;

            // 目标位置有己方棋子，不能走
            if (getPieceAt)
            {
                CChessPiece* targetPiece = getPieceAt(col, row, userData);
                if (targetPiece && targetPiece->GetColor() == piece->GetColor())
                    return false;
            }

            switch (piece->GetType())
            {
            case PT_JU:   return CanJuGoto(fromCol, fromRow, col, row, getPieceAt, userData);
            case PT_MA:   return CanMaGoto(fromCol, fromRow, col, row, getPieceAt, userData);
            case PT_XIANG:return CanXiangGoto(fromCol, fromRow, col, row, piece->GetColor(), getPieceAt, userData);
            case PT_SHI:  return CanShiGoto(fromCol, fromRow, col, row, piece->GetColor(), getPieceAt, userData);
            case PT_JIANG:return CanJiangGoto(fromCol, fromRow, col, row, piece->GetColor(), getPieceAt, userData);
            case PT_PAO:  return CanPaoGoto(fromCol, fromRow, col, row, getPieceAt, userData);
            case PT_BING: return CanBingGoto(fromCol, fromRow, col, row, piece->GetColor(), getPieceAt, userData);
            case PT_HOU:  return CanHouGoto(fromCol, fromRow, col, row, piece->GetColor(), getPieceAt, userData);
            case PT_JUN:  return CanJunGoto(fromCol, fromRow, col, row, getPieceAt, userData);
            case PT_DU:   return CanDuGoto(fromCol, fromRow, col, row,  getPieceAt, userData);
            default:      return false;
            }
        }
        // 1. 将/帅：九宫内横竖走一步
        static  bool CanJiangGoto(int fromCol, int fromRow, int col, int row, PieceColor color, GetPieceAtFunc getPieceAt, void* userData) {
            // 必须在九宫内
            if (!IsInPalace(col, row, color)) return false;

            int dx = abs(col - fromCol);
            int dy = abs(row - fromRow);
            // 只能横竖走一步
            return (dx + dy == 1);
        }

        // 2. 炮：横竖直线，移动不越子，吃子隔一子
        static   bool CanPaoGoto(int fromCol, int fromRow, int col, int row, GetPieceAtFunc getPieceAt, void* userData) {
            // 必须同行或同列
            if (fromCol != col && fromRow != row) return false;

            int count = CountPiecesBetween(fromCol, fromRow, col, row, getPieceAt, userData);
            auto target = getPieceAt(col, row, userData);

            if (target) {
                // 吃子：中间必须正好隔一个子
                return count == 1;
            }
            else {
                // 移动：中间不能有子
                return count == 0;
            }
        }

        // 3. 兵/卒：过河前只能前进一步，过河后可前、左、右，不可后退
        static  bool CanBingGoto(int fromCol, int fromRow, int col, int row, PieceColor color, GetPieceAtFunc getPieceAt, void* userData) {
            int dx = abs(col - fromCol);
            int dy = abs(row - fromRow);

            // 只能走一步
            if (dx + dy != 1) return false;

            if (color == RED) {
                // 红方：只能向上(row减小)
                if (row > fromRow) return false;
                // 未过河(红方 row >= 5)只能向上，不能左右
                if (fromRow >= 5 && col != fromCol) return false;
            }
            else {
                // 黑方：只能向下(row增加)
                if (row < fromRow) return false;
                // 未过河(黑方 row <= 4)只能向下，不能左右
                if (fromRow <= 4 && col != fromCol) return false;
            }

            return true;
        }

        // 4. 后：九宫内横竖斜无限制（类似 confined Queen）
        static   bool CanHouGoto(int fromCol, int fromRow, int col, int row, PieceColor color, GetPieceAtFunc getPieceAt, void* userData) {
            // 必须在九宫内
            if (!IsInPalace(col, row, color)) return false;

            int dx = abs(col - fromCol);
            int dy = abs(row - fromRow);

            // 必须同行、同列或同对角线
            if (fromCol != col && fromRow != row && dx != dy) return false;
            // 不能原地不动
            if (dx == 0 && dy == 0) return false;

            // 路径上不能有阻挡
            return CountPiecesBetween(fromCol, fromRow, col, row, getPieceAt, userData) == 0;
        }

        // 5. 军：全图横竖斜一格限制（类似 Queen）
        static   bool CanJunGoto(int fromCol, int fromRow, int col, int row, GetPieceAtFunc getPieceAt, void* userData) {
            int dx = abs(col - fromCol);
            int dy = abs(row - fromRow);

            // 必须同行、同列或同对角线
            if (fromCol != col && fromRow != row && dx != dy) return false;
            // 不能原地不动
            if (dx == 0 && dy == 0) return false;
            if (dx > 1 || dy > 1) { return false; }
            // 路径上不能有阻挡
            return CountPiecesBetween(fromCol, fromRow, col, row, getPieceAt, userData) == 0;
        }
        // 5. 督：全图横竖斜无限制（类似 Queen）
        static   bool CanDuGoto(int fromCol, int fromRow, int col, int row, GetPieceAtFunc getPieceAt, void* userData) {
            int dx = abs(col - fromCol);
            int dy = abs(row - fromRow);

            // 必须同行、同列或同对角线
            if (fromCol != col && fromRow != row && dx != dy) return false;
            // 不能原地不动
            if (dx == 0 && dy == 0) return false;
            //if (dx > 1 || dy > 1) { return false; }
            if (getPieceAt && CountPiecesBetweenDiag(fromCol, fromRow, col, row, getPieceAt, userData) > 0)
                return false;
            // 路径上不能有阻挡
            return CountPiecesBetween(fromCol, fromRow, col, row, getPieceAt, userData) == 0;
        }
        // 获取某棋子所有可走的位置列表
        static std::vector<POS> ListCanGotoPos(CChessPiece* piece,
            GetPieceAtFunc getPieceAt = nullptr, void* userData = nullptr)
        {
            std::vector<POS> result;
            if (!piece) return result;

            int fromCol = piece->GetX();
            int fromRow = piece->GetY();

            switch (piece->GetType())
            {
            case PT_JU:   ListJuMoves(fromCol, fromRow, piece->GetColor(), result, getPieceAt, userData); break;
            case PT_MA:   ListMaMoves(fromCol, fromRow, piece->GetColor(), result, getPieceAt, userData); break;
            case PT_XIANG:ListXiangMoves(fromCol, fromRow, piece->GetColor(), result, getPieceAt, userData); break;
            case PT_SHI:  ListShiMoves(fromCol, fromRow, piece->GetColor(), result, getPieceAt, userData); break;
            case PT_JIANG:ListJiangMoves(fromCol, fromRow, piece->GetColor(), result, getPieceAt, userData); break;
            case PT_PAO:  ListPaoMoves(fromCol, fromRow, piece->GetColor(), result, getPieceAt, userData); break;
            case PT_BING: ListBingMoves(fromCol, fromRow, piece->GetColor(), result, getPieceAt, userData); break;
            case PT_HOU:  ListHouMoves(fromCol, fromRow, piece->GetColor(), result, getPieceAt, userData); break;
            case PT_JUN:  ListJunMoves(fromCol, fromRow, piece->GetColor(), result, getPieceAt, userData); break;
            case PT_DU:   ListDuMoves(fromCol, fromRow, piece->GetColor(), result, getPieceAt, userData); break;
            default: break;
            }
            return result;
        }

    private:
        // ==================== 辅助：统计两点之间（不含端点）的棋子数 ====================
        static int CountPiecesBetween(int x1, int y1, int x2, int y2,
            GetPieceAtFunc getPieceAt, void* userData)
        {
            if (!getPieceAt) return 0;
            int count = 0;
            int dx = 0, dy = 0;
            if (x1 == x2) dy = (y2 > y1) ? 1 : -1;
            else if (y1 == y2) dx = (x2 > x1) ? 1 : -1;
            else return 0; // 不在同一直线上

            int cx = x1 + dx;
            int cy = y1 + dy;
            while (cx != x2 || cy != y2)
            {
                if (getPieceAt(cx, cy, userData))
                    count++;
                cx += dx;
                cy += dy;
            }
            return count;
        }

        // ==================== 车：横竖直线，无子阻拦可走任意格 ====================
        static bool CanJuGoto(int fc, int fr, int tc, int tr,
            GetPieceAtFunc getPieceAt, void* userData)
        {
            if (fc != tc && fr != tr) return false; // 必须同行或同列
            if (getPieceAt && CountPiecesBetween(fc, fr, tc, tr, getPieceAt, userData) > 0)
                return false; // 中间有子不能走
            return true;
        }

        static void ListJuMoves(int col, int row, PieceColor color,
            std::vector<POS>& out, GetPieceAtFunc getPieceAt, void* userData)
        {
            // 四个方向延伸
            int dirs[4][2] = { {0,1},{0,-1},{1,0},{-1,0} };
            for (auto& d : dirs)
            {
                for (int step = 1; ; step++)
                {
                    int nc = col + d[0] * step;
                    int nr = row + d[1] * step;
                    if (!IsInBoard(nc, nr)) break;
                    if (getPieceAt)
                    {
                        CChessPiece* p = getPieceAt(nc, nr, userData);
                        if (p)
                        {
                            if (p->GetColor() != color)
                                out.emplace_back(nc, nr); // 可吃子
                            break; // 遇到棋子停止
                        }
                    }
                    out.emplace_back(nc, nr);
                }
            }
        }

        // ==================== 马：走日字，蹩马腿 ====================
        static bool CanMaGoto(int fc, int fr, int tc, int tr,
            GetPieceAtFunc getPieceAt, void* userData)
        {
            int dc = abs(tc - fc);
            int dr = abs(tr - fr);
            bool isRi = ((dc == 1 && dr == 2) || (dc == 2 && dr == 1));
            bool isMu = ((dc == 3 && dr == 1) || (dc == 1 && dr == 3));
            if (!isRi && !isMu)
                return false;
            if (isMu)
                return true; // 目字大跳：不蹩腿、不越子，直接可达

            // 日字：检查蹩马腿
            if (getPieceAt)
            {
                int legCol = fc, legRow = fr;
                if (dc == 2) legCol = fc + (tc > fc ? 1 : -1); // 横走两格，腿在横向相邻
                else         legRow = fr + (tr > fr ? 1 : -1); // 竖走两格，腿在竖向相邻
                if (getPieceAt(legCol, legRow, userData))
                    return false;
            }
            return true;
        }

        static void ListMaMoves(int col, int row, PieceColor color,
            std::vector<POS>& out, GetPieceAtFunc getPieceAt, void* userData)
        {
            // 8个可能的日字位置，以及对应的蹩腿位置
            struct MoveInfo { int dc, dr, legCol, legRow; };
            MoveInfo moves[8] = {
                {1,2, 0,1}, {-1,2, 0,1}, {1,-2, 0,-1}, {-1,-2, 0,-1},
                {2,1, 1,0}, {-2,1, -1,0}, {2,-1, 1,0}, {-2,-1, -1,0}
            };
            for (auto& m : moves)
            {
                int nc = col + m.dc;
                int nr = row + m.dr;
                if (!IsInBoard(nc, nr)) continue;
                // 检查蹩腿
                int legCol = col + m.legCol;
                int legRow = row + m.legRow;
                if (getPieceAt && getPieceAt(legCol, legRow, userData))
                    continue;
                // 检查目标位置
                if (getPieceAt)
                {
                    CChessPiece* p = getPieceAt(nc, nr, userData);
                    if (p && p->GetColor() == color)
                        continue;
                }
                out.emplace_back(nc, nr);
            }

            // 升级：目字大跳（直3横1 / 直1横3），不蹩腿、不越子
            int mu[8][2] = {
                {3, 1}, {3, -1}, {-3, 1}, {-3, -1},
                {1, 3}, {1, -3}, {-1, 3}, {-1, -3}
            };
            for (auto& m2 : mu)
            {
                int nc = col + m2[0];
                int nr = row + m2[1];
                if (!IsInBoard(nc, nr)) continue;
                if (getPieceAt)
                {
                    CChessPiece* p = getPieceAt(nc, nr, userData);
                    if (p && p->GetColor() == color)
                        continue;
                }
                out.emplace_back(nc, nr);
            }
        }

        // ==================== 相/象：走田字，塞象眼，不可过河 ====================
        static bool CanXiangGoto(int fc, int fr, int tc, int tr, PieceColor color,
            GetPieceAtFunc getPieceAt, void* userData)
        {
            int dc = abs(tc - fc);
            int dr = abs(tr - fr);
            if (dc != 2 || dr != 2) return false; // 必须走田字
#ifndef SHI_XIANG_GUOHE
            // 不可过河
            if (HasCrossedRiver(tr, color)) return false;
#endif
            // 检查塞象眼（田字中心）
            if (getPieceAt)
            {
                int eyeCol = (fc + tc) / 2;
                int eyeRow = (fr + tr) / 2;
                if (getPieceAt(eyeCol, eyeRow, userData))
                    return false;
            }
            return true;
        }

        static void ListXiangMoves(int col, int row, PieceColor color,
            std::vector<POS>& out, GetPieceAtFunc getPieceAt, void* userData)
        {
            int dirs[4][2] = { {2,2},{-2,2},{2,-2},{-2,-2} };
            for (auto& d : dirs)
            {
                int nc = col + d[0];
                int nr = row + d[1];
                if (!IsInBoard(nc, nr)) continue;
#ifndef SHI_XIANG_GUOHE
                if (HasCrossedRiver(nr, color)) continue;
#endif
                // 塞象眼
                int eyeCol = (col + nc) / 2;
                int eyeRow = (row + nr) / 2;
                if (getPieceAt && getPieceAt(eyeCol, eyeRow, userData))
                    continue;
                if (getPieceAt)
                {
                    CChessPiece* p = getPieceAt(nc, nr, userData);
                    if (p && p->GetColor() == color)
                        continue;
                }
                out.emplace_back(nc, nr);
            }
        }

        // ==================== 仕/士：不可过河，在斜线上无距离限制 ====================
        static bool CanShiGoto(int fc, int fr, int tc, int tr, PieceColor color,
            GetPieceAtFunc getPieceAt, void* userData)
        {
            int dc = abs(tc - fc);
            int dr = abs(tr - fr);
            if (dc != dr || dc == 0) return false; // 必须斜线移动，且至少走一格

#ifndef SHI_XIANG_GUOHE
            // 不可过河
            if (HasCrossedRiver(tr, color)) return false;
#endif
            // 斜线路径上不能有棋子阻挡
            if (getPieceAt && CountPiecesBetweenDiag(fc, fr, tc, tr, getPieceAt, userData) > 0)
                return false;

            return true;
        }

        // 统计斜线路径上的棋子数（不含端点）
        static int CountPiecesBetweenDiag(int x1, int y1, int x2, int y2,
            GetPieceAtFunc getPieceAt, void* userData)
        {
            if (!getPieceAt) return 0;
            int dx = (x2 > x1) ? 1 : -1;
            int dy = (y2 > y1) ? 1 : -1;
            if (abs(x2 - x1) != abs(y2 - y1)) return -1; // 不是斜线

            int count = 0;
            int cx = x1 + dx;
            int cy = y1 + dy;
            while (cx != x2 || cy != y2)
            {
                if (getPieceAt(cx, cy, userData))
                    count++;
                cx += dx;
                cy += dy;
            }
            return count;
        }

        static void ListShiMoves(int col, int row, PieceColor color,
            std::vector<POS>& out, GetPieceAtFunc getPieceAt, void* userData)
        {
            // 四个斜线方向，每个方向逐步延伸直到出界或遇到棋子
            int dirs[4][2] = { {1,1},{-1,1},{1,-1},{-1,-1} };
            for (auto& d : dirs)
            {
                for (int step = 1; ; step++)
                {
                    int nc = col + d[0] * step;
                    int nr = row + d[1] * step;
                    if (!IsInBoard(nc, nr)) break;
#ifndef SHI_XIANG_GUOHE
                    if (HasCrossedRiver(nr, color)) break; // 不能过河
#endif
                    if (getPieceAt)
                    {
                        CChessPiece* p = getPieceAt(nc, nr, userData);
                        if (p)
                        {
                            if (p->GetColor() != color)
                                out.emplace_back(nc, nr); // 可吃子
                            break; // 遇到棋子停止
                        }
                    }
                    out.emplace_back(nc, nr);
                }
            }
        }
        static bool IsOwnPiece(int col, int row, PieceColor myColor, GetPieceAtFunc getPieceAt,void* ud) {
            auto target = getPieceAt(col, row, ud);
            return target && target->GetColor() == myColor;
        }
        static  void ListJiangMoves(int fromCol, int fromRow, PieceColor color, std::vector<POS>& result, GetPieceAtFunc getPieceAt, void* userData) {
            int dx[] = { 0, 0, 1, -1 };
            int dy[] = { 1, -1, 0, 0 };

            for (int i = 0; i < 4; ++i) {
                int nx = fromCol + dx[i];
                int ny = fromRow + dy[i];
                // 必须在九宫内且不能吃己方棋子
                if (IsInPalace(nx, ny, color) && !IsOwnPiece(nx, ny, color, getPieceAt, userData)) {
                    result.push_back({ nx, ny });
                }
            }
        }

        // 2. 炮：横竖直线，移动不越子，吃子隔一子
        static void ListPaoMoves(int fromCol, int fromRow, PieceColor color, std::vector<POS>& result, GetPieceAtFunc getPieceAt, void* userData) {
            int dx[] = { 0, 0, 1, -1 };
            int dy[] = { 1, -1, 0, 0 };

            for (int dir = 0; dir < 4; ++dir) {
                int cx = fromCol + dx[dir];
                int cy = fromRow + dy[dir];
                bool hasJumped = false; // 是否已经翻过一个子

                while (cx >= 0 && cx <= MAX_COLS && cy >= 0 && cy <= MAX_ROWS) {
                    auto target = getPieceAt(cx, cy, userData);
                    if (!target) {
                        // 如果还没翻过子，可以走到空位
                        if (!hasJumped) result.push_back({ cx, cy });
                    }
                    else {
                        if (!hasJumped) {
                            // 遇到第一个子，架起炮台
                            hasJumped = true;
                        }
                        else {
                            // 遇到第二个子，如果是敌方则可吃，如果是己方则不可吃且停止
                            if (target->GetColor() != color) {
                                result.push_back({ cx, cy });
                            }
                            break; // 无论敌我，遇到第二个子后该方向结束
                        }
                    }
                    cx += dx[dir];
                    cy += dy[dir];
                }
            }
        }

        // 3. 兵/卒：过河前只能前进一步，过河后可前、左、右，不可后退
        static   void ListBingMoves(int fromCol, int fromRow, PieceColor color, std::vector<POS>& result, GetPieceAtFunc getPieceAt, void* userData) {
            int forward = (color == RED) ? -1 : 1;

            // 1. 向前一步
            int fx = fromCol;
            int fy = fromRow + forward;
            if (fy >= 0 && fy <= MAX_ROWS && !IsOwnPiece(fx, fy, color, getPieceAt,userData)) {
                result.push_back({ fx, fy });
            }

            // 2. 判断是否过河 (红方 row <= 4 为过河, 黑方 row >= 6 为过河)
            bool crossedRiver = false;
            if (color == RED && fromRow <= 4) crossedRiver = true;
            if (color == BLACK && fromRow >= 6) crossedRiver = true;
            
            if (crossedRiver) {
                int dx[] = { 1, -1 }; // 左右
                for (int i = 0; i < 2; ++i) {
                    int nx = fromCol + dx[i];
                    int ny = fromRow;
                    if (nx >= 0 && nx <= 10 && !IsOwnPiece(nx, ny, color, getPieceAt, userData)) {
                        result.push_back({ nx, ny });
                    }
                }
            }
        }

        // 4. 后：九宫内横竖斜无限制（类似 confined Queen）
        static void ListHouMoves(int fromCol, int fromRow, PieceColor color, std::vector<POS>& result, GetPieceAtFunc getPieceAt, void* userData) {
            int dx[] = { 0, 0, 1, -1, 1, 1, -1, -1 }; // 横竖 + 斜向
            int dy[] = { 1, -1, 0, 0, 1, -1, 1, -1 };

            for (int dir = 0; dir < 8; ++dir) {
                int cx = fromCol + dx[dir];
                int cy = fromRow + dy[dir];

                while (cx >= 0 && cx <= MAX_COLS && cy >= 0 && cy <= MAX_ROWS) {
                    // 必须在九宫内
                    if (!IsInPalace(cx, cy, color)) break;

                    auto target = getPieceAt(cx, cy, userData);
                    if (!target) {
                        result.push_back({ cx, cy });
                    }
                    else {
                        // 遇到棋子，如果是敌方则可吃
                        if (target->GetColor() != color) {
                            result.push_back({ cx, cy });
                        }
                        break; // 无论敌我，遇到棋子后该方向结束
                    }
                    cx += dx[dir];
                    cy += dy[dir];
                }
            }
        }

        // 5. 军：全图横竖斜，但最多走一格（类似受限的 King，但无九宫限制）
        static void ListJunMoves(int fromCol, int fromRow, PieceColor color, std::vector<POS>& result, GetPieceAtFunc getPieceAt, void* userData) {
            // 8个方向：横、竖、斜
            int dx[] = { 0, 0, 1, -1, 1, 1, -1, -1 };
            int dy[] = { 1, -1, 0, 0, 1, -1, 1, -1 };

            for (int dir = 0; dir < 8; ++dir) {
                int cx = fromCol + dx[dir];
                int cy = fromRow + dy[dir];

                // 不再需要 while 循环，直接判断目标点是否在棋盘内
                if (cx >= 0 && cx <= MAX_COLS && cy >= 0 && cy <= MAX_ROWS) {
                    auto target = getPieceAt(cx, cy, userData);
                    // 目标点为空，或者目标点是敌方棋子（可以吃）
                    if (!target || target->GetColor() != color) {
                        result.push_back({ cx, cy });
                    }
                }
            }
        }
		// 5. 督：全图横竖斜无限制（类似 Queen）
        static void ListDuMoves(int fromCol, int fromRow, PieceColor color,
            std::vector<POS>& result, GetPieceAtFunc getPieceAt, void* userData) {
            int dx[] = { 0, 0, 1, -1, 1, 1, -1, -1 };
            int dy[] = { 1, -1, 0, 0, 1, -1, 1, -1 };

            for (int dir = 0; dir < 8; ++dir) {
                int cx = fromCol + dx[dir];
                int cy = fromRow + dy[dir];

                // 把边界检查放进 while 条件里，更清晰
                while (cx >= 0 && cx <= MAX_COLS && cy >= 0 && cy <= MAX_ROWS) {
                    auto target = getPieceAt(cx, cy, userData);
                    if (target) {
                        // 遇到棋子：如果是敌方就加入结果，然后无论敌我都停止该方向
                        if (target->GetColor() != color) {
                            result.push_back({ cx, cy });
                        }
                        break;
                    }
                    // 空位：加入结果，并继续沿该方向步进
                    result.push_back({ cx, cy });
                    cx += dx[dir];
                    cy += dy[dir];
                }
            }
        }

    };
}
