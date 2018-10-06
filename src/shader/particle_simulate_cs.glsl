// ------------------------------------------------------------------
// INPUTS -----------------------------------------------------------
// ------------------------------------------------------------------

layout (local_size_x = 1, local_size_y = 1) in;
layout (binding = 0, rgba32f) uniform image2D img_framebuffer;

// ------------------------------------------------------------------
// UNIFORMS ---------------------------------------------------------
// ------------------------------------------------------------------

uniform int u_NumFrames;
uniform float u_Accum;
uniform float u_FOV;
uniform float u_AspectRatio;
uniform vec2 u_Resolution;
uniform mat4 u_InvViewMat;
uniform mat4 u_InvProjectionMat;

// ------------------------------------------------------------------
// GLOBALS ----------------------------------------------------------
// ------------------------------------------------------------------

uint g_state = 0;

// ------------------------------------------------------------------
// CONSTANTS --------------------------------------------------------
// ------------------------------------------------------------------

const float kPI = 3.14159265359;
const int kSamplesPerPixel = 4;

#define MATERIAL_LAMBERTIAN 0
#define MATERIAL_METAL 1
#define MATERIAL_DIELECTRIC 2

// ------------------------------------------------------------------
// STRUCTURES -------------------------------------------------------
// ------------------------------------------------------------------

struct Ray
{
    vec3 direction;
    vec3 origin;
};

struct HitRecord
{
    float t;
    vec3 position;
    vec3 normal;
    int material_id;
};

struct Metal
{
    float roughness;
};

struct Dielectric
{
    float ref_idx;
};

struct Material
{
    int type;
    vec3 albedo;
    Metal metal;
    Dielectric dielectric;
};

struct Sphere
{
    int material_id;
    float radius;
    vec3 position;
};

struct Scene
{   
    int num_spheres;
    int num_materials;
    Sphere spheres[32];
    Material materials[32];
};

// ------------------------------------------------------------------
// FUNCTIONS --------------------------------------------------------
// ------------------------------------------------------------------

uint rand(inout uint state)
{
    uint x = state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 15;
    state = x;
    return x;
}

// ------------------------------------------------------------------

float random_float_01(inout uint state)
{
    return (rand(state) & 0xFFFFFF) / 16777216.0f;
}

// ------------------------------------------------------------------

vec3 random_in_unit_disk(inout uint state)
{
    float a = random_float_01(state) * 2.0f * 3.1415926f;
    vec2 xy = vec2(cos(a), sin(a));
    xy *= sqrt(random_float_01(state));
    return vec3(xy, 0);
}

// ------------------------------------------------------------------

vec3 random_in_unit_sphere(inout uint state)
{
    float z = random_float_01(state) * 2.0f - 1.0f;
    float t = random_float_01(state) * 2.0f * 3.1415926f;
    float r = sqrt(max(0.0, 1.0f - z * z));
    float x = r * cos(t);
    float y = r * sin(t);
    vec3 res = vec3(x, y, z);
    res *= pow(random_float_01(state), 1.0 / 3.0);
    return res;
}

// ------------------------------------------------------------------

vec3 random_unit_vector(inout uint state)
{
    float z = random_float_01(state) * 2.0f - 1.0f;
    float a = random_float_01(state) * 2.0f * 3.1415926f;
    float r = sqrt(1.0f - z * z);
    float x = r * cos(a);
    float y = r * sin(a);
    return vec3(x, y, z);
}

// ------------------------------------------------------------------

Ray compute_ray(float x, float y)
{
    x = x * 2.0 - 1.0;
    y = y * 2.0 - 1.0;

    vec4 clip_pos = vec4(x, y, -1.0, 1.0);
    vec4 view_pos = u_InvProjectionMat * clip_pos;

    vec3 dir = vec3(u_InvViewMat * vec4(view_pos.x, view_pos.y, -1.0, 0.0));
    dir = normalize(dir);

    vec4 origin = u_InvViewMat * vec4(0.0, 0.0, 0.0, 1.0);
    origin.xyz /= origin.w;

    Ray r;

    r.origin = origin.xyz;
    r.direction = dir;

    return r;
}

// ------------------------------------------------------------------

Ray create_ray(in vec3 origin, in vec3 dir)
{
    Ray r;

    r.origin = origin;
    r.direction = dir;

    return r;
}

// ------------------------------------------------------------------

bool scatter_lambertian(in Ray in_ray, in HitRecord rec, in Material mat, out vec3 attenuation, out Ray scattered_ray)
{
    vec3 new_dir = rec.position + rec.normal + random_in_unit_sphere(g_state);
    scattered_ray = create_ray(rec.position, normalize(new_dir - rec.position));
    attenuation = mat.albedo;

    return true;
}

// ------------------------------------------------------------------

bool scatter_metal(in Ray in_ray, in HitRecord rec, in Material mat, out vec3 attenuation, out Ray scattered_ray)
{
    vec3 new_dir = reflect(in_ray.direction, rec.normal);
    new_dir = normalize(new_dir + mat.metal.roughness * random_in_unit_sphere(g_state));
    scattered_ray = create_ray(rec.position, normalize(new_dir));
    attenuation = mat.albedo;

    return dot(scattered_ray.direction, rec.normal) > 0;
}

// ------------------------------------------------------------------

bool refract(in vec3 v, in vec3 n, in float ni_over_nt, out vec3 refracted)
{
    vec3 uv = normalize(v);
    float dt = dot(uv, n);
    float discriminant = 1.0 - ni_over_nt * ni_over_nt * (1 - dt * dt);

    if (discriminant > 0)
    {
        refracted = ni_over_nt * (uv - n * dt) - n * sqrt(discriminant);
        return true;
    }
    else
        return false;
}

// ------------------------------------------------------------------

float schlick(float cosine, float ref_idx)
{
    float r0 = (1.0 - ref_idx) / (1.0 + ref_idx);
    r0 = r0 * r0;
    return r0 + (1.0 - r0) * pow((1.0 - cosine), 5);
}

// ------------------------------------------------------------------

bool scatter_dielectric(in Ray in_ray, in HitRecord rec, in Material mat, out vec3 attenuation, out Ray scattered_ray)
{
    vec3 outward_normal;
    vec3 reflected = reflect(in_ray.direction, rec.normal);
    float ni_over_nt;
    attenuation = vec3(1.0, 1.0, 1.0);
    vec3 refracted;
    float reflect_prob;
    float cosine;

    if (dot(in_ray.direction, rec.normal) > 0)
    {
        outward_normal = -rec.normal;
        ni_over_nt = mat.dielectric.ref_idx;
        cosine = mat.dielectric.ref_idx * dot(in_ray.direction, rec.normal) / length(in_ray.direction);
    }
    else
    {
        outward_normal = rec.normal;
        ni_over_nt = 1.0 / mat.dielectric.ref_idx;
        cosine = -dot(in_ray.direction, rec.normal) / length(in_ray.direction);
    }

    if (refract(in_ray.direction, outward_normal, ni_over_nt, refracted))
        reflect_prob = schlick(cosine, mat.dielectric.ref_idx);
    else
        reflect_prob = 1.0;

    if (random_float_01(g_state) < reflect_prob)
        scattered_ray = create_ray(rec.position, reflected);
    else
        scattered_ray = create_ray(rec.position, refracted);

    return true;
}

// ------------------------------------------------------------------

bool ray_sphere_hit(in float t_min, in float t_max, in Ray r, in Sphere s, out HitRecord hit)
{
    vec3 oc = r.origin - s.position;
    float a = dot(r.direction, r.direction);
    float b = dot(oc, r.direction);
    float c = dot(oc, oc) - s.radius * s.radius;
    float discriminant = b * b - a * c;

    if (discriminant > 0.0)
    {
        float temp = (-b - sqrt(b * b - a * c)) / a;

        if (temp < t_max && temp > t_min)
        {
            hit.t = temp;
            hit.position = r.origin + r.direction * hit.t;
            hit.normal = normalize((hit.position - s.position) / s.radius);
            hit.material_id = s.material_id;

            return true;
        }

        temp = (-b + sqrt(b * b - a * c)) / a;

        if (temp < t_max && temp > t_min)
        {
            hit.t = temp;
            hit.position = r.origin + r.direction * hit.t;
            hit.normal = normalize((hit.position - s.position) / s.radius);
            hit.material_id = s.material_id;

            return true;
        }
    }

    return false;
}

bool ray_scene_hit(in float t_min, in float t_max, in Ray ray, in Scene scene, out HitRecord rec)
{
    float closest = t_max;
    bool hit_anything = false;

    for (int i = 0; i < scene.num_spheres; i++)
    {
        if (ray_sphere_hit(t_min, closest, ray, scene.spheres[i], rec))
        {
            hit_anything = true;
            closest = rec.t;
        }
    }

    return hit_anything;
}

bool trace_once(in Ray ray, in Scene scene, out HitRecord rec)
{
    if (ray_scene_hit(0.001, 100000.0, ray, scene, rec))
        return true;
    else
        return false;
}

vec3 trace(in Ray ray, in Scene scene)
{
    HitRecord rec;
    Ray new_ray = ray;
    vec3 attenuation = vec3(0.0, 0.0, 0.0);
    int depth = 0;
    vec3 color = vec3(1.0, 1.0, 1.0);
   
    while (depth < 50)
    {
        if (trace_once(new_ray, scene, rec))
        {
            Ray scattered_ray;
                
            if (scene.materials[rec.material_id].type == MATERIAL_LAMBERTIAN)
            {
                if (scatter_lambertian(new_ray, rec, scene.materials[rec.material_id], attenuation, scattered_ray))
                {
                    color *= attenuation;
                    new_ray = scattered_ray;
                }
                else
                {
                    attenuation = vec3(0.0, 0.0, 0.0);
                    color *= attenuation;
                    break;
                }
            }
            else if (scene.materials[rec.material_id].type == MATERIAL_METAL)
            {
                if (scatter_metal(new_ray, rec, scene.materials[rec.material_id], attenuation, scattered_ray))
                {
                    color *= attenuation;
                    new_ray = scattered_ray;
                }
                else
                {
                    attenuation = vec3(0.0, 0.0, 0.0);
                    color *= attenuation;
                    break;
                }
            }
            else if (scene.materials[rec.material_id].type == MATERIAL_DIELECTRIC)
            {
                if (scatter_dielectric(new_ray, rec, scene.materials[rec.material_id], attenuation, scattered_ray))
                {
                    color *= attenuation;
                    new_ray = scattered_ray;
                }
                else
                {
                    attenuation = vec3(0.0, 0.0, 0.0);
                    color *= attenuation;
                    break;
                }
            }
        }
        else
        {
            float t = 0.5 * (ray.direction.y + 1.0);
            vec3 sky_color = (1.0 - t) * vec3(1.0) + t * vec3(0.5, 0.7, 1.0);
            color *= sky_color;
            break;
        }

        depth++;
    }

    if (depth < 50) 
        return color;
    else
        return vec3(0.0, 0.0, 0.0);
}

// ------------------------------------------------------------------
// MAIN -------------------------------------------------------------
// ------------------------------------------------------------------

void main()
{
    ivec2 pixel_coords = ivec2(gl_GlobalInvocationID.xy);
   
    g_state = gl_GlobalInvocationID.x * 1973 + gl_GlobalInvocationID.y * 9277 + uint(u_NumFrames) * 2699 | 1;

    Scene scene;

    scene.num_spheres = 6;
    scene.num_materials = 5;

    // Materials

    // Blue Lambertian
    scene.materials[0].type = MATERIAL_LAMBERTIAN;
    scene.materials[0].albedo = vec3(0.3, 0.3, 0.8);

    // Floor
    scene.materials[1].type = MATERIAL_LAMBERTIAN;
    scene.materials[1].albedo = vec3(0.2, 0.2, 0.2);

    // Yellow Metal
    scene.materials[2].type = MATERIAL_METAL;
    scene.materials[2].albedo = vec3(0.8, 0.6, 0.2);
    scene.materials[2].metal.roughness = 1.0;

    // Glass
    scene.materials[3].type = MATERIAL_DIELECTRIC;
    scene.materials[3].albedo = vec3(0.8, 0.8, 0.8);
    scene.materials[3].dielectric.ref_idx = 1.5;

    // Grey Metal
    scene.materials[4].type = MATERIAL_METAL;
    scene.materials[4].albedo = vec3(0.8, 0.8, 0.8);
    scene.materials[4].metal.roughness = 0.3;

    // Spheres

    scene.spheres[0].radius = 10.0;
    scene.spheres[0].position = vec3(0.0, 0.0, 20.0);
    scene.spheres[0].material_id = 0;

    scene.spheres[1].radius = 10.0;
    scene.spheres[1].position = vec3(20.0, 0.0, 0.0);
    scene.spheres[1].material_id = 2;

    scene.spheres[2].radius = 10.0;
    scene.spheres[2].position = vec3(-20.0, 0.0, 0.0);
    scene.spheres[2].material_id = 3;

    scene.spheres[3].radius = -9.5;
    scene.spheres[3].position = vec3(-20.0, 0.0, 0.0);
    scene.spheres[3].material_id = 3;

    scene.spheres[4].radius = 1000.0;
    scene.spheres[4].position = vec3(0.0, -1010.0, 0.0);
    scene.spheres[4].material_id = 1;

    scene.spheres[5].radius = 10.0;
    scene.spheres[5].position = vec3(0.0, 0.0, -20.0);
    scene.spheres[5].material_id = 4;

    vec3 color = vec3(0.0);

    for (int i = 0; i < kSamplesPerPixel; i++)
    {
        vec2 altered_coord = vec2(pixel_coords.x + random_float_01(g_state), pixel_coords.y +  + random_float_01(g_state));
        vec2 tex_coord = altered_coord / u_Resolution;
        Ray ray = compute_ray(tex_coord.x, tex_coord.y);
        color += trace(ray, scene);
    }

    color /= float(kSamplesPerPixel);

    vec3 prev_color = imageLoad(img_framebuffer, pixel_coords).rgb;

    vec3 final = mix(color, prev_color, u_Accum);

    imageStore(img_framebuffer, pixel_coords, vec4(final, 1.0));
}

// ------------------------------------------------------------------