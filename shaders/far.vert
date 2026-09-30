#version 450
// THE FAR WORLD'S GROUND (CANON S18.1) — vertex stage.
//
// The same world-space the composite lives in (metres, Y absolute), so the far
// sheet and the near mesh need no second basis and the camera is one camera.
// It carries no UV: there is no tile material grid out here, only the cell's
// MATERIAL ID, and the fragment reads the ground table with it.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in float inMaterial;

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 sunDir;
    vec4 sunColor;
    vec4 ambient;
    mat4 lightMvp;
} pc;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vWorld;
// FLAT, and it is the whole fix. A material id is an ORDINAL into a table, so
// interpolating it manufactures rows nobody authored: between meadow(4) and
// rock(11) lie desert(6) and steppe(7), both sandy-yellow, and between
// meadow(4) and waterbed(13) lies shore(10) as well. That is the yellow thread
// the owner photographed along every far biome border and every far coastline
// — confirmed 2026-09-30 by painting fractional ordinals magenta in a live
// run: the threads went magenta, all of them.
//
// The law was already written down one file away (far_mesh.h: «the average of
// two ordinals is a third material nobody authored») and the vertex stage even
// obeys it — it picks the NEAREST cell's id. It was the interpolator, which
// nobody had told, that broke it. Prose is not a mechanism; `flat` is.
layout(location = 2) flat out float vMaterial;

void main() {
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    vNormal = inNormal;
    vWorld = inPos;
    vMaterial = inMaterial;
}
