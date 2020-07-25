#include <simplex_noise.glsl>

float rand(vec3 co)
{
    return fract(sin(dot(co.xyz, vec3(12.9898, 78.233, 45.5432))) * 43758.5453);
}
vec3 randomPointOnSphere(float u, float v, float radius)
{
    float theta = 2 * PI * u;
    float phi   = acos(2 * v - 1);
    float x     = radius * sin(phi) * cos(theta);
    float y     = radius * sin(phi) * sin(theta);
    float z     = radius * cos(phi);
    return vec3(x, y, z);
}
vec3 curlNoise(vec3 coord)
{
    vec3 dx = vec3(EPSILON, 0.0, 0.0);
    vec3 dy = vec3(0.0, EPSILON, 0.0);
    vec3 dz = vec3(0.0, 0.0, EPSILON);

    vec3 dpdx0 = vec3(snoise(coord - dx));
    vec3 dpdx1 = vec3(snoise(coord + dx));
    vec3 dpdy0 = vec3(snoise(coord - dy));
    vec3 dpdy1 = vec3(snoise(coord + dy));
    vec3 dpdz0 = vec3(snoise(coord - dz));
    vec3 dpdz1 = vec3(snoise(coord + dz));

    float x = dpdy1.z - dpdy0.z + dpdz1.y - dpdz0.y;
    float y = dpdz1.x - dpdz0.x + dpdx1.z - dpdx0.z;
    float z = dpdx1.y - dpdx0.y + dpdy1.x - dpdy0.x;

    return vec3(x, y, z) / EPSILON * 2.0;
}