// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "PulseScoreSettings.h"

UPulseScoreSettings::UPulseScoreSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("PulseScore");
}

FName UPulseScoreSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

FName UPulseScoreSettings::GetSectionName() const
{
	return TEXT("PulseScore");
}

const UPulseScoreSettings& UPulseScoreSettings::Get()
{
	const UPulseScoreSettings* Settings = GetDefault<UPulseScoreSettings>();
	check(Settings);
	return *Settings;
}
