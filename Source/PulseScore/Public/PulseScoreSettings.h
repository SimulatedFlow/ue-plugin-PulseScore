// Copyright 2026 Silvan Teufel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "PulseScoreTypes.h"
#include "PulseScoreSettings.generated.h"

/**
 * Project-wide defaults for PulseScore, under Project Settings -> Plugins -> PulseScore.
 *
 * The split is the same one every plugin here makes. What belongs to a *piece of music* - its tempo, its
 * stems, where each layer comes in - is on the score asset, because it changes from the menu theme to the
 * boss fight. What a *project* decided once - that its music switches on bars, that a stinger pulls the mix
 * down six decibels, that no more than eight stems may sound at once - is here, so it is written down in
 * one place instead of on every score and every call site.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "PulseScore"))
class PULSESCORE_API UPulseScoreSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPulseScoreSettings();

	//~ UDeveloperSettings interface
	virtual FName GetCategoryName() const override;
	virtual FName GetSectionName() const override;

	/** The settings object, never null. */
	static const UPulseScoreSettings& Get();

	//~ Timing ------------------------------------------------------------------------------------------

	/**
	 * The boundary every call that says "Project Default" resolves to.
	 *
	 * Bar, because it is right far more often than it is wrong, and because the failure mode of being too
	 * coarse - the change arrives a bar late - is a great deal kinder than the failure mode of being too
	 * fine, which is a stem restarting in the middle of a snare.
	 *
	 * Setting this to Default itself is meaningless and is treated as Bar.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Timing")
	EPulseScoreQuantization DefaultQuantization = EPulseScoreQuantization::Bar;

	/**
	 * Name of the Quartz clock PulseScore creates.
	 *
	 * Worth changing only if the project already runs a clock of its own under this name. Two systems
	 * sharing a clock is not automatically wrong - it is how you keep gameplay and music on the same
	 * pulse - but whichever of them sets the tempo last wins, and that is rarely what either intended.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Timing")
	FName ClockName = FName(TEXT("PulseScore"));

	/**
	 * Keep the music running while the game is paused.
	 *
	 * On, because a pause menu that cuts the score dead is a pause menu that sounds broken. This makes the
	 * layer voices UI sounds and keeps the Quartz subsystem ticking, so bar and beat carry on and the score
	 * is still in phase when the game resumes.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Timing")
	bool bMusicIgnoresPause = true;

	//~ Mixing ------------------------------------------------------------------------------------------

	/**
	 * Edge width used by any layer that left its own at -1, in intensity units.
	 *
	 * 0.15 means a layer's window spends roughly the outer 15% of its range fading in and the same fading
	 * out. Wide enough that a sweep across the window is a crossfade rather than a switch, narrow enough
	 * that a layer with a window of 0.4 still has a plateau where it is genuinely at full.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Mixing", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float DefaultEdgeWidth = 0.15f;

	/** Fade length used by any layer that left its own at -1, in bars. */
	UPROPERTY(config, EditAnywhere, Category = "Mixing", meta = (ClampMin = "0.0", UIMax = "8.0"))
	float DefaultFadeBars = 1.0f;

	/**
	 * Most layers that may be audible at once.
	 *
	 * A ceiling on voices, not on the score. Layers over the cap are held at zero - they keep playing and
	 * they keep their phase - so raising the cap mid-level brings them back in on the beat rather than from
	 * wherever a fresh start would have landed. Which layers survive the cap is decided by priority first
	 * and gain second, so the cap never mutes the loud stem to keep one that was fading out.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Mixing", meta = (ClampMin = "1", ClampMax = "32"))
	int32 MaxConcurrentLayers = 8;

	/** A trim over the whole score, applied on top of every layer's own gain. */
	UPROPERTY(config, EditAnywhere, Category = "Mixing", meta = (ClampMin = "0.0", UIMax = "2.0"))
	float MasterVolume = 1.0f;

	/**
	 * Gain below which a layer counts as silent for the audible-layer count and the concurrency cap.
	 *
	 * Not a mute. The voice keeps playing at whatever tiny gain it has; this is only the line the counters
	 * and the cap draw, so a layer at 0.001 is not reported as one of your eight.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Mixing", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float AudibleGainThreshold = 0.01f;

	//~ Stingers ----------------------------------------------------------------------------------------

	/**
	 * How far a stinger pulls the layers marked for it down, 0..1.
	 *
	 * 0.6 is roughly eight decibels: enough to open a hole for the hit, not so much that the score appears
	 * to stop. Layers with bDuckedByStinger off are untouched.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Stingers", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StingerDuckAmount = 0.6f;

	/** Seconds the duck takes to reach full depth. Short - the hole has to be open before the hit lands. */
	UPROPERTY(config, EditAnywhere, Category = "Stingers",
		meta = (ClampMin = "0.0", UIMax = "1.0", ForceUnits = "s"))
	float DuckAttackSeconds = 0.05f;

	/** Seconds the duck stays at full depth before it starts letting go. */
	UPROPERTY(config, EditAnywhere, Category = "Stingers",
		meta = (ClampMin = "0.0", UIMax = "8.0", ForceUnits = "s"))
	float DuckHoldSeconds = 0.5f;

	/** Seconds the duck takes to come back up. Long, because a mix that snaps back is a mix that pumps. */
	UPROPERTY(config, EditAnywhere, Category = "Stingers",
		meta = (ClampMin = "0.0", UIMax = "8.0", ForceUnits = "s"))
	float DuckReleaseSeconds = 0.8f;

	//~ Sections ----------------------------------------------------------------------------------------

	/**
	 * Refuse a section change the score's allow-list does not permit.
	 *
	 * On. The whole reason the allow-list exists is that somebody wrote down the shape of the piece; taking
	 * the transition anyway and hoping turns that document into a comment. Off downgrades the refusal to a
	 * warning and takes the transition, which is occasionally what a project in the middle of reworking its
	 * score graph wants for an afternoon.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Sections")
	bool bStrictSectionTransitions = true;

	//~ Presentation ------------------------------------------------------------------------------------

	/**
	 * Start with the counters overlay on.
	 *
	 * Off by default, because it is a development tool and not a HUD. PulseScore.Show 1 turns it on at any
	 * time, and it draws on UCanvas rather than through UMG so it is still there in a cooked Shipping build
	 * where a debug-only draw would have been compiled out.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Presentation")
	bool bShowOverlayByDefault = false;

	/**
	 * Draw the overlay through AHUD::OnHUDPostRender, so a project keeps its own HUD class.
	 *
	 * On, because the alternative is asking a project to reparent its HUD to see a music system's numbers,
	 * and no project is going to do that.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Presentation")
	bool bAutoDrawOverlayOnAnyHUD = true;

	/** Where the overlay sits, in pixels from the top left of the viewport. */
	UPROPERTY(config, EditAnywhere, Category = "Presentation")
	FVector2D OverlayOrigin = FVector2D(28.0f, 90.0f);

	/** How wide the overlay panel is drawn, in pixels. */
	UPROPERTY(config, EditAnywhere, Category = "Presentation",
		meta = (ClampMin = "160.0", UIMax = "900.0"))
	float OverlayWidth = 430.0f;
};
