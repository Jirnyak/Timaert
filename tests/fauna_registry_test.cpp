// Unit tests for the global creature registry (sm::sub, src/sub/fauna.cpp).
// Pins the stable-id contract the subworld spawn / death / loot path relies on:
//
//   * a creature's stable id IS its ordinal in THE one body table — there is
//     no monster catalog beside it and no `0x100` bit any more (2026-08-20);
//   * creature_def_from_kind() recovers the exact row, returning nullptr for
//     the humanoid rows and for anything that names no row at all.
//
// If catalog order ever changed (which would silently re-key live and saved
// entities) or the bit contract regressed, these assertions fail. Plain main(),
// no framework — mirrors tests/item_use_parity_test.cpp.

#include "check.h"

#include "macro/fauna.h"
#include "sub/crowd_layout.h"  // полосы толпы — данные субмира (2026-11-02)
#include "sub/gens/dispatch.h" // feature_fauna_band — полоса модуля фичи (M-371)
#include "sub/dgn/dispatch.h"  // dungeon_kind_row — полоса рода норы (M-391)
#include "tables/faction.h"   // faction_index — перевод имени в ординал (M-187)
#include "tables/feature_type.h" // FT_Count — отрезок байтов слоя фич

#include <cstdint>
#include <cstdio>
#include <string_view>

using namespace sm;
using sm::FaunaEntry;

int main() {
    const auto catalog = sm::creature_catalog();
    const int n = int(catalog.size());

    CHECK(n > 0, "catalog is non-empty");

    // ── structural invariants: every entry well-formed ──────────────────────
    for (int i = 0; i < n; ++i) {
        const FaunaEntry* e = catalog[i];
        CHECK(e != nullptr, "catalog entry is non-null");
        if (e == nullptr) continue;
        CHECK(e->id != nullptr && e->id[0] != '\0', "entry has a non-empty stable id");
        CHECK(e->label != nullptr, "entry has a label");
    }
    // ids and entry pointers are unique across the catalog.
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            if (catalog[i] == nullptr || catalog[j] == nullptr) continue;
            CHECK(catalog[i] != catalog[j],
                   "each creature entry appears at most once in the catalog");
            CHECK(std::string_view(catalog[i]->id) != catalog[j]->id,
                   "creature stable ids are unique across the catalog");
        }
    }

    // ── round-trip: index <-> kind <-> def <-> id ───────────────────────────
    for (int i = 0; i < n; ++i) {
        const FaunaEntry* e = catalog[i];
        const std::uint16_t kind = std::uint16_t(sm::creature_index(e));

        CHECK(sm::creature_def_from_kind(kind) == e,
               "creature_def_from_kind(ordinal) recovers the row");
        CHECK(sm::creature_index(e) == int(kind),
               "a row's index IS the kind it answers to");
        CHECK(sm::creature_def(e->id) == e,
               "creature_def(id) recovers the same entry");
        CHECK(sm::creature_index(sm::creature_def_from_kind(kind)) == int(kind),
               "kind -> def -> index closes the loop");

        // (The old "same number without the 0x100 bit is a humanoid" check
        // died with the bit: there is no second number to compare against any
        // more. What it guarded — humanoid rows are not creatures — is asserted
        // directly over the role rows below.)
    }

    // ── the humanoid rows are never creatures ────────────────────────────────
    // ПРИРОДА СУДИТ, А НЕ КАТАЛОГ (2026-09-21): каталог — вид на ОДНУ
    // таблицу тел, и строку он отдаёт всякому существующему роду; «человек
    // ли он» — отдельный вопрос отдельной двери. Прежде здесь стоял гейт
    // «гуманоид не тварь», и он вычёркивал бы культиста из его же каталога.
    for (std::uint16_t k : {std::uint16_t(sm::NPCType::Peasant),
                            std::uint16_t(sm::NPCType::Guard),
                            std::uint16_t(sm::NPCType::Sorceress),
                            std::uint16_t(sm::NPCType::Merchant)}) {
        CHECK(sm::is_folk_kind(k), "народ — это архетип строки, не каталог");
        CHECK(sm::npc_archetype(sm::NPCType(k)) == sm::ObjectArchetype::Folk,
              "и тот же ответ даёт сама лестница");
    }
    // ОСЬ РАЗОБРАЛА СВАЛКУ, И СВИДЕТЕЛЬ ТЕПЕРЬ ЭТО ВИДИТ (M-295): до наряда
    // волк и конь несли ОДНУ колонку `NpcNature::Fauna` и были для мира
    // неразличимы; здесь стоял `is_fauna_kind` на обоих. Дверь снесена (ноль
    // продакшен-читателей), а вопрос стал точнее — у них РАЗНЫЕ архетипы.
    CHECK(sm::npc_archetype(sm::NPCType::Wolf) == sm::ObjectArchetype::Animal,
          "волк — животное нашего мира");
    CHECK(sm::npc_archetype(sm::NPCType::Horse)
              == sm::ObjectArchetype::Livestock,
          "конь — скот, и это НЕ тот же архетип, что у волка");
    CHECK(sm::npc_archetype(sm::NPCType::Goblin)
              == sm::ObjectArchetype::Monster,
          "гоблин — разумная тварь, а не зверь (ось по РАЗУМУ, не по рукам)");
    CHECK(sm::npc_archetype(sm::NPCType::Lich) == sm::ObjectArchetype::Void,
          "лич — хтонь");
    for (std::uint16_t k : {std::uint16_t(sm::NPCType::Wolf),
                            std::uint16_t(sm::NPCType::Horse),
                            std::uint16_t(sm::NPCType::Goblin),
                            std::uint16_t(sm::NPCType::Lich)}) {
        CHECK(!sm::is_folk_kind(k), "и ни один из них не народ");
    }

    // ── a kind that names no row at all -> nullptr ───────────────────────────
    CHECK(sm::creature_def_from_kind(std::uint16_t(sm::NPCType::Count)) == nullptr,
           "one past the last row -> nullptr");
    CHECK(sm::creature_def_from_kind(std::uint16_t(0x1FF)) == nullptr,
           "a number far outside the table -> nullptr");

    // ── unknown id / null / non-catalog pointer ──────────────────────────────
    CHECK(sm::creature_def("definitely_not_a_creature_id") == nullptr,
           "unknown id -> nullptr");
    CHECK(sm::creature_index(nullptr) == -1,
           "creature_index(nullptr) == -1");
    FaunaEntry stranger{};
    stranger.id = "stranger";
    CHECK(sm::creature_index(&stranger) == -1,
           "an entry not in the catalog -> index -1");

    // ── THE BESTIARY IS REACHABLE (content witness, 2026-09-11) ────────────
    //
    // A species row is content only if SOMETHING can actually raise it. Both
    // ways of being dead content are silent: a zero `weight` and a habitat
    // mask that no place asks for read exactly like a species that is simply
    // rare. Доска сменилась 2026-10-04 (D-хвост): РОЛЛ ТОЛПЫ МЁРТВ —
    // pick_crowd_row уничтожена, житель есть ГОЛОВА инвентаря места на всех
    // путях. У строки осталось ДВЕ земли, и они разной природы:
    //   · дикая/логовная — прогонная: roll_spawns обязан реально поднять
    //     строку с земель, которые у мира есть;
    //   · толпа места — СТРУКТУРНАЯ: строка достижима, если её habitat
    //     пересекает полосу crowdHabitat хоть одного рода мест — такую
    //     строку селят двери поселения душ (settle_souls / авторский посев),
    //     роллом её больше не рожает никто.
    // Потерянный бит habitat по-прежнему падает здесь: строка, которую не
    // поднимает ни ролл, ни одна полоса толпы, — мёртвый контент.
    {
        // ГОЛОВАМИ, А НЕ ФЛАЖКОМ (M-391): «строка поднимается» и «сколько её
        // поднимается» — два вопроса, и второй нужен, чтобы Лича утверждать
        // ЧИСЛОМ, а не галочкой.
        std::uint64_t raised[std::size_t(sm::NPCType::Count)] = {};
        auto sweep = [&](sm::FaunaBand band, sm::Biome biome, bool forest,
                         std::uint8_t danger, std::uint32_t salt) {
            sm::SpawnContext ctx{};
            ctx.biome = biome;
            ctx.forest = forest;
            ctx.band = band;
            ctx.danger = danger;
            std::uint32_t rng = 0x5EED0000u ^ salt;
            for (int i = 0; i < 4096; ++i) {
                for (const auto& p : sm::roll_spawns(ctx, rng)) {
                    if (p.entry) ++raised[std::size_t(p.entry->row)];
                }
            }
        };
        auto seen = [&](sm::NPCType row) {
            return raised[std::size_t(row)] != 0;
        };
        // ── ЗЕМЛИ СМЕТАЮТСЯ ВСЕ, И ОНИ СПРАШИВАЮТСЯ ДВЕРЬМИ (M-391) ──────
        //
        // ЗДЕСЬ СМЕТАЛАСЬ ОДНА МОДУЛЬНАЯ ПОЛОСА — РУИННАЯ, ВЫБРАННАЯ ПО
        // ИМЕНИ, — И ЭТО БЫЛА ДЫРА РОВНО ТОГО РОДА, КОТОРЫЙ СВИДЕТЕЛЬ ОБЯЗАН
        // ЛОВИТЬ. Бит `kHabSpire` носили 14 строк каталога, НИ ОДНА полоса
        // дерева его не производила (замер M-391), а проверка «строка
        // достижима» всё равно зеленела: Лич уходил в escape-hatch
        // «crowd ground» ниже, потому что `kHabSpire` стоит полосой ТОЛПЫ
        // рода `Spire` (`kCrowdLayout@src/sub/crowd_layout.h`). Толпы у шпиля
        // нет с M-320 — сквада нет, значит голов инвентаря нет, значит селить
        // некого, — то есть та земля была ФИКТИВНОЙ.
        //
        // Теперь сметаются ВСЕ двери полос, какие у дерева есть: КАЖДЫЙ байт
        // слоя фич и КАЖДЫЙ род норы данжа. Выбор по имени кончился, значит
        // забытая полоса нового байта падает здесь сама.
        const sm::FaunaBand ruinBand =
            sm::sub::feature_fauna_band(std::uint8_t(sm::FT_Ruin));
        CHECK(ruinBand.bit != 0, "модуль руины объявляет свою полосу спавна");
        const sm::FaunaBand spireBand =
            sm::sub::feature_fauna_band(std::uint8_t(sm::FT_Spire));
        CHECK(spireBand.bit != 0, "модуль шпиля объявляет свою полосу спавна");
        // ОДИН ШПИЛЬ — ОДИН ОТВЕТ НА ВОПРОС «КТО В НЁМ ЖИВЁТ» (DOD п.6).
        // Улицу шпиля пишет модуль байта фичи, ярусы башни — род норы; это
        // две СЦЕНЫ одной вещи, и разойтись битом им нельзя, иначе у одного
        // шпиля два бестиария. Сравнивается РОВНО бит: счёт голов за визит
        // есть баланс (ЗАКОН ПРИОРИТЕТА СТРУКТУРЫ), и свидетель, краснеющий
        // на перекалибровку, мешал бы развитию (§8 п.7).
        CHECK(sm::sub::dungeon_kind_row(sm::sub::DungeonRef::SpireTower).den.bit
                  == spireBand.bit,
              "улица шпиля и ярусы его башни роллят ОДНУ полосу — у одной "
              "вещи один бестиарий");
        for (int d = 0; d <= 255; d += 15) {
            const auto danger = std::uint8_t(d);
            // ВСЕ полосы модулей байтов фич — по одной соли на байт.
            for (int ft = 0; ft < int(sm::FT_Count); ++ft) {
                const sm::FaunaBand b =
                    sm::sub::feature_fauna_band(std::uint8_t(ft));
                if (b.bit == 0) continue;   // {} = земля отвечает (ниже)
                sweep(b, sm::Meadow, false, danger, 0x200u + std::uint32_t(ft));
            }
            // ВСЕ полосы родов нор данжа — тем же порядком.
            for (int k = 0; k <= int(sm::sub::DungeonRef::PrologueRoad); ++k) {
                const sm::FaunaBand b =
                    sm::sub::dungeon_kind_row(std::uint8_t(k)).den;
                if (b.bit == 0) continue;
                sweep(b, sm::Meadow, false, danger, 0x300u + std::uint32_t(k));
            }
            // И ЗЕМЛЯ — полоса {} по каждому биому, который у мира есть.
            sweep(sm::FaunaBand{}, sm::Meadow,   false, danger, 3u);
            sweep(sm::FaunaBand{}, sm::Mountain, false, danger, 4u);
            sweep(sm::FaunaBand{}, sm::Swamp,    false, danger, 5u);
            sweep(sm::FaunaBand{}, sm::Desert,   false, danger, 6u);
            sweep(sm::FaunaBand{}, sm::Steppe,   false, danger, 7u);
            sweep(sm::FaunaBand{}, sm::Valley,   true,  danger, 8u);
            sweep(sm::FaunaBand{}, sm::Taiga,    false, danger, 9u);
            sweep(sm::FaunaBand{}, sm::Tundra,   false, danger, 10u);
            sweep(sm::FaunaBand{}, sm::Snow,     false, danger, 11u);
            sweep(sm::FaunaBand{}, sm::Tropics,  false, danger, 12u);
        }
        // ── ЛИЧ — ОТДЕЛЬНЫЙ, НЕЗАВИСИМЫЙ ВОПРОС, И ОН УТВЕРЖДАЕТСЯ ЧИСЛОМ ──
        //
        // Он ЕДИНСТВЕННАЯ строка каталога, у которой ареал равен `kHabSpire`
        // одним битом (перепись M-391: бит носят 14 строк, 13 из них достижимы
        // другой землёй — руиной, горой, лесом, тундрой). Значит «родов в
        // шпиле стало больше» его появления НЕ доказывает, и утверждение у
        // него своё. Проверка стоит ОТДЕЛЬНО от общей достижимости выше
        // именно потому, что общая зеленеет через crowd-hatch, а эта — нет.
        CHECK(raised[std::size_t(sm::NPCType::Lich)] != 0,
              "Лич встаёт: его ареал есть kHabSpire ЕДИНСТВЕННЫМ, и до M-391 "
              "эту землю не производила ни одна полоса дерева");
        std::printf("fauna_registry_test: лич поднят %llu раз\n",
                    (unsigned long long)raised[std::size_t(sm::NPCType::Lich)]);
        // Союз полос толпы ВСЕХ родов мест — вторая земля, структурная.
        //
        // ЭТОТ ESCAPE-HATCH ЕЩЁ ДЕРЖИТ ФИКЦИЮ, И ОНА НАЗВАНА ВСЛУХ: полоса
        // толпы рода `Spire` (`kHabSpire`) и рода `Lair` (`kHabRuin`) не
        // достаёт ни до одного живого читателя — у шпиля нет сквада с M-320, а
        // сквад рода `Lair` в дереве не рождает НИ ОДИН писатель (перепись
        // M-391: 0 писателей, 6 вхождений — все строки таблиц). Поэтому счётчик
        // `crowd-stripe-only` ниже есть ПРИБОР над этой фикцией, а не вердикт:
        // строка, прошедшая ТОЛЬКО толпой, достижима лишь на бумаге. Снос
        // мёртвой половины партиции — отдельный наряд (**M-400**).
        std::uint16_t crowdUnion = 0;
        for (std::size_t k = 0; k < std::size_t(sm::SquadType::Count); ++k) {
            crowdUnion |= sm::sub::crowd_layout(sm::SquadType(k)).crowdHabitat;
        }
        CHECK(crowdUnion != 0, "хоть один род мест держит толпу");
        int reachable = 0;
        int viaCrowd = 0;
        for (int i = 0; i < n; ++i) {
            const FaunaEntry* e = catalog[i];
            if (e == nullptr || e->weight == 0) continue;   // named-only rows
            ++reachable;
            const bool crowdGround =
                (sm::npc_def(e->row).habitat & crowdUnion) != 0;
            viaCrowd += crowdGround && !seen(e->row) ? 1 : 0;
            CHECK(seen(e->row) || crowdGround,
                   "a rollable catalog row has ground SOMETHING can raise it "
                   "from: roll_spawns or a settlement crowd stripe");
            if (!seen(e->row) && !crowdGround) {
                std::printf("  unreachable: %s\n", e->id);
            }
        }
        CHECK(reachable > 0, "the catalog has rollable rows at all");
        std::printf("fauna_registry_test: rollable=%d, crowd-stripe-only=%d\n",
                    reachable, viaCrowd);
        // (Свидетель «толпа шпиля — много видов» умер вместе с роллом,
        // которого он был носителем: состав толпы теперь ФАКТ ИНВЕНТАРЯ —
        // что поселили двери душ, то и стоит, — и прибором каталога он
        // больше не является. §5 п.6: механика снята, носитель назван.)
    }

    // ── M-39: ДИЧЬ ПРИНАДЛЕЖИТ ЗЕМЛЕ, А НЕ ВИДУ МЕСТА ───────────────────
    // Закон (§7 «ОДИН РЕЕСТР ФРАКЦИЙ», DOD п.6): на вопрос «чей это зверь»
    // отвечает ОДНА ступень — строка вида, колонкой `wildFaction`. До M-39
    // над ней стояла строковая колонка `spawnFaction` ВИДА МЕСТА, и она
    // побеждала: у шпиля `faunaHabitat = kPlaceFaunaGround`, то есть его
    // дикие ролы — обычные звери склона, и колонка штамповала их
    // Демоническими Ордами. Ровно ту двусмысленность «гарнизон или дичь»
    // place_registry.h объявлял убитой тремя строками выше колонки.
    // Знамя места достаёт до его ТОЛПЫ и ГАРНИЗОНА (это его население) и не
    // достаёт до зверя на холме.
    {
        sm::SpawnContext ruinCell{};
        ruinCell.biome = sm::Biome::Meadow;
        ruinCell.band =
            sm::sub::feature_fauna_band(std::uint8_t(sm::FT_Ruin));
        ruinCell.danger = 128;   // середина руинной полосы генератора (51..229)
        int picks = 0, nonDemon = 0, mismatched = 0;
        for (std::uint32_t s = 0; s < 256u; ++s) {
            std::uint32_t rngState = 0xA11CE000u ^ s;
            for (const sm::FaunaPick& p : sm::roll_spawns(ruinCell, rngState)) {
                ++picks;
                // Ответ РОВНО тот, что сказала строка вида. Это не копия
                // продакшен-логики: утверждается тождество двух ЧИТАТЕЛЕЙ
                // одной колонки, а прежний код их разводил.
                // Носитель стал ОРДИНАЛОМ (M-187), закон не тронут:
                // сравниваются те же два читателя одной колонки, только
                // числами, а имя переводится на границе — здесь.
                if (p.faction != std::uint16_t(sm::faction_index(
                        sm::npc_def(p.entry->row).wildFaction))) {
                    ++mismatched;
                }
                if (p.faction
                    != std::uint16_t(sm::faction_index("demons"))) {
                    ++nonDemon;
                }
            }
        }
        CHECK(picks > 0, "a ruin cell really does roll wild heads");
        CHECK(mismatched == 0,
              "a wild roll wears its OWN row's banner — no place-kind "
              "override above it");
        // НЕГАТИВНЫЙ КОНТРОЛЬ: 35 % веса руинного пула несут строки с
        // wildFaction="wildlife" (мера: разбор habitat/weight/wildFaction по
        // tables/npc.h, дерево m39-faction, 2026-09-28 — 20 из 57). Пока
        // колонка вида жила, ВСЯКАЯ из них выходила демоном, и этот счётчик
        // был бы нулём. Возврат override красит строку.
        CHECK(nonDemon > 0,
              "a ruin's vermin stay vermin: the dead city does not conscript "
              "rats into the Demonic Hordes");
        std::printf("fauna_registry_test: ruin picks=%d non-demon=%d\n",
                    picks, nonDemon);
    }

    std::printf("fauna_registry_test: catalog=%d entries\n", n);
    return sm::test::report("fauna_registry_test");
}
