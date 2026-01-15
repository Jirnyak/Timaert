inline int idx(int x, int y, int N)
{
    return y * N + x;
}

inline float noise2D(int x, int y, uint32_t seed)
{
    uint32_t h = x * 374761393u + y * 668265263u + seed * 1442695041u;
    h ^= h >> 13;
    h *= 1274126177u;
    return (h & 0xFFFFFF) / float(0xFFFFFF) * 2.0f - 1.0f;
}

void diffuseStep(
    const float* in,
    float* out,
    int N,
    float rate
)
{
    for (int y = 1; y < N - 1; ++y)
    {
        for (int x = 1; x < N - 1; ++x)
        {
            int i = idx(x, y, N);

            out[i] = in[i] + rate * (
                in[idx(x - 1, y, N)] +
                in[idx(x + 1, y, N)] +
                in[idx(x, y - 1, N)] +
                in[idx(x, y + 1, N)] -
                4.0f * in[i]
            );
        }
    }

    // Copy borders unchanged
    for (int x = 0; x < N; ++x)
    {
        out[idx(x, 0, N)] = in[idx(x, 0, N)];
        out[idx(x, N - 1, N)] = in[idx(x, N - 1, N)];
    }
    for (int y = 0; y < N; ++y)
    {
        out[idx(0, y, N)] = in[idx(0, y, N)];
        out[idx(N - 1, y, N)] = in[idx(N - 1, y, N)];
    }
}

void generateUniversalField(
    float* field,        // NxN output
    float* temp,         // NxN temp buffer
    int N,
    int octaves,         // number of layers (4–8 typical)
    int diffusionSteps,  // smoothing per octave (5–20)
    float baseDiffusion, // 0.1–0.25
    float baseNoise,     // 0.2–1.0
    uint32_t seed
)
{
    // Initialize field
    for (int i = 0; i < N * N; ++i)
        field[i] = 0.0f;

    float noiseAmp = baseNoise;
    float diffusion = baseDiffusion;

    for (int o = 0; o < octaves; ++o)
    {
        // Inject noise
        for (int y = 1; y < N - 1; ++y)
        {
            for (int x = 1; x < N - 1; ++x)
            {
                int i = idx(x, y, N);
                field[i] += noise2D(x, y, seed + o * 1013u) * noiseAmp;
            }
        }

        // Diffuse
        for (int s = 0; s < diffusionSteps; ++s)
        {
            diffuseStep(field, temp, N, diffusion);

            // swap buffers
            float* swap = field;
            field = temp;
            temp = swap;
        }

        noiseAmp *= 0.5f;
        diffusion *= 0.5f;
    }
}

void addSources(
    float* field,
    const float* sources,
    int N,
    float strength
)
{
    for (int i = 0; i < N * N; ++i)
        field[i] += sources[i] * strength;
}

void normalize01(float* field, int count)
{
    float minV = field[0];
    float maxV = field[0];

    for (int i = 1; i < count; ++i)
    {
        if (field[i] < minV) minV = field[i];
        if (field[i] > maxV) maxV = field[i];
    }

    float inv = 1.0f / (maxV - minV + 1e-6f);

    for (int i = 0; i < count; ++i)
        field[i] = (field[i] - minV) * inv;
}


