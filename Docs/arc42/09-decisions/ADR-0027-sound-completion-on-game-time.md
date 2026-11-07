# ADR-0027: Sound completion follows game time, not the audio device

- **Status:** Accepted
- **Date:** 2026-11-07

## Context

The sound layer frees a channel once the driver reports that its buffer stopped playing (`Sound.c`, the
channel sweep that calls `SoundDriver_GetStatusAndCaps`). That state reaches the simulation: a COG thread in
`SITHCOG_STATUS_WAITING_SOUND_TO_STOP` wakes only when its channel stops (`sithCog.c`). In retail, the
DirectSound device decided, so script timing followed the audio hardware. The Null driver reports every sound
as finished at once, and the v4 reference traces were recorded that way. M7 adds a real device (miniaudio,
ADR-0010); if the device decided, windowed play would stop being deterministic, replays (ADR-0018) would
diverge, and a run with sound would differ from one without.

## Decision

Whether a channel is still playing is computed by the sound layer from the sound's length, its pitch, its
loop flag and the elapsed game time, the same way for every backend. The audio device only renders what the
sound layer says is playing; its own play position never reaches the simulation. The Null driver follows the
same rule, so sounds no longer finish instantly.

## Consequences

- Windowed play, headless runs and replays agree, with and without an audio device.
- The Null traces change once. The owner agreed (2026-11-07) to record new reference traces (v5) for this
  change; the change and the new references land together, with the trace difference explained.
- Close to retail at normal speed; behavior that depended on the device's timing (slow devices, focus loss)
  isn't reproduced.
- A device that falls behind or loses sync is a presentation problem only.

## Amendment (2026-11-07)

"Game time" means the sound layer's own clock: the platform clock (`SoundDriver_GetTimeMsec`, which reads
`stdPlatform_GetTimeMsec`) minus the time the sound layer was paused. `sound` sits below `sith`, so it can't
read `sithTime`. Headless runs and replays drive that clock (ADR-0018), so it is as deterministic as the
simulation's own frame time, and it keeps running on load screens, where the simulation is stopped but music
plays. Every backend, including the real device, must answer `SoundDriver_GetTimeMsec` from the platform
clock, never from the device's play position.
