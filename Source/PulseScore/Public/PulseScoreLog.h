// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

/** Everything PulseScore says. Refused transitions and missing stingers are logged here, never swallowed. */
DECLARE_LOG_CATEGORY_EXTERN(LogPulseScore, Log, All);
