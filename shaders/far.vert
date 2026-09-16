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
layout(location = 2) out float vMaterial;

void main() {
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    vNormal = inNormal;
    vWorld = inPos;
    vMaterial = inMaterial;
}
