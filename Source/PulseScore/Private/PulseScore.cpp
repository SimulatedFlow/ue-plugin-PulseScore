// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "PulseScore.h"
#include "PulseScoreLog.h"

DEFINE_LOG_CATEGORY(LogPulseScore);

#define LOCTEXT_NAMESPACE "FPulseScoreModule"

void FPulseScoreModule::StartupModule()
{
	UE_LOG(LogPulseScore, Log, TEXT("PulseScore started."));
}

void FPulseScoreModule::ShutdownModule()
{
	UE_LOG(LogPulseScore, Log, TEXT("PulseScore shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPulseScoreModule, PulseScore)
