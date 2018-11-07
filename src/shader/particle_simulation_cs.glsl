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

layout(std430, binding = 3) buffer ParticleIndices_t
{
    uint count;
    uint indices[]
} AliveIndicesPostSim;

layout(std430, binding = 4) buffer ParticleDrawArgs_t
{
    uint count;
    uint instance_count;
    uint first;
    uint base_instance;
} ParticleDrawArgs;

uniform float u_DeltaTime;

// ------------------------------------------------------------------
// FUNCTIONS --------------------------------------------------------
// ------------------------------------------------------------------

void push_dead_index(uint index)
{
    uint insert_idx = atomicAdd(DeadIndices.count, 1);
    DeadIndices.indices[insert_idx] = index;
}

uint pop_dead_index()
{
    uint index = atomicAdd(DeadIndices.count, -1);
    return DeadIndices.indices[index - 1];
}

void push_alive_index(uint index)
{
    uint insert_idx = atomicAdd(AliveIndicesPostSim.alive, 1);
    AliveIndicesPostSim.indices[insert_idx] = index;
}

uint pop_alive_index()
{
    uint index = atomicAdd(AliveIndicesPreSim.alive, -1);
    return AliveIndicesPreSim.indices[index - 1];
}

// ------------------------------------------------------------------
// MAIN -------------------------------------------------------------
// ------------------------------------------------------------------

void main()
{
    uint index = gl_GlobalInvocationID.x;

    if (index < ParticleData.simulation_count)
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

            particle.position += particle.velocity;

            ParticleData.particles[particle_index] = particle;

            // Append index back into AliveIndices list
            push_alive_index(particle_index);

            // Increment draw count
            atomicAdd(ParticleDrawArgs.instance_count, 1);
        }
    }
}