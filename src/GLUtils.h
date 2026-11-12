#ifndef _GLUTIL_H_
#define _GLUTIL_H_

// PLATFORM DEPENDENT
extern PFNGLCREATEPROGRAMPROC GL_glCreateProgram;
extern PFNGLCREATESHADERPROC GL_glCreateShader;
extern PFNGLSHADERSOURCEPROC GL_glShaderSource;
extern PFNGLCOMPILESHADERPROC GL_glCompileShader;
extern PFNGLGETSHADERIVPROC GL_glGetShaderiv;
extern PFNGLGETPROGRAMIVPROC GL_glGetProgramiv;
extern PFNGLDELETESHADERPROC GL_glDeleteShader;
extern PFNGLATTACHSHADERPROC GL_glAttachShader;
extern PFNGLLINKPROGRAMPROC GL_glLinkProgram;
extern PFNGLUSEPROGRAMPROC	GL_glUseProgram;

extern PFNGLGENVERTEXARRAYSPROC			GL_glGenVertexArrays;
extern PFNGLBINDVERTEXARRAYPROC			GL_glBindVertexArray;
extern PFNGLGENBUFFERSPROC				GL_glGenBuffers;
extern PFNGLBINDBUFFERPROC				GL_glBindBuffer;
extern PFNGLBUFFERDATAPROC				GL_glBufferData;
extern PFNGLBUFFERSUBDATAPROC			GL_glBufferSubData;
extern PFNGLGETATTRIBLOCATIONPROC		GL_glGetAttribLocation;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC	GL_glEnableVertexAttribArray;
extern PFNGLVERTEXATTRIBPOINTERPROC		GL_glVertexAttribPointer;
extern PFNGLACTIVETEXTUREPROC			GL_glActiveTexture;
extern PFNGLGETUNIFORMLOCATIONPROC		GL_glGetUniformLocation;
extern PFNGLUNIFORM1IPROC				GL_glUniform1i;
extern PFNGLUNIFORM1FPROC				GL_glUniform1f;

extern PFNWGLSWAPINTERVALEXTPROC		GL_wglSwapIntervalEXT;

extern PFNGLGETSHADERINFOLOGPROC		GL_glGetShaderInfoLog;
extern PFNGLGETPROGRAMINFOLOGPROC		GL_glGetProgramInfoLog;

void InitGLExtensions();

GLuint LoadShaders(const char *VSSource, const char *FSSource);

#endif