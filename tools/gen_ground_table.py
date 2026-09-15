#!/usr/bin/env python3
"""Generate shaders/ground_surface.glsl from the two ground CSVs.

    python3 tools/gen_ground_table.py

data/ground_materials.csv is one row per TerrainMaterial id (sub/material.h)
and data/ground_cover.csv is one row per cover layer. Between them they are the
single source of truth for how the subworld ground looks: colour, how blotchy,
at what wavelength, how deep the relief, and what grows on top. The shader
holds no numbers of its own — it holds the SHAPES (the families) and reads
every number from the generated table, so owner feedback is an edit to a CSV
cell and a re-run, never a shader rewrite.

The generated file is committed so the build needs no Python, and the
`ground_table_test` ctest re-derives the whole table from the CSVs and fails on
any drift — a hand-edit to either side is caught by the next `ctest`.

THE COLOUR MODEL, and the one thing to understand before editing either CSV:
a ground is a MIXTURE of two real constituents, and the procedural field
chooses the PROPORTION — never the brightness. `fresh` is what accumulates
(sward, lichen, soft snow, silt), `worn` is what exposure leaves (litter, bare
earth, scoured crust, polished crest), and every colour the shader can put on
screen lies on the segment between them.

That bound is the whole point. The model this replaced multiplied one authored
colour by a lognormal and drifted its hue along an authored vector, which could
— and did — manufacture olives and teals no material had. "Dirty" ground was
never too much texture; it was colours nobody chose. A mix cannot do that, by
construction, and no amount of retuning the old one could have promised it.

So this generator emits no sigmas and no hue axes. What it emits is the two
constituents, their MIDPOINT as the ground's mean colour (kGroundAlbedo, now
derived rather than authored beside numbers that could contradict it — at range
the mix settles to exactly this), and how far each half of the ladder swings
the mix.
"""

import csv
import math
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAT_CSV = os.path.join(REPO, "data", "ground_materials.csv")
COVER_CSV = os.path.join(REPO, "data", "ground_cover.csv")
OUT_GLSL = os.path.join(REPO, "shaders", "ground_surface.glsl")

# Procedural shape families. Must match the kGf* constants this generator
# emits — the shader gets them from here, so the two cannot drift.
FAMILIES = ["soil", "turf", "sand", "furrow", "stone", "track", "mud"]

# ── THE LADDER ──────────────────────────────────────────────────────────────
# A ground's colour is ONE field with a continuous spectrum, and these three
# numbers describe it. They are here rather than in the shader because the
# per-row octave span and the two normalisers below are DERIVED from them, and
# a constant whose derived values live in another file is a constant that
# drifts.
#
# Why a continuous spectrum at all: the synth used to have three named bands —
# a 28 m patchwork, the family's ~1 m structure, a 4 cm grain — with nothing in
# between. Measured on the meadow row that is content at 28.6, 7.1, 0.80, 0.30,
# 0.040 and 0.011 m and two holes almost a decade wide. Past the range where
# the pixel footprint has eaten the family band, the ONLY thing left was the
# 28 m patchwork: a single spatial frequency at full contrast, which is what a
# lava lamp is and what the owner named on the city screenshot (2026-09-14).
# Nature shows no single frequency; isolate one octave of any natural surface
# and it reads as blotches on paint rather than as ground.
#
# LACUNARITY 3 is the ratio this shader's own shapes already use (the families
# step by 2.7–4.0 and the grain by 3.73), and it spans the whole surface — the
# terrain patchwork down to the grain — in the six or seven octaves every row
# below turns out to need.
LADDER_LACUNARITY = 3.0

# GAIN — how much of its amplitude an octave keeps from the one below it.
# THE one knob for "how is the roughness distributed across scales", and the
# only taste number in the ladder:
#   lower  → the coarsest octave dominates; the far view goes back to blobs,
#   higher → more energy at the fine end; the near ground reads grainier.
# 0.75 grades the meadow's seven octaves at 53/30/17 % of the terrain half's
# variance and 49/27/15/9 % of the surface half's, so no single scale is ever
# more than about half of what the eye is looking at. For reference the three
# hand-authored splits it replaces, converted to this lacunarity, were 0.90
# (patchwork), 0.48 (family) and 0.67 (grain) — this sits among them.
LADDER_GAIN = 0.75

# Where the ladder STOPS at the coarse end, in cycles per metre: one patch per
# ~28 m. That is the scale at which ground reads as TERRAIN (a dry hollow, a
# mossier slope) rather than as surface, and it is the number the accepted
# macro band already used (mesh.frag kMacroFreq). Coarser than this the
# variation is the material MAP's job — a different biome, a field, a road —
# and the map is a texture that filters itself.
LADDER_FLOOR_FREQ = 0.035


def ladder_span(meso_freq, grain_freq):
    """The octave indices this row's ladder runs between, and its normalisers.

    Octave i sits at meso_freq * LACUNARITY^i and carries amplitude GAIN^i, so
    index 0 IS the row's own meso frequency — the octave the family SHAPE
    stands in for. Anchoring the amplitude there (rather than at the coarse
    end) is what keeps a family's character equally strong on every material,
    whatever span its numbers ask for.

    The span is not authored: it is the two ends the row already names — the
    patchwork floor above and the row's own grain — snapped to the nearest
    whole octave. So a ladder cannot drift from the CSV, and adding a material
    cannot forget to describe it.

    The two normalisers are 1/sqrt(sum of the squared amplitudes) over each
    half. They are constants of the MATERIAL, never of the fragment: dividing
    by the per-fragment weighted sum instead would renormalise the far view
    straight back to full contrast, which is the whole thing we are trying to
    lose.
    """
    def snap(ratio):
        return int(round(math.log(ratio) / math.log(LADDER_LACUNARITY)))

    # At least one octave on each side: a row whose grain sits inside its own
    # meso octave still gets a surface, and one whose meso is already at the
    # patchwork scale still gets a terrain half.
    i_lo = min(snap(LADDER_FLOOR_FREQ / meso_freq), -1)
    i_hi = max(snap(grain_freq / meso_freq), 1)
    coarse = sum(LADDER_GAIN ** (2 * i) for i in range(i_lo, 0))
    fine = sum(LADDER_GAIN ** (2 * i) for i in range(0, i_hi + 1))
    return i_lo, i_hi, 1.0 / math.sqrt(coarse), 1.0 / math.sqrt(fine)


def die(msg):
    sys.stderr.write("gen_ground_table: %s\n" % msg)
    sys.exit(1)


def read_csv(path):
    """Rows of a '#'-commented CSV, as dicts, in file order.

    A row with more fields than the header is a hard error, not a silent
    truncation: an unquoted comma inside a note used to eat the tail of that
    note, and the generated comment simply came out short.
    """
    with open(path, newline="", encoding="utf-8") as f:
        lines = [ln for ln in f if not ln.lstrip().startswith("#")]
    rows = list(csv.DictReader(lines))
    for i, row in enumerate(rows):
        if None in row:
            die("%s row %d has more fields than the header — quote the cell "
                "that contains a comma" % (os.path.basename(path), i))
        if any(v is None for v in row.values()):
            die("%s row %d is missing fields" % (os.path.basename(path), i))
    return rows


def fnum(row, col, i, lo=None, hi=None):
    try:
        v = float(row[col])
    except (KeyError, TypeError, ValueError):
        die("row %d: bad %s %r" % (i, col, row.get(col)))
    if lo is not None and v < lo:
        die("row %d: %s %s below %s" % (i, col, v, lo))
    if hi is not None and v > hi:
        die("row %d: %s %s above %s" % (i, col, v, hi))
    return v


def wrap(text, width, indent):
    """Comment body → lines of at most `width` columns, wrapped on spaces."""
    out, line = [], indent
    for word in text.split():
        if len(line) + len(word) + 1 > width and line != indent:
            out.append(line)
            line = indent
        line += ("" if line == indent else " ") + word
    if line != indent:
        out.append(line)
    return out


def elements(out, values, names, kind):
    """One array element per line, comma BEFORE the trailing name comment."""
    for i, (v, n) in enumerate(zip(values, names)):
        tail = "," if i + 1 < len(values) else " "
        out.append("    %s%s  // %2d %s" % (v, tail, i, n))


def main():
    mats = read_csv(MAT_CSV)
    covers = read_csv(COVER_CSV)
    if not mats or not covers:
        die("a CSV came back empty — is the path right?")

    cover_ix = {}
    for i, row in enumerate(covers):
        if int(row["id"]) != i:
            die("cover row %d carries id %s — ids are ordinals" % (i, row["id"]))
        cover_ix[row["name"]] = i

    names, fams, albedo, fresh, worn, surf, damp, cover, notes = (
        [], [], [], [], [], [], [], [], [])
    spread, edge, ladder = [], [], []
    heads = []
    for i, row in enumerate(mats):
        if int(row["id"]) != i:
            die("row %d carries id %s — ids are ordinals, append-only"
                % (i, row["id"]))
        fam = row["family"]
        if fam not in FAMILIES:
            die("row %d: unknown family %r (known: %s)"
                % (i, fam, ", ".join(FAMILIES)))
        sd = fnum(row, "sd", i, 0.0, 1.0)
        macro_sd = fnum(row, "macro_sd", i, 0.0, 1.0)
        meso_m = fnum(row, "meso_m", i, 0.05, 64.0)
        micro_m = fnum(row, "micro_m", i, 0.005, 1.0)
        cname = row["cover"]
        if cname != "none" and cname not in cover_ix:
            die("row %d: cover %r has no row in data/ground_cover.csv"
                % (i, cname))
        cid = cover_ix.get(cname, 0)
        density = fnum(row, "cover_density", i, 0.0, 1.0)
        if cid == 0 and density > 0.0:
            die("row %d: cover 'none' with density %s — say what grows first"
                % (i, density))

        names.append(row["name"])
        notes.append(row["note"])
        fams.append("%du" % FAMILIES.index(fam))
        fr = [fnum(row, "fresh_" + c, i, 0.0, 1.0) for c in "rgb"]
        wo = [fnum(row, "worn_" + c, i, 0.0, 1.0) for c in "rgb"]
        fresh.append("vec3(%.5f, %.5f, %.5f)" % tuple(fr))
        worn.append("vec3(%.5f, %.5f, %.5f)" % tuple(wo))
        # DERIVED, never authored: the mean colour of this ground is the
        # midpoint of what it is made of, and the mix settles there once the
        # pixel footprint has averaged the field away. Authoring it beside the
        # constituents would be a third number free to contradict the other two.
        # The MIDPOINT is what the mix settles to once the pixel footprint has
        # averaged the field away — "what this ground is at range". It is
        # documentation, not data: nothing reads it, so it rides the row's
        # header line below and no array carries it.
        albedo.append(tuple(0.5 * (a + b) for a, b in zip(fr, wo)))
        # x = meso frequency (cycles/m), y = relief height (m) — the bump
        # scale. It was a vec4 while the multiplicative model needed a sigma
        # and a hue width; those retired with it, and a struct that keeps dead
        # lanes "for padding" is how the next reader learns to distrust the
        # whole table.
        surf.append("vec2(%.5f, %.5f)"
                    % (1.0 / meso_m, fnum(row, "relief_m", i, 0.0, 4.0)))
        spread.append("vec2(%.5f, %.5f)" % (sd, macro_sd))
        edge.append("%.5f" % fnum(row, "edge_m", i, 0.0, 8.0))
        damp.append("%.5f" % fnum(row, "damp", i, 0.0, 1.0))
        cover.append("vec2(%.5f, %.5f)" % (float(cid), density))
        i_lo, i_hi, n_coarse, n_fine = ladder_span(1.0 / meso_m, 1.0 / micro_m)
        ladder.append("vec4(%5.1f, %4.1f, %.5f, %.5f)"
                      % (float(i_lo), float(i_hi), n_coarse, n_fine))
        heads.append("// %2d %-9s %-7s sd %.2f/%.2f  meso %.2f m  grain %.2f m  "
                     "relief %.3f m  cover %s %.2f  ladder %.1f m..%.0f mm  "
                     "mean %.2f/%.2f/%.2f"
                     % (i, row["name"], fam, sd, macro_sd, meso_m, micro_m,
                        fnum(row, "relief_m", i), cname, density,
                        meso_m * LADDER_LACUNARITY ** (-i_lo),
                        1000.0 * meso_m / LADDER_LACUNARITY ** i_hi,
                        albedo[-1][0], albedo[-1][1], albedo[-1][2]))

    cnames, cfresh, cworn, cpar, cnotes = [], [], [], [], []
    for i, row in enumerate(covers):
        cnames.append(row["name"])
        cnotes.append(row["note"])
        cfresh.append("vec3(%.5f, %.5f, %.5f)"
                      % tuple(fnum(row, "fresh_" + c, i, 0.0, 1.0)
                              for c in "rgb"))
        cworn.append("vec3(%.5f, %.5f, %.5f)"
                     % tuple(fnum(row, "worn_" + c, i, 0.0, 1.0)
                             for c in "rgb"))
        # x = strands per metre, y = layer height (m), z = wind response,
        # w = how far the strand field swings the COVERAGE.
        cpar.append("vec4(%.5f, %.5f, %.5f, %.5f)"
                    % (fnum(row, "strand_per_m", i, 0.0, 256.0),
                       fnum(row, "height_m", i, 0.0, 4.0),
                       fnum(row, "wind", i, 0.0, 4.0),
                       fnum(row, "sd", i, 0.0, 1.0)))

    o = []
    o.append("// GENERATED by tools/gen_ground_table.py from")
    o.append("// data/ground_materials.csv + data/ground_cover.csv — do not")
    o.append("// hand-edit. Edit the CSV and re-run it; the `ground_table_test`")
    o.append("// ctest re-derives this file from the CSVs and fails on drift.")
    o.append("//")
    o.append("// Included by shaders/mesh.frag, the one shader that draws the")
    o.append("// subworld ground. The shader holds the procedural SHAPES (the")
    o.append("// families below); every NUMBER lives here, from the CSV.")
    o.append("#ifndef TIMAERT_GROUND_SURFACE")
    o.append("#define TIMAERT_GROUND_SURFACE")
    o.append("")
    o.append("// Procedural shape families — the id space of the `family`")
    o.append("// column. A family is a SHAPE; materials sharing one differ")
    o.append("// only by the numbers in their row.")
    for i, f in enumerate(FAMILIES):
        o.append("const uint kGf%s = %du;" % (f.capitalize(), i))
    o.append("")
    o.append("// Row count. mesh.frag clamps the sampled material id against")
    o.append("// this: an out-of-range const-array read is undefined in GLSL,")
    o.append("// and the id arrives from a texture byte.")
    o.append("const uint kGroundCount = %du;" % len(mats))
    o.append("const uint kCoverCount = %du;" % len(covers))
    o.append("")
    for i, n in enumerate(names):
        o.append(heads[i])
        o.extend(wrap(notes[i], 76, "//    "))
    o.append("")
    o.append("// THE TWO CONSTITUENTS each ground is made of, as linear")
    o.append("// colours. `fresh` is what ACCUMULATES on it (sward, lichen,")
    o.append("// soft snow, the silt in a hollow); `worn` is what EXPOSURE")
    o.append("// leaves (litter and bare earth, scoured crust, the polished")
    o.append("// crest of a dune). The procedural field chooses the")
    o.append("// PROPORTION, never the brightness — so every colour the")
    o.append("// ground can show lies on the segment between these two, and")
    o.append("// the shader cannot manufacture one nobody authored. That")
    o.append("// bound is the law; see mesh.frag ground_of.")
    o.append("const vec3 kGroundFresh[%d] = vec3[%d](" % (len(mats), len(mats)))
    elements(o, fresh, names, "fresh")
    o.append(");")
    o.append("")
    o.append("const vec3 kGroundWorn[%d] = vec3[%d](" % (len(mats), len(mats)))
    elements(o, worn, names, "worn")
    o.append(");")
    o.append("")
    o.append("// Which shape each ground wears.")
    o.append("const uint kGroundFamily[%d] = uint[%d](" % (len(mats), len(mats)))
    elements(o, fams, names, "family")
    o.append(");")
    o.append("")
    o.append("// x = meso structure frequency (cycles per metre, = 1/meso_m),")
    o.append("// y = relief height in METRES — the normal-perturbation scale.")
    o.append("const vec2 kGroundSurface[%d] = vec2[%d]("
             % (len(mats), len(mats)))
    elements(o, surf, names, "surface")
    o.append(");")
    o.append("")
    o.append("// THE LADDER a ground's colour is built from: one field with a")
    o.append("// continuous spectrum, read as octave i at meso_freq *")
    o.append("// kLadderLacunarity^i carrying amplitude kLadderGain^i. Index 0")
    o.append("// IS the row's meso frequency — the octave the family SHAPE")
    o.append("// stands in for — so the two halves below are the TERRAIN's")
    o.append("// patchwork (i < 0, wearing macro_sd) and the SURFACE's own")
    o.append("// roughness (i >= 0, wearing sd). The span is derived from the")
    o.append("// row: it runs from the patchwork floor (%.3f cycles/m, one"
             % LADDER_FLOOR_FREQ)
    o.append("// patch per ~%.0f m) down to the row's own grain, snapped to"
             % (1.0 / LADDER_FLOOR_FREQ))
    o.append("// whole octaves. Nothing here is authored; see")
    o.append("// tools/gen_ground_table.py ladder_span.")
    o.append("//")
    o.append("// x = coarsest octave index, y = finest,")
    o.append("// z = unit-variance normaliser of the terrain half,")
    o.append("// w = unit-variance normaliser of the surface half. Both are")
    o.append("//     FULL-RESOLUTION constants: the ladder must LOSE contrast")
    o.append("//     with range, so it is never renormalised per fragment.")
    o.append("const float kLadderLacunarity = %.1f;" % LADDER_LACUNARITY)
    o.append("const float kLadderGain = %.2f;" % LADDER_GAIN)
    o.append("const vec4 kGroundLadder[%d] = vec4[%d](" % (len(mats), len(mats)))
    elements(o, ladder, names, "ladder")
    o.append(");")
    o.append("")
    o.append("// How far each half of the ladder swings the mix, as a standard")
    o.append("// deviation of the mix fraction (which lives in 0..1 about the")
    o.append("// midpoint). x = the surface half, at and above this ground's")
    o.append("// own meso frequency: how intermixed it is underfoot. y = the")
    o.append("// terrain half below it: how patchy it looks from a hillside.")
    o.append("// They ADD into ONE fraction, because \"how worn is this spot\"")
    o.append("// is a single fact measured at two scales — which is also why")
    o.append("// the cover reads the SAME fraction and a drier hollow gets")
    o.append("// paler soil AND paler grass with no second field to keep in")
    o.append("// step.")
    o.append("const vec2 kGroundSpread[%d] = vec2[%d]("
             % (len(mats), len(mats)))
    elements(o, spread, names, "spread")
    o.append(");")
    o.append("")
    o.append("// How far this ground's own margin wanders into its neighbour,")
    o.append("// in metres — the amplitude of mesh.frag's second material")
    o.append("// sample. A built thing (a road) keeps a small number and stays")
    o.append("// crisp; sand creeps with a large one.")
    o.append("const float kGroundEdge[%d] = float[%d]("
             % (len(mats), len(mats)))
    elements(o, edge, names, "edge")
    o.append(");")
    o.append("")
    o.append("// How much this ground darkens inside the shoreline height band.")
    o.append("const float kGroundDamp[%d] = float[%d]("
             % (len(mats), len(mats)))
    elements(o, damp, names, "damp")
    o.append(");")
    o.append("")

    o.append("// x = cover row id (0 = bare), y = density on this ground.")
    o.append("const vec2 kGroundCover[%d] = vec2[%d]("
             % (len(mats), len(mats)))
    elements(o, cover, names, "cover")
    o.append(");")
    o.append("")
    for i, n in enumerate(cnames):
        o.append("// cover %d %s" % (i, n))
        o.extend(wrap(cnotes[i], 76, "//    "))
    o.append("")
    o.append("// The SAME two-constituent law one layer up: a sward is green")
    o.append("// blades and the straw among them. Looked up with the GROUND's")
    o.append("// mix fraction, not one of its own — that is what makes a drier")
    o.append("// hollow carry paler soil and paler grass at once, structurally,")
    o.append("// with no multiply and no second field.")
    o.append("const vec3 kCoverFresh[%d] = vec3[%d]("
             % (len(covers), len(covers)))
    elements(o, cfresh, cnames, "cover fresh")
    o.append(");")
    o.append("")
    o.append("const vec3 kCoverWorn[%d] = vec3[%d]("
             % (len(covers), len(covers)))
    elements(o, cworn, cnames, "cover worn")
    o.append(");")
    o.append("")
    o.append("// x = strands per metre, y = layer height in metres,")
    o.append("// z = wind response, w = how far the strand field swings the")
    o.append("// COVERAGE. The strands decide how much ground shows between")
    o.append("// them, never what colour the blades are. The strand SLOPE —")
    o.append("// what tilts the normal — is the product y*x, never a fourth")
    o.append("// number to keep in step.")
    o.append("const vec4 kCoverParams[%d] = vec4[%d]("
             % (len(covers), len(covers)))
    elements(o, cpar, cnames, "cover params")
    o.append(");")
    o.append("")
    o.append("#endif // TIMAERT_GROUND_SURFACE")

    with open(OUT_GLSL, "w", encoding="utf-8") as f:
        f.write("\n".join(o) + "\n")
    sys.stderr.write("gen_ground_table: %d grounds, %d covers -> %s\n"
                     % (len(mats), len(covers),
                        os.path.relpath(OUT_GLSL, REPO)))


if __name__ == "__main__":
    main()
