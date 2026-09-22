// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PulseScoreTypes.h"
#include "PulseScoreStatics.generated.h"

class UPulseScoreAsset;
class UPulseScoreSubsystem;

/**
 * Everything PulseScore can do, reachable from a Blueprint node with no component in the way.
 *
 * Two halves, and the split matters.
 *
 * The first half forwards to the world's conductor: play, stop, intensity, section, stinger, state. A
 * project that wants one music system for the whole game calls these and never places anything.
 *
 * The second half is the arithmetic - which layer is how loud at intensity x, where the next boundary
 * falls, whether a transition is legal, what a duck does to a set of gains - and it is **static, pure and
 * world-free**. That is not tidiness. It is the LootForge lesson written into the design: a
 * GameInstanceSubsystem cannot be NewObject'd in an automation test, so any logic that lives inside one is
 * logic that is never tested. Here the maths sits in functions that take a data asset and a float, so the
 * tests exercise the same code the mixer runs - not a re-implementation of it that agrees with it until
 * somebody edits one of the two.
 */
UCLASS(meta = (DisplayName = "Pulse Score"))
class PULSESCORE_API UPulseScoreStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	//~ The conductor ------------------------------------------------------------------------------------

	/** The PulseScore subsystem for the context object's world, or null outside a game world. */
	UFUNCTION(BlueprintPure, Category = "PulseScore", meta = (WorldContext = "WorldContextObject"))
	static UPulseScoreSubsystem* GetPulseScore(const UObject* WorldContextObject);

	/**
	 * Load a score, start its clock and start every layer together.
	 *
	 * The boundary applies to the first note: Immediate starts now, Bar waits for the clock's next bar line,
	 * which is what you want when a score is taking over from one that is already running.
	 */
	UFUNCTION(BlueprintCallable, Category = "PulseScore", meta = (WorldContext = "WorldContextObject"))
	static bool PlayScore(const UObject* WorldContextObject, UPulseScoreAsset* Score,
		EPulseScoreQuantization When = EPulseScoreQuantization::Immediate);

	/** Fade the layers out and stop the clock on the given boundary. */
	UFUNCTION(BlueprintCallable, Category = "PulseScore", meta = (WorldContext = "WorldContextObject"))
	static void StopScore(const UObject* WorldContextObject,
		EPulseScoreQuantization When = EPulseScoreQuantization::Default);

	/**
	 * Tell the conductor how hot the situation is, 0..1.
	 *
	 * This is the whole game-facing API in one call. It does not name a stem, it does not know what a stem
	 * is, and that is the point: the composer decides what 0.7 sounds like, in the score asset, without a
	 * line of gameplay code changing.
	 */
	UFUNCTION(BlueprintCallable, Category = "PulseScore", meta = (WorldContext = "WorldContextObject"))
	static void SetIntensity(const UObject* WorldContextObject, float Intensity,
		EPulseScoreQuantization When = EPulseScoreQuantization::Default);

	/** The intensity the mix is currently built from. */
	UFUNCTION(BlueprintPure, Category = "PulseScore", meta = (WorldContext = "WorldContextObject"))
	static float GetIntensity(const UObject* WorldContextObject);

	/**
	 * Move to a named section on the given boundary.
	 *
	 * False when the score has no such section, or when the current section's allow-list does not permit it
	 * and strict transitions are on. A refusal is logged with the reason - it is never silent, because a
	 * section change that quietly did not happen is the hardest kind of audio bug to find.
	 */
	UFUNCTION(BlueprintCallable, Category = "PulseScore", meta = (WorldContext = "WorldContextObject"))
	static bool EnterSection(const UObject* WorldContextObject, FName SectionId,
		EPulseScoreQuantization When = EPulseScoreQuantization::Default);

	/** The section that is sounding. */
	UFUNCTION(BlueprintPure, Category = "PulseScore", meta = (WorldContext = "WorldContextObject"))
	static FName GetCurrentSection(const UObject* WorldContextObject);

	/** Fire a named one-shot on the given boundary and duck the layers marked for it while it sounds. */
	UFUNCTION(BlueprintCallable, Category = "PulseScore", meta = (WorldContext = "WorldContextObject"))
	static bool TriggerStinger(const UObject* WorldContextObject, FName StingerId,
		EPulseScoreQuantization When = EPulseScoreQuantization::Default);

	/** Everything the conductor knows. Empty and not playing when there is no score. */
	UFUNCTION(BlueprintPure, Category = "PulseScore", meta = (WorldContext = "WorldContextObject"))
	static FPulseScoreState GetScoreState(const UObject* WorldContextObject);

	/** A score is loaded and its clock is running. */
	UFUNCTION(BlueprintPure, Category = "PulseScore", meta = (WorldContext = "WorldContextObject"))
	static bool IsScorePlaying(const UObject* WorldContextObject);

	/** Turn the counters overlay on or off. Same thing PulseScore.Show does. */
	UFUNCTION(BlueprintCallable, Category = "PulseScore|Debug", meta = (WorldContext = "WorldContextObject"))
	static void SetShowOverlay(const UObject* WorldContextObject, bool bShow);

	//~ The arithmetic -----------------------------------------------------------------------------------

	/**
	 * How loud one layer is at one intensity, before its trim and before ducking.
	 *
	 * The shape: silent at and outside the window edges, full across the middle, and an edge-width ramp at
	 * each end. Two exceptions, both of them the musically obvious thing rather than the geometrically
	 * consistent one:
	 *
	 *   A window whose top is at 1 has no upper ramp. Intensity cannot go above 1, so a fade-out there would
	 *   mean the layer written for maximum intensity goes silent exactly when the fight is at its worst.
	 *
	 *   A window whose bottom is at 0 has no lower ramp, for the same reason at the other end: the bed that
	 *   is supposed to be there when nothing is happening should not be silent when nothing is happening.
	 *
	 * EdgeWidth below zero means "use the project default"; the resolved value is passed in as
	 * DefaultEdgeWidth, so this function still has no dependency on settings, a world, or anything else.
	 */
	UFUNCTION(BlueprintPure, Category = "PulseScore|Maths")
	static float EvaluateLayerGain(const FPulseScoreLayer& Layer, float Intensity, float DefaultEdgeWidth = 0.15f);

	/**
	 * The gain of every layer in a score at one intensity, in one section.
	 *
	 * The order of operations, which is the whole mixer: window gain, then the layer trim, then the
	 * section's layer list, then the concurrency cap. Ducking is deliberately *not* in here - it is a live
	 * envelope, not a property of the score, and folding it in would make this function need a clock.
	 *
	 * MaxConcurrentLayers at or below zero means no cap.
	 */
	UFUNCTION(BlueprintCallable, Category = "PulseScore|Maths")
	static void EvaluateLayerGains(const UPulseScoreAsset* Score, float Intensity, FName SectionId,
		TArray<FPulseScoreLayerGain>& OutGains, int32 MaxConcurrentLayers = 0,
		float DefaultEdgeWidth = 0.15f, float AudibleGainThreshold = 0.01f);

	/**
	 * Pull the layers marked bDuckedByStinger down by DuckAmount, 0..1.
	 *
	 * Multiplicative, so releasing the duck restores the gains exactly - there is nothing stored and nothing
	 * to drift. Layers that are not marked come out bit-for-bit unchanged.
	 */
	UFUNCTION(BlueprintCallable, Category = "PulseScore|Maths")
	static void ApplyStingerDuck(UPARAM(ref) TArray<FPulseScoreLayerGain>& Gains, float DuckAmount);

	/** Seconds one bar lasts. Zero tempo or beats yields zero, and callers treat that as "no clock". */
	UFUNCTION(BlueprintPure, Category = "PulseScore|Maths")
	static float GetSecondsPerBar(float Tempo, int32 BeatsPerBar);

	/**
	 * Seconds from now until the next boundary of this kind.
	 *
	 * Zero when the clock is already sitting on one, so a call that arrives exactly on the bar line is not
	 * pushed a whole bar away. This is what the overlay counts down and what the subsystem uses to reject a
	 * Quartz notification that belongs to a switch it has already replaced - which is why it is worth having
	 * as a pure function rather than as three lines inside the scheduler.
	 *
	 * SectionEnd needs SectionStartSeconds and SectionLengthBars; with no declared length it falls back to
	 * the next bar.
	 */
	UFUNCTION(BlueprintPure, Category = "PulseScore|Maths")
	static float TimeUntilBoundary(EPulseScoreQuantization When, float ClockSeconds, float Tempo,
		int32 BeatsPerBar, float SectionStartSeconds = 0.0f, int32 SectionLengthBars = 0);

	/**
	 * Whether the score permits going from one section to another, and why not when it does not.
	 *
	 * Entering from nothing is always allowed, a section may always re-enter itself, and a section with an
	 * empty allow-list is unconstrained. A target that does not exist in the score is refused whatever the
	 * lists say - that one is a typo, not a design decision.
	 */
	UFUNCTION(BlueprintPure, Category = "PulseScore|Maths")
	static bool IsSectionTransitionAllowed(const UPulseScoreAsset* Score, FName FromSection, FName ToSection,
		FString& OutReason);

	/** Pull one layer's gain out of a state struct. What the demo HUD's per-layer bars are drawn from. */
	UFUNCTION(BlueprintPure, Category = "PulseScore|Maths")
	static float GetLayerGain(const FPulseScoreState& State, FName LayerId);

	/** Resolve Default against the project settings. Never returns Default. */
	UFUNCTION(BlueprintPure, Category = "PulseScore|Maths")
	static EPulseScoreQuantization ResolveQuantization(EPulseScoreQuantization When);
};
