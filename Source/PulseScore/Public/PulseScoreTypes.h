// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "PulseScoreTypes.generated.h"

class USoundBase;

/**
 * When a switch is allowed to happen.
 *
 * This is the type that makes the plugin what it is. Every call into PulseScore carries one, and the
 * transition is scheduled against a Quartz clock rather than executed where the call happened to land in
 * the frame. Music that changes half a beat late does not sound "slightly late" - it sounds wrong, because
 * the ear measures it against the pulse it is already hearing.
 */
UENUM(BlueprintType)
enum class EPulseScoreQuantization : uint8
{
	/**
	 * Use the project default from Project Settings -> Plugins -> PulseScore.
	 *
	 * This is the default value of every Blueprint pin, so a project that decided once that its music
	 * switches on bars never types "Bar" again. It is a sentinel, not a boundary: it resolves before
	 * anything is scheduled, and the project setting can never itself be Default.
	 */
	Default			UMETA(DisplayName = "Project Default"),

	/** Right now, mid-bar, mid-note. Correct for a death sting, wrong for almost everything else. */
	Immediate		UMETA(DisplayName = "Immediate"),

	/** The next beat. The tightest musical boundary - responsive, and it can cut a held note. */
	Beat			UMETA(DisplayName = "Next Beat"),

	/** The next bar line. The default, and the one that is right when you are not sure. */
	Bar				UMETA(DisplayName = "Next Bar"),

	/** The next even pair of bars, for scores whose phrases are two bars long. */
	TwoBars			UMETA(DisplayName = "Next Two Bars"),

	/**
	 * The end of the section that is playing.
	 *
	 * Needs the section to declare a LengthInBars; a section with no length has no end to wait for, and this
	 * falls back to Bar rather than never firing. That fallback is deliberate - silently never switching is
	 * the worst of the three possible behaviours.
	 */
	SectionEnd		UMETA(DisplayName = "End of Section"),
};

/**
 * One stem of the score, and the window of intensity it belongs to.
 *
 * A layer is not a clip that gets started and stopped. It is started once with every other layer, on the
 * same Quartz clock, and it plays for as long as the score does. What changes is its gain. Out of its
 * window it plays at zero and stays exactly in phase with the rest, so when the drums come back in they are
 * on the same beat as the bass instead of one restart-latency behind it.
 */
USTRUCT(BlueprintType)
struct PULSESCORE_API FPulseScoreLayer
{
	GENERATED_BODY()

	/**
	 * The name this layer is addressed by, in the overlay and in the section allow-lists.
	 *
	 * Must be unique inside a score. Duplicates are not an error the plugin can fix for you, but the asset's
	 * own validation says so, because two layers called "Drums" means one of them will never be heard.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	FName Id;

	/**
	 * The sound. Anything a USoundBase can be - a MetaSound, a Sound Cue, a wave.
	 *
	 * It has to loop on its own. PulseScore does not re-trigger layers: a re-trigger is a new voice with a
	 * new start latency, and after four of them the stems have drifted apart. Set the looping on the asset
	 * and the conductor never has to touch it again.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	TObjectPtr<USoundBase> Sound;

	/** Bottom of the intensity window. At exactly this value the layer is silent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float IntensityMin = 0.0f;

	/** Top of the intensity window. At exactly this value the layer is silent, unless the window ends at 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float IntensityMax = 1.0f;

	/**
	 * How much of the window each edge takes to ramp from silent to full, in intensity units.
	 *
	 * Negative means "use the project default", which is how a score stays consistent without repeating a
	 * number on twelve layers. Zero is a hard gate: the layer is either in or out, which is occasionally
	 * what a percussion layer wants and almost never what a pad wants.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer",
		meta = (ClampMin = "-1.0", ClampMax = "0.5"))
	float EdgeWidth = -1.0f;

	/**
	 * Seconds - measured in bars - the gain takes to reach a new target.
	 *
	 * Negative means the project default. This is the smoothing constant of the layer's fader, so a value of
	 * one bar on a 120 BPM 4/4 score is two seconds of approach, not a two second linear ramp: intensity
	 * usually moves continuously, and a fader chasing a moving target is a smoother, not a ramp.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer",
		meta = (ClampMin = "-1.0", UIMax = "8.0"))
	float FadeBars = -1.0f;

	/** A constant trim on top of the window gain, for balancing stems that were not mixed together. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer",
		meta = (ClampMin = "0.0", UIMax = "2.0"))
	float VolumeScale = 1.0f;

	/** Duck this layer while a stinger plays. Leave off for the layer the stinger is supposed to sit with. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	bool bDuckedByStinger = true;

	/**
	 * Priority when more layers want to be audible than MaxConcurrentLayers allows.
	 *
	 * Higher wins. Two layers at the same priority are separated by their gain, so the cap never silences
	 * the loud one to keep a layer that was about to fade out anyway.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer", meta = (UIMin = "-8", UIMax = "8"))
	int32 Priority = 0;
};

/**
 * A named part of the piece, and the parts it is allowed to become.
 *
 * The allow-list is the point. A score that can go from Exploration to Combat but not from Victory back to
 * Ambush is a score with a shape, and the shape has to be written down somewhere the runtime can check it -
 * otherwise every caller has to remember it, and one of them will not.
 */
USTRUCT(BlueprintType)
struct PULSESCORE_API FPulseScoreSection
{
	GENERATED_BODY()

	/** The name the game passes to EnterSection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Section")
	FName Id;

	/**
	 * Sections this one may lead to. Empty means unconstrained.
	 *
	 * Empty is not a dead end, and reading it as one would turn "the author has not written the graph yet"
	 * into "the music can never change again". An author who wants a real dead end lists nothing but the
	 * section itself.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Section")
	TArray<FName> AllowedNextSections;

	/**
	 * Layers allowed to sound in this section. Empty means every layer, subject to its intensity window.
	 *
	 * This is a mute mask on top of the intensity mix, not a second mixer. A layer excluded here is silent
	 * but still playing, exactly like a layer outside its window, and for the same reason.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Section")
	TArray<FName> LayerIds;

	/** Stinger fired when this section is entered. Optional; None means the change is made quietly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Section")
	FName TransitionStinger;

	/**
	 * How long the section is, in bars. Zero means it has no declared length.
	 *
	 * The only thing that gives SectionEnd quantisation a meaning. It does not stop or loop anything - the
	 * layers keep playing regardless - it is a phrase length, so a change asked for at bar 3 of an 8 bar
	 * section waits for bar 8 instead of cutting the phrase in half.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Section", meta = (ClampMin = "0", UIMax = "64"))
	int32 LengthInBars = 0;

	/** Intensity forced on entry, 0..1. Negative leaves the intensity where the game put it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Section",
		meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float EntryIntensity = -1.0f;
};

/** What one layer is doing right now: the answer EvaluateLayerGains produces, one entry per layer. */
USTRUCT(BlueprintType)
struct PULSESCORE_API FPulseScoreLayerGain
{
	GENERATED_BODY()

	/** Matches FPulseScoreLayer::Id. */
	UPROPERTY(BlueprintReadOnly, Category = "PulseScore")
	FName LayerId;

	/** Linear gain, 0..1 before the layer trim and usually 0..VolumeScale after it. */
	UPROPERTY(BlueprintReadOnly, Category = "PulseScore")
	float Gain = 0.0f;

	/**
	 * Carried through from the layer so ducking needs nothing but the gain set.
	 *
	 * It is here so ApplyStingerDuck is a pure function of its argument: no asset lookup, no world, nothing
	 * to mock in a test.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "PulseScore")
	bool bDuckedByStinger = false;

	/** False when the concurrency cap or the section's layer list silenced a layer that its window allowed. */
	UPROPERTY(BlueprintReadOnly, Category = "PulseScore")
	bool bAllowedBySection = true;

	FPulseScoreLayerGain() = default;

	FPulseScoreLayerGain(FName InLayerId, float InGain, bool bInDucked, bool bInAllowed)
		: LayerId(InLayerId)
		, Gain(InGain)
		, bDuckedByStinger(bInDucked)
		, bAllowedBySection(bInAllowed)
	{
	}
};

/**
 * Everything the conductor knows, in one struct a Blueprint can read.
 *
 * The demo HUD is built entirely out of this, and that is the test of it: if a counters box cannot be
 * written from this struct alone, the struct is missing something the plugin should have been telling you.
 */
USTRUCT(BlueprintType)
struct PULSESCORE_API FPulseScoreState
{
	GENERATED_BODY()

	/** A score is loaded and its clock is running. */
	UPROPERTY(BlueprintReadOnly, Category = "PulseScore")
	bool bPlaying = false;

	/** Asset name of the score, for the overlay. None when nothing is playing. */
	UPROPERTY(BlueprintReadOnly, Category = "PulseScore")
	FName ScoreName;

	/** The section that is sounding. */
	UPROPERTY(BlueprintReadOnly, Category = "PulseScore")
	FName CurrentSection;

	/** The section that will be sounding at the next boundary. None when no change is queued. */
	UPROPERTY(BlueprintReadOnly, Category = "PulseScore")
	FName PendingSection;

	/** Seconds until the queued change lands. Zero when nothing is queued. The countdown on the overlay. */
	UPROPERTY(BlueprintReadOnly, Category = "PulseScore")
	float SecondsUntilPendingSection = 0.0f;

	/** The intensity the mix is currently built from, 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "PulseScore")
	float Intensity = 0.0f;

	/** The intensity waiting for its boundary. Equals Intensity when nothing is queued. */
	UPROPERTY(BlueprintReadOnly, Category = "PulseScore")
	float PendingIntensity = 0.0f;

	/** Beats per minute of the running clock. */
	UPROPERTY(BlueprintReadOnly, Category = "PulseScore")
	float Tempo = 0.0f;

	/** Numerator of the time signature. */
	UPROPERTY(BlueprintReadOnly, Category = "PulseScore")
	int32 BeatsPerBar = 4;

	/** Bar the clock is on, counting from 1. */
	UPROPERTY(BlueprintReadOnly, Category = "PulseScore")
	int32 Bar = 0;

	/** Beat inside the bar, counting from 1. */
	UPROPERTY(BlueprintReadOnly, Category = "PulseScore")
	int32 Beat = 0;

	/** How far through the current beat, 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "PulseScore")
	float BeatFraction = 0.0f;

	/** Seconds the clock has been running. */
	UPROPERTY(BlueprintReadOnly, Category = "PulseScore")
	float SecondsElapsed = 0.0f;

	/** Bars since the current section started, counting from 0. */
	UPROPERTY(BlueprintReadOnly, Category = "PulseScore")
	int32 BarsIntoSection = 0;

	/** How hard the stinger ducker is pulling right now, 0..1. Zero when no stinger is sounding. */
	UPROPERTY(BlueprintReadOnly, Category = "PulseScore")
	float DuckAmount = 0.0f;

	/** Gain per layer, in the score's layer order. */
	UPROPERTY(BlueprintReadOnly, Category = "PulseScore")
	TArray<FPulseScoreLayerGain> LayerGains;

	/** How many layers are above the audibility threshold, against MaxConcurrentLayers. */
	UPROPERTY(BlueprintReadOnly, Category = "PulseScore")
	int32 AudibleLayers = 0;
};

/**
 * The attack-hold-release envelope a stinger pulls the marked layers down with.
 *
 * Not a USTRUCT and not a UObject: it is three floats and a phase, it has no reason to be reflected, and
 * keeping it plain is what lets the automation test drive it a millisecond at a time with no world, no
 * audio device and no subsystem. The subsystem owns one; the tests own their own.
 */
struct PULSESCORE_API FPulseScoreDucker
{
	enum class EPhase : uint8
	{
		Idle,
		Attack,
		Hold,
		Release
	};

	/**
	 * Start (or restart) the duck.
	 *
	 * Retriggering while already ducking does not stack and does not restart from zero - it goes back to the
	 * hold phase at the current depth. Two stingers a beat apart must not pull the mix twice as far down as
	 * one, and they must not produce an audible step back up between them either.
	 */
	void Trigger(float InDepth, float InAttackSeconds, float InHoldSeconds, float InReleaseSeconds);

	/** Advance the envelope. Safe to call with a zero delta and safe to call while idle. */
	void Update(float DeltaSeconds);

	/** Cancel immediately, without a release. What Stop does. */
	void Reset();

	/** Current depth, 0 (no duck) to Depth (full duck). */
	float GetAmount() const { return Amount; }

	/** Anything but idle. */
	bool IsDucking() const { return Phase != EPhase::Idle; }

	/** Which part of the envelope is running. */
	EPhase GetPhase() const { return Phase; }

private:
	EPhase Phase = EPhase::Idle;
	float Amount = 0.0f;
	float Depth = 0.0f;
	float AttackSeconds = 0.05f;
	float HoldSeconds = 0.5f;
	float ReleaseSeconds = 0.8f;
	float PhaseElapsed = 0.0f;

	/** Depth the release started from, so a release interrupted mid-attack still ends at exactly zero. */
	float ReleaseFrom = 0.0f;
};
