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
#include "macro/player_entity.h"
#include "macro/state.h"
#include "bus/store.h"

namespace sm {

inline void player_journal_capture(GameState& gs, const Session& ses,
                                   const MacroStore& st) {
    const Chronicle& c = gs.chronicle;
    if (!c.ready() || gs.mapW <= 0 || gs.mapH <= 0) return;
    PlayerState& p = gs.player;
    // WHERE he stands = the flag holder's cell (подпосадка 4) — locality is
    // asked of the world, like possession above.
    const ecs::MacroCell* pcell = player_flag_cell(ses, st);
    const int px = pcell ? ecs::cell_x(*pcell, gs.mapW) : -1;
    const int py = pcell ? ecs::cell_y(*pcell, gs.mapW) : -1;
    // Participation is by the ordinal his deeds FILE UNDER — and while he wears
    // a possessed lord that is the LORD's ordinal, wherever the deed happened:
    // «узнаётся УЧАСТИЕ, где бы ни случилось». A capture keyed to his own squad
    // alone learned a possessed reign only by standing on its cell.
    //
    // «Носит ли он кого-то» is asked through THE door (player_entity.h). It
    // used to be asked right here, by scanning the flag and comparing its spawn
    // ordinal to the reserved magic number — a second, hand-written answer to a
    // question the flag already answers, and one that would have drifted the
    // day «кто оригинал» stopped being spelled as that constant.
    // «ЧЬИ ЭТО ДЕЛА» — ОДИН ОРДИНАЛ, АКТИВНОГО (ломоть 7б M-421). Здесь
    // стояли ДВА ответа: ординал вселённого лорда (через
    // `player_wears_another_body`) И ординал дома, — и сверка шла с обоими.
    // Рода «дом» больше нет, а «вся семья» (2026-09-14) говорит, что дела
    // подшиваются под того, кем ты есть; два ответа свелись в один.
    // Распаковка кэша ОДИН раз перед циклом, ноль сканов на факт.
    const std::uint32_t meOrd = player_flag_ordinal(ses, st);
    const auto isHis = [meOrd](std::uint8_t kind, std::uint32_t ordinal) {
        if (fact_subject_kind(kind) != std::uint8_t(FactSubject::Squad))
            return false;
        // 0 — «никто» (закон нуля-ординала): безымянный субъект не его.
        return meOrd != 0 && ordinal == meOrd;
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
        if (p.journalCount >= PlayerState::kJournalFactsCap) {
            p.journalFull = 1;   // loud, never a silent drop of his past
            break;
        }
        WorldFact learned = f;
        // The copy is of the immutable RECORD; the chain link belongs to the
        // ring's index, not to the fact — save.cpp write_fact refuses to
        // store the derived link for the same reason (a stored derivative is
        // a second truth). A live link here rode into the save as garbage.
        learned.nextInCell = 0u;
        p.journal[p.journalCount] = learned;
        ++p.journalCount;
    }
    p.journalSeenSeq = c.nextSeq - 1u;
}

} // namespace sm
