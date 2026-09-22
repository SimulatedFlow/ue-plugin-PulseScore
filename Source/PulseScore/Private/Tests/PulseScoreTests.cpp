// Copyright 2026 Silvan Teufel. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#include "PulseScoreAsset.h"
#include "PulseScoreStatics.h"
#include "PulseScoreTypes.h"

/**
 * Everything under test here is static and world-free, which is the whole reason the maths was put in a
 * Blueprint function library instead of inside the subsystem. A UTickableWorldSubsystem cannot be
 * NewObject'd standalone - it needs a world - so any logic living in one is logic that never gets a test.
 * These exercise the same functions the mixer calls on every tick, not a second copy of them.
 */
namespace PulseScoreTest
{
	static UPulseScoreAsset* MakeScore()
	{
		UPulseScoreAsset* Score = NewObject<UPulseScoreAsset>(GetTransientPackage());
		Score->Tempo = 120.0f;
		Score->BeatsPerBar = 4;
		return Score;
	}

	static FPulseScoreLayer MakeLayer(FName Id, float Min, float Max, float EdgeWidth, bool bDucked = true)
	{
		FPulseScoreLayer Layer;
		Layer.Id = Id;
		Layer.IntensityMin = Min;
		Layer.IntensityMax = Max;
		Layer.EdgeWidth = EdgeWidth;
		Layer.bDuckedByStinger = bDucked;
		return Layer;
	}

	static float FindGain(const TArray<FPulseScoreLayerGain>& Gains, FName LayerId)
	{
		for (const FPulseScoreLayerGain& Gain : Gains)
		{
			if (Gain.LayerId == LayerId)
			{
				return Gain.Gain;
			}
		}
		return -1.0f;
	}
}

//~ 1. The window ----------------------------------------------------------------------------------------

/**
 * A layer's window is silent at its closed edges, full across its middle, and never goes backwards on the
 * way between the two. Everything the mixer does rests on this being true; if the ramp is not monotone, a
 * slow rise in intensity produces a layer that fades in, dips, and fades in again.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPulseScoreWindowGainTest, "PulseScore.Mix.LayerWindowGain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPulseScoreWindowGainTest::RunTest(const FString& /*Parameters*/)
{
	using namespace PulseScoreTest;

	// A window in the middle of the range: both edges are closed, so both are silent.
	const FPulseScoreLayer Middle = MakeLayer(TEXT("Middle"), 0.3f, 0.7f, 0.1f);

	TestEqual(TEXT("silent below the window"), UPulseScoreStatics::EvaluateLayerGain(Middle, 0.1f), 0.0f);
	TestEqual(TEXT("silent at the bottom edge"), UPulseScoreStatics::EvaluateLayerGain(Middle, 0.3f), 0.0f);
	TestEqual(TEXT("full in the plateau"), UPulseScoreStatics::EvaluateLayerGain(Middle, 0.5f), 1.0f);
	TestEqual(TEXT("silent at the top edge"), UPulseScoreStatics::EvaluateLayerGain(Middle, 0.7f), 0.0f);
	TestEqual(TEXT("silent above the window"), UPulseScoreStatics::EvaluateLayerGain(Middle, 0.9f), 0.0f);

	// Halfway up the lower ramp is exactly half gain, which is what makes it a crossfade rather than a
	// curve nobody can predict from the two numbers in the editor.
	TestEqual(TEXT("half way up the ramp"), UPulseScoreStatics::EvaluateLayerGain(Middle, 0.35f), 0.5f,
		KINDA_SMALL_NUMBER);

	// Monotone up the rising edge and down the falling one.
	float Previous = -1.0f;
	for (int32 Step = 0; Step <= 50; ++Step)
	{
		const float X = 0.3f + (0.1f * Step) / 50.0f;
		const float Gain = UPulseScoreStatics::EvaluateLayerGain(Middle, X);
		if (Gain < Previous - KINDA_SMALL_NUMBER)
		{
			AddError(FString::Printf(TEXT("Gain went backwards on the rising edge at intensity %.4f: %.4f after %.4f"),
				X, Gain, Previous));
			break;
		}
		Previous = Gain;
	}

	Previous = 2.0f;
	for (int32 Step = 0; Step <= 50; ++Step)
	{
		const float X = 0.6f + (0.1f * Step) / 50.0f;
		const float Gain = UPulseScoreStatics::EvaluateLayerGain(Middle, X);
		if (Gain > Previous + KINDA_SMALL_NUMBER)
		{
			AddError(FString::Printf(TEXT("Gain went up on the falling edge at intensity %.4f: %.4f after %.4f"),
				X, Gain, Previous));
			break;
		}
		Previous = Gain;
	}

	// A window that reaches the top of the range has no upper ramp: the layer written for maximum intensity
	// must be at full when the intensity is at maximum, not fading out of it.
	const FPulseScoreLayer Top = MakeLayer(TEXT("Top"), 0.7f, 1.0f, 0.1f);
	TestEqual(TEXT("open top edge is full at 1"), UPulseScoreStatics::EvaluateLayerGain(Top, 1.0f), 1.0f);
	TestEqual(TEXT("open top window still silent at its closed bottom"),
		UPulseScoreStatics::EvaluateLayerGain(Top, 0.7f), 0.0f);

	// And a bed that spans the whole range is always at full.
	const FPulseScoreLayer Bed = MakeLayer(TEXT("Bed"), 0.0f, 1.0f, 0.1f);
	TestEqual(TEXT("bed is full at zero intensity"), UPulseScoreStatics::EvaluateLayerGain(Bed, 0.0f), 1.0f);
	TestEqual(TEXT("bed is full at full intensity"), UPulseScoreStatics::EvaluateLayerGain(Bed, 1.0f), 1.0f);

	// A layer out of its window is at zero - and the point of the design is that it is still *playing*.
	// That half cannot be asserted here without a world; what can be asserted is that the mixer asks for
	// silence rather than for the layer to stop.
	UPulseScoreAsset* Score = MakeScore();
	Score->Layers.Add(Middle);
	Score->Layers.Add(Top);
	Score->Layers.Add(Bed);

	TArray<FPulseScoreLayerGain> Gains;
	UPulseScoreStatics::EvaluateLayerGains(Score, 0.5f, NAME_None, Gains);

	TestEqual(TEXT("one gain per layer"), Gains.Num(), 3);
	TestEqual(TEXT("middle layer is up at 0.5"), FindGain(Gains, TEXT("Middle")), 1.0f);
	TestEqual(TEXT("top layer is silent at 0.5"), FindGain(Gains, TEXT("Top")), 0.0f);
	TestEqual(TEXT("bed is up at 0.5"), FindGain(Gains, TEXT("Bed")), 1.0f);

	return true;
}

//~ 2. The boundary --------------------------------------------------------------------------------------

/**
 * A switch asked for with Bar quantisation has to land on a bar line - not near one.
 *
 * At 120 BPM in 4/4 a bar is exactly two seconds, so the assertion is arithmetic rather than a tolerance
 * argument: whatever moment the request arrives at, request time plus the wait is a whole number of bars.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPulseScoreBarBoundaryTest, "PulseScore.Timing.BarBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPulseScoreBarBoundaryTest::RunTest(const FString& /*Parameters*/)
{
	constexpr float Tempo = 120.0f;
	constexpr int32 BeatsPerBar = 4;

	const float SecondsPerBar = UPulseScoreStatics::GetSecondsPerBar(Tempo, BeatsPerBar);
	TestEqual(TEXT("a bar at 120 BPM in 4/4 is two seconds"), SecondsPerBar, 2.0f, KINDA_SMALL_NUMBER);

	// Ask at forty different moments spread across several bars. Every one of them has to land on a bar.
	for (int32 Step = 0; Step < 40; ++Step)
	{
		const float ClockSeconds = 0.137f * Step;
		const float Wait = UPulseScoreStatics::TimeUntilBoundary(EPulseScoreQuantization::Bar, ClockSeconds,
			Tempo, BeatsPerBar);

		const float ApplyAt = ClockSeconds + Wait;
		const float Remainder = FMath::Fmod(ApplyAt, SecondsPerBar);

		if (Remainder > 1.0e-3f && SecondsPerBar - Remainder > 1.0e-3f)
		{
			AddError(FString::Printf(
				TEXT("A Bar-quantised switch asked for at %.4f s would land at %.4f s, which is %.4f into a bar."),
				ClockSeconds, ApplyAt, Remainder));
			break;
		}

		if (Wait < 0.0f || Wait > SecondsPerBar + KINDA_SMALL_NUMBER)
		{
			AddError(FString::Printf(TEXT("The wait at %.4f s was %.4f s, which is not inside one bar."),
				ClockSeconds, Wait));
			break;
		}
	}

	// A request that arrives exactly on the line is not pushed a whole bar away.
	TestEqual(TEXT("already on a bar line waits for nothing"),
		UPulseScoreStatics::TimeUntilBoundary(EPulseScoreQuantization::Bar, 4.0f, Tempo, BeatsPerBar),
		0.0f);

	// The other boundaries, for the same reason and with the same arithmetic.
	TestEqual(TEXT("a beat is half a second"),
		UPulseScoreStatics::TimeUntilBoundary(EPulseScoreQuantization::Beat, 0.2f, Tempo, BeatsPerBar),
		0.3f, 1.0e-4f);
	TestEqual(TEXT("two bars is four seconds"),
		UPulseScoreStatics::TimeUntilBoundary(EPulseScoreQuantization::TwoBars, 0.5f, Tempo, BeatsPerBar),
		3.5f, 1.0e-4f);
	TestEqual(TEXT("Immediate waits for nothing"),
		UPulseScoreStatics::TimeUntilBoundary(EPulseScoreQuantization::Immediate, 1.3f, Tempo, BeatsPerBar),
		0.0f);

	// SectionEnd measures from where the section started and repeats with the phrase, so a change asked for
	// during the third pass through an eight bar phrase waits for the end of that pass.
	const float SectionStart = 10.0f;
	const float PhraseSeconds = SecondsPerBar * 8.0f;
	TestEqual(TEXT("half way through the first phrase"),
		UPulseScoreStatics::TimeUntilBoundary(EPulseScoreQuantization::SectionEnd, SectionStart + 5.0f,
			Tempo, BeatsPerBar, SectionStart, 8),
		PhraseSeconds - 5.0f, 1.0e-3f);
	TestEqual(TEXT("half way through the third phrase"),
		UPulseScoreStatics::TimeUntilBoundary(EPulseScoreQuantization::SectionEnd,
			SectionStart + PhraseSeconds * 2.0f + 5.0f, Tempo, BeatsPerBar, SectionStart, 8),
		PhraseSeconds - 5.0f, 1.0e-3f);

	// A section with no declared length has no end to wait for and falls back to the next bar rather than
	// never firing at all.
	TestEqual(TEXT("no declared length falls back to the bar"),
		UPulseScoreStatics::TimeUntilBoundary(EPulseScoreQuantization::SectionEnd, 0.5f, Tempo, BeatsPerBar,
			0.0f, 0),
		1.5f, 1.0e-4f);

	return true;
}

//~ 3. The section graph ---------------------------------------------------------------------------------

/**
 * A transition the score does not permit is refused and says why - it is never quietly taken.
 *
 * This is the difference between an allow-list that documents the shape of a piece and one that is a
 * comment. If an illegal transition were taken anyway, nothing downstream could tell that the graph had
 * been violated, and the graph would rot within a month.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPulseScoreSectionGraphTest, "PulseScore.Sections.TransitionRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPulseScoreSectionGraphTest::RunTest(const FString& /*Parameters*/)
{
	using namespace PulseScoreTest;

	UPulseScoreAsset* Score = MakeScore();

	FPulseScoreSection Explore;
	Explore.Id = TEXT("Explore");
	Explore.AllowedNextSections = { TEXT("Combat") };

	FPulseScoreSection Combat;
	Combat.Id = TEXT("Combat");
	Combat.AllowedNextSections = { TEXT("Explore"), TEXT("Victory") };

	FPulseScoreSection Victory;
	Victory.Id = TEXT("Victory");
	// Deliberately empty: unconstrained, not a dead end.

	Score->Sections = { Explore, Combat, Victory };

	FString Reason;

	TestTrue(TEXT("Explore may become Combat"),
		UPulseScoreStatics::IsSectionTransitionAllowed(Score, TEXT("Explore"), TEXT("Combat"), Reason));

	// The one that matters: Explore never listed Victory, so it is refused rather than taken.
	TestFalse(TEXT("Explore may not jump straight to Victory"),
		UPulseScoreStatics::IsSectionTransitionAllowed(Score, TEXT("Explore"), TEXT("Victory"), Reason));
	TestTrue(TEXT("and the refusal explains itself"), !Reason.IsEmpty());
	TestTrue(TEXT("the reason names the section that refused"), Reason.Contains(TEXT("Explore")));
	TestTrue(TEXT("the reason names the section that was asked for"), Reason.Contains(TEXT("Victory")));

	// A section that does not exist is a typo, and is refused whatever any list says.
	Reason.Reset();
	TestFalse(TEXT("a section that is not in the score is refused"),
		UPulseScoreStatics::IsSectionTransitionAllowed(Score, TEXT("Combat"), TEXT("Nowhere"), Reason));
	TestTrue(TEXT("and that refusal explains itself too"), !Reason.IsEmpty());

	// Entering from nothing is how a score starts, and it is always allowed.
	TestTrue(TEXT("entering from nothing is allowed"),
		UPulseScoreStatics::IsSectionTransitionAllowed(Score, NAME_None, TEXT("Combat"), Reason));

	// Re-entering the same section is a no-op, not a violation.
	TestTrue(TEXT("a section may re-enter itself"),
		UPulseScoreStatics::IsSectionTransitionAllowed(Score, TEXT("Explore"), TEXT("Explore"), Reason));

	// An empty allow-list means "no opinion", not "never again".
	TestTrue(TEXT("an empty allow-list is unconstrained"),
		UPulseScoreStatics::IsSectionTransitionAllowed(Score, TEXT("Victory"), TEXT("Explore"), Reason));

	return true;
}

//~ 4. The stinger duck ----------------------------------------------------------------------------------

/**
 * A stinger pulls the marked layers down and puts them back exactly where they were.
 *
 * "Exactly" is the assertion worth making. The duck is multiplicative and stateless on purpose - nothing is
 * stored - so a mix that was moving while the stinger sounded still comes back to the right place, and a
 * layer that was never marked comes back bit for bit.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPulseScoreStingerDuckTest, "PulseScore.Stingers.DuckAndRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPulseScoreStingerDuckTest::RunTest(const FString& /*Parameters*/)
{
	using namespace PulseScoreTest;

	UPulseScoreAsset* Score = MakeScore();
	Score->Layers.Add(MakeLayer(TEXT("Bed"), 0.0f, 1.0f, 0.1f, /*bDucked*/ true));
	Score->Layers.Add(MakeLayer(TEXT("Lead"), 0.0f, 1.0f, 0.1f, /*bDucked*/ false));

	TArray<FPulseScoreLayerGain> Before;
	UPulseScoreStatics::EvaluateLayerGains(Score, 0.5f, NAME_None, Before);
	TestEqual(TEXT("both layers start at full"), FindGain(Before, TEXT("Bed")), 1.0f);
	TestEqual(TEXT("and so does the lead"), FindGain(Before, TEXT("Lead")), 1.0f);

	// An envelope with numbers that are easy to reason about: a tenth of a second in, a quarter held, half
	// a second back out.
	FPulseScoreDucker Ducker;
	Ducker.Trigger(0.6f, 0.1f, 0.25f, 0.5f);
	TestTrue(TEXT("the ducker is running once triggered"), Ducker.IsDucking());
	TestEqual(TEXT("but it has not moved yet"), Ducker.GetAmount(), 0.0f);

	// Half way through the attack.
	Ducker.Update(0.05f);
	TestEqual(TEXT("half way into the attack it is half way down"), Ducker.GetAmount(), 0.3f, 1.0e-4f);

	// Through the attack and into the hold.
	Ducker.Update(0.05f);
	TestEqual(TEXT("the attack reaches full depth"), Ducker.GetAmount(), 0.6f, 1.0e-4f);

	TArray<FPulseScoreLayerGain> Ducked = Before;
	UPulseScoreStatics::ApplyStingerDuck(Ducked, Ducker.GetAmount());

	TestEqual(TEXT("the marked layer is pulled down"), FindGain(Ducked, TEXT("Bed")), 0.4f, 1.0e-4f);
	TestEqual(TEXT("the unmarked layer is untouched"), FindGain(Ducked, TEXT("Lead")), 1.0f);

	// Retriggering while ducking must not stack. Two stingers a beat apart cannot pull the mix twice as far
	// down as one.
	Ducker.Trigger(0.6f, 0.1f, 0.25f, 0.5f);
	Ducker.Update(0.01f);
	TestEqual(TEXT("a retrigger does not deepen the duck"), Ducker.GetAmount(), 0.6f, 1.0e-4f);

	// Run out the hold and the release.
	for (int32 Step = 0; Step < 200 && Ducker.IsDucking(); ++Step)
	{
		Ducker.Update(0.01f);
	}

	TestFalse(TEXT("the ducker finishes"), Ducker.IsDucking());
	TestEqual(TEXT("and lets go completely"), Ducker.GetAmount(), 0.0f);

	TArray<FPulseScoreLayerGain> Restored = Before;
	UPulseScoreStatics::ApplyStingerDuck(Restored, Ducker.GetAmount());

	TestEqual(TEXT("the marked layer is exactly back"), FindGain(Restored, TEXT("Bed")),
		FindGain(Before, TEXT("Bed")));
	TestEqual(TEXT("the unmarked layer never moved"), FindGain(Restored, TEXT("Lead")),
		FindGain(Before, TEXT("Lead")));

	return true;
}

//~ 5. The concurrency cap and the section layer list ----------------------------------------------------

/**
 * The cap silences the least important audible layers rather than the most recently added ones, and a
 * section's layer list is a mute mask on top of the windows.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPulseScoreLayerCapTest, "PulseScore.Mix.ConcurrencyCapAndSectionMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPulseScoreLayerCapTest::RunTest(const FString& /*Parameters*/)
{
	using namespace PulseScoreTest;

	UPulseScoreAsset* Score = MakeScore();

	FPulseScoreLayer Essential = MakeLayer(TEXT("Essential"), 0.0f, 1.0f, 0.0f);
	Essential.Priority = 5;
	Essential.VolumeScale = 0.4f;

	FPulseScoreLayer Loud = MakeLayer(TEXT("Loud"), 0.0f, 1.0f, 0.0f);
	Loud.Priority = 0;
	Loud.VolumeScale = 1.0f;

	FPulseScoreLayer Quiet = MakeLayer(TEXT("Quiet"), 0.0f, 1.0f, 0.0f);
	Quiet.Priority = 0;
	Quiet.VolumeScale = 0.5f;

	Score->Layers = { Essential, Loud, Quiet };

	TArray<FPulseScoreLayerGain> Gains;
	UPulseScoreStatics::EvaluateLayerGains(Score, 0.5f, NAME_None, Gains, /*MaxConcurrentLayers*/ 2);

	// Priority first: the quiet essential layer survives, the loud one survives on gain, and the merely
	// quiet one is the one that goes.
	TestEqual(TEXT("the essential layer survives the cap"), FindGain(Gains, TEXT("Essential")), 0.4f, 1.0e-4f);
	TestEqual(TEXT("the loudest of the rest survives"), FindGain(Gains, TEXT("Loud")), 1.0f, 1.0e-4f);
	TestEqual(TEXT("the quietest is capped out"), FindGain(Gains, TEXT("Quiet")), 0.0f);

	// No cap means no cap.
	UPulseScoreStatics::EvaluateLayerGains(Score, 0.5f, NAME_None, Gains, /*MaxConcurrentLayers*/ 0);
	TestEqual(TEXT("without a cap the third layer is back"), FindGain(Gains, TEXT("Quiet")), 0.5f, 1.0e-4f);

	// A section that names layers mutes the ones it does not name.
	FPulseScoreSection Sparse;
	Sparse.Id = TEXT("Sparse");
	Sparse.LayerIds = { TEXT("Essential") };
	Score->Sections = { Sparse };

	UPulseScoreStatics::EvaluateLayerGains(Score, 0.5f, TEXT("Sparse"), Gains);
	TestEqual(TEXT("the listed layer sounds"), FindGain(Gains, TEXT("Essential")), 0.4f, 1.0e-4f);
	TestEqual(TEXT("an unlisted layer is silent"), FindGain(Gains, TEXT("Loud")), 0.0f);

	// An empty list is "no opinion", not "no layers".
	FPulseScoreSection Full;
	Full.Id = TEXT("Full");
	Score->Sections.Add(Full);

	UPulseScoreStatics::EvaluateLayerGains(Score, 0.5f, TEXT("Full"), Gains);
	TestEqual(TEXT("an empty section list lets everything through"), FindGain(Gains, TEXT("Loud")), 1.0f, 1.0e-4f);

	return true;
}

//~ 6. Score validation ----------------------------------------------------------------------------------

/** A score says what is wrong with it, and being unplayable is a different answer from being untidy. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPulseScoreValidationTest, "PulseScore.Asset.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPulseScoreValidationTest::RunTest(const FString& /*Parameters*/)
{
	using namespace PulseScoreTest;

	UPulseScoreAsset* Score = MakeScore();

	TArray<FString> Problems;
	TestFalse(TEXT("a score with no layers cannot play"), Score->Validate(Problems));
	TestTrue(TEXT("and it says so"), Problems.Num() > 0);

	// A layer with no sound is a problem, and with no other layers it is a fatal one.
	Score->Layers.Add(MakeLayer(TEXT("Silent"), 0.0f, 1.0f, 0.1f));
	Problems.Reset();
	TestFalse(TEXT("a layer with no sound is not a playable score"), Score->Validate(Problems));

	// An empty window is reported even though it breaks nothing.
	FPulseScoreLayer Empty = MakeLayer(TEXT("Empty"), 0.5f, 0.5f, 0.1f);
	Score->Layers.Add(Empty);
	Problems.Reset();
	Score->Validate(Problems);

	const bool bMentionsWindow = Problems.ContainsByPredicate([](const FString& Problem)
	{
		return Problem.Contains(TEXT("Empty")) && Problem.Contains(TEXT("window"));
	});
	TestTrue(TEXT("an empty intensity window is reported"), bMentionsWindow);

	// A transition to a section that does not exist is caught at author time rather than at run time.
	FPulseScoreSection Broken;
	Broken.Id = TEXT("Broken");
	Broken.AllowedNextSections = { TEXT("Missing") };
	Score->Sections = { Broken };

	Problems.Reset();
	Score->Validate(Problems);

	const bool bMentionsMissing = Problems.ContainsByPredicate([](const FString& Problem)
	{
		return Problem.Contains(TEXT("Missing"));
	});
	TestTrue(TEXT("a transition to a section that does not exist is reported"), bMentionsMissing);

	// The start section falls back to the first one rather than refusing to play.
	Score->StartSection = TEXT("NotThere");
	TestEqual(TEXT("an unknown start section falls back to the first"), Score->GetEffectiveStartSection(),
		FName(TEXT("Broken")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
