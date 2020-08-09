#define _USE_MATH_DEFINES
#include <ogl.h>
#include <application.h>
#include <mesh.h>
#include <camera.h>
#include <material.h>
#include <memory>
#include <iostream>
#include <stack>
#include <random>
#include <chrono>
#include <random>
#include <bruneton_sky_model.h>
#include <shadow_map.h>
#include "imgui_curve_editor.h"
#include "imgui_color_gradient.h"

#undef min
#undef max
#define CAMERA_FAR_PLANE 1000.0f
#define MAX_PARTICLES 1000000
#define GRADIENT_SAMPLES 32
#define LOCAL_SIZE 32

struct GlobalUniforms
{
    DW_ALIGNED(16)
    glm::mat4 view_proj;
};

enum EmissionShape
{
    EMISSION_SHAPE_SPHERE,
    EMISSION_SHAPE_BOX,
    EMISSION_SHAPE_CONE
};

enum DirectionType
{
    DIRECTION_TYPE_SINGLE,
    DIRECTION_TYPE_OUTWARDS
};

enum PropertyChangeType
{
    PROPERTY_CONSTANT,
    PROPERTY_OVER_TIME
};

class GPUParticleSystem : public dw::Application
{
protected:
    // -----------------------------------------------------------------------------------------------------------------------------------

    bool init(int argc, const char* argv[]) override
    {
        // Create GPU resources.
        if (!create_shaders())
            return false;

        load_mesh();
        create_buffers();
        create_textures();
        create_framebuffers();

        // Create camera.
        create_camera();
        particle_initialize();

        glEnable(GL_MULTISAMPLE);

        m_debug_draw.set_distance_fade(true);
        m_debug_draw.set_depth_test(true);
        m_debug_draw.set_fade_start(5.0f);
        m_debug_draw.set_fade_end(10.0f);

        m_color_gradient.getMarks().clear();
        m_color_gradient.addMark(0.0f, ImColor(1.0f, 0.0f, 0.0f));
        m_color_gradient.addMark(0.225f, ImColor(1.0f, 1.0f, 0.0f));
        m_color_gradient.addMark(0.4f, ImColor(0.086f, 0.443f, 0.039f));
        m_color_gradient.addMark(0.6f, ImColor(0.0f, 0.983f, 0.77f));
        m_color_gradient.addMark(0.825f, ImColor(0.0f, 0.011f, 0.969f));
        m_color_gradient.addMark(1.0f, ImColor(0.939f, 0.0f, 1.0f));

        update_color_over_time_texture();
        update_size_over_time_texture();

        m_generator = std::mt19937(m_random());

        m_sky_model.initialize();
        m_shadow_map.initialize(2048);

        m_sky_model.set_sun_angle(glm::radians(-30.0f));
        m_shadow_map.set_direction(m_sky_model.direction());
        m_shadow_map.set_extents(12.0f);

        glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

        return true;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void update(double delta) override
    {
        m_accumulator += float(m_delta_seconds);

        std::uniform_real_distribution<> distribution(1.0f, 10000.0f);

        m_seeds                = glm::vec3(distribution(m_generator), distribution(m_generator), distribution(m_generator));
        m_max_active_particles = m_max_lifetime * m_emission_rate;

        if (m_debug_gui)
            debug_gui();

        // Update camera.
        update_camera();

        render_depth_prepass();

        particle_kickoff();
        particle_emission();
        particle_simulation();

        m_sky_model.update_cubemap();
        render_shadow_map();
        render_lit_scene();

        m_sky_model.render_skybox(0, 0, m_width, m_height, m_main_camera->m_view, m_main_camera->m_projection, nullptr);

        if (m_show_grid)
            m_debug_draw.grid(m_main_camera->m_view_projection, 1.0f, 10.0f);

        m_debug_draw.render(nullptr, m_width, m_height, m_main_camera->m_view_projection, m_main_camera->m_position);

        m_pre_sim_idx  = m_pre_sim_idx == 0 ? 1 : 0;
        m_post_sim_idx = m_post_sim_idx == 0 ? 1 : 0;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void shutdown() override
    {
        m_shadow_map.shutdown();
        m_sky_model.shutdown();
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void window_resized(int width, int height) override
    {
        // Override window resized method to update camera projection.
        m_main_camera->update_projection(60.0f, 0.1f, CAMERA_FAR_PLANE, float(m_width) / float(m_height));
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void key_pressed(int code) override
    {
        // Handle forward movement.
        if (code == GLFW_KEY_W)
            m_heading_speed = m_camera_speed;
        else if (code == GLFW_KEY_S)
            m_heading_speed = -m_camera_speed;

        // Handle sideways movement.
        if (code == GLFW_KEY_A)
            m_sideways_speed = -m_camera_speed;
        else if (code == GLFW_KEY_D)
            m_sideways_speed = m_camera_speed;

        if (code == GLFW_KEY_SPACE)
            m_mouse_look = true;

        if (code == GLFW_KEY_G)
            m_debug_gui = !m_debug_gui;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void key_released(int code) override
    {
        // Handle forward movement.
        if (code == GLFW_KEY_W || code == GLFW_KEY_S)
            m_heading_speed = 0.0f;

        // Handle sideways movement.
        if (code == GLFW_KEY_A || code == GLFW_KEY_D)
            m_sideways_speed = 0.0f;

        if (code == GLFW_KEY_SPACE)
            m_mouse_look = false;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void mouse_pressed(int code) override
    {
        // Enable mouse look.
        if (code == GLFW_MOUSE_BUTTON_RIGHT)
            m_mouse_look = true;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void mouse_released(int code) override
    {
        // Disable mouse look.
        if (code == GLFW_MOUSE_BUTTON_RIGHT)
            m_mouse_look = false;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

protected:
    // -----------------------------------------------------------------------------------------------------------------------------------

    dw::AppSettings intial_app_settings() override
    {
        dw::AppSettings settings;

        settings.resizable             = true;
        settings.maximized             = false;
        settings.refresh_rate          = 60;
        settings.major_ver             = 4;
        settings.width                 = 1920;
        settings.height                = 1080;
        settings.title                 = "GPU Particle System (c) 2020 Dihara Wijetunga";
        settings.enable_debug_callback = false;

        return settings;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

private:
    // -----------------------------------------------------------------------------------------------------------------------------------

    void debug_gui()
    {
        std::string active_count = "Max Active Particles: " + std::to_string(m_max_active_particles);

        ImGui::Text(active_count.c_str());
        ImGui::InputFloat3("Position", &m_position.x);
        ImGui::InputInt("Emission Rate (Particles/Second)", &m_emission_rate);
        ImGui::InputFloat("Min Lifetime", &m_min_lifetime);
        ImGui::InputFloat("Max Lifetime", &m_max_lifetime);
        ImGui::InputFloat("Min Initial Speed", &m_min_initial_speed);
        ImGui::InputFloat("Max Initial Speed", &m_max_initial_speed);
        ImGui::InputFloat3("Constant Velocity", &m_constant_velocity.x);
        ImGui::InputFloat("Viscosity", &m_viscosity);
        ImGui::Checkbox("Affected by Gravity", &m_affected_by_gravity);
        ImGui::Checkbox("Depth Buffer Collision", &m_depth_buffer_collision);
        if (m_depth_buffer_collision)
            ImGui::SliderFloat("Restitution", &m_restitution, 0.0f, 1.0f);
        ImGui::SliderFloat("Sphere Radius", &m_sphere_radius, 0.1f, 25.0f);

        if (ImGui::InputFloat("Start Size", &m_start_size))
            update_size_over_time_texture();

        if (ImGui::InputFloat("End Size", &m_end_size))
            update_size_over_time_texture();

        if (ImGui::Bezier("Size Over Time", m_size_curve))
            update_size_over_time_texture();

        if (ImGui::GradientEditor("Color Over Time:", &m_color_gradient, m_dragging_mark, m_selected_mark))
            update_color_over_time_texture();

        ImGui::Checkbox("Show Grid", &m_show_grid);
        float sun_angle = m_sky_model.sun_angle();
        ImGui::SliderAngle("Sun Angle", &sun_angle, 0.0f, -180.0f);
        m_sky_model.set_sun_angle(sun_angle);
        m_shadow_map.set_direction(m_sky_model.direction());
        ImGui::InputFloat("Shadow Bias", &m_shadow_bias);
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void render_particles(std::unique_ptr<dw::gl::Program>& program, glm::mat4 view, glm::mat4 projection)
    {
        glEnable(GL_DEPTH_TEST);

        program->use();

        program->set_uniform("u_Rotation", glm::radians(m_rotation));
        program->set_uniform("u_View", view);
        program->set_uniform("u_Proj", projection);

        if (program->set_uniform("s_ColorOverTime", 0))
            m_color_over_time->bind(0);

        if (program->set_uniform("s_SizeOverTime", 1))
            m_size_over_time->bind(1);

        m_particle_data_ssbo->bind_base(0);
        m_alive_indices_ssbo[m_post_sim_idx]->bind_base(1);

        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_draw_indirect_args_ssbo->handle());

        glDrawArraysIndirect(GL_TRIANGLES, 0);
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void render_mesh(dw::Mesh* mesh, glm::mat4 model, std::unique_ptr<dw::gl::Program>& program)
    {
        program->set_uniform("u_Model", model);

        // Bind vertex array.
        mesh->mesh_vertex_array()->bind();

        for (uint32_t i = 0; i < mesh->sub_mesh_count(); i++)
        {
            dw::SubMesh& submesh = mesh->sub_meshes()[i];

            program->set_uniform("u_Color", glm::vec3(0.7f));
            program->set_uniform("u_Direction", m_sky_model.direction());
            program->set_uniform("u_LightColor", m_shadow_map.color());

            // Issue draw call.
            glDrawElementsBaseVertex(GL_TRIANGLES, submesh.index_count, GL_UNSIGNED_INT, (void*)(sizeof(unsigned int) * submesh.base_index), submesh.base_vertex);
        }
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void render_scene(std::unique_ptr<dw::gl::Program>& program)
    {
        // Bind shader program.
        program->use();

        program->set_uniform("u_LightViewProj", m_shadow_map.projection() * m_shadow_map.view());
        program->set_uniform("u_ViewProj", m_main_camera->m_view_projection);
        program->set_uniform("u_Bias", m_shadow_bias);

        if (program->set_uniform("s_ShadowMap", 0))
            m_shadow_map.texture()->bind(0);

        // Draw scene.
        render_mesh(m_playground.get(), glm::mat4(1.0f), program);
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void render_lit_scene()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glViewport(0, 0, m_width, m_height);

        render_particles(m_particle_program, m_main_camera->m_view, m_main_camera->m_projection);
        render_scene(m_mesh_lit_program);
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void render_shadow_map()
    {
        m_shadow_map.begin_render();

        render_particles(m_particle_depth_program, m_shadow_map.view(), m_shadow_map.projection());

        m_mesh_depth_program->use();
        m_mesh_depth_program->set_uniform("u_ViewProj", m_shadow_map.projection() * m_shadow_map.view());
        render_mesh(m_playground.get(), glm::mat4(1.0f), m_mesh_depth_program);

        m_shadow_map.end_render();
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void render_depth_prepass()
    {
        m_scene_depth_fbo->bind();
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glViewport(0, 0, m_width, m_height);

        render_scene(m_depth_prepass_program);
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void particle_initialize()
    {
        m_particle_initialize_program->use();

        m_dead_indices_ssbo->bind_base(0);
        m_counters_ssbo->bind_base(1);

        m_particle_initialize_program->set_uniform("u_MaxParticles", MAX_PARTICLES);

        glDispatchCompute(ceil(float(MAX_PARTICLES) / float(LOCAL_SIZE)), 1, 1);

        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void particle_kickoff()
    {
        m_particle_update_kickoff_program->use();

        m_emission_delta = 1.0f / float(m_emission_rate); // In seconds

        m_particles_per_frame = 0;

        while (m_accumulator >= m_emission_delta)
        {
            m_accumulator -= m_emission_delta;
            m_particles_per_frame++;
        }

        m_particle_update_kickoff_program->set_uniform("u_ParticlesPerFrame", m_particles_per_frame);
        m_particle_update_kickoff_program->set_uniform("u_PreSimIdx", m_pre_sim_idx);
        m_particle_update_kickoff_program->set_uniform("u_PostSimIdx", m_post_sim_idx);

        m_particle_data_ssbo->bind_base(0);
        m_dispatch_emission_indirect_args_ssbo->bind_base(1);
        m_dispatch_simulation_indirect_args_ssbo->bind_base(2);
        m_draw_indirect_args_ssbo->bind_base(3);
        m_counters_ssbo->bind_base(4);

        glDispatchCompute(1, 1, 1);

        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void particle_emission()
    {
        m_particle_emission_program->use();

        m_particle_emission_program->set_uniform("u_Seeds", m_seeds);
        m_particle_emission_program->set_uniform("u_Position", m_position);
        m_particle_emission_program->set_uniform("u_MinInitialSpeed", m_min_initial_speed);
        m_particle_emission_program->set_uniform("u_MaxInitialSpeed", m_max_initial_speed);
        m_particle_emission_program->set_uniform("u_MinLifetime", m_max_lifetime);
        m_particle_emission_program->set_uniform("u_MaxLifetime", m_max_lifetime);
        m_particle_emission_program->set_uniform("u_EmissionShape", int(m_emission_shape));
        m_particle_emission_program->set_uniform("u_DirectionType", int(m_direction_type));
        m_particle_emission_program->set_uniform("u_Direction", m_direction);
        m_particle_emission_program->set_uniform("u_SphereRadius", m_sphere_radius);
        m_particle_emission_program->set_uniform("u_PreSimIdx", m_pre_sim_idx);

        m_particle_data_ssbo->bind_base(0);
        m_dead_indices_ssbo->bind_base(1);
        m_alive_indices_ssbo[m_pre_sim_idx]->bind_base(2);
        m_counters_ssbo->bind_base(3);

        glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, m_dispatch_emission_indirect_args_ssbo->handle());

        glDispatchComputeIndirect(0);

        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void particle_simulation()
    {
        m_particle_simulation_program->use();

        m_particle_simulation_program->set_uniform("u_DeltaTime", float(m_delta_seconds));
        m_particle_simulation_program->set_uniform("u_Viscosity", m_viscosity);
        m_particle_simulation_program->set_uniform("u_PreSimIdx", m_pre_sim_idx);
        m_particle_simulation_program->set_uniform("u_PostSimIdx", m_post_sim_idx);
        m_particle_simulation_program->set_uniform("u_ConstantVelocity", m_constant_velocity);
        m_particle_simulation_program->set_uniform("u_AffectedByGravity", (int)m_affected_by_gravity);
        m_particle_simulation_program->set_uniform("u_DepthBufferCollision", (int)m_depth_buffer_collision);
        m_particle_simulation_program->set_uniform("u_Restitution", m_restitution);
        m_particle_simulation_program->set_uniform("u_ViewProj", m_main_camera->m_view_projection);

        if (m_particle_simulation_program->set_uniform("s_Depth", 0))
            m_scene_depth_rt->bind(0);

        if (m_particle_simulation_program->set_uniform("s_Normals", 1))
            m_scene_normals_rt->bind(1);

        m_particle_data_ssbo->bind_base(0);
        m_dead_indices_ssbo->bind_base(1);
        m_alive_indices_ssbo[m_pre_sim_idx]->bind_base(2);
        m_alive_indices_ssbo[m_post_sim_idx]->bind_base(3);
        m_draw_indirect_args_ssbo->bind_base(4);
        m_counters_ssbo->bind_base(5);

        glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, m_dispatch_simulation_indirect_args_ssbo->handle());

        glDispatchComputeIndirect(0);

        glMemoryBarrier(GL_ALL_BARRIER_BITS);
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void load_mesh()
    {
        m_playground = dw::Mesh::load("Particle_Playground.obj");
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    bool create_shaders()
    {
        {
            // Create general shaders
            m_particle_vs                = std::unique_ptr<dw::gl::Shader>(dw::gl::Shader::create_from_file(GL_VERTEX_SHADER, "shader/particle_vs.glsl"));
            m_particle_fs                = std::unique_ptr<dw::gl::Shader>(dw::gl::Shader::create_from_file(GL_FRAGMENT_SHADER, "shader/particle_fs.glsl"));
            m_particle_initialize_cs     = std::unique_ptr<dw::gl::Shader>(dw::gl::Shader::create_from_file(GL_COMPUTE_SHADER, "shader/particle_initialize_cs.glsl"));
            m_particle_update_kickoff_cs = std::unique_ptr<dw::gl::Shader>(dw::gl::Shader::create_from_file(GL_COMPUTE_SHADER, "shader/particle_update_kickoff_cs.glsl"));
            m_particle_emission_cs       = std::unique_ptr<dw::gl::Shader>(dw::gl::Shader::create_from_file(GL_COMPUTE_SHADER, "shader/particle_emission_cs.glsl"));
            m_particle_simulation_cs     = std::unique_ptr<dw::gl::Shader>(dw::gl::Shader::create_from_file(GL_COMPUTE_SHADER, "shader/particle_simulation_cs.glsl"));
            m_mesh_vs                    = std::unique_ptr<dw::gl::Shader>(dw::gl::Shader::create_from_file(GL_VERTEX_SHADER, "shader/mesh_vs.glsl"));
            m_mesh_fs                    = std::unique_ptr<dw::gl::Shader>(dw::gl::Shader::create_from_file(GL_FRAGMENT_SHADER, "shader/mesh_fs.glsl"));
            m_depth_fs                   = std::unique_ptr<dw::gl::Shader>(dw::gl::Shader::create_from_file(GL_FRAGMENT_SHADER, "shader/depth_fs.glsl"));
            m_depth_prepass_fs                   = std::unique_ptr<dw::gl::Shader>(dw::gl::Shader::create_from_file(GL_FRAGMENT_SHADER, "shader/depth_prepass_fs.glsl"));

            {
                if (!m_particle_vs || !m_particle_fs)
                {
                    DW_LOG_FATAL("Failed to create Shaders");
                    return false;
                }

                // Create general shader program
                dw::gl::Shader* shaders[] = { m_particle_vs.get(), m_particle_fs.get() };
                m_particle_program        = std::make_unique<dw::gl::Program>(2, shaders);

                if (!m_particle_program)
                {
                    DW_LOG_FATAL("Failed to create Shader Program");
                    return false;
                }
            }

            {
                if (!m_particle_vs || !m_depth_fs)
                {
                    DW_LOG_FATAL("Failed to create Shaders");
                    return false;
                }

                // Create general shader program
                dw::gl::Shader* shaders[] = { m_particle_vs.get(), m_depth_fs.get() };
                m_particle_depth_program  = std::make_unique<dw::gl::Program>(2, shaders);

                if (!m_particle_depth_program)
                {
                    DW_LOG_FATAL("Failed to create Shader Program");
                    return false;
                }
            }

            {
                if (!m_mesh_vs || !m_mesh_fs)
                {
                    DW_LOG_FATAL("Failed to create Shaders");
                    return false;
                }

                // Create general shader program
                dw::gl::Shader* shaders[] = { m_mesh_vs.get(), m_mesh_fs.get() };
                m_mesh_lit_program        = std::make_unique<dw::gl::Program>(2, shaders);

                if (!m_mesh_lit_program)
                {
                    DW_LOG_FATAL("Failed to create Shader Program");
                    return false;
                }
            }

            {
                if (!m_mesh_vs || !m_depth_prepass_fs)
                {
                    DW_LOG_FATAL("Failed to create Shaders");
                    return false;
                }

                // Create general shader program
                dw::gl::Shader* shaders[] = { m_mesh_vs.get(), m_depth_prepass_fs.get() };
                m_depth_prepass_program        = std::make_unique<dw::gl::Program>(2, shaders);

                if (!m_depth_prepass_program)
                {
                    DW_LOG_FATAL("Failed to create Shader Program");
                    return false;
                }
            }

            {
                if (!m_mesh_vs || !m_depth_fs)
                {
                    DW_LOG_FATAL("Failed to create Shaders");
                    return false;
                }

                // Create general shader program
                dw::gl::Shader* shaders[] = { m_mesh_vs.get(), m_depth_fs.get() };
                m_mesh_depth_program      = std::make_unique<dw::gl::Program>(2, shaders);

                if (!m_mesh_depth_program)
                {
                    DW_LOG_FATAL("Failed to create Shader Program");
                    return false;
                }
            }

            {
                if (!m_particle_initialize_cs)
                {
                    DW_LOG_FATAL("Failed to create Shaders");
                    return false;
                }

                // Create general shader program
                dw::gl::Shader* shaders[]     = { m_particle_initialize_cs.get() };
                m_particle_initialize_program = std::make_unique<dw::gl::Program>(1, shaders);

                if (!m_particle_initialize_program)
                {
                    DW_LOG_FATAL("Failed to create Shader Program");
                    return false;
                }
            }

            {
                if (!m_particle_update_kickoff_cs)
                {
                    DW_LOG_FATAL("Failed to create Shaders");
                    return false;
                }

                // Create general shader program
                dw::gl::Shader* shaders[]         = { m_particle_update_kickoff_cs.get() };
                m_particle_update_kickoff_program = std::make_unique<dw::gl::Program>(1, shaders);

                if (!m_particle_update_kickoff_program)
                {
                    DW_LOG_FATAL("Failed to create Shader Program");
                    return false;
                }
            }

            {
                if (!m_particle_emission_cs)
                {
                    DW_LOG_FATAL("Failed to create Shaders");
                    return false;
                }

                // Create general shader program
                dw::gl::Shader* shaders[]   = { m_particle_emission_cs.get() };
                m_particle_emission_program = std::make_unique<dw::gl::Program>(1, shaders);

                if (!m_particle_emission_program)
                {
                    DW_LOG_FATAL("Failed to create Shader Program");
                    return false;
                }
            }

            {
                if (!m_particle_simulation_cs)
                {
                    DW_LOG_FATAL("Failed to create Shaders");
                    return false;
                }

                // Create general shader program
                dw::gl::Shader* shaders[]     = { m_particle_simulation_cs.get() };
                m_particle_simulation_program = std::make_unique<dw::gl::Program>(1, shaders);

                if (!m_particle_simulation_program)
                {
                    DW_LOG_FATAL("Failed to create Shader Program");
                    return false;
                }
            }
        }

        return true;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    bool create_buffers()
    {
        struct Particle
        {
            glm::vec4 lifetime;
            glm::vec4 velocity;
            glm::vec4 position;
            glm::vec4 color;
        };

        m_draw_indirect_args_ssbo                = std::make_unique<dw::gl::ShaderStorageBuffer>(GL_STATIC_DRAW, sizeof(int32_t) * 4, nullptr);
        m_dispatch_emission_indirect_args_ssbo   = std::make_unique<dw::gl::ShaderStorageBuffer>(GL_STATIC_DRAW, sizeof(int32_t) * 3, nullptr);
        m_dispatch_simulation_indirect_args_ssbo = std::make_unique<dw::gl::ShaderStorageBuffer>(GL_STATIC_DRAW, sizeof(int32_t) * 3, nullptr);
        m_particle_data_ssbo                     = std::make_unique<dw::gl::ShaderStorageBuffer>(GL_STATIC_DRAW, sizeof(Particle) * MAX_PARTICLES, nullptr);
        m_alive_indices_ssbo[0]                  = std::make_unique<dw::gl::ShaderStorageBuffer>(GL_STATIC_DRAW, sizeof(int32_t) * MAX_PARTICLES, nullptr);
        m_alive_indices_ssbo[1]                  = std::make_unique<dw::gl::ShaderStorageBuffer>(GL_STATIC_DRAW, sizeof(int32_t) * MAX_PARTICLES, nullptr);
        m_dead_indices_ssbo                      = std::make_unique<dw::gl::ShaderStorageBuffer>(GL_STATIC_DRAW, sizeof(int32_t) * MAX_PARTICLES, nullptr);
        m_counters_ssbo                          = std::make_unique<dw::gl::ShaderStorageBuffer>(GL_STATIC_DRAW, sizeof(int32_t) * 5, nullptr);

        return true;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void create_framebuffers()
    {
        m_scene_depth_rt = std::make_unique<dw::gl::Texture2D>(m_width, m_height, 1, 1, 1, GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT);
        m_scene_normals_rt = std::make_unique<dw::gl::Texture2D>(m_width, m_height, 1, 1, 1, GL_RGB32F, GL_RGB, GL_FLOAT);

        m_scene_depth_fbo = std::make_unique<dw::gl::Framebuffer>();
        m_scene_depth_fbo->attach_render_target(0, m_scene_normals_rt.get(), 0, 0);
        m_scene_depth_fbo->attach_depth_stencil_target(m_scene_depth_rt.get(), 0, 0);
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void create_textures()
    {
        m_color_over_time = std::make_unique<dw::gl::Texture1D>(GRADIENT_SAMPLES, 1, 1, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
        m_size_over_time  = std::make_unique<dw::gl::Texture1D>(GRADIENT_SAMPLES, 1, 1, GL_R32F, GL_RED, GL_FLOAT);

        m_color_over_time->set_min_filter(GL_NEAREST);
        m_size_over_time->set_min_filter(GL_NEAREST);

        m_color_over_time->set_wrapping(GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
        m_size_over_time->set_wrapping(GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void update_color_over_time_texture()
    {
        float delta = 1.0f / float(GRADIENT_SAMPLES);
        float x     = 0.0f;

        std::vector<uint8_t> samples;

        for (uint32_t i = 0; i < GRADIENT_SAMPLES; i++)
        {
            glm::vec4 color;
            m_color_gradient.getColorAt(x, &color.x);

            samples.push_back(color.x * 255.0f);
            samples.push_back(color.y * 255.0f);
            samples.push_back(color.z * 255.0f);
            samples.push_back(color.w * 255.0f);

            x += delta;
        }

        m_color_over_time->set_data(0, 0, samples.data());
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void update_size_over_time_texture()
    {
        float delta     = 1.0f / float(GRADIENT_SAMPLES);
        float x         = 0.0f;
        float size_diff = m_end_size - m_start_size;

        std::vector<float> samples;

        for (uint32_t i = 0; i < GRADIENT_SAMPLES; i++)
        {
            float size = m_start_size + ImGui::BezierValue(x, m_size_curve) * size_diff;
            samples.push_back(size);

            x += delta;
        }

        m_size_over_time->set_data(0, 0, samples.data());
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void create_camera()
    {
        m_main_camera = std::make_unique<dw::Camera>(60.0f, 0.1f, CAMERA_FAR_PLANE, float(m_width) / float(m_height), glm::vec3(10.0f, 5.0f, 5.0f), glm::vec3(-1.0f, 0.0, 0.0f));
        m_main_camera->set_rotatation_delta(glm::vec3(0.0f, -90.0f, 0.0f));
        m_main_camera->update();
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void update_transforms(dw::Camera* camera)
    {
        // Update camera matrices.
        m_global_uniforms.view_proj = camera->m_projection * camera->m_view;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void update_camera()
    {
        dw::Camera* current = m_main_camera.get();

        float forward_delta = m_heading_speed * m_delta;
        float right_delta   = m_sideways_speed * m_delta;

        current->set_translation_delta(current->m_forward, forward_delta);
        current->set_translation_delta(current->m_right, right_delta);

        m_camera_x = m_mouse_delta_x * m_camera_sensitivity;
        m_camera_y = m_mouse_delta_y * m_camera_sensitivity;

        if (m_mouse_look)
        {
            // Activate Mouse Look
            current->set_rotatation_delta(glm::vec3((float)(m_camera_y),
                                                    (float)(m_camera_x),
                                                    (float)(0.0f)));
        }
        else
        {
            current->set_rotatation_delta(glm::vec3((float)(0),
                                                    (float)(0),
                                                    (float)(0)));
        }

        current->update();
        update_transforms(current);
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

private:
    std::unique_ptr<dw::gl::Shader> m_particle_vs;
    std::unique_ptr<dw::gl::Shader> m_particle_fs;
    std::unique_ptr<dw::gl::Shader> m_particle_initialize_cs;
    std::unique_ptr<dw::gl::Shader> m_particle_update_kickoff_cs;
    std::unique_ptr<dw::gl::Shader> m_particle_emission_cs;
    std::unique_ptr<dw::gl::Shader> m_particle_simulation_cs;
    std::unique_ptr<dw::gl::Shader> m_mesh_vs;
    std::unique_ptr<dw::gl::Shader> m_mesh_fs;
    std::unique_ptr<dw::gl::Shader> m_depth_fs;
    std::unique_ptr<dw::gl::Shader> m_depth_prepass_fs;

    std::unique_ptr<dw::gl::Program> m_particle_program;
    std::unique_ptr<dw::gl::Program> m_particle_initialize_program;
    std::unique_ptr<dw::gl::Program> m_particle_update_kickoff_program;
    std::unique_ptr<dw::gl::Program> m_particle_emission_program;
    std::unique_ptr<dw::gl::Program> m_particle_simulation_program;
    std::unique_ptr<dw::gl::Program> m_mesh_lit_program;
    std::unique_ptr<dw::gl::Program> m_mesh_depth_program;
    std::unique_ptr<dw::gl::Program> m_particle_depth_program;
    std::unique_ptr<dw::gl::Program> m_depth_prepass_program;

    std::unique_ptr<dw::gl::ShaderStorageBuffer> m_draw_indirect_args_ssbo;
    std::unique_ptr<dw::gl::ShaderStorageBuffer> m_dispatch_emission_indirect_args_ssbo;
    std::unique_ptr<dw::gl::ShaderStorageBuffer> m_dispatch_simulation_indirect_args_ssbo;
    std::unique_ptr<dw::gl::ShaderStorageBuffer> m_particle_data_ssbo;
    std::unique_ptr<dw::gl::ShaderStorageBuffer> m_alive_indices_ssbo[2];
    std::unique_ptr<dw::gl::ShaderStorageBuffer> m_dead_indices_ssbo;
    std::unique_ptr<dw::gl::ShaderStorageBuffer> m_counters_ssbo;

    std::unique_ptr<dw::gl::Texture2D>   m_scene_depth_rt;
    std::unique_ptr<dw::gl::Texture2D>   m_scene_normals_rt;
    std::unique_ptr<dw::gl::Framebuffer> m_scene_depth_fbo;

    std::unique_ptr<dw::gl::Texture1D> m_size_over_time;
    std::unique_ptr<dw::gl::Texture1D> m_color_over_time;

    std::unique_ptr<dw::Camera> m_main_camera;

    dw::BrunetonSkyModel m_sky_model;
    dw::ShadowMap        m_shadow_map;
    dw::Mesh::Ptr        m_playground;

    GlobalUniforms m_global_uniforms;

    // Camera controls.
    bool  m_debug_gui          = true;
    bool  m_mouse_look         = false;
    bool  m_show_grid          = true;
    float m_heading_speed      = 0.0f;
    float m_sideways_speed     = 0.0f;
    float m_camera_sensitivity = 0.05f;
    float m_camera_speed       = 0.005f;

    // Camera orientation.
    float m_camera_x;
    float m_camera_y;

    // Particle settings
    int32_t       m_max_active_particles = 0;      // Max Lifetime * Emission Rate
    int32_t       m_emission_rate        = 500; // Particles per second
    float         m_min_lifetime         = 2.0f;   // Seconds
    float         m_max_lifetime         = 2.5f;  // Seconds
    float         m_min_initial_speed    = 1.0f;
    float         m_max_initial_speed    = 4.0f;
    float         m_start_size           = 0.01f; // Seconds
    float         m_end_size             = 0.005f; // Seconds
    bool          m_affected_by_gravity  = true;
    bool          m_depth_buffer_collision = true;
    glm::vec3     m_position             = glm::vec3(0.0f, 3.0f, 0.0f);
    glm::vec3     m_direction            = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3     m_constant_velocity    = glm::vec3(0.0f);
    float         m_rotation             = 0.0f;
    int32_t       m_pre_sim_idx          = 0;
    int32_t       m_post_sim_idx         = 1;
    float         m_accumulator          = 0.0f;
    float         m_emission_delta       = 0.0f;
    float         m_viscosity            = 0.0f;
    float         m_restitution            = 0.5f;
    int32_t       m_particles_per_frame  = 0;
    EmissionShape m_emission_shape       = EMISSION_SHAPE_SPHERE;
    DirectionType m_direction_type       = DIRECTION_TYPE_OUTWARDS;
    float         m_sphere_radius        = 0.1f;
    float         m_shadow_bias          = 0.00001f;

    // Random
    glm::vec3          m_seeds = glm::vec4(0.0f);
    std::random_device m_random;
    std::mt19937       m_generator;

    // UI
    ImGradient      m_color_gradient;
    float           m_size_curve[5] = { 0.000f, 0.000f, 1.000f, 1.000f, 0.0f };
    ImGradientMark* m_dragging_mark = nullptr;
    ImGradientMark* m_selected_mark = nullptr;
};

DW_DECLARE_MAIN(GPUParticleSystem)