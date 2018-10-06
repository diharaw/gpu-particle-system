#include <application.h>
#include <mesh.h>
#include <camera.h>
#include <material.h>
#include <utility.h>
#include <memory>

class ProgressivePathTracer : public dw::Application
{
protected:

	// -----------------------------------------------------------------------------------------------------------------------------------

	bool init(int argc, const char* argv[]) override
	{
		if (!load_shaders())
			return false;

		create_camera();
		create_image();
		create_quad();

		int work_grp_cnt[3];

		glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &work_grp_cnt[0]);
		glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1, &work_grp_cnt[1]);
		glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2, &work_grp_cnt[2]);

		DW_LOG_INFO("Max Work Group Count, X: " + std::to_string(work_grp_cnt[0]) + ", Y:" + std::to_string(work_grp_cnt[1]) + ", Z:" + std::to_string(work_grp_cnt[2]));

		int work_grp_size[3];

		glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0, &work_grp_size[0]);
		glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 1, &work_grp_size[1]);
		glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 2, &work_grp_size[2]);

		DW_LOG_INFO("Max Work Group Size, X: " + std::to_string(work_grp_size[0]) + ", Y:" + std::to_string(work_grp_size[1]) + ", Z:" + std::to_string(work_grp_size[2]));

		int work_grp_inv;

		glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &work_grp_inv);

		DW_LOG_INFO("Max Work Group Invocations: " + std::to_string(work_grp_inv));

		return true;
	}

	// -----------------------------------------------------------------------------------------------------------------------------------

	void update(double delta) override
	{
		// Check if camera had moved.
		check_camera_movement();

		// Update camera.
		update_camera();

		// Perform path tracing 
		path_trace();

		// Render image to screen
		render_to_backbuffer();

		// Set back to false before checking movement next frame.
		m_camera_moved = false;
	}

	// -----------------------------------------------------------------------------------------------------------------------------------

	void shutdown() override
	{

	}

	// -----------------------------------------------------------------------------------------------------------------------------------

	dw::AppSettings intial_app_settings() override
	{
		dw::AppSettings settings;

		settings.resizable = false;
		settings.width = 1280;
		settings.height = 720;
		settings.title = "GPU Path Tracer - Dihara Wijetunga (c) 2018";

		return settings;
	}

	// -----------------------------------------------------------------------------------------------------------------------------------

	void window_resized(int width, int height) override
	{
		// Override window resized method to update camera projection.
		m_main_camera->update_projection(60.0f, 0.1f, 1000.0f, float(m_width) / float(m_height));
		m_aspect_ratio = float(m_width) / float(m_height);
		m_resolution = glm::vec2(m_width, m_height);
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
	}

	// -----------------------------------------------------------------------------------------------------------------------------------

	void mouse_pressed(int code) override
	{
		// Enable mouse look.
		if (code == GLFW_MOUSE_BUTTON_LEFT)
			m_mouse_look = true;
	}

	// -----------------------------------------------------------------------------------------------------------------------------------

	void mouse_released(int code) override
	{
		// Disable mouse look.
		if (code == GLFW_MOUSE_BUTTON_LEFT)
			m_mouse_look = false;
	}

	// -----------------------------------------------------------------------------------------------------------------------------------

private:

	// -----------------------------------------------------------------------------------------------------------------------------------

	void check_camera_movement()
	{
		if (m_mouse_look && (m_mouse_delta_x != 0 || m_mouse_delta_y != 0) || m_heading_speed != 0.0f || m_sideways_speed != 0.0f)
			m_camera_moved = true;
		else
			m_camera_moved = false;
	}

	// -----------------------------------------------------------------------------------------------------------------------------------

	void path_trace()
	{
		m_image->bind_image(0, 0, 0, GL_READ_WRITE, GL_RGBA32F);
		
		m_path_tracer_program->use();

		if (m_camera_moved)
			m_num_frames = 0;

		m_path_tracer_program->set_uniform("u_NumFrames", m_num_frames);
		m_path_tracer_program->set_uniform("u_Accum", float(m_num_frames) / float(m_num_frames + 1));
		m_path_tracer_program->set_uniform("u_FOV", glm::radians(m_main_camera->m_fov));
		m_path_tracer_program->set_uniform("u_AspectRatio", m_aspect_ratio);
		m_path_tracer_program->set_uniform("u_Resolution", m_resolution);
		m_path_tracer_program->set_uniform("u_InvViewMat", glm::inverse(m_main_camera->m_view));
		m_path_tracer_program->set_uniform("u_InvProjectionMat", glm::inverse(m_main_camera->m_projection));

		glDispatchCompute(m_width, m_height, 1);

		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		m_num_frames++;
	}

	// -----------------------------------------------------------------------------------------------------------------------------------

	void render_to_backbuffer()
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		m_quad_vao->bind();

		m_present_program->use();

		if (m_present_program->set_uniform("s_Image", 0))
			m_image->bind(0);

		glDrawArrays(GL_TRIANGLES, 0, 6);
	}

	// -----------------------------------------------------------------------------------------------------------------------------------

	bool load_shaders()
	{
		std::string src;

		if (!dw::utility::read_text("shader/present_vs.glsl", src))
		{
			DW_LOG_ERROR("Failed to read present_vs.glsl");
			return false;
		}

		m_present_vs = std::make_unique<dw::Shader>(GL_VERTEX_SHADER, src);

		src.clear();

		if (!dw::utility::read_text("shader/present_fs.glsl", src))
		{
			DW_LOG_ERROR("Failed to read present_fs.glsl");
			return false;
		}

		m_present_fs = std::make_unique<dw::Shader>(GL_FRAGMENT_SHADER, src);

		dw::Shader* present_shaders[] = { m_present_vs.get(), m_present_fs.get() };

		m_present_program = std::make_unique<dw::Program>(2, present_shaders);

		src.clear();

		if (!dw::utility::read_text("shader/path_tracer_cs.glsl", src))
		{
			DW_LOG_ERROR("Failed to read path_tracer_cs.glsl");
			return false;
		}

		m_path_tracer_cs = std::make_unique<dw::Shader>(GL_COMPUTE_SHADER, src);

		dw::Shader* path_tracer_shader = m_path_tracer_cs.get();
		m_path_tracer_program = std::make_unique<dw::Program>(1, &path_tracer_shader);

		return true;
	}

	// -----------------------------------------------------------------------------------------------------------------------------------

	void create_camera()
	{
		m_main_camera = std::make_unique<dw::Camera>(60.0f, 0.1f, 1000.0f, float(m_width) / float(m_height), glm::vec3(0.0f, 0.0f, 30.0f), glm::vec3(0.0f, 0.0, -1.0f));
		m_aspect_ratio = float(m_width) / float(m_height);
		m_resolution = glm::vec2(m_width, m_height);
	}

	// -----------------------------------------------------------------------------------------------------------------------------------

	void create_image()
	{
		m_image = std::make_unique<dw::Texture2D>(m_width, m_height, 1, 1, 1, GL_RGBA32F, GL_RGBA, GL_FLOAT);
		m_image->set_min_filter(GL_LINEAR);
		m_image->set_wrapping(GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
	}

	// -----------------------------------------------------------------------------------------------------------------------------------

	void create_quad()
	{
		const float vertices[] =
		{
			-1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
			-1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
			1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
			1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
			1.0f,  -1.0f, 0.0f, 1.0f, 0.0f,
			-1.0f, -1.0f, 0.0f, 0.0f, 0.0f
		};

		m_quad_vbo = std::make_unique<dw::VertexBuffer>(GL_STATIC_DRAW, sizeof(vertices), (void*)vertices);

		dw::VertexAttrib quad_attribs[] =
		{
			{ 3, GL_FLOAT, false, 0, },
			{ 2, GL_FLOAT, false, sizeof(float) * 3 }
		};

		m_quad_vao = std::make_unique<dw::VertexArray>(m_quad_vbo.get(), nullptr, sizeof(float) * 5, 2, quad_attribs);
	}

	// -----------------------------------------------------------------------------------------------------------------------------------

	void update_camera()
	{
		dw::Camera* current = m_main_camera.get();

		float forward_delta = m_heading_speed * m_delta;
		float right_delta = m_sideways_speed * m_delta;

		current->set_translation_delta(current->m_forward, forward_delta);
		current->set_translation_delta(current->m_right, right_delta);

		if (m_mouse_look)
		{
			// Activate Mouse Look
			current->set_rotatation_delta(glm::vec3((float)(m_mouse_delta_y * m_camera_sensitivity * m_delta),
				(float)(m_mouse_delta_x * m_camera_sensitivity * m_delta),
				(float)(0.0f)));
		}
		else
		{
			current->set_rotatation_delta(glm::vec3((float)(0),
				(float)(0),
				(float)(0)));
		}

		current->update();
	}

	// -----------------------------------------------------------------------------------------------------------------------------------

private:
	// GPU Resources.
	std::unique_ptr<dw::Shader>  m_present_vs;
	std::unique_ptr<dw::Shader>  m_present_fs;
	std::unique_ptr<dw::Program> m_present_program;
	std::unique_ptr<dw::Shader>  m_path_tracer_cs;
	std::unique_ptr<dw::Program> m_path_tracer_program;
	std::unique_ptr<dw::Texture> m_image;
	std::unique_ptr<dw::VertexBuffer> m_quad_vbo;
	std::unique_ptr<dw::VertexArray>  m_quad_vao;

	// Uniforms
	float m_aspect_ratio;
	glm::vec2 m_resolution;
	int32_t m_num_frames = 0;

	// Camera controls.
	bool m_mouse_look = false;
	bool m_camera_moved = false;
	float m_heading_speed = 0.0f;
	float m_sideways_speed = 0.0f;
	float m_camera_sensitivity = 0.005f;
	float m_camera_speed = 0.1f;

	// Camera.
	std::unique_ptr<dw::Camera> m_main_camera;
};

DW_DECLARE_MAIN(ProgressivePathTracer)