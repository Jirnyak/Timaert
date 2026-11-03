// SfxId — словарь ЗВУКОВ игры. ПЕРЕЕХАЛ из macro/audio.h ломтём 1б эпика
// пакетов v1: субмир очередит звук именем ряда (`queue_sfx@src/sub/engine.h`),
// а сама система звука (AudioSystem, SDL_mixer, MusicId) осталась в
// macro/audio.h — там состояние, здесь только ИМЕНА. Тот же шов, которым
// FeatureType уехал в tables/feature_type.h, а слой остался в macro/features.h
// (M-181, граница ТИП против ЭКЗЕМПЛЯРА).
#pragma once
#include <cstdint>

namespace sm {

// The first LIVE rows since the 2026-08-29 canon audit emptied this enum
// (Witch was loaded-but-never-played dead content): the melee feedback trio
// (owner 2026-09-06 — «не чувствуется сражение», the silent-swing session).
// Every row is played by a real call site or it does not get a row.
//
// THE ASSET LAW, sound edition (the universal-sprite-resolver rule applied
// to audio): a missing file is NOT silence. Every row has a PROCEDURAL
// DEFAULT synthesized at init (audio.cpp synth_sfx_chunk), and a real file
// in assets/sound with the row's name simply overrides it. Adding a sound is
// one enum value + one kSfxAssets row; shipping it is optionally one file.
enum class SfxId : std::uint8_t {
    MeleeSwing = 0,   // the arc itself — heard on EVERY swing, hit or air
    MeleeHit,         // the blow bit flesh
    MeleeBlocked,     // armour swallowed the blow whole (damage door blocked)
    // The 2026-09-09 trio (owner queue, NEXT_SESSION §5): appended — the
    // asset table mirrors this order and a .wav simply overrides the synth.
    SpellCast,        // a cast leaving the hand (any cast — one door, one voice)
    PlayerHurt,       // the blow that found the INHABITED body (hp dropped)
    Death,            // a body going down in the scene (never the player's own)
    Count,
};

} // namespace sm
