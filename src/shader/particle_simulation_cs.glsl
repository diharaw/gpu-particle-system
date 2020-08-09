#include <curl_noise.glsl>

// ------------------------------------------------------------------
// CONSTANTS ---------------------------------------------------------
// ------------------------------------------------------------------

#define LOCAL_SIZE 32
#define CAMERA_NEAR_PLANE 0.1
#define CAMERA_FAR_PLANE 1000.0
#define MIN_THICKNESS 0.001

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

uniform mat4 u_ViewProj;
uniform float u_DeltaTime;
uniform int   u_PreSimIdx;
uniform int   u_PostSimIdx;
uniform float u_Viscosity;
uniform float u_Restitution;
uniform vec3  u_ConstantVelocity;
uniform int   u_AffectedByGravity;
uniform int   u_DepthBufferCollision;

uniform sampler2D s_Depth;
uniform sampler2D s_Normals;

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

float exp_01_to_linear_01_depth(float z, float n, float f)
{
    float z_buffer_params_y = f / n;
    float z_buffer_params_x = 1.0 - z_buffer_params_y;

    return 1.0 / (z_buffer_params_x * z + z_buffer_params_y);
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

            if (u_AffectedByGravity == 1)
                particle.velocity.xyz += vec3(0.0, -9.8, 0.0) * u_DeltaTime;

            if (u_DepthBufferCollision == 1)
            {
                vec4 position = u_ViewProj * vec4(particle.position.xyz, 1.0);
                position.xyz /= position.w;

                vec2 tex_coord = position.xy * 0.5 + vec2(0.5);

                vec3 surface_normal = normalize(texture(s_Normals, tex_coord).rgb);

                float g_buffer_depth = exp_01_to_linear_01_depth(texture(s_Depth, tex_coord).r, CAMERA_NEAR_PLANE, CAMERA_FAR_PLANE);
                float particle_depth = exp_01_to_linear_01_depth(position.z * 0.5 + 0.5, CAMERA_NEAR_PLANE, CAMERA_FAR_PLANE);

                if ((particle_depth > g_buffer_depth) && (particle_depth - g_buffer_depth) < MIN_THICKNESS)
                {
                    if (dot(particle.velocity.xyz, surface_normal) < 0.0)
                        particle.velocity.xyz = reflect(particle.velocity.xyz, surface_normal) * u_Restitution; 
                } 
            }

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