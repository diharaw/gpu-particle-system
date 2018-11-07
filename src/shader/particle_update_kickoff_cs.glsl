// ------------------------------------------------------------------
// CONSTANTS ---------------------------------------------------------
// ------------------------------------------------------------------

#define LOCAL_SIZE 256

// ------------------------------------------------------------------
// INPUTS -----------------------------------------------------------
// ------------------------------------------------------------------

layout (local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

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

layout(std430, binding = 0) buffer ParticleData_t
{
    uint simulation_count;
    uint emission_count;
    Particle particles[];
} ParticleData;

layout(std430, binding = 1) buffer EmissionDispatchArgs_t
{
    uint num_groups_x;
    uint num_groups_y;
    uint num_groups_z;
} EmissionDispatchArgs;

layout(std430, binding = 2) buffer SimulationDispatchArgs_t
{
    uint num_groups_x;
    uint num_groups_y;
    uint num_groups_z;
} SimulationDispatchArgs;

layout(std430, binding = 3) buffer ParticleDrawArgs_t
{
    uint count;
    uint instance_count;
    uint first;
    uint base_instance;
} ParticleDrawArgs;

layout(std430, binding = 4) buffer ParticleIndices_t
{
    uint count;
    uint indices[]
} DeadIndices;

layout(std430, binding = 5) buffer ParticleIndices_t
{
    uint count;
    uint indices[]
} AliveIndicesPreSim;

layout(std430, binding = 6) buffer ParticleIndices_t
{
    uint count;
    uint indices[]
} AliveIndicesPostSim;

uniform uint u_ParticlesPerFrame;

// ------------------------------------------------------------------
// MAIN -------------------------------------------------------------
// ------------------------------------------------------------------

void main()
{
    // Reset particle indirect draw instance count
    ParticleDrawArgs.instance_count = 0;

    // We can't emit more particles than we have available
    ParticleData.emission_count = min(u_ParticlesPerFrame, DeadIndices.count);

    EmissionDispatchArgs.local_size_x = ceil((float)ParticleData.emission_count / LOCAL_SIZE);
    EmissionDispatchArgs.local_size_y = 1;
    EmissionDispatchArgs.local_size_z = 1;

    // Calculate total number of particles to simulate this frame
    ParticleData.simulation_count = AliveIndicesPreSim.count + ParticleData.emission_count;

    SimulationDispatchArgs.local_size_x = ceil((float)ParticleData.simulation_count / LOCAL_SIZE);
    SimulationDispatchArgs.local_size_y = 1;
    SimulationDispatchArgs.local_size_z = 1;

    // Reset post simulation alive index count to 0
    AliveIndicesPostSim.count = 0;
}