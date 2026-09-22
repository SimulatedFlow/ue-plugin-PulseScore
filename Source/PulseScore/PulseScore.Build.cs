// Copyright 2026 Silvan Teufel. All Rights Reserved.

using UnrealBuildTool;

public class PulseScore : ModuleRules
{
	public PulseScore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// One runtime module and nothing else. The conductor has to exist in a packaged game, so there is
		// nowhere in this plugin that an editor-only module could sit.
		//
		// AudioMixer is the load-bearing dependency: UQuartzSubsystem and UQuartzClockHandle live there, and
		// they are the reason a switch lands on the beat instead of on a frame boundary.
		//
		// SignalProcessing is public rather than private for one concrete reason: every layer voice owns an
		// Audio::FVolumeFader, that voice type is declared inside UPulseScoreSubsystem, and the subsystem's
		// header is one a consuming game module includes. A private dependency does not propagate its
		// include paths, so DSP/VolumeFader.h has to be reachable from outside this module or nobody can
		// include our own public header.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AudioMixer",
			"SignalProcessing",
			"DeveloperSettings",
		});

		// AudioExtensions   - FAudioParameter, the type a named parameter is handed to a MetaSound layer as.
		//                     PulseScore forwards them straight through so a score can drive a stem's own
		//                     internals (a filter cutoff, a variation index) without going around the
		//                     conductor to find the voice.
		// RenderCore        - GWhiteTexture, the one-pixel texture the overlay's panel and gain bars are
		//                     tiled from. UCanvas has no untextured rectangle.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AudioExtensions",
			"RenderCore",
		});

		// Deliberately NOT here:
		//   UMG      - the product draws its numbers on UCanvas from AHUD so they survive a cooked Shipping
		//              build. The demo map's button strip is a UMG asset in Content calling the Blueprint
		//              library, exactly the way a project would use it.
		//   UnrealEd - everything here ships.
		//   Niagara / Chaos - nothing here is a particle or a rigid body.
	}
}
