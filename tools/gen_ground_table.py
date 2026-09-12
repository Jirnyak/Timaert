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

CALIBRATION. `cv` is a coefficient of variation of luminance (lum_std / mean,
linear), not a taste knob: the multiplicative variation the shader applies is
the mean-preserving lognormal exp(sigma*z - sigma^2/2), whose CV is
sqrt(exp(sigma^2) - 1). Inverting gives the sigma this generator emits:

    sigma = sqrt(ln(1 + cv^2))

so "how blotchy" is quoted in one unit that is comparable across materials and
measurable off a photograph, exactly as the reference project calibrates its
material set (measured anchors: smooth concrete 0.08, bare soil 0.22,
corroded metal 0.44).
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


def sigma_of_cv(cv):
    """The lognormal width that reproduces a coefficient of variation."""
    return math.sqrt(math.log(1.0 + cv * cv))


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

    names, fams, albedo, surf, micro, chroma, damp, cover, notes = (
        [], [], [], [], [], [], [], [], [])
    macro, edge = [], []
    heads = []
    for i, row in enumerate(mats):
        if int(row["id"]) != i:
            die("row %d carries id %s — ids are ordinals, append-only"
                % (i, row["id"]))
        fam = row["family"]
        if fam not in FAMILIES:
            die("row %d: unknown family %r (known: %s)"
                % (i, fam, ", ".join(FAMILIES)))
        cv = fnum(row, "cv", i, 0.0, 2.0)
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
        albedo.append("vec3(%.5f, %.5f, %.5f)"
                      % (fnum(row, "albedo_r", i, 0.0, 1.0),
                         fnum(row, "albedo_g", i, 0.0, 1.0),
                         fnum(row, "albedo_b", i, 0.0, 1.0)))
        # x = lognormal sigma, y = meso frequency (cycles/m), z = chroma sigma,
        # w = relief height (m) — the bump scale.
        surf.append("vec4(%.5f, %.5f, %.5f, %.5f)"
                    % (sigma_of_cv(cv), 1.0 / meso_m,
                       fnum(row, "chroma_sigma", i, 0.0, 1.0),
                       fnum(row, "relief_m", i, 0.0, 4.0)))
        macro.append("%.5f" % sigma_of_cv(fnum(row, "macro_cv", i, 0.0, 2.0)))
        edge.append("%.5f" % fnum(row, "edge_m", i, 0.0, 8.0))
        micro.append("%.5f" % (1.0 / micro_m))
        damp.append("%.5f" % fnum(row, "damp", i, 0.0, 1.0))
        chroma.append("vec3(%.5f, %.5f, %.5f)"
                      % (fnum(row, "chroma_r", i, 0.0, 4.0),
                         fnum(row, "chroma_g", i, 0.0, 4.0),
                         fnum(row, "chroma_b", i, 0.0, 4.0)))
        cover.append("vec2(%.5f, %.5f)" % (float(cid), density))
        heads.append("// %2d %-9s %-7s CV %.2f/%.2f  meso %.2f m  grain %.2f m  "
                     "relief %.3f m  cover %s %.2f"
                     % (i, row["name"], fam, cv,
                        fnum(row, "macro_cv", i), meso_m, micro_m,
                        fnum(row, "relief_m", i), cname, density))

    cnames, ccol, cpar, cnotes = [], [], [], []
    for i, row in enumerate(covers):
        cnames.append(row["name"])
        cnotes.append(row["note"])
        ccol.append("vec3(%.5f, %.5f, %.5f)"
                    % (fnum(row, "colour_r", i, 0.0, 1.0),
                       fnum(row, "colour_g", i, 0.0, 1.0),
                       fnum(row, "colour_b", i, 0.0, 1.0)))
        # x = strands per metre, y = layer height (m), z = wind response,
        # w = lognormal sigma of the cover's own luminance.
        cpar.append("vec4(%.5f, %.5f, %.5f, %.5f)"
                    % (fnum(row, "strand_per_m", i, 0.0, 256.0),
                       fnum(row, "height_m", i, 0.0, 4.0),
                       fnum(row, "wind", i, 0.0, 4.0),
                       sigma_of_cv(fnum(row, "cv", i, 0.0, 2.0))))

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
    o.append("// Mean linear colour of the unlit ground.")
    o.append("const vec3 kGroundAlbedo[%d] = vec3[%d](" % (len(mats), len(mats)))
    elements(o, albedo, names, "albedo")
    o.append(");")
    o.append("")
    o.append("// Which shape each ground wears.")
    o.append("const uint kGroundFamily[%d] = uint[%d](" % (len(mats), len(mats)))
    elements(o, fams, names, "family")
    o.append(");")
    o.append("")
    o.append("// x = lognormal sigma reproducing the row's target luminance CV,")
    o.append("// y = meso structure frequency (cycles per metre, = 1/meso_m),")
    o.append("// z = chroma sigma (lognormal width of the hue drift),")
    o.append("// w = relief height in METRES — the normal-perturbation scale.")
    o.append("const vec4 kGroundSurface[%d] = vec4[%d]("
             % (len(mats), len(mats)))
    elements(o, surf, names, "surface")
    o.append(");")
    o.append("")
    o.append("// Lognormal sigma of the TERRAIN-scale patchwork (from macro_cv).")
    o.append("// The band that survives to the horizon: past a couple of")
    o.append("// hundred metres the pixel footprint has eaten every finer one,")
    o.append("// and this is all that keeps a bare biome from being a flat")
    o.append("// plane of one colour.")
    o.append("const float kGroundMacroSigma[%d] = float[%d]("
             % (len(mats), len(mats)))
    elements(o, macro, names, "macro")
    o.append(");")
    o.append("")
    o.append("// Grain frequency in cycles per metre (= 1/micro_m).")
    o.append("const float kGroundGrainFreq[%d] = float[%d]("
             % (len(mats), len(mats)))
    elements(o, micro, names, "grain")
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
    o.append("// RGB axis the hue drift travels along (>1 warms a channel).")
    o.append("const vec3 kGroundChromaAxis[%d] = vec3[%d]("
             % (len(mats), len(mats)))
    elements(o, chroma, names, "chroma")
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
    o.append("// Mean linear colour of each cover layer.")
    o.append("const vec3 kCoverColour[%d] = vec3[%d]("
             % (len(covers), len(covers)))
    elements(o, ccol, cnames, "cover colour")
    o.append(");")
    o.append("")
    o.append("// x = strands per metre, y = layer height in metres,")
    o.append("// z = wind response, w = lognormal sigma of the cover's own")
    o.append("// luminance. The strand SLOPE — what tilts the normal — is")
    o.append("// the product y*x, never a fourth number to keep in step.")
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
