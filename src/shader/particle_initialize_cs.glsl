// ------------------------------------------------------------------
// CONSTANTS ---------------------------------------------------------
// ------------------------------------------------------------------

#define LOCAL_SIZE 32

// ------------------------------------------------------------------
// INPUTS -----------------------------------------------------------
// ------------------------------------------------------------------

layout (local_size_x = LOCAL_SIZE, local_size_y = 1, local_size_z = 1) in;

// ------------------------------------------------------------------
// UNIFORMS ---------------------------------------------------------
// ------------------------------------------------------------------

layout(std430, binding = 0) buffer ParticleDeadIndices_t
{
    uint indices[];
} DeadIndices;

layout(std430, binding = 1) buffer ParticleAlivePreSimIndices_t
{
    uint indices[];
} AliveIndicesPreSim;

layout(std430, binding = 2) buffer ParticleCounters_t
{
    uint dead_count;
    uint alive_pre_sim_count;
    uint alive_post_sim_count;
    uint simulation_count;
    uint emission_count;
} Counters;

uniform int u_MaxParticles;

// ------------------------------------------------------------------
// MAIN -------------------------------------------------------------
// ------------------------------------------------------------------

void main()
{
    uint index = gl_GlobalInvocationID.x;

    if (index == 0)
    {
        // Initialize counts
        Counters.dead_count = u_MaxParticles - 1;
        Counters.alive_pre_sim_count = 0;
    }

    DeadIndices.indices[index] = index;
}