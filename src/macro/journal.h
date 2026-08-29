// The player's journal CAPTURE — the reader that turns the world's chronicle
// into the player's knowledge (CANON S20.1 + owner rulings 2026-08-28).
//
// S11 applied to history: the world does not TELL what the player never
// learned. The chronicle stays one and whole (legends mode reads it raw);
// what the player's journal shows is the slice he LEARNED — and this file is
// the one place that decides what "learned" means:
//   · he TOOK PART — the fact names his squad ordinal as subject or object;
//   · it happened HERE — on the macro cell he is standing on the tick it was
//     filed (the subworld files into that same cell, so everything he sees
//     below is his by construction).
// Rumours will come through this same door later: a rumour is a chronicle
// record read to him, and hearing it appends it here like witnessing does.
//
// Deliberately a READER, not a writer hook: chronicle_record stays pure and
// none of the fact writers knows the journal exists. Once per tick the player
// asks "what happened since seq N" — the chronicle is globally ordered by
// seq, so the scan touches only the tick's few new records (usually none).
#pragma once

#include "core/torus.h"
#include "ecs/components.h"
#include "macro/state.h"

namespace sm {

inline void player_journal_capture(GameState& gs) {
    const Chronicle& c = gs.chronicle;
    if (!c.ready() || gs.mapW <= 0 || gs.mapH <= 0) return;
    PlayerState& p = gs.player;
    // DOD discipline (owner): the WHOLE cap is reserved once, here at the
    // first capture of a session — 32 MB flat POD by the house's own "size
    // is no argument" — so the game loop never allocates for the journal
    // again and the array stays one contiguous block for the save to write
    // byte-in-byte. (Not in a constructor: a fixture GameState that never
    // captures pays nothing.)
    if (p.journal.capacity() < std::size_t(PlayerState::kJournalFactsCap)) {
        p.journal.reserve(std::size_t(PlayerState::kJournalFactsCap));
    }
    const int px = wrapi(int(p.x), gs.mapW);
    const int py = wrapi(int(p.y), gs.mapH);
    // Participation is by the ordinal his deeds FILE UNDER — and while he
    // wears a possessed lord (possession.md, PlayerState::possessedMacroSpawnId)
    // that is the LORD's ordinal, wherever the deed happened: «узнаётся
    // УЧАСТИЕ, где бы ни случилось». A capture keyed to his own squad alone
    // learned a possessed reign only by standing on its cell.
    const int possessed = p.possessedMacroSpawnId;
    const auto isHis = [possessed](std::uint8_t kind, std::uint32_t ordinal) {
        if (fact_subject_kind(kind) != std::uint8_t(FactSubject::Squad))
            return false;
        return ordinal == ecs::kPlayerSquadOrdinal
            || (possessed >= 0 && ordinal == std::uint32_t(possessed));
    };
    for (std::uint32_t s = p.journalSeenSeq + 1u; s < c.nextSeq; ++s) {
        const WorldFact& f = c.ring[std::size_t((s - 1u) % kChronicleFacts)];
        // A slot that no longer holds its seq was evicted before this reader
        // ever ran (a scan that fell a whole ring behind) — nothing to learn.
        if (f.seq != s) continue;
        const bool tookPart = isHis(f.subjectKind, f.subject)
                           || isHis(f.objectKind, f.object);
        const bool happenedHere = int(f.x) == px && int(f.y) == py;
        if (!tookPart && !happenedHere) continue;
        if (p.journal.size() >= std::size_t(PlayerState::kJournalFactsCap)) {
            p.journalFull = 1;   // loud, never a silent drop of his past
            break;
        }
        WorldFact learned = f;
        // The copy is of the immutable RECORD; the chain link belongs to the
        // ring's index, not to the fact — save.cpp write_fact refuses to
        // store the derived link for the same reason (a stored derivative is
        // a second truth). A live link here rode into the save as garbage.
        learned.nextInCell = 0u;
        p.journal.push_back(learned);
    }
    p.journalSeenSeq = c.nextSeq - 1u;
}

} // namespace sm
