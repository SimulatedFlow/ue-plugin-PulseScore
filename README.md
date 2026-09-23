# PulseScore - Adaptive Music Director

**Layered adaptive music on Quartz: intensity-driven stem mixing, bar-quantised transitions, stingers and
automatic ducking - without a single Blueprint timer.**

Unreal Engine 5.8 - Win64 - one runtime module, no editor module, no third-party code.

---

## What it is

Quartz gives you a sample-accurate clock. MetaSounds gives you stems. Neither of them gives you a
**conductor** - the layer that turns "combat started" into a drum track fading in on the next bar line.

That layer is this plugin. The game reports a state:

```cpp
UPulseScoreStatics::SetIntensity(this, 0.7f);
UPulseScoreStatics::EnterSection(this, TEXT("Combat"));
UPulseScoreStatics::TriggerStinger(this, TEXT("Ambush"));
```

PulseScore decides which stems play at what volume, and switches **on the beat** rather than in the middle
of one.

## What it is not

**PulseScore does not compose and it does not synthesise.** It plays and mixes the `USoundBase` assets you
already have - MetaSounds, Sound Cues, looping waves - and every note in it is yours. It is the conductor,
not the orchestra.

---

## The three ideas

### 1. Nothing switches mid-bar

Every call carries an `EPulseScoreQuantization`: `Immediate`, `Beat`, `Bar`, `TwoBars` or `SectionEnd`, with
`Bar` as the project default. Section changes, stops and stingers are scheduled against a Quartz clock, so
they land on the boundary rather than wherever the calling frame fell. A `SetTimer` gets you to within a
frame. Quartz gets you to within a sample, because it schedules on the audio render thread.

### 2. Intensity mixes, it does not switch

Every layer owns an intensity window - `IntensityMin`, `IntensityMax` and a soft edge. Outside its window a
layer **keeps playing at zero**. It is never stopped.

That is the whole trick of layered music. A stopped layer has to be restarted, and a restarted layer comes
in one start-latency behind everything else. A layer held at zero comes back exactly on the beat, because it
never left it.

### 3. A refusal is louder than a silent failure

A section change the score's allow-list does not permit is refused and logged with its reason. A missing
stinger says so. A score with a layer that has no sound says so at the moment it is asked to play.

---

## Quick start

1. Create a **Pulse Score** data asset. Set `Tempo` and `BeatsPerBar` to match your stems.
2. Add a layer per stem. Give each one an `Id`, a looping `Sound`, and an intensity window.
3. Add sections if the piece has named parts, and list what each may lead to.
4. Either drop a **Pulse Score** component on an actor and point it at the asset, or call
   `PulseScore -> Play Score` from anywhere.
5. Call `Set Intensity` from your gameplay code and listen.

Turn the counters overlay on with `PulseScore.Show 1` to see bar, beat, section, intensity, the duck and a
meter per layer.

---

## What ships

| | |
|---|---|
| `UPulseScoreSubsystem` | The conductor. One per world, one Quartz clock, one voice per layer. |
| `UPulseScoreAsset` | The piece: tempo, layers, sections, stingers. |
| `UPulseScoreComponent` | The designer-facing facade, with curve and distance intensity drivers. |
| `UPulseScoreStatics` | Every call as a static Blueprint node, plus the world-free maths. |
| `UPulseScoreSettings` | Project defaults under Project Settings -> Plugins -> PulseScore. |

Console commands: `PulseScore.Show`, `PulseScore.Stats`, `PulseScore.Intensity`, `PulseScore.Section`,
`PulseScore.Stinger`, `PulseScore.Stop`.

---

## Documentation

Full documentation, including the demo map walkthrough, is in [`Docs/DOCUMENTATION.md`](Docs/DOCUMENTATION.md)
and at <https://wiki.teufel-engineering.com/en/PulseScore/documentation>.

## Support

teufelsilvan@gmail.com

Copyright 2026 Silvan Teufel. All Rights Reserved.

<!-- SF-STORE-BLOCK:BEGIN -->
## 🛒 Source-available — see before you buy

This repository contains the **full source** of a commercial Unreal Engine plugin. It is **source-available, not open source**: read it, evaluate it, then buy a license to use it. See **the Fab Content License Agreement / Unreal Engine EULA (purchase required)**.

**Get it / Buy:**
- **Buy on Fab** (this plugin): https://www.fab.com/listings/b49acca9-985b-4d1d-9de1-9a2b9b1cebee
- Fab store — all our UE5 plugins: https://www.fab.com/sellers/Silvan%20Teufel

### 📬 **Free UE5 Snippet-Pack**

10 ready-to-use C++/Blueprint building blocks (subsystems, versioned saves, async nodes, editor tooling) — MIT licensed. Get it by joining the newsletter — plus a heads-up when something new ships. Double opt-in, unsubscribe in one click, no address sharing.

👉 **[Get the free pack](https://silvan.teufel-engineering.com/newsletter/plugins/?q=gh)**

_© 2026 Silvan Teufel. All rights reserved._
<!-- SF-STORE-BLOCK:END -->
