// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "PulseScoreComponent.h"

#include "Curves/CurveFloat.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "PulseScoreAsset.h"
#include "PulseScoreLog.h"
#include "PulseScoreSubsystem.h"

UPulseScoreComponent::UPulseScoreComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// Music is not gameplay. Nothing here replicates, nothing here is authoritative, and a dedicated server
	// has no business creating audio components - so the component simply does not exist on one.
	bAutoActivate = true;
	SetIsReplicatedByDefault(false);
}

void UPulseScoreComponent::BeginPlay()
{
	Super::BeginPlay();

	UpdateTickEnabled();

	if (bPlayOnBeginPlay)
	{
		PlayScore();
	}
}

void UPulseScoreComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bStopOnEndPlay && IsDirectingScore())
	{
		// Only stop what this component started. A second component that took over the conductor in the
		// meantime owns it now, and pulling its music out from under it on our way off the level would be
		// the sort of action at a distance nobody can debug.
		if (UPulseScoreSubsystem* Subsystem = GetPulseScore())
		{
			Subsystem->Stop(Quantization);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void UPulseScoreComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (IntensityDriver == EPulseScoreIntensityDriver::Manual)
	{
		return;
	}

	UPulseScoreSubsystem* Subsystem = GetPulseScore();
	if (!Subsystem || !Subsystem->IsPlaying())
	{
		return;
	}

	DriverElapsed += DeltaTime;

	const float Raw = SampleDriver();

	if (!bHasDrivenIntensity || IntensitySmoothingSeconds <= 0.0f)
	{
		DrivenIntensity = Raw;
		bHasDrivenIntensity = true;
	}
	else
	{
		// Exponential approach rather than a constant rate. A distance driver on a sprinting player produces
		// a value that jumps at every corner, and a constant-rate follow would take the same time to cross a
		// small step as a large one - which reads as the music lagging behind the fight.
		const float Alpha = FMath::Clamp(DeltaTime / IntensitySmoothingSeconds, 0.0f, 1.0f);
		DrivenIntensity = FMath::Lerp(DrivenIntensity, Raw, Alpha);
	}

	// Immediate, deliberately. The component is producing a continuous value, and quantising a continuous
	// value would mean holding it still for a bar and then stepping - which is audibly worse than letting
	// the layer faders do the smoothing they are already there to do. The component's own Quantization is
	// for its discrete calls: sections, stingers and the stop.
	Subsystem->SetIntensity(DrivenIntensity, EPulseScoreQuantization::Immediate);
}

//~ API --------------------------------------------------------------------------------------------------

bool UPulseScoreComponent::PlayScore()
{
	if (!Score)
	{
		UE_LOG(LogPulseScore, Warning, TEXT("%s has no score to play."), *GetPathName());
		return false;
	}

	UPulseScoreSubsystem* Subsystem = GetPulseScore();
	if (!Subsystem)
	{
		return false;
	}

	DriverElapsed = 0.0f;
	bHasDrivenIntensity = false;

	if (!Subsystem->PlayScore(Score, EPulseScoreQuantization::Immediate))
	{
		return false;
	}

	if (!StartSection.IsNone() && StartSection != Subsystem->GetCurrentSection())
	{
		Subsystem->EnterSection(StartSection, EPulseScoreQuantization::Immediate);
	}

	return true;
}

void UPulseScoreComponent::StopScore()
{
	if (UPulseScoreSubsystem* Subsystem = GetPulseScore())
	{
		Subsystem->Stop(Quantization);
	}
}

void UPulseScoreComponent::SetIntensity(float Intensity)
{
	DrivenIntensity = FMath::Clamp(Intensity, 0.0f, 1.0f);
	bHasDrivenIntensity = true;

	if (UPulseScoreSubsystem* Subsystem = GetPulseScore())
	{
		Subsystem->SetIntensity(DrivenIntensity, Quantization);
	}
}

bool UPulseScoreComponent::EnterSection(FName SectionId)
{
	UPulseScoreSubsystem* Subsystem = GetPulseScore();
	return Subsystem ? Subsystem->EnterSection(SectionId, Quantization) : false;
}

bool UPulseScoreComponent::TriggerStinger(FName StingerId)
{
	UPulseScoreSubsystem* Subsystem = GetPulseScore();
	return Subsystem ? Subsystem->TriggerStinger(StingerId, Quantization) : false;
}

bool UPulseScoreComponent::IsDirectingScore() const
{
	const UPulseScoreSubsystem* Subsystem = GetPulseScore();
	return Subsystem && Subsystem->IsPlaying() && Subsystem->GetScore() == Score;
}

UPulseScoreSubsystem* UPulseScoreComponent::GetPulseScore() const
{
	return UPulseScoreSubsystem::Get(this);
}

//~ Drivers ----------------------------------------------------------------------------------------------

float UPulseScoreComponent::SampleDriver() const
{
	switch (IntensityDriver)
	{
	case EPulseScoreIntensityDriver::Curve:
	{
		if (!IntensityCurve)
		{
			return DrivenIntensity;
		}

		float Time = DriverElapsed;
		if (bLoopCurve)
		{
			float CurveMin = 0.0f;
			float CurveMax = 0.0f;
			IntensityCurve->GetTimeRange(CurveMin, CurveMax);

			const float Period = LoopSeconds > 0.0f ? LoopSeconds : CurveMax;
			if (Period > 0.0f)
			{
				Time = FMath::Fmod(DriverElapsed, Period);
			}
		}

		return FMath::Clamp(IntensityCurve->GetFloatValue(Time), 0.0f, 1.0f);
	}

	case EPulseScoreIntensityDriver::DistanceToActor:
	{
		const AActor* Owner = GetOwner();
		if (!Owner)
		{
			return DrivenIntensity;
		}

		const AActor* Target = DistanceTarget;
		if (!Target)
		{
			// Falling back to the local player pawn is the case this driver is nearly always used for -
			// "how close is the player" - and making the designer wire that up on every actor would be a
			// field that is always filled in with the same thing.
			if (const APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0))
			{
				Target = Controller->GetPawn();
			}
		}

		if (!Target)
		{
			return DrivenIntensity;
		}

		const float Distance = FVector::Dist(Owner->GetActorLocation(), Target->GetActorLocation());
		const float Near = FMath::Min(NearDistance, FarDistance);
		const float Far = FMath::Max(NearDistance, FarDistance);

		if (Far - Near <= KINDA_SMALL_NUMBER)
		{
			return Distance <= Near ? 1.0f : 0.0f;
		}

		return FMath::Clamp(1.0f - (Distance - Near) / (Far - Near), 0.0f, 1.0f);
	}

	default:
		return DrivenIntensity;
	}
}

void UPulseScoreComponent::UpdateTickEnabled()
{
	// A component that is not driving anything has no reason to be in the tick list. There will be one of
	// these on the player and one on every trigger in the level, and the ones on the triggers are all
	// Manual.
	SetComponentTickEnabled(IntensityDriver != EPulseScoreIntensityDriver::Manual);
}
