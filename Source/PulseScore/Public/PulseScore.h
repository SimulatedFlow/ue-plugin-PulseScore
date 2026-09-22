// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Runtime module for PulseScore.
 *
 * Loads at PreDefault so the world subsystem, the component and the PulseScore.* console commands all exist
 * before the first game world is built. A level that starts its music on BeginPlay must find a conductor
 * already standing there, not one that arrives a phase later.
 */
class FPulseScoreModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
