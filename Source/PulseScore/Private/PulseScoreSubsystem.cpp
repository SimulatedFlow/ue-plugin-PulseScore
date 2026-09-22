// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "PulseScoreSubsystem.h"

#include "AudioParameter.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Components/AudioComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "GlobalRenderResources.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/StringBuilder.h"
#include "PulseScoreAsset.h"
#include "PulseScoreLog.h"
#include "PulseScoreSettings.h"
#include "PulseScoreStatics.h"
#include "Quartz/AudioMixerClockHandle.h"
#include "Quartz/QuartzSubsystem.h"
#include "SceneTypes.h"
#include "Sound/SoundBase.h"

namespace PulseScorePrivate
{
	static constexpr float LineHeight = 15.0f;
	static constexpr float BoxPadding = 8.0f;

	/** Lines the overlay draws before the per-layer rows. */
	static constexpr int32 FixedOverlayLines = 7;

	/**
	 * How close to a due time counts as having reached it. Matches the tolerance in the maths library.
	 *
	 * NAMED DIFFERENTLY FROM THE ONE IN PulseScoreStatics.cpp ON PURPOSE. Both files open
	 * `namespace PulseScorePrivate`, and in a UNITY build they are concatenated into a single
	 * translation unit - at which point two `static constexpr float SubsystemBoundaryTolerance` in the same
	 * namespace are a redefinition and the module does not compile. `static` gives internal linkage,
	 * so it is legal across separate translation units and the collision only appears once they are
	 * merged. Adaptive Unity hid it here (both files were excluded from the unity blob because they
	 * were being edited); in a buyer's project, where these files are not recently modified, they
	 * would be merged and the plugin would fail to build. Found on 11.09.2026 by building with
	 * -DisableAdaptiveUnity.
	 */
	static constexpr float SubsystemBoundaryTolerance = 1.0e-4f;

	static const FLinearColor PanelBackground(0.0f, 0.0f, 0.0f, 0.62f);
	static const FLinearColor HeadingColor(0.55f, 0.85f, 1.0f, 1.0f);
	static const FLinearColor BodyColor(0.88f, 0.88f, 0.88f, 1.0f);
	static const FLinearColor DimColor(0.55f, 0.55f, 0.55f, 1.0f);
	static const FLinearColor GoodColor(0.45f, 0.95f, 0.55f, 1.0f);
	static const FLinearColor WarnColor(1.0f, 0.80f, 0.35f, 1.0f);
	static const FLinearColor BarColor(0.35f, 0.70f, 1.0f, 1.0f);

	static void DrawFilledRect(UCanvas* Canvas, const FVector2D& Position, const FVector2D& Size,
		const FLinearColor& Color)
	{
		FCanvasTileItem Tile(Position, GWhiteTexture, Size, Color);
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
	}

	/** A twenty-character meter, so a gain reads as a level and not as a number that has to be compared. */
	static void AppendMeter(TStringBuilder<192>& Builder, float Alpha)
	{
		constexpr int32 Cells = 20;
		const int32 Filled = FMath::Clamp(FMath::RoundToInt(Alpha * Cells), 0, Cells);

		Builder.AppendChar(TEXT('['));
		for (int32 Index = 0; Index < Cells; ++Index)
		{
			Builder.AppendChar(Index < Filled ? TEXT('=') : TEXT('.'));
		}
		Builder.AppendChar(TEXT(']'));
	}

	/**
	 * Turn one of our boundaries into a Quartz one.
	 *
	 * TwoBars is two bars rather than a quantisation of its own, and SectionEnd is however many bars are
	 * left of the phrase - Quartz has no notion of a section, so the number of bars is worked out here from
	 * the wait the maths library already computed and handed to Quartz as a multiplier. Rounding it is safe
	 * because that wait was itself derived from whole bars.
	 */
	static FQuartzQuantizationBoundary MakeBoundary(EPulseScoreQuantization When, float WaitSeconds,
		float SecondsPerBar)
	{
		switch (UPulseScoreStatics::ResolveQuantization(When))
		{
		case EPulseScoreQuantization::Immediate:
			return FQuartzQuantizationBoundary(EQuartzCommandQuantization::None, 1.0f);

		case EPulseScoreQuantization::Beat:
			return FQuartzQuantizationBoundary(EQuartzCommandQuantization::Beat, 1.0f);

		case EPulseScoreQuantization::Bar:
			return FQuartzQuantizationBoundary(EQuartzCommandQuantization::Bar, 1.0f);

		case EPulseScoreQuantization::TwoBars:
			return FQuartzQuantizationBoundary(EQuartzCommandQuantization::Bar, 2.0f);

		case EPulseScoreQuantization::SectionEnd:
		{
			const float Bars = SecondsPerBar > 0.0f ? WaitSeconds / SecondsPerBar : 1.0f;
			const float Multiplier = FMath::Max(1.0f, FMath::RoundToFloat(Bars));
			return FQuartzQuantizationBoundary(EQuartzCommandQuantization::Bar, Multiplier);
		}

		default:
			return FQuartzQuantizationBoundary(EQuartzCommandQuantization::Bar, 1.0f);
		}
	}

	static UPulseScoreSubsystem* GetSubsystem(UWorld* World)
	{
		return World ? World->GetSubsystem<UPulseScoreSubsystem>() : nullptr;
	}

	/** Console-argument spelling of a boundary. Anything unrecognised is the project default. */
	static EPulseScoreQuantization ParseQuantization(const FString& Argument)
	{
		if (Argument.Equals(TEXT("immediate"), ESearchCase::IgnoreCase)
			|| Argument.Equals(TEXT("now"), ESearchCase::IgnoreCase))
		{
			return EPulseScoreQuantization::Immediate;
		}
		if (Argument.Equals(TEXT("beat"), ESearchCase::IgnoreCase))
		{
			return EPulseScoreQuantization::Beat;
		}
		if (Argument.Equals(TEXT("bar"), ESearchCase::IgnoreCase))
		{
			return EPulseScoreQuantization::Bar;
		}
		if (Argument.Equals(TEXT("twobars"), ESearchCase::IgnoreCase))
		{
			return EPulseScoreQuantization::TwoBars;
		}
		if (Argument.Equals(TEXT("sectionend"), ESearchCase::IgnoreCase))
		{
			return EPulseScoreQuantization::SectionEnd;
		}

		return EPulseScoreQuantization::Default;
	}
}

//~ Lifetime ---------------------------------------------------------------------------------------------

void UPulseScoreSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// The Quartz subsystem has to exist before we ask it for a clock. Declaring the dependency here rather
	// than hoping for creation order is the difference between a plugin that works and one that works on
	// the machine it was written on.
	Collection.InitializeDependency<UQuartzSubsystem>();

	const UPulseScoreSettings& Settings = UPulseScoreSettings::Get();
	ClockName = Settings.ClockName;
	MasterVolume = Settings.MasterVolume;
	DefaultEdgeWidth = Settings.DefaultEdgeWidth;
	DefaultFadeBars = Settings.DefaultFadeBars;
	AudibleGainThreshold = Settings.AudibleGainThreshold;
	MaxConcurrentLayers = Settings.MaxConcurrentLayers;
	bShowOverlay = Settings.bShowOverlayByDefault;
	bAutoDrawOverlayOnAnyHUD = Settings.bAutoDrawOverlayOnAnyHUD;

	RebindHudDelegate();
}

void UPulseScoreSubsystem::Deinitialize()
{
	if (HudPostRenderHandle.IsValid())
	{
		AHUD::OnHUDPostRender.Remove(HudPostRenderHandle);
		HudPostRenderHandle.Reset();
	}

	StopImmediately();

	Super::Deinitialize();
}

bool UPulseScoreSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Game and PIE. Starting a score in an editor viewport while somebody is dressing a level is the kind
	// of help nobody asks for twice.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UPulseScoreSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPulseScoreSubsystem, STATGROUP_Tickables);
}

UPulseScoreSubsystem* UPulseScoreSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject,
		EGetWorldErrorMode::ReturnNull) : nullptr;

	return World ? World->GetSubsystem<UPulseScoreSubsystem>() : nullptr;
}

//~ Tick -------------------------------------------------------------------------------------------------

void UPulseScoreSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bPlaying || !CurrentScore)
	{
		return;
	}

	ReadTransport(DeltaTime);

	// The intensity target lands here rather than on a Quartz command. A gain change is a fade, and a fade
	// cannot be heard to be a frame late; spending a scheduled audio-render-thread command on it would buy
	// accuracy nobody can perceive and leave a callback that can outlive the score that asked for it.
	if (PendingIntensityDueSeconds >= 0.0f && ClockSeconds + PulseScorePrivate::SubsystemBoundaryTolerance >= PendingIntensityDueSeconds)
	{
		CurrentIntensity = PendingIntensity;
		PendingIntensityDueSeconds = -1.0f;
	}

	// Backstops for the two things that *are* scheduled through Quartz. When there is an audio device the
	// notification arrives first and clears the pending state, and these never run. When there is not - a
	// server, a -nosound session, an automation run - the music still has to change sections, so the clock
	// time we predicted for the boundary is honoured on the tick that passes it. The prediction is the same
	// pure function the tests cover, which is why it can be trusted to stand in for the real thing.
	if (!PendingSection.IsNone() && ClockSeconds + PulseScorePrivate::SubsystemBoundaryTolerance >= PendingSectionDueSeconds)
	{
		const FName Section = PendingSection;
		PendingSection = NAME_None;
		ApplySectionChange(Section);
	}

	if (bStopPending && ClockSeconds + PulseScorePrivate::SubsystemBoundaryTolerance >= PendingStopDueSeconds)
	{
		StopImmediately();
		return;
	}

	if (PendingStingerDueSeconds >= 0.0f && ClockSeconds + PulseScorePrivate::SubsystemBoundaryTolerance >= PendingStingerDueSeconds)
	{
		BeginStingerDuck();
	}

	Ducker.Update(DeltaTime);
	UpdateMix(DeltaTime);
	PruneStingerVoices();

	//~ Publish the state ------------------------------------------------------------------------------

	State.bPlaying = bPlaying;
	State.ScoreName = CurrentScore->GetFName();
	State.CurrentSection = CurrentSection;
	State.PendingSection = PendingSection;
	State.SecondsUntilPendingSection = PendingSection.IsNone()
		? 0.0f
		: FMath::Max(PendingSectionDueSeconds - ClockSeconds, 0.0f);
	State.Intensity = CurrentIntensity;
	State.PendingIntensity = PendingIntensityDueSeconds >= 0.0f ? PendingIntensity : CurrentIntensity;
	State.Tempo = CurrentScore->Tempo;
	State.BeatsPerBar = CurrentScore->BeatsPerBar;
	State.SecondsElapsed = ClockSeconds;
	State.BarsIntoSection = CurrentScore->GetSecondsPerBar() > 0.0f
		? FMath::FloorToInt((ClockSeconds - SectionStartSeconds) / CurrentScore->GetSecondsPerBar())
		: 0;
	State.DuckAmount = Ducker.GetAmount();

	State.LayerGains.Reset(Voices.Num());
	State.AudibleLayers = 0;
	for (const FVoice& Voice : Voices)
	{
		// What the state reports is what the fader is actually at, not what the mixer asked for. The
		// difference between the two is the fade, and the fade is the thing worth watching.
		const float Gain = Voice.Fader.GetVolume();
		State.LayerGains.Emplace(Voice.LayerId, Gain, Voice.bDuckedByStinger, true);

		if (Gain > AudibleGainThreshold)
		{
			++State.AudibleLayers;
		}
	}
}

//~ Transport --------------------------------------------------------------------------------------------

bool UPulseScoreSubsystem::PlayScore(UPulseScoreAsset* Score, EPulseScoreQuantization When)
{
	if (!Score)
	{
		UE_LOG(LogPulseScore, Warning, TEXT("PlayScore: no score given."));
		return false;
	}

	TArray<FString> Problems;
	const bool bPlayable = Score->Validate(Problems);

	for (const FString& Problem : Problems)
	{
		UE_LOG(LogPulseScore, Warning, TEXT("Score '%s': %s"), *Score->GetName(), *Problem);
	}

	if (!bPlayable)
	{
		UE_LOG(LogPulseScore, Error, TEXT("PlayScore: score '%s' cannot play. See the warnings above."),
			*Score->GetName());
		return false;
	}

	StopImmediately();

	CurrentScore = Score;

	if (!StartClock(*Score))
	{
		CurrentScore = nullptr;
		return false;
	}

	CurrentSection = Score->GetEffectiveStartSection();
	SectionStartSeconds = 0.0f;
	ClockSeconds = 0.0f;
	LastBar = 0;
	LastBeat = 0;
	bMetronomeAlive = false;

	const int32 SectionIndex = Score->FindSectionIndex(CurrentSection);
	if (SectionIndex != INDEX_NONE && Score->Sections[SectionIndex].EntryIntensity >= 0.0f)
	{
		CurrentIntensity = FMath::Clamp(Score->Sections[SectionIndex].EntryIntensity, 0.0f, 1.0f);
	}

	PendingIntensity = CurrentIntensity;
	PendingIntensityDueSeconds = -1.0f;

	BuildVoices(When);

	if (Voices.Num() == 0)
	{
		UE_LOG(LogPulseScore, Error,
			TEXT("PlayScore: score '%s' produced no voices. There is probably no audio device."),
			*Score->GetName());
		DestroyClock();
		CurrentScore = nullptr;
		return false;
	}

	bPlaying = true;

	UE_LOG(LogPulseScore, Log, TEXT("Playing '%s': %d layers, %.1f BPM, %d/4, section '%s'."),
		*Score->GetName(), Voices.Num(), Score->Tempo, Score->BeatsPerBar, *CurrentSection.ToString());

	OnSectionChanged.Broadcast(NAME_None, CurrentSection);
	return true;
}

void UPulseScoreSubsystem::Stop(EPulseScoreQuantization When)
{
	if (!bPlaying)
	{
		return;
	}

	const float Wait = UPulseScoreStatics::TimeUntilBoundary(When, ClockSeconds,
		CurrentScore ? CurrentScore->Tempo : 0.0f,
		CurrentScore ? CurrentScore->BeatsPerBar : 4,
		SectionStartSeconds, GetCurrentSectionLengthBars());

	if (Wait <= 0.0f)
	{
		StopImmediately();
		return;
	}

	// A second Stop while one is already queued is not a second stop. It is the same request arriving
	// again, and queueing a second notification would leave one of them to fire into a subsystem that has
	// already torn everything down.
	if (bStopPending)
	{
		return;
	}

	bStopPending = true;
	PendingStopDueSeconds = ClockSeconds + Wait;

	// Start the layers fading now so the stop lands on silence rather than cutting a bar short. The fade is
	// the wait itself, so the last thing heard is the boundary, not a chop before it.
	for (FVoice& Voice : Voices)
	{
		Voice.TargetGain = 0.0f;
		Voice.Fader.StartFade(0.0f, Wait, Audio::EFaderCurve::Linear);
	}
	bFadingToStop = true;

	ScheduleNotify(When, GET_FUNCTION_NAME_CHECKED(UPulseScoreSubsystem, HandleStopBoundary));
}

void UPulseScoreSubsystem::StopImmediately()
{
	TearDownVoices();
	DestroyClock();

	bPlaying = false;
	bStopPending = false;
	bFadingToStop = false;
	CurrentScore = nullptr;
	CurrentSection = NAME_None;
	PendingSection = NAME_None;
	PendingSectionDueSeconds = 0.0f;
	PendingStopDueSeconds = 0.0f;
	PendingStingerDueSeconds = -1.0f;
	PendingStingerId = NAME_None;
	PendingIntensityDueSeconds = -1.0f;
	SectionStartSeconds = 0.0f;
	ClockSeconds = 0.0f;
	LastBar = 0;
	LastBeat = 0;
	bMetronomeAlive = false;
	Ducker.Reset();

	State = FPulseScoreState();
}

//~ Direction --------------------------------------------------------------------------------------------

void UPulseScoreSubsystem::SetIntensity(float Intensity, EPulseScoreQuantization When)
{
	if (Intensity < 0.0f || Intensity > 1.0f)
	{
		if (!bWarnedIntensityRange)
		{
			bWarnedIntensityRange = true;
			UE_LOG(LogPulseScore, Warning,
				TEXT("SetIntensity was given %.3f. Intensity is 0..1 and the value has been clamped. This is logged once."),
				Intensity);
		}
	}

	const float Clamped = FMath::Clamp(Intensity, 0.0f, 1.0f);
	PendingIntensity = Clamped;

	if (!bPlaying || !CurrentScore)
	{
		// Remember it anyway. A game that sets the intensity before the music starts has said something
		// true, and throwing it away would mean the score comes in at whatever the last level left behind.
		CurrentIntensity = Clamped;
		PendingIntensityDueSeconds = -1.0f;
		return;
	}

	const float Wait = UPulseScoreStatics::TimeUntilBoundary(When, ClockSeconds, CurrentScore->Tempo,
		CurrentScore->BeatsPerBar, SectionStartSeconds, GetCurrentSectionLengthBars());

	if (Wait <= 0.0f)
	{
		CurrentIntensity = Clamped;
		PendingIntensityDueSeconds = -1.0f;
		return;
	}

	PendingIntensityDueSeconds = ClockSeconds + Wait;
}

bool UPulseScoreSubsystem::EnterSection(FName SectionId, EPulseScoreQuantization When)
{
	if (!bPlaying || !CurrentScore)
	{
		UE_LOG(LogPulseScore, Warning, TEXT("EnterSection('%s'): nothing is playing."), *SectionId.ToString());
		return false;
	}

	if (CurrentScore->FindSectionIndex(SectionId) == INDEX_NONE)
	{
		// A section that does not exist is a typo, not a design decision, so this one is refused whatever
		// the strictness setting says.
		UE_LOG(LogPulseScore, Warning, TEXT("EnterSection: '%s' is not a section in score '%s'."),
			*SectionId.ToString(), *CurrentScore->GetName());
		return false;
	}

	FString Reason;
	if (!UPulseScoreStatics::IsSectionTransitionAllowed(CurrentScore, CurrentSection, SectionId, Reason))
	{
		if (UPulseScoreSettings::Get().bStrictSectionTransitions)
		{
			UE_LOG(LogPulseScore, Warning, TEXT("EnterSection refused: %s"), *Reason);
			return false;
		}

		UE_LOG(LogPulseScore, Warning,
			TEXT("EnterSection taken anyway (strict transitions are off): %s"), *Reason);
	}

	const float Wait = UPulseScoreStatics::TimeUntilBoundary(When, ClockSeconds, CurrentScore->Tempo,
		CurrentScore->BeatsPerBar, SectionStartSeconds, GetCurrentSectionLengthBars());

	if (Wait <= 0.0f)
	{
		PendingSection = NAME_None;
		ApplySectionChange(SectionId);
		return true;
	}

	// A newer request replaces an older one rather than queueing behind it. The newest call is always the
	// better description of what the game wants; finishing the stale one first would put the score into a
	// section the game has already moved on from, and then move it again a bar later.
	PendingSection = SectionId;
	PendingSectionDueSeconds = ClockSeconds + Wait;

	ScheduleNotify(When, GET_FUNCTION_NAME_CHECKED(UPulseScoreSubsystem, HandleSectionBoundary));
	return true;
}

bool UPulseScoreSubsystem::TriggerStinger(FName StingerId, EPulseScoreQuantization When)
{
	if (!bPlaying || !CurrentScore)
	{
		UE_LOG(LogPulseScore, Warning, TEXT("TriggerStinger('%s'): nothing is playing."), *StingerId.ToString());
		return false;
	}

	USoundBase* Sound = CurrentScore->FindStinger(StingerId);
	if (!Sound)
	{
		UE_LOG(LogPulseScore, Warning, TEXT("TriggerStinger: score '%s' has no stinger called '%s'."),
			*CurrentScore->GetName(), *StingerId.ToString());
		return false;
	}

	UWorld* World = GetWorld();
	UAudioComponent* Component = UGameplayStatics::CreateSound2D(World, Sound, 1.0f, 1.0f, 0.0f, nullptr,
		false, /*bAutoDestroy*/ true);

	if (!Component)
	{
		UE_LOG(LogPulseScore, Warning, TEXT("TriggerStinger: could not create a voice for '%s'."),
			*StingerId.ToString());
		return false;
	}

	Component->bAllowSpatialization = false;
	Component->bIsUISound = UPulseScoreSettings::Get().bMusicIgnoresPause;

	const float SecondsPerBar = CurrentScore->GetSecondsPerBar();
	const float Wait = UPulseScoreStatics::TimeUntilBoundary(When, ClockSeconds, CurrentScore->Tempo,
		CurrentScore->BeatsPerBar, SectionStartSeconds, GetCurrentSectionLengthBars());

	FQuartzQuantizationBoundary Boundary = PulseScorePrivate::MakeBoundary(When, Wait, SecondsPerBar);

	FOnQuartzCommandEventBP StartedDelegate;
	StartedDelegate.BindUFunction(this,
		GET_FUNCTION_NAME_CHECKED(UPulseScoreSubsystem, HandleStingerStarted));

	PendingStingerId = StingerId;

	// The duck starts when the sound starts, not when this was called. A stinger queued three beats out
	// must not flatten the mix for three beats first, waiting for a hit that has not landed.
	PendingStingerDueSeconds = ClockSeconds + Wait;

	UQuartzClockHandle* Handle = ClockHandle;
	Component->PlayQuantized(this, Handle, Boundary, StartedDelegate, 0.0f, 0.0f, 1.0f,
		EAudioFaderCurve::Linear);

	StingerComponents.Add(Component);
	return true;
}

//~ Layer parameters -------------------------------------------------------------------------------------

bool UPulseScoreSubsystem::SetLayerParameter(FName LayerId, FName ParameterName, float Value)
{
	for (const FVoice& Voice : Voices)
	{
		if (Voice.LayerId != LayerId)
		{
			continue;
		}

		UAudioComponent* Component = Voice.Component.Get();
		if (!Component)
		{
			return false;
		}

		TArray<FAudioParameter> Parameters;
		Parameters.Emplace(FAudioParameter(ParameterName, Value));
		Component->SetParameters(MoveTemp(Parameters));
		return true;
	}

	return false;
}

int32 UPulseScoreSubsystem::SetLayerParameterOnAll(FName ParameterName, float Value)
{
	int32 Applied = 0;

	for (const FVoice& Voice : Voices)
	{
		if (UAudioComponent* Component = Voice.Component.Get())
		{
			TArray<FAudioParameter> Parameters;
			Parameters.Emplace(FAudioParameter(ParameterName, Value));
			Component->SetParameters(MoveTemp(Parameters));
			++Applied;
		}
	}

	return Applied;
}

//~ Voices -----------------------------------------------------------------------------------------------

void UPulseScoreSubsystem::BuildVoices(EPulseScoreQuantization When)
{
	TearDownVoices();

	if (!CurrentScore)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const UPulseScoreSettings& Settings = UPulseScoreSettings::Get();
	const float SecondsPerBar = CurrentScore->GetSecondsPerBar();
	const float Wait = UPulseScoreStatics::TimeUntilBoundary(When, 0.0f, CurrentScore->Tempo,
		CurrentScore->BeatsPerBar, 0.0f, 0);

	// One boundary for every layer, built once. This is what keeps the stems in phase: they are not started
	// "at about the same time", they are handed to the audio render thread with the same quantised start,
	// so they begin on the same sample.
	FQuartzQuantizationBoundary Boundary = PulseScorePrivate::MakeBoundary(When, Wait, SecondsPerBar);
	const FOnQuartzCommandEventBP EmptyDelegate;

	Voices.Reserve(CurrentScore->Layers.Num());
	LayerComponents.Reserve(CurrentScore->Layers.Num());

	for (int32 LayerIndex = 0; LayerIndex < CurrentScore->Layers.Num(); ++LayerIndex)
	{
		const FPulseScoreLayer& Layer = CurrentScore->Layers[LayerIndex];
		if (!Layer.Sound)
		{
			continue;
		}

		UAudioComponent* Component = UGameplayStatics::CreateSound2D(World, Layer.Sound, 1.0f, 1.0f, 0.0f,
			nullptr, false, /*bAutoDestroy*/ false);

		if (!Component)
		{
			UE_LOG(LogPulseScore, Warning, TEXT("Could not create a voice for layer '%s'."),
				*Layer.Id.ToString());
			continue;
		}

		Component->bAllowSpatialization = false;
		Component->bIsUISound = Settings.bMusicIgnoresPause;

		// Silent at the start and faded up by the mixer on the first tick. Starting at the computed gain
		// would be one frame faster and would make every score begin with a click on whichever layers are
		// already in their window.
		Component->SetVolumeMultiplier(0.0f);

		UQuartzClockHandle* Handle = ClockHandle;
		Component->PlayQuantized(this, Handle, Boundary, EmptyDelegate, 0.0f, 0.0f, 1.0f,
			EAudioFaderCurve::Linear);

		FVoice Voice;
		Voice.LayerId = Layer.Id;
		Voice.LayerIndex = LayerIndex;
		Voice.Component = Component;
		Voice.bDuckedByStinger = Layer.bDuckedByStinger;
		Voice.Fader.SetVolume(0.0f);

		Voices.Add(MoveTemp(Voice));
		LayerComponents.Add(Component);
	}
}

void UPulseScoreSubsystem::TearDownVoices()
{
	for (TObjectPtr<UAudioComponent>& Component : LayerComponents)
	{
		if (Component)
		{
			Component->Stop();
			Component->DestroyComponent();
		}
	}
	LayerComponents.Reset();
	Voices.Reset();

	for (TObjectPtr<UAudioComponent>& Component : StingerComponents)
	{
		if (Component)
		{
			Component->Stop();
		}
	}
	StingerComponents.Reset();
}

void UPulseScoreSubsystem::UpdateMix(float DeltaTime)
{
	if (!CurrentScore)
	{
		return;
	}

	const float SecondsPerBar = CurrentScore->GetSecondsPerBar();

	// Window gains first, then the duck. The duck is a live envelope and not a property of the score, which
	// is exactly why it is applied here rather than folded into the evaluation - the evaluation has to stay
	// a pure function of the asset and one float.
	UPulseScoreStatics::EvaluateLayerGains(CurrentScore, CurrentIntensity, CurrentSection, GainScratch,
		MaxConcurrentLayers, DefaultEdgeWidth, AudibleGainThreshold);
	UPulseScoreStatics::ApplyStingerDuck(GainScratch, Ducker.GetAmount());

	for (FVoice& Voice : Voices)
	{
		UAudioComponent* Component = Voice.Component.Get();

		if (!bFadingToStop && GainScratch.IsValidIndex(Voice.LayerIndex))
		{
			const float Target = GainScratch[Voice.LayerIndex].Gain * MasterVolume;

			const float LayerFadeBars = CurrentScore->Layers.IsValidIndex(Voice.LayerIndex)
				&& CurrentScore->Layers[Voice.LayerIndex].FadeBars >= 0.0f
				? CurrentScore->Layers[Voice.LayerIndex].FadeBars
				: DefaultFadeBars;

			// Only restart the fade when the target has actually moved. Restarting it every frame with an
			// unchanged target would reset the elapsed time every frame and the fader would crawl instead of
			// arriving.
			if (!FMath::IsNearlyEqual(Voice.Fader.GetTargetVolume(), Target, 0.001f))
			{
				Voice.Fader.StartFade(Target, LayerFadeBars * SecondsPerBar, Audio::EFaderCurve::Linear);
			}

			Voice.TargetGain = Target;
		}

		Voice.Fader.Update(DeltaTime);

		if (Component)
		{
			Component->SetVolumeMultiplier(Voice.Fader.GetVolume());
		}
	}
}

void UPulseScoreSubsystem::PruneStingerVoices()
{
	StingerComponents.RemoveAll([](const TObjectPtr<UAudioComponent>& Component)
	{
		return !Component || !Component->IsPlaying();
	});
}

void UPulseScoreSubsystem::BeginStingerDuck()
{
	PendingStingerDueSeconds = -1.0f;

	const UPulseScoreSettings& Settings = UPulseScoreSettings::Get();
	Ducker.Trigger(Settings.StingerDuckAmount, Settings.DuckAttackSeconds, Settings.DuckHoldSeconds,
		Settings.DuckReleaseSeconds);

	OnStingerStarted.Broadcast(PendingStingerId);
}

//~ Clock ------------------------------------------------------------------------------------------------

bool UPulseScoreSubsystem::StartClock(const UPulseScoreAsset& Score)
{
	UWorld* World = GetWorld();
	UQuartzSubsystem* Quartz = UQuartzSubsystem::Get(World);

	if (!Quartz)
	{
		UE_LOG(LogPulseScore, Error, TEXT("There is no Quartz subsystem in this world."));
		return false;
	}

	FQuartzClockSettings ClockSettings;
	ClockSettings.TimeSignature.NumBeats = FMath::Max(Score.BeatsPerBar, 1);
	ClockSettings.TimeSignature.BeatType = EQuartzTimeSignatureQuantization::QuarterNote;

	// Override the settings if a clock of this name already exists. Two scores sharing one clock at two
	// different tempos is not a state worth supporting quietly, and the second score is the one the game
	// just asked for.
	ClockHandle = Quartz->CreateNewClock(this, ClockName, ClockSettings,
		/*bOverrideSettingsIfClockExists*/ true, /*bUseAudioEngineClockManager*/ true);

	if (!ClockHandle)
	{
		UE_LOG(LogPulseScore, Error, TEXT("Quartz would not create the clock '%s'."), *ClockName.ToString());
		return false;
	}

	UQuartzClockHandle* Handle = ClockHandle;
	const FQuartzQuantizationBoundary Immediate(EQuartzCommandQuantization::None, 1.0f);
	const FOnQuartzCommandEventBP EmptyDelegate;

	ClockHandle->SetBeatsPerMinute(this, Immediate, EmptyDelegate, Handle, Score.Tempo);

	FOnQuartzMetronomeEventBP MetronomeDelegate;
	MetronomeDelegate.BindUFunction(this,
		GET_FUNCTION_NAME_CHECKED(UPulseScoreSubsystem, HandleMetronomeEvent));

	ClockHandle->SubscribeToQuantizationEvent(this, EQuartzCommandQuantization::Beat, MetronomeDelegate, Handle);
	ClockHandle->SubscribeToQuantizationEvent(this, EQuartzCommandQuantization::Bar, MetronomeDelegate, Handle);

	ClockHandle->StartClock(this, Handle);

	if (UPulseScoreSettings::Get().bMusicIgnoresPause)
	{
		Quartz->SetQuartzSubsystemTickableWhenPaused(true);
	}

	return true;
}

void UPulseScoreSubsystem::DestroyClock()
{
	if (!ClockHandle)
	{
		return;
	}

	UQuartzClockHandle* Handle = ClockHandle;

	ClockHandle->UnsubscribeFromAllTimeDivisions(this, Handle);
	ClockHandle->StopClock(this, /*CancelPendingEvents*/ true, Handle);

	if (UQuartzSubsystem* Quartz = UQuartzSubsystem::Get(GetWorld()))
	{
		Quartz->DeleteClockByName(this, ClockName);
	}

	ClockHandle = nullptr;
}

void UPulseScoreSubsystem::ReadTransport(float DeltaTime)
{
	if (!CurrentScore)
	{
		return;
	}

	const float SecondsPerBeat = CurrentScore->GetSecondsPerBeat();
	const int32 BeatsPerBar = FMath::Max(CurrentScore->BeatsPerBar, 1);

	// Advance our own clock first and let Quartz correct it. The transport timestamp is the authority when
	// there is an audio device to produce one; when there is not - a dedicated server, an automation run, a
	// session started with -nosound - the score still has to know where it is, and a clock that stands still
	// would mean no section ever changes again.
	ClockSeconds += DeltaTime;

	if (ClockHandle)
	{
		const FQuartzTransportTimeStamp TimeStamp = ClockHandle->GetCurrentTimestamp(this);
		if (TimeStamp.Seconds > 0.0f)
		{
			ClockSeconds = TimeStamp.Seconds;
			State.Bar = TimeStamp.Bars;
			State.Beat = TimeStamp.Beat;
			State.BeatFraction = TimeStamp.BeatFraction;

			if (!bMetronomeAlive)
			{
				BroadcastTransport(TimeStamp.Bars, TimeStamp.Beat);
			}
			return;
		}
	}

	if (SecondsPerBeat <= 0.0f)
	{
		return;
	}

	const double TotalBeats = ClockSeconds / SecondsPerBeat;
	const int32 WholeBeats = FMath::FloorToInt(TotalBeats);

	State.Bar = (WholeBeats / BeatsPerBar) + 1;
	State.Beat = (WholeBeats % BeatsPerBar) + 1;
	State.BeatFraction = static_cast<float>(TotalBeats - WholeBeats);

	if (!bMetronomeAlive)
	{
		BroadcastTransport(State.Bar, State.Beat);
	}
}

void UPulseScoreSubsystem::BroadcastTransport(int32 Bar, int32 Beat)
{
	if (Bar == LastBar && Beat == LastBeat)
	{
		return;
	}

	const bool bNewBar = Bar != LastBar;

	LastBar = Bar;
	LastBeat = Beat;

	if (bNewBar)
	{
		OnBar.Broadcast(Bar);
	}

	OnBeat.Broadcast(Bar, Beat);
}

void UPulseScoreSubsystem::ScheduleNotify(EPulseScoreQuantization When, FName HandlerFunctionName)
{
	if (!ClockHandle || !CurrentScore)
	{
		return;
	}

	const float Wait = UPulseScoreStatics::TimeUntilBoundary(When, ClockSeconds, CurrentScore->Tempo,
		CurrentScore->BeatsPerBar, SectionStartSeconds, GetCurrentSectionLengthBars());

	FQuartzQuantizationBoundary Boundary = PulseScorePrivate::MakeBoundary(When, Wait,
		CurrentScore->GetSecondsPerBar());

	FOnQuartzCommandEventBP Delegate;
	Delegate.BindUFunction(this, HandlerFunctionName);

	ClockHandle->NotifyOnQuantizationBoundary(this, Boundary, Delegate, 0.0f);
}

void UPulseScoreSubsystem::ApplySectionChange(FName NewSection)
{
	if (!CurrentScore)
	{
		return;
	}

	const FName OldSection = CurrentSection;
	CurrentSection = NewSection;
	SectionStartSeconds = ClockSeconds;

	const int32 SectionIndex = CurrentScore->FindSectionIndex(NewSection);
	if (SectionIndex != INDEX_NONE)
	{
		const FPulseScoreSection& Section = CurrentScore->Sections[SectionIndex];

		if (Section.EntryIntensity >= 0.0f)
		{
			CurrentIntensity = FMath::Clamp(Section.EntryIntensity, 0.0f, 1.0f);
			PendingIntensity = CurrentIntensity;
			PendingIntensityDueSeconds = -1.0f;
		}

		if (!Section.TransitionStinger.IsNone())
		{
			// Immediate, because we are already standing on the boundary this change was scheduled for.
			// Quantising it again would push the transition hit a whole bar past the transition.
			TriggerStinger(Section.TransitionStinger, EPulseScoreQuantization::Immediate);
		}
	}

	UE_LOG(LogPulseScore, Verbose, TEXT("Section '%s' -> '%s' at %.3f s (bar %d)."),
		*OldSection.ToString(), *NewSection.ToString(), ClockSeconds, State.Bar);

	OnSectionChanged.Broadcast(OldSection, NewSection);
}

int32 UPulseScoreSubsystem::GetCurrentSectionLengthBars() const
{
	if (!CurrentScore)
	{
		return 0;
	}

	const int32 SectionIndex = CurrentScore->FindSectionIndex(CurrentSection);
	return SectionIndex != INDEX_NONE ? CurrentScore->Sections[SectionIndex].LengthInBars : 0;
}

//~ Quartz callbacks -------------------------------------------------------------------------------------

void UPulseScoreSubsystem::HandleMetronomeEvent(FName InClockName, EQuartzCommandQuantization QuantizationType,
	int32 NumBars, int32 Beat, float BeatFraction)
{
	bMetronomeAlive = true;
	BroadcastTransport(NumBars, Beat);
}

void UPulseScoreSubsystem::HandleSectionBoundary(EQuartzCommandDelegateSubType EventType, FName Name)
{
	if (EventType != EQuartzCommandDelegateSubType::CommandOnStarted || PendingSection.IsNone())
	{
		return;
	}

	// A notification that belongs to a request we have already replaced arrives early. Every scheduled
	// change records the clock time it is due at, so an early one is recognisably early and is dropped
	// rather than applying the newer change a bar before it was asked for.
	if (ClockSeconds + PulseScorePrivate::SubsystemBoundaryTolerance < PendingSectionDueSeconds)
	{
		return;
	}

	const FName Section = PendingSection;
	PendingSection = NAME_None;
	ApplySectionChange(Section);
}

void UPulseScoreSubsystem::HandleStopBoundary(EQuartzCommandDelegateSubType EventType, FName Name)
{
	if (EventType != EQuartzCommandDelegateSubType::CommandOnStarted || !bStopPending)
	{
		return;
	}

	StopImmediately();
}

void UPulseScoreSubsystem::HandleStingerStarted(EQuartzCommandDelegateSubType EventType, FName Name)
{
	if (EventType != EQuartzCommandDelegateSubType::CommandOnStarted)
	{
		return;
	}

	BeginStingerDuck();
}

//~ Overlay ----------------------------------------------------------------------------------------------

void UPulseScoreSubsystem::SetShowOverlay(bool bShow)
{
	bShowOverlay = bShow;
}

int32 UPulseScoreSubsystem::GetOverlayLineCount() const
{
	return PulseScorePrivate::FixedOverlayLines + State.LayerGains.Num();
}

void UPulseScoreSubsystem::DrawOverlay(UCanvas* Canvas, const FVector2D& Origin, float Width) const
{
	using namespace PulseScorePrivate;

	if (!Canvas)
	{
		return;
	}

	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!Font)
	{
		return;
	}

	LastOverlayDrawFrame = GFrameCounter;

	const float BoxHeight = GetOverlayLineCount() * LineHeight + BoxPadding * 2.0f;
	DrawFilledRect(Canvas,
		FVector2D(Origin.X - BoxPadding, Origin.Y - BoxPadding),
		FVector2D(Width, BoxHeight),
		PanelBackground);

	float LineY = static_cast<float>(Origin.Y);
	auto DrawLine = [&](FStringView Line, const FLinearColor& Color)
	{
		FCanvasTextStringViewItem Item(FVector2D(Origin.X, LineY), Line, Font, Color);
		Canvas->DrawItem(Item);
		LineY += LineHeight;
	};

	TStringBuilder<192> Line;

	Line.Reset();
	Line.Append(TEXT("PulseScore"));
	DrawLine(Line.ToView(), HeadingColor);

	if (!State.bPlaying)
	{
		Line.Reset();
		Line.Append(TEXT("stopped"));
		DrawLine(Line.ToView(), DimColor);
		return;
	}

	Line.Reset();
	Line.Appendf(TEXT("Score         %s   %.1f BPM   %d/4"),
		*State.ScoreName.ToString(), State.Tempo, State.BeatsPerBar);
	DrawLine(Line.ToView(), BodyColor);

	// Bar and beat with the beat drawn as a row of pips, because a number that changes twice a second is
	// unreadable on a screenshot and a row of dots is not.
	Line.Reset();
	Line.Appendf(TEXT("Bar %-5d beat %d/%d  "), State.Bar, State.Beat, State.BeatsPerBar);
	for (int32 Index = 1; Index <= State.BeatsPerBar; ++Index)
	{
		Line.AppendChar(Index == State.Beat ? TEXT('O') : TEXT('.'));
		Line.AppendChar(TEXT(' '));
	}
	Line.Appendf(TEXT("  %6.2f s"), State.SecondsElapsed);
	DrawLine(Line.ToView(), GoodColor);

	Line.Reset();
	if (State.PendingSection.IsNone())
	{
		Line.Appendf(TEXT("Section       %s   (bar %d of section)"),
			*State.CurrentSection.ToString(), State.BarsIntoSection + 1);
		DrawLine(Line.ToView(), BodyColor);
	}
	else
	{
		Line.Appendf(TEXT("Section       %s -> %s in %.2f s"),
			*State.CurrentSection.ToString(), *State.PendingSection.ToString(),
			State.SecondsUntilPendingSection);
		DrawLine(Line.ToView(), WarnColor);
	}

	Line.Reset();
	Line.Append(TEXT("Intensity     "));
	AppendMeter(Line, State.Intensity);
	Line.Appendf(TEXT(" %.2f"), State.Intensity);
	if (!FMath::IsNearlyEqual(State.Intensity, State.PendingIntensity, 0.001f))
	{
		Line.Appendf(TEXT(" -> %.2f"), State.PendingIntensity);
	}
	DrawLine(Line.ToView(), BarColor);

	Line.Reset();
	Line.Append(TEXT("Duck          "));
	AppendMeter(Line, State.DuckAmount);
	Line.Appendf(TEXT(" %.2f"), State.DuckAmount);
	DrawLine(Line.ToView(), State.DuckAmount > 0.0f ? WarnColor : DimColor);

	Line.Reset();
	Line.Appendf(TEXT("Layers        %d audible of %d   cap %d"),
		State.AudibleLayers, State.LayerGains.Num(), MaxConcurrentLayers);
	DrawLine(Line.ToView(), BodyColor);

	// The per-layer meters are the whole reason this box exists. An adaptive music plugin with nothing on
	// screen is one you have to take on trust, and a still frame of it proves nothing at all.
	for (const FPulseScoreLayerGain& Gain : State.LayerGains)
	{
		Line.Reset();
		Line.Appendf(TEXT("  %-11s "), *Gain.LayerId.ToString());
		AppendMeter(Line, Gain.Gain);
		Line.Appendf(TEXT(" %.2f"), Gain.Gain);
		DrawLine(Line.ToView(), Gain.Gain > AudibleGainThreshold ? BodyColor : DimColor);
	}
}

void UPulseScoreSubsystem::RebindHudDelegate()
{
	if (bAutoDrawOverlayOnAnyHUD && !HudPostRenderHandle.IsValid())
	{
		HudPostRenderHandle = AHUD::OnHUDPostRender.AddUObject(this, &UPulseScoreSubsystem::OnAnyHUDPostRender);
	}
	else if (!bAutoDrawOverlayOnAnyHUD && HudPostRenderHandle.IsValid())
	{
		AHUD::OnHUDPostRender.Remove(HudPostRenderHandle);
		HudPostRenderHandle.Reset();
	}
}

void UPulseScoreSubsystem::OnAnyHUDPostRender(AHUD* HUD, UCanvas* Canvas)
{
	if (!bShowOverlay || !HUD || !Canvas)
	{
		return;
	}

	if (HUD->GetWorld() != GetWorld())
	{
		return;
	}

	// One box per frame however many HUDs there are. Split screen would otherwise draw it twice, in the
	// same place, at double the opacity.
	if (LastOverlayDrawFrame == GFrameCounter)
	{
		return;
	}

	const UPulseScoreSettings& Settings = UPulseScoreSettings::Get();
	DrawOverlay(Canvas, Settings.OverlayOrigin, Settings.OverlayWidth);
}

//~ Log --------------------------------------------------------------------------------------------------

void UPulseScoreSubsystem::LogState() const
{
	if (!State.bPlaying)
	{
		UE_LOG(LogPulseScore, Display, TEXT("PulseScore: stopped."));
		return;
	}

	UE_LOG(LogPulseScore, Display, TEXT("PulseScore:"));
	UE_LOG(LogPulseScore, Display, TEXT("  Score        %s   %.1f BPM   %d beats/bar"),
		*State.ScoreName.ToString(), State.Tempo, State.BeatsPerBar);
	UE_LOG(LogPulseScore, Display, TEXT("  Transport    bar %d beat %d (%.2f)   %.3f s"),
		State.Bar, State.Beat, State.BeatFraction, State.SecondsElapsed);

	if (State.PendingSection.IsNone())
	{
		UE_LOG(LogPulseScore, Display, TEXT("  Section      %s   bar %d of section"),
			*State.CurrentSection.ToString(), State.BarsIntoSection + 1);
	}
	else
	{
		UE_LOG(LogPulseScore, Display, TEXT("  Section      %s -> %s in %.3f s"),
			*State.CurrentSection.ToString(), *State.PendingSection.ToString(),
			State.SecondsUntilPendingSection);
	}

	UE_LOG(LogPulseScore, Display, TEXT("  Intensity    %.3f (target %.3f)   duck %.3f"),
		State.Intensity, State.PendingIntensity, State.DuckAmount);
	UE_LOG(LogPulseScore, Display, TEXT("  Layers       %d audible of %d (cap %d)"),
		State.AudibleLayers, State.LayerGains.Num(), MaxConcurrentLayers);

	for (const FPulseScoreLayerGain& Gain : State.LayerGains)
	{
		UE_LOG(LogPulseScore, Display, TEXT("    %-16s %.3f"), *Gain.LayerId.ToString(), Gain.Gain);
	}
}

//~ Console commands -------------------------------------------------------------------------------------

namespace PulseScorePrivate
{
	static FAutoConsoleCommandWithWorldAndArgs CmdShow(
		TEXT("PulseScore.Show"),
		TEXT("PulseScore.Show [0|1] - draw the bar, section, intensity and per-layer gain box."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UPulseScoreSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogPulseScore, Warning, TEXT("PulseScore.Show: no PulseScore subsystem in this world."));
				return;
			}

			const bool bShow = Args.Num() > 0 ? (FCString::Atoi(*Args[0]) != 0) : !Subsystem->IsShowingOverlay();
			Subsystem->SetShowOverlay(bShow);
			UE_LOG(LogPulseScore, Display, TEXT("PulseScore.Show: %s"), bShow ? TEXT("on") : TEXT("off"));
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdStats(
		TEXT("PulseScore.Stats"),
		TEXT("PulseScore.Stats - print the transport, the section and every layer gain to the log."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			const UPulseScoreSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogPulseScore, Warning, TEXT("PulseScore.Stats: no PulseScore subsystem in this world."));
				return;
			}
			Subsystem->LogState();
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdIntensity(
		TEXT("PulseScore.Intensity"),
		TEXT("PulseScore.Intensity <0..1> [immediate|beat|bar|twobars|sectionend] - set the intensity."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UPulseScoreSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogPulseScore, Warning, TEXT("PulseScore.Intensity: no PulseScore subsystem in this world."));
				return;
			}

			if (Args.Num() == 0)
			{
				UE_LOG(LogPulseScore, Display, TEXT("PulseScore.Intensity: %.3f"), Subsystem->GetIntensity());
				return;
			}

			const EPulseScoreQuantization When = Args.Num() > 1
				? ParseQuantization(Args[1])
				: EPulseScoreQuantization::Default;

			Subsystem->SetIntensity(FCString::Atof(*Args[0]), When);
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdSection(
		TEXT("PulseScore.Section"),
		TEXT("PulseScore.Section <Name> [immediate|beat|bar|twobars|sectionend] - queue a section change."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UPulseScoreSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogPulseScore, Warning, TEXT("PulseScore.Section: no PulseScore subsystem in this world."));
				return;
			}

			if (Args.Num() == 0)
			{
				UE_LOG(LogPulseScore, Display, TEXT("PulseScore.Section: %s"),
					*Subsystem->GetCurrentSection().ToString());
				return;
			}

			const EPulseScoreQuantization When = Args.Num() > 1
				? ParseQuantization(Args[1])
				: EPulseScoreQuantization::Default;

			if (!Subsystem->EnterSection(FName(*Args[0]), When))
			{
				UE_LOG(LogPulseScore, Warning, TEXT("PulseScore.Section: refused. See the warning above."));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdStinger(
		TEXT("PulseScore.Stinger"),
		TEXT("PulseScore.Stinger <Name> [immediate|beat|bar|twobars|sectionend] - fire a one-shot."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UPulseScoreSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem || Args.Num() == 0)
			{
				UE_LOG(LogPulseScore, Warning, TEXT("PulseScore.Stinger <Name>"));
				return;
			}

			const EPulseScoreQuantization When = Args.Num() > 1
				? ParseQuantization(Args[1])
				: EPulseScoreQuantization::Default;

			Subsystem->TriggerStinger(FName(*Args[0]), When);
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdStop(
		TEXT("PulseScore.Stop"),
		TEXT("PulseScore.Stop [immediate|beat|bar|twobars|sectionend] - stop the score on a boundary."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UPulseScoreSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogPulseScore, Warning, TEXT("PulseScore.Stop: no PulseScore subsystem in this world."));
				return;
			}

			Subsystem->Stop(Args.Num() > 0 ? ParseQuantization(Args[0]) : EPulseScoreQuantization::Default);
		}));
}
