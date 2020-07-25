// ------------------------------------------------------------------
// CONSTANTS ---------------------------------------------------------
// ------------------------------------------------------------------

#define LOCAL_SIZE 32

// ------------------------------------------------------------------
// INPUTS -----------------------------------------------------------
// ------------------------------------------------------------------

layout(local_size_x = LOCAL_SIZE, local_size_y = 1, local_size_z = 1) in;

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

uniform vec3  u_EmitterPosition;
uniform vec3  u_EmitterVelocity;
uniform float u_EmitterLifetime;
uniform int   u_PreSimIdx;

layout(std430, binding = 0) buffer ParticleData_t
{
    Particle particles[];
}
ParticleData;

layout(std430, binding = 1) buffer ParticleDeadIndices_t
{
    uint indices[];
}
DeadIndices;

layout(std430, binding = 2) buffer ParticleAlivePreSimIndices_t
{
    uint indices[];
}
AliveIndicesPreSim;

layout(std430, binding = 3) buffer Counters_t
{
    uint dead_count;
    uint alive_count[2];
    uint simulation_count;
    uint emission_count;
}
Counters;

// ------------------------------------------------------------------
// FUNCTIONS --------------------------------------------------------
// ------------------------------------------------------------------

uint pop_dead_index()
{
    uint index = atomicAdd(Counters.dead_count, -1);
    return DeadIndices.indices[index - 1];
}

void push_alive_index(uint index)
{
    uint insert_idx                        = atomicAdd(Counters.alive_count[u_PreSimIdx], 1);
    AliveIndicesPreSim.indices[insert_idx] = index;
}

// ------------------------------------------------------------------
// MAIN -------------------------------------------------------------
// ------------------------------------------------------------------

void main()
{
    uint index = gl_GlobalInvocationID.x;

    if (index < Counters.emission_count)
    {
        uint particle_index = pop_dead_index();

        ParticleData.particles[particle_index].position.xyz = u_EmitterPosition;
        ParticleData.particles[particle_index].velocity.xyz = u_EmitterVelocity;
        ParticleData.particles[particle_index].lifetime.xy  = vec2(0.0, u_EmitterLifetime);

        push_alive_index(particle_index);
    }
}