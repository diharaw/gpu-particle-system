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
#define CAMERA_FAR_PLANE 1000.0f
#define MAX_PARTICLES 1000000
#define GRADIENT_SAMPLES 32

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
        if (m_debug_gui)
            debug_gui();

        // Update camera.
        update_camera();

        update_uniforms();

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        render_particle();

        m_debug_draw.grid(m_main_camera->m_view_projection, 1.0f, 10.0f);

        m_debug_draw.render(nullptr, m_width, m_height, m_main_camera->m_view_projection, m_main_camera->m_position);
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
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void render_particle()
    {
        m_particle_program->use();

        m_particle_program->set_uniform("u_Rotation", glm::radians(m_rotation));
        m_particle_program->set_uniform("u_Position", m_position);
        m_particle_program->set_uniform("u_View", m_main_camera->m_view);
        m_particle_program->set_uniform("u_Proj", m_main_camera->m_projection);

        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    bool create_shaders()
    {
        {
            // Create general shaders
            m_particle_vs = std::unique_ptr<dw::gl::Shader>(dw::gl::Shader::create_from_file(GL_VERTEX_SHADER, "shader/particle_vs.glsl"));
            m_particle_fs = std::unique_ptr<dw::gl::Shader>(dw::gl::Shader::create_from_file(GL_FRAGMENT_SHADER, "shader/particle_fs.glsl"));

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
        }

        return true;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    bool create_buffers()
    {
        // Create uniform buffer for global data
        m_global_ubo = std::make_unique<dw::gl::UniformBuffer>(GL_DYNAMIC_DRAW, sizeof(GlobalUniforms));

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

    void update_uniforms()
    {
        void* ptr = m_global_ubo->map(GL_WRITE_ONLY);
        memcpy(ptr, &m_global_uniforms, sizeof(GlobalUniforms));
        m_global_ubo->unmap();
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
    std::unique_ptr<dw::gl::Shader>  m_particle_vs;
    std::unique_ptr<dw::gl::Shader>  m_particle_fs;
    std::unique_ptr<dw::gl::Program> m_particle_program;

    std::unique_ptr<dw::gl::ShaderStorageBuffer> m_draw_indirect_args_ssbo;
    std::unique_ptr<dw::gl::ShaderStorageBuffer> m_dispatch_indirect_args_ssbo;
    std::unique_ptr<dw::gl::ShaderStorageBuffer> m_particle_data_ssbo;
    std::unique_ptr<dw::gl::ShaderStorageBuffer> m_alive_indices_pre_sim_ssbo;
    std::unique_ptr<dw::gl::ShaderStorageBuffer> m_alive_indices_post_sim_ssbo;
    std::unique_ptr<dw::gl::ShaderStorageBuffer> m_dead_indices_ssbo;

    std::unique_ptr<dw::gl::Texture1D> m_scale_over_time;
    std::unique_ptr<dw::gl::Texture1D> m_color_over_time;

    std::unique_ptr<dw::gl::UniformBuffer> m_global_ubo;
    std::unique_ptr<dw::Camera>            m_main_camera;

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
    uint32_t           m_max_active_particles = 0;    // Max Lifetime * Emission Rate
    uint32_t           m_emission_rate        = 0;    // Particles per second
    float              m_min_lifetime         = 0.0f; // Seconds
    float              m_max_lifetime         = 0.0f; // Seconds
    float              m_min_velocity         = 0.0f;
    float              m_max_velocity         = 0.0f;
    bool               m_affected_by_gravity  = false;
    PropertyChangeType m_color_mode           = PROPERTY_CONSTANT;
    PropertyChangeType m_scale_mode           = PROPERTY_CONSTANT;
    glm::vec3          m_position             = glm::vec3(0.0f);
    float              m_rotation             = 0.0f;
};

DW_DECLARE_MAIN(GPUParticleSystem)