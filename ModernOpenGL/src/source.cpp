#define STB_IMAGE_IMPLEMENTATION

#include <string_view>
#include <iostream>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <tuple>
#include <array>
#include <random>
#include <vector>
#include <experimental/filesystem>

#include <SDL.h>
#include <glad\glad.h>
#include <stb_image.h>
#include <glm\glm.hpp>
#include <glm\gtc\type_ptr.hpp>
#include <glm\gtx\transform.hpp>
#include <glm\gtc\matrix_transform.hpp>

namespace fs = std::experimental::filesystem;

extern "C" { _declspec(dllexport) unsigned int NvOptimusEnablement = 0x00000001; }

inline std::string read_text_file(std::string_view filepath)
{
	if (!fs::exists(filepath.data()))
	{
		std::ostringstream message;
		message << "file " << filepath.data() << " does not exist.";
		throw fs::filesystem_error(message.str());
	}
	std::ifstream file(filepath.data());
	return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

struct vertex_t
{
	glm::vec3 position, color, normal;
	glm::vec2 texcoord;
	vertex_t(glm::vec3 const& position, glm::vec3 const& color, glm::vec3 const& normal, glm::vec2 const& texcoord)
		: position(position), color(color), normal(normal), texcoord(texcoord) {}
};

struct attrib_format_t
{
	GLuint attrib_index;
	GLint size;
	GLenum type;
	GLuint relative_offset;
};

template<typename T>
constexpr std::pair<GLint, GLenum> type_to_size_enum()
{
	if constexpr (std::is_same_v<T, float>)
		return std::make_pair(1, GL_FLOAT);
	if constexpr (std::is_same_v<T, int>)
		return std::make_pair(1, GL_INT);
	if constexpr (std::is_same_v<T, unsigned int>)
		return std::make_pair(1, GL_UNSIGNED_INT);
	if constexpr (std::is_same_v<T, glm::vec2>)
		return std::make_pair(2, GL_FLOAT);
	if constexpr (std::is_same_v<T, glm::vec3>)
		return std::make_pair(3, GL_FLOAT);
	if constexpr (std::is_same_v<T, glm::vec4>)
		return std::make_pair(4, GL_FLOAT);
	throw std::runtime_error("unsupported type");
}

template<typename T>
inline attrib_format_t create_attrib_format(GLuint attrib_index, GLuint relative_offset)
{
	auto const[comp_count, type] = type_to_size_enum<T>();
	return attrib_format_t{ attrib_index, comp_count, type, relative_offset };
}

template<typename T>
inline GLuint create_buffer(std::vector<T> const& buff, GLenum flags = GL_DYNAMIC_STORAGE_BIT)
{
	GLuint name = 0;
	glCreateBuffers(1, &name);
	glNamedBufferStorage(name, sizeof(std::vector<T>::value_type) * buff.size(), buff.data(), flags);
	return name;
}

template<typename T>
std::tuple<GLuint, GLuint, GLuint> create_geometry(std::vector<T> const& vertices, std::vector<uint8_t> const& indices, std::vector<attrib_format_t> const& attrib_formats)
{
	GLuint vao = 0;
	auto vbo = create_buffer(vertices);
	auto ibo = create_buffer(indices);

	glCreateVertexArrays(1, &vao);
	glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(T));
	glVertexArrayElementBuffer(vao, ibo);

	for (auto const& format : attrib_formats)
	{
		glEnableVertexArrayAttrib(vao, format.attrib_index);
		glVertexArrayAttribFormat(vao, format.attrib_index, format.size, format.type, GL_FALSE, format.relative_offset);
		glVertexArrayAttribBinding(vao, format.attrib_index, 0);
	}

	return std::make_tuple(vao, vbo, ibo);
}

void validate_program(GLuint shader, std::string_view filename)
{
	GLint compiled = 0;
	glProgramParameteri(shader, GL_PROGRAM_SEPARABLE, GL_TRUE);
	glGetProgramiv(shader, GL_LINK_STATUS, &compiled);
	if (compiled == GL_FALSE)
	{
		std::array<char, 1024> compiler_log;
		glGetProgramInfoLog(shader, compiler_log.size(), nullptr, compiler_log.data());
		glDeleteShader(shader);

		std::ostringstream message;
		message << "shader " << filename << " contains error(s):\n\n" << compiler_log.data() << '\n';
		std::clog << message.str();
	}
}

std::tuple<GLuint, GLuint, GLuint> create_program(std::string_view vert_filepath, std::string_view frag_filepath)
{
	auto const vert_source = read_text_file(vert_filepath);
	auto const frag_source = read_text_file(frag_filepath);

	auto const v_ptr = vert_source.data();
	auto const f_ptr = frag_source.data();
	GLuint pipeline = 0;
	auto vert = glCreateShaderProgramv(GL_VERTEX_SHADER, 1, &v_ptr);
	auto frag = glCreateShaderProgramv(GL_FRAGMENT_SHADER, 1, &f_ptr);

	validate_program(vert, vert_filepath);
	validate_program(frag, frag_filepath);

	glCreateProgramPipelines(1, &pipeline);
	glUseProgramStages(pipeline, GL_VERTEX_SHADER_BIT, vert);
	glUseProgramStages(pipeline, GL_FRAGMENT_SHADER_BIT, frag);

	return std::make_tuple(pipeline, vert, frag);
}

GLuint create_shader(GLuint vert, GLuint frag)
{
	GLuint pipeline = 0;
	glCreateProgramPipelines(1, &pipeline);
	glUseProgramStages(pipeline, GL_VERTEX_SHADER_BIT, vert);
	glUseProgramStages(pipeline, GL_FRAGMENT_SHADER_BIT, frag);
	return pipeline;
}

GLuint create_texture_2d(GLenum internal_format, GLenum format, GLsizei width, GLsizei height, void* data = nullptr, GLenum filter = GL_LINEAR, GLenum repeat = GL_REPEAT)
{
	GLuint tex = 0;
	glCreateTextures(GL_TEXTURE_2D, 1, &tex);
	glTextureStorage2D(tex, 1, internal_format, width, height);

	glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, filter);
	glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, filter);
	glTextureParameteri(tex, GL_TEXTURE_WRAP_S, repeat);
	glTextureParameteri(tex, GL_TEXTURE_WRAP_T, repeat);
	glTextureParameteri(tex, GL_TEXTURE_WRAP_R, repeat);

	if (data)
	{
		glTextureSubImage2D(tex, 0, 0, 0, width, height, format, GL_UNSIGNED_BYTE, data);
	}

	return tex;
}

template<typename T = nullptr_t>
GLuint create_texture_cube(GLenum internal_format, GLenum format, GLsizei width, GLsizei height, std::array<T*, 6> const& data)
{
	GLuint tex = 0;
	glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &tex);
	glTextureStorage2D(tex, 1, internal_format, width, height);

	for (GLint i = 0; i < 6; ++i)
	{
		if (data[i])
		{
			glTextureSubImage3D(tex, 0, 0, 0, i, width, height, 1, format, GL_UNSIGNED_BYTE, data[i]);
		}
	}

	return tex;
}

using stb_comp_t = decltype(STBI_default);
GLuint create_texture_2d_from_file(std::string_view filepath, stb_comp_t comp = STBI_rgb_alpha)
{
	int x, y, c;
	if (!fs::exists(filepath.data()))
	{
		std::ostringstream message;
		message << "file " << filepath.data() << " does not exist.";
		throw std::runtime_error(message.str());
	}
	const auto data = stbi_load(filepath.data(), &x, &y, &c, comp);

	auto const[in, ex] = [comp]() {
		switch (comp)
		{
		case STBI_rgb_alpha:	return std::make_pair(GL_RGBA8, GL_RGBA);
		case STBI_rgb:			return std::make_pair(GL_RGB8, GL_RGB);
		case STBI_grey:			return std::make_pair(GL_R8, GL_RED);
		case STBI_grey_alpha:	return std::make_pair(GL_RG8, GL_RG);
		default: throw std::runtime_error("invalid format");
		}
	}();

	const auto name = create_texture_2d(in, ex, x, y, data);
	stbi_image_free(data);
	return name;
}

GLuint create_texture_cube_from_file(std::array<std::string_view, 6> const& filepath, stb_comp_t comp = STBI_rgb_alpha)
{
	int x, y, c;
	std::array<stbi_uc*, 6> faces;

	auto const[in, ex] = [comp]() {
		switch (comp)
		{
		case STBI_rgb_alpha:	return std::make_pair(GL_RGBA8, GL_RGBA);
		case STBI_rgb:			return std::make_pair(GL_RGB8, GL_RGB);
		case STBI_grey:			return std::make_pair(GL_R8, GL_RED);
		case STBI_grey_alpha:	return std::make_pair(GL_RG8, GL_RG);
		default: throw std::runtime_error("invalid format");
		}
	}();

	for (auto i = 0; i < 6; i++)
	{
		faces[i] = stbi_load(filepath[i].data(), &x, &y, &c, comp);
	}

	const auto name = create_texture_cube(in, ex, x, y, faces);

	for (auto face : faces)
	{
		stbi_image_free(face);
	}
	return name;
}

GLuint create_framebuffer(std::vector<GLuint> const& cols, GLuint depth = GL_NONE)
{
	GLuint fbo = 0;
	glCreateFramebuffers(1, &fbo);

	for (auto i = 0; i < cols.size(); i++)
	{
		glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0 + i, cols[i], 0);
	}

	std::array<GLenum, 32> draw_buffs;
	for (GLenum i = 0; i < cols.size(); i++)
	{
		draw_buffs[i] = GL_COLOR_ATTACHMENT0 + i;
	}

	glNamedFramebufferDrawBuffers(fbo, cols.size(), draw_buffs.data());

	if (depth != GL_NONE)
	{
		glNamedFramebufferTexture(fbo, GL_DEPTH_ATTACHMENT, depth, 0);
	}

	if (glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		throw std::runtime_error("incomplete framebuffer");
	}
	return fbo;
}

template <typename T>
inline void set_uniform(GLuint shader, GLint location, T const& value)
{
	if		constexpr(std::is_same_v<T, GLint>)		glProgramUniform1i(shader, location, value);
	else if constexpr(std::is_same_v<T, GLuint>)	glProgramUniform1ui(shader, location, value);
	else if constexpr(std::is_same_v<T, bool>)		glProgramUniform1ui(shader, location, value);
	else if constexpr(std::is_same_v<T, GLfloat>)	glProgramUniform1f(shader, location, value);
	else if constexpr(std::is_same_v<T, GLdouble>)	glProgramUniform1d(shader, location, value);
	else if constexpr(std::is_same_v<T, glm::vec2>) glProgramUniform2fv(shader, location, 1, glm::value_ptr(value));
	else if constexpr(std::is_same_v<T, glm::vec3>) glProgramUniform3fv(shader, location, 1, glm::value_ptr(value));
	else if constexpr(std::is_same_v<T, glm::vec4>) glProgramUniform4fv(shader, location, 1, glm::value_ptr(value));
	else if constexpr(std::is_same_v<T, glm::ivec2>)glProgramUniform2iv(shader, location, 1, glm::value_ptr(value));
	else if constexpr(std::is_same_v<T, glm::ivec3>)glProgramUniform3iv(shader, location, 1, glm::value_ptr(value));
	else if constexpr(std::is_same_v<T, glm::ivec4>)glProgramUniform4iv(shader, location, 1, glm::value_ptr(value));
	else if constexpr(std::is_same_v<T, glm::uvec2>)glProgramUniform2uiv(shader, location, 1, glm::value_ptr(value));
	else if constexpr(std::is_same_v<T, glm::uvec3>)glProgramUniform3uiv(shader, location, 1, glm::value_ptr(value));
	else if constexpr(std::is_same_v<T, glm::uvec4>)glProgramUniform4uiv(shader, location, 1, glm::value_ptr(value));
	else if constexpr(std::is_same_v<T, glm::quat>) glProgramUniform4fv(shader, location, 1, glm::value_ptr(value));
	else if constexpr(std::is_same_v<T, glm::mat3>) glProgramUniformMatrix3fv(shader, location, 1, GL_FALSE, glm::value_ptr(value));
	else if constexpr(std::is_same_v<T, glm::mat4>) glProgramUniformMatrix4fv(shader, location, 1, GL_FALSE, glm::value_ptr(value));
	else throw std::runtime_error("unsupported type");
}

inline void delete_shader(GLuint pr, GLuint vs, GLuint fs)
{
	glDeleteProgramPipelines(1, &pr);
	glDeleteProgram(vs);
	glDeleteProgram(fs);
}

using glDeleterFunc = void (APIENTRYP)(GLuint item);
using glDeleterFuncv = void (APIENTRYP)(GLsizei n, const GLuint *items);
inline void delete_items(glDeleterFuncv deleter, std::initializer_list<GLuint> items) { deleter(items.size(), items.begin()); }
inline void delete_items(glDeleterFunc deleter, std::initializer_list<GLuint> items)
{
	for (size_t i = 0; i < items.size(); i++)
	{
		deleter(*(items.begin() + i));
	}
}

inline glm::vec3 orbit_axis(float angle, glm::vec3 const& axis, glm::vec3 const& spread) { return glm::angleAxis(angle, axis) * spread; }
inline float lerp(float a, float b, float f) { return a + f * (b - a); }

/*
std::vector<glm::vec3> calc_tangents(std::vector<vertex_t> const& vertices, std::vector<uint8_t> const& indices)
{
	std::vector<glm::vec3> res;
	res.reserve(indices.size());
	for (auto q = 0; q < 6; ++q)
	{
		auto
			v = q * 4,
			i = q * 6;
		glm::vec3
			edge0 = vertices[indices[i + 1]].position - vertices[indices[i + 0]].position, edge1 = vertices[indices[i + 2]].position - vertices[indices[0]].position,
			edge2 = vertices[indices[i + 4]].position - vertices[indices[i + 3]].position, edge3 = vertices[indices[i + 5]].position - vertices[indices[3]].position;

		glm::vec2
			delta_uv0 = vertices[indices[i + 1]].texcoord - vertices[indices[i + 0]].texcoord, delta_uv1 = vertices[indices[i + 2]].texcoord - vertices[indices[i + 0]].texcoord,
			delta_uv2 = vertices[indices[i + 4]].texcoord - vertices[indices[i + 3]].texcoord, delta_uv3 = vertices[indices[i + 5]].texcoord - vertices[indices[i + 3]].texcoord;

		float const
			f0 = 1.0f / (delta_uv0.x * delta_uv1.y - delta_uv1.x * delta_uv0.y),
			f1 = 1.0f / (delta_uv2.x * delta_uv3.y - delta_uv3.x * delta_uv2.y);

		auto const
			t0 = glm::normalize(glm::vec3(
				f0 * (delta_uv1.y * edge0.x - delta_uv0.y * edge1.x),
				f0 * (delta_uv1.y * edge0.y - delta_uv0.y * edge1.y),
				f0 * (delta_uv1.y * edge0.z - delta_uv0.y * edge1.z)
			)),
			t1 = glm::normalize(glm::vec3(
				f1 * (delta_uv3.y * edge2.x - delta_uv2.y * edge3.x),
				f1 * (delta_uv3.y * edge2.y - delta_uv2.y * edge3.y),
				f1 * (delta_uv3.y * edge2.z - delta_uv2.y * edge3.z)
			));

		res.push_back(t0); res.push_back(t0); res.push_back(t0);
		res.push_back(t1); res.push_back(t1); res.push_back(t1);
	}
	return res;
}
*/

/*
std::vector<glm::vec3> generate_ssao_kernel()
{
	std::vector<glm::vec3>  res;
	res.reserve(64);

	std::uniform_real_distribution<float> random(0.0f, 1.0f);
	std::default_random_engine generator;

	for (int i = 0; i < 64; ++i)
	{
		auto sample = glm::normalize(glm::vec3(
			random(generator) * 2.0f - 1.0f,
			random(generator) * 2.0f - 1.0f,
			random(generator)
		))* random(generator)
		  * lerp(0.1f, 1.0f, glm::pow(float(i) / 64.0f, 2));
		res.push_back(sample);
	}
	return res;
}

std::vector<glm::vec3> generate_ssao_noise()
{
	std::vector<glm::vec3>  res;
	res.reserve(16);

	std::uniform_real_distribution<float> random(0.0f, 1.0f);
	std::default_random_engine generator;

	for (int i = 0; i < 16; ++i)
		res.push_back(glm::vec3(
			random(generator) * 2.0f - 1.0f,
			random(generator) * 2.0f - 1.0f,
			0.0f
		));

	return res;
}
*/

#if _DEBUG
void APIENTRY gl_debug_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
{
	std::ostringstream str;
	str << "---------------------opengl-callback-start------------\n";
	str << "message: " << message << '\n';
	str << "type: ";
	switch (type)
	{
	case GL_DEBUG_TYPE_ERROR: str << "ERROR"; break;
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: str << "DEPRECATED_BEHAVIOR"; break;
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: str << "UNDEFINED_BEHAVIOR";	break;
	case GL_DEBUG_TYPE_PORTABILITY: str << "PORTABILITY"; break;
	case GL_DEBUG_TYPE_PERFORMANCE: str << "PERFORMANCE"; break;
	case GL_DEBUG_TYPE_OTHER: str << "OTHER"; break;
	}
	str << '\n';
	str << "id: " << id << '\n';
	str << "severity: ";
	switch (severity)
	{
	case GL_DEBUG_SEVERITY_LOW: str << "LOW"; break;
	case GL_DEBUG_SEVERITY_MEDIUM: str << "MEDIUM";	break;
	case GL_DEBUG_SEVERITY_HIGH: str << "HIGH";	break;
	}
	str << "\n---------------------opengl-callback-end--------------\n";

	std::clog << str.str();
}
#endif

template<typename ... Args>
std::string string_format(const std::string& format, Args ... args)
{
	const size_t size = snprintf(nullptr, 0, format.c_str(), args ...) + 1; // Extra space for '\0'
	std::unique_ptr<char[]> buf(new char[size]);
	snprintf(buf.get(), size, format.c_str(), args ...);
	return std::string(buf.get(), buf.get() + size - 1); // We don't want the '\0' inside
}

void measure_frames(SDL_Window* const window, double& deltaTimeAverage, int& frameCounter, int framesToAverage)
{
	if (frameCounter == framesToAverage)
	{
		deltaTimeAverage /= framesToAverage;

		auto window_title = string_format("frametime = %.3fms, fps = %.1f", 1000.0*deltaTimeAverage, 1.0 / deltaTimeAverage);
		SDL_SetWindowTitle(window, window_title.c_str());

		deltaTimeAverage = 0.0;
		frameCounter = 0;
	}
}

int main(int argc, char* argv[])
{
	constexpr auto window_width = 1920;
	constexpr auto window_height = 1080;
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
	const auto window = SDL_CreateWindow("ModernOpenGL\0", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, window_width, window_height, SDL_WINDOW_OPENGL);
	const auto gl_context = SDL_GL_CreateContext(window);
	//SDL_GL_SetSwapInterval(0);
	auto ev = SDL_Event();

	auto key_count = 0;
	const auto key_state = SDL_GetKeyboardState(&key_count);

	std::array<bool, 512> key;
	std::array<bool, 512> key_pressed;
	std::array<bool, 512> key_released;

	auto const[screen_width, screen_height] = []()
	{
		SDL_DisplayMode display_mode;
		SDL_GetCurrentDisplayMode(0, &display_mode);
		return std::pair<int, int>(display_mode.w, display_mode.h);
		//return std::pair<int, int>(320, 200);
	}();

	if (!gladLoadGL())
	{
		SDL_GL_DeleteContext(gl_context);
		SDL_DestroyWindow(window);
		throw std::runtime_error("failed to load gl");
	}

	std::clog << glGetString(GL_VERSION) << '\n';

#if _DEBUG
	if (glDebugMessageCallback)
	{
		std::clog << "registered opengl debug callback\n";
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		glDebugMessageCallback(gl_debug_callback, nullptr);
		GLuint unusedIds = 0;
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, &unusedIds, true);
	}
	else
	{
		std::clog << "glDebugMessageCallback not available\n";
	}
#endif

	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_PROGRAM_POINT_SIZE);

	std::vector<vertex_t> const vertices_cube =
	{
		vertex_t(glm::vec3(-0.5f, 0.5f,-0.5f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f,-1.0f), glm::vec2(0.0f, 0.0f)),
		vertex_t(glm::vec3(0.5f, 0.5f,-0.5f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f,-1.0f), glm::vec2(1.0f, 0.0f)),
		vertex_t(glm::vec3(0.5f,-0.5f,-0.5f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.0f,-1.0f), glm::vec2(1.0f, 1.0f)),
		vertex_t(glm::vec3(-0.5f,-0.5f,-0.5f), glm::vec3(1.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.0f,-1.0f), glm::vec2(0.0f, 1.0f)),

		vertex_t(glm::vec3(0.5f, 0.5f,-0.5f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec2(0.0f, 0.0f)),
		vertex_t(glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec2(1.0f, 0.0f)),
		vertex_t(glm::vec3(0.5f,-0.5f, 0.5f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec2(1.0f, 1.0f)),
		vertex_t(glm::vec3(0.5f,-0.5f,-0.5f), glm::vec3(1.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec2(0.0f, 1.0f)),

		vertex_t(glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec2(1.0f, 0.0f)),
		vertex_t(glm::vec3(-0.5f, 0.5f, 0.5f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec2(0.0f, 0.0f)),
		vertex_t(glm::vec3(-0.5f,-0.5f, 0.5f), glm::vec3(1.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec2(0.0f, 1.0f)),
		vertex_t(glm::vec3(0.5f,-0.5f, 0.5f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec2(1.0f, 1.0f)),

		vertex_t(glm::vec3(-0.5f, 0.5f, 0.5f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec2(1.0f, 0.0f)),
		vertex_t(glm::vec3(-0.5f, 0.5f,-0.5f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec2(0.0f, 0.0f)),
		vertex_t(glm::vec3(-0.5f,-0.5f,-0.5f), glm::vec3(1.0f, 0.0f, 1.0f), glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec2(0.0f, 1.0f)),
		vertex_t(glm::vec3(-0.5f,-0.5f, 0.5f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec2(1.0f, 1.0f)),

		vertex_t(glm::vec3(-0.5f, 0.5f, 0.5f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(0.0f, 0.0f)),
		vertex_t(glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(1.0f, 0.0f)),
		vertex_t(glm::vec3(0.5f, 0.5f,-0.5f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(1.0f, 1.0f)),
		vertex_t(glm::vec3(-0.5f, 0.5f,-0.5f), glm::vec3(1.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(0.0f, 1.0f)),

		vertex_t(glm::vec3(0.5f,-0.5f, 0.5f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f,-1.0f, 0.0f), glm::vec2(1.0f, 0.0f)),
		vertex_t(glm::vec3(-0.5f,-0.5f, 0.5f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f,-1.0f, 0.0f), glm::vec2(0.0f, 0.0f)),
		vertex_t(glm::vec3(-0.5f,-0.5f,-0.5f), glm::vec3(1.0f, 0.0f, 1.0f), glm::vec3(0.0f,-1.0f, 0.0f), glm::vec2(0.0f, 1.0f)),
		vertex_t(glm::vec3(0.5f,-0.5f,-0.5f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f,-1.0f, 0.0f), glm::vec2(1.0f, 1.0f)),
	};

	std::vector<vertex_t> const	vertices_quad =
	{
		vertex_t(glm::vec3(-0.5f, 0.0f, 0.5f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(0.0f, 0.0f)),
		vertex_t(glm::vec3(0.5f, 0.0f, 0.5f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(1.0f, 0.0f)),
		vertex_t(glm::vec3(0.5f, 0.0f,-0.5f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(1.0f, 1.0f)),
		vertex_t(glm::vec3(-0.5f, 0.0f,-0.5f), glm::vec3(1.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(0.0f, 1.0f)),
	};

	std::vector<uint8_t> const indices_cube =
	{
		0,   1,  2,  2,  3,  0,
		4,   5,  6,  6,  7,  4,
		8,   9, 10, 10, 11,  8,

		12, 13, 14, 14, 15, 12,
		16, 17, 18, 18, 19, 16,
		20, 21, 22, 22, 23, 20,
	};

	std::vector<uint8_t> const indices_quad =
	{
		0,   1,  2,  2,  3,  0,
	};

	auto const texture_cube_diffuse = create_texture_2d_from_file(".\\textures\\T_Default_D.png", STBI_rgb);
	auto const texture_cube_specular = create_texture_2d_from_file(".\\textures\\T_Default_S.png", STBI_grey);
	auto const texture_cube_normal = create_texture_2d_from_file(".\\textures\\T_Default_N.png", STBI_rgb);
	auto const texture_skybox = create_texture_cube_from_file({
			".\\textures\\TC_SkySpace_Xn.png",
			".\\textures\\TC_SkySpace_Xp.png",
			".\\textures\\TC_SkySpace_Yn.png",
			".\\textures\\TC_SkySpace_Yp.png",
			".\\textures\\TC_SkySpace_Zn.png",
			".\\textures\\TC_SkySpace_Zp.png"
		});

	/* framebuffer textures */
	auto const texture_gbuffer_color = create_texture_2d(GL_RGB8, GL_RGB, screen_width, screen_height, nullptr, GL_NEAREST);
	auto const texture_gbuffer_position = create_texture_2d(GL_RGB16F, GL_RGB, screen_width, screen_height, nullptr, GL_NEAREST);
	auto const texture_gbuffer_normal = create_texture_2d(GL_RGB16F, GL_RGB, screen_width, screen_height, nullptr, GL_NEAREST);
	auto const texture_gbuffer_albedo = create_texture_2d(GL_RGBA16F, GL_RGBA, screen_width, screen_height, nullptr, GL_NEAREST);
	auto const texture_gbuffer_depth = create_texture_2d(GL_DEPTH_COMPONENT32, GL_DEPTH, screen_width, screen_height, nullptr, GL_NEAREST);

	auto const fb_gbuffer = create_framebuffer({ texture_gbuffer_position, texture_gbuffer_normal, texture_gbuffer_albedo }, texture_gbuffer_depth);
	auto const fb_finalcolor = create_framebuffer({ texture_gbuffer_color });

	/* vertex formatting information */
	auto const vertex_format =
	{
		create_attrib_format<glm::vec3>(0, offsetof(vertex_t, position)),
		create_attrib_format<glm::vec3>(1, offsetof(vertex_t, color)),
		create_attrib_format<glm::vec3>(2, offsetof(vertex_t, normal)),
		create_attrib_format<glm::vec2>(3, offsetof(vertex_t, texcoord))
	};

	/* geometry buffers */
	auto const vao_empty = [] { GLuint name = 0; glCreateVertexArrays(1, &name); return name; }();
	auto const[vao_cube, vbo_cube, ibo_cube] = create_geometry(vertices_cube, indices_cube, vertex_format);
	auto const[vao_quad, vbo_quad, ibo_quad] = create_geometry(vertices_quad, indices_quad, vertex_format);

	/* shaders */
	auto const[pr, vert_shader, frag_shader] = create_program(".\\shaders\\main.vert", ".\\shaders\\main.frag");
	auto const[pr_g, vert_shader_g, frag_shader_g] = create_program(".\\shaders\\gbuffer.vert", ".\\shaders\\gbuffer.frag");

	/* uniforms */
	constexpr auto uniform_projection = 0;
	constexpr auto uniform_cam_pos = 0;
	constexpr auto uniform_cam_dir = 0;
	constexpr auto uniform_view = 1;
	constexpr auto uniform_fov = 1;
	constexpr auto uniform_aspect = 2;
	constexpr auto uniform_modl = 2;
	constexpr auto uniform_lght = 3;
	constexpr auto uniform_uvs_diff = 3;

	constexpr auto fov = glm::radians(60.0f);
	auto const camera_projection = glm::perspective(fov, float(window_width) / float(window_height), 0.1f, 1000.0f);
	set_uniform(vert_shader_g, uniform_projection, camera_projection);

	auto t1 = SDL_GetTicks() / 1000.0;

	const auto framesToAverage = 10;
	auto deltaTimeAverage = 0.0;  // first moment
	auto frameCounter = 0;

	while (ev.type != SDL_QUIT)
	{
		const auto t2 = SDL_GetTicks() / 1000.0;
		const auto dt = t2 - t1;
		t1 = t2;

		deltaTimeAverage += dt;
		frameCounter++;

		measure_frames(window, deltaTimeAverage, frameCounter, framesToAverage);

		if (SDL_PollEvent(&ev))
		{
			for (int i = 0; i < key_count; i++)
			{
				key_pressed[i] = !key[i] && key_state[i];
				key_released[i] = key[i] && !key_state[i];
				key[i] = bool(key_state[i]);
			}
		}
		static auto rot_x = 0.0f;
		static auto rot_y = 0.0f;
		static glm::vec3 camera_position = glm::vec3(0.0f, 0.0f, -7.0f);
		static glm::quat camera_orientation = glm::vec3(0.0f, 0.0f, 0.0f);
		auto const camera_forward = camera_orientation * glm::vec3(0.0f, 0.0f, 1.0f);
		auto const camera_up = camera_orientation * glm::vec3(0.0f, 1.0f, 0.0f);
		auto const camera_right = camera_orientation * glm::vec3(1.0f, 0.0f, 0.0f);

		if (key[SDL_SCANCODE_LEFT])		rot_y += 0.025f;
		if (key[SDL_SCANCODE_RIGHT])	rot_y -= 0.025f;
		if (key[SDL_SCANCODE_UP])		rot_x -= 0.025f;
		if (key[SDL_SCANCODE_DOWN])		rot_x += 0.025f;

		camera_orientation = glm::quat(glm::vec3(rot_x, rot_y, 0.0f));

		if (key[SDL_SCANCODE_W]) camera_position += camera_forward * 0.1f;
		if (key[SDL_SCANCODE_A]) camera_position += camera_right * 0.1f;
		if (key[SDL_SCANCODE_S]) camera_position -= camera_forward * 0.1f;
		if (key[SDL_SCANCODE_D]) camera_position -= camera_right * 0.1f;

		static float cube_speed = 1.0f;
		if (key[SDL_SCANCODE_Q]) cube_speed -= 0.01f;
		if (key[SDL_SCANCODE_E]) cube_speed += 0.01f;

		auto const camera_view = glm::lookAt(camera_position, camera_position + camera_forward, camera_up);
		set_uniform(vert_shader_g, uniform_view, camera_view);

		/* g-buffer pass */
		static auto const viewport_width = screen_width;
		static auto const viewport_height = screen_height;
		glViewport(0, 0, viewport_width, viewport_height);

		auto const depth_clear_val = 1.0f;
		glClearNamedFramebufferfv(fb_gbuffer, GL_COLOR, 0, glm::value_ptr(glm::vec3(0.0f)));
		glClearNamedFramebufferfv(fb_gbuffer, GL_COLOR, 1, glm::value_ptr(glm::vec3(0.0f)));
		glClearNamedFramebufferfv(fb_gbuffer, GL_COLOR, 2, glm::value_ptr(glm::vec4(0.0f)));
		glClearNamedFramebufferfv(fb_gbuffer, GL_DEPTH, 0, &depth_clear_val);

		glBindFramebuffer(GL_FRAMEBUFFER, fb_gbuffer);

		glBindTextureUnit(0, texture_cube_diffuse);
		glBindTextureUnit(1, texture_cube_specular);
		glBindTextureUnit(2, texture_cube_normal);

		glBindProgramPipeline(pr_g);
		glBindVertexArray(vao_cube);

		/* cube orbit */
		auto const cube_position = glm::vec3(0.0f, 0.0f, 0.0f);
		static auto cube_rotation = 0.0f;

		set_uniform(vert_shader_g, uniform_modl, glm::translate(cube_position) * glm::rotate(cube_rotation*cube_speed, glm::vec3(0.0f, 1.0f, 0.0f)));

		glDrawElements(GL_TRIANGLES, indices_cube.size(), GL_UNSIGNED_BYTE, nullptr);

		for (auto i = 0; i < 4; i++)
		{
			auto const orbit_amount = (cube_rotation * cube_speed + float(i) * 90.0f * glm::pi<float>() / 180.0f);
			auto const orbit_pos = orbit_axis(orbit_amount, glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(0.0f, 2.0f, 0.0f)) + glm::vec3(-2.0f, 0.0f, 0.0f);
			set_uniform(vert_shader_g, uniform_modl,
				glm::translate(cube_position + orbit_pos) *
				glm::rotate(orbit_amount, glm::vec3(0.0f, -1.0f, 0.0f))
			);

			glDrawElements(GL_TRIANGLES, indices_cube.size(), GL_UNSIGNED_BYTE, nullptr);
		}
		cube_rotation += 0.1f;

		glBindVertexArray(vao_quad);

		set_uniform(vert_shader_g, uniform_modl, glm::translate(glm::vec3(0.0f, -3.0f, 0.0f)) * glm::scale(glm::vec3(10.0f, 1.0f, 10.0f)));

		glDrawElements(GL_TRIANGLES, indices_quad.size(), GL_UNSIGNED_BYTE, nullptr);

		/* actual shading pass */
		glClearNamedFramebufferfv(fb_finalcolor, GL_COLOR, 0, glm::value_ptr(glm::vec3(0.0f)));
		glClearNamedFramebufferfv(fb_finalcolor, GL_DEPTH, 0, &depth_clear_val);

		glBindFramebuffer(GL_FRAMEBUFFER, fb_finalcolor);

		glBindTextureUnit(0, texture_gbuffer_position);
		glBindTextureUnit(1, texture_gbuffer_normal);
		glBindTextureUnit(2, texture_gbuffer_albedo);
		glBindTextureUnit(3, texture_gbuffer_depth);
		glBindTextureUnit(4, texture_skybox);

		glBindProgramPipeline(pr);
		glBindVertexArray(vao_empty);

		set_uniform(frag_shader, uniform_cam_pos, camera_position);
		set_uniform(vert_shader, uniform_cam_dir, glm::inverse(glm::mat3(camera_view)));
		set_uniform(vert_shader, uniform_fov, fov);
		set_uniform(vert_shader, uniform_aspect, float(viewport_width) / float(viewport_height));
		set_uniform(vert_shader, uniform_uvs_diff, glm::vec2(
			float(viewport_width) / float(screen_width),
			float(viewport_height) / float(screen_height)
		));

		glDrawArrays(GL_TRIANGLES, 0, 6);

		/* scale raster */
		glViewport(0, 0, window_width, window_height);

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glBlitNamedFramebuffer(fb_finalcolor, 0, 0, 0, viewport_width, viewport_height, 0, 0, window_width, window_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

		SDL_GL_SwapWindow(window);
	}

	delete_items(glDeleteBuffers,
		{
		vbo_cube, ibo_cube,
		vbo_quad, ibo_quad,
		});
	delete_items(glDeleteTextures,
		{
		texture_cube_diffuse, texture_cube_specular, texture_cube_normal,
		texture_gbuffer_position, texture_gbuffer_albedo, texture_gbuffer_normal, texture_gbuffer_depth, texture_gbuffer_color,
		texture_skybox
		});
	delete_items(glDeleteProgram, {
		vert_shader, frag_shader,
		vert_shader_g, frag_shader_g,
		});

	delete_items(glDeleteProgramPipelines, { pr, pr_g });
	delete_items(glDeleteVertexArrays, { vao_cube, vao_empty });

	glDeleteFramebuffers(1, &fb_gbuffer);
	glDeleteFramebuffers(1, &fb_finalcolor);

	SDL_GL_DeleteContext(gl_context);
	SDL_DestroyWindow(window);
	return 0;
}
