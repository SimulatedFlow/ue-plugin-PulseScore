// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "PulseScoreTypes.h"

void FPulseScoreDucker::Trigger(float InDepth, float InAttackSeconds, float InHoldSeconds, float InReleaseSeconds)
{
	Depth = FMath::Clamp(InDepth, 0.0f, 1.0f);
	AttackSeconds = FMath::Max(InAttackSeconds, 0.0f);
	HoldSeconds = FMath::Max(InHoldSeconds, 0.0f);
	ReleaseSeconds = FMath::Max(InReleaseSeconds, 0.0f);

	if (Depth <= 0.0f)
	{
		Reset();
		return;
	}

	// Retriggering does not stack and does not restart from silence. A second stinger a beat after the
	// first must not pull the mix twice as far down, and it must not let the mix jump back up to full on
	// its way to being pulled down again either - so if we are already at or past the depth we want, we go
	// straight back to holding it.
	if (Amount >= Depth)
	{
		Phase = EPhase::Hold;
		Amount = Depth;
		PhaseElapsed = 0.0f;
		return;
	}

	Phase = EPhase::Attack;
	PhaseElapsed = 0.0f;
}

void FPulseScoreDucker::Update(float DeltaSeconds)
{
	if (Phase == EPhase::Idle || DeltaSeconds <= 0.0f)
	{
		return;
	}

	PhaseElapsed += DeltaSeconds;

	switch (Phase)
	{
	case EPhase::Attack:
	{
		if (AttackSeconds <= 0.0f)
		{
			Amount = Depth;
			Phase = EPhase::Hold;
			PhaseElapsed = 0.0f;
			break;
		}

		const float Alpha = FMath::Clamp(PhaseElapsed / AttackSeconds, 0.0f, 1.0f);
		Amount = Depth * Alpha;
		if (Alpha >= 1.0f)
		{
			Amount = Depth;
			Phase = EPhase::Hold;
			PhaseElapsed = 0.0f;
		}
		break;
	}

	case EPhase::Hold:
	{
		Amount = Depth;
		if (PhaseElapsed >= HoldSeconds)
		{
			// Remember where the release starts from rather than releasing from Depth. An envelope that was
			// interrupted mid-attack has to come back to exactly zero, and dividing by Depth when Amount is
			// somewhere below it would leave a step at the end.
			ReleaseFrom = Amount;
			Phase = EPhase::Release;
			PhaseElapsed = 0.0f;
		}
		break;
	}

	case EPhase::Release:
	{
		if (ReleaseSeconds <= 0.0f)
		{
			Reset();
			break;
		}

		const float Alpha = FMath::Clamp(PhaseElapsed / ReleaseSeconds, 0.0f, 1.0f);
		Amount = ReleaseFrom * (1.0f - Alpha);
		if (Alpha >= 1.0f)
		{
			Reset();
		}
		break;
	}

	default:
		break;
	}
}

void FPulseScoreDucker::Reset()
{
	Phase = EPhase::Idle;
	Amount = 0.0f;
	ReleaseFrom = 0.0f;
	PhaseElapsed = 0.0f;
}
