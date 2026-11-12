#include "Includes.h"
#include "GLUtils.h"

const char *VertexShaderSource = "			\
#version 150								\n\
in vec2 VertexPosition;						\n\
in vec2 Coordinate;							\n\
out vec2 Coord;								\n\
uniform float Depth;						\n\
void main() {								\n\
	gl_Position = vec4(VertexPosition, Depth, 1.0);\n\
	Coord = Coordinate;						\n\
}											\n\
";

const char *FragmentShaderSource = "	\n\
#version 150							\n"
#ifdef USE_GL_TEXTURE_RECTANGLE
"uniform sampler2DRect Tex;				\n"
#else
"uniform sampler2D Tex;					\n"
#endif
"uniform sampler1D Palette;				\n\
uniform float ItMul;					\n\
uniform float MaxIt;					\n\
uniform float ColorOffset;				\n\
uniform bool GammaCorrection;			\n\
in vec2 Coord;							\n\
out vec4 Color;							\n\
void main() {							\n\
	float val = texture(Tex, Coord).r;	\n\
	if (isnan(val)) discard;			\n\
	if (val >= MaxIt) { Color = vec4(0.0, 0.0, 0.0, 1.0); return; }		\n\
	Color = texture(Palette, val * ItMul + ColorOffset);\n\
	if (GammaCorrection) Color.rgb = sqrt(Color.rgb);\n\
	return;				\n\
}										\n\
";

const char *LineVertexShaderSource = "			\
#version 150								\n\
in vec2 coord;								\n\
void main() {								\n\
	gl_Position = vec4(coord, -0.1, 1.0);	\n\
}											\n\
";

const char *LineFragmentShaderSource = "	\n\
#version 150							\n\
out vec4 Color;							\n\
void main() {							\n\
	Color = vec4(1.0, 1.0, 1.0, 0.625);return;			\n\
}										\n\
";
GLuint LineShaderProgram;
GLuint LineCoordBuffer;
GLuint LineCoordAttribute;


float PositionArray[] = {
	-1.0,	-1.0,
	-1.0,	1.0,
	1.0,	1.0,
	1.0,	-1.0,
};

float CoordinateArray[] = {
	0.0,	0.0,
	0.0,	1.0,
	1.0,	1.0,
	1.0,	0.0,
};

GLuint ShaderProgram;
GLuint vao;
GLuint PositionBuffer;
GLuint PositionAttribute;
GLuint CoordinateBuffer;
GLuint CoordinateAttribute;

GLuint IterationConutTexture;

GLuint PaletteImage;

float PaletteSize = 6.0;
void InitRenderResources() {
	InitGLExtensions();
#ifdef _WIN32
	// The SDL front-end sets the swap interval through SDL instead.
	GL_wglSwapIntervalEXT(1);
#endif
	ShaderProgram = LoadShaders(VertexShaderSource, FragmentShaderSource);

	GL_glUseProgram(ShaderProgram);

	GL_glGenVertexArrays(1, &vao);
	GL_glBindVertexArray(vao);

	GL_glGenBuffers(1, &PositionBuffer);
	GL_glBindBuffer(GL_ARRAY_BUFFER, PositionBuffer);
	GL_glBufferData(GL_ARRAY_BUFFER, sizeof(PositionArray), PositionArray, GL_DYNAMIC_DRAW);

	PositionAttribute = GL_glGetAttribLocation(ShaderProgram, "VertexPosition");
	GL_glEnableVertexAttribArray(PositionAttribute);
	GL_glVertexAttribPointer(PositionAttribute, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

	GL_glGenBuffers(1, &CoordinateBuffer);
	GL_glBindBuffer(GL_ARRAY_BUFFER, CoordinateBuffer);
	GL_glBufferData(GL_ARRAY_BUFFER, sizeof(CoordinateArray), CoordinateArray, GL_DYNAMIC_DRAW);

	CoordinateAttribute = GL_glGetAttribLocation(ShaderProgram, "Coordinate");
	GL_glEnableVertexAttribArray(CoordinateAttribute);
	GL_glVertexAttribPointer(CoordinateAttribute, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

	GL_glActiveTexture(GL_TEXTURE2);
	glGenTextures(1, &PaletteImage);
	glBindTexture(GL_TEXTURE_1D, PaletteImage);

	SetPalette(Global::Palette);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	GL_glUniform1i(GL_glGetUniformLocation(ShaderProgram, "Palette"), 2);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glPointSize(5);
	glLineWidth(2);

	LineShaderProgram = LoadShaders(LineVertexShaderSource, LineFragmentShaderSource);
	GL_glUseProgram(LineShaderProgram);
	
	GL_glGenBuffers(1, &LineCoordBuffer);
	GL_glBindBuffer(GL_ARRAY_BUFFER, LineCoordBuffer);
	GL_glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 2 * 2, nullptr, GL_STREAM_DRAW);
	
	LineCoordAttribute = GL_glGetAttribLocation(LineShaderProgram, "coord");
	GL_glEnableVertexAttribArray(LineCoordAttribute);
	GL_glVertexAttribPointer(LineCoordAttribute, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

	auto err = glGetError();

	if (err != GL_NO_ERROR) PlatformDebugBreak();

	ResizeViewport(InitialWindowWidth, InitialWindowHeight);
}

void ResizeViewport(int32_t Width, int32_t Height) {
	glViewport(0, 0, Width, Height);
}

void RenderFractal(FractalContext &Context) {
	TextureDescription td[32];
	size_t NumberObtained;

	Context.GetTextures(td, 32, NumberObtained);
	if (!NumberObtained) return;

	GL_glUseProgram(ShaderProgram);

	GL_glActiveTexture(GL_TEXTURE0);

	GL_glUniform1f(GL_glGetUniformLocation(ShaderProgram, "ItMul"), 1.0 / Global::ItDiv);
	GL_glUniform1f(GL_glGetUniformLocation(ShaderProgram, "MaxIt"), Global::MaxIt);

	RelRect View;
	View.X = Context.RenderLocation.X;
	View.Y = Context.RenderLocation.Y;
	View.HalfH = Context.RenderLocation.HalfH;
	View.HalfW = View.HalfH * Context.ImageWidth / Context.ImageHeight;
	for (size_t i = 0; i < NumberObtained; i++) {
		GL_glActiveTexture(GL_TEXTURE0);
		glBindTexture(TARGET_TEXTRUE, td[i].Texture);

		HRReal diffXFractal = td[i].FractalRect.X - View.X;
		HRReal diffYFractal = td[i].FractalRect.Y - View.Y;

		HRReal diffX = Global::TransformMatrix[0][0] * diffXFractal + Global::TransformMatrix[1][0] * diffYFractal;
		HRReal diffY = Global::TransformMatrix[0][1] * diffXFractal + Global::TransformMatrix[1][1] * diffYFractal;

		SRReal Left = SRReal((diffX - td[i].FractalRect.HalfW) / View.HalfW);
		SRReal Right = SRReal((diffX + td[i].FractalRect.HalfW) / View.HalfW);
		SRReal Bottom = SRReal((diffY - td[i].FractalRect.HalfH) / View.HalfH);
		SRReal Top = SRReal((diffY + td[i].FractalRect.HalfH) / View.HalfH);

		PositionArray[0] = Left;
		PositionArray[1] = Bottom;
		PositionArray[2] = Left;
		PositionArray[3] = Top;
		PositionArray[4] = Right;
		PositionArray[5] = Top;
		PositionArray[6] = Right;
		PositionArray[7] = Bottom;

		Left = td[i].TextureRect.X;
		Right = Left + td[i].TextureRect.Width;
		Bottom = td[i].TextureRect.Y;
		Top = Bottom + td[i].TextureRect.Height;

		CoordinateArray[0] = Left;
		CoordinateArray[1] = Bottom;
		CoordinateArray[2] = Left;
		CoordinateArray[3] = Top;
		CoordinateArray[4] = Right;
		CoordinateArray[5] = Top;
		CoordinateArray[6] = Right;
		CoordinateArray[7] = Bottom;

		GL_glBindBuffer(GL_ARRAY_BUFFER, PositionBuffer);
		GL_glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(PositionArray), PositionArray);
		GL_glVertexAttribPointer(PositionAttribute, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

		GL_glBindBuffer(GL_ARRAY_BUFFER, CoordinateBuffer);
		GL_glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(CoordinateArray), CoordinateArray);
		GL_glVertexAttribPointer(CoordinateAttribute, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

		GL_glUniform1i(GL_glGetUniformLocation(ShaderProgram, "Tex"), 0);
		GL_glUniform1f(GL_glGetUniformLocation(ShaderProgram, "Depth"), float(i) / NumberObtained);
		GL_glUniform1i(GL_glGetUniformLocation(ShaderProgram, "GammaCorrection"), Global::GammaCorrection);
		GL_glUniform1f(GL_glGetUniformLocation(ShaderProgram, "ColorOffset"), Global::ColoringValueOffset + 0.5 / PaletteSize);

		glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	}
}

void SetPalette(const std::vector<RGBA> &Palette) {
	if (!Palette.size()) return;
	GL_glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_1D, PaletteImage);
	glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA32F, Palette.size(), 0, GL_RGBA, GL_FLOAT, (const GLvoid *)Palette.data());
	PaletteSize = Palette.size();
}

void RenderLineToFeature(double mouseX, double mouseY, double featureX, double featureY) {
	GL_glUseProgram(LineShaderProgram);

	double diffX = featureX - mouseX;
	double diffY = featureY - mouseY;
	double length = sqrt(diffX * diffX + diffY * diffY);

	if (length <= 0.015) return;
	diffX /= length;
	diffY /= length;
	mouseX += diffX * 0.005;
	mouseY += diffY * 0.005;
	featureX -= diffX * 0.005;
	featureY -= diffY * 0.005;

	float invAspectRatio = float(FContext.ImageHeight) / FContext.ImageWidth;
	float coords[4] = { (float)mouseX * invAspectRatio, (float)mouseY, (float)featureX * invAspectRatio, (float)featureY };

	GL_glBindBuffer(GL_ARRAY_BUFFER, LineCoordBuffer);
	GL_glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(coords), coords);
	GL_glVertexAttribPointer(LineCoordAttribute, 2, GL_FLOAT, GL_FALSE, 0, 0);

	glDrawArrays(GL_LINES, 0, 2);
}

void BeginRender() {
	glClearColor(0.f, 0.f, 0.f, 0.f);
	glClearDepth(1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	IntPix ViewWidth, ViewHeight;
	if ((SRReal)WindowWidth / WindowHeight >= (SRReal)FContext.ImageWidth / FContext.ImageHeight) {
		ViewHeight = WindowHeight;
		ViewWidth = WindowHeight * FContext.ImageWidth / FContext.ImageHeight;
		} else {
		ViewHeight = WindowWidth * FContext.ImageHeight / FContext.ImageWidth;
		ViewWidth = WindowWidth;
	}
	glViewport((WindowWidth - ViewWidth) / 2, (WindowHeight - ViewHeight) / 2, ViewWidth, ViewHeight);
}

void EndRender() {
	PlatformPresent();
}

void EnablePaletteMipmap(bool Enable) {
	glBindTexture(GL_TEXTURE_1D, PaletteImage);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, Enable ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
}