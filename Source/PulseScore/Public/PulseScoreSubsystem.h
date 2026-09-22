// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Sound/QuartzQuantizationUtilities.h"
#include "DSP/VolumeFader.h"
#include "PulseScoreTypes.h"
#include "PulseScoreSubsystem.generated.h"

class AHUD;
class UAudioComponent;
class UCanvas;
class UPulseScoreAsset;
class UQuartzClockHandle;

/** A bar line went past. Bars count from 1. The demo HUD's beat lamp is this and nothing else. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPulseScoreBarEvent, int32, Bar);

/** A beat went past, inside the bar. Fires on the bar line too. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPulseScoreBeatEvent, int32, Bar, int32, Beat);

/** A queued section change actually landed. Fires on the boundary, not when it was requested. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPulseScoreSectionChanged, FName, OldSection, FName, NewSection);

/** A stinger started sounding, which is also the moment the duck starts. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPulseScoreStingerStarted, FName, StingerId);

/**
 * The conductor.
 *
 * The game reports a state. This decides what that state sounds like and when the change is allowed to
 * happen. Three ideas hold the whole thing up, and each of them is a decision that could have gone the
 * other way:
 *
 * **Nothing switches mid-bar.** Every call carries an EPulseScoreQuantization and the change is scheduled
 * against a Quartz clock, so it lands on a beat or a bar line rather than wherever the calling frame fell.
 * A SetTimer gets you to within a frame; Quartz gets you to within a sample, because it schedules on the
 * audio render thread and not on the game thread.
 *
 * **Intensity mixes, it does not switch.** Every layer plays for as long as the score does. Out of its
 * window it plays at zero. Stopping it instead would save a voice and cost the one thing layered music is
 * for: when the drums come back they are still exactly on the beat, because they never left it. This is the
 * single most important line in the plugin and the one most likely to be "optimised" away by somebody who
 * has not heard what a restarted stem sounds like against a running one.
 *
 * **A refusal is louder than a silent failure.** A section change the score does not permit is rejected and
 * logged with its reason. A missing stinger says so. A score with a layer that has no sound says so at the
 * moment it is asked to play.
 *
 * Not a decision, but worth writing down: only section changes, stops and stingers go through Quartz.
 * Intensity is applied on the tick that passes its boundary, because a gain change is a fade and a fade is
 * not something a sample of latency can be heard on. Spending a Quartz command on it would buy accuracy
 * nobody can hear and add a scheduled callback that can outlive the score that asked for it.
 *
 * Game and PIE worlds only.
 */
UCLASS(meta = (DisplayName = "Pulse Score Subsystem"))
class PULSESCORE_API UPulseScoreSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	//~ UWorldSubsystem interface
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	//~ FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** The conductor for whatever world the context object belongs to, or null. */
	static UPulseScoreSubsystem* Get(const UObject* WorldContextObject);

	//~ Transport ---------------------------------------------------------------------------------------

	/**
	 * Load a score, create its clock, and start every layer on the same boundary.
	 *
	 * Replaces whatever was playing. False when the score is null or unplayable - no tempo, no layers, or
	 * every layer missing its sound. Problems that are survivable are logged and the score plays anyway,
	 * because half a score is better than silence and the log says which half.
	 */
	UFUNCTION(BlueprintCallable, Category = "PulseScore")
	bool PlayScore(UPulseScoreAsset* Score, EPulseScoreQuantization When = EPulseScoreQuantization::Immediate);

	/** Fade out, stop the voices and delete the clock, on the given boundary. */
	UFUNCTION(BlueprintCallable, Category = "PulseScore")
	void Stop(EPulseScoreQuantization When = EPulseScoreQuantization::Default);

	/** Stop everything on this frame, with no fade and no boundary. What Deinitialize does. */
	UFUNCTION(BlueprintCallable, Category = "PulseScore")
	void StopImmediately();

	/** A score is loaded and its clock is running. */
	UFUNCTION(BlueprintPure, Category = "PulseScore")
	bool IsPlaying() const { return bPlaying; }

	/** The score that is loaded, or null. */
	UFUNCTION(BlueprintPure, Category = "PulseScore")
	UPulseScoreAsset* GetScore() const { return CurrentScore; }

	//~ Direction ---------------------------------------------------------------------------------------

	/**
	 * How hot the situation is, 0..1. Clamped, not wrapped - 1.4 is 1, and the log says so once.
	 *
	 * The value takes effect on the given boundary; the gains then move toward it over each layer's fade.
	 * Immediate is honest here: it changes the target now, and the faders still take their fade time, so
	 * "immediate" means "start moving now", not "jump".
	 */
	UFUNCTION(BlueprintCallable, Category = "PulseScore")
	void SetIntensity(float Intensity, EPulseScoreQuantization When = EPulseScoreQuantization::Default);

	/** The intensity the mix is currently built from. */
	UFUNCTION(BlueprintPure, Category = "PulseScore")
	float GetIntensity() const { return CurrentIntensity; }

	/**
	 * Queue a section change for the given boundary.
	 *
	 * False - and nothing queued - when the section does not exist or the transition is not permitted.
	 * A second call replaces the first: the newest request is always the better description of what the
	 * game wants, and finishing a stale one first would put the score in a section the game has already
	 * moved on from.
	 */
	UFUNCTION(BlueprintCallable, Category = "PulseScore")
	bool EnterSection(FName SectionId, EPulseScoreQuantization When = EPulseScoreQuantization::Default);

	/** The section that is sounding. */
	UFUNCTION(BlueprintPure, Category = "PulseScore")
	FName GetCurrentSection() const { return CurrentSection; }

	/** The section queued for the next boundary, or None. */
	UFUNCTION(BlueprintPure, Category = "PulseScore")
	FName GetPendingSection() const { return PendingSection; }

	/**
	 * Fire a named one-shot on the given boundary.
	 *
	 * The duck starts when the sound actually starts, not when this was called - Quartz tells us, so a
	 * stinger queued three beats out does not flatten the mix for three beats first.
	 */
	UFUNCTION(BlueprintCallable, Category = "PulseScore")
	bool TriggerStinger(FName StingerId, EPulseScoreQuantization When = EPulseScoreQuantization::Default);

	//~ Layer parameters --------------------------------------------------------------------------------

	/**
	 * Push a named float to one layer's live voice - a MetaSound input, usually.
	 *
	 * PulseScore mixes stems; it does not pretend to be the only thing that may have an opinion about them.
	 * A score whose "Combat" drum stem takes a Fill Density input should be able to drive it from the same
	 * place that drives the intensity, without the caller having to find the audio component behind a layer
	 * name. False when nothing is playing or the layer is not in the score.
	 */
	UFUNCTION(BlueprintCallable, Category = "PulseScore")
	bool SetLayerParameter(FName LayerId, FName ParameterName, float Value);

	/** The same parameter on every layer voice. Returns how many voices took it. */
	UFUNCTION(BlueprintCallable, Category = "PulseScore")
	int32 SetLayerParameterOnAll(FName ParameterName, float Value);

	//~ State -------------------------------------------------------------------------------------------

	/** Everything the conductor knows, rebuilt once per tick. */
	UFUNCTION(BlueprintPure, Category = "PulseScore")
	const FPulseScoreState& GetState() const { return State; }

	/** The Quartz clock PulseScore is running on, so a project can hang its own quantised events off it. */
	UFUNCTION(BlueprintPure, Category = "PulseScore")
	UQuartzClockHandle* GetClockHandle() const { return ClockHandle; }

	//~ Events ------------------------------------------------------------------------------------------

	/** A bar line went past. */
	UPROPERTY(BlueprintAssignable, Category = "PulseScore")
	FPulseScoreBarEvent OnBar;

	/** A beat went past. */
	UPROPERTY(BlueprintAssignable, Category = "PulseScore")
	FPulseScoreBeatEvent OnBeat;

	/** A queued section change landed. */
	UPROPERTY(BlueprintAssignable, Category = "PulseScore")
	FPulseScoreSectionChanged OnSectionChanged;

	/** A stinger started sounding. */
	UPROPERTY(BlueprintAssignable, Category = "PulseScore")
	FPulseScoreStingerStarted OnStingerStarted;

	//~ Overlay -----------------------------------------------------------------------------------------

	/** Show or hide the counters overlay. */
	UFUNCTION(BlueprintCallable, Category = "PulseScore|Debug")
	void SetShowOverlay(bool bShow);

	/** The overlay is on. */
	UFUNCTION(BlueprintPure, Category = "PulseScore|Debug")
	bool IsShowingOverlay() const { return bShowOverlay; }

	/** Draw the counters panel at this corner and width. Called by the HUD hook. */
	void DrawOverlay(UCanvas* Canvas, const FVector2D& Origin, float Width) const;

	/** Print the same numbers to the log. What PulseScore.Stats calls. */
	void LogState() const;

	//~ Quartz callbacks --------------------------------------------------------------------------------
	//
	// UFUNCTIONs because Quartz's delegates are dynamic. They are public for the same reason - a dynamic
	// delegate is bound by name through the reflection system, so hiding them would buy nothing.

	/** Metronome subscription: one per beat, one per bar. Drives the bar and beat events. */
	UFUNCTION()
	void HandleMetronomeEvent(FName ClockName, EQuartzCommandQuantization QuantizationType, int32 NumBars,
		int32 Beat, float BeatFraction);

	/** The boundary a queued section change was scheduled for has arrived. */
	UFUNCTION()
	void HandleSectionBoundary(EQuartzCommandDelegateSubType EventType, FName Name);

	/** The boundary a queued stop was scheduled for has arrived. */
	UFUNCTION()
	void HandleStopBoundary(EQuartzCommandDelegateSubType EventType, FName Name);

	/** A stinger voice actually started. The duck begins here and nowhere else. */
	UFUNCTION()
	void HandleStingerStarted(EQuartzCommandDelegateSubType EventType, FName Name);

private:
	//~ Voices ------------------------------------------------------------------------------------------

	/**
	 * One layer's playing voice and its fader.
	 *
	 * Not a USTRUCT, because Audio::FVolumeFader is not one either and wrapping the engine's fader in a
	 * reflected shell to satisfy the garbage collector would be the tail wagging the dog. The strong
	 * references live in LayerComponents, which is reflected; this holds a weak pointer to the same object
	 * and both are only ever built and torn down in one place.
	 */
	struct FVoice
	{
		FName LayerId;
		int32 LayerIndex = INDEX_NONE;
		TWeakObjectPtr<UAudioComponent> Component;
		Audio::FVolumeFader Fader;
		float TargetGain = 0.0f;
		bool bDuckedByStinger = true;
	};

	TArray<FVoice> Voices;

	/** The layer voices, held strongly so nothing collects them out from under the mix. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> LayerComponents;

	/** Stinger voices in flight. Auto-destroying, so this is pruned rather than emptied. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> StingerComponents;

	/** Create one voice per layer and start them all on the same boundary. */
	void BuildVoices(EPulseScoreQuantization When);

	/** Stop and drop every voice. */
	void TearDownVoices();

	/** Recompute the target gains and move each fader one tick toward them. */
	void UpdateMix(float DeltaTime);

	/** Drop stinger voices that have finished. */
	void PruneStingerVoices();

	/** Start the duck envelope and raise the stinger event. Both the Quartz path and the tick path land here. */
	void BeginStingerDuck();

	//~ Clock -------------------------------------------------------------------------------------------

	/** Create the Quartz clock for a score's tempo and time signature, subscribe to it and start it. */
	bool StartClock(const UPulseScoreAsset& Score);

	/** Unsubscribe, stop and delete the clock. */
	void DestroyClock();

	/** Read bar, beat and elapsed time off the clock into the state. */
	void ReadTransport(float DeltaTime);

	/** Raise the bar and beat events, once each, when the transport has actually moved on. */
	void BroadcastTransport(int32 Bar, int32 Beat);

	/** Schedule a Quartz notification for a boundary, bound to one of the handlers above. */
	void ScheduleNotify(EPulseScoreQuantization When, FName HandlerFunctionName);

	/** Apply a queued section change now: swap the section, fire the transition stinger, raise the event. */
	void ApplySectionChange(FName NewSection);

	/** Declared length of the section that is sounding, in bars. Zero when it has none. */
	int32 GetCurrentSectionLengthBars() const;

	//~ Overlay -----------------------------------------------------------------------------------------

	void RebindHudDelegate();
	void OnAnyHUDPostRender(AHUD* HUD, UCanvas* Canvas);

	/** How many lines the overlay draws, so the panel behind it can be sized before anything is written. */
	int32 GetOverlayLineCount() const;

	//~ State -------------------------------------------------------------------------------------------

	UPROPERTY(Transient)
	TObjectPtr<UPulseScoreAsset> CurrentScore;

	UPROPERTY(Transient)
	TObjectPtr<UQuartzClockHandle> ClockHandle;

	/** Rebuilt every tick. Public through GetState so a HUD can read it without asking twelve questions. */
	FPulseScoreState State;

	bool bPlaying = false;

	FName CurrentSection;
	FName PendingSection;

	/** Clock seconds the queued section change is due at. Guards against a stale Quartz notification. */
	float PendingSectionDueSeconds = 0.0f;

	/** Clock seconds the current section started at. What SectionEnd quantisation measures from. */
	float SectionStartSeconds = 0.0f;

	float CurrentIntensity = 0.0f;
	float PendingIntensity = 0.0f;

	/**
	 * Clock seconds the pending intensity is due at, or negative when nothing is pending.
	 *
	 * Applied in Tick rather than through Quartz. See the note on the class: a gain target is a fade, and a
	 * frame of latency on a fade is not a thing anybody can hear.
	 */
	float PendingIntensityDueSeconds = -1.0f;

	/** True between a Stop request and its boundary, so a second Stop does not queue a second notify. */
	bool bStopPending = false;

	/** Clock seconds the queued stop is due at. */
	float PendingStopDueSeconds = 0.0f;

	/**
	 * True while the layers are fading out into a queued stop.
	 *
	 * The mixer stops writing targets for the duration, because the intensity does not stop moving just
	 * because the music is ending - and a fade-out that a sine wave keeps pulling back up is not a fade-out.
	 */
	bool bFadingToStop = false;

	/** Clock seconds, read from the Quartz transport each tick. */
	float ClockSeconds = 0.0f;

	int32 LastBar = 0;
	int32 LastBeat = 0;

	/**
	 * The Quartz metronome has been heard from at least once.
	 *
	 * Until it has, the bar and beat events are raised from the transport read instead. A dedicated server
	 * or a -nosound session has no metronome and still has a score that has to change sections.
	 */
	bool bMetronomeAlive = false;

	FPulseScoreDucker Ducker;

	/** Name of the stinger that is next to start, so the started callback can report it. */
	FName PendingStingerId;

	/** Clock seconds the queued stinger is due at, or negative when none is queued. */
	float PendingStingerDueSeconds = -1.0f;

	/** An out-of-range intensity has already been complained about. Once is a service; every frame is noise. */
	bool bWarnedIntensityRange = false;

	//~ Cached settings ---------------------------------------------------------------------------------

	FName ClockName;
	float MasterVolume = 1.0f;
	float DefaultEdgeWidth = 0.15f;
	float DefaultFadeBars = 1.0f;
	float AudibleGainThreshold = 0.01f;
	int32 MaxConcurrentLayers = 8;

	//~ Overlay state -----------------------------------------------------------------------------------

	bool bShowOverlay = false;
	bool bAutoDrawOverlayOnAnyHUD = true;
	FDelegateHandle HudPostRenderHandle;

	/** Frame the overlay was last drawn on, so the HUD hook and a manual call cannot stack. */
	mutable uint64 LastOverlayDrawFrame = 0;

	/** Scratch buffer for the per-tick gain evaluation, so the mixer allocates nothing per frame. */
	TArray<FPulseScoreLayerGain> GainScratch;
};
