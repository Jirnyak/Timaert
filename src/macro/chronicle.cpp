#include "macro/chronicle.h"

#include "core/torus.h"

#include <algorithm>

namespace sm {

namespace {

// The index cell a macro cell belongs to. The world is a torus, so this wraps
// the same way everything else that walks it does.
inline std::size_t index_cell(const Chronicle& c, int x, int y) {
    const int gx = wrapi(x / kChronicleCellSize, c.cols);
    const int gy = wrapi(y / kChronicleCellSize, c.rows);
    return std::size_t(gy) * std::size_t(c.cols) + std::size_t(gx);
}

// Is `seq` still in the ring, and is it OLDER than the fact that linked to it?
//
// Both halves matter. The first says the slot has not been overwritten; the
// second catches the case where it was overwritten by a NEWER fact, which
// would otherwise turn a chain into a loop. Together they make unlinking
// unnecessary: the chain simply ends where the ring forgot.
inline bool link_alive(const Chronicle& c, std::uint32_t seq,
                       std::uint32_t fromSeq) {
    return seq != 0u && seq >= c.oldest_seq() && seq < fromSeq;
}

inline const WorldFact* slot_of(const Chronicle& c, std::uint32_t seq) {
    if (seq == 0u) return nullptr;
    const WorldFact& f = c.ring[(seq - 1u) % kChronicleFacts];
    return f.seq == seq ? &f : nullptr;
}

} // namespace

void chronicle_init(Chronicle& c, int mapW, int mapH) {
    c.cols = std::max(1, (mapW + kChronicleCellSize - 1) / kChronicleCellSize);
    c.rows = std::max(1, (mapH + kChronicleCellSize - 1) / kChronicleCellSize);
    c.ring.assign(kChronicleFacts, WorldFact{});
    c.cellHead.assign(std::size_t(c.cols) * std::size_t(c.rows), 0u);
    c.annals.clear();
    c.annalsFull = false;
    c.nextSeq = 1u;
    c.countingDay = -1;
    c.factsToday = 0u;
}

std::uint32_t chronicle_record(Chronicle& c, const WorldFact& fact) {
    if (!c.ready()) return 0u;
    if (fact.kind == 0u || fact.kind >= std::uint16_t(FactKind::Count)) return 0u;

    const std::uint32_t seq = c.nextSeq++;
    WorldFact& slot = c.ring[(seq - 1u) % kChronicleFacts];
    slot = fact;
    slot.seq = seq;

    // Link it at the HEAD of its index cell: newest first, which is the order
    // every reader wants and the only order a ring can cheaply keep.
    const std::size_t cell = index_cell(c, int(fact.x), int(fact.y));
    slot.nextInCell = c.cellHead[cell];
    c.cellHead[cell] = seq;

    // The instrument: how many facts this day is producing, so the ring's size
    // can be tuned from evidence instead of from feel.
    if (fact.day != c.countingDay) {
        c.countingDay = fact.day;
        c.factsToday = 0u;
    }
    ++c.factsToday;

    // ...and if a NAMED participant took part, the world keeps it for good.
    // The ring will forget this same fact in a season; the annals will not.
    if (subject_is_named(fact.subjectKind) || subject_is_named(fact.objectKind)) {
        if (c.annals.size() < std::size_t(kChronicleAnnals)) {
            c.annals.push_back(slot);
        } else {
            // Loud rather than silent: a world whose legend outgrew its cap
            // has a tuning problem, and dropping history quietly is how you
            // never find out.
            c.annalsFull = true;
        }
    }
    return seq;
}

void chronicle_rebuild_links(Chronicle& c) {
    if (!c.ready()) return;
    std::fill(c.cellHead.begin(), c.cellHead.end(), 0u);
    // Oldest to newest, linking each at the head — which is exactly the order
    // and the operation `chronicle_record` performed, so the chains come back
    // identical rather than merely equivalent.
    for (std::uint32_t seq = c.oldest_seq(); seq < c.nextSeq; ++seq) {
        WorldFact& f = c.ring[(seq - 1u) % kChronicleFacts];
        if (f.seq != seq) continue;   // never written, or already overwritten
        const std::size_t cell = index_cell(c, int(f.x), int(f.y));
        f.nextInCell = c.cellHead[cell];
        c.cellHead[cell] = seq;
    }
}

int chronicle_near(const Chronicle& c, int x, int y, int radiusCells,
                   std::int32_t sinceDay, FactVisitor visit, void* user) {
    if (!c.ready() || !visit) return 0;
    const int r = std::max(0, radiusCells);
    const int cx0 = x / kChronicleCellSize;
    const int cy0 = y / kChronicleCellSize;
    int offered = 0;
    for (int oy = -r; oy <= r; ++oy) {
        for (int ox = -r; ox <= r; ++ox) {
            const std::size_t cell =
                index_cell(c, (cx0 + ox) * kChronicleCellSize,
                           (cy0 + oy) * kChronicleCellSize);
            std::uint32_t seq = c.cellHead[cell];
            std::uint32_t fromSeq = c.nextSeq;   // the head may be anything older
            while (const WorldFact* f = link_alive(c, seq, fromSeq)
                                            ? slot_of(c, seq) : nullptr) {
                // Newest first, so the first fact older than the window ends
                // this cell's walk: everything behind it is older still.
                if (f->day < sinceDay) break;
                visit(user, *f);
                ++offered;
                fromSeq = seq;
                seq = f->nextInCell;
            }
        }
    }
    return offered;
}

int chronicle_recent(const Chronicle& c, std::int32_t sinceDay, int limit,
                     FactVisitor visit, void* user) {
    if (!c.ready() || !visit || limit <= 0) return 0;
    int offered = 0;
    // Backwards from the newest sequence: the ring is written in time order,
    // so walking sequences down IS walking time backwards, with no index.
    const std::uint32_t oldest = c.oldest_seq();
    for (std::uint32_t seq = c.nextSeq; seq-- > oldest && offered < limit; ) {
        const WorldFact* f = slot_of(c, seq);
        if (!f) continue;
        if (f->day < sinceDay) break;   // in time order, so the rest is older
        visit(user, *f);
        ++offered;
    }
    return offered;
}

int chronicle_near_kind(const Chronicle& c, int x, int y, int radiusCells,
                        FactKind kind, std::int32_t today,
                        FactVisitor visit, void* user) {
    if (kind >= FactKind::Count || !visit) return 0;
    const int days = int(fact_kind_def(kind).interestDays);
    // The window is the ROW's, so retuning what "recent" means for a kind
    // moves every reader at once and none of them restates it.
    struct Filter { FactKind kind; FactVisitor visit; void* user; int n; };
    Filter fl{kind, visit, user, 0};
    chronicle_near(c, x, y, radiusCells, today - days + 1,
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
    const std::uint8_t kind = fact_subject_kind(packed);
    const char* name = nullptr;
    switch (kind) {
        case std::uint8_t(FactSubject::Squad):
            if (naming.squad) name = naming.squad(naming.user, id);
            return append(out, cap, len, name ? name : "a band");
        case std::uint8_t(FactSubject::Landmark):
            if (naming.landmark) name = naming.landmark(naming.user, id);
            return append(out, cap, len, name ? name : "a place");
        case std::uint8_t(FactSubject::Faction):
            if (naming.faction) name = naming.faction(naming.user, id);
            return append(out, cap, len, name ? name : "a people");
        case std::uint8_t(FactSubject::Cell):
            return append(out, cap, len, "this place");
        default:
            return append(out, cap, len, "someone");
    }
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

int chronicle_annals_of(const Chronicle& c, std::uint8_t subjectKind,
                        std::uint32_t subject, int limit,
                        FactVisitor visit, void* user) {
    if (!visit || limit <= 0) return 0;
    int offered = 0;
    // Newest first, like every other view on the past. A linear walk on
    // purpose: the annals are asked rarely (a chronicle screen, a bard, a
    // quest that wants a grudge), and a second index would be a second thing
    // to keep true.
    for (std::size_t i = c.annals.size(); i-- > 0 && offered < limit; ) {
        const WorldFact& f = c.annals[i];
        if (subjectKind != 0u) {
            const bool asSubject =
                f.subjectKind == subjectKind && f.subject == subject;
            const bool asObject =
                f.objectKind == subjectKind && f.object == subject;
            if (!asSubject && !asObject) continue;
        }
        visit(user, f);
        ++offered;
    }
    return offered;
}

} // namespace sm
