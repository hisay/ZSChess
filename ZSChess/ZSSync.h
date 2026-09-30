// ZSSync.h
// 坐标一致性校验：以引擎坐标为绝对基准（x==col 从左到右 0-10，y==row 从上到下 0-10）。
// 原则（用户确认）：
//   1. from,to 坐标是唯一确定性数据；同一坐标下要么唯一一子、要么空位。
//   2. 界面棋子列表与引擎 Position 对同一坐标必须完全一致（空对空、子对子：颜色+类型语义）。
//   3. 一旦出现"界面有子/引擎无子"或"同坐标棋子类型不同"，即严重 BUG，必须落日志并可中断对局。
// 本文件为纯头文件，供 AutoTrainer / CQiPan 等同时持有界面棋盘与引擎 Position 的模块调用。
#pragma once
#include <Windows.h>
#include <vector>
#include <string>
#include <cstdio>
#include "CChessPiece.h"
#include "position.h"

namespace ZSSync {

    // ---------- 界面 E_PieceType ↔ 引擎 PieceType 语义映射 ----------
    inline int UiToEngineType(E_PieceType t) {
        switch (t) {
        case PT_BING:  return zschess::BING;   // 兵
        case PT_JUN:   return zschess::JUN;    // 军
        case PT_XIANG: return zschess::XIANG;  // 象
        case PT_SHI:   return zschess::SHI;    // 士
        case PT_MA:    return zschess::MA;     // 马
        case PT_PAO:   return zschess::PAO;    // 炮
        case PT_HOU:   return zschess::HOU;    // 后
        case PT_JU:    return zschess::CHE;    // 车
        case PT_DU:    return zschess::DU;     // 督
        case PT_JIANG: return zschess::JIANG;  // 将
        default:       return zschess::NO_PIECE_TYPE;
        }
    }
    inline E_PieceType EngineToUiType(int t) {
        switch (t) {
        case zschess::BING:  return PT_BING;
        case zschess::JUN:   return PT_JUN;
        case zschess::XIANG: return PT_XIANG;
        case zschess::SHI:   return PT_SHI;
        case zschess::MA:    return PT_MA;
        case zschess::PAO:   return PT_PAO;
        case zschess::HOU:   return PT_HOU;
        case zschess::CHE:   return PT_JU;
        case zschess::DU:    return PT_DU;
        case zschess::JIANG: return PT_JIANG;
        default:             return PT_NONE;
        }
    }

    // 中文棋子名（日志用）
    inline const wchar_t* UiTypeName(E_PieceType t) {
        switch (t) {
        case PT_JU:    return L"车";
        case PT_MA:    return L"马";
        case PT_XIANG: return L"象";
        case PT_SHI:   return L"士";
        case PT_JIANG: return L"将";
        case PT_PAO:   return L"炮";
        case PT_BING:  return L"兵";
        case PT_HOU:   return L"后";
        case PT_JUN:   return L"军";
        case PT_DU:    return L"督";
        default:       return L"?";
        }
    }

    // ---------- 日志 ----------
    // 写 exe 目录 sync_error.log（UTF-8 BOM，追加）
    inline void LogSyncError(const std::wstring& section, const std::wstring& detail) {
        wchar_t exe[MAX_PATH] = { 0 };
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring dir = exe;
        size_t slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) dir = dir.substr(0, slash);
        dir += L"\\sync_error.log";
        FILE* f = nullptr;
        if (_wfopen_s(&f, dir.c_str(), L"a, ccs=UTF-8") == 0 && f) {
            SYSTEMTIME st; GetLocalTime(&st);
            fwprintf(f, L"[%04d-%02d-%02d %02d:%02d:%02d] [%s] %s\r\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                section.c_str(), detail.c_str());
            fclose(f);
        }
    }

    // ---------- 界面棋盘自检（无需引擎 Position） ----------
    // 检查：坐标越界、同一格出现多个棋子（重复坐标）、将/帅缺失。
    // 返回错误数；report 收集明细。
    inline int VerifyUiBoardSelf(const std::vector<CChessPiece>& board, std::wstring& report) {
        int errs = 0;
        int grid[121] = { 0 };
        bool hasRedKing = false, hasBlackKing = false;
        wchar_t buf[128];
        for (const auto& p : board) {
            int x = p.GetX(), y = p.GetY();
            if (x < 0 || x > 10 || y < 0 || y > 10) {
                swprintf_s(buf, L"越界坐标 (%d,%d) %s%s", x, y,
                    p.GetColor() == RED ? L"红" : L"黑", UiTypeName(p.GetType()));
                report += buf; report += L"\n"; errs++;
                continue;
            }
            int idx = y * 11 + x;
            if (grid[idx]) {
                swprintf_s(buf, L"重复坐标 (%d,%d)：%s%s 共存", x, y,
                    p.GetColor() == RED ? L"红" : L"黑", UiTypeName(p.GetType()));
                report += buf; report += L"\n"; errs++;
                continue;
            }
            grid[idx] = 1;
            if (p.GetType() == PT_JIANG) {
                if (p.GetColor() == RED) hasRedKing = true;
                else hasBlackKing = true;
            }
        }
        if (!hasRedKing) { report += L"缺少红将\n"; errs++; }
        if (!hasBlackKing) { report += L"缺少黑将\n"; errs++; }
        return errs;
    }

    // ---------- 界面 ↔ 引擎 全棋盘一致性校验 ----------
    // pos    = 引擎当前局面（绝对基准）
    // board  = 界面棋子列表（CChessPiece）
    // 返回不一致数；report 收集最多 maxReport 条差异。
    inline int VerifyBoardSync(const zschess::Position& pos,
                               const std::vector<CChessPiece>& board,
                               std::wstring& report, int maxReport = 8) {
        int errs = 0;
        int uiGrid[121] = { 0 };
        for (const auto& p : board) {
            int x = p.GetX(), y = p.GetY();
            if (x < 0 || x > 10 || y < 0 || y > 10) continue; // 越界由 VerifyUiBoardSelf 负责
            uiGrid[y * 11 + x]++;
        }
        wchar_t buf[192];
        for (int y = 0; y < 11; y++) {
            for (int x = 0; x < 11; x++) {
                int idx = y * 11 + x;
                zschess::Piece ep = pos.piece_on(x, y);
                bool eEmpty = (ep == zschess::EMPTY);
                bool uEmpty = (uiGrid[idx] == 0);
                if (eEmpty != uEmpty) {
                    if (errs < maxReport) {
                        if (eEmpty && !uEmpty) {
                            // 界面有子、引擎无子：找界面该子信息
                            const CChessPiece* up = nullptr;
                            for (const auto& p : board)
                                if (p.GetX() == x && p.GetY() == y) { up = &p; break; }
                            swprintf_s(buf, L"(%d,%d) 界面有子(%s%s) 引擎为空", x, y,
                                up && up->GetColor() == RED ? L"红" : L"黑",
                                up ? UiTypeName(up->GetType()) : L"?");
                        } else {
                            swprintf_s(buf, L"(%d,%d) 界面为空 引擎有子", x, y);
                        }
                        report += buf; report += L"\n";
                    }
                    errs++;
                    continue;
                }
                if (eEmpty) continue;
                // 双方都有子：对比颜色与类型语义
                const CChessPiece* up = nullptr;
                for (const auto& p : board)
                    if (p.GetX() == x && p.GetY() == y) { up = &p; break; }
                if (!up) continue;
                bool colorOk = ((up->GetColor() == RED) == (zschess::piece_color(ep) == zschess::RED));
                bool typeOk = (UiToEngineType(up->GetType()) == zschess::piece_type(ep));
                if (!colorOk || !typeOk) {
                    if (errs < maxReport) {
                        swprintf_s(buf, L"(%d,%d) 界面=%s%s 引擎=%s%s",
                            x, y,
                            up->GetColor() == RED ? L"红" : L"黑", UiTypeName(up->GetType()),
                            zschess::piece_color(ep) == zschess::RED ? L"红" : L"黑",
                            UiTypeName(EngineToUiType(zschess::piece_type(ep))));
                        report += buf; report += L"\n";
                    }
                    errs++;
                }
            }
        }
        return errs;
    }

    // ---------- 落子前来源校验（AI 着法进界面前） ----------
    // from 坐标在界面必须有一个属于 side 的棋子；返回 true 表示可用。
    inline bool VerifyMoveSource(const std::vector<CChessPiece>& board, PieceColor side,
                                 int fromCol, int fromRow) {
        if (fromCol < 0 || fromCol > 10 || fromRow < 0 || fromRow > 10) return false;
        for (const auto& p : board) {
            if (p.GetX() == fromCol && p.GetY() == fromRow && p.GetColor() == side) return true;
        }
        return false;
    }

} // namespace ZSSync
