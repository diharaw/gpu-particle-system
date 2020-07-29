// ------------------------------------------------------------------
// OUTPUT VARIABLES  ------------------------------------------------
// ------------------------------------------------------------------

out vec3 FS_OUT_Color;

// ------------------------------------------------------------------
// INPUT VARIABLES  -------------------------------------------------
// ------------------------------------------------------------------

in vec3 FS_IN_WorldPos;
in vec3 FS_IN_Normal;

// ------------------------------------------------------------------
// UNIFORMS ---------------------------------------------------------
// ------------------------------------------------------------------

uniform mat4      u_LightViewProj;
uniform vec3      u_LightColor;
uniform vec3      u_Color;
uniform vec3      u_Direction;
uniform float     u_Bias;
uniform sampler2D s_ShadowMap;

// ------------------------------------------------------------------

float shadow_occlussion(vec3 p)
{
    // Transform frag position into Light-space.
    vec4 light_space_pos = u_LightViewProj * vec4(p, 1.0);

    vec3 proj_coords = light_space_pos.xyz / light_space_pos.w;
    // transform to [0,1] range
    proj_coords = proj_coords * 0.5 + 0.5;
    // get closest depth value from light's perspective (using [0,1] range fragPosLight as coords)
    float closest_depth = texture(s_ShadowMap, proj_coords.xy).r;
    // get depth of current fragment from light's perspective
    float current_depth = proj_coords.z;
    // check whether current frag pos is in shadow
    float bias   = u_Bias;
    float shadow = current_depth - bias > closest_depth ? 1.0 : 0.0;

    return 1.0 - shadow;
}

// ------------------------------------------------------------------
// MAIN -------------------------------------------------------------
// ------------------------------------------------------------------

void main()
{
    float shadow = shadow_occlussion(FS_IN_WorldPos);

    vec3 L = normalize(-u_Direction);
    vec3 N = normalize(FS_IN_Normal);

    FS_OUT_Color = u_Color * clamp(dot(N, L), 0.0, 1.0) * shadow + u_Color * 0.1;
}

// ------------------------------------------------------------------
