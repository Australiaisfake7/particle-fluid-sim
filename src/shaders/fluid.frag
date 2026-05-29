#version 460 core
out vec4 FragColor;

const float pi = 3.14159265358979323846;

uniform uvec2 gridSize;
uniform uvec2 screenRes;
uniform float h;
uniform float particleBrightness;

struct Particle
{
    vec2 position;
    vec2 velocity;
};

layout(std430, binding = 0) buffer Particles
{
    Particle particles[];
};

layout(std430, binding = 1) readonly buffer GridCellPointers
{
    uvec2 gridCellPointers[];
};

uint gridCell(Particle p)
{
    uvec2 pos = uvec2(p.position);

    return pos.x + pos.y * gridSize.x;
}

float cubicSpline(float q)
{
    if (q < 1.0)
    {
        return 10.0 / (28.0 * pi * h * h) * (pow(2.0 - q, 3.0) - 4 * pow(1.0 - q, 3.0));
    }
    if (q <= 2.0)
    {
        return 10.0 / (28.0 * pi * h * h) * pow(2.0 - q, 3.0);
    }
    return 0.0;
}

void main()
{
    float density = 0.0;

    vec2 pos = gl_FragCoord.xy / vec2(screenRes) * vec2(gridSize);
    ivec2 startCell = ivec2(floor(pos));

    int mR = 1 + int(floor(2 * h));

    for (int y = -mR; y <= mR; y++)
    {
        for (int x = -mR; x <= mR; x++)
        {
            ivec2 cell = startCell + ivec2(x, y);

            if (cell.x >= int(gridSize.x) || cell.x < 0 || cell.y >= int(gridSize.y) || cell.y < 0)
            {
                continue;
            }
            uint cellCoord = uint(cell.x) + uint(cell.y) * gridSize.x;

            for (uint i = gridCellPointers[cellCoord].x; i < gridCellPointers[cellCoord].y; i++)
            {
                vec2 r = pos - particles[i].position;
                float dist = length(r);
                if (dist == 0.0) continue;

                float strength = cubicSpline(dist / h);

                density += strength;
            }
        }
    }

    FragColor = vec4(0.0f, 0.0f, density * particleBrightness, 1.0f);
}