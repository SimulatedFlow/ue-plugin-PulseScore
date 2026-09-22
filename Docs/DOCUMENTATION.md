# PulseScore - Adaptive Music Director

**Documentation**

Unreal Engine **5.8** - **Win64** - one runtime module (`PulseScore`), no editor module, no third-party code.

---

## Table of contents

1. [What PulseScore is, and what it is not](#1-what-pulsescore-is-and-what-it-is-not)
2. [Supported engine and platforms](#2-supported-engine-and-platforms)
3. [Installation](#3-installation)
4. [Quick start](#4-quick-start)
5. [The model](#5-the-model)
6. [Setting up a score](#6-setting-up-a-score)
7. [Quantisation](#7-quantisation)
8. [Intensity and the layer window](#8-intensity-and-the-layer-window)
9. [Sections](#9-sections)
10. [Stingers and ducking](#10-stingers-and-ducking)
11. [The component](#11-the-component)
12. [The Blueprint library](#12-the-blueprint-library)
13. [Code examples](#13-code-examples)
14. [Project settings](#14-project-settings)
15. [The counters overlay and console commands](#15-the-counters-overlay-and-console-commands)
16. [The demo map](#16-the-demo-map)
17. [Tests](#17-tests)
18. [Performance and threading](#18-performance-and-threading)
19. [Troubleshooting](#19-troubleshooting)
20. [API reference](#20-api-reference)

---

## 1. What PulseScore is, and what it is not

Unreal already ships the building blocks of adaptive music. **Quartz** is a sample-accurate clock that can
schedule a sound to start on a musical boundary. **MetaSounds** is a graph you can build a stem in. What
neither of them is, is a **conductor** - the layer that turns "the fight started" into a drum track fading in
on the next bar line, and the layer that knows the fight cannot go straight to the victory theme.

PulseScore is that layer, and nothing else.

**It does not compose.** There is no generation, no chord model, no arrangement engine.

**It does not synthesise.** It creates no sound of its own. Every note that comes out of it came out of a
`USoundBase` you provided.

It plays your stems, mixes them against a single number, and switches on the beat. If that sentence sounds
small, it is the same kind of small as "decides what to aim at, but does not fire" - it is the part that is
tedious to write, easy to write badly, and needed by every project that wants music to respond.

---

## 2. Supported engine and platforms

| | |
|---|---|
| **Engine version** | Unreal Engine 5.8 (`"EngineVersion": "5.8.0"`) |
| **Target platform** | **Win64** - the single value in the module's `PlatformAllowList` |
| **Module** | one, `PulseScore`, `Type: Runtime`, `LoadingPhase: PreDefault` |
| **Project type** | C++ **and** Blueprint-only projects (see the note below) |
| **Configurations** | Development and Shipping, both verified with `RunUAT BuildPlugin` |
| **Third-party code** | none |
| **Engine dependencies** | `Core`, `CoreUObject`, `Engine`, `AudioMixer`, `SignalProcessing`, `DeveloperSettings` (public); `AudioExtensions`, `RenderCore` (private) |
| **Plugin dependencies** | none - PulseScore does not require any other Marketplace or Fab plugin |

**On Blueprint-only projects.** Everything PulseScore does is reachable from Blueprint: the score is a data
asset, the whole API is on a Blueprint function library, and the component is spawnable. A Blueprint-only
project uses the shipped binaries and never needs a compiler. A C++ project can additionally include the
public headers and call the subsystem directly.

**On other platforms.** The plugin contains no platform-specific code - it is Quartz, `UAudioComponent` and
`UCanvas`, all of which are cross-platform. `PlatformAllowList` is nevertheless set to `Win64` alone, because
Fab requires the list to match the platforms the product page claims and the ones the package was actually
built and tested for. Building for another platform is a matter of adding it to that list and rebuilding; it
is deliberately not claimed here until it has been done and verified.

---

## 3. Installation

### From a Fab / Marketplace download (recommended)

1. Close the Unreal Editor.
2. Copy the `PulseScore` folder into your project's `Plugins` directory, so the layout is
   `<YourProject>/Plugins/PulseScore/PulseScore.uplugin`.
   *(Installing into the engine's `Engine/Plugins/Marketplace` folder works too, if you want it in every
   project.)*
3. Open the project. If you are asked to rebuild missing modules, say yes.
4. **Edit -> Plugins -> Audio -> PulseScore**, tick **Enabled**, restart the editor when prompted.
5. Optional, and worth doing once: **Project Settings -> Plugins -> PulseScore** to set the project's default
   quantisation and ducking behaviour.

### Verifying the install

Open any level, press Play and type into the console:

```
PulseScore.Show 1
```

The counters panel appears (it will say no score is playing). If it does not, the plugin is not enabled or
the module failed to load - check the Output Log for `LogPulseScore`.

### For a C++ project

Add the module to your target's dependencies:

```csharp
// <YourProject>.Build.cs
PublicDependencyModuleNames.AddRange(new string[] { "PulseScore" });
```

Then include what you need:

```cpp
#include "PulseScoreStatics.h"
#include "PulseScoreSubsystem.h"
#include "PulseScoreAsset.h"
#include "PulseScoreComponent.h"
#include "PulseScoreTypes.h"
```

### Uninstalling

Remove the `Plugins/PulseScore` folder. Any `UPulseScoreAsset` in your content and any `UPulseScoreComponent`
on your actors become missing-class references, so delete or replace those first if you want a clean project.

---

## 4. Quick start

Five minutes, in the order that actually works.

**1. Prepare the stems.** Two to six looping audio assets, all written at the **same tempo**, each looping on
its own (tick **Looping** on the Sound Wave, or build the loop into the Sound Cue / MetaSound). PulseScore
never re-triggers a layer, so the loop has to be the asset's own job.

**2. Create the score.** Content Browser -> **Miscellaneous -> Data Asset -> Pulse Score**. Set `Tempo` and
`BeatsPerBar` to match the stems.

**3. Add the layers.** One entry per stem:

| Id | Sound | IntensityMin | IntensityMax |
|---|---|---|---|
| `Bed` | your pad loop | 0.0 | 1.0 |
| `Pulse` | your bass loop | 0.15 | 1.0 |
| `Drums` | your drum loop | 0.45 | 1.0 |
| `Lead` | your lead loop | 0.75 | 1.0 |

That is already an adaptive score: intensity 0 is the pad alone, intensity 1 is everything.

**4. Play it.** Either drop a **Pulse Score** component on an actor in the level, set its `Score`, and leave
`bPlayOnBeginPlay` on - or call `Play Score` from any Blueprint:

```
PulseScore -> Play Score (Score = DA_MyScore, When = Immediate)
```

**5. Drive it.** From gameplay, whenever the situation changes:

```
PulseScore -> Set Intensity (Intensity = 0.7)
```

That single float is the whole game-facing API. The composer decides in the score asset what 0.7 sounds like,
without a line of gameplay code changing.

**6. Watch it.** `PulseScore.Show 1` in the console shows bar and beat, the section, the intensity, the duck
depth and a gain meter per layer. Push the intensity around and watch the layers cross-fade.

### Then, when you want more

* **Sections** - add named parts (`Exploration`, `Combat`, `Victory`) and list what each may lead to. Call
  `Enter Section`; the change is queued and taken on the next bar line.
* **Stingers** - put one-shots in the `Stingers` map and call `Trigger Stinger`. The marked layers duck
  around it automatically.
* **Boundaries** - every call takes a quantisation pin. Leave it on `Project Default` unless a specific call
  needs to be `Immediate`.

---

## 5. The model

There are four nouns.

**The score** (`UPulseScoreAsset`) is a piece of music: a tempo, a time signature, a list of layers, a list
of sections and a map of stingers. It is a data asset, so it is content, and a composer can be handed one to
fill in.

**A layer** is one stem. It has an `Id`, a `USoundBase`, and an intensity window. Layers all start together
and none of them stops until the score does.

**A section** is a named part of the piece - Exploration, Combat, Victory - with the list of sections it may
lead to, an optional list of the layers allowed to sound in it, an optional transition stinger and an
optional length in bars.

**A stinger** is a one-shot addressed by name. It is fired on a boundary and it ducks the layers marked for
it while it sounds.

And there is one verb the game actually uses: **intensity**, a float from 0 to 1. Everything else is
occasional. Intensity is what a fight, a chase or a countdown feeds in every frame.

The conductor (`UPulseScoreSubsystem`) is a `UTickableWorldSubsystem`. There is exactly one per world, it
holds exactly one Quartz clock, and it is created for Game and PIE worlds only - starting a score in an
editor viewport while somebody is dressing a level is the kind of help nobody asks for twice.

---

## 6. Setting up a score

### The stems

Every stem must be written **at the same tempo** as the score, and every looping layer must loop **on its
own**. PulseScore does not time-stretch and it does not re-trigger:

* Stretching in real time is a different plugin with a different cost, and a layered score whose layers
  disagree about the tempo is not a score.
* A re-trigger is a new voice with a new start latency. After four of them the stems have drifted apart, and
  the drift is exactly what layered music exists to avoid.

Set the looping flag on the Sound Wave, the Sound Cue or the MetaSound, and the conductor never has to touch
it again.

### The asset

Create a **Pulse Score** asset (Content Browser -> Miscellaneous -> Data Asset -> Pulse Score).

| Field | Meaning |
|---|---|
| `Tempo` | Beats per minute. Everything is measured from this. |
| `BeatsPerBar` | Numerator of the time signature. |
| `Layers` | One entry per stem, in the order the overlay lists them. |
| `Sections` | The named parts and the graph between them. Optional. |
| `StartSection` | The section `PlayScore` enters. Blank uses the first one. |
| `Stingers` | Name to sound, for one-shots. |

The asset validates itself. In the editor, **Validate Assets** reports every problem it can see - a layer
with no sound, a duplicate id, an empty intensity window, a transition to a section that does not exist, a
section naming a stinger that is not in the map. Being *unplayable* (no tempo, no beats, no layer with a
sound) is an error; everything else is a warning, because a score missing one stem out of five is still worth
hearing and the log says which stem is missing.

---

## 7. Quantisation

Every call into PulseScore carries an `EPulseScoreQuantization`:

| Value | Meaning |
|---|---|
| `Project Default` | Whatever Project Settings says. The default on every pin. |
| `Immediate` | Now, mid-bar, mid-note. Right for a death sting, wrong for nearly everything else. |
| `Next Beat` | The tightest musical boundary. Responsive, and it can cut a held note. |
| `Next Bar` | The default, and the right answer when you are not sure. |
| `Next Two Bars` | For scores whose phrases are two bars long. |
| `End of Section` | The end of the current section's phrase. Needs the section to declare a `LengthInBars`. |

### How a switch is actually scheduled

1. `UPulseScoreStatics::TimeUntilBoundary` computes, in seconds, when the boundary falls. This is a pure
   function with no world behind it, and it is unit-tested.
2. The change is recorded as pending, together with the clock time it is due at.
3. A Quartz notification is scheduled for the same boundary. When it arrives, the change is applied - on the
   audio render thread's schedule, which is sample-accurate.
4. If the notification never arrives - a dedicated server, a `-nosound` session, an automation run with no
   audio device - the tick that passes the predicted due time applies the change instead.

Step 4 matters more than it looks. A music system that silently stops changing sections when there is no
audio device is a music system that behaves differently in your CI than on your desk.

### Why intensity is different

Intensity is applied on the tick that passes its boundary rather than through a Quartz command. A gain
change is a **fade**, and a fade cannot be heard to be a frame late. Spending a scheduled audio-render-thread
command on it would buy accuracy nobody can perceive and leave a callback that can outlive the score that
asked for it.

Section changes, stops and stingers do go through Quartz, because those are attacks and a sample of latency
on an attack is audible.

### `End of Section` repeats with the phrase

A section is a length, not a countdown that expires. A change asked for during the third pass through an
eight bar phrase waits for the end of *that* pass. A section with no declared length falls back to the next
bar rather than never firing - silently never switching is the worst of the three possible behaviours.

---

## 8. Intensity and the layer window

A layer sounds inside `[IntensityMin, IntensityMax]`, with an `EdgeWidth` ramp at each end:

```
gain
 1 |        ______________
   |       /              \
   |      /                \
 0 |_____/                  \_____
       Min  Min+Edge  Max-Edge  Max      intensity ->
```

* At and outside the closed edges the layer is **silent**.
* Across the middle it is at full.
* The ramps never overlap: `EdgeWidth` is clamped to half the window, so a narrow window still reaches full
  gain instead of being permanently at half volume.

### The two exceptions

**A window whose top is at 1.0 has no upper ramp.** Intensity cannot go higher, so a fade-out there would
silence the layer written for maximum intensity exactly when the fight is at its worst.

**A window whose bottom is at 0.0 has no lower ramp**, for the same reason at the other end: the quiet bed
should be audible when nothing is happening.

This is the one place where the musically obvious answer beats the geometrically consistent one.

### A silent layer is still playing

**This is the most important sentence in the documentation.** A layer outside its window is held at gain
zero. It is not stopped, not paused and not unloaded. When the intensity comes back it is still exactly in
phase with every other stem, because it never stopped being in phase.

It costs a voice. That is the trade, and it is the right one: a restarted stem against a running one is
audibly wrong in a way that a spare voice never is. `MaxConcurrentLayers` is there for projects that need to
bound the voice count.

### Order of operations

The mixer, in order:

1. **Window gain** from the intensity.
2. **`VolumeScale`**, the layer's constant trim.
3. **The section's layer list**, if it names any - a mute mask, not a second mixer.
4. **The concurrency cap**, priority first and gain second.
5. **The stinger duck**, applied last because it is a live envelope and not a property of the score.
6. **`MasterVolume`** and the per-layer fader.

Steps 1 to 4 are `UPulseScoreStatics::EvaluateLayerGains`, a static function with no world behind it. Step 5
is `ApplyStingerDuck`. Both are unit-tested, and both are the same code the mixer runs - not a
re-implementation that agrees with it until somebody edits one of the two.

### Fades

`FadeBars` on a layer (or `DefaultFadeBars` in settings) is the fader's **smoothing constant**, not a linear
ramp time. Intensity usually moves continuously, and a fader chasing a moving target is a smoother. A value
of one bar at 120 BPM in 4/4 means the gain covers most of the remaining distance in about two seconds.

---

## 9. Sections

A section change is queued with `EnterSection` and lands on the boundary you asked for. While it is pending,
`GetScoreState` reports both the current and the pending section, plus a countdown - which is what the
overlay draws.

### The allow-list

`AllowedNextSections` is the shape of the piece written down where the runtime can check it. A change the
list does not permit is **refused and logged with its reason**, not quietly taken. If it were taken anyway,
nothing downstream could tell the graph had been violated and the graph would rot within a month.

Three rules that are easy to get wrong, so they are spelled out:

* **An empty list is unconstrained, not a dead end.** Reading it the other way would turn "the author has not
  drawn the graph yet" into "the music can never change again".
* **Entering from nothing is always allowed.** The list says where a section may *go*, and a score that is
  not playing yet is not in one.
* **A section may always re-enter itself.** That is a no-op, not a violation.

A target section that does not exist in the score is refused whatever the lists say - and whatever
`bStrictSectionTransitions` says - because that one is a typo, not a design decision.

Turning off `bStrictSectionTransitions` downgrades a refusal to a warning and takes the transition. That is
occasionally what a project in the middle of reworking its score graph wants for an afternoon.

### A newer request replaces an older one

Calling `EnterSection` twice before the first has landed does not queue two changes. The newest call is
always the better description of what the game wants; finishing the stale one first would put the score into
a section the game has already moved on from, and then move it again a bar later.

Every queued change records the clock time it is due at, so a Quartz notification belonging to a request that
has already been replaced arrives *early*, is recognised as early, and is dropped.

### On entry

A section can force an `EntryIntensity` (leave it at -1 to keep whatever the game set) and fire a
`TransitionStinger`. The transition stinger is fired `Immediate`, because the change is already standing on
the boundary it was scheduled for - quantising it again would push the transition hit a whole bar past the
transition.

---

## 10. Stingers and ducking

`TriggerStinger(Name, When)` looks the sound up in the score's `Stingers` map and plays it quantised on its
own voice. Missing names are logged, not swallowed.

While it sounds, every layer with `bDuckedByStinger` is pulled down by an attack-hold-release envelope
configured in project settings. Layers with the flag off are untouched - that is for the layer the stinger is
supposed to sit *with*.

Three properties worth knowing:

* **The duck starts when the sound starts**, not when `TriggerStinger` was called. A stinger queued three
  beats out does not flatten the mix for three beats first, waiting for a hit that has not landed.
* **The duck is multiplicative and stateless.** Nothing is stored, so letting it go restores the gains
  exactly - even if the intensity moved while the stinger was sounding.
* **Retriggering does not stack.** Two stingers a beat apart do not pull the mix twice as far down, and there
  is no audible step back up between them.

---

## 11. The component

`UPulseScoreComponent` is the convenient way in: drop it on an actor, point it at a score, and the actor
conducts. The plugin does not need it - everything it does is one call into `UPulseScoreStatics` - but it
saves two things every project would otherwise re-write.

**Curve over time.** Point `IntensityCurve` at a float curve and the component reads it against seconds since
the score started, optionally wrapping at `LoopSeconds`. This is what the demo director uses.

**Distance to actor.** The component maps the distance between its owner and `DistanceTarget` through
`NearDistance` (intensity 1) and `FarDistance` (intensity 0). Leaving the target blank falls back to the
local player pawn, because "how close is the player" is what this driver is nearly always for.

`IntensitySmoothingSeconds` smooths the driven value with an exponential approach - a distance driver on a
sprinting player jumps at every corner, and a constant-rate follow would take the same time to cross a small
step as a large one.

The driven value is pushed with `Immediate` quantisation on purpose: quantising a continuous value means
holding it still for a bar and then stepping, which is audibly worse than letting the layer faders do the
smoothing they are already there for. The component's own `Quantization` applies to its discrete calls -
sections, stingers and the stop.

With `Manual`, the component does not tick at all.

There is exactly one conductor per world, so two components on two actors are two things shouting at the same
subsystem. That is not an error - a level might well want the player's component to drive the intensity while
a trigger volume's component changes sections - but the last writer of a given value wins, and the overlay
shows what actually happened.

### Note on the method names

The component's "is this component's score the one playing" method is called `IsDirectingScore`, not
`IsActive` or `IsRegistered`. Both of those already exist on `UActorComponent` and mean something else;
hiding a base class method with an unrelated meaning is a bug that compiles.

---

## 12. The Blueprint library

`UPulseScoreStatics` has two halves, and the split is deliberate.

**The conductor half** forwards to the world's subsystem: `PlayScore`, `StopScore`, `SetIntensity`,
`GetIntensity`, `EnterSection`, `GetCurrentSection`, `TriggerStinger`, `GetScoreState`, `IsScorePlaying`,
`SetShowOverlay`. A project with one music system for the whole game calls these and places nothing.

**The maths half** is static, pure and world-free:

| Function | Answers |
|---|---|
| `EvaluateLayerGain` | How loud is this layer at this intensity? |
| `EvaluateLayerGains` | The gain of every layer in a score, in a section, under a cap. |
| `ApplyStingerDuck` | What does a duck of this depth do to a set of gains? |
| `TimeUntilBoundary` | How long until the next boundary of this kind? |
| `IsSectionTransitionAllowed` | May the score go from here to there, and why not? |
| `GetSecondsPerBar` | Arithmetic. |
| `GetLayerGain` | One layer's gain out of a state struct. |
| `ResolveQuantization` | `Project Default` turned into a real boundary. |

That half exists in this shape because of a lesson from an earlier plugin: a subsystem cannot be
`NewObject`'d in an automation test - it needs a world - so any logic that lives inside one is logic that
never gets a test. Here the maths takes a data asset and a float, so the tests exercise the same functions
the mixer calls on every tick.

It also means you can call them yourself. A HUD that wants to draw the mix it *would* have at intensity 0.9
can ask, without touching the running score.

---

## 13. Code examples

All of these are C++. Every one of them has a one-node Blueprint equivalent with the same name.

### Start a score and drive it from gameplay

```cpp
#include "PulseScoreStatics.h"

void AMyGameMode::BeginPlay()
{
    Super::BeginPlay();

    // MusicScore is a UPROPERTY(EditDefaultsOnly) TObjectPtr<UPulseScoreAsset>.
    UPulseScoreStatics::PlayScore(this, MusicScore, EPulseScoreQuantization::Immediate);
}

void AMyGameMode::OnCombatHeatChanged(float Heat01)
{
    // The whole game-facing API in one call. The score decides what 0.7 sounds like.
    UPulseScoreStatics::SetIntensity(this, Heat01);
}
```

### Change section on a bar line, and fire a stinger with it

```cpp
void AMyEncounter::StartFight()
{
    // Queued now, taken on the next bar line - Project Default is Bar.
    const bool bAccepted = UPulseScoreStatics::EnterSection(this, TEXT("Combat"));
    if (!bAccepted)
    {
        // The score's allow-list refused it, and LogPulseScore already said why.
        return;
    }

    UPulseScoreStatics::TriggerStinger(this, TEXT("Ambush"), EPulseScoreQuantization::Beat);
}

void AMyEncounter::PlayerDied()
{
    // The one case Immediate is right for: the hit has to land now, not on the bar.
    UPulseScoreStatics::TriggerStinger(this, TEXT("Death"), EPulseScoreQuantization::Immediate);
    UPulseScoreStatics::StopScore(this, EPulseScoreQuantization::Immediate);
}
```

### Read the state for your own HUD

```cpp
void AMyHUD::DrawMusicPanel()
{
    const FPulseScoreState State = UPulseScoreStatics::GetScoreState(this);
    if (!State.bPlaying)
    {
        return;
    }

    DrawText(FString::Printf(TEXT("%s  bar %d.%d  %s"),
        *State.ScoreName.ToString(), State.Bar, State.Beat, *State.CurrentSection.ToString()));

    if (!State.PendingSection.IsNone())
    {
        DrawText(FString::Printf(TEXT("-> %s in %.1fs"),
            *State.PendingSection.ToString(), State.SecondsUntilPendingSection));
    }

    for (const FPulseScoreLayerGain& Layer : State.LayerGains)
    {
        DrawBar(Layer.LayerId, Layer.Gain);   // one meter per stem
    }
}
```

### Subscribe to the beat

```cpp
void AMyActor::BeginPlay()
{
    Super::BeginPlay();

    if (UPulseScoreSubsystem* Pulse = UPulseScoreSubsystem::Get(this))
    {
        Pulse->OnBar.AddDynamic(this, &AMyActor::HandleBar);
        Pulse->OnSectionChanged.AddDynamic(this, &AMyActor::HandleSectionChanged);
    }
}

void AMyActor::HandleBar(int32 Bar)
{
    // Fires on the bar line, from the Quartz metronome. Flash something.
}

void AMyActor::HandleSectionChanged(FName OldSection, FName NewSection)
{
    // Fires when the change actually lands, not when it was requested.
}
```

### Put your own gameplay events on the music's clock

```cpp
if (const UPulseScoreSubsystem* Pulse = UPulseScoreSubsystem::Get(this))
{
    if (UQuartzClockHandle* Clock = Pulse->GetClockHandle())
    {
        // Your own quantised command, on exactly the clock the music runs on -
        // this is how a door opening lands on the beat too.
    }
}
```

### Ask the maths without touching the running score

```cpp
// "What would the mix look like at full intensity?" - no world, no clock, no side effects.
TArray<FPulseScoreLayerGain> Gains;
UPulseScoreStatics::EvaluateLayerGains(MusicScore, 1.0f, TEXT("Combat"), Gains);

// "How long until the next bar line?"
const float Wait = UPulseScoreStatics::TimeUntilBoundary(
    EPulseScoreQuantization::Bar, ClockSeconds, MusicScore->Tempo, MusicScore->BeatsPerBar);

// "May we go from here to there, and if not, why not?"
FString Reason;
const bool bAllowed = UPulseScoreStatics::IsSectionTransitionAllowed(
    MusicScore, TEXT("Victory"), TEXT("Combat"), Reason);
```

### Drive a MetaSound input on one stem

```cpp
if (UPulseScoreSubsystem* Pulse = UPulseScoreSubsystem::Get(this))
{
    // The layer's own voice takes the parameter; you never have to find the audio component.
    Pulse->SetLayerParameter(TEXT("Drums"), TEXT("FillDensity"), 0.8f);
}
```

### Build a score in code

Rarely what you want - a score is content - but it is what the automation tests do, and it shows the shape of
the data:

```cpp
UPulseScoreAsset* Score = NewObject<UPulseScoreAsset>(GetTransientPackage());
Score->Tempo = 120.0f;
Score->BeatsPerBar = 4;

FPulseScoreLayer Bed;
Bed.Id = TEXT("Bed");
Bed.Sound = MyPadLoop;
Bed.IntensityMin = 0.0f;      // open at the bottom: no lower ramp
Bed.IntensityMax = 1.0f;      // open at the top: no upper ramp
Score->Layers.Add(Bed);

FPulseScoreLayer Drums;
Drums.Id = TEXT("Drums");
Drums.Sound = MyDrumLoop;
Drums.IntensityMin = 0.45f;   // fades in across the lower edge
Drums.IntensityMax = 1.0f;
Score->Layers.Add(Drums);

FPulseScoreSection Explore;
Explore.Id = TEXT("Exploration");
Explore.AllowedNextSections = { TEXT("Combat") };
Score->Sections.Add(Explore);

Score->StartSection = TEXT("Exploration");
```

### Use the component instead

No code at all: add a **Pulse Score** component, set `Score`, set `IntensityDriver` to **Distance To Actor**,
set `NearDistance` and `FarDistance`. The music now gets more intense as the player approaches, and nothing
in the project calls PulseScore at all.

---

## 14. Project settings

**Project Settings -> Plugins -> PulseScore.** What belongs to a piece of music is on the score asset; what a
project decided once is here.

### Timing

| Setting | Default | Notes |
|---|---|---|
| `DefaultQuantization` | `Bar` | What every `Project Default` pin resolves to. |
| `ClockName` | `PulseScore` | Worth changing only if the project already runs a clock under this name. |
| `bMusicIgnoresPause` | on | Keeps the score running through a pause menu, and keeps bar and beat counting. |

### Mixing

| Setting | Default | Notes |
|---|---|---|
| `DefaultEdgeWidth` | 0.15 | Used by any layer that left its own at -1. |
| `DefaultFadeBars` | 1.0 | Fader smoothing constant, in bars. |
| `MaxConcurrentLayers` | 8 | Layers over the cap are held at zero, not stopped. |
| `MasterVolume` | 1.0 | A trim over the whole score. |
| `AudibleGainThreshold` | 0.01 | Where the counters and the cap draw the line. Not a mute. |

### Stingers

| Setting | Default | Notes |
|---|---|---|
| `StingerDuckAmount` | 0.6 | Roughly eight decibels. |
| `DuckAttackSeconds` | 0.05 | Short - the hole has to be open before the hit lands. |
| `DuckHoldSeconds` | 0.5 | |
| `DuckReleaseSeconds` | 0.8 | Long, because a mix that snaps back pumps. |

### Sections

| Setting | Default | Notes |
|---|---|---|
| `bStrictSectionTransitions` | on | Off downgrades a refusal to a warning and takes the transition. |

### Presentation

| Setting | Default | Notes |
|---|---|---|
| `bShowOverlayByDefault` | off | It is a development tool, not a HUD. |
| `bAutoDrawOverlayOnAnyHUD` | on | So a project keeps its own HUD class. |
| `OverlayOrigin` / `OverlayWidth` | 28,90 / 430 | Where the panel sits. |

---

## 15. The counters overlay and console commands

`PulseScore.Show 1` draws a panel with the score name and tempo, bar and beat (with a row of pips, because a
number that changes twice a second is unreadable on a screenshot and a row of dots is not), the current
section and any pending one with a countdown, an intensity meter, the duck depth, the audible-layer count
against the cap, and **a meter per layer**.

The per-layer meters are the reason the box exists. An adaptive music plugin with nothing on screen is one
you have to take on trust, and a still frame of it proves nothing at all.

It draws on `UCanvas` through `AHUD::OnHUDPostRender`, so it needs no HUD class of your own and it survives a
cooked Shipping build - a debug-only draw would have been compiled out of exactly the build where you most
want to check the mix.

| Command | Does |
|---|---|
| `PulseScore.Show [0\|1]` | Toggle the panel. No argument flips it. |
| `PulseScore.Stats` | Print the same numbers to the log. |
| `PulseScore.Intensity <0..1> [boundary]` | Set the intensity. No argument prints it. |
| `PulseScore.Section <Name> [boundary]` | Queue a section change. No argument prints the current one. |
| `PulseScore.Stinger <Name> [boundary]` | Fire a one-shot. |
| `PulseScore.Stop [boundary]` | Stop the score on a boundary. |

The optional boundary argument is one of `immediate`, `beat`, `bar`, `twobars`, `sectionend`. Anything else
is the project default.

---

## 16. The demo map

**`/PulseScore/PulseScore/Maps/L_PulseScoreDemo`**

A small arena with a player start, a director, a tower of stem indicators and an on-screen button strip.
Everything in it is built from the plugin's own Blueprint nodes - there is nothing in the demo that a project
could not write in an afternoon, which is the point of it.

| Asset | Does |
|---|---|
| `Maps/L_PulseScoreDemo` | The arena. Open it and press Play. |
| `Blueprints/BP_PulseScoreDemoGameMode` | Sets the demo HUD and pawn. Start here to see the wiring. |
| `Blueprints/BP_PulseScoreDemoDirector` | Holds the `UPulseScoreComponent`, plays the score and drives the intensity so the mix moves on its own. |
| `Blueprints/BP_PulseScoreDemoHUD` | Creates the panel widget and turns the counters overlay on. |
| `Blueprints/BP_PulseScoreStemTower` | One lit column per layer, scaled and coloured by that layer's live gain - the mix, in the world, without reading a number. |
| `UI/WBP_PulseScoreDemoPanel` | The button strip: four intensity presets, three sections, two stingers, play and stop. |
| `Scores/DA_PulseScore_DemoScore` | The demo score: five layers with overlapping windows, four sections with an allow-list, and a layer mask on Victory. |
| `Audio/S_PulseScore_*` | The five loops (`Bed`, `Calm`, `Pulse`, `Drums`, `Lead`) and two stingers (`Ambush`, `Victory`). |
| `Materials/`, `UI/M_PulseScoreBar` | The arena surfaces and the gain-bar material. |

**What to do in it.** Press Play. The counters box is on from the start, next to the button strip.

1. Walk the intensity up with the `10% / 40% / 70% / 100%` buttons and watch layers fade in across their
   windows - in the counters box, and as columns on the stem tower.
2. Press `Combat`. The counters box shows the section as **pending** with a countdown; the change lands on
   the next bar line, not when you clicked.
3. Press a stinger. The duck meter jumps, the marked layers drop, and they come back exactly where they
   were.
4. Press `Victory`. That section carries a layer mask, so stems the intensity would allow are muted anyway -
   and they are still playing, still in phase, which is why coming back out of Victory is seamless.

Every button on the strip is **one node** in `UPulseScoreStatics`. If a demo needs a helper Blueprint to make
a plugin usable, the plugin's API is wrong.

**On the music:** the loops and stingers that ship with the demo were generated for this plugin. There are no
third-party music files in this package - nothing in it that cannot be redistributed.

---

## 17. Tests

Automation tests under `PulseScore.*`, runnable from **Window -> Test Automation** or:

```
UnrealEditor-Cmd.exe <YourProject>.uproject -ExecCmds="Automation RunTests PulseScore" -unattended -nopause
```

| Test | Asserts |
|---|---|
| `PulseScore.Mix.LayerWindowGain` | The window is silent at its closed edges, full across its middle, exactly half way up the ramp at half way, and monotone on both edges. Open-ended windows have no ramp at the open end. |
| `PulseScore.Timing.BarBoundary` | A `Bar`-quantised switch asked for at forty different moments lands on a bar line every time, and the wait is never longer than one bar. Also `Beat`, `TwoBars`, `Immediate` and `SectionEnd`, including that `SectionEnd` repeats with the phrase and falls back to the bar when a section has no length. |
| `PulseScore.Sections.TransitionRules` | An unlisted transition is refused and the refusal names both sections. A section that does not exist is refused. Entry from nothing, re-entry and an empty allow-list are all permitted. |
| `PulseScore.Stingers.DuckAndRestore` | The envelope reaches half depth half way through the attack and full depth at the end of it; marked layers are pulled down and unmarked ones are untouched; a retrigger does not deepen the duck; and after the release the gains are *exactly* back. |
| `PulseScore.Mix.ConcurrencyCapAndSectionMask` | The cap keeps the high-priority layer over a louder one, and gain decides between equals. A section's layer list mutes what it does not name; an empty one lets everything through. |
| `PulseScore.Asset.Validation` | A score with no playable layer is unplayable; empty windows and transitions to sections that do not exist are reported; an unknown start section falls back to the first. |

None of them needs an audio device or a world, which is why they run in CI.

---

## 18. Performance and threading

**Per tick, per score:** one gain evaluation over the layer array (a handful of comparisons per layer, no
allocation - the buffer is reused), one fader update per voice, one `SetVolumeMultiplier` per voice, and one
transport read. There is no search, no trace and nothing that scales with anything but the layer count.

**Voices:** one audio component per layer for the life of the score, plus one per stinger in flight. Stinger
voices auto-destroy and are pruned each tick. This is the cost of holding silent layers in phase, and it is
the trade the design is built on.

**Threading:** everything in the plugin runs on the game thread. Quartz does the audio-render-thread work,
which is exactly why the plugin uses it rather than a timer. The dynamic delegates Quartz calls back on are
marshalled to the game thread by Quartz itself, so there is nothing here you need a lock for.

**Garbage collection:** the layer voice array holds plain data and a weak pointer; the strong references live
in a reflected array next to it. Nothing per-frame is allocated and nothing per-frame is reflected.

**Networking:** PulseScore is client-side. Music is a presentation concern; drive it from whatever gameplay
state is already replicated rather than replicating the conductor.

---

## 19. Troubleshooting

**Nothing plays.** Check the log for `LogPulseScore`. A score with no tempo, no beats per bar or no layer
with a sound refuses to play and says which. If `PlayScore` reports "produced no voices", there is no audio
device.

**A layer never comes in.** Its window is probably empty or backwards - `Validate Assets` reports both. Turn
the overlay on and watch its meter against the intensity meter.

**A layer at maximum intensity goes quiet.** Its `IntensityMax` is below 1.0, so it has a closed upper edge
and fades out over it. Set `IntensityMax` to 1.0 if it is meant to be the top layer.

**A section change does not happen.** It was almost certainly refused. The log says so, with the reason and
the list of what *was* allowed. If the target really should be reachable, add it to the source section's
`AllowedNextSections`.

**A section change happens late.** That is quantisation working. Check what boundary you asked for; the
project default is the next bar, and `SectionEnd` on an eight bar phrase can be seven bars away. The overlay
counts the wait down.

**The stems drift apart.** Something is stopping and restarting layers, which PulseScore never does - check
whether a second system is calling `PlayScore` repeatedly. Also check that every stem is genuinely written at
the score's tempo.

**The mix pumps.** `DuckReleaseSeconds` is too short, or stingers are firing more often than the envelope can
finish. The duck depth is on the overlay.

**Music keeps playing after a level ends.** The component's `bStopOnEndPlay` is off, or something other than
the component started the score. The subsystem is per-world, so it goes with the world - but a score started
in one world does not follow you to the next.

**The overlay does not appear.** `bAutoDrawOverlayOnAnyHUD` is off, or the level has no `AHUD` at all (a
custom game mode with `HUDClass = nullptr` has nothing to draw on). Set a HUD class, or call
`DrawOverlay` from your own `HUD` draw.

---

## 20. API reference

### `UPulseScoreSubsystem` (`UTickableWorldSubsystem`)

```cpp
static UPulseScoreSubsystem* Get(const UObject* WorldContextObject);

bool  PlayScore(UPulseScoreAsset* Score, EPulseScoreQuantization When = Immediate);
void  Stop(EPulseScoreQuantization When = Default);
void  StopImmediately();
bool  IsPlaying() const;
UPulseScoreAsset* GetScore() const;

void  SetIntensity(float Intensity, EPulseScoreQuantization When = Default);
float GetIntensity() const;
bool  EnterSection(FName SectionId, EPulseScoreQuantization When = Default);
FName GetCurrentSection() const;
FName GetPendingSection() const;
bool  TriggerStinger(FName StingerId, EPulseScoreQuantization When = Default);

bool  SetLayerParameter(FName LayerId, FName ParameterName, float Value);
int32 SetLayerParameterOnAll(FName ParameterName, float Value);

const FPulseScoreState& GetState() const;
UQuartzClockHandle*     GetClockHandle() const;

void  SetShowOverlay(bool bShow);
bool  IsShowingOverlay() const;
void  DrawOverlay(UCanvas* Canvas, const FVector2D& Origin, float Width) const;
void  LogState() const;
```

Multicast delegates: `OnBar(int32 Bar)`, `OnBeat(int32 Bar, int32 Beat)`,
`OnSectionChanged(FName Old, FName New)`, `OnStingerStarted(FName StingerId)`.

`GetClockHandle` is exposed so a project can hang its own quantised events off the same clock the music runs
on - which is how you get a gameplay event to land on the beat too.

### `UPulseScoreAsset` (`UPrimaryDataAsset`)

```cpp
float Tempo;                                   // BPM
int32 BeatsPerBar;
TArray<FPulseScoreLayer>   Layers;
TArray<FPulseScoreSection> Sections;
FName                      StartSection;
TMap<FName, TObjectPtr<USoundBase>> Stingers;

float GetSecondsPerBar() const;
float GetSecondsPerBeat() const;
int32 FindLayerIndex(FName LayerId) const;
int32 FindSectionIndex(FName SectionId) const;
FName GetEffectiveStartSection() const;
USoundBase* FindStinger(FName StingerId) const;
bool  Validate(TArray<FString>& OutProblems) const;
```

### `UPulseScoreStatics` (`UBlueprintFunctionLibrary`)

```cpp
// The conductor - all take a WorldContextObject.
static UPulseScoreSubsystem* GetPulseScore(const UObject* WorldContextObject);
static bool  PlayScore(const UObject*, UPulseScoreAsset* Score, EPulseScoreQuantization When = Immediate);
static void  StopScore(const UObject*, EPulseScoreQuantization When = Default);
static void  SetIntensity(const UObject*, float Intensity, EPulseScoreQuantization When = Default);
static float GetIntensity(const UObject*);
static bool  EnterSection(const UObject*, FName SectionId, EPulseScoreQuantization When = Default);
static FName GetCurrentSection(const UObject*);
static bool  TriggerStinger(const UObject*, FName StingerId, EPulseScoreQuantization When = Default);
static FPulseScoreState GetScoreState(const UObject*);
static bool  IsScorePlaying(const UObject*);
static void  SetShowOverlay(const UObject*, bool bShow);

// The maths - static, pure, world-free, unit-tested.
static float EvaluateLayerGain(const FPulseScoreLayer& Layer, float Intensity, float DefaultEdgeWidth = 0.15f);
static void  EvaluateLayerGains(const UPulseScoreAsset* Score, float Intensity, FName SectionId,
                                TArray<FPulseScoreLayerGain>& OutGains, int32 MaxConcurrentLayers = 0,
                                float DefaultEdgeWidth = 0.15f, float AudibleGainThreshold = 0.01f);
static void  ApplyStingerDuck(TArray<FPulseScoreLayerGain>& Gains, float DuckAmount);
static float GetSecondsPerBar(float Tempo, int32 BeatsPerBar);
static float TimeUntilBoundary(EPulseScoreQuantization When, float ClockSeconds, float Tempo,
                               int32 BeatsPerBar, float SectionStartSeconds = 0.0f, int32 SectionLengthBars = 0);
static bool  IsSectionTransitionAllowed(const UPulseScoreAsset* Score, FName FromSection, FName ToSection,
                                        FString& OutReason);
static float GetLayerGain(const FPulseScoreState& State, FName LayerId);
static EPulseScoreQuantization ResolveQuantization(EPulseScoreQuantization When);
```

### `UPulseScoreComponent` (`UActorComponent`)

```cpp
// Setup
TObjectPtr<UPulseScoreAsset> Score;
bool  bPlayOnBeginPlay;      // default on
FName StartSection;
bool  bStopOnEndPlay;        // default on
EPulseScoreQuantization Quantization;

// Intensity driver: Manual | Curve | DistanceToActor
EPulseScoreIntensityDriver IntensityDriver;
TObjectPtr<UCurveFloat> IntensityCurve;
bool  bLoopCurve;  float LoopSeconds;
TObjectPtr<AActor> DistanceTarget;
float NearDistance;  float FarDistance;
float IntensitySmoothingSeconds;

// API
bool  PlayScore();
void  StopScore();
void  SetIntensity(float Intensity);
bool  EnterSection(FName SectionId);
bool  TriggerStinger(FName StingerId);
bool  IsDirectingScore() const;
float GetDrivenIntensity() const;
UPulseScoreSubsystem* GetPulseScore() const;
```

### `FPulseScoreLayer`

`Id`, `Sound`, `IntensityMin`, `IntensityMax`, `EdgeWidth` (-1 = project default), `FadeBars` (-1 = project
default), `VolumeScale`, `bDuckedByStinger`, `Priority`.

### `FPulseScoreSection`

`Id`, `AllowedNextSections`, `LayerIds`, `TransitionStinger`, `LengthInBars`, `EntryIntensity` (-1 = leave
the intensity alone).

### `FPulseScoreLayerGain`

`LayerId`, `Gain`, `bDuckedByStinger`, `bAllowedBySection`.

### `FPulseScoreState`

`bPlaying`, `ScoreName`, `CurrentSection`, `PendingSection`, `SecondsUntilPendingSection`, `Intensity`,
`PendingIntensity`, `Tempo`, `BeatsPerBar`, `Bar`, `Beat`, `BeatFraction`, `SecondsElapsed`,
`BarsIntoSection`, `DuckAmount`, `LayerGains`, `AudibleLayers`.

### `EPulseScoreQuantization`

`Default` (project default), `Immediate`, `Beat`, `Bar`, `TwoBars`, `SectionEnd`.

### `EPulseScoreIntensityDriver`

`Manual`, `Curve`, `DistanceToActor`.

---

Support: teufelsilvan@gmail.com

Copyright 2026 Silvan Teufel. All Rights Reserved.
