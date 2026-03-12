// Vertex shader for full-screen quad
export const quadVertexShader = `#version 300 es
in vec2 a_position;
out vec2 v_uv;

void main() {
    v_uv = a_position * 0.5 + 0.5;
    gl_Position = vec4(a_position, 0.0, 1.0);
}
`;

// Master terrain generation shader
export const mainTerrainShader = `#version 300 es
precision highp float;

in vec2 v_uv;
out vec4 fragColor;

uniform sampler2D u_maskTexture;
uniform float u_seed;
uniform float u_heightScale;
uniform float u_moistureScale;
uniform float u_temperatureVariation;
uniform float u_tempMin;
uniform float u_tempMax;
uniform float u_roadFlattenHeight;
uniform float u_heightOctaves;
uniform float u_moistureOctaves;
uniform float u_domainWarp;
uniform float u_seaLevel;
uniform float u_roadWarpIntensity;
uniform float u_settlementBlur;
uniform float u_continentScale;
uniform float u_continentIntensity;
uniform float u_ridgeIntensity;

// Permutation table
const int perm[512] = int[512](
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
    8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,
    117,35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,
    165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,
    105,92,41,55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,
    187,208,89,18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,
    3,64,52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,
    227,47,16,58,17,182,189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,
    221,153,101,155,167,43,172,9,129,22,39,253,19,98,108,110,79,113,224,232,
    178,185,112,104,218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,
    241,81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,184,84,204,
    176,115,121,50,45,127,4,150,254,138,236,205,93,222,114,67,29,24,72,243,141,
    128,195,78,66,215,61,156,180,
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
    8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,
    117,35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,
    165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,
    105,92,41,55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,
    187,208,89,18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,
    3,64,52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,
    227,47,16,58,17,182,189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,
    221,153,101,155,167,43,172,9,129,22,39,253,19,98,108,110,79,113,224,232,
    178,185,112,104,218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,
    241,81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,184,84,204,
    176,115,121,50,45,127,4,150,254,138,236,205,93,222,114,67,29,24,72,243,141,
    128,195,78,66,215,61,156,180
);

float fade(float t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

float grad(int hash, float x, float y) {
    int h = hash & 7;
    float u = h < 4 ? x : y;
    float v = h < 4 ? y : x;
    return ((h & 1) != 0 ? -u : u) + ((h & 2) != 0 ? -2.0 * v : 2.0 * v);
}

// Periodic Perlin noise for seamless tiling
float periodicNoise(vec2 p, float period, float seed) {
    p += seed;
    float px = mod(p.x, period);
    float py = mod(p.y, period);
    
    int xi = int(floor(px)) & 255;
    int yi = int(floor(py)) & 255;
    int xi1 = int(mod(floor(px) + 1.0, period)) & 255;
    int yi1 = int(mod(floor(py) + 1.0, period)) & 255;
    
    float xf = fract(px);
    float yf = fract(py);
    
    float u = fade(xf);
    float v = fade(yf);
    
    int aa = perm[perm[xi] + yi];
    int ab = perm[perm[xi] + yi1];
    int ba = perm[perm[xi1] + yi];
    int bb = perm[perm[xi1] + yi1];
    
    float x1 = mix(grad(aa, xf, yf), grad(ba, xf - 1.0, yf), u);
    float x2 = mix(grad(ab, xf, yf - 1.0), grad(bb, xf - 1.0, yf - 1.0), u);
    
    return mix(x1, x2, v);
}

float fbm(vec2 p, int octaves, float persistence, float period, float seed) {
    float value = 0.0;
    float amplitude = 1.0;
    float frequency = 1.0;
    float maxValue = 0.0;
    
    for (int i = 0; i < 8; i++) {
        if (i >= octaves) break;
        value += amplitude * periodicNoise(p * frequency, period * frequency, seed + float(i) * 100.0);
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= 2.0;
    }
    
    return value / maxValue;
}

// Blur the mask texture to create smooth transitions
// Uses a spiral sampling pattern for efficiency
float blurredMask(sampler2D tex, vec2 uv, float blurRadius) {
    float result = 0.0;
    float totalWeight = 0.0;
    
    // Center sample
    result = texture(tex, uv).r;
    totalWeight = 1.0;
    
    // Spiral sample pattern - more efficient than grid
    const float goldenAngle = 2.39996323; // Golden angle in radians
    const int numSamples = 32;
    
    for (int i = 0; i < numSamples; i++) {
        float t = float(i) / float(numSamples);
        float angle = float(i) * goldenAngle;
        float radius = sqrt(t) * blurRadius; // sqrt for uniform distribution
        
        vec2 offset = vec2(cos(angle), sin(angle)) * radius;
        vec2 sampleUV = fract(uv + offset); // Wrap for torus topology
        
        // Weight falls off towards edges
        float weight = 1.0 - t * 0.5;
        
        result += texture(tex, sampleUV).r * weight;
        totalWeight += weight;
    }
    
    return result / totalWeight;
}

void main() {
    vec2 uv = v_uv;
    vec2 pos = uv * 8.0;
    
    // Domain warping for more organic terrain
    float warpX = fbm(pos + vec2(0.0, 0.0), 3, 0.5, 8.0, u_seed + 50.0);
    float warpY = fbm(pos + vec2(5.2, 1.3), 3, 0.5, 8.0, u_seed + 60.0);
    vec2 q = vec2(warpX, warpY) * u_domainWarp;
    
    // === ROAD WARPING - Make roads wiggle significantly ===
    // Multi-octave domain warping for the road lookup
    // This makes the straight roads look like natural, winding paths
    // u_roadWarpIntensity controls the overall curviness
    float warpIntensity = u_roadWarpIntensity;
    float warpScale1 = 0.03 * warpIntensity;  // Large-scale bends
    float warpScale2 = 0.015 * warpIntensity; // Medium-scale curves  
    float warpScale3 = 0.006 * warpIntensity; // Fine wiggles
    
    // Large smooth bends (low frequency) - makes roads curve around terrain
    float warpNoise1X = fbm(pos * 1.5, 2, 0.5, 12.0, u_seed + 400.0);
    float warpNoise1Y = fbm(pos * 1.5 + vec2(7.3, 2.8), 2, 0.5, 12.0, u_seed + 410.0);
    
    // Medium curves - natural path variation
    float warpNoise2X = fbm(pos * 4.0, 3, 0.5, 32.0, u_seed + 420.0);
    float warpNoise2Y = fbm(pos * 4.0 + vec2(3.1, 9.2), 3, 0.5, 32.0, u_seed + 430.0);
    
    // Fine detail wiggles - small path adjustments
    float warpNoise3X = fbm(pos * 10.0, 2, 0.5, 80.0, u_seed + 440.0);
    float warpNoise3Y = fbm(pos * 10.0 + vec2(5.7, 1.4), 2, 0.5, 80.0, u_seed + 450.0);
    
    // Combine all warp levels
    vec2 roadWarp = vec2(0.0);
    roadWarp += (vec2(warpNoise1X, warpNoise1Y) - 0.5) * warpScale1;
    roadWarp += (vec2(warpNoise2X, warpNoise2Y) - 0.5) * warpScale2;
    roadWarp += (vec2(warpNoise3X, warpNoise3Y) - 0.5) * warpScale3;
    
    vec2 maskUV = fract(uv + roadWarp); // Ensure wrapping
    float maskVal = texture(u_maskTexture, maskUV).r;
    
    // === BLUR THE MASK for smooth terrain flattening ===
    // Apply a large blur to create gradual terrain transitions around roads/cities
    // u_settlementBlur controls the radius of the flattened area
    float blurRadius = u_settlementBlur;
    float blurredMaskVal = blurredMask(u_maskTexture, maskUV, blurRadius);
    
    // Height calculation with domain warping
    int heightOct = int(u_heightOctaves);
    float noiseHeight = fbm(pos + q, heightOct, 0.5, 8.0, u_seed);
    noiseHeight = noiseHeight * 0.5 + 0.5;

    // Continental structure — very low frequency for coherent landmasses / ocean basins
    float cScale = max(0.001, u_continentScale);
    float continentBias = fbm(pos * cScale, 2, 0.5, 8.0 * cScale, u_seed + 700.0);
    noiseHeight += continentBias * u_continentIntensity;

    // Mountain ridges — 1-abs(noise) creates long connected chains
    float ridgeBase = fbm(pos * 0.7, 3, 0.55, 5.6, u_seed + 800.0);
    float ridge = pow(1.0 - abs(ridgeBase), 3.0) * u_ridgeIntensity;
    noiseHeight += ridge;
    noiseHeight = clamp(noiseHeight, 0.0, 1.0);

    noiseHeight = pow(noiseHeight, u_heightScale);
    
    // === FLATTEN TERRAIN with blurred mask ===
    // Use the blurred mask to create gradual terrain elevation changes
    // The blurred mask creates a smooth "carved" area around settlements
    // CRITICAL: Ensure roads and settlements are ALWAYS above sea level
    // Use smoothstep to guarantee minimum elevation near settlements
    
    float minSafeHeight = u_seaLevel + 0.08; // Minimum height for settlements
    float targetHeight = max(u_roadFlattenHeight, minSafeHeight);
    
    // Mix towards target height based on blurred mask
    float baseMixedHeight = mix(noiseHeight, targetHeight, blurredMaskVal);
    
    // Apply smoothstep to GUARANTEE areas with settlement influence are above water
    // The more mask influence, the more we force the height above sea level
    float settlementInfluence = smoothstep(0.0, 0.5, blurredMaskVal);
    float guaranteedMinHeight = mix(0.0, minSafeHeight, settlementInfluence);
    
    // Final height is the maximum of the mixed height and the guaranteed minimum
    float finalHeight = max(baseMixedHeight, guaranteedMinHeight);
    
    // Moisture calculation
    int moistOct = int(u_moistureOctaves);
    float noiseMoist = fbm(pos + q * 0.5, moistOct, 0.5, 4.0, u_seed + 200.0);
    noiseMoist = noiseMoist * 0.5 + 0.5;
    noiseMoist = pow(noiseMoist, u_moistureScale);
    
    // Temperature calculation based on latitude
    float latitude = 1.0 - abs(uv.y - 0.5) * 2.0;
    float noiseTemp = fbm(pos, 3, 0.5, 4.0, u_seed + 300.0) * 0.5 + 0.5;
    float temp01 = latitude * (1.0 - u_temperatureVariation) + noiseTemp * u_temperatureVariation;

    // Map 0..1 to °C range
    float tempC = mix(u_tempMin, u_tempMax, clamp(temp01, 0.0, 1.0));

    // Pack temperature as 0..1 for storage (still derived from °C settings)
    float finalTemp = clamp((tempC - u_tempMin) / max(0.0001, (u_tempMax - u_tempMin)), 0.0, 1.0);
    
    // Store original (non-blurred) mask for road visualization but blurred for influence
    fragColor = vec4(finalHeight, noiseMoist, finalTemp, maskVal);
}
`;

// Visual terrain shader (biome coloring via lookup texture + rivers)
export const visualTerrainShader = `#version 300 es
precision highp float;

in vec2 v_uv;
out vec4 fragColor;

uniform sampler2D u_masterTexture;
uniform sampler2D u_maskTexture;
uniform sampler2D u_biomeTexture;
uniform sampler2D u_riverTexture;
uniform float u_seaLevel;
uniform float u_snowLevel;
uniform float u_beachWidth;
uniform float u_tempMin;
uniform float u_tempMax;
uniform float u_riverMoistureBoost;
uniform vec2 u_mapSize;

// Colors for special zones (water, beach, snow, rock)
vec3 deepWater = vec3(0.02, 0.08, 0.22);
vec3 shallowWater = vec3(0.08, 0.28, 0.48);
vec3 coastWater = vec3(0.12, 0.42, 0.55);
vec3 beach = vec3(0.85, 0.80, 0.62);
vec3 wetSand = vec3(0.70, 0.65, 0.50);
vec3 tundra = vec3(0.50, 0.52, 0.45);
vec3 snow = vec3(0.95, 0.96, 0.98);
vec3 ice = vec3(0.85, 0.90, 0.95);
vec3 rock = vec3(0.42, 0.40, 0.38);
vec3 darkRock = vec3(0.28, 0.26, 0.24);

float getBiomeIndex(float height, float moisture, float temp, float mask, float beachW) {
    float tempC = mix(u_tempMin, u_tempMax, clamp(temp, 0.0, 1.0));
    if (height < u_seaLevel) return 0.0;
    float landHeight = (height - u_seaLevel) / (1.0 - u_seaLevel);
    float beachEnd = beachW * 2.0;
    if (landHeight < beachEnd && tempC > 1.0) return 1.0;
    if (landHeight < beachEnd * 0.5 && tempC <= 1.0) return 8.0;
    if (height > u_snowLevel) return 5.0;
    float rockStart = u_snowLevel - 0.12;
    if (height > rockStart) return 4.0;
    // Terrain index from biome matrix texture alpha
    return texture(u_biomeTexture, vec2(moisture, temp)).a * 255.0;
}

vec3 getBiomeColor(float height, float moisture, float temp, float mask, float beachW) {
    float tempC = mix(u_tempMin, u_tempMax, clamp(temp, 0.0, 1.0));

    // === WATER (depth + ice) ===
    if (height < u_seaLevel) {
        float waterDepth = height / u_seaLevel;
        float iceAmount = smoothstep(0.5, -2.0, tempC);

        vec3 waterColor;
        if (waterDepth > 0.9) {
            waterColor = mix(shallowWater, coastWater, (waterDepth - 0.9) / 0.1);
        } else if (waterDepth > 0.6) {
            waterColor = shallowWater;
        } else if (waterDepth > 0.3) {
            waterColor = mix(deepWater, shallowWater, (waterDepth - 0.3) / 0.3);
        } else {
            waterColor = deepWater;
        }

        vec3 iceColor = mix(ice, vec3(0.78, 0.86, 0.92), smoothstep(0.2, 0.95, waterDepth));
        return mix(waterColor, iceColor, iceAmount);
    }

    float landHeight = (height - u_seaLevel) / (1.0 - u_seaLevel);
    float beachEnd = beachW * 2.0;

    // === WARM BEACH ===
    if (landHeight < beachEnd && tempC > 1.0) {
        float beachProgress = landHeight / beachEnd;
        if (beachProgress < 0.3) {
            return mix(wetSand, beach, beachProgress / 0.3);
        } else if (beachProgress < 0.7) {
            return beach;
        } else {
            vec3 vegColor = texture(u_biomeTexture, vec2(moisture, temp)).rgb;
            return mix(beach, vegColor, (beachProgress - 0.7) / 0.3);
        }
    }

    // === COLD SHORE ===
    if (landHeight < beachEnd * 0.5 && tempC <= 1.0) {
        float t = landHeight / (beachEnd * 0.5);
        return mix(darkRock, tundra, t);
    }

    // === SNOW CAPS ===
    if (height > u_snowLevel) {
        float snowAmount = clamp((height - u_snowLevel) / (1.0 - u_snowLevel), 0.0, 1.0);
        vec3 mountainBase = tempC > 8.0 ? rock : darkRock;
        float cold = clamp((8.0 - tempC) / 20.0, 0.0, 1.0);
        float snowCoverage = snowAmount * (0.6 + cold * 0.8);
        return mix(mountainBase, snow, clamp(snowCoverage, 0.0, 1.0));
    }

    // === MOUNTAIN ROCK ===
    float rockStart = u_snowLevel - 0.12;
    if (height > rockStart) {
        float rockAmount = (height - rockStart) / 0.12;
        vec3 baseVeg = texture(u_biomeTexture, vec2(moisture, temp)).rgb;
        return mix(baseVeg, rock, rockAmount);
    }

    // === BIOME MATRIX LOOKUP ===
    // All remaining land is determined by temperature x moisture matrix
    return texture(u_biomeTexture, vec2(moisture, temp)).rgb;
}

void main() {
    vec2 texelSize = 1.0 / u_mapSize;

    vec4 master = texture(u_masterTexture, v_uv);
    float height = master.r;
    float moisture = master.g;
    float temp = master.b;
    float mask = master.a;

    // River moisture boost — shifts biomes toward wetter variants near rivers
    float riverVal = texture(u_riverTexture, v_uv).r;
    float adjustedMoisture = min(1.0, moisture + riverVal * u_riverMoistureBoost);

    // Hillshading
    float hL = texture(u_masterTexture, fract(v_uv - vec2(texelSize.x, 0.0))).r;
    float hR = texture(u_masterTexture, fract(v_uv + vec2(texelSize.x, 0.0))).r;
    float hU = texture(u_masterTexture, fract(v_uv - vec2(0.0, texelSize.y))).r;
    float hD = texture(u_masterTexture, fract(v_uv + vec2(0.0, texelSize.y))).r;

    vec3 normal = normalize(vec3(
        (hL - hR) * 5.0,
        (hU - hD) * 5.0,
        0.12
    ));

    vec3 lightDir = normalize(vec3(-0.4, -0.6, 0.8));
    float lighting = dot(normal, lightDir) * 0.5 + 0.5;
    lighting = mix(0.65, 1.0, lighting);

    vec3 color = getBiomeColor(height, adjustedMoisture, temp, mask, u_beachWidth);

    // Hillshading (less on water and roads)
    float hillshadeStrength = height < u_seaLevel ? 0.15 : (mask > 0.2 ? 0.3 : 1.0);
    color *= mix(1.0, lighting, hillshadeStrength);

    // River overlay (after hillshading so rivers look flat/reflective)
    if (riverVal > 0.02 && height >= u_seaLevel) {
        float riverStrength = smoothstep(0.02, 0.40, riverVal);
        vec3 riverColor = mix(vec3(0.12, 0.35, 0.52), vec3(0.06, 0.22, 0.40), riverStrength);
        color = mix(color, riverColor, riverStrength * 0.85);
    }

    // Subtle noise texture (integer-style hash, no sin)
    vec2 np = v_uv * u_mapSize.y;
    float noise = fract(dot(np, vec2(127.1, 311.7)));
    noise = fract(noise * noise * 43758.5453);
    color += (noise - 0.5) * 0.012;
    color = pow(color, vec3(0.97));

    float biomeIdx = getBiomeIndex(height, adjustedMoisture, temp, mask, u_beachWidth);
    fragColor = vec4(clamp(color, 0.0, 1.0), (biomeIdx + 0.5) / 9.0);
}
`;

// Channel visualization shader with color ramps
export const channelViewShader = `#version 300 es
precision highp float;

in vec2 v_uv;
out vec4 fragColor;

uniform sampler2D u_texture;
uniform int u_channel; // 0=Height, 1=Moisture, 2=Temp, 3=Mask, 4=RGB

// Height color ramp (deep blue -> green -> brown -> white)
vec3 heightColor(float h) {
    if (h < 0.4) {
        // Water: dark blue to light blue
        return mix(vec3(0.0, 0.1, 0.3), vec3(0.2, 0.4, 0.6), h / 0.4);
    } else if (h < 0.5) {
        // Beach/lowland: tan
        return mix(vec3(0.2, 0.4, 0.6), vec3(0.4, 0.5, 0.3), (h - 0.4) / 0.1);
    } else if (h < 0.65) {
        // Plains: green
        return mix(vec3(0.4, 0.5, 0.3), vec3(0.3, 0.5, 0.2), (h - 0.5) / 0.15);
    } else if (h < 0.8) {
        // Hills: brown
        return mix(vec3(0.3, 0.5, 0.2), vec3(0.5, 0.4, 0.3), (h - 0.65) / 0.15);
    } else {
        // Mountains: gray to white
        return mix(vec3(0.5, 0.4, 0.3), vec3(1.0, 1.0, 1.0), (h - 0.8) / 0.2);
    }
}

// Moisture color ramp (brown -> yellow -> green -> blue)
vec3 moistureColor(float m) {
    if (m < 0.25) {
        return mix(vec3(0.6, 0.4, 0.2), vec3(0.7, 0.6, 0.3), m / 0.25);
    } else if (m < 0.5) {
        return mix(vec3(0.7, 0.6, 0.3), vec3(0.4, 0.6, 0.3), (m - 0.25) / 0.25);
    } else if (m < 0.75) {
        return mix(vec3(0.4, 0.6, 0.3), vec3(0.2, 0.5, 0.5), (m - 0.5) / 0.25);
    } else {
        return mix(vec3(0.2, 0.5, 0.5), vec3(0.2, 0.4, 0.7), (m - 0.75) / 0.25);
    }
}

// Temperature color ramp (blue -> cyan -> green -> yellow -> red)
vec3 temperatureColor(float t) {
    if (t < 0.2) {
        return mix(vec3(0.2, 0.2, 0.8), vec3(0.2, 0.6, 0.8), t / 0.2);
    } else if (t < 0.4) {
        return mix(vec3(0.2, 0.6, 0.8), vec3(0.3, 0.7, 0.4), (t - 0.2) / 0.2);
    } else if (t < 0.6) {
        return mix(vec3(0.3, 0.7, 0.4), vec3(0.8, 0.8, 0.3), (t - 0.4) / 0.2);
    } else if (t < 0.8) {
        return mix(vec3(0.8, 0.8, 0.3), vec3(0.9, 0.5, 0.2), (t - 0.6) / 0.2);
    } else {
        return mix(vec3(0.9, 0.5, 0.2), vec3(0.8, 0.2, 0.2), (t - 0.8) / 0.2);
    }
}

void main() {
    vec4 texColor = texture(u_texture, v_uv);
    
    if (u_channel == 0) {
        // Height with color ramp
        fragColor = vec4(heightColor(texColor.r), 1.0);
    } else if (u_channel == 1) {
        // Moisture with color ramp
        fragColor = vec4(moistureColor(texColor.g), 1.0);
    } else if (u_channel == 2) {
        // Temperature with color ramp
        fragColor = vec4(temperatureColor(texColor.b), 1.0);
    } else if (u_channel == 3) {
        // Mask (roads) - grayscale with tint
        float m = texColor.a;
        vec3 maskColor = mix(vec3(0.1, 0.1, 0.15), vec3(1.0, 0.9, 0.7), m);
        fragColor = vec4(maskColor, 1.0);
    } else {
        fragColor = texColor;
    }
}
`;

// Road/city rendering vertex shader
export const roadVertexShader = `#version 300 es
in vec2 a_position;
uniform vec2 u_offset;
uniform vec2 u_scale;

void main() {
    vec2 pos = (a_position + u_offset) * u_scale * 2.0 - 1.0;
    gl_Position = vec4(pos, 0.0, 1.0);
}
`;

export const roadFragmentShader = `#version 300 es
precision highp float;
out vec4 fragColor;
uniform float u_intensity;

void main() {
    fragColor = vec4(u_intensity, u_intensity, u_intensity, 1.0);
}
`;

// Roads visualization shader with special coloring
export const roadsViewShader = `#version 300 es
precision highp float;

in vec2 v_uv;
out vec4 fragColor;

uniform sampler2D u_texture;

void main() {
    float mask = texture(u_texture, v_uv).r;
    
    // Dark background with bright roads
    vec3 bgColor = vec3(0.08, 0.1, 0.12);
    vec3 roadColor = vec3(0.9, 0.85, 0.7);
    vec3 cityColor = vec3(1.0, 0.95, 0.8);
    
    vec3 color = bgColor;
    
    if (mask > 0.7) {
        // City
        color = mix(roadColor, cityColor, (mask - 0.7) / 0.3);
    } else if (mask > 0.1) {
        // Road
        color = mix(bgColor, roadColor, (mask - 0.1) / 0.6);
    }
    
    fragColor = vec4(color, 1.0);
}
`;

// Layer 5: Traversability shader
// Generates a bitmap indicating which pixels are traversable (for A* pathfinding)
// Water and high mountains are not traversable, BUT ICE is traversable.
export const traversabilityShader = `#version 300 es
precision highp float;

in vec2 v_uv;
out vec4 fragColor;

uniform sampler2D u_masterTexture;
uniform float u_seaLevel;
uniform float u_maxTraversableHeight;
uniform float u_tempMin;
uniform float u_tempMax;

void main() {
    vec4 master = texture(u_masterTexture, v_uv);
    float height = master.r;
    float temp01 = master.b;
    float mask = master.a;  // Road/city influence

    // Convert stored temperature back to °C
    float tempC = mix(u_tempMin, u_tempMax, clamp(temp01, 0.0, 1.0));

    // Determine if traversable:
    // - Water (height < seaLevel) is NOT traversable, EXCEPT if frozen (tempC < 0)
    // - High mountains (height > maxTraversableHeight) are NOT traversable
    // - Roads/cities are ALWAYS traversable

    bool isWater = height < u_seaLevel;
    bool isIce = isWater && (tempC < 0.0);
    bool isTooHigh = height > u_maxTraversableHeight;
    bool isRoadOrCity = mask > 0.1;  // Road/city influence

    float traversable = 0.0;

    if (isRoadOrCity) {
        traversable = 1.0;
    } else if (isIce) {
        traversable = 1.0;
    } else if (!isWater && !isTooHigh) {
        traversable = 1.0;
    }

    // Output: 
    // R = traversable (0..1),
    // G = height (0..1) for cost,
    // B = road influence,
    // A = ice flag (0..1) for debugging/visualization
    fragColor = vec4(traversable, height, mask, isIce ? 1.0 : 0.0);
}
`;

// Traversability visualization shader
export const traversabilityViewShader = `#version 300 es
precision highp float;

in vec2 v_uv;
out vec4 fragColor;

uniform sampler2D u_traversabilityTexture;

void main() {
    vec4 data = texture(u_traversabilityTexture, v_uv);
    float traversable = data.r;
    float height = data.g;
    float roadInfluence = data.b;
    float isIce = data.a;

    vec3 color;

    if (isIce > 0.5) {
        // Ice: traversable water
        color = vec3(0.75, 0.88, 0.95);
    } else if (traversable < 0.5) {
        // Not traversable - red tint
        color = vec3(0.4, 0.1, 0.1);
    } else if (roadInfluence > 0.1) {
        // Road/city - bright yellow/gold
        color = vec3(0.9, 0.8, 0.3);
    } else {
        // Traversable land - green with height variation
        float brightness = 0.3 + height * 0.5;
        color = vec3(0.1, brightness, 0.15);
    }

    fragColor = vec4(color, 1.0);
}
`;
