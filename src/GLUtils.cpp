#include "Includes.h"
#include <iostream>

PFNGLCREATEPROGRAMPROC	GL_glCreateProgram;
PFNGLCREATESHADERPROC	GL_glCreateShader;
PFNGLSHADERSOURCEPROC	GL_glShaderSource;
PFNGLCOMPILESHADERPROC	GL_glCompileShader;
PFNGLGETSHADERIVPROC	GL_glGetShaderiv;
PFNGLGETPROGRAMIVPROC	GL_glGetProgramiv;
PFNGLDELETESHADERPROC	GL_glDeleteShader;
PFNGLATTACHSHADERPROC	GL_glAttachShader;
PFNGLLINKPROGRAMPROC	GL_glLinkProgram;
PFNGLUSEPROGRAMPROC		GL_glUseProgram;

PFNGLGENVERTEXARRAYSPROC			GL_glGenVertexArrays;
PFNGLBINDVERTEXARRAYPROC			GL_glBindVertexArray;
PFNGLGENBUFFERSPROC					GL_glGenBuffers;
PFNGLBINDBUFFERPROC					GL_glBindBuffer;
PFNGLBUFFERDATAPROC					GL_glBufferData;
PFNGLBUFFERSUBDATAPROC				GL_glBufferSubData;
PFNGLGETATTRIBLOCATIONPROC			GL_glGetAttribLocation;
PFNGLENABLEVERTEXATTRIBARRAYPROC	GL_glEnableVertexAttribArray;
PFNGLVERTEXATTRIBPOINTERPROC		GL_glVertexAttribPointer;
PFNGLACTIVETEXTUREPROC				GL_glActiveTexture;
PFNGLGETUNIFORMLOCATIONPROC			GL_glGetUniformLocation;
PFNGLUNIFORM1IPROC					GL_glUniform1i;
PFNGLUNIFORM1FPROC					GL_glUniform1f;

PFNWGLSWAPINTERVALEXTPROC			GL_wglSwapIntervalEXT;

PFNGLGETSHADERINFOLOGPROC			GL_glGetShaderInfoLog;
PFNGLGETPROGRAMINFOLOGPROC			GL_glGetProgramInfoLog;

void InitGLExtensions() {
	GL_glCreateProgram = (PFNGLCREATEPROGRAMPROC)PlatformGetGLProcAddress("glCreateProgram");
	GL_glCreateShader = (PFNGLCREATESHADERPROC)PlatformGetGLProcAddress("glCreateShader");
	GL_glShaderSource = (PFNGLSHADERSOURCEPROC)PlatformGetGLProcAddress("glShaderSource");
	GL_glCompileShader = (PFNGLCOMPILESHADERPROC)PlatformGetGLProcAddress("glCompileShader");
	GL_glGetShaderiv = (PFNGLGETSHADERIVPROC)PlatformGetGLProcAddress("glGetShaderiv");
	GL_glGetProgramiv = (PFNGLGETPROGRAMIVPROC)PlatformGetGLProcAddress("glGetProgramiv");
	GL_glDeleteShader = (PFNGLDELETESHADERPROC)PlatformGetGLProcAddress("glDeleteShader");
	GL_glAttachShader = (PFNGLATTACHSHADERPROC)PlatformGetGLProcAddress("glAttachShader");
	GL_glLinkProgram = (PFNGLLINKPROGRAMPROC)PlatformGetGLProcAddress("glLinkProgram");
	GL_glUseProgram = (PFNGLUSEPROGRAMPROC)PlatformGetGLProcAddress("glUseProgram");
	GL_glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)PlatformGetGLProcAddress("glGenVertexArrays");
	GL_glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)PlatformGetGLProcAddress("glBindVertexArray");
	GL_glGenBuffers = (PFNGLGENBUFFERSPROC)PlatformGetGLProcAddress("glGenBuffers");
	GL_glBindBuffer = (PFNGLBINDBUFFERPROC)PlatformGetGLProcAddress("glBindBuffer");
	GL_glBufferData = (PFNGLBUFFERDATAPROC)PlatformGetGLProcAddress("glBufferData");
	GL_glBufferSubData = (PFNGLBUFFERSUBDATAPROC)PlatformGetGLProcAddress("glBufferSubData");
	GL_glGetAttribLocation = (PFNGLGETATTRIBLOCATIONPROC)PlatformGetGLProcAddress("glGetAttribLocation");
	GL_glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)PlatformGetGLProcAddress("glEnableVertexAttribArray");
	GL_glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)PlatformGetGLProcAddress("glVertexAttribPointer");
	GL_glActiveTexture = (PFNGLACTIVETEXTUREPROC)PlatformGetGLProcAddress("glActiveTexture");
	GL_glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)PlatformGetGLProcAddress("glGetUniformLocation");
	GL_glUniform1i = (PFNGLUNIFORM1IPROC)PlatformGetGLProcAddress("glUniform1i");
	GL_glUniform1f = (PFNGLUNIFORM1FPROC)PlatformGetGLProcAddress("glUniform1f");
	GL_wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)PlatformGetGLProcAddress("wglSwapIntervalEXT");
	GL_glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)PlatformGetGLProcAddress("glGetShaderInfoLog");
	GL_glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)PlatformGetGLProcAddress("glGetProgramInfoLog");
}

GLuint LoadShaders(const char *VSSource, const char *FSSource) {
	GLuint Program = GL_glCreateProgram();

	GLuint VertexShader = GL_glCreateShader(GL_VERTEX_SHADER);
	GLuint FragmentShader = GL_glCreateShader(GL_FRAGMENT_SHADER);
	GL_glShaderSource(VertexShader, 1, &VSSource, nullptr);
	GL_glShaderSource(FragmentShader, 1, &FSSource, nullptr);
	GL_glCompileShader(VertexShader);

	GLint Compiled;
	GL_glGetShaderiv(VertexShader, GL_COMPILE_STATUS, &Compiled);
	if (!Compiled) {
#ifdef _DEBUG
		GLsizei Length;
		GL_glGetShaderiv(VertexShader, GL_INFO_LOG_LENGTH, &Length);

		GLchar *Log = new GLchar[Length + 1];
		GL_glGetShaderInfoLog(VertexShader, Length, &Length, Log);
		std::cerr << "Shader compilation failed: " << Log << std::endl;
		delete[] Log;
#endif /* DEBUG */
		GL_glDeleteShader(VertexShader);
		return 0;
	}

	GL_glCompileShader(FragmentShader);

	GL_glGetShaderiv(FragmentShader, GL_COMPILE_STATUS, &Compiled);
	if (!Compiled) {
#ifdef _DEBUG
		GLsizei Length;
		GL_glGetShaderiv(FragmentShader, GL_INFO_LOG_LENGTH, &Length);

		GLchar *Log = new GLchar[Length + 1];
		GL_glGetShaderInfoLog(FragmentShader, Length, &Length, Log);
		std::cerr << "Shader compilation failed: " << Log << std::endl;
		delete[] Log;
#endif /* DEBUG */
		GL_glDeleteShader(VertexShader);
		GL_glDeleteShader(FragmentShader);
		return 0;
	}

	GL_glAttachShader(Program, VertexShader);
	GL_glAttachShader(Program, FragmentShader);

	GL_glLinkProgram(Program);

	GLint Linked;
	GL_glGetProgramiv(Program, GL_LINK_STATUS, &Linked);
	if (!Linked) {
#ifdef _DEBUG
		GLsizei Length;
		GL_glGetProgramiv(Program, GL_INFO_LOG_LENGTH, &Length);

		GLchar *Log = new GLchar[Length + 1];
		GL_glGetProgramInfoLog(Program, Length, &Length, Log);
		std::cerr << "Shader linking failed: " << Log << std::endl;
		delete[] Log;
#endif /* DEBUG */

		GL_glDeleteShader(VertexShader);
		GL_glDeleteShader(FragmentShader);

		return 0;
	}

	return Program;
}