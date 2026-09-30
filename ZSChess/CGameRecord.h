#pragma once
// 棋谱模块：记录/保存/加载/复盘 每一步走法。
// - 中文记法（类中国象棋：红方列从右到左 1-11、黑方列从左到右 1-11；直走"进/退N"、横走"平N"、斜走"进/退+目标列"）
// - 每步同时保存引擎坐标 from(x,y)-to(x,y)，AI 可直接解析复现
// - 文件格式：文本，人类可读，AI 可解析
#include <Windows.h>
#include <vector>
#include <string>
#include "CChessPiece.h"

struct RecordStep {
    int number = 0;               // 第几步（1-based）
    PieceColor side = RED;        // 走棋方
    wchar_t notation[48] = { 0 }; // 中文记法（含 吃X / 将军 / 绝杀 后缀）
    int fromCol = -1, fromRow = -1;
    int toCol = -1, toRow = -1;
    bool wasCapture = false;
    wchar_t capturedLabel[8] = { 0 };
    bool inCheck = false;         // 走完后对对方将军
    bool mate = false;            // 走完后将死对方
    int evalScore = 0;            // 该步走完后引擎评估分（红方视角：正=红优 负=黑优；仅运行时显示，不写入棋谱文件）
};

class CGameRecord {
public:
    void Clear() { m_steps.clear(); }
    void Append(const RecordStep& s) { m_steps.push_back(s); }
    // 保留前 keepCount 步（删除之后的步，用于复盘节点重走时截断）
    void Truncate(int keepCount) {
        if (keepCount <= 0) { m_steps.clear(); return; }
        if ((int)m_steps.size() > keepCount) m_steps.resize(keepCount);
    }
    int Count() const { return (int)m_steps.size(); }
    const RecordStep& At(int idx) const { return m_steps[idx]; }

    // ---- 中文记法 ----
    // 由一次走子生成人类可读记法（board 为走子前的局面）
    static std::wstring MakeNotation(const std::vector<CChessPiece>& board, PieceColor side,
        int fCol, int fRow, int tCol, int tRow, const wchar_t* capturedLabel);
    // 由记法反解析出 from/to（board 为走子前的局面；成功返回 true）
    static bool ParseNotation(const std::wstring& text, PieceColor side,
        const std::vector<CChessPiece>& board, int& fCol, int& fRow, int& tCol, int& tRow);

    // ---- 文件 ----
    // 保存整局棋谱到文件；eventTime 形如 2026-09-28_14-15-30；result 形如 红胜/黑胜
    bool SaveToFile(const std::wstring& path, const std::wstring& eventTime, const std::wstring& result) const;
    // 从文件加载棋谱（成功返回 true；steps/eventTime/result 输出）
    static bool LoadFromFile(const std::wstring& path, std::vector<RecordStep>& steps,
        std::wstring& eventTime, std::wstring& result);

    // ---- 工具 ----
    // 生成终局文件名：YYYYMMDD_HHMMSS_结果.txt
    static std::wstring MakeTimestampFilename(const std::wstring& result);
    // exe 所在目录下的 棋谱 子目录（不存在则创建）
    static std::wstring DefaultRecordDir();
    // 生成"复制局面"文本：11x11 网格 + 引擎可构造的棋子列表 + 走方
    static std::wstring MakePositionText(const std::vector<CChessPiece>& board, PieceColor side);
    // 把一步应用到棋子列表（直接移动/移除目标；不校验）
    static void ApplyStep(std::vector<CChessPiece>& board, const RecordStep& s);

private:
    std::vector<RecordStep> m_steps;
};
