#include "osd.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bcm_host.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include "stb_image.h"

#define FNT_CELL_HEIGHT 16
#define FNT_CELL_WIDTH 8
#define FNT_FIRST 32
#define FNT_IMG_HEIGHT 48
#define FNT_IMG_WIDTH 256
#define FNT_PATH "/usr/local/share/fpv/unifont.png"

#define MARGIN_BOTTOM 16
#define MARGIN_LEFT 28
#define MARGIN_RIGHT 28
#define MARGIN_TOP 16

#define REC_IMG_HEIGHT 64
#define REC_IMG_WIDTH 64
#define REC_PATH "/usr/local/share/fpv/rec.png"

struct osd
{
	DISPMANX_DISPLAY_HANDLE_T dispmanx_display;
	DISPMANX_ELEMENT_HANDLE_T dispmanx_element;
	EGL_DISPMANX_WINDOW_T dispmanx_window;

	EGLContext context;
	EGLDisplay display;
	EGLSurface surface;

	struct
	{
		uint32_t width, height;
	} screen;

	GLuint font_texture, rec_texture;
	GLuint program;
	GLuint vbo;

	struct
	{
		GLuint screen_size;
		GLuint texture;
	} uniforms;

	int32_t altitude;
	uint16_t heading;
	uint16_t voltage;
	uint8_t cells;
	uint8_t recording;
};

typedef struct
{
	uint8_t r, g, b, a;
} color_t;

typedef struct
{
	float x, y;
} vec2_t;

struct vertex
{
	vec2_t pos;
	vec2_t tex;
	color_t color;
};

#define ATTRIB_POS 1
#define ATTRIB_TEX 2
#define ATTRIB_COL 3

static const char *_error;

static const char fragment_source[] =
"varying vec4 v_col;"
"varying vec2 v_tex;"
""
"uniform sampler2D u_texture;"
""
"void main()"
"{"
	"vec4 texel = texture2D(u_texture, v_tex);"
	"gl_FragColor = v_col * vec4(1, 1, 1, texel.r);"
"}";

static const char vertex_source[] =
"attribute vec2 a_pos;"
"attribute vec2 a_tex;"
"attribute vec4 a_col;"
""
"varying vec4 v_col;"
"varying vec2 v_tex;"
""
"uniform vec2 u_screen_size;"
"void main()"
"{"
	"v_col = a_col;"
	"v_tex = a_tex;"
	"vec2 pos = (a_pos / u_screen_size) * vec2(2, -2) + vec2(-1, 1);"
	"gl_Position = vec4(pos, 0.0, 1.0);"
"}";

static GLuint create_shader(GLenum type, const char *source)
{
	GLint result;
	GLuint shader;
	
	shader = glCreateShader(type);
	if(!shader)
	{
		_error = "Failed to create shader.";
		goto fail;
	}

	glShaderSource(shader, 1, (const GLchar **)&source, 0);
	glCompileShader(shader);

	glGetShaderiv(shader, GL_COMPILE_STATUS, &result);
	if(!result)
	{
		_error = "Failed to compile shader.";
		goto fail;
	}

	return shader;

fail:
	if(shader) glDeleteShader(shader);
	return 0;
}

static GLuint load_texture(const char *path, GLenum filter)
{
	uint8_t *bitmap;
	int components, height, width;
	GLenum format, internal_format;
	GLuint texture;	

	bitmap = stbi_load(path, &width, &height, &components, 0);
	if(!bitmap)
	{
		_error = "Failed to load bitmap font.";
		goto fail;
	}

	switch(components)
	{
		case 1:
			format = GL_LUMINANCE;
			internal_format = GL_LUMINANCE;
			break;

		case 2:
			format = GL_LUMINANCE_ALPHA;
			internal_format = GL_LUMINANCE_ALPHA;
			break;

		case 3:
			format = GL_RGB;
			internal_format = GL_RGB;
			break;

		case 4:
			format = GL_RGBA;
			internal_format = GL_RGBA;
			break;

	}

	glGenTextures(1, &texture);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
	glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0, format, GL_UNSIGNED_BYTE, bitmap);

	stbi_image_free(bitmap);
	return texture;

fail:
	if(bitmap) stbi_image_free(bitmap);
	if(texture) glDeleteTextures(1, &texture);
	return 0;
}

static void osd_deinit_display(osd_t osd)
{
	DISPMANX_UPDATE_HANDLE_T update;

	if(osd->surface) eglDestroySurface(osd->display, osd->surface);
	
	if(osd->dispmanx_element)
	{
		update = vc_dispmanx_update_start(0);
		vc_dispmanx_element_remove(update, osd->dispmanx_element);
		vc_dispmanx_update_submit_sync(update);
	}
	
	if(osd->dispmanx_display) vc_dispmanx_display_close(osd->dispmanx_display);

	if(osd->display)
	{
		eglMakeCurrent(osd->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		if(osd->context) eglDestroyContext(osd->display, osd->context);
		eglTerminate(osd->display);
	}
}

void osd_deinit_gl_font(osd_t osd)
{
	if(osd->font_texture) glDeleteTextures(1, &osd->font_texture);
}

void osd_deinit_gl_rec(osd_t osd)
{
	if(osd->rec_texture) glDeleteTextures(1, &osd->rec_texture);
}

void osd_deinit_gl_shader(osd_t osd)
{
	if(osd->program) glDeleteProgram(osd->program);
}

void osd_deinit_gl_vbo(osd_t osd)
{
	if(osd->vbo) glDeleteBuffers(1, &osd->vbo);
}

void osd_deinit_gl(osd_t osd)
{
	osd_deinit_gl_vbo(osd);
	osd_deinit_gl_shader(osd);
	osd_deinit_gl_rec(osd);
	osd_deinit_gl_font(osd);
}

void osd_deinit(osd_t osd)
{
	if(osd)
	{
		osd_deinit_gl(osd);
		osd_deinit_display(osd);
		free(osd);
	}
}

static int osd_init_display(osd_t osd)
{
	const EGLint attributes[] =
	{
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_NONE,
	};

	const EGLint context_attributes[] =
	{
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE,
	};
	
	EGLConfig config;
	EGLBoolean result_b;
	int result_i;
	
	VC_RECT_T dst_rect, src_rect;
	DISPMANX_UPDATE_HANDLE_T update;

	osd->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if(osd->display == EGL_NO_DISPLAY)
	{
		_error = "Failed to get default display.";
		goto fail;
	}

	result_b = eglInitialize(osd->display, 0, 0);
	if(result_b == EGL_FALSE)
	{
		_error = "Failed to initialize EGL display.";
		goto fail;
	}
	
	result_b = eglChooseConfig(osd->display, attributes, &config, 1, &result_i);
	if(result_b == EGL_FALSE)
	{
		_error = "Failed to choose EGL configuration.";
		goto fail;
	}
	
	osd->context = eglCreateContext(osd->display, config, EGL_NO_CONTEXT, context_attributes);
	if(osd->context == EGL_NO_CONTEXT)
	{
		_error = "Failed to create EGL context.";
		goto fail;
	}
	
	result_i = graphics_get_display_size(0, &osd->screen.width, &osd->screen.height);
	if(result_i < 0)
	{
		_error = "Failed to get display size.";
		goto fail;
	}
	
	src_rect.x = 0;
	src_rect.y = 0;
	src_rect.width = osd->screen.width << 16;
	src_rect.height = osd->screen.height << 16;
	
	dst_rect.x = 0;
	dst_rect.y = 0;
	dst_rect.width = osd->screen.width;
	dst_rect.height = osd->screen.height;
	
	osd->dispmanx_display = vc_dispmanx_display_open(0);
	update = vc_dispmanx_update_start(0);
	
	osd->dispmanx_element = vc_dispmanx_element_add(update, osd->dispmanx_display, 192, &dst_rect, 0, &src_rect, DISPMANX_PROTECTION_NONE, 0, 0, 0);

	osd->dispmanx_window.element = osd->dispmanx_element;
	osd->dispmanx_window.height = osd->screen.height;
	osd->dispmanx_window.width = osd->screen.width;
	vc_dispmanx_update_submit_sync(update);
	
	osd->surface = eglCreateWindowSurface(osd->display, config, &osd->dispmanx_window, 0);
	if(osd->surface == EGL_NO_SURFACE)
	{
		_error = "Failed to create EGL surface.";
		goto fail;
	}
	
	result_b = eglMakeCurrent(osd->display, osd->surface, osd->surface, osd->context);
	if(result_b == EGL_FALSE)
	{
		_error = "Failed to activate EGL display.";
		goto fail;
	}
	
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	
	return 0;

fail:
	if(osd) osd_deinit_display(osd);
	return 1;
}

static int osd_init_gl_font(osd_t osd)
{
	GLuint texture = load_texture(FNT_PATH, GL_NEAREST);
	if(!texture) goto fail;

	osd->font_texture = texture;
	return 0;
	
fail:
	if(texture) glDeleteTextures(1, &texture);
	return 1;
}

static int osd_init_gl_rec(osd_t osd)
{
	GLuint texture = load_texture(REC_PATH, GL_LINEAR);
	if(!texture) goto fail;

	osd->rec_texture = texture;
	return 0;
	
fail:
	if(texture) glDeleteTextures(1, &texture);
	return 1;
}

static int osd_init_gl_shader(osd_t osd)
{
	GLuint fragment_shader = 0;
	GLuint vertex_shader = 0;
	GLuint program = 0;
	GLint result;
	
	fragment_shader = create_shader(GL_FRAGMENT_SHADER, fragment_source);
	if(!fragment_shader) goto fail;

	vertex_shader = create_shader(GL_VERTEX_SHADER, vertex_source);
	if(!vertex_shader) goto fail;

	program = glCreateProgram();
	glBindAttribLocation(program, ATTRIB_POS, "a_pos");
	glBindAttribLocation(program, ATTRIB_TEX, "a_tex");
	glBindAttribLocation(program, ATTRIB_COL, "a_col");

	glAttachShader(program, fragment_shader);
	glAttachShader(program, vertex_shader);
	glLinkProgram(program);
	glDetachShader(program, fragment_shader);
	glDetachShader(program, vertex_shader);

	glDeleteShader(fragment_shader); fragment_shader = 0;
	glDeleteShader(vertex_shader); vertex_shader = 0;

	glGetProgramiv(program, GL_LINK_STATUS, &result);
	if(!result)
	{
		_error = "Failed to link shader program.";
		goto fail;
	}

	osd->program = program;
	glUseProgram(program);

	osd->uniforms.screen_size = glGetUniformLocation(program, "u_screen_size");
	osd->uniforms.texture = glGetUniformLocation(program, "u_texture");
	glUniform2f(osd->uniforms.screen_size, osd->screen.width, osd->screen.height);
	glUniform1i(osd->uniforms.texture, 0);

	return 0;

fail:
	if(program) glDeleteProgram(program);
	if(fragment_shader) glDeleteShader(fragment_shader);
	if(vertex_shader) glDeleteShader(vertex_shader);
	return 1;
}

static int osd_init_gl_vbo(osd_t osd)
{
	const struct vertex vertices[] =
	{
		{{osd->screen.width / 2 - 128, osd->screen.height / 2 - 128},
		 {0.0f, 0.0f},
		 {255, 0, 0, 255}},
		{{osd->screen.width / 2 - 128, osd->screen.height / 2 + 128},
		 {0.0f, 1.0f},
		 {255, 0, 0, 255}},
		{{osd->screen.width / 2 + 128, osd->screen.height / 2 + 128},
		 {1.0f, 1.0f},
		 {255, 0, 0, 255}},
		{{osd->screen.width / 2 - 128, osd->screen.height / 2 - 128},
		 {0.0f, 0.0f},
		 {255, 0, 0, 255}},
		{{osd->screen.width / 2 + 128, osd->screen.height / 2 + 128},
		 {1.0f, 1.0f},
		 {255, 0, 0, 255}},
		{{osd->screen.width / 2 + 128, osd->screen.height / 2 - 128},
		 {1.0f, 0.0f},
		 {255, 0, 0, 255}},
	};

	glGenBuffers(1, &osd->vbo);
	glBindBuffer(GL_ARRAY_BUFFER, osd->vbo);

	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	glEnableVertexAttribArray(ATTRIB_POS);
	glEnableVertexAttribArray(ATTRIB_TEX);
	glEnableVertexAttribArray(ATTRIB_COL);
	glVertexAttribPointer(ATTRIB_POS, 2, GL_FLOAT, GL_FALSE, sizeof(struct vertex), (void *)offsetof(struct vertex, pos));
	glVertexAttribPointer(ATTRIB_TEX, 2, GL_FLOAT, GL_FALSE, sizeof(struct vertex), (void *)offsetof(struct vertex, tex));
	glVertexAttribPointer(ATTRIB_COL, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(struct vertex), (void *)offsetof(struct vertex, color));

	return 0;
}

static int osd_init_gl(osd_t osd)
{
	int result;

	result = osd_init_gl_font(osd);
	if(result) goto fail;

	result = osd_init_gl_rec(osd);
	if(result) goto fail;

	result = osd_init_gl_shader(osd);
	if(result) goto fail;	

	result = osd_init_gl_vbo(osd);
	if(result) goto fail;

	return 0;

fail:
	osd_deinit_gl(osd);
	return 1;
}

osd_t osd_init(void)
{
	osd_t osd;
	int result;

	osd = malloc(sizeof(struct osd));
	if(!osd)
	{
		_error = "Failed to allocate OSD object.";
		goto fail;
	}
	memset(osd, 0, sizeof(struct osd));

	result = osd_init_display(osd);
	if(result) goto fail;

	result = osd_init_gl(osd);
	if(result) goto fail;

	return osd;

fail:
	if(osd) osd_deinit(osd);
	return 0;
}

const char *osd_error(void)
{
	return _error;
}

typedef struct
{
	const char *string;
	vec2_t origin, anchor;
	color_t color;
	float scale;
} draw_string_t;

static void draw_string(draw_string_t *draw)
{
	struct vertex *vertices = 0;
	size_t i, length;
	float cw, ch, x, y;

	cw = FNT_CELL_WIDTH * draw->scale;
	ch = FNT_CELL_HEIGHT * draw->scale;

	length = strlen(draw->string);
	vertices = malloc(length * 6 * sizeof(struct vertex));

	x = draw->origin.x - draw->anchor.x * cw * length;
	y = draw->origin.y - draw->anchor.y * ch;

	for(i = 0; i < length; i++)
	{
		float ul, ur, vb, vt;
		uint8_t c, cx, cy;
		c = draw->string[i];

		cx = (c - FNT_FIRST) % (FNT_IMG_WIDTH / FNT_CELL_WIDTH);
		cy = (c - FNT_FIRST) / (FNT_IMG_WIDTH / FNT_CELL_WIDTH);

		ul = cx * ((float)FNT_CELL_WIDTH / (float)FNT_IMG_WIDTH);
		ur = ul + ((float)FNT_CELL_WIDTH / (float)FNT_IMG_WIDTH);
		vt = cy * ((float)FNT_CELL_HEIGHT / (float)FNT_IMG_HEIGHT);
		vb = vt + ((float)FNT_CELL_HEIGHT / (float)FNT_IMG_HEIGHT);

		vertices[6*i + 0].pos.x = x;
		vertices[6*i + 0].pos.y = y;
		vertices[6*i + 0].tex.x = ul;
		vertices[6*i + 0].tex.y = vt;
		vertices[6*i + 0].color = draw->color;

		vertices[6*i + 1].pos.x = x;
		vertices[6*i + 1].pos.y = y + ch;
		vertices[6*i + 1].tex.x = ul;
		vertices[6*i + 1].tex.y = vb;
		vertices[6*i + 1].color = draw->color;

		vertices[6*i + 2].pos.x = x + cw;
		vertices[6*i + 2].pos.y = y + ch;
		vertices[6*i + 2].tex.x = ur;
		vertices[6*i + 2].tex.y = vb;
		vertices[6*i + 2].color = draw->color;

		vertices[6*i + 3].pos.x = x;
		vertices[6*i + 3].pos.y = y;
		vertices[6*i + 3].tex.x = ul;
		vertices[6*i + 3].tex.y = vt;
		vertices[6*i + 3].color = draw->color;

		vertices[6*i + 4].pos.x = x + cw;
		vertices[6*i + 4].pos.y = y + ch;
		vertices[6*i + 4].tex.x = ur;
		vertices[6*i + 4].tex.y = vb;
		vertices[6*i + 4].color = draw->color;

		vertices[6*i + 5].pos.x = x + cw;
		vertices[6*i + 5].pos.y = y;
		vertices[6*i + 5].tex.x = ur;
		vertices[6*i + 5].tex.y = vt;
		vertices[6*i + 5].color = draw->color;

		x += cw;
	}

	glBufferData(GL_ARRAY_BUFFER, length * 6 * sizeof(struct vertex), vertices, GL_STREAM_DRAW);
	free(vertices);

	glDrawArrays(GL_TRIANGLES, 0, 6 * length);
}

typedef struct
{
	vec2_t origin, anchor, size;
	vec2_t tex_a, tex_b;
	color_t color;
} draw_texture_t;

static void draw_texture(const draw_texture_t *draw)
{
	float left = draw->origin.x - (draw->anchor.x * draw->size.x);
	float right = left + draw->size.x;
	float top = draw->origin.y - (draw->anchor.y * draw->size.y);
	float bottom = top + draw->size.y;

	struct vertex vertices[6] =
	{
		{
			.pos = {left, top},
			.tex = {draw->tex_a.x, draw->tex_a.y},
			.color = draw->color,
		},
		{
			.pos = {left, bottom},
			.tex = {draw->tex_a.x, draw->tex_b.y},
			.color = draw->color,
		},
		{
			.pos = {right, bottom},
			.tex = {draw->tex_b.x, draw->tex_b.y},
			.color = draw->color,
		},
		{
			.pos = {left, top},
			.tex = {draw->tex_a.x, draw->tex_a.y},
			.color = draw->color,
		},
		{
			.pos = {right, bottom},
			.tex = {draw->tex_b.x, draw->tex_b.y},
			.color = draw->color,
		},
		{
			.pos = {right, top},
			.tex = {draw->tex_b.x, draw->tex_a.y},
			.color = draw->color,
		},
	};

	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
	glDrawArrays(GL_TRIANGLES, 0, 6);
}

void osd_set_altitude(osd_t osd, int32_t altitude)
{
	osd->altitude = altitude;
}

void osd_set_heading(osd_t osd, uint16_t heading)
{
	osd->heading = heading;
}

void osd_set_recording(osd_t osd, uint8_t recording)
{
	osd->recording = recording;
}

void osd_set_voltage(osd_t osd, uint16_t voltage, uint8_t cells)
{
	osd->cells = cells;
	osd->voltage = voltage;
}

void osd_update(osd_t osd)
{
	glClear(GL_COLOR_BUFFER_BIT);

	glBindTexture(GL_TEXTURE_2D, osd->font_texture);
	{
		int16_t whole = osd->altitude / 100;
		uint16_t frac = (osd->altitude >= 0 ? osd->altitude : -osd->altitude) % 100;
	
		char buffer[14];
		sprintf(buffer, "ALT % 3d.%02u m", whole, frac);

		draw_string_t draw = {
			.string = buffer,
			.origin = {MARGIN_LEFT, MARGIN_TOP},
			.anchor = {0, 0},
			.color = {255, 255, 255, 255},
			.scale = 2.0f,
		};
		draw_string(&draw);
	}
	
	{
		char buffer[17];
		uint16_t cell_voltage;

		sprintf(buffer, "BAT % 3u.%u V (%uS)", osd->voltage / 1000, (osd->voltage % 1000) / 100, osd->cells);
		draw_string_t draw = {
			.string = buffer,
			.origin = {MARGIN_LEFT, 2 * FNT_CELL_HEIGHT + MARGIN_TOP},
			.anchor = {0, 0},
			.color = {0, 255, 0, 255},
			.scale = 2.0f,
		};
		
		if(osd->cells)
			cell_voltage = osd->voltage / osd->cells;
		else
			cell_voltage = osd->voltage;

		if(cell_voltage < 3500)
		{
			draw.color = (color_t){255, 0, 0, 255};

			draw_string_t draw2 = {
				.string = "BATTERY LOW",
				.origin = {osd->screen.width / 2, osd->screen.height / 2},
				.anchor = {0.5f, 0.5f},
				.color = {255, 0, 0, 255},
				.scale = 3.0f,
			};
			draw_string(&draw2);
		}
		else if(cell_voltage < 3700)
		{
			draw.color = (color_t){255, 255, 0, 255};
		}

		draw_string(&draw);
	}
	
	{
		char buffer[8];
		sprintf(buffer, "HDG %03u", osd->heading / 100);

		draw_string_t draw = {
			.string = buffer,
			.origin = {MARGIN_LEFT, 4 * FNT_CELL_HEIGHT + MARGIN_TOP},
			.anchor = {0, 0},
			.color = {255, 255, 255, 255},
			.scale = 2.0f,
		};
		draw_string(&draw);
	}

	if(osd->recording)
	{
		glBindTexture(GL_TEXTURE_2D, osd->rec_texture);

		draw_texture_t draw = {
			.origin = {osd->screen.width - MARGIN_RIGHT, MARGIN_TOP},
			.anchor = {1.0f, 0.0f},
			.size = REC_IMG_WIDTH, REC_IMG_HEIGHT,
			.tex_a = {0.0f, 0.0f},
			.tex_b = {1.0f, 1.0f},
			.color = {255, 0, 0, 255},
		};
		draw_texture(&draw);
	}

	eglSwapBuffers(osd->display, osd->surface);
}
