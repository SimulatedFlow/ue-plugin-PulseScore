// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PulseScoreTypes.h"
#include "PulseScoreAsset.generated.h"

class USoundBase;

/**
 * The piece: tempo, time signature, the stems, the sections and the stingers.
 *
 * A data asset rather than a component or an actor, because a score is content. A composer hands over a
 * folder of stems and a note about which one comes in when; this is where that note lives, in a form the
 * conductor can read. Nothing here knows about a world, which is also why the whole selection maths in
 * UPulseScoreStatics can be unit-tested against a score built in a transient package.
 *
 * A UPrimaryDataAsset so a project can load scores by asset id and cook only the ones a level references.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Pulse Score"))
class PULSESCORE_API UPulseScoreAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	//~ Tempo -------------------------------------------------------------------------------------------

	/**
	 * Beats per minute. Every stem in the score has to be written at this tempo.
	 *
	 * PulseScore does not time-stretch. It cannot: stretching a stem in real time is a different plugin with
	 * a different cost, and a layered score whose layers disagree about the tempo is not a score. The clock
	 * is set from this number, and the bar length that every quantised switch is measured against comes from
	 * it and BeatsPerBar alone.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tempo",
		meta = (ClampMin = "20.0", ClampMax = "400.0", UIMin = "40.0", UIMax = "220.0"))
	float Tempo = 120.0f;

	/** Numerator of the time signature - how many beats make a bar. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tempo", meta = (ClampMin = "1", ClampMax = "32"))
	int32 BeatsPerBar = 4;

	//~ Content -----------------------------------------------------------------------------------------

	/**
	 * The stems, in mix order.
	 *
	 * All of them start together and none of them ever stops before the score does. The order is what the
	 * overlay lists them in; it has no effect on the mix.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Score")
	TArray<FPulseScoreLayer> Layers;

	/** The named parts of the piece and the graph of what may follow what. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Score")
	TArray<FPulseScoreSection> Sections;

	/** The section PlayScore enters. None uses the first entry in Sections. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Score")
	FName StartSection;

	/**
	 * One-shots, addressed by name: transition hits, ambush stings, the accent on a door opening.
	 *
	 * A stinger is not a layer. It is fired once, on a boundary, and it ducks the layers marked for it while
	 * it sounds. It does not have to loop and it does not have to match the tempo - though it will sound a
	 * great deal better if it does.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Score")
	TMap<FName, TObjectPtr<USoundBase>> Stingers;

	//~ Queries -----------------------------------------------------------------------------------------

	/** Seconds one bar lasts at this score's tempo and time signature. */
	UFUNCTION(BlueprintPure, Category = "PulseScore")
	float GetSecondsPerBar() const;

	/** Seconds one beat lasts at this score's tempo. */
	UFUNCTION(BlueprintPure, Category = "PulseScore")
	float GetSecondsPerBeat() const;

	/** Index into Layers, or INDEX_NONE. */
	UFUNCTION(BlueprintPure, Category = "PulseScore")
	int32 FindLayerIndex(FName LayerId) const;

	/** Index into Sections, or INDEX_NONE. */
	UFUNCTION(BlueprintPure, Category = "PulseScore")
	int32 FindSectionIndex(FName SectionId) const;

	/** The section to start in: StartSection when it exists, otherwise the first one, otherwise None. */
	UFUNCTION(BlueprintPure, Category = "PulseScore")
	FName GetEffectiveStartSection() const;

	/** The sound behind a stinger name, or null. */
	UFUNCTION(BlueprintPure, Category = "PulseScore")
	USoundBase* FindStinger(FName StingerId) const;

	/**
	 * Everything wrong with this score, in plain sentences.
	 *
	 * Called on PlayScore and logged, so a score with a layer that has no sound says so once at the moment
	 * it is asked to play rather than being silently one stem short for the rest of the level. Returns true
	 * when the score is playable at all - which is a lower bar than being free of problems.
	 */
	bool Validate(TArray<FString>& OutProblems) const;

#if WITH_EDITOR
	//~ UObject interface
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
