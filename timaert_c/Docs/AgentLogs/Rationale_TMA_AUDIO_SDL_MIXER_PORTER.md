# Rationale: TMA_AUDIO_SDL_MIXER_PORTER

Date: 2026-05-15

## SDL_mixer Native Backend

Problem: TypeScript browser audio semantics needed a native C++ replacement without silent no-audio success on desktop.
Solution: Use SDL2_mixer for native decode/playback, expose a small `AudioSystem`, and make native CMake fail fast when SDL2_mixer is unavailable.
Rejected Alternatives: A stub backend on native was rejected because it reports false parity. Ad hoc per-frame loading was rejected because it would cause I/O churn and noisy failure loops.
Scalability potential: Low keeps one loaded music stream and one-shot SFX; Middle/High/Ultra can add richer routing later without changing stable IDs.
Hardware Impact: On i3/MX350-class hardware, preloaded handles avoid repeated disk/decode spikes during frame updates.

## Asset Lookup

Problem: Running from repo root and from `build-msvc` used different working directories, so relative sound paths could fail.
Solution: Search executable-base `assets/sound` first, then cwd-relative asset roots.
Rejected Alternatives: Hard-coded absolute paths were rejected because they break portable builds and CI.
Scalability potential: Low uses the same shipped MP3s; High/Ultra can add higher bitrate variants behind the same registry later.
Hardware Impact: Avoids repeated failed lookup attempts; expected frame impact is 0 us after init.

## Stable Registry Contract

Problem: Music/SFX enums index fixed registries; future enum drift could silently desync metadata, filenames, and tests.
Solution: Add compile-time count guards and contract tests for stable IDs, filenames, and minimum asset sizes.
Rejected Alternatives: String-only playback calls were rejected because they weaken compile-time coverage and make gameplay call sites typo-prone.
Scalability potential: New tracks can be added by extending enum, registry, and tests in one forced path.
Hardware Impact: Runtime cost is 0 us for compile-time guards and test-only asset size checks.

## RAII Ownership

Problem: SDL_mixer chunk/music handles cannot be copied safely.
Solution: Delete `AudioSystem` copy/move construction and assignment; destructor calls `shutdown()`.
Rejected Alternatives: Shared ownership was rejected because SDL_mixer global state and raw handles make lifetime ambiguity dangerous.
Scalability potential: Keeps backend swappable while preserving deterministic shutdown.
Hardware Impact: Prevents leaks and double-free risk; no frame cost.

## App Music Sync

Problem: Failed `play_music` calls could be retried every frame, while stopped valid tracks needed recovery.
Solution: Track desired, current, playing, and failed music IDs; retry only when desired changes or a valid current track stops.
Rejected Alternatives: Blind per-frame playback was rejected because it would spam logs and burn CPU on missing assets.
Scalability potential: Low devices avoid failure spam; top-tier devices can later add richer transitions using the same desired-state hook.
Hardware Impact: Saves repeated branch/I/O/log work on failure paths; normal frame cost remains effectively 0 us.

## Runtime Proof

Problem: Metadata-only tests could pass while SDL_mixer decode/playback was broken.
Solution: Add `audio_runtime_test` using SDL dummy audio driver to init, decode, play, stop, shutdown, and prove destructor cleanup.
Rejected Alternatives: Manual title-screen smoke alone was rejected because it is weaker and harder to isolate.
Scalability potential: Gives a stable contract before adding more SFX/music.
Hardware Impact: Test-only cost; runtime path unchanged.

## Hecton Import Placement

Problem: User required all Timaert/Samosbor docs/tasks/logs from Hecton transferred, but exact Timaert/Samosbor labels were absent in Hecton files.
Solution: Keep exact-match lane in `Docs/Imported/Hecton`, and preserve the full current Hecton documentation corpus under `Docs/Imported/Hecton8/2026-05-15_docs_tasks_logs`.
Rejected Alternatives: Merging Hecton logs into active Timaert `Docs/Tasks` or `Docs/AgentLogs` was rejected because those folders are live Timaert state.
Scalability potential: The import folder can absorb later deltas without polluting active project ledgers.
Hardware Impact: Documentation-only action; 0 us runtime impact.

## Native Dependency Gate Recheck

Problem: The native CMake SDL2_mixer branch drifted back to a status-only fallback, which could produce a desktop build with fake audio parity.
Solution: Restore `message(FATAL_ERROR ...)` when SDL2_mixer is unavailable on native builds.
Rejected Alternatives: Keeping the no-mixer fallback for native was rejected because it hides missing runtime audio during integration.
Scalability potential: Low-end and high-end native builds now share the same real audio dependency contract.
Hardware Impact: Configure-time only; 0 us runtime impact.

## SFX Shutdown Lifetime

Problem: `Mix_FreeChunk` can run while one-shot SFX channels are still active during shutdown/destruction.
Solution: Halt all SFX channels with `Mix_HaltChannel(-1)` before halting music and freeing chunks.
Rejected Alternatives: Trusting SDL_mixer to tolerate active chunk free was rejected because it creates backend-specific lifetime risk.
Scalability potential: Safe shutdown remains deterministic as more SFX are added.
Hardware Impact: Shutdown-only cost; 0 us steady-frame impact.

## No-Hecton-Write Boundary

Problem: User explicitly forbade writing Timaert docs into Hecton while still requiring all Timaert/Samosbor-relevant Hecton docs/tasks/logs to be transferred.
Solution: Treat `C:\hades\Hecton8` as read-only source for this lane, copy only into `C:\Timaert\timaert_c\Docs\Imported`, and record provenance in Timaert manifests.
Rejected Alternatives: Writing status, cleanup notes, or Timaert import ledgers back into Hecton was rejected because it contaminates the source project and violates the direction of transfer.
Scalability potential: Future imports can repeat the same one-way copy without mixing active project ledgers.
Hardware Impact: Documentation-only boundary; 0 us runtime impact.
