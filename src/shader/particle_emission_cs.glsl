#include <random.glsl>

// ------------------------------------------------------------------
// CONSTANTS ---------------------------------------------------------
// ------------------------------------------------------------------

#define LOCAL_SIZE 32
#define EMISSION_SHAPE_SPHERE 0
#define EMISSION_SHAPE_BOX 1
#define EMISSION_SHAPE_CONE 2
#define DIRECTION_TYPE_SINGLE 0
#define DIRECTION_TYPE_OUTWARD 1

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

uniform vec3  u_Seeds;
uniform vec3  u_Position;
uniform float u_MinInitialSpeed;
uniform float u_MaxInitialSpeed;
uniform float u_MinLifetime;
uniform float u_MaxLifetime;
uniform vec3  u_Direction;
uniform int   u_EmissionShape;
uniform int   u_DirectionType;
uniform float u_SphereRadius;
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

        vec3 position = u_Position;

        if (u_EmissionShape == EMISSION_SHAPE_SPHERE)
            position += randomPointOnSphere(rand(u_Seeds.xyz / (index + 1)), rand(u_Seeds.yzx / (index + 1)), u_SphereRadius * rand(u_Seeds.zyx / (index + 1)));
        else if (u_EmissionShape == EMISSION_SHAPE_BOX)
            position += vec3(0.0);
        else if (u_EmissionShape == EMISSION_SHAPE_CONE)
            position += vec3(0.0);

        vec3 direction = u_Direction;

        if (u_DirectionType == DIRECTION_TYPE_OUTWARD)
            direction = normalize(position - u_Position);

        float initial_speed = u_MinInitialSpeed + (u_MaxInitialSpeed - u_MinInitialSpeed) * rand(u_Seeds.xzy / (index + 1));
        float lifetime      = u_MinLifetime + (u_MaxLifetime - u_MinLifetime) * rand(u_Seeds.zyx / (index + 1));

        ParticleData.particles[particle_index].position.xyz = position;
        ParticleData.particles[particle_index].velocity.xyz = direction * initial_speed;
        ParticleData.particles[particle_index].lifetime.xy  = vec2(0.0, lifetime);

        push_alive_index(particle_index);
    }
}