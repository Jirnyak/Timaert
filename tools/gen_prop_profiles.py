#!/usr/bin/env python3
"""Generate the prop-profile table into BOTH sides from one CSV.

    python3 tools/gen_prop_profiles.py

data/prop_profiles.csv is the single source. It emits:
  * shaders/prop_profiles.glsl — the rings, read by the vertex stage that
    builds the body;
  * src/tables/prop_profiles.h — the same rows for the CPU, which needs only
    two things from them: how many vertices a draw of this profile takes, and
    where the bark ends.

TWO OUTPUTS, ONE SOURCE, AND THAT IS THE WHOLE POINT. The vertex count is a
FUNCTION of segments and rings, so if the shader held the rings and the engine
held the count by hand, the two would be one careless edit away from drawing a
shape with the wrong number of vertices — the silent kind of wrong, where the
last ring of every tree in the world simply does not appear. Derived from the
same row, they cannot disagree; `prop_profile_test` re-derives both files and
fails on drift, the same guard ground_surface.glsl has.

Both generated files are committed, so the build needs no Python.
"""

import csv
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV_PATH = os.path.join(REPO, "data", "prop_profiles.csv")
OUT_GLSL = os.path.join(REPO, "shaders", "prop_profiles.glsl")
OUT_HPP = os.path.join(REPO, "src", "tables", "prop_profiles.h")

# A profile may not carry more rings than this. It is a CAP, not a guess: the
# GLSL table is a flat const array and the vertex stage indexes it with a
# runtime profile id, so the stride has to be a compile-time constant. Six
# rings is two more than the richest row today — raise it here and both files
# follow.
MAX_RINGS = 6

# Same reasoning for the foliage tiers: a flat const array needs a constant
# stride, and four tiers is one more than the richest row today.
MAX_TIERS = 4


def die(msg):
    sys.stderr.write("gen_prop_profiles: %s\n" % msg)
    sys.exit(1)


def read_rows():
    with open(CSV_PATH, newline="", encoding="utf-8") as f:
        lines = [ln for ln in f if not ln.lstrip().startswith("#")]
    rows = list(csv.DictReader(lines))
    out = []
    for i, row in enumerate(rows):
        if int(row["id"]) != i:
            die("row %d carries id %s — ids are ordinals, append-only"
                % (i, row["id"]))
        segments = int(row["segments"])
        if segments < 3 or segments > 32:
            die("row %d: %d segments — a body of revolution needs 3..32"
                % (i, segments))
        rings = []
        for part in row["rings"].split(";"):
            r, _, y = part.strip().partition("@")
            rings.append((float(r), float(y)))
        if len(rings) < 2:
            die("row %d: a profile needs at least two rings" % i)
        if len(rings) > MAX_RINGS:
            die("row %d: %d rings over the cap of %d — raise MAX_RINGS"
                % (i, len(rings), MAX_RINGS))
        for k in range(1, len(rings)):
            if rings[k][1] <= rings[k - 1][1]:
                die("row %d: ring %d does not stand above the one below it "
                    "(y %.3f after %.3f)" % (i, k, rings[k][1],
                                             rings[k - 1][1]))
        if abs(rings[0][1]) > 1e-6 or abs(rings[-1][1] - 1.0) > 1e-6:
            die("row %d: a profile spans the prop exactly — first ring at "
                "y 0, last at y 1" % i)
        bark = float(row["bark_top"])
        if not 0.0 <= bark <= 1.0:
            die("row %d: bark_top %.3f outside 0..1" % (i, bark))
        planes = int(row["leaf_planes"])
        if planes < 0 or planes > 8:
            die("row %d: %d leaf planes — 0..8" % (i, planes))
        tiers = []
        cell = row["leaves"].strip()
        if cell:
            for part in cell.split(";"):
                y, _, size = part.strip().partition("@")
                tiers.append((float(y), float(size)))
        if len(tiers) > MAX_TIERS:
            die("row %d: %d leaf tiers over the cap of %d" % (i, len(tiers),
                                                              MAX_TIERS))
        for k in range(1, len(tiers)):
            if tiers[k][0] <= tiers[k - 1][0]:
                die("row %d: leaf tier %d does not stand above the one below"
                    % (i, k))
        if planes == 0 and tiers:
            die("row %d: tiers with no planes to stand on" % i)
        bark_rgb = tuple(float(row["bark_" + c]) for c in "rgb")
        for v in bark_rgb:
            if not 0.0 <= v <= 1.0:
                die("row %d: bark colour %.3f outside 0..1" % (i, v))
        out.append({"name": row["name"], "segments": segments,
                    "rings": rings, "bark": bark, "barkRgb": bark_rgb,
                    "planes": planes, "tiers": tiers, "note": row["note"]})
    return out


def vertex_count(row):
    """Trunk quads + cap, then six vertices per foliage card.

    One draw covers the whole prop — trunk AND leaves — because a tree is one
    thing and splitting it into two passes would make every tree two draws and
    two chances to disagree about where it stands.
    """
    trunk = row["segments"] * 6 * (len(row["rings"]) - 1) + row["segments"] * 3
    leaves = row["planes"] * len(row["tiers"]) * 6
    return trunk + leaves


def wrap(text, width, indent):
    out, line = [], indent
    for word in text.split():
        if len(line) + len(word) + 1 > width and line != indent:
            out.append(line)
            line = indent
        line += ("" if line == indent else " ") + word
    if line != indent:
        out.append(line)
    return out


def emit_glsl(rows):
    o = ["// GENERATED by tools/gen_prop_profiles.py from",
         "// data/prop_profiles.csv — do not hand-edit. Edit the CSV and",
         "// re-run; `prop_profile_test` re-derives this file and fails on",
         "// drift.",
         "//",
         "// A profile is a BODY OF REVOLUTION given by rings: `segments`",
         "// sides around the axis, and for each ring the radius it stands at",
         "// (fraction of half-width) and the height it stands on (fraction of",
         "// height). The vertex stage builds quads between neighbouring rings",
         "// and closes the top with a fan; a ring of radius 0 collapses its",
         "// quads into triangles on its own, so a cone needs no branch.",
         "#ifndef TIMAERT_PROP_PROFILES",
         "#define TIMAERT_PROP_PROFILES",
         ""]
    o.append("const uint kProfileCount = %du;" % len(rows))
    o.append("const uint kProfileMaxRings = %du;" % MAX_RINGS)
    o.append("")
    for i, r in enumerate(rows):
        o.append("// %d %s — %d segments, %d rings, %d vertices"
                 % (i, r["name"], r["segments"], len(r["rings"]),
                    vertex_count(r)))
        o.extend(wrap(r["note"], 74, "//    "))
    o.append("")
    o.append("// Sides around the axis.")
    o.append("const uint kProfileSegments[%d] = uint[%d](" % (len(rows), len(rows)))
    for i, r in enumerate(rows):
        o.append("    %du%s  // %d %s" % (r["segments"],
                                          "," if i + 1 < len(rows) else " ",
                                          i, r["name"]))
    o.append(");")
    o.append("")
    o.append("// How many rings each profile actually uses.")
    o.append("const uint kProfileRingCount[%d] = uint[%d]("
             % (len(rows), len(rows)))
    for i, r in enumerate(rows):
        o.append("    %du%s  // %d %s" % (len(r["rings"]),
                                          "," if i + 1 < len(rows) else " ",
                                          i, r["name"]))
    o.append(");")
    o.append("")
    o.append("// Where the trunk ends and the crown begins, as a height")
    o.append("// fraction — one number instead of a second material, the same")
    o.append("// way a house splits wall from roof by vLocalY.")
    o.append("const float kProfileBarkTop[%d] = float[%d]("
             % (len(rows), len(rows)))
    for i, r in enumerate(rows):
        o.append("    %.5f%s  // %d %s" % (r["bark"],
                                           "," if i + 1 < len(rows) else " ",
                                           i, r["name"]))
    o.append(");")
    o.append("")
    o.append("// The trunk's own colour. The CROWN's is not here: it is the")
    o.append("// cover row `canopy` (ground_surface.glsl), which the far sheet")
    o.append("// reads too — one wood seen at two distances, one colour.")
    o.append("const vec3 kProfileBark[%d] = vec3[%d]("
             % (len(rows), len(rows)))
    for i, r in enumerate(rows):
        o.append("    vec3(%.5f, %.5f, %.5f)%s  // %d %s"
                 % (r["barkRgb"][0], r["barkRgb"][1], r["barkRgb"][2],
                    "," if i + 1 < len(rows) else " ", i, r["name"]))
    o.append(");")
    o.append("")
    o.append("// FOLIAGE: how many crossed cards stand at each tier, and")
    o.append("// where the tiers are. Leaves are CARDS rather than a body of")
    o.append("// revolution because foliage is not a surface — a cone of")
    o.append("// revolution reads as a toy, which is exactly the look this")
    o.append("// table replaced.")
    o.append("const uint kProfileLeafPlanes[%d] = uint[%d]("
             % (len(rows), len(rows)))
    for i, r in enumerate(rows):
        o.append("    %du%s  // %d %s" % (r["planes"],
                                          "," if i + 1 < len(rows) else " ",
                                          i, r["name"]))
    o.append(");")
    o.append("")
    o.append("const uint kProfileLeafTiers[%d] = uint[%d]("
             % (len(rows), len(rows)))
    for i, r in enumerate(rows):
        o.append("    %du%s  // %d %s" % (len(r["tiers"]),
                                          "," if i + 1 < len(rows) else " ",
                                          i, r["name"]))
    o.append(");")
    o.append("")
    o.append("const uint kProfileMaxTiers = %du;" % MAX_TIERS)
    o.append("// x = height fraction of the tier, y = its half-width as a")
    o.append("// fraction of the prop's. Padded to the stride, last repeated.")
    o.append("const vec2 kProfileLeaves[%d] = vec2[%d]("
             % (len(rows) * MAX_TIERS, len(rows) * MAX_TIERS))
    lcells = []
    for i, r in enumerate(rows):
        pad = r["tiers"] + [r["tiers"][-1] if r["tiers"] else (0.0, 0.0)] \
            * (MAX_TIERS - len(r["tiers"]))
        for k, (y, size) in enumerate(pad):
            lcells.append(("    vec2(%.5f, %.5f)" % (y, size),
                           "%d %s tier %d" % (i, r["name"], k)))
    for n, (text, label) in enumerate(lcells):
        o.append("%s%s  // %s" % (text, "," if n + 1 < len(lcells) else " ",
                                  label))
    o.append(");")
    o.append("")
    o.append("// The rings themselves, PADDED to the cap so the stride is a")
    o.append("// compile-time constant: profile p, ring k is index")
    o.append("// p * kProfileMaxRings + k. x = radius fraction, y = height")
    o.append("// fraction. Padding repeats the last ring, so an over-read")
    o.append("// degenerates instead of reaching into the next profile.")
    o.append("const vec2 kProfileRings[%d] = vec2[%d]("
             % (len(rows) * MAX_RINGS, len(rows) * MAX_RINGS))
    cells = []
    for i, r in enumerate(rows):
        padded = r["rings"] + [r["rings"][-1]] * (MAX_RINGS - len(r["rings"]))
        for k, (rad, y) in enumerate(padded):
            cells.append(("    vec2(%.5f, %.5f)" % (rad, y),
                          "%d %s ring %d" % (i, r["name"], k)))
    for n, (text, label) in enumerate(cells):
        o.append("%s%s  // %s" % (text, "," if n + 1 < len(cells) else " ",
                                  label))
    o.append(");")
    o.append("")
    o.append("#endif // TIMAERT_PROP_PROFILES")
    with open(OUT_GLSL, "w", encoding="utf-8") as f:
        f.write("\n".join(o) + "\n")


def emit_hpp(rows):
    o = ["// GENERATED by tools/gen_prop_profiles.py from",
         "// data/prop_profiles.csv — do not hand-edit. Edit the CSV and",
         "// re-run; `prop_profile_test` re-derives this file and fails on",
         "// drift.",
         "//",
         "// The CPU half of the profile table. The engine does not build the",
         "// body — the vertex stage does, out of shaders/prop_profiles.glsl —",
         "// so what it needs from a row is exactly two things: how many",
         "// vertices one draw of this profile takes, and (for the renderer's",
         "// own bookkeeping) what the row is called. Both are DERIVED from",
         "// the same CSV line the shader reads, which is why they cannot",
         "// drift apart: a hand-kept vertex count would be one careless edit",
         "// away from dropping the last ring of every tree in the world, and",
         "// nothing would fail to compile.",
         "#pragma once",
         "",
         "#include <cstdint>",
         "",
         "namespace sm {",
         ""]
    o.append("enum class PropProfile : std::uint8_t {")
    for i, r in enumerate(rows):
        o.append("    %s = %d," % (r["name"].title().replace("_", ""), i))
    o.append("};")
    o.append("")
    o.append("inline constexpr int kPropProfileCount = %d;" % len(rows))
    o.append("")
    o.append("// Vertices one draw of this profile takes: `segments` quads")
    o.append("// between every pair of rings, plus a fan cap on top.")
    o.append("inline constexpr std::uint32_t kPropProfileVertices["
             "kPropProfileCount] = {")
    for i, r in enumerate(rows):
        o.append("    %du,  // %d %s — %d segments × %d ring gaps + cap"
                 % (vertex_count(r), i, r["name"], r["segments"],
                    len(r["rings"]) - 1))
    o.append("};")
    o.append("")
    o.append("} // namespace sm")
    with open(OUT_HPP, "w", encoding="utf-8") as f:
        f.write("\n".join(o) + "\n")


def main():
    rows = read_rows()
    if not rows:
        die("the CSV came back empty — is the path right?")
    emit_glsl(rows)
    emit_hpp(rows)
    sys.stderr.write("gen_prop_profiles: %d profiles -> %s, %s\n"
                     % (len(rows), os.path.relpath(OUT_GLSL, REPO),
                        os.path.relpath(OUT_HPP, REPO)))


if __name__ == "__main__":
    main()
