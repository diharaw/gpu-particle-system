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

enum EmitterShape
{
    EMITTER_SHAPE_SPHERE,
    EMITTER_SHAPE_CONE
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

        if (!create_buffers())
            return false;

        // Create camera.
        create_camera();
        particle_initialize();

        glEnable(GL_MULTISAMPLE);

        m_debug_draw.set_distance_fade(true);
        m_debug_draw.set_depth_test(true);
        m_debug_draw.set_fade_start(5.0f);
        m_debug_draw.set_fade_end(10.0f);

        return true;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void update(double delta) override
    {
        m_accumulator += float(m_delta_seconds);

        if (m_debug_gui)
            debug_gui();

        // Update camera.
        update_camera();

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        particle_kickoff();
        particle_emission();
        particle_simulation();
        render_particles();

        m_debug_draw.grid(m_main_camera->m_view_projection, 1.0f, 10.0f);

        m_debug_draw.render(nullptr, m_width, m_height, m_main_camera->m_view_projection, m_main_camera->m_position);

        m_pre_sim_idx  = m_pre_sim_idx == 0 ? 1 : 0;
        m_post_sim_idx = m_post_sim_idx == 0 ? 1 : 0;
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
        ImGui::InputFloat("Rotation", &m_rotation);
        ImGui::InputFloat3("Position", &m_position.x);
        ImGui::SliderInt("Emission Rate (Particles/Second)", &m_emission_rate, 1, 100);
        ImGui::SliderFloat3("Velocity", &m_max_velocity.x, 0.1f, 5.0f);

        std::string txt = "Particles Per Frame: " + std::to_string(m_particles_per_frame);
        ImGui::Text(txt.c_str());
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void render_particles()
    {
        glEnable(GL_DEPTH_TEST);

        m_particle_program->use();

        m_particle_program->set_uniform("u_Rotation", glm::radians(m_rotation));
        m_particle_program->set_uniform("u_View", m_main_camera->m_view);
        m_particle_program->set_uniform("u_Proj", m_main_camera->m_projection);

        m_particle_data_ssbo->bind_base(0);
        m_alive_indices_ssbo[m_post_sim_idx]->bind_base(1);

        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_draw_indirect_args_ssbo->handle());

        glDrawArraysIndirect(GL_TRIANGLES, 0);
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

        m_particle_emission_program->set_uniform("u_EmitterPosition", m_position);
        m_particle_emission_program->set_uniform("u_EmitterVelocity", m_max_velocity);
        m_particle_emission_program->set_uniform("u_EmitterLifetime", m_max_lifetime);
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
        m_particle_simulation_program->set_uniform("u_PreSimIdx", m_pre_sim_idx);
        m_particle_simulation_program->set_uniform("u_PostSimIdx", m_post_sim_idx);

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

    void create_camera()
    {
        m_main_camera = std::make_unique<dw::Camera>(60.0f, 0.1f, CAMERA_FAR_PLANE, float(m_width) / float(m_height), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(-1.0f, 0.0, 0.0f));
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

    std::unique_ptr<dw::gl::Program> m_particle_program;
    std::unique_ptr<dw::gl::Program> m_particle_initialize_program;
    std::unique_ptr<dw::gl::Program> m_particle_update_kickoff_program;
    std::unique_ptr<dw::gl::Program> m_particle_emission_program;
    std::unique_ptr<dw::gl::Program> m_particle_simulation_program;

    std::unique_ptr<dw::gl::ShaderStorageBuffer> m_draw_indirect_args_ssbo;
    std::unique_ptr<dw::gl::ShaderStorageBuffer> m_dispatch_emission_indirect_args_ssbo;
    std::unique_ptr<dw::gl::ShaderStorageBuffer> m_dispatch_simulation_indirect_args_ssbo;
    std::unique_ptr<dw::gl::ShaderStorageBuffer> m_particle_data_ssbo;
    std::unique_ptr<dw::gl::ShaderStorageBuffer> m_alive_indices_ssbo[2];
    std::unique_ptr<dw::gl::ShaderStorageBuffer> m_dead_indices_ssbo;
    std::unique_ptr<dw::gl::ShaderStorageBuffer> m_counters_ssbo;

    std::unique_ptr<dw::gl::Texture1D> m_scale_over_time;
    std::unique_ptr<dw::gl::Texture1D> m_color_over_time;

    std::unique_ptr<dw::Camera> m_main_camera;

    GlobalUniforms m_global_uniforms;

    // Camera controls.
    bool  m_visualize_displacement_map = false;
    bool  m_debug_gui                  = true;
    bool  m_mouse_look                 = false;
    float m_heading_speed              = 0.0f;
    float m_sideways_speed             = 0.0f;
    float m_camera_sensitivity         = 0.05f;
    float m_camera_speed               = 0.001f;

    // Camera orientation.
    float m_camera_x;
    float m_camera_y;

    // Particle settings
    int32_t            m_max_active_particles = 0;    // Max Lifetime * Emission Rate
    int32_t            m_emission_rate        = 100;  // Particles per second
    float              m_min_lifetime         = 0.0f; // Seconds
    float              m_max_lifetime         = 3.0f; // Seconds
    glm::vec3          m_min_velocity         = glm::vec3(0.0f);
    glm::vec3          m_max_velocity         = glm::vec3(0.0f, 2.0f, 0.0f);
    bool               m_affected_by_gravity  = false;
    PropertyChangeType m_color_mode           = PROPERTY_CONSTANT;
    PropertyChangeType m_scale_mode           = PROPERTY_CONSTANT;
    glm::vec3          m_position             = glm::vec3(0.0f);
    float              m_rotation             = 0.0f;
    int32_t            m_pre_sim_idx          = 0;
    int32_t            m_post_sim_idx         = 1;
    float              m_accumulator          = 0.0f;
    float              m_emission_delta       = 0.0f;
    int32_t            m_particles_per_frame  = 0;
};

DW_DECLARE_MAIN(GPUParticleSystem)