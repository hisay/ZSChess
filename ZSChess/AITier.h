// AITier.h
// AI 等级系统：六档等级映射到搜索资源。
// 勾选"使用AI等级"时，等级参数（深度/时限/线程）取自 ai_config.ini 的 [level0..5] 段
// （未配置的项用内建默认值）；未勾选时沿用用户手动配置（深度/线程/时限）+ 等级噪声/杀王偏好。
#pragma once
#include <Windows.h>

// 避开 windows.h 的 min/max 宏：自定义封顶辅助
inline int tier_cap(int v, int maxv) { return v < maxv ? v : maxv; }

enum class AITier {
    NEWBIE = 0,        // 新手
    ENTRY,             // 入门
    INTERMEDIATE,      // 中级
    ADVANCED,          // 高级
    MASTER,            // 大师
    GRANDMASTER,       // 特级
    COUNT
};

struct TierConfig {
    int depth;        // 深度上限（<=0 表示用满配置）
    int timeMs;       // 每步时限
    int threads;      // 线程数
    int noiseCenti;   // 低等级评估扰动（百分之一分；0=确定性）
    int noiseProb;    // 扰动概率（%）
    bool preferMate;  // 战术加成：优先杀王
};

inline const wchar_t* TierName(AITier t) {
    static const wchar_t* names[] = {
        L"新手", L"入门", L"中级", L"高级", L"大师", L"特级"
    };
    int i = (int)t;
    if (i < 0) i = 0;
    if (i >= (int)AITier::COUNT) i = (int)AITier::COUNT - 1;
    return names[i];
}

// 等级参数预设：内建默认值，由 AIConfigDlg::LoadConfig 从 ini [level0..5] 段覆盖
struct TierPresets {
    bool useTier = true;         // 勾选"使用AI等级"：等级参数直接生效
    int depth[6]   = { 3, 5, 8, 12, 18, 24 };
    int timeMs[6]  = { 5000, 8000, 14200, 20000, 33500, 45000 };
    int threads[6] = { 1, 3, 7, 12, 18, 24 };
};

// 全局等级参数源（ini 读入 / 默认）
inline TierPresets& TierPresetData() { static TierPresets p; return p; }

// 由配置参数宏观决定等级（加权总分，参数越大等级越高）
inline AITier TierFromConfig(int depth, int maxThreads, int timeSec) {
    int d = depth < 0 ? 0 : depth;
    int th = maxThreads < 0 ? 0 : maxThreads;
    int ts = timeSec < 0 ? 0 : timeSec;
    int score = d * 3 + tier_cap(th, 8) + tier_cap(ts, 15) * 2;
    if (score >= 95)  return AITier::GRANDMASTER;   // 约 depth>=23 且资源足
    if (score >= 62)  return AITier::MASTER;        // 约 depth>=16
    if (score >= 40)  return AITier::ADVANCED;      // 约 depth>=10
    if (score >= 24)  return AITier::INTERMEDIATE;  // 约 depth>=6
    if (score >= 10)  return AITier::ENTRY;         // 约 depth>=3
    return AITier::NEWBIE;
}

// 等级对应的引擎参数与战术加成
// - 勾选"使用AI等级"（TierPresetData().useTier）：直接用该档 ini/默认 深度·时限·线程
// - 未勾选：用用户手动配置（cfgDepth/cfgTimeMs/cfgThreads 为上限），等级仅提供噪声/杀王偏好
inline TierConfig TierToConfig(AITier t, int cfgDepth, int cfgTimeMs, int cfgThreads) {
    TierConfig c;
    int lv = (int)t;
    if (lv < 0) lv = 0;
    if (lv > 5) lv = 5;
    if (TierPresetData().useTier) {
        // 勾选：等级参数全部来自该档预设（ini [levelN] 或内建默认）
        c.depth = TierPresetData().depth[lv];
        c.timeMs = TierPresetData().timeMs[lv];
        c.threads = TierPresetData().threads[lv];
    }
    else {
        // 未勾选：现状逻辑（手动配置封顶）
        switch (t) {
        case AITier::NEWBIE:
            c.depth = tier_cap(cfgDepth <= 0 ? 3 : cfgDepth, 3);
            c.timeMs = tier_cap(cfgTimeMs <= 0 ? 400 : cfgTimeMs, 400);
            c.threads = 1;
            break;
        case AITier::ENTRY:
            c.depth = tier_cap(cfgDepth <= 0 ? 5 : cfgDepth, 5);
            c.timeMs = tier_cap(cfgTimeMs <= 0 ? 700 : cfgTimeMs, 700);
            c.threads = 1;
            break;
        case AITier::INTERMEDIATE:
            c.depth = tier_cap(cfgDepth <= 0 ? 8 : cfgDepth, 8);
            c.timeMs = tier_cap(cfgTimeMs <= 0 ? 1200 : cfgTimeMs, 1200);
            c.threads = 2;
            break;
        case AITier::ADVANCED:
            c.depth = tier_cap(cfgDepth <= 0 ? 12 : cfgDepth, 12);
            c.timeMs = tier_cap(cfgTimeMs <= 0 ? 2000 : cfgTimeMs, 2000);
            c.threads = 4;
            break;
        case AITier::MASTER:
            c.depth = tier_cap(cfgDepth <= 0 ? 16 : cfgDepth, 16);
            c.timeMs = tier_cap(cfgTimeMs <= 0 ? 3500 : cfgTimeMs, 3500);
            c.threads = 6;
            break;
        case AITier::GRANDMASTER:
        default:
            c.depth = cfgDepth > 0 ? cfgDepth : 20;   // 用满配置深度
            c.timeMs = cfgTimeMs > 0 ? cfgTimeMs : 5000; // 用满配置时限
            c.threads = cfgThreads > 0 ? cfgThreads : 8;
            break;
        }
    }
    // 噪声/杀王偏好（按等级，与是否勾选无关）
    switch (t) {
    case AITier::NEWBIE: c.noiseCenti = 120; c.noiseProb = 70; c.preferMate = false; break;
    case AITier::ENTRY:  c.noiseCenti = 80;  c.noiseProb = 45; c.preferMate = false; break;
    case AITier::INTERMEDIATE: c.noiseCenti = 30; c.noiseProb = 15; c.preferMate = false; break;
    case AITier::ADVANCED: c.noiseCenti = 0; c.noiseProb = 0; c.preferMate = false; break;
    case AITier::MASTER: c.noiseCenti = 0; c.noiseProb = 0; c.preferMate = false; break;
    case AITier::GRANDMASTER:
    default: c.noiseCenti = 0; c.noiseProb = 0; c.preferMate = true; break; // 特级战术加成：优先杀王
    }
    return c;
}
