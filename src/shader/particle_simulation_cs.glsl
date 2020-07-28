#include <curl_noise.glsl>

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
    vec4 lifetime;
    vec4 velocity;
    vec4 position;
    vec4 color;
};

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

layout(std430, binding = 3) buffer ParticleAlivePostSimIndices_t
{
    uint indices[];
}
AliveIndicesPostSim;

layout(std430, binding = 4) buffer ParticleDrawArgs_t
{
    uint count;
    uint instance_count;
    uint first;
    uint base_instance;
}
ParticleDrawArgs;

layout(std430, binding = 5) buffer ParticleCounters_t
{
    uint dead_count;
    uint alive_count[2];
    uint simulation_count;
    uint emission_count;
}
Counters;

uniform float u_DeltaTime;
uniform int   u_PreSimIdx;
uniform int   u_PostSimIdx;
uniform float u_Viscosity;
uniform vec3  u_ConstantVelocity;

// ------------------------------------------------------------------
// FUNCTIONS --------------------------------------------------------
// ------------------------------------------------------------------

void push_dead_index(uint index)
{
    uint insert_idx                 = atomicAdd(Counters.dead_count, 1);
    DeadIndices.indices[insert_idx] = index;
}

uint pop_dead_index()
{
    uint index = atomicAdd(Counters.dead_count, -1);
    return DeadIndices.indices[index - 1];
}

void push_alive_index(uint index)
{
    uint insert_idx                         = atomicAdd(Counters.alive_count[u_PostSimIdx], 1);
    AliveIndicesPostSim.indices[insert_idx] = index;
}

uint pop_alive_index()
{
    uint index = atomicAdd(Counters.alive_count[u_PreSimIdx], -1);
    return AliveIndicesPreSim.indices[index - 1];
}

// ------------------------------------------------------------------
// MAIN -------------------------------------------------------------
// ------------------------------------------------------------------

void main()
{
    uint index = gl_GlobalInvocationID.x;

    if (index < Counters.simulation_count)
    {
        // Consume an Alive particle index
        uint particle_index = pop_alive_index();

        Particle particle = ParticleData.particles[particle_index];

        // Is it dead?
        if (particle.lifetime.x >= particle.lifetime.y)
        {
            // If dead, just append into the DeadIndices list
            push_dead_index(particle_index);
        }
        else
        {
            // If still alive, increment lifetime and run simulation
            particle.lifetime.x += u_DeltaTime;

            if (u_Viscosity != 0.0)
                particle.velocity.xyz += (curl_noise(particle.position.xyz) - particle.velocity.xyz) * u_Viscosity * u_DeltaTime;

            particle.position.xyz += (particle.velocity.xyz + u_ConstantVelocity) * u_DeltaTime;

            ParticleData.particles[particle_index] = particle;

            // Append index back into AliveIndices list
            push_alive_index(particle_index);

            // Increment draw count
            atomicAdd(ParticleDrawArgs.instance_count, 1);
        }
    }
}