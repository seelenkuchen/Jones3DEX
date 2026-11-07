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
