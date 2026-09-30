#pragma once

int GetConfiguredAIDepth();
int GetConfiguredAITier();
int GetConfiguredAIMaxThreads();
bool GetConfiguredAIHeuristics();
bool GetConfiguredAIPreferMate();
int GetConfiguredAITimeSec();
int GetTrainRedTier();
int GetTrainBlackTier();
void SetTrainTiers(int red, int black);
int GetTrainGames();
void SetTrainGames(int n);
