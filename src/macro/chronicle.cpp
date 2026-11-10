#include "macro/chronicle.h"

#include "bus/store.h"     // MacroStore — резолв участника факта в слот
#include "core/torus.h"

#include <algorithm>
#include <cstdio>

namespace sm {

namespace {

// The index cell a macro cell belongs to. The world is a torus, so this wraps
// the same way everything else that walks it does.
inline std::size_t index_cell(const Chronicle& c, int x, int y) {
    const int gx = wrapi(x / kChronicleCellSize, c.cols);
    const int gy = wrapi(y / kChronicleCellSize, c.rows);
    return std::size_t(gy) * std::size_t(c.cols) + std::size_t(gx);
}

// Участник, у которого бывает цепочка: сквад, подвижный или неподвижный.
inline bool is_party(std::uint8_t packedKind) {
    const std::uint8_t k = fact_subject_kind(packedKind);
    return k == std::uint8_t(FactSubject::Squad)
        || k == std::uint8_t(FactSubject::Rooted);
}

// ── ДОГОН ЦЕПОЧКИ ПО КЛЕТКЕ ──────────────────────────────────────────────
// Курсор впереди лога значит, что лог заменён другим (новый мир, чужая
// летопись) — выведенное забывается и собирается заново. Лог той же или
// большей длины этим не опознать; для него есть `chronicle_rebuild_links`.
void sync_cells(const Chronicle& c, ChronicleLinks& l) {
    if (l.cellSynced >= c.nextSeq) {
        std::fill_n(l.cellHead, kChronicleIndexCells, 0u);
        l.cellSynced = 0u;
    }
    for (std::uint32_t seq = l.cellSynced + 1u; seq < c.nextSeq; ++seq) {
        const WorldFact& f = c.facts[seq - 1u];
        // Link it at the HEAD of its index cell: newest first, which is the
        // order every reader wants.
        const std::size_t cell = index_cell(c, int(f.x), int(f.y));
        l.nextInCell[seq - 1u] = l.cellHead[cell];
        l.cellHead[cell] = seq;
    }
    l.cellSynced = c.nextSeq - 1u;
}

// ── ДОГОН ЦЕПОЧЕК ПО УЧАСТНИКУ ───────────────────────────────────────────
// Участник резолвится в слот В МОМЕНТ ДОГОНА. Факт о скваде, которого к этому
// моменту нет, в цепочку не ложится — и это тот же ответ, что дала бы
// пересборка с нуля: его цепочка недостижима в обоих случаях. Голова, чей факт
// называет не этого участника, принадлежит прежнему жильцу слота: новая
// цепочка начинается с нуля, а не продолжает чужую.
void sync_parties(const Chronicle& c, ChronicleLinks& l, const MacroStore& st) {
    if (l.partySynced >= c.nextSeq) {
        std::fill_n(l.subjectHead, std::size_t(kUnifiedCap), 0u);
        std::fill_n(l.objectHead, std::size_t(kUnifiedCap), 0u);
        l.partySynced = 0u;
    }
    for (std::uint32_t seq = l.partySynced + 1u; seq < c.nextSeq; ++seq) {
        const WorldFact& f = c.facts[seq - 1u];
        l.nextInSubject[seq - 1u] = 0u;
        l.nextInObject[seq - 1u] = 0u;
        if (is_party(f.subjectKind)) {
            const MacroHandle h = macro_handle_by_spawn_id(st, f.subject);
            if (st.valid(h)) {
                std::uint32_t head = l.subjectHead[h.slot];
                if (head != 0u && c.facts[head - 1u].subject != f.subject)
                    head = 0u;
                l.nextInSubject[seq - 1u] = head;
                l.subjectHead[h.slot] = seq;
            }
        }
        if (is_party(f.objectKind)) {
            const MacroHandle h = macro_handle_by_spawn_id(st, f.object);
            if (st.valid(h)) {
                std::uint32_t head = l.objectHead[h.slot];
                if (head != 0u && c.facts[head - 1u].object != f.object)
                    head = 0u;
                l.nextInObject[seq - 1u] = head;
                l.objectHead[h.slot] = seq;
            }
        }
    }
    l.partySynced = c.nextSeq - 1u;
}

// Головы цепочек `who` — или нули, если истории у него нет либо в слоте
// лежит цепочка прежнего жильца (доступ стёрт смертью, история цела).
struct PartyHeads { std::uint32_t subject = 0, object = 0; };
PartyHeads party_heads(const Chronicle& c, ChronicleLinks& l,
                       const MacroStore& st, MacroHandle who) {
    PartyHeads h{};
    if (!c.ready() || !st.valid(who)) return h;
    sync_parties(c, l, st);
    const std::uint32_t ordinal = st.spawnId[who.slot].index;
    h.subject = l.subjectHead[who.slot];
    if (h.subject != 0u && c.facts[h.subject - 1u].subject != ordinal)
        h.subject = 0u;
    h.object = l.objectHead[who.slot];
    if (h.object != 0u && c.facts[h.object - 1u].object != ordinal)
        h.object = 0u;
    return h;
}

// Обход двух цепочек слиянием по номеру, новейшими вперёд. Пустая цепочка
// (голова 0) делает слияние обходом одной — так «что я сделал» и «что сделали
// со мной» суть тот же цикл, а не два своих.
int walk_parties(const Chronicle& c, const ChronicleLinks& l,
                 std::uint32_t subjectSeq, std::uint32_t objectSeq,
                 std::int32_t sinceDay, int limit,
                 FactVisitor visit, void* user) {
    int offered = 0;
    while ((subjectSeq != 0u || objectSeq != 0u) && offered < limit) {
        const std::uint32_t seq = std::max(subjectSeq, objectSeq);
        const WorldFact& f = c.facts[seq - 1u];
        // Newest first, so the first fact older than the window ends the walk:
        // everything behind it on both chains is older still.
        if (f.day < sinceDay) break;
        visit(user, f);
        ++offered;
        if (subjectSeq == seq) subjectSeq = l.nextInSubject[seq - 1u];
        if (objectSeq == seq) objectSeq = l.nextInObject[seq - 1u];
    }
    return offered;
}

} // namespace

void chronicle_init(Chronicle& c, int mapW, int mapH) {
    // Мир вне капа клеток невыразим (fail-closed): сетка индекса не названа,
    // `ready()` отвечает «нет». Внутри капа округление вверх не выводит сетку
    // за `kChronicleIndexCells` — сторона капа кратна стороне индексной клетки.
    const bool legal = world_cells_within_cap(mapW, mapH) > 0u;
    c.cols = legal
        ? std::max(1, (mapW + kChronicleCellSize - 1) / kChronicleCellSize) : 0;
    c.rows = legal
        ? std::max(1, (mapH + kChronicleCellSize - 1) / kChronicleCellSize) : 0;
    // Чистится ровно то, что было записано: хвост за счётом и так стоит нулём
    // (инвариант лога), а счёт не бывает больше капа. Байты летописи не помнят
    // прежнего мира, и рождение пустого мира не платит проходом по всему капу.
    std::fill_n(c.facts, std::size_t(std::min(c.count(), kChronicleFactsCap)),
                WorldFact{});
    c.nextSeq = 1u;
    c.refused = 0u;
}

std::uint32_t chronicle_record(Chronicle& c, const WorldFact& fact) {
    if (!c.ready()) return 0u;
    if (fact.kind == 0u || fact.kind >= std::uint16_t(FactKind::Count)) return 0u;

    // КОНЕЦ ЭПОХИ: лог исчерпан. Ни одна прежняя запись не тронута; новое не
    // пишется, отказ сосчитан в состоянии и один раз назван вслух.
    if (c.epoch_over()) {
        if (c.refused == 0u) {
            std::fprintf(stderr,
                         "[chronicle] КОНЕЦ ЭПОХИ: лог летописи исчерпан "
                         "(%u записей, кап kChronicleFactsCap). Новые факты не "
                         "пишутся; прошлое цело. Отказы считает "
                         "Chronicle::refused.\n",
                         unsigned(kChronicleFactsCap));
        }
        if (c.refused != 0xFFFFFFFFu) ++c.refused;
        return 0u;
    }

    const std::uint32_t seq = c.nextSeq++;
    WorldFact& slot = c.facts[seq - 1u];
    slot = fact;
    slot.seq = seq;
    return seq;
}

std::unique_ptr<ChronicleLinks> make_chronicle_links() {
    return std::make_unique<ChronicleLinks>();
}

void chronicle_rebuild_links(ChronicleLinks& l) {
    std::fill_n(l.cellHead, kChronicleIndexCells, 0u);
    std::fill_n(l.subjectHead, std::size_t(kUnifiedCap), 0u);
    std::fill_n(l.objectHead, std::size_t(kUnifiedCap), 0u);
    l.cellSynced = 0u;
    l.partySynced = 0u;
}

int chronicle_near(const Chronicle& c, ChronicleLinks& links,
                   int x, int y, int radiusCells,
                   std::int32_t sinceDay, FactVisitor visit, void* user) {
    if (!c.ready() || !visit) return 0;
    sync_cells(c, links);
    const int r = std::max(0, radiusCells);
    const int cx0 = x / kChronicleCellSize;
    const int cy0 = y / kChronicleCellSize;
    int offered = 0;
    for (int oy = -r; oy <= r; ++oy) {
        for (int ox = -r; ox <= r; ++ox) {
            const std::size_t cell =
                index_cell(c, (cx0 + ox) * kChronicleCellSize,
                           (cy0 + oy) * kChronicleCellSize);
            for (std::uint32_t seq = links.cellHead[cell]; seq != 0u;
                 seq = links.nextInCell[seq - 1u]) {
                const WorldFact& f = c.facts[seq - 1u];
                // Newest first, so the first fact older than the window ends
                // this cell's walk: everything behind it is older still.
                if (f.day < sinceDay) break;
                visit(user, f);
                ++offered;
            }
        }
    }
    return offered;
}

int chronicle_recent(const Chronicle& c, std::int32_t sinceDay, int limit,
                     FactVisitor visit, void* user) {
    if (!c.ready() || !visit || limit <= 0) return 0;
    int offered = 0;
    // Backwards from the newest sequence: the log is written in time order,
    // so walking sequences down IS walking time backwards, with no index.
    const std::uint32_t count = c.count();
    for (std::uint32_t back = 0; back < count && offered < limit; ++back) {
        const WorldFact& f = c.facts[count - 1u - back];
        if (f.day < sinceDay) break;   // in time order, so the rest is older
        visit(user, f);
        ++offered;
    }
    return offered;
}

int chronicle_by_subject(const Chronicle& c, ChronicleLinks& links,
                         const MacroStore& st, MacroHandle who,
                         std::int32_t sinceDay, int limit,
                         FactVisitor visit, void* user) {
    if (!visit || limit <= 0) return 0;
    const PartyHeads h = party_heads(c, links, st, who);
    return walk_parties(c, links, h.subject, 0u, sinceDay, limit, visit, user);
}

int chronicle_by_object(const Chronicle& c, ChronicleLinks& links,
                        const MacroStore& st, MacroHandle who,
                        std::int32_t sinceDay, int limit,
                        FactVisitor visit, void* user) {
    if (!visit || limit <= 0) return 0;
    const PartyHeads h = party_heads(c, links, st, who);
    return walk_parties(c, links, 0u, h.object, sinceDay, limit, visit, user);
}

int chronicle_about(const Chronicle& c, ChronicleLinks& links,
                    const MacroStore& st, MacroHandle who,
                    std::int32_t sinceDay, int limit,
                    FactVisitor visit, void* user) {
    if (!visit || limit <= 0) return 0;
    const PartyHeads h = party_heads(c, links, st, who);
    return walk_parties(c, links, h.subject, h.object, sinceDay, limit, visit,
                        user);
}

int chronicle_near_kind(const Chronicle& c, ChronicleLinks& links,
                        int x, int y, int radiusCells,
                        FactKind kind, std::int32_t today,
                        FactVisitor visit, void* user) {
    if (kind >= FactKind::Count || !visit) return 0;
    const int days = int(fact_kind_def(kind).interestDays);
    // The window is the ROW's, so retuning what "recent" means for a kind
    // moves every reader at once and none of them restates it.
    struct Filter { FactKind kind; FactVisitor visit; void* user; int n; };
    Filter fl{kind, visit, user, 0};
    chronicle_near(c, links, x, y, radiusCells, today - days + 1,
                   [](void* u, const WorldFact& f) {
                       Filter& s = *static_cast<Filter*>(u);
                       if (f.kind != std::uint16_t(s.kind)) return;
                       s.visit(s.user, f);
                       ++s.n;
                   }, &fl);
    return fl.n;
}

namespace {

// Append into a fixed buffer, never past it. Returns the new length.
int append(char* out, int cap, int len, const char* text) {
    if (!text) return len;
    while (*text && len < cap - 1) out[len++] = *text++;
    out[len] = '\0';
    return len;
}

int append_num(char* out, int cap, int len, long v) {
    char tmp[24];
    int n = 0;
    if (v == 0) { tmp[n++] = '0'; }
    bool neg = v < 0;
    unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;
    while (u && n < 20) { tmp[n++] = char('0' + (u % 10)); u /= 10; }
    if (neg && n < 21) tmp[n++] = '-';
    while (n-- > 0 && len < cap - 1) out[len++] = tmp[n];
    out[len] = '\0';
    return len;
}

// One participant, said as well as the naming allows. A chronicle that could
// only speak about things it has names for would be silent about most of the
// world, so an unresolvable ordinal still gets an honest noun.
int append_who(char* out, int cap, int len, std::uint8_t packed,
               std::uint32_t id, const FactNaming& naming) {
    // ОСЬ СУДИТСЯ СВОИМ ТИПОМ, А НЕ БАЙТОМ (ЗАКОН СЛОВАРЯ п.4). Здесь
    // переключались по `std::uint8_t` с метками-кастами и `default:` — то
    // есть `-Wswitch` молчал бы О НОВОМ РОДЕ ДАЖЕ БЕЗ `default:`, потому что
    // ось была отмыта через байт. Теперь все шесть родов названы поимённо и
    // умолчания нет: седьмой род краснит сборку. Образец рядом — `renown_slot
    // @src/macro/squad.h`, который так и делает и пишет об этом вслух.
    const FactSubject kind = FactSubject(fact_subject_kind(packed));
    const char* name = nullptr;
    switch (kind) {
        case FactSubject::None: return append(out, cap, len, "someone");
        case FactSubject::Count: return append(out, cap, len, "someone");
        case FactSubject::Squad:
            if (naming.squad) name = naming.squad(naming.user, id);
            return append(out, cap, len, name ? name : "a band");
        case FactSubject::Rooted:
            if (naming.rooted) name = naming.rooted(naming.user, id);
            return append(out, cap, len, name ? name : "a place");
        case FactSubject::Faction:
            if (naming.faction) name = naming.faction(naming.user, id);
            return append(out, cap, len, name ? name : "a people");
        case FactSubject::Cell:
            return append(out, cap, len, "this place");
    }
    return append(out, cap, len, "someone");
}

} // namespace

int fact_sentence(const WorldFact& f, const FactNaming& naming,
                  char* out, int cap) {
    if (!out || cap <= 0) return 0;
    out[0] = '\0';
    if (f.kind == 0u || f.kind >= std::uint16_t(FactKind::Count)) return 0;

    int len = 0;
    len = append_who(out, cap, len, f.subjectKind, f.subject, naming);
    len = append(out, cap, len, " ");
    // The VERB comes from the kind's own row, so a new kind speaks by being
    // added to the table and nothing here changes.
    len = append(out, cap, len, fact_kind_def(FactKind(f.kind)).label);
    if (fact_subject_kind(f.objectKind) != std::uint8_t(FactSubject::None)) {
        len = append(out, cap, len, " ");
        len = append_who(out, cap, len, f.objectKind, f.object, naming);
    }
    if (f.amount != 0) {
        len = append(out, cap, len, " (");
        len = append_num(out, cap, len, long(f.amount));
        len = append(out, cap, len, ")");
    }
    len = append(out, cap, len, ", day ");
    len = append_num(out, cap, len, long(f.day));
    return len;
}

} // namespace sm
