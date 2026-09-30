#include "CGameRecord.h"
#include <fstream>
#include <cstdio>
#include <cwchar>

// ==================== UTF-8 编解码辅助 ====================
// 棋谱文件统一用 UTF-8（带 BOM），与工程源文件编码一致。
static std::wstring Utf8Decode(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n);
    return out;
}
static std::string Utf8Encode(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::string out(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n, nullptr, nullptr);
    return out;
}
static bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY));
}

// ==================== 记法名称 ====================
// 标准中国象棋字：红 车马相士帅炮兵（军/督/后 自定义，红黑同字）
static const wchar_t* NotateName(E_PieceType pt, PieceColor side) {
    switch (pt) {
    case PT_JU:    return L"车";
    case PT_MA:    return L"马";
    case PT_XIANG: return (side == RED) ? L"相" : L"象";
    case PT_SHI:   return (side == RED) ? L"士" : L"仕";
    case PT_JIANG: return (side == RED) ? L"帅" : L"将";
    case PT_PAO:   return L"炮";
    case PT_BING:  return (side == RED) ? L"兵" : L"卒";
    case PT_HOU:   return L"后";
    case PT_JUN:   return L"军";
    case PT_DU:    return L"督";
    default:       return L"?";
    }
}
// 走棋方视角列号：红方从右到左 1-11（col=10→1），黑方从左到右 1-11（col=0→1）
static int ViewCol(int x, PieceColor side) {
    return (side == RED) ? (11 - x) : (x + 1);
}
// 视角列号 → 棋盘列
static int BoardCol(int viewCol, PieceColor side) {
    return (side == RED) ? (11 - viewCol) : (viewCol - 1);
}
// 是否朝"进"方向（红 y 减小、黑 y 增大）
static bool IsForward(PieceColor side, int fRow, int tRow) {
    if (side == RED) return tRow < fRow;
    return tRow > fRow;
}

std::wstring CGameRecord::MakeNotation(const std::vector<CChessPiece>& board, PieceColor side,
    int fCol, int fRow, int tCol, int tRow, const wchar_t* capturedLabel) {
    // 定位移动子
    const CChessPiece* mover = nullptr;
    for (const auto& p : board) {
        if (p.GetX() == fCol && p.GetY() == fRow && p.GetColor() == side) { mover = &p; break; }
    }
    if (!mover) return L"";

    std::wstring s;
    // 同名同列冲突：加 前/后 前缀（中国象棋规则）
    int sameColSameName = 0;
    bool moverIsFront = true; // 只要有任一同列同名子比 mover 更前，mover 即为"后"
    for (const auto& p : board) {
        if (p.GetColor() != side || p.GetType() != mover->GetType()) continue;
        if (p.GetX() != fCol) continue;
        sameColSameName++;
        if (&p == mover) continue;
        // 红方 y 小者在前；黑方 y 大者在前
        bool otherFront = (side == RED) ? (p.GetY() < fRow) : (p.GetY() > fRow);
        if (otherFront) moverIsFront = false;
    }
    if (sameColSameName > 1) {
        // 中国象棋惯例：有 前/后 前缀时省略列号
        s += moverIsFront ? L"前" : L"后";
        s += NotateName(mover->GetType(), side);
    } else {
        s += NotateName(mover->GetType(), side);
        s += std::to_wstring(ViewCol(fCol, side));
    }

    // 动作
    int dc = tCol - fCol, dr = tRow - fRow;
    if (dc == 0) {
        // 直走：进/退 N
        int n = abs(dr);
        s += IsForward(side, fRow, tRow) ? L"进" : L"退";
        s += std::to_wstring(n);
    } else if (dr == 0) {
        // 横走：平 目标列
        s += L"平";
        s += std::to_wstring(ViewCol(tCol, side));
    } else {
        // 斜走（督/后/军/相/士/马）：进/退 + 目标列（中国象棋马/相风格）
        s += IsForward(side, fRow, tRow) ? L"进" : L"退";
        s += std::to_wstring(ViewCol(tCol, side));
    }

    // 吃子 / 将军 / 绝杀 后缀（调用方负责传入准确标记）
    if (capturedLabel && capturedLabel[0]) {
        s += L" 吃";
        s += capturedLabel;
    }
    return s;
}

bool CGameRecord::ParseNotation(const std::wstring& text, PieceColor side,
    const std::vector<CChessPiece>& board, int& fCol, int& fRow, int& tCol, int& tRow) {
    // 例：督六进三 / 前车进三 / 后平四。吃掉后缀"吃X/将军/绝杀"
    std::wstring t = text;
    for (;;) {
        size_t k = t.find(L"吃");
        if (k != std::wstring::npos) { t = t.substr(0, k); continue; }
        break;
    }
    for (;;) {
        size_t k = t.find(L"将军");
        if (k != std::wstring::npos) { t = t.substr(0, k); continue; }
        break;
    }
    size_t k2 = t.find(L"绝杀");
    if (k2 != std::wstring::npos) t = t.substr(0, k2);

    // 前/后 前缀：必须后跟棋子名（否则"后7平4"的"后"是棋子名而非前缀）
    bool front = false, back = false;
    {
        auto isNameStart = [](wchar_t c) {
            return c == L'车' || c == L'马' || c == L'相' || c == L'象' || c == L'士' || c == L'仕' ||
                   c == L'帅' || c == L'将' || c == L'炮' || c == L'兵' || c == L'卒' ||
                   c == L'后' || c == L'军' || c == L'督';
        };
        if (t.size() > 1 && (t[0] == L'前' || t[0] == L'后') && isNameStart(t[1])) {
            front = (t[0] == L'前');
            back = (t[0] == L'后');
            t = t.substr(1);
        }
    }

    // 棋子名 + 列号
    const wchar_t* names[11] = { L"车", L"马", L"相", L"象", L"士", L"仕", L"帅", L"将", L"炮", L"兵", L"卒" };
    E_PieceType ntypes[11] = { PT_JU, PT_MA, PT_XIANG, PT_XIANG, PT_SHI, PT_SHI, PT_JIANG, PT_JIANG, PT_PAO, PT_BING, PT_BING };
    const wchar_t* names2[4] = { L"后", L"军", L"督", L"?" };
    E_PieceType ntypes2[4] = { PT_HOU, PT_JUN, PT_DU, PT_NONE };

    E_PieceType pt = PT_NONE;
    int nameLen = 0;
    for (int i = 0; i < 11; i++) {
        if (t.rfind(names[i], 0) == 0) { pt = ntypes[i]; nameLen = (int)wcslen(names[i]); break; }
    }
    if (pt == PT_NONE) {
        for (int i = 0; i < 3; i++) {
            if (t.rfind(names2[i], 0) == 0) { pt = ntypes2[i]; nameLen = (int)wcslen(names2[i]); break; }
        }
    }
    if (pt == PT_NONE) return false;

    std::wstring rest = t.substr(nameLen);
    // 列号：连续阿拉伯数字（支持 1-11，含两位 10/11）；无列号时（前/后 前缀记法）viewCol=0
    int viewCol = 0;
    {
        size_t i = 0;
        while (i < rest.size() && rest[i] >= L'0' && rest[i] <= L'9') {
            viewCol = viewCol * 10 + (rest[i] - L'0');
            i++;
        }
        rest = rest.substr(i);
    }
    if (viewCol < 1 || viewCol > 11) {
        // 无列号：必须有 前/后 前缀，否则无法定位
        if (!front && !back) return false;
    }

    // 在 board 中定位源棋子（名 + 列 + 前/后）
    int srcCol = -1, srcRow = -1;
    if (viewCol >= 1 && viewCol <= 11) {
        // 有列号：按列筛选
        for (const auto& p : board) {
            if (p.GetColor() != side || p.GetType() != pt) continue;
            if (ViewCol(p.GetX(), side) != viewCol) continue;
            if (front || back) {
                // 该列是否只有一个此子？(记法含前/后，说明该列有多个)
                int cnt = 0;
                for (const auto& q : board) if (q.GetColor() == side && q.GetType() == pt && q.GetX() == p.GetX()) cnt++;
                if (cnt < 2) continue;
                // 该列内 y 排序：红方最小者为前，黑方最大者为前
                int minY = 99, maxY = -1;
                for (const auto& q : board) if (q.GetColor() == side && q.GetType() == pt && q.GetX() == p.GetX()) {
                    if (q.GetY() < minY) minY = q.GetY();
                    if (q.GetY() > maxY) maxY = q.GetY();
                }
                bool thisFront = (side == RED) ? (p.GetY() == minY) : (p.GetY() == maxY);
                if (front && !thisFront) continue;
                if (back && thisFront) continue;
            }
            srcCol = p.GetX(); srcRow = p.GetY();
            break;
        }
    } else {
        // 无列号（前/后 前缀）：在该色该类型全部棋子中选最前/最后
        int bestY = -1;
        for (const auto& p : board) {
            if (p.GetColor() != side || p.GetType() != pt) continue;
            int y = p.GetY();
            bool better = false;
            if (front) better = (side == RED) ? (y < bestY || bestY < 0) : (y > bestY || bestY < 0);
            else better = (side == RED) ? (y > bestY || bestY < 0) : (y < bestY || bestY < 0);
            if (better) { bestY = y; srcCol = p.GetX(); srcRow = y; }
        }
    }
    if (srcCol < 0) return false;

    // 动作
    wchar_t act = 0;
    int num = 0;
    if (rest.size() >= 1) act = rest[0];
    {
        size_t i = 1;
        while (i < rest.size() && rest[i] >= L'0' && rest[i] <= L'9') { num = num * 10 + (rest[i] - L'0'); i++; }
    }
    if (act == L'进' || act == L'退') {
        int dirRow = (act == L'进') ? ((side == RED) ? -1 : 1) : ((side == RED) ? 1 : -1);
        // 候选1：直走 num 格
        int straightRow = srcRow + dirRow * num;
        bool straightOK = (straightRow >= 0 && straightRow <= 10);
        // 候选2：斜走到目标列 num（要求列差 == 行差，即 45 度斜线）
        int diagCol = BoardCol(num, side);
        int dCol = abs(diagCol - srcCol);
        int diagRow = srcRow + dirRow * dCol;
        bool diagOK = (diagCol != srcCol && diagRow >= 0 && diagRow <= 10);
        if (straightOK) {
            tCol = srcCol; tRow = straightRow;
        } else if (diagOK) {
            tCol = diagCol; tRow = diagRow;
        } else {
            return false;
        }
    } else if (act == L'平') {
        tCol = BoardCol(num, side);
        tRow = srcRow;
    } else {
        return false;
    }
    if (tCol < 0 || tCol > 10 || tRow < 0 || tRow > 10) return false;
    fCol = srcCol; fRow = srcRow;
    return true;
}

// ==================== 保存 / 加载 ====================
bool CGameRecord::SaveToFile(const std::wstring& path, const std::wstring& eventTime, const std::wstring& result) const {
    std::wstring content;
    content += L"ZSChessGame v1\r\n";
    content += L"Event " + eventTime + L"\r\n";
    content += L"Result " + result + L"\r\n";
    content += L"Moves\r\n";
    for (const auto& s : m_steps) {
        content += std::to_wstring(s.number) + L". ";
        content += (s.side == RED) ? L"红 " : L"黑 ";
        content += s.notation;
        content += L" ";
        content += std::to_wstring(s.fromCol) + L"," + std::to_wstring(s.fromRow) +
            L"-" + std::to_wstring(s.toCol) + L"," + std::to_wstring(s.toRow);
        content += L"\r\n";
    }
    std::string utf8 = Utf8Encode(content);
    // 写 UTF-8 带 BOM
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    f.write((const char*)bom, 3);
    f.write(utf8.data(), (std::streamsize)utf8.size());
    f.close();
    return true;
}

bool CGameRecord::LoadFromFile(const std::wstring& path, std::vector<RecordStep>& steps,
    std::wstring& eventTime, std::wstring& result) {
    steps.clear();
    eventTime.clear();
    result.clear();
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF)
        data = data.substr(3);
    std::wstring wdata = Utf8Decode(data);

    // 逐行解析
    std::vector<std::wstring> lines;
    {
        std::wstring cur;
        for (wchar_t c : wdata) {
            if (c == L'\n') { lines.push_back(cur); cur.clear(); }
            else if (c != L'\r') cur += c;
        }
        if (!cur.empty()) lines.push_back(cur);
    }

    bool inMoves = false;
    for (const auto& line : lines) {
        if (line.rfind(L"ZSChessGame", 0) == 0) continue;
        if (line.rfind(L"Event ", 0) == 0) { eventTime = line.substr(6); continue; }
        if (line.rfind(L"Result ", 0) == 0) { result = line.substr(7); continue; }
        if (line.rfind(L"Moves", 0) == 0) { inMoves = true; continue; }
        if (!inMoves || line.empty()) continue;

        // 格式：N. 红|黑 记法 fx,fy-tx,ty（按空格拆分，最后一段为坐标，中间为记法）
        RecordStep s;
        std::vector<std::wstring> toks;
        {
            std::wstring cur;
            for (wchar_t c : line) {
                if (c == L' ') { if (!cur.empty()) { toks.push_back(cur); cur.clear(); } }
                else cur += c;
            }
            if (!cur.empty()) toks.push_back(cur);
        }
        int fx = -1, fy = -1, tx = -1, ty = -1;
        if (toks.size() >= 3) {
            const std::wstring& coord = toks[toks.size() - 1];
            // 形如 5,8-5,5
            size_t dash = coord.find(L'-');
            size_t c1 = coord.find(L',');
            size_t c2 = coord.find(L',', c1 + 1);
            if (dash != std::wstring::npos && c1 != std::wstring::npos && c2 != std::wstring::npos && c2 > dash) {
                fx = _wtoi(coord.substr(0, c1).c_str());
                fy = _wtoi(coord.substr(c1 + 1, dash - c1 - 1).c_str());
                tx = _wtoi(coord.substr(dash + 1, c2 - dash - 1).c_str());
                ty = _wtoi(coord.substr(c2 + 1).c_str());
            }
        }
        // 序号：第一段 "N."
        int num = 0;
        PieceColor side = RED;
        if (!toks.empty()) {
            std::wstring first = toks[0];
            if (!first.empty() && first.back() == L'.') first.pop_back();
            num = _wtoi(first.c_str());
        }
        if (toks.size() >= 2) {
            if (toks[1] == L"黑") side = BLACK;
            else if (toks[1] == L"红") side = RED;
        }
        s.number = num;
        s.side = side;
        s.fromCol = fx; s.fromRow = fy; s.toCol = tx; s.toRow = ty;
        // 记法：中间段拼接（可能含空格，如 "吃子 将军"）
        {
            std::wstring notPart;
            for (size_t i = 2; i + 1 < toks.size(); i++) {
                if (i > 2) notPart += L' ';
                notPart += toks[i];
            }
            wcsncpy_s(s.notation, notPart.c_str(), 47);
        }
        // 吃子/将军/绝杀 标记（从记法文本解析）
        if (wcsstr(s.notation, L"吃")) s.wasCapture = true;
        if (wcsstr(s.notation, L"绝杀")) { s.mate = true; }
        if (wcsstr(s.notation, L"将军")) s.inCheck = true;
        steps.push_back(s);
    }
    return !steps.empty();
}

// ==================== 工具 ====================
std::wstring CGameRecord::MakeTimestampFilename(const std::wstring& result) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[64];
    swprintf_s(buf, L"%04d%02d%02d_%02d%02d%02d_%s.txt",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, result.c_str());
    return buf;
}

std::wstring CGameRecord::DefaultRecordDir() {
    wchar_t exe[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    size_t slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) dir = dir.substr(0, slash);
    dir += L"\\棋谱";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring CGameRecord::MakePositionText(const std::vector<CChessPiece>& board, PieceColor side) {
    // 网格字符映射：空=.. ；红大写、黑小写
    static const wchar_t* map[2][11] = {
        { L"K", L"C", L"M", L"X", L"S", L"H", L"A", L"P", L"J", L"D", L"?" },  // RED
        { L"k", L"c", L"m", L"x", L"s", L"h", L"a", L"p", L"j", L"d", L"?" }   // BLACK
    };
    static const E_PieceType order[10] = { PT_JIANG, PT_JU, PT_MA, PT_XIANG, PT_SHI, PT_HOU, PT_PAO, PT_BING, PT_JUN, PT_DU };
    // 网格查表
    wchar_t grid[11][11][3];
    for (int y = 0; y < 11; y++) for (int x = 0; x < 11; x++) wcscpy_s(grid[y][x], L"..");
    for (const auto& p : board) {
        int x = p.GetX(), y = p.GetY();
        if (x < 0 || x > 10 || y < 0 || y > 10) continue;
        E_PieceType pt = p.GetType();
        int idx = -1;
        for (int i = 0; i < 10; i++) if (order[i] == pt) { idx = i; break; }
        if (idx < 0) continue;
        wcscpy_s(grid[y][x], map[(p.GetColor() == RED) ? 0 : 1][idx]);
    }

    std::wstring s;
    s += L"ZSChess Position v1\r\n";
    s += (side == RED) ? L"Side: RED\r\n" : L"Side: BLACK\r\n";
    s += L"Board:\r\n";
    for (int y = 0; y < 11; y++) {
        for (int x = 0; x < 11; x++) { s += grid[y][x]; s += L" "; }
        s += L"\r\n";
    }
    // 引擎可直接构造的棋子列表
    s += L"Pieces: ";
    bool first = true;
    for (const auto& p : board) {
        if (!first) s += L", ";
        first = false;
        s += (p.GetColor() == RED) ? L"R_" : L"B_";
        switch (p.GetType()) {
        case PT_JIANG: s += L"JIANG"; break;
        case PT_JU:    s += L"CHE"; break;
        case PT_MA:    s += L"MA"; break;
        case PT_XIANG: s += L"XIANG"; break;
        case PT_SHI:   s += L"SHI"; break;
        case PT_HOU:   s += L"HOU"; break;
        case PT_PAO:   s += L"PAO"; break;
        case PT_BING:  s += L"BING"; break;
        case PT_JUN:   s += L"JUN"; break;
        case PT_DU:    s += L"DU"; break;
        default:       s += L"?"; break;
        }
        s += L"(" + std::to_wstring(p.GetX()) + L"," + std::to_wstring(p.GetY()) + L")";
    }
    s += L"\r\n";
    return s;
}

void CGameRecord::ApplyStep(std::vector<CChessPiece>& board, const RecordStep& s) {
    for (auto it = board.begin(); it != board.end();) {
        if (it->GetX() == s.toCol && it->GetY() == s.toRow) { it = board.erase(it); break; }
        else ++it;
    }
    for (auto& p : board) {
        if (p.GetX() == s.fromCol && p.GetY() == s.fromRow && p.GetColor() == s.side) {
            p.SetX(s.toCol); p.SetY(s.toRow);
            break;
        }
    }
}
