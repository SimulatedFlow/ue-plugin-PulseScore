// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "PulseScoreAsset.h"
#include "PulseScoreLog.h"
#include "Sound/SoundBase.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "PulseScoreAsset"

float UPulseScoreAsset::GetSecondsPerBar() const
{
	return GetSecondsPerBeat() * FMath::Max(BeatsPerBar, 1);
}

float UPulseScoreAsset::GetSecondsPerBeat() const
{
	return Tempo > 0.0f ? 60.0f / Tempo : 0.0f;
}

int32 UPulseScoreAsset::FindLayerIndex(FName LayerId) const
{
	return Layers.IndexOfByPredicate([LayerId](const FPulseScoreLayer& Layer)
	{
		return Layer.Id == LayerId;
	});
}

int32 UPulseScoreAsset::FindSectionIndex(FName SectionId) const
{
	return Sections.IndexOfByPredicate([SectionId](const FPulseScoreSection& Section)
	{
		return Section.Id == SectionId;
	});
}

FName UPulseScoreAsset::GetEffectiveStartSection() const
{
	if (!StartSection.IsNone() && FindSectionIndex(StartSection) != INDEX_NONE)
	{
		return StartSection;
	}

	// A score with sections but no valid start section still has an obvious answer: the first one. Refusing
	// to play because a name was left blank would be pedantry, and a score with no sections at all is a
	// perfectly reasonable thing - it is a piece that is only ever mixed by intensity.
	return Sections.Num() > 0 ? Sections[0].Id : NAME_None;
}

USoundBase* UPulseScoreAsset::FindStinger(FName StingerId) const
{
	const TObjectPtr<USoundBase>* Found = Stingers.Find(StingerId);
	return Found ? Found->Get() : nullptr;
}

bool UPulseScoreAsset::Validate(TArray<FString>& OutProblems) const
{
	if (Tempo <= 0.0f)
	{
		OutProblems.Add(TEXT("Tempo is zero or negative. Nothing can be quantised against a clock that does not run."));
	}

	if (BeatsPerBar < 1)
	{
		OutProblems.Add(TEXT("BeatsPerBar is below 1. A bar has to contain at least one beat."));
	}

	if (Layers.Num() == 0)
	{
		OutProblems.Add(TEXT("The score has no layers. There is nothing to mix."));
	}

	int32 PlayableLayers = 0;
	TSet<FName> SeenLayerIds;
	for (int32 Index = 0; Index < Layers.Num(); ++Index)
	{
		const FPulseScoreLayer& Layer = Layers[Index];

		if (Layer.Id.IsNone())
		{
			OutProblems.Add(FString::Printf(TEXT("Layer %d has no Id. It cannot be named in a section list or read off the overlay."), Index));
		}
		else if (SeenLayerIds.Contains(Layer.Id))
		{
			OutProblems.Add(FString::Printf(TEXT("Layer Id '%s' is used more than once. One of them will never be addressable."), *Layer.Id.ToString()));
		}
		else
		{
			SeenLayerIds.Add(Layer.Id);
		}

		if (!Layer.Sound)
		{
			OutProblems.Add(FString::Printf(TEXT("Layer '%s' has no sound."), *Layer.Id.ToString()));
		}
		else
		{
			++PlayableLayers;
		}

		if (Layer.IntensityMax <= Layer.IntensityMin)
		{
			OutProblems.Add(FString::Printf(
				TEXT("Layer '%s' has an empty intensity window (%.2f..%.2f). It can never be heard."),
				*Layer.Id.ToString(), Layer.IntensityMin, Layer.IntensityMax));
		}
	}

	TSet<FName> SeenSectionIds;
	for (int32 Index = 0; Index < Sections.Num(); ++Index)
	{
		const FPulseScoreSection& Section = Sections[Index];

		if (Section.Id.IsNone())
		{
			OutProblems.Add(FString::Printf(TEXT("Section %d has no Id."), Index));
		}
		else if (SeenSectionIds.Contains(Section.Id))
		{
			OutProblems.Add(FString::Printf(TEXT("Section Id '%s' is used more than once."), *Section.Id.ToString()));
		}
		else
		{
			SeenSectionIds.Add(Section.Id);
		}

		for (const FName& Next : Section.AllowedNextSections)
		{
			if (FindSectionIndex(Next) == INDEX_NONE)
			{
				OutProblems.Add(FString::Printf(
					TEXT("Section '%s' allows a transition to '%s', which is not a section in this score."),
					*Section.Id.ToString(), *Next.ToString()));
			}
		}

		for (const FName& LayerId : Section.LayerIds)
		{
			if (FindLayerIndex(LayerId) == INDEX_NONE)
			{
				OutProblems.Add(FString::Printf(
					TEXT("Section '%s' lists layer '%s', which is not a layer in this score."),
					*Section.Id.ToString(), *LayerId.ToString()));
			}
		}

		if (!Section.TransitionStinger.IsNone() && !Stingers.Contains(Section.TransitionStinger))
		{
			OutProblems.Add(FString::Printf(
				TEXT("Section '%s' names transition stinger '%s', which is not in the Stingers map."),
				*Section.Id.ToString(), *Section.TransitionStinger.ToString()));
		}
	}

	for (const TPair<FName, TObjectPtr<USoundBase>>& Pair : Stingers)
	{
		if (!Pair.Value)
		{
			OutProblems.Add(FString::Printf(TEXT("Stinger '%s' has no sound."), *Pair.Key.ToString()));
		}
	}

	if (!StartSection.IsNone() && FindSectionIndex(StartSection) == INDEX_NONE)
	{
		OutProblems.Add(FString::Printf(
			TEXT("StartSection '%s' is not a section in this score. The first section will be used instead."),
			*StartSection.ToString()));
	}

	// Playable is a lower bar than clean. A score missing one stem out of five is still worth hearing, and
	// the log has already said which stem is missing - refusing to play it would help nobody.
	return Tempo > 0.0f && BeatsPerBar >= 1 && PlayableLayers > 0;
}

#if WITH_EDITOR
EDataValidationResult UPulseScoreAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	TArray<FString> Problems;
	const bool bPlayable = Validate(Problems);

	for (const FString& Problem : Problems)
	{
		// Everything is a warning except being unplayable, which is the error. A score with a typo in a
		// section list should still cook; a score that cannot make a sound should stop somebody.
		Context.AddWarning(FText::FromString(Problem));
	}

	if (!bPlayable)
	{
		Context.AddError(LOCTEXT("PulseScoreUnplayable",
			"This score cannot play: it needs a positive tempo, at least one beat per bar and at least one layer with a sound."));
		return EDataValidationResult::Invalid;
	}

	return Problems.Num() > 0 ? EDataValidationResult::Valid : Result;
}
#endif

#undef LOCTEXT_NAMESPACE
