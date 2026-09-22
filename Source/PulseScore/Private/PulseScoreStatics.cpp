// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "PulseScoreStatics.h"
#include "PulseScoreAsset.h"
#include "PulseScoreSettings.h"
#include "PulseScoreSubsystem.h"

namespace PulseScorePrivate
{
	/**
	 * How close to a boundary counts as being on it, in seconds.
	 *
	 * A tenth of a millisecond. Small enough that no musical decision turns on it, large enough that the
	 * floating point remainder of a clock that has been running for twenty minutes does not push a call that
	 * arrived exactly on the bar line a whole bar into the future.
	 */
	static constexpr float BoundaryTolerance = 1.0e-4f;

	/** Seconds from ClockSeconds to the next integer multiple of Period, or zero when already on one. */
	static float TimeToNextMultiple(float ClockSeconds, float Period)
	{
		if (Period <= 0.0f)
		{
			return 0.0f;
		}

		const float Clock = FMath::Max(ClockSeconds, 0.0f);
		const float Remainder = FMath::Fmod(Clock, Period);

		if (Remainder <= BoundaryTolerance)
		{
			return 0.0f;
		}

		return Period - Remainder;
	}
}

//~ The conductor ----------------------------------------------------------------------------------------

UPulseScoreSubsystem* UPulseScoreStatics::GetPulseScore(const UObject* WorldContextObject)
{
	return UPulseScoreSubsystem::Get(WorldContextObject);
}

bool UPulseScoreStatics::PlayScore(const UObject* WorldContextObject, UPulseScoreAsset* Score,
	EPulseScoreQuantization When)
{
	UPulseScoreSubsystem* Subsystem = UPulseScoreSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->PlayScore(Score, When) : false;
}

void UPulseScoreStatics::StopScore(const UObject* WorldContextObject, EPulseScoreQuantization When)
{
	if (UPulseScoreSubsystem* Subsystem = UPulseScoreSubsystem::Get(WorldContextObject))
	{
		Subsystem->Stop(When);
	}
}

void UPulseScoreStatics::SetIntensity(const UObject* WorldContextObject, float Intensity,
	EPulseScoreQuantization When)
{
	if (UPulseScoreSubsystem* Subsystem = UPulseScoreSubsystem::Get(WorldContextObject))
	{
		Subsystem->SetIntensity(Intensity, When);
	}
}

float UPulseScoreStatics::GetIntensity(const UObject* WorldContextObject)
{
	const UPulseScoreSubsystem* Subsystem = UPulseScoreSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetIntensity() : 0.0f;
}

bool UPulseScoreStatics::EnterSection(const UObject* WorldContextObject, FName SectionId,
	EPulseScoreQuantization When)
{
	UPulseScoreSubsystem* Subsystem = UPulseScoreSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->EnterSection(SectionId, When) : false;
}

FName UPulseScoreStatics::GetCurrentSection(const UObject* WorldContextObject)
{
	const UPulseScoreSubsystem* Subsystem = UPulseScoreSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetCurrentSection() : NAME_None;
}

bool UPulseScoreStatics::TriggerStinger(const UObject* WorldContextObject, FName StingerId,
	EPulseScoreQuantization When)
{
	UPulseScoreSubsystem* Subsystem = UPulseScoreSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->TriggerStinger(StingerId, When) : false;
}

FPulseScoreState UPulseScoreStatics::GetScoreState(const UObject* WorldContextObject)
{
	const UPulseScoreSubsystem* Subsystem = UPulseScoreSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->GetState() : FPulseScoreState();
}

bool UPulseScoreStatics::IsScorePlaying(const UObject* WorldContextObject)
{
	const UPulseScoreSubsystem* Subsystem = UPulseScoreSubsystem::Get(WorldContextObject);
	return Subsystem ? Subsystem->IsPlaying() : false;
}

void UPulseScoreStatics::SetShowOverlay(const UObject* WorldContextObject, bool bShow)
{
	if (UPulseScoreSubsystem* Subsystem = UPulseScoreSubsystem::Get(WorldContextObject))
	{
		Subsystem->SetShowOverlay(bShow);
	}
}

//~ The arithmetic ---------------------------------------------------------------------------------------

float UPulseScoreStatics::EvaluateLayerGain(const FPulseScoreLayer& Layer, float Intensity,
	float DefaultEdgeWidth)
{
	const float X = FMath::Clamp(Intensity, 0.0f, 1.0f);

	// Tolerate a window written backwards rather than treating it as empty. An author who typed 0.7 in Min
	// and 0.3 in Max meant a window, and refusing to hear one is a worse answer than reading it the obvious
	// way. A genuinely empty window - the two numbers equal - really is silent, and the asset says so.
	const float Low = FMath::Min(Layer.IntensityMin, Layer.IntensityMax);
	const float High = FMath::Max(Layer.IntensityMin, Layer.IntensityMax);
	if (High <= Low)
	{
		return 0.0f;
	}

	// A window that reaches an end of the intensity range has no ramp at that end. Intensity cannot go above
	// 1, so a fade-out at the top would silence the layer written for maximum intensity at exactly the
	// moment the fight is at its worst; the same argument at 0 keeps the quiet bed audible when nothing is
	// happening. This is the one place where the musically obvious answer beats the geometrically
	// consistent one, and it is worth the two extra branches.
	const bool bOpenBottom = Low <= 0.0f;
	const bool bOpenTop = High >= 1.0f;

	if ((X < Low && !bOpenBottom) || (X > High && !bOpenTop))
	{
		return 0.0f;
	}

	// Never let the two ramps overlap, or a narrow window would never reach full gain and the layer would
	// only ever be heard at half volume - which reads as a mixing bug, not as a window that is too narrow.
	const float RequestedEdge = Layer.EdgeWidth < 0.0f ? DefaultEdgeWidth : Layer.EdgeWidth;
	const float Edge = FMath::Clamp(RequestedEdge, 0.0f, (High - Low) * 0.5f);

	if (Edge <= 0.0f)
	{
		// A hard gate. The closed edges are silent, so the window boundaries still read as 0 rather than
		// flickering between 0 and 1 on a value that lands exactly on them.
		if ((!bOpenBottom && X <= Low) || (!bOpenTop && X >= High))
		{
			return 0.0f;
		}
		return 1.0f;
	}

	float Gain = 1.0f;

	if (!bOpenBottom && X < Low + Edge)
	{
		Gain = FMath::Min(Gain, (X - Low) / Edge);
	}

	if (!bOpenTop && X > High - Edge)
	{
		Gain = FMath::Min(Gain, (High - X) / Edge);
	}

	return FMath::Clamp(Gain, 0.0f, 1.0f);
}

void UPulseScoreStatics::EvaluateLayerGains(const UPulseScoreAsset* Score, float Intensity, FName SectionId,
	TArray<FPulseScoreLayerGain>& OutGains, int32 MaxConcurrentLayers, float DefaultEdgeWidth,
	float AudibleGainThreshold)
{
	OutGains.Reset();

	if (!Score)
	{
		return;
	}

	OutGains.Reserve(Score->Layers.Num());

	const int32 SectionIndex = Score->FindSectionIndex(SectionId);
	const FPulseScoreSection* Section = SectionIndex != INDEX_NONE ? &Score->Sections[SectionIndex] : nullptr;

	for (const FPulseScoreLayer& Layer : Score->Layers)
	{
		// An empty layer list on a section is not "no layers", it is "no opinion". A section that wanted
		// silence would have to say so with an intensity, and none of them ever do.
		const bool bAllowed = !Section || Section->LayerIds.Num() == 0 || Section->LayerIds.Contains(Layer.Id);

		const float WindowGain = bAllowed ? EvaluateLayerGain(Layer, Intensity, DefaultEdgeWidth) : 0.0f;
		const float Gain = WindowGain * FMath::Max(Layer.VolumeScale, 0.0f);

		OutGains.Emplace(Layer.Id, Gain, Layer.bDuckedByStinger, bAllowed);
	}

	if (MaxConcurrentLayers <= 0)
	{
		return;
	}

	TArray<int32> Audible;
	Audible.Reserve(OutGains.Num());
	for (int32 Index = 0; Index < OutGains.Num(); ++Index)
	{
		if (OutGains[Index].Gain > AudibleGainThreshold)
		{
			Audible.Add(Index);
		}
	}

	if (Audible.Num() <= MaxConcurrentLayers)
	{
		return;
	}

	// Priority first, gain second. Gain alone would let the cap mute the stem the composer marked as
	// essential in favour of one that happened to be a hair louder on this frame - and the swap would then
	// flip back a frame later, which is audible in a way that a missing layer is not.
	const TArray<FPulseScoreLayer>& Layers = Score->Layers;
	Audible.Sort([&Layers, &OutGains](const int32& A, const int32& B)
	{
		const int32 PriorityA = Layers.IsValidIndex(A) ? Layers[A].Priority : 0;
		const int32 PriorityB = Layers.IsValidIndex(B) ? Layers[B].Priority : 0;
		if (PriorityA != PriorityB)
		{
			return PriorityA > PriorityB;
		}
		return OutGains[A].Gain > OutGains[B].Gain;
	});

	for (int32 Rank = MaxConcurrentLayers; Rank < Audible.Num(); ++Rank)
	{
		OutGains[Audible[Rank]].Gain = 0.0f;
	}
}

void UPulseScoreStatics::ApplyStingerDuck(TArray<FPulseScoreLayerGain>& Gains, float DuckAmount)
{
	const float Amount = FMath::Clamp(DuckAmount, 0.0f, 1.0f);
	if (Amount <= 0.0f)
	{
		return;
	}

	// Multiplicative and stateless. Nothing is stored, so letting the duck go restores the gains exactly -
	// there is no "original volume" to get out of step with an intensity that moved while the stinger was
	// sounding.
	const float Scale = 1.0f - Amount;
	for (FPulseScoreLayerGain& Gain : Gains)
	{
		if (Gain.bDuckedByStinger)
		{
			Gain.Gain *= Scale;
		}
	}
}

float UPulseScoreStatics::GetSecondsPerBar(float Tempo, int32 BeatsPerBar)
{
	if (Tempo <= 0.0f || BeatsPerBar < 1)
	{
		return 0.0f;
	}

	return (60.0f / Tempo) * BeatsPerBar;
}

float UPulseScoreStatics::TimeUntilBoundary(EPulseScoreQuantization When, float ClockSeconds, float Tempo,
	int32 BeatsPerBar, float SectionStartSeconds, int32 SectionLengthBars)
{
	using namespace PulseScorePrivate;

	const EPulseScoreQuantization Resolved = ResolveQuantization(When);

	if (Resolved == EPulseScoreQuantization::Immediate || Tempo <= 0.0f || BeatsPerBar < 1)
	{
		return 0.0f;
	}

	const float SecondsPerBeat = 60.0f / Tempo;
	const float SecondsPerBar = SecondsPerBeat * BeatsPerBar;

	switch (Resolved)
	{
	case EPulseScoreQuantization::Beat:
		return TimeToNextMultiple(ClockSeconds, SecondsPerBeat);

	case EPulseScoreQuantization::Bar:
		return TimeToNextMultiple(ClockSeconds, SecondsPerBar);

	case EPulseScoreQuantization::TwoBars:
		return TimeToNextMultiple(ClockSeconds, SecondsPerBar * 2.0f);

	case EPulseScoreQuantization::SectionEnd:
	{
		if (SectionLengthBars <= 0)
		{
			// A section with no declared length has no end to wait for. Falling back to the next bar is the
			// least surprising of the three options; never firing would be the worst.
			return TimeToNextMultiple(ClockSeconds, SecondsPerBar);
		}

		const float Period = SecondsPerBar * SectionLengthBars;
		const float Elapsed = ClockSeconds - SectionStartSeconds;

		if (Elapsed <= 0.0f)
		{
			return FMath::Max(-Elapsed, 0.0f);
		}

		// The phrase repeats. A section is a length, not a countdown that expires - a change asked for
		// during the third pass through an eight bar phrase should wait for the end of that pass, not
		// discover that the only section end went by ninety seconds ago and fire instantly.
		const float Remainder = FMath::Fmod(Elapsed, Period);
		return Remainder <= BoundaryTolerance ? 0.0f : Period - Remainder;
	}

	default:
		return 0.0f;
	}
}

bool UPulseScoreStatics::IsSectionTransitionAllowed(const UPulseScoreAsset* Score, FName FromSection,
	FName ToSection, FString& OutReason)
{
	OutReason.Reset();

	if (!Score)
	{
		OutReason = TEXT("There is no score loaded.");
		return false;
	}

	if (ToSection.IsNone())
	{
		OutReason = TEXT("The target section is None.");
		return false;
	}

	if (Score->FindSectionIndex(ToSection) == INDEX_NONE)
	{
		OutReason = FString::Printf(TEXT("'%s' is not a section in this score."), *ToSection.ToString());
		return false;
	}

	// Entering from nothing is always allowed. The allow-list describes where a section may *go*, and a
	// score that is not playing yet is not in one.
	if (FromSection.IsNone() || FromSection == ToSection)
	{
		return true;
	}

	const int32 FromIndex = Score->FindSectionIndex(FromSection);
	if (FromIndex == INDEX_NONE)
	{
		// The section we are supposedly in is not in this score - which happens when a score is swapped
		// under a running conductor. Refusing here would strand the music in a section that does not exist.
		return true;
	}

	const FPulseScoreSection& From = Score->Sections[FromIndex];

	// An empty list is unconstrained, not a dead end. Reading it the other way would turn "the author has
	// not drawn the graph yet" into "the music can never change again", which is a silent failure in a
	// plugin whose whole job is changing the music.
	if (From.AllowedNextSections.Num() == 0)
	{
		return true;
	}

	if (From.AllowedNextSections.Contains(ToSection))
	{
		return true;
	}

	OutReason = FString::Printf(
		TEXT("Section '%s' does not allow a transition to '%s'. Allowed: %s"),
		*FromSection.ToString(), *ToSection.ToString(),
		*FString::JoinBy(From.AllowedNextSections, TEXT(", "), [](const FName& Name)
		{
			return Name.ToString();
		}));

	return false;
}

float UPulseScoreStatics::GetLayerGain(const FPulseScoreState& State, FName LayerId)
{
	for (const FPulseScoreLayerGain& Gain : State.LayerGains)
	{
		if (Gain.LayerId == LayerId)
		{
			return Gain.Gain;
		}
	}

	return 0.0f;
}

EPulseScoreQuantization UPulseScoreStatics::ResolveQuantization(EPulseScoreQuantization When)
{
	if (When != EPulseScoreQuantization::Default)
	{
		return When;
	}

	const EPulseScoreQuantization Configured = UPulseScoreSettings::Get().DefaultQuantization;

	// A project setting of Default would resolve to itself forever. Bar is the documented meaning of the
	// setting anyway, so this is the loop guard and the sane value in one line.
	return Configured == EPulseScoreQuantization::Default ? EPulseScoreQuantization::Bar : Configured;
}
