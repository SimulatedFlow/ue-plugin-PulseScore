# PulseScore - Adaptive Music Director

## Short description

Layered adaptive music on Quartz: intensity-driven stem mixing, bar-quantised transitions, stingers and
automatic ducking - without a single Blueprint timer.

---

## Long description

Quartz gives you a sample-accurate clock. MetaSounds gives you stems. Neither of them gives you a
**conductor** - the layer that turns "the fight started" into a drum track fading in on the next bar line,
and that knows the fight cannot go straight to the victory theme.

PulseScore is that layer. Your game reports a state - `SetIntensity(0.7)`, `EnterSection("Combat")`,
`TriggerStinger("Ambush")` - and PulseScore decides which stems play at what volume and switches **on the
beat** instead of in the middle of one.

### Nothing switches mid-bar

Every call carries a boundary: **Immediate, Beat, Bar, TwoBars** or **SectionEnd**, with Bar as the project
default. Section changes, stops and stingers are scheduled against a Quartz clock, so they land on that
boundary rather than wherever the calling frame fell. A `SetTimer` gets you to within a frame. Quartz gets
you to within a sample, because it schedules on the audio render thread.

### Intensity mixes, it does not switch

Every layer owns an intensity window with a soft edge. A layer outside its window **keeps playing at zero -
it is never stopped**. That is the whole trick of layered music: a stopped stem has to be restarted, and a
restarted stem comes in one start-latency behind the others. A stem held at zero comes back exactly on the
beat, because it never left it.

A window that reaches the top of the intensity range has no fade-out at the top, so the layer written for
maximum intensity is at full when the fight is at its worst - not fading out of it.

### Stingers duck, and put the mix back exactly

A one-shot fired by name ducks the layers you marked for it through an attack-hold-release envelope. The duck
starts when the sound actually starts - not when you called it - so a stinger queued three beats out does not
flatten the mix for three beats first. It is multiplicative and stateless, so releasing it restores the gains
exactly, even if the intensity moved while it was sounding. Two stingers a beat apart do not stack.

### Sections have a shape, and the shape is enforced

Each section lists the sections it may lead to. A change the list does not permit is **refused and logged
with its reason**, never quietly taken - because a transition that silently did not happen is the hardest
kind of audio bug to find. An empty list is unconstrained, not a dead end. A newer request replaces an older
pending one rather than queueing behind it.

### Numbers on screen

A Canvas overlay that survives a cooked Shipping build prints bar and beat, the current and pending section
with a countdown to the switch, tempo, an intensity meter, the duck depth, the audible-layer count against
the cap - and **a meter per layer**. An adaptive music plugin with nothing on screen is one you have to take
on trust.

Console commands: `PulseScore.Show`, `PulseScore.Stats`, `PulseScore.Intensity`, `PulseScore.Section`,
`PulseScore.Stinger`, `PulseScore.Stop`.

### The maths is testable, and tested

Which layer is how loud at intensity x, when the next boundary falls, whether a transition is legal, what a
duck does to a set of gains - all of it lives in **static Blueprint library functions with no world and no
subsystem behind them**. That is why it can be unit-tested, and the tests exercise the same code the mixer
runs on every tick rather than a re-implementation of it. Six automation tests ship with the plugin.

It also means you can call them yourself: a HUD that wants to draw the mix it *would* have at intensity 0.9
can ask, without touching the running score.

---

## What this is not

**PulseScore does not compose and it does not synthesise.** It plays and mixes the `USoundBase` assets you
already have - MetaSounds, Sound Cues, looping waves. Every note that comes out of it is yours. It is the
conductor, not the orchestra.

There is no time-stretching: your stems must be written at the score's tempo, and the plugin says so rather
than quietly stretching them badly.

---

## What ships

* `UPulseScoreSubsystem` - the conductor. One per world, one Quartz clock, one voice per layer.
* `UPulseScoreAsset` - the piece: tempo, layers, sections, stingers. A data asset, so a composer can be
  handed one.
* `UPulseScoreComponent` - the designer-facing facade, with curve-over-time and distance-to-actor intensity
  drivers built in.
* `UPulseScoreStatics` - every call as a static Blueprint node, plus the world-free maths.
* `UPulseScoreSettings` - project defaults under Project Settings.
* A demo map with a zone ring, a sine director, a button strip and the counters box - every button one node.
* Full documentation with a troubleshooting section.

**No UMG in the product. No Niagara. No Chaos. No editor module. No third-party code.** One runtime module.
Built and verified on Win64.

---

## Technical details

* **Engine:** Unreal Engine 5.8
* **Platforms:** Win64 (built and verified)
* **Modules:** 1 runtime (`PulseScore`, LoadingPhase PreDefault)
* **Dependencies:** Core, CoreUObject, Engine, AudioMixer, SignalProcessing, DeveloperSettings,
  AudioExtensions, RenderCore - all engine modules
* **Network replicated:** no (music is not gameplay)
* **Supported build targets:** Development, Shipping, and everything between
* **Blueprint:** everything is Blueprint-callable; nothing requires C++

## Demo content

Four short stems generated for this plugin. **No third-party music files** - nothing in this package that
cannot be redistributed.

---

Support: teufelsilvan@gmail.com
Documentation: https://wiki.teufel-engineering.com/en/PulseScore/documentation
