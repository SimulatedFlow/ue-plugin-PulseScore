// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PulseScoreTypes.h"
#include "PulseScoreComponent.generated.h"

class UCurveFloat;
class UPulseScoreAsset;
class UPulseScoreSubsystem;

/**
 * Where a component's intensity comes from.
 *
 * Three sources, and each of them is a thing a designer actually wants to do without opening a Blueprint
 * graph. Anything more elaborate than these is a graph, and SetIntensity is one node.
 */
UENUM(BlueprintType)
enum class EPulseScoreIntensityDriver : uint8
{
	/** Nobody drives it. The component ticks not at all and the game calls SetIntensity itself. */
	Manual			UMETA(DisplayName = "Manual"),

	/** A float curve read against seconds since the score started. The demo director is this. */
	Curve			UMETA(DisplayName = "Curve Over Time"),

	/** Distance to a target actor, mapped through a near and a far radius. Proximity as intensity. */
	DistanceToActor	UMETA(DisplayName = "Distance To Actor"),
};

/**
 * The convenient way in: drop it on an actor, point it at a score, and the actor conducts.
 *
 * The plugin does not need this component. Everything it does is one call into UPulseScoreStatics, and a
 * project with a music manager of its own should use those directly. What the component buys is the two
 * cases that would otherwise be a timeline and a distance check re-written in every project: an intensity
 * curve over time, and an intensity that follows how close something is.
 *
 * There is exactly one conductor per world, so two of these on two actors are two things shouting at the
 * same subsystem. That is not an error - a level might well want the player's component to drive intensity
 * while a trigger volume's component changes sections - but the last writer of a given value wins, and the
 * overlay shows what actually happened.
 */
UCLASS(ClassGroup = (Audio), meta = (BlueprintSpawnableComponent, DisplayName = "Pulse Score"))
class PULSESCORE_API UPulseScoreComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPulseScoreComponent();

	//~ UActorComponent interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	//~ Setup -------------------------------------------------------------------------------------------

	/** The score this component plays. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PulseScore")
	TObjectPtr<UPulseScoreAsset> Score;

	/** Start the score on BeginPlay. Off when something else decides when the music starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PulseScore")
	bool bPlayOnBeginPlay = true;

	/** Section entered on start. None uses the score's own start section. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PulseScore")
	FName StartSection;

	/**
	 * Stop the score when this component's actor is destroyed or the level ends.
	 *
	 * On, because a component that started the music owns it. Off is right for the case where this actor
	 * was only ever the thing that pressed play - a trigger at the level entrance, say - and the music is
	 * meant to outlive it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PulseScore")
	bool bStopOnEndPlay = true;

	/** Boundary used by this component's own calls when they do not name one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PulseScore")
	EPulseScoreQuantization Quantization = EPulseScoreQuantization::Default;

	//~ Intensity ---------------------------------------------------------------------------------------

	/** Where the intensity comes from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PulseScore|Intensity")
	EPulseScoreIntensityDriver IntensityDriver = EPulseScoreIntensityDriver::Manual;

	/**
	 * Intensity against seconds, for the Curve driver.
	 *
	 * Read at the elapsed time since the component started driving, clamped to 0..1. A curve that runs off
	 * its last key holds that value; with bLoopCurve on, the time wraps at LoopSeconds instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PulseScore|Intensity",
		meta = (EditCondition = "IntensityDriver == EPulseScoreIntensityDriver::Curve"))
	TObjectPtr<UCurveFloat> IntensityCurve;

	/** Wrap the curve time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PulseScore|Intensity",
		meta = (EditCondition = "IntensityDriver == EPulseScoreIntensityDriver::Curve"))
	bool bLoopCurve = true;

	/** Seconds the curve time wraps at. Zero uses the curve's own last key time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PulseScore|Intensity",
		meta = (ClampMin = "0.0", UIMax = "120.0", ForceUnits = "s",
			EditCondition = "IntensityDriver == EPulseScoreIntensityDriver::Curve && bLoopCurve"))
	float LoopSeconds = 0.0f;

	/** The actor whose distance drives the intensity. Null falls back to the local player pawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PulseScore|Intensity",
		meta = (EditCondition = "IntensityDriver == EPulseScoreIntensityDriver::DistanceToActor"))
	TObjectPtr<AActor> DistanceTarget;

	/** At or inside this distance the intensity is 1, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PulseScore|Intensity",
		meta = (ClampMin = "0.0", UIMax = "20000.0", ForceUnits = "cm",
			EditCondition = "IntensityDriver == EPulseScoreIntensityDriver::DistanceToActor"))
	float NearDistance = 400.0f;

	/** At or outside this distance the intensity is 0, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PulseScore|Intensity",
		meta = (ClampMin = "0.0", UIMax = "50000.0", ForceUnits = "cm",
			EditCondition = "IntensityDriver == EPulseScoreIntensityDriver::DistanceToActor"))
	float FarDistance = 3000.0f;

	/**
	 * Seconds the driven intensity takes to follow a step in its source, as a smoothing constant.
	 *
	 * Separate from the layer fades, and needed because a distance driver on a sprinting player produces a
	 * value that jitters with every corner. Zero passes the raw value straight through.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PulseScore|Intensity",
		meta = (ClampMin = "0.0", UIMax = "10.0", ForceUnits = "s"))
	float IntensitySmoothingSeconds = 0.5f;

	//~ API ---------------------------------------------------------------------------------------------

	/** Start this component's score. False when there is no score or no conductor. */
	UFUNCTION(BlueprintCallable, Category = "PulseScore")
	bool PlayScore();

	/** Stop the score on this component's boundary. */
	UFUNCTION(BlueprintCallable, Category = "PulseScore")
	void StopScore();

	/** Set the intensity directly. Only meaningful with the Manual driver; the others overwrite it. */
	UFUNCTION(BlueprintCallable, Category = "PulseScore")
	void SetIntensity(float Intensity);

	/** Move to a section on this component's boundary. */
	UFUNCTION(BlueprintCallable, Category = "PulseScore")
	bool EnterSection(FName SectionId);

	/** Fire a stinger on this component's boundary. */
	UFUNCTION(BlueprintCallable, Category = "PulseScore")
	bool TriggerStinger(FName StingerId);

	/**
	 * This component's score is the one the conductor is playing.
	 *
	 * Named for what it answers rather than for the shortest phrasing, because the shortest phrasing here
	 * would have been IsActive or IsRegistered - and both of those are already UActorComponent methods that
	 * mean something else. Hiding a base class method with an unrelated meaning is a bug that compiles.
	 */
	UFUNCTION(BlueprintPure, Category = "PulseScore")
	bool IsDirectingScore() const;

	/** The last intensity this component pushed, after smoothing. */
	UFUNCTION(BlueprintPure, Category = "PulseScore")
	float GetDrivenIntensity() const { return DrivenIntensity; }

	/** The world's conductor, or null. */
	UFUNCTION(BlueprintPure, Category = "PulseScore")
	UPulseScoreSubsystem* GetPulseScore() const;

private:
	/** Work out what the driver says the intensity should be this tick, before smoothing. */
	float SampleDriver() const;

	/** Turn ticking on only for the drivers that need it. */
	void UpdateTickEnabled();

	/** Seconds since this component started driving, for the curve. */
	float DriverElapsed = 0.0f;

	/** The smoothed value last pushed to the conductor. */
	float DrivenIntensity = 0.0f;

	/** True once this component has pushed at least one value, so the first sample is not smoothed from 0. */
	bool bHasDrivenIntensity = false;
};
