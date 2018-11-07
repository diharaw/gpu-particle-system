// ------------------------------------------------------------------
// CONSTANTS ---------------------------------------------------------
// ------------------------------------------------------------------

#define LOCAL_SIZE 256

// ------------------------------------------------------------------
// INPUTS -----------------------------------------------------------
// ------------------------------------------------------------------

layout (local_size_x = LOCAL_SIZE, local_size_y = 1, local_size_z = 1) in;

// ------------------------------------------------------------------
// UNIFORMS ---------------------------------------------------------
// ------------------------------------------------------------------

struct Particle
{
    vec2 lifetime;
    vec3 velocity;
    vec3 position;
    vec4 color;
};

layout(std140) uniform EmitterData_t
{
    vec3 position;
    float lifetime;
} EmitterData;

layout(std430, binding = 0) buffer ParticleData_t
{
    uint simulation_count;
    uint emission_count;
    Particle particles[];
} ParticleData;

layout(std430, binding = 1) buffer ParticleIndices_t
{
    uint count;
    uint indices[]
} DeadIndices;

layout(std430, binding = 2) buffer ParticleIndices_t
{
    uint count;
    uint indices[]
} AliveIndicesPreSim;

// ------------------------------------------------------------------
// FUNCTIONS --------------------------------------------------------
// ------------------------------------------------------------------

uint pop_dead_index()
{
    uint index = atomicAdd(DeadIndices.dead, -1);
    return DeadIndices.indices[index - 1];
}

void push_alive_index(uint index)
{
    uint insert_idx = atomicAdd(AliveIndicesPreSim.alive, 1);
    AliveIndicesPreSim.indices[insert_idx] = index;
}

// ------------------------------------------------------------------
// MAIN -------------------------------------------------------------
// ------------------------------------------------------------------

void main()
{
    uint index = gl_GlobalInvocationID.x;

    if (index < Counters.emission)
    {
        uint particle_index = pop_dead_index();

        ParticleData.particles[particle_index].position = EmitterData.position;
        ParticleData.particles[particle_index].lifetime = vec2(0.0, EmitterData.lifetime);

        push_alive_index(particle_index);
    }
}