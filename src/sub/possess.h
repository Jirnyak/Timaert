// ── ВСЕЛЕНИЕ: ЭТО ПЕРЕНОС ФЛАЖКА, И БОЛЬШЕ НИЧЕГО ───────────────────────
//
// Owner's ruling, 2026-09-12: «одержимость — это не более чем перенос флажка
// (эффект для будущих спеллов)». It is not a system, it has no ceremony, and
// the machinery that had grown around it — an exit-remap query, an identity
// adoption door, a struct to carry the answer between them, and a design doc —
// was cut on that word. Since 2026-09-17 the future spell EXISTS — the
// `possession` row (macro/spells.h) and its effect (content/spells/effects.cpp)
// are the only caller besides the harness — and the hardcoded take doors
// (possess_aim, the console command) died with it.
//
// The whole of it follows from the model the project already has: the player is
// an ordinary body carrying a flag (CANON S4, «игрок == НПЦ»), so moving the
// flag to another body is moving the player. Every consumer — camera, input,
// incoming combat, the minimap — follows the flag by construction, and since
// the mirror law (sub/record.h) the body's bars, bag and gear are its RECORD's,
// so an inhabited lord fights as himself without anybody arranging it.
//
// Header-only on purpose: the door is a handful of registry moves with no
// dependency beyond record.h, and its callers (the spell effect TU, the
// engine, the harness) must not have to link the whole spawn layer to move a
// flag.
#pragma once

#include "ecs/world.h"
#include "sub/record.h"

namespace sm::sub {

// current_player_body: тело, на которое смотрит ссылка сцены «активное
// тело» (вердикт 2026-10-05 — активность есть ССЫЛКА, не свойство тела);
// entt::null = ссылки нет (между сценами).
inline entt::entity current_player_body(ecs::World& w) {
    return avatar_entity(w.reg);
}

// Move the player flag onto `target` (must be a live, positioned scene body).
// «Одержимость — не более чем перенос флажка» (owner 2026-09-12) — flags move
// in one movement, so there is no span in which the scene says one man and
// the map says another, and every `player_*` door answers about the man you
// actually are.
//
// Removes AvatarTag from the current body; if that body was the hero husk (no
// NPCKind) it is destroyed — the husk is a projection of his macro record, so
// nothing is lost and no inert, un-rendered, un-AI'd zombie is stranded in the
// scene. A vacated FOREIGN body keeps all its components and, with the flag
// gone, its AI / rendering / targetability resume automatically (every such
// path is AvatarTag-gated).
//
// No-op returning false if target is null / invalid / unpositioned / already
// the player. Which flags move is record.h's two births read out loud (закон
// шва, owner verdict 2026-09-17): a body with a RECORD carries both flags —
// you remain him across the seam; a DERIVED body (macro_record_of answers with no
// body itself) carries only the scene flag — the macro flag stays home, so
// leaving the scene IS the reset, written nowhere. Pure ECS: the caller
// re-mirrors the position scalars from the new body afterwards (the engine
// tick does it via pull_player_entity_to_scalars).
//
// `playerFlagBits` — адрес ОДНОГО числа `GameState::playerFlagBits` (1е
// кластер 5): КЭШ макро-флажка; истина — колонка playerFlag анкеты (5б),
// оба носителя пишет одна дверь переноса (transfer_player_flag). Субмиру
// не даётся GameState — только это число (закон двух миров: канал узкий и
// названный); звонящий без мира (фикстура) законно передаёт nullptr —
// тогда двигается только сценический флаг (AvatarTag), макро-мира у
// фикстуры нет.
inline bool possess_entity(ecs::World& w, entt::entity target,
                           std::uint32_t* playerFlagBits) {
    auto& reg = w.reg;
    if (target == entt::null || !reg.valid(target)) return false;
    if (!reg.all_of<ecs::Position>(target)) return false; // must be a real body
    const entt::entity cur = current_player_body(w);
    if (cur == target) return false;                      // already inhabiting it

    // ЗАКОН ШВА (вердикт владельца 2026-09-17) — record.h's two honest births
    // read out loud, each getting exactly the possession it can carry:
    //   · a PROJECTION answers `macro_record_of` with the store slot it backlinks
    //     — BOTH flags move, and leaving the scene you simply REMAIN him
    //     («одержим лордом… который контекстно анкета макромира — остаётся
    //     им»);
    //   · a DERIVED body — a wolf, a citizen rolled from a cell seed, a
    //     console spawn — answers with ITSELF: nothing above remembers it and
    //     it dies with the scene, so the macro flag has nowhere to land and
    //     STAYS HOME. The scene flag alone rides it, and the exit reset
    //     («одержим генерик — при выходе сброс») is not written anywhere: the
    //     body dies with the scene while your macro flag never left you.
    // Запись — хэндлом: с 1е кластера 5 макро-флаг ЕСТЬ биты GameState,
    // entt-носитель записи флажку больше не нужен вовсе.
    const MacroHandle recH = macro_record_of(reg, target);

    if (reg.valid(cur)) {
        // Hero husk (no NPCKind) has no independent existence — since the
        // mirror law it is a projection of his macro record like any other
        // body, so destroying it loses nothing and strands no inert,
        // un-rendered, un-AI'd zombie in the scene. A vacated FOREIGN body
        // keeps every component; with the reference gone its AI / draw /
        // targetability all resume by construction (each is is_avatar-gated).
        if (!reg.all_of<ecs::NPCKind>(cur)) reg.destroy(cur);
    }
    // Перенос — ОДНА перезапись ссылки: полуперенесённого состояния («тег
    // снят, тег не поставлен») больше не существует по построению.
    set_avatar(reg, target);

    // …AND THE MACRO FLAG RIDES THE SAME MOVEMENT — when it has a record to
    // ride to. It used to be deferred to leave() — AvatarTag moved here,
    // the macro flag followed on the way out — and for the whole span
    // between them the question «кем я хожу» had two answers standing on two
    // different records. That is the §45 shape exactly, and it is why a
    // potion drunk in a lord's body healed the husk the player had left
    // behind. A DERIVED take (recH пуст — nothing above remembers the body)
    // moves nothing here: the macro flag stays on the caster's own record,
    // which IS the exit reset.
    //
    // Exactly-one holds by the door itself (5б): истина — колонка playerFlag
    // анкеты, биты — кэш; дверь переноса пишет оба носителя одним движением
    // (запись нового = срыв старого через кэш). Store у сцены уже есть —
    // тот же ctx-мост, которым macro_record_of ответил хэндлом.
    if (recH.slot != kMacroNoSlot && playerFlagBits) {
        transfer_player_flag(store_of(w), *playerFlagBits, recH);
    }
    return true;
}

} // namespace sm::sub
