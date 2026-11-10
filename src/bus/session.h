// ── СЕССИЯ — ТО, ЧТО ЖИВЁТ ОТ ЗАПУСКА ДО ВЫХОДА И МИРОМ НЕ ЯВЛЯЕТСЯ ─────────
//
// Вердикт владельца 2026-11-10 (сейв-эпик, вопрос В1 ряда M-439): четыре члена
// — лента HUD, кэш активного флажка, счётчик события состава и аренда окна —
// ВЫНЕСЕНЫ из `GameState@src/macro/state.h` на `App@src/app/app_state.h`.
// `GameState` с этого дня есть состояние макромира БЕЗ ОГОВОРОК: его байты
// снимает прямой дамп (CRAFT §10), и списка «что сбросить после загрузки» у
// состояния нет — сбрасывать в нём нечего.
//
// ЧТО ЗДЕСЬ ЛЕЖИТ И ПОЧЕМУ ЭТО НЕ МИР:
//   · `feed`          — слова HUD, умирающие с моментом (представление);
//   · `playerFlagRef` — КЭШ носителя колонки `playerFlag` стора. Истина —
//     колонка (род 2); кэш пишет одна дверь переноса вместе с колонкой
//     (`transfer_player_flag@src/bus/store.h`), а после загрузки его
//     пересобирает скан колонки (`resolve_player_handles_after_load@src/macro/player_entity.h`);
//   · `navEpoch`      — счётчик СОБЫТИЯ «состав осёдлых сменился»: по нему
//     поднимается запечённое от состава (навигация). Растёт в двери рождения
//     осёдлого сквада и в двери загрузки (мир заменён целиком — тоже событие);
//   · `leasedWindow`  — клетка-центр девяти, которые ведёт субмир. На макро-слое
//     аренды нет по построению, а сохраняться можно только на нём — то есть в
//     файле ей нечего было бы сказать. ВО ФРЕЙМ она по-прежнему едет колонкой
//     рода 4 (`LocalContext::leasedWindow@src/bus/local_frame.h`): пакет несёт
//     её получателю, состояние мира её не хранит.
//
// ГДЕ ЖИВЁТ ЭКЗЕМПЛЯР. Один — членом `App`. Симуляция видит его АДРЕСОМ в
// конверте слоёв (`MacroWorld::session@src/macro/macro_world.h`) или явным
// параметром двери; нулевой адрес есть «сессии нет» — фикстура без игрока и
// без аренды, и читается он нулевым вкладом (аренды нет, флажка нет).
//
// Заголовок нищий (ни одного тела дороже O(1), AGENTS §5 п.13) и лежит на шине
// форм, потому что читают его оба мира: субмир сдаёт и снимает аренду, макромир
// её слушает.
#pragma once
#include <cstdint>
#include <cstdio>

#include "core/macro_handle.h"   // MacroHandle — кэш носителя флажка
#include "core/torus.h"          // cell_step — рамка 3×3 есть восемь соседей

namespace sm {

// ── THE SESSION FEED: words that die with the moment ─────────────────────
// (owner, 2026-08-28: «это вообще не нужно хранить даже в сессии — пишется
// в UI как в Might & Magic и сразу забывается»). The old EventLogRing kept
// 8192 std::strings in the SAVE; but a session message ("Game saved.",
// "You have learned Fireball!") is not a fact of the world and not the
// player's journal — it is presentation. So the channel is a tiny POD ring
// the HUD fades out, NEVER serialized. What the world remembers is the
// chronicle; what the player learned is his journal; this is neither.
struct SessionFeed {
    static constexpr int kLines = 8;     // more than fits on screen anyway
    static constexpr int kTextCap = 112; // one HUD line, NUL included
    struct Line {
        char  text[kTextCap];
        float ttl = 0.0f;                // seconds of screen life left
    };
    Line lines[kLines]{};
    std::uint8_t head = 0;               // where the NEXT line goes
};

// The ONE door into the session feed (drawn and faded by the HUD, never
// saved). Overlong lines are cut at the HUD's own width — a feed line is a
// glance, not a document.
inline void session_feed_push(SessionFeed& f, const char* text) {
    if (!text || text[0] == '\0') return;
    SessionFeed::Line& l = f.lines[f.head % SessionFeed::kLines];
    std::snprintf(l.text, sizeof l.text, "%s", text);
    l.ttl = 6.0f;   // seconds on screen — M&M's own unhurried fade
    f.head = std::uint8_t((f.head + 1) % SessionFeed::kLines);
}

// «АРЕНДЫ НЕТ» — последнее значение типа (ЗАКОН УЗКОГО ИНДЕКСА): клетка 0
// законна, и нулём тут сказать нечего.
inline constexpr std::uint32_t kNoWindowLease = 0xFFFFFFFFu;

struct Session {
    SessionFeed   feed;
    // «Кем я на карте» — кэш носителя колонки `playerFlag` (M-220: хэндл
    // {слот, ID} напрямую; «никого» — id == 0, закон нуля-ординала).
    MacroHandle   playerFlagRef{};
    // СМЕНА СОСТАВА ОСЁДЛЫХ — СОБЫТИЕ, А НЕ СОСТОЯНИЕ (CANON S9, владелец
    // 2026-09-20: «рождение-смерть ландмарка это конкретные события, и должна
    // быть единая система-дверь, никаких проверщиков»). Всё, что запечено ОТ
    // СОСТАВА (навигация S7), сравнивает одно число вместо прохода по миру.
    std::uint32_t navEpoch = 0;
    // АРЕНДА ОКНА — ОДНО ЧИСЛО (AGENTS ЗАКОН ШВА, вердикт владельца 2026-10-06:
    // «смещение клетки рамки это смена аренды а арендована может быть всегда
    // одна клетка (ну и рамка вокруг неё 3х3)»). Хранится клетка-центр;
    // арендованное множество есть она плюс восемь соседей через `cell_step`.
    // Флага «игрок внизу» нет — вопрос «кто ведёт эту запись» есть вопрос О
    // КЛЕТКЕ.
    std::uint32_t leasedWindow = kNoWindowLease;
};

// ── ДВЕРИ АРЕНДЫ ОКНА — ОДНА НА СДАЧУ, ОДНА НА СНЯТИЕ ────────────────────
// КОНЦОВ АРЕНДЫ ДВА, И ОНИ РАВНОПРАВНЫ (вердикт владельца 2026-10-06):
// подъём наверх и РЕ-ЦЕНТР рамки на другую клетку. Для мира это одно и то же
// событие «аренда сменилась», поэтому ре-центр идёт ТОЙ ЖЕ дверью сдачи с
// новой клеткой, а не своим путём.
//
// ДАНЖ — ЧАСТНЫЙ СЛУЧАЙ «АРЕНДЫ НЕТ», И ЭТО ЗНАЧЕНИЕ, А НЕ ВЕТКА (вердикт
// того же дня: «данж частный случай когда арендованой клетки нет всё должно
// работать нормально — но при выходе из данжа сразу арендуется клетка куда
// вышли … не хардкодить а те же двери как вход в субмир»): спуск в данж
// СНИМАЕТ аренду, выход из него СДАЁТ клетку выхода — обе эти двери.
inline void lease_window_at(Session& ses, std::uint32_t cell) {
    ses.leasedWindow = cell;
}
inline void release_window(Session& ses) {
    ses.leasedWindow = kNoWindowLease;
}

// ЭТА КЛЕТКА В РАМКЕ 3×3 ОТ ЦЕНТРА АРЕНДЫ? Голая геометрия: её же спрашивает
// ядро марша через `MarchGround::leaseCenter` (изъятие ног, ломоть 2 M-419).
// Центр плюс восемь румбов — `cell_step` по ЗАКОНУ АДРЕСА, то есть без ручной
// свёртки и без пары координат. Девять сравнений целых, ни одного деления:
// вопрос задаётся в свипе, в суде встречи и в цикле марша — на горячем пути.
inline bool cell_in_lease_frame(std::uint32_t center, std::uint32_t cell,
                                int side) noexcept {
    if (center == kNoWindowLease) return false;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (cell_step(center, dx, dy, side) == cell) return true;
        }
    }
    return false;
}

// ЭТА КЛЕТКА В АРЕНДЕ? Обёртка над геометрией для читателей с сессией на
// руках (суды автобоя/встречи, свип мёртвых записей). Нулевой адрес сессии —
// «аренды нет»: фикстура без субмира ничего не сдавала.
inline bool cell_is_leased(const Session* ses, std::uint32_t cell,
                           int side) noexcept {
    return ses && cell_in_lease_frame(ses->leasedWindow, cell, side);
}

} // namespace sm
