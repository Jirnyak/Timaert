// THE ground table is data, and this is what keeps it that way.
//
// shaders/ground_surface.glsl is generated from data/ground_materials.csv and
// data/ground_cover.csv by tools/gen_ground_table.py. Generated-and-committed
// is the right trade (the build needs no Python, the shader needs no runtime
// table upload) but it has one failure mode: somebody edits the GLSL, or edits
// the CSV and forgets to re-run, and the two quietly disagree. From then on
// the CSV is a lie — owner feedback lands in a file nothing reads.
//
// So this test re-derives the WHOLE table from the CSVs, in C++, and compares
// every number against what the GLSL declares. It is the same guard the
// reference project spends its `source_rules` ctest on, in the shape this tree
// already uses for registries (sheet_registry_test, bonus_registry_test,
// spell_registry_test): the table's ordinals are the law, and a row's meaning
// is checked, not just its presence.
//
// What it pins:
//   1. every CSV row stands at its own ordinal, and that ordinal is the
//      TerrainMaterial id the material texture carries (sub/material.h);
//   2. every number in the GLSL is the number the CSV says, including the
//      DERIVED ones — the mean colour from the two constituents it is the
//      midpoint of, the frequency from the wavelength, the ladder's span from
//      the two ends the row names — so none of it can be quietly hand-tuned;
//   2b. every constituent colour is a real colour in 0..1, which is what makes
//      "the shader cannot show a colour nobody authored" true rather than
//      merely intended;
//   3. a cover named by a ground row exists, and a bare ground has no density;
//   4. the one material id the shader hardcodes (the ploughed field's
//      north-south twin, whose axes it swaps) is still that field.
#include "check.h"

#include "sub/material.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace sm;

#ifndef TIMAERT_REPO_DIR
#error "ground_table_test needs TIMAERT_REPO_DIR (set in CMakeLists.txt)"
#endif

std::string slurp(const char* rel) {
    const std::string path = std::string(TIMAERT_REPO_DIR) + "/" + rel;
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// ── CSV ─────────────────────────────────────────────────────────────────────
// Comment lines ('#') are skipped and double-quoted fields may contain the
// separator — the notes do, and an unquoted comma there used to eat the tail
// of the note silently.
std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (char c : line) {
        if (c == '"') { quoted = !quoted; continue; }
        if (c == ',' && !quoted) { out.push_back(cur); cur.clear(); continue; }
        cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

struct Csv {
    std::vector<std::string> head;
    std::vector<std::vector<std::string>> rows;

    int col(const std::string& name) const {
        for (std::size_t i = 0; i < head.size(); ++i)
            if (head[i] == name) return int(i);
        return -1;
    }
    const std::string& at(std::size_t r, const std::string& name) const {
        static const std::string kEmpty;
        const int c = col(name);
        if (c < 0 || std::size_t(c) >= rows[r].size()) return kEmpty;
        return rows[r][std::size_t(c)];
    }
    double num(std::size_t r, const std::string& name) const {
        const std::string& s = at(r, name);
        return s.empty() ? 0.0 : std::stod(s);
    }
};

Csv read_csv(const char* rel) {
    Csv csv;
    std::istringstream in(slurp(rel));
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (csv.head.empty()) csv.head = split_csv(line);
        else csv.rows.push_back(split_csv(line));
    }
    return csv;
}

// ── GLSL ────────────────────────────────────────────────────────────────────
// Comments are stripped first: they carry the row's numbers in prose, and a
// number scraper that read them would compare the table against its own
// documentation.
std::string strip_comments(const std::string& src) {
    std::string out;
    out.reserve(src.size());
    for (std::size_t i = 0; i < src.size(); ++i) {
        if (src[i] == '/' && i + 1 < src.size() && src[i + 1] == '/') {
            while (i < src.size() && src[i] != '\n') ++i;
        }
        if (i < src.size()) out.push_back(src[i]);
    }
    return out;
}

// Every numeric literal inside the initialiser of `name`, in order.
std::vector<double> glsl_array(const std::string& src, const std::string& name) {
    std::vector<double> out;
    const std::size_t decl = src.find(" " + name + "[");
    if (decl == std::string::npos) return out;
    const std::size_t eq = src.find('=', decl);
    if (eq == std::string::npos) return out;
    const std::size_t open = src.find('(', eq);
    if (open == std::string::npos) return out;
    int depth = 0;
    for (std::size_t i = open; i < src.size(); ++i) {
        const char c = src[i];
        if (c == '(') { ++depth; continue; }
        if (c == ')') { if (--depth == 0) break; continue; }
        // A digit that follows a letter belongs to an identifier, not to the
        // data: the elements are spelled `vec3(...)` and `vec4(...)`, and a
        // scraper that took their 3 and 4 for values read the type name as a
        // row.
        const char prev = i > 0 ? src[i - 1] : ' ';
        const bool inWord = (prev >= 'a' && prev <= 'z')
                            || (prev >= 'A' && prev <= 'Z') || prev == '_';
        if (!inWord
            && ((c >= '0' && c <= '9') || c == '-'
                || (c == '.' && i + 1 < src.size() && src[i + 1] >= '0'
                    && src[i + 1] <= '9'))) {
            std::size_t j = i;
            while (j < src.size()
                   && ((src[j] >= '0' && src[j] <= '9') || src[j] == '.'
                       || src[j] == '-' || src[j] == 'e' || src[j] == '+'))
                ++j;
            out.push_back(std::stod(src.substr(i, j - i)));
            i = j - 1;
        }
    }
    return out;
}

// A scalar `const <type> kName = <n>u?;`
double glsl_scalar(const std::string& src, const std::string& name) {
    const std::size_t at = src.find(" " + name + " ");
    if (at == std::string::npos) return -1.0;
    const std::size_t eq = src.find('=', at);
    if (eq == std::string::npos) return -1.0;
    return std::stod(src.substr(eq + 1));
}

bool near_eq(double a, double b) { return std::fabs(a - b) < 1e-4; }

// The coarse end of every ladder, in cycles per metre — one patch per ~28 m,
// the scale at which ground reads as TERRAIN rather than as surface. Mirrors
// tools/gen_ground_table.py LADDER_FLOOR_FREQ; it is the one ladder number the
// generator does NOT emit (nothing in the shader reads it), so it is spelled
// here to keep the re-derivation honest.
const double kLadderFloorFreq = 0.035;

const char* kFamilies[] = {"soil", "turf", "sand", "furrow",
                           "stone", "track", "mud"};

void check_array(const std::vector<double>& got,
                 const std::vector<double>& want, const char* what) {
    CHECK(got.size() == want.size(), what);
    if (got.size() != want.size()) {
        std::fprintf(stderr, "    %s: GLSL has %zu numbers, the CSV says %zu\n",
                     what, got.size(), want.size());
        return;
    }
    bool ok = true;
    for (std::size_t i = 0; i < got.size(); ++i) {
        if (near_eq(got[i], want[i])) continue;
        ok = false;
        std::fprintf(stderr,
                     "    %s: element %zu is %.5f, the CSV derives %.5f\n",
                     what, i, got[i], want[i]);
    }
    CHECK(ok, what);
}

void test_generated_table_matches_the_csvs() {
    const Csv mats = read_csv("data/ground_materials.csv");
    const Csv covers = read_csv("data/ground_cover.csv");
    const std::string glsl =
        strip_comments(slurp("shaders/ground_surface.glsl"));

    CHECK(!mats.rows.empty(), "data/ground_materials.csv has rows");
    CHECK(!covers.rows.empty(), "data/ground_cover.csv has rows");
    CHECK(!glsl.empty(), "shaders/ground_surface.glsl is readable");
    if (mats.rows.empty() || covers.rows.empty() || glsl.empty()) return;

    // 1. The ordinal IS the id. A row that drifted off its index would repaint
    //    the world: the material texture carries the byte, nothing else.
    bool ordinals = true;
    for (std::size_t i = 0; i < mats.rows.size(); ++i)
        if (int(mats.num(i, "id")) != int(i)) ordinals = false;
    for (std::size_t i = 0; i < covers.rows.size(); ++i)
        if (int(covers.num(i, "id")) != int(i)) ordinals = false;
    CHECK(ordinals, "every row of both CSVs stands at its own ordinal");

    // ...and the id space is the C++ one. sub/material.h is the authority for
    // what the renderer writes into the material texture; a table shorter than
    // that enum would clamp real ground to the wrong row.
    CHECK(mats.rows.size() == std::size_t(sub::TM_FieldV) + 1,
          "the table has exactly one row per TerrainMaterial id");
    CHECK(near_eq(glsl_scalar(glsl, "kGroundCount"),
                  double(mats.rows.size())),
          "kGroundCount is the CSV's row count");
    CHECK(near_eq(glsl_scalar(glsl, "kCoverCount"),
                  double(covers.rows.size())),
          "kCoverCount is the cover CSV's row count");

    // 2. Every number, re-derived.
    std::vector<double> fresh, worn, family, surface, spread, ladder,
        edge, damp, cover;
    // THE BOUND the whole colour model rests on: every colour the shader can
    // show lies on the segment between a row's two constituents, so a ladder
    // that cannot leave 0..1 cannot leave the palette. Nothing downstream can
    // restore that guarantee if a constituent is out of range, which is why
    // it is checked here rather than trusted.
    bool constituentsSane = true;
    // The ladder's two shared constants come from the GLSL, so the span and
    // the normalisers below are re-derived against whatever the generator
    // actually emitted — a hand-edit to either constant is then caught by the
    // arrays disagreeing, not merely by the constant looking different.
    const double lac = glsl_scalar(glsl, "kLadderLacunarity");
    const double gain = glsl_scalar(glsl, "kLadderGain");
    CHECK(lac > 1.0 && gain > 0.0 && gain < 1.0,
          "the ladder's lacunarity is a real step and its gain a real decay");
    bool famKnown = true, coverKnown = true, bareIsBare = true;
    for (std::size_t i = 0; i < mats.rows.size(); ++i) {
        // The two constituents. Their midpoint — what the mix settles to at
        // range — is documentation on the row's header line, not an array:
        // nothing reads it, and a table that carries numbers nobody reads is
        // how a reader learns to distrust the ones that matter.
        for (const char* ch : {"r", "g", "b"}) {
            const double f = mats.num(i, std::string("fresh_") + ch);
            const double w = mats.num(i, std::string("worn_") + ch);
            if (f < 0.0 || f > 1.0 || w < 0.0 || w > 1.0)
                constituentsSane = false;
            fresh.push_back(f);
            worn.push_back(w);
        }

        int fam = -1;
        for (int f = 0; f < int(sizeof(kFamilies) / sizeof(kFamilies[0])); ++f)
            if (mats.at(i, "family") == kFamilies[f]) fam = f;
        if (fam < 0) { famKnown = false; fam = 0; }
        family.push_back(double(fam));

        surface.push_back(1.0 / mats.num(i, "meso_m"));
        surface.push_back(mats.num(i, "relief_m"));

        spread.push_back(mats.num(i, "sd"));
        spread.push_back(mats.num(i, "macro_sd"));

        // THE LADDER, re-derived exactly as tools/gen_ground_table.py does it:
        // the span is the two ends the row already names — the patchwork floor
        // and the row's own grain — snapped to whole octaves around its meso
        // frequency, and the normalisers are the full-resolution standard
        // deviations of each half. Nothing here is authored, so what this pins
        // is that the shader's octaves still follow the CSV's wavelengths: an
        // edit to meso_m or micro_m MUST move the ladder, and a hand-tuned
        // ladder that no longer matches them is the drift this test exists for.
        const double mesoF = 1.0 / mats.num(i, "meso_m");
        const double grainF = 1.0 / mats.num(i, "micro_m");
        const double iLo = std::min(
            std::round(std::log(kLadderFloorFreq / mesoF) / std::log(lac)), -1.0);
        const double iHi = std::max(
            std::round(std::log(grainF / mesoF) / std::log(lac)), 1.0);
        double coarse = 0.0, fine = 0.0;
        for (int k = int(iLo); k < 0; ++k) coarse += std::pow(gain, 2 * k);
        for (int k = 0; k <= int(iHi); ++k) fine += std::pow(gain, 2 * k);
        ladder.push_back(iLo);
        ladder.push_back(iHi);
        ladder.push_back(1.0 / std::sqrt(coarse));
        ladder.push_back(1.0 / std::sqrt(fine));

        edge.push_back(mats.num(i, "edge_m"));
        damp.push_back(mats.num(i, "damp"));

        // 3. A named cover must exist, and bare ground must be bare.
        int cid = -1;
        for (std::size_t c = 0; c < covers.rows.size(); ++c)
            if (covers.at(c, "name") == mats.at(i, "cover")) cid = int(c);
        if (cid < 0) { coverKnown = false; cid = 0; }
        const double density = mats.num(i, "cover_density");
        if (cid == 0 && density > 0.0) bareIsBare = false;
        cover.push_back(double(cid));
        cover.push_back(density);
    }
    CHECK(famKnown, "every ground names a family the generator knows");
    CHECK(coverKnown, "every ground names a cover row that exists");
    CHECK(bareIsBare, "bare ground carries no cover density");

    check_array(glsl_array(glsl, "kGroundFresh"), fresh, "kGroundFresh");
    check_array(glsl_array(glsl, "kGroundWorn"), worn, "kGroundWorn");
    check_array(glsl_array(glsl, "kGroundFamily"), family, "kGroundFamily");
    check_array(glsl_array(glsl, "kGroundSurface"), surface, "kGroundSurface");
    check_array(glsl_array(glsl, "kGroundSpread"), spread, "kGroundSpread");
    check_array(glsl_array(glsl, "kGroundLadder"), ladder, "kGroundLadder");
    check_array(glsl_array(glsl, "kGroundEdge"), edge, "kGroundEdge");
    check_array(glsl_array(glsl, "kGroundDamp"), damp, "kGroundDamp");
    check_array(glsl_array(glsl, "kGroundCover"), cover, "kGroundCover");

    // The cover wears the SAME two-constituent law, so it gets the same
    // guarantee and the same check.
    std::vector<double> cfresh, cworn, cpar;
    for (std::size_t i = 0; i < covers.rows.size(); ++i) {
        for (const char* ch : {"r", "g", "b"}) {
            const double f = covers.num(i, std::string("fresh_") + ch);
            const double w = covers.num(i, std::string("worn_") + ch);
            if (f < 0.0 || f > 1.0 || w < 0.0 || w > 1.0)
                constituentsSane = false;
            cfresh.push_back(f);
            cworn.push_back(w);
        }
        cpar.push_back(covers.num(i, "strand_per_m"));
        cpar.push_back(covers.num(i, "height_m"));
        cpar.push_back(covers.num(i, "wind"));
        cpar.push_back(covers.num(i, "sd"));
    }
    CHECK(constituentsSane,
          "every constituent colour, ground and cover, is in 0..1");
    check_array(glsl_array(glsl, "kCoverFresh"), cfresh, "kCoverFresh");
    check_array(glsl_array(glsl, "kCoverWorn"), cworn, "kCoverWorn");
    check_array(glsl_array(glsl, "kCoverParams"), cpar, "kCoverParams");

    // The family constants the shader dispatches on are emitted by the same
    // generator — if the order here and there ever parted, every ground would
    // wear the wrong shape.
    bool famOrder = true;
    for (int f = 0; f < int(sizeof(kFamilies) / sizeof(kFamilies[0])); ++f) {
        std::string name = "kGf";
        name += char(kFamilies[f][0] - 'a' + 'A');
        name += kFamilies[f] + 1;
        if (!near_eq(glsl_scalar(glsl, name), double(f))) famOrder = false;
    }
    CHECK(famOrder, "the kGf* family constants stand in the generator's order");
}

// The ONE material id mesh.frag names out loud: the ploughed field's
// north-south twin, whose axes it swaps to turn the furrows. Everything else
// in that shader is dispatched by family or read from the table, so this is
// the only place an id and a meaning can part company.
void test_the_one_hardcoded_id_still_means_what_the_shader_thinks() {
    const Csv mats = read_csv("data/ground_materials.csv");
    const std::string frag = slurp("shaders/mesh.frag");
    CHECK(!frag.empty(), "shaders/mesh.frag is readable");
    if (frag.empty() || mats.rows.size() < 15) return;
    CHECK(frag.find("mid == 14u") != std::string::npos,
          "mesh.frag still turns the field by id");
    CHECK(mats.at(14, "name") == "field_v",
          "id 14 is the north-south ploughed field the shader swaps");
    CHECK(int(sub::TM_FieldV) == 14,
          "...and sub/material.h agrees that is where it lives");
    // The twin is the SAME field: one look, two orientations. If somebody
    // tunes id 9 and forgets 14, half the world's fields change and half do
    // not — and only on cells whose plough happened to run the other way.
    bool twins = true;
    for (const char* col : {"family", "fresh_r", "fresh_g", "fresh_b",
                            "worn_r", "worn_g", "worn_b", "sd", "macro_sd",
                            "meso_m", "micro_m", "relief_m", "edge_m", "damp",
                            "cover", "cover_density"}) {
        if (mats.at(9, col) != mats.at(14, col)) {
            twins = false;
            std::fprintf(stderr,
                         "    field twin drift: %s is %s on id 9 and %s on 14\n",
                         col, mats.at(9, col).c_str(),
                         mats.at(14, col).c_str());
        }
    }
    CHECK(twins, "the two ploughed fields differ only in orientation");
}

} // namespace

int main() {
    test_generated_table_matches_the_csvs();
    test_the_one_hardcoded_id_still_means_what_the_shader_thinks();
    return sm::test::report("ground_table_test");
}
