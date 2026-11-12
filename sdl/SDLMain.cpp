// SDL3 + Dear ImGui front-end.
//
// This replaces the Win32 window, menu and dialog code of the Windows build so
// that the same user interface runs on Linux, macOS and Windows.  Everything
// below the front-end (Computation, FractalContext, PixelManager, Render) is
// shared with the Win32 build; the front-end only creates the window and the
// OpenGL context, feeds input into FractalContext and draws the UI.

#include "Includes.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// --- globals the shared code expects (they live in MainWindow.cpp for Win32) --
size_t WindowWidth = 1600;
size_t WindowHeight = 900;
size_t InitialWindowWidth = 1600;
size_t InitialWindowHeight = 900;
int32_t MouseX = 0, MouseY = 0;

namespace Global {
	bool moved = false;
	bool ImageSizeFollowWindowSize = true;
}

static bool UseDE = true;
static bool UseLinearApproximation = true;
static bool UsePalleteMipmaps = false;
static bool Panning = false;

static SDL_Window *Window = nullptr;
static SDL_GLContext GLContext = nullptr;

// --- platform hooks ---------------------------------------------------------
void *PlatformGetGLProcAddress(const char *Name) {
	return (void *)SDL_GL_GetProcAddress(Name);
}

void PlatformPresent() {
	SDL_GL_SwapWindow(Window);
}

// PlatformDependent.h
void SetWorkerPriority() {
	SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW);
}

void ErrorMessage(const char *Message) {
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Imagina", Message, Window);
}

void ErrorMessage(const char *Title, const char *Message) {
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, Title, Message, Window);
}

namespace {

// --- small helpers ----------------------------------------------------------
std::string Narrow(const std::wstring_view Text) {
	// The descriptions built by FeatureFinder are ASCII, so a plain copy is
	// enough and avoids the deprecated codecvt machinery.
	std::string Result;
	Result.reserve(Text.size());
	for (wchar_t Character : Text) Result.push_back(Character < 128 ? char(Character) : '?');
	return Result;
}

std::string FormatMpf(const mpf_class &Value, int Digits, char Format = 'f') {
	std::string Buffer(size_t(Digits) + 64, '\0');
	gmp_sprintf(Buffer.data(), ("%." + std::to_string(Digits) + "F" + Format).c_str(), Value.get_mpf_t());
	Buffer.resize(Buffer.find('\0'));
	return Buffer;
}

enum class ZoomType { Height, HalfHeight, Magnification, Depth };

std::string ZoomToString(ZoomType Type, const HRReal &HalfH, const HRReal &OriginalHalfH) {
	char Buffer[128];
	switch (Type) {
		case ZoomType::Height:
			gmp_sprintf(Buffer, "%.12Fg", (2.0_hr * HalfH).to_mpf_class(53).get_mpf_t());
			break;
		case ZoomType::HalfHeight:
			gmp_sprintf(Buffer, "%.12Fg", HalfH.to_mpf_class(53).get_mpf_t());
			break;
		case ZoomType::Magnification:
			gmp_sprintf(Buffer, "%.12Fg", (OriginalHalfH / HalfH).to_mpf_class(53).get_mpf_t());
			break;
		case ZoomType::Depth:
			snprintf(Buffer, sizeof(Buffer), "%.15g", (double)log2(OriginalHalfH / HalfH));
			break;
	}
	return Buffer;
}

HRReal ParseZoom(ZoomType Type, const std::string &Text, const HRReal &OriginalHalfH) {
	HRReal Zoom = mpf_class(Text.data(), 52);
	switch (Type) {
		case ZoomType::Height:
			Zoom *= 0.5;
			break;
		case ZoomType::HalfHeight:
			break;
		case ZoomType::Magnification:
			Zoom = OriginalHalfH / Zoom;
			break;
		case ZoomType::Depth:
			Zoom = OriginalHalfH / pow2(SRReal(Zoom));
			break;
	}
	return Zoom;
}

// --- dialogs ----------------------------------------------------------------
struct UIState {
	bool ShowImageSize = false;
	bool ShowIterationLimit = false;
	bool ShowLocation = false;
	bool ShowTransformation = false;
	bool ShowTasks = false;
	bool ShowAbout = false;
	bool ShowFeature = false;

	int ImageWidth = 0, ImageHeight = 0;
	bool ImageFollowWindow = true;
	bool ImageSetWindowSize = false;

	int IterationLimit = 0;

	int ZoomTypeIndex = 2;
	std::string LocationReal, LocationImaginary, LocationZoom;
	int LocationIterations = 0;

	bool FlipImaginary = false;
	float Rotation = 0, StretchAngle = 0, StretchRatio = 1;

	std::string Error;
	std::string Licence;
	bool LicenceLoaded = false;
};

UIState UI;

void OpenImageSizeDialog() {
	UI.ImageWidth = int(FContext.ImageWidth);
	UI.ImageHeight = int(FContext.ImageHeight);
	UI.ImageFollowWindow = Global::ImageSizeFollowWindowSize;
	UI.ImageSetWindowSize = false;
	UI.ShowImageSize = true;
}

void OpenIterationLimitDialog() {
	UI.IterationLimit = int(Global::ItLim);
	UI.ShowIterationLimit = true;
}

void OpenLocationDialog() {
	size_t Digits = size_t(std::max(0.0, -log10(FContext.CurrentLocation.HalfH))) + 10;
	UI.LocationReal = FormatMpf(mpf_class(FContext.CenterCoordinate.X + HPReal(FContext.CurrentLocation.X)), int(Digits));
	UI.LocationImaginary = FormatMpf(mpf_class(FContext.CenterCoordinate.Y + HPReal(FContext.CurrentLocation.Y)), int(Digits));
	UI.LocationZoom = ZoomToString(ZoomType(UI.ZoomTypeIndex), FContext.CurrentLocation.HalfH, 2.0_hr);
	UI.LocationIterations = int(Global::ItLim);
	UI.ShowLocation = true;
}

void OpenTransformationDialog() {
	UI.FlipImaginary = Global::FlipImaginaryAxis;
	UI.Rotation = float(Global::Rotation);
	UI.StretchAngle = float(Global::StretchAngle);
	UI.StretchRatio = float(Global::StretchRatio);
	UI.ShowTransformation = true;
}

void SetFractalType(FractalTypeEnum Type) {
	CurrentFractalType = Type;
	switch (Type) {
		case FractalTypeEnum::Mandelbrot:
			FContext.ChangeFractalType(Type, UseLinearApproximation);
			break;
		default:
			CurrentFractalType = Type;
			FContext.ChangeFractalType(Type, UseLinearApproximation && Type == FractalTypeEnum::Mandelbrot);
			break;
	}
}

void ResetLocation() {
	FContext.ResetLocation();
}

// --- menu bar ---------------------------------------------------------------
void DrawMenuBar() {
	if (!ImGui::BeginMainMenuBar()) return;

	if (ImGui::BeginMenu("File")) {
		ImGui::MenuItem("Open...", "Ctrl+O", false, false);
		ImGui::MenuItem("Save...", "Ctrl+S", false, false);
		ImGui::MenuItem("Save image...", "Ctrl+Shift+S", false, false);
		ImGui::Separator();
		if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
			SDL_Event Quit = {};
			Quit.type = SDL_EVENT_QUIT;
			SDL_PushEvent(&Quit);
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Fractal")) {
		if (ImGui::BeginMenu("Formula")) {
			if (ImGui::MenuItem("Mandelbrot", nullptr, CurrentFractalType == FractalTypeEnum::Mandelbrot)) SetFractalType(FractalTypeEnum::Mandelbrot);
			if (ImGui::MenuItem("Tricorn", nullptr, CurrentFractalType == FractalTypeEnum::Tricorn)) SetFractalType(FractalTypeEnum::Tricorn);
			if (ImGui::MenuItem("Burning ship", nullptr, CurrentFractalType == FractalTypeEnum::BurningShip)) SetFractalType(FractalTypeEnum::BurningShip);
			if (ImGui::MenuItem("Nova", nullptr, CurrentFractalType == FractalTypeEnum::Nova)) SetFractalType(FractalTypeEnum::Nova);
			ImGui::EndMenu();
		}

		bool DistanceEstimation = UseDE;
		bool SupportsDE = CurrentFractalType == FractalTypeEnum::Nova ||
			(CurrentFractalType == FractalTypeEnum::Mandelbrot && UseLinearApproximation);
		if (ImGui::MenuItem("Distance estimation", nullptr, &DistanceEstimation, SupportsDE)) {
			UseDE = DistanceEstimation;
			FContext.SetUseDE(UseDE);
		}

		bool HighQuality = Global::HighQuality;
		bool SupportsHQ = CurrentFractalType == FractalTypeEnum::Mandelbrot && UseLinearApproximation;
		if (ImGui::MenuItem("High quality (Experimental)", nullptr, &HighQuality, SupportsHQ)) {
			Global::HighQuality = HighQuality;
			FContext.InvalidatePixel();
		}

		ImGui::Separator();
		if (ImGui::MenuItem("Location...")) OpenLocationDialog();
		if (ImGui::MenuItem("Iteration limit...")) OpenIterationLimitDialog();
		if (ImGui::MenuItem("Transformation...")) OpenTransformationDialog();
		ImGui::Separator();
		if (ImGui::MenuItem("Increase color density", "A")) {
			Global::ItDiv = std::max(8.0_sr, Global::ItDiv / 2);
			if (Global::PreModulo && Global::ItDiv * Global::MaxPreModMultiplier < Global::ItMod) {
				Global::ItMod = Global::ItDiv * Global::DefaultPreModMultiplier;
				FContext.InvalidatePixel();
			}
		}
		if (ImGui::MenuItem("Decrease color density", "S")) {
			Global::ItDiv = std::min(0x1p48_sr, Global::ItDiv * 2);
			if (Global::PreModulo && Global::ItDiv > Global::ItMod) {
				Global::ItMod = Global::ItDiv * Global::DefaultPreModMultiplier;
				FContext.InvalidatePixel();
			}
		}
		if (ImGui::MenuItem("Increase iteration limit", "D")) {
			Global::ItLim = std::min(UINT64_C(1) << 48, Global::ItLim * 2);
			FContext.ParameterChanged = true;
		}
		if (ImGui::MenuItem("Decrease iteration limit", "F")) {
			Global::ItLim = std::max(UINT64_C(8), Global::ItLim / 2);
			FContext.ParameterChanged = true;
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Reset location")) ResetLocation();

		if (FContext.Feature && ImGui::MenuItem("Feature finder...")) UI.ShowFeature = true;
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Computation")) {
		if (ImGui::BeginMenu("Algorithm")) {
			bool CanChange = CurrentFractalType == FractalTypeEnum::Mandelbrot;
			if (ImGui::MenuItem("Perturbation", nullptr, !UseLinearApproximation, CanChange)) {
				UseLinearApproximation = false;
				FContext.ChangeFractalType(CurrentFractalType, false);
			}
			if (ImGui::MenuItem("Linear approximation", nullptr, UseLinearApproximation, CanChange)) {
				UseLinearApproximation = true;
				FContext.ChangeFractalType(CurrentFractalType, true);
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Recompute")) {
			if (ImGui::MenuItem("Reference")) FContext.InvalidateReference();
			if (ImGui::MenuItem("Pixel")) FContext.InvalidatePixel();
			if (ImGui::MenuItem("All")) FContext.InvalidateAll();
			ImGui::EndMenu();
		}
		ImGui::MenuItem("Lock reference", nullptr, &FContext.LockReference);
		ImGui::MenuItem("Tasks...", nullptr, &UI.ShowTasks);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Image")) {
		if (ImGui::MenuItem("Image size...")) OpenImageSizeDialog();
		ImGui::MenuItem("Bilinear filter", nullptr, &Global::UseBilinearFilter);
		if (ImGui::MenuItem("Pre-modulo (workaround for precision loss)", nullptr, &Global::PreModulo)) FContext.InvalidatePixel();
		if (ImGui::MenuItem("Palette mipmaps", nullptr, &UsePalleteMipmaps)) EnablePaletteMipmap(UsePalleteMipmaps);
		ImGui::MenuItem("Gamma correction", nullptr, &Global::GammaCorrection);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Help")) {
		if (ImGui::MenuItem("About / License...")) {
			if (!UI.LicenceLoaded) {
				std::ifstream File(IMAGINA_LICENSE_PATH);
				std::stringstream Contents;
				Contents << File.rdbuf();
				UI.Licence = Contents.str();
				UI.LicenceLoaded = true;
			}
			UI.ShowAbout = true;
		}
		ImGui::EndMenu();
	}

	ImGui::EndMainMenuBar();
}

// --- dialog windows ---------------------------------------------------------
void DrawDialogs() {
	if (UI.ShowImageSize) {
		ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_Appearing);
		if (ImGui::Begin("Image size", &UI.ShowImageSize, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Checkbox("Follow window size", &UI.ImageFollowWindow);
			ImGui::InputInt("Width", &UI.ImageWidth);
			ImGui::InputInt("Height", &UI.ImageHeight);
			ImGui::Checkbox("Set window size", &UI.ImageSetWindowSize);
			ImGui::Separator();
			if (ImGui::Button("Apply", ImVec2(100, 0))) {
				int Width = std::max(16, UI.ImageWidth), Height = std::max(16, UI.ImageHeight);
				Global::ImageSizeFollowWindowSize = UI.ImageFollowWindow;
				FContext.SetResolution(Width, Height);
				UI.ImageWidth = Width;
				UI.ImageHeight = Height;
				Global::Redraw = true;
				if (UI.ImageSetWindowSize) SDL_SetWindowSize(Window, Width, Height);
			}
			ImGui::SameLine();
			if (ImGui::Button("Close", ImVec2(100, 0))) UI.ShowImageSize = false;
		}
		ImGui::End();
	}

	if (UI.ShowIterationLimit) {
		if (ImGui::Begin("Iteration limit", &UI.ShowIterationLimit, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::InputInt("Iterations", &UI.IterationLimit);
			if (ImGui::Button("Apply", ImVec2(100, 0))) {
				Global::ItLim = std::max(UINT64_C(2), uint64_t(std::max(2, UI.IterationLimit)));
				FContext.ParameterChanged = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Close", ImVec2(100, 0))) UI.ShowIterationLimit = false;
		}
		ImGui::End();
	}

	if (UI.ShowLocation) {
		ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_Appearing);
		if (ImGui::Begin("Location", &UI.ShowLocation, ImGuiWindowFlags_AlwaysAutoResize)) {
			static const char *ZoomTypes[] = { "Height", "Half height", "Magnification", "Depth" };
			if (ImGui::Combo("Unit", &UI.ZoomTypeIndex, ZoomTypes, 4)) {
				UI.LocationZoom = ZoomToString(ZoomType(UI.ZoomTypeIndex), FContext.CurrentLocation.HalfH, 2.0_hr);
			}
			ImGui::InputText("Real", &UI.LocationReal);
			ImGui::InputText("Imaginary", &UI.LocationImaginary);
			ImGui::InputText("Zoom", &UI.LocationZoom);
			ImGui::InputInt("Iterations", &UI.LocationIterations);
			ImGui::Separator();
			if (ImGui::Button("Apply", ImVec2(100, 0))) {
				try {
					mpf_class PrevX(FContext.CenterCoordinate.X), PrevY(FContext.CenterCoordinate.Y);
					try {
						HRReal HalfH = ParseZoom(ZoomType(UI.ZoomTypeIndex), UI.LocationZoom, 2.0_hr);
						if (HalfH > 16.0_hr) HalfH = 16.0_hr;
						int64_t LowestBit = std::min<int64_t>(0, HalfH.Exponent);
						uint64_t Precision = uint64_t(-LowestBit + 64);

						FContext.CenterCoordinate.X = mpf_class(UI.LocationReal.data(), Precision);
						FContext.CenterCoordinate.Y = mpf_class(UI.LocationImaginary.data(), Precision);

						Global::ItLim = std::max(UINT64_C(2), uint64_t(std::max(2, UI.LocationIterations)));
						UI.LocationIterations = int(Global::ItLim);

						FContext.CurrentLocation.HalfH = HalfH;
						FContext.CurrentLocation.X = 0.0_hr;
						FContext.CurrentLocation.Y = 0.0_hr;
						FContext.RenderLocation = FContext.CurrentLocation;
						FContext.EvalLocation = FContext.CurrentLocation;
						FContext.InvalidateAll();
						SetDefaultPrecision(Precision);
						UI.Error.clear();
					} catch (...) {
						FContext.CenterCoordinate.X = std::move(PrevX);
						FContext.CenterCoordinate.Y = std::move(PrevY);
						throw;
					}
				} catch (const std::exception &) {
					UI.Error = "Value invalid.";
				} catch (...) {
					UI.Error = "Value invalid.";
				}
			}
			if (!UI.Error.empty()) {
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", UI.Error.c_str());
			}
		}
		ImGui::End();
	}

	if (UI.ShowTransformation) {
		if (ImGui::Begin("Transformation", &UI.ShowTransformation, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Checkbox("Flip imaginary axis", &UI.FlipImaginary);
			ImGui::InputFloat("Rotation", &UI.Rotation);
			ImGui::InputFloat("Stretch angle", &UI.StretchAngle);
			ImGui::InputFloat("Stretch ratio", &UI.StretchRatio);
			ImGui::Separator();
			if (ImGui::Button("Apply", ImVec2(100, 0))) {
				if (UI.StretchRatio == 0.0f) {
					UI.Error = "Value invalid.";
				} else {
					UI.Error.clear();
					Global::FlipImaginaryAxis = UI.FlipImaginary;
					Global::Rotation = SRReal(UI.Rotation);
					Global::StretchAngle = SRReal(UI.StretchAngle);
					Global::StretchRatio = SRReal(UI.StretchRatio);

					if (!Global::FlipImaginaryAxis && Global::Rotation == 0.0 && Global::StretchRatio == 1.0) {
						Global::TransformMatrix = glm::dmat2(1.0);
						Global::InvTransformMatrix = glm::dmat2(1.0);
						Global::Transform = false;
					} else {
						Global::Transform = true;
						SRReal sinRotation = sin(glm::radians(Global::Rotation));
						SRReal cosRotation = cos(glm::radians(Global::Rotation));
						SRReal sinStretchAngle = sin(glm::radians(Global::StretchAngle));
						SRReal cosStretchAngle = cos(glm::radians(Global::StretchAngle));

						Global::TransformMatrix = glm::dmat2(cosStretchAngle, sinStretchAngle, -sinStretchAngle, cosStretchAngle);
						Global::TransformMatrix *= glm::dmat2(Global::StretchRatio, 0.0, 0.0, 1.0);
						Global::TransformMatrix *= glm::dmat2(cosStretchAngle, -sinStretchAngle, sinStretchAngle, cosStretchAngle);
						Global::TransformMatrix *= glm::dmat2(cosRotation, sinRotation, -sinRotation, cosRotation);
						Global::TransformMatrix *= glm::dmat2(1.0, 0.0, 0.0, Global::FlipImaginaryAxis ? -1.0 : 1.0);
						Global::InvTransformMatrix = glm::inverse(Global::TransformMatrix);
					}
					FContext.InvalidatePixel();
				}
			}
			if (!UI.Error.empty()) {
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", UI.Error.c_str());
			}
		}
		ImGui::End();
	}

	if (UI.ShowTasks) {
		if (ImGui::Begin("Tasks", &UI.ShowTasks, ImGuiWindowFlags_AlwaysAutoResize)) {
			SRReal Numerator = 0, Denominator = 1;
			auto &Manager = FContext.pixelManager;
			if (Manager.GetProgress(Numerator, Denominator) && Denominator > 0) {
				ImGui::ProgressBar(float(Numerator / Denominator), ImVec2(220, 0));
			}
			ImGui::Text("Reference: %s", FContext.reference ? "ready" : "not computed");
			ImGui::Text("Pixels: %s", Manager.Completed() ? "complete" : "computing");
			ImGui::Text("Threads: %zu", size_t(FContext.ThreadCount));
			if (ImGui::Button("Cancel")) FContext.CancelAll();
		}
		ImGui::End();
	}

	if (UI.ShowFeature && FContext.Feature) {
		if (ImGui::Begin("Feature", &UI.ShowFeature, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextUnformatted(Narrow(FContext.Feature->Name()).c_str());
			ImGui::Separator();
			ImGui::TextUnformatted(Narrow(FContext.Feature->Information()).c_str());
			ImGui::Separator();
			if (ImGui::Button("Go to feature")) {
				HRReal X, Y;
				FContext.Feature->GetCoordinate(X, Y);
				FContext.SetLocation({ X, Y }, 128, FContext.CurrentLocation.HalfH / 8);
				UI.ShowFeature = false;
			}
		}
		ImGui::End();
	}

	if (UI.ShowAbout) {
		ImGui::SetNextWindowSize(ImVec2(700, 600), ImGuiCond_Appearing);
		if (ImGui::Begin("About Imagina", &UI.ShowAbout)) {
			ImGui::TextUnformatted("Imagina - fast fractal renderer & explorer");
			ImGui::TextUnformatted("Modified version (2026): zoom sequence export with lossless raw map files.");
			ImGui::Separator();
			ImGui::TextUnformatted("This build uses the SDL3 + Dear ImGui front-end.");
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::BeginChild("Licence", ImVec2(0, 0), ImGuiChildFlags_Borders);
			ImGui::TextUnformatted(UI.Licence.c_str());
			ImGui::EndChild();
		}
		ImGui::End();
	}
}

// --- window/input -----------------------------------------------------------
void ResizeWindow(int32_t Width, int32_t Height) {
	WindowWidth = size_t(Width);
	WindowHeight = size_t(Height);
	ResizeViewport(Width, Height);
	if (Global::ImageSizeFollowWindowSize) FContext.SetResolution(Width, Height);
	Global::Redraw = true;
}

bool HandleEvent(const SDL_Event &Event) {
	ImGuiIO &IO = ImGui::GetIO();

	switch (Event.type) {
		case SDL_EVENT_QUIT:
			return false;

		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
			ResizeWindow(Event.window.data1, Event.window.data2);
			break;

		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			if (Event.button.button == SDL_BUTTON_LEFT && !IO.WantCaptureMouse) {
				MouseX = int32_t(Event.button.x * IO.DisplayFramebufferScale.x);
				MouseY = int32_t(Event.button.y * IO.DisplayFramebufferScale.y);
				Panning = true;
			}
			break;

		case SDL_EVENT_MOUSE_BUTTON_UP:
			if (Event.button.button == SDL_BUTTON_LEFT) Panning = false;
			break;

		case SDL_EVENT_MOUSE_MOTION: {
			int32_t X = int32_t(Event.motion.x * IO.DisplayFramebufferScale.x);
			int32_t Y = int32_t(Event.motion.y * IO.DisplayFramebufferScale.y);
			if (!(Event.motion.state & SDL_BUTTON_LMASK)) Panning = false;
			if (Panning) {
				double MovementX = -double(X - MouseX) / WindowHeight * 2.0;
				double MovementY = double(Y - MouseY) / WindowHeight * 2.0;

				double FractalMovementX = Global::InvTransformMatrix[0][0] * MovementX + Global::InvTransformMatrix[1][0] * MovementY;
				double FractalMovementY = Global::InvTransformMatrix[0][1] * MovementX + Global::InvTransformMatrix[1][1] * MovementY;

				Global::moved = true;
				FContext.Move(FractalMovementX, FractalMovementY);
			}
			MouseX = X;
			MouseY = Y;
			Global::Redraw = true;
			break;
		}

		case SDL_EVENT_MOUSE_WHEEL: {
			if (IO.WantCaptureMouse) break;
			float CursorX = Event.wheel.mouse_x;
			float CursorY = Event.wheel.mouse_y;
			SRReal X = (SRReal(CursorX * IO.DisplayFramebufferScale.x) - WindowWidth * 0.5) / WindowHeight * 2.0;
			SRReal Y = -(SRReal(CursorY * IO.DisplayFramebufferScale.y) / WindowHeight * 2.0 - 1.0);

			SRReal FractalX = Global::InvTransformMatrix[0][0] * X + Global::InvTransformMatrix[1][0] * Y;
			SRReal FractalY = Global::InvTransformMatrix[0][1] * X + Global::InvTransformMatrix[1][1] * Y;

			if (Event.wheel.y > 0) FContext.ZoomIn(FractalX, FractalY);
			else if (Event.wheel.y < 0) FContext.ZoomOut(FractalX, FractalY);
			break;
		}

		case SDL_EVENT_KEY_DOWN: {
			if (IO.WantCaptureKeyboard) break;
			bool Control = (Event.key.mod & SDL_KMOD_CTRL) != 0;
			if (Control) {
				if (Event.key.key == SDLK_S) { /* save: not available in this front-end yet */ }
				if (Event.key.key == SDLK_O) { /* open: not available in this front-end yet */ }
			} else {
				switch (Event.key.key) {
					case SDLK_A: Global::ItDiv = std::max(8.0_sr, Global::ItDiv / 2); Global::Redraw = true; break;
					case SDLK_S: Global::ItDiv = std::min(0x1p48_sr, Global::ItDiv * 2); Global::Redraw = true; break;
					case SDLK_E: Global::ColorCyclingSpeed = -0.1; Global::Redraw = true; break;
					case SDLK_R: Global::ColorCyclingSpeed = 0.1; Global::Redraw = true; break;
					case SDLK_D: Global::ItLim = std::min(UINT64_C(1) << 48, Global::ItLim * 2); FContext.ParameterChanged = true; break;
					case SDLK_F: Global::ItLim = std::max(UINT64_C(8), Global::ItLim / 2); FContext.ParameterChanged = true; break;
					default: break;
				}
			}
			break;
		}

		case SDL_EVENT_KEY_UP:
			if (Event.key.key == SDLK_E || Event.key.key == SDLK_R) {
				Global::ColorCyclingSpeed = 0.0;
				Global::Redraw = true;
			}
			break;

		default:
			break;
	}
	return true;
}

} // namespace

// --frames <n>      quit after n frames (0 = run until the window is closed)
// --screenshot <f>  write the last frame to <f> as a BMP (used for testing)
int main(int argc, char **argv) {
	int RunFrames = 0;
	const char *ScreenshotPath = nullptr;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--frames") && i + 1 < argc) RunFrames = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) ScreenshotPath = argv[++i];
	}

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		return 1;
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

	Window = SDL_CreateWindow("Imagina", int(InitialWindowWidth), int(InitialWindowHeight),
		SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
	if (!Window) {
		fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
		return 1;
	}

	GLContext = SDL_GL_CreateContext(Window);
	if (!GLContext) {
		fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
		return 1;
	}
	SDL_GL_MakeCurrent(Window, GLContext);
	SDL_GL_SetSwapInterval(1);

	int PixelWidth = 0, PixelHeight = 0;
	SDL_GetWindowSizeInPixels(Window, &PixelWidth, &PixelHeight);
	WindowWidth = InitialWindowWidth = size_t(PixelWidth);
	WindowHeight = InitialWindowHeight = size_t(PixelHeight);

	InitRenderResources();
	Computation::Init();

	FContext.CurrentLocation = { 0.0, 0.0, 2.0 };
	FContext.RenderLocation = { 0.0, 0.0, 2.0 };
	FContext.EvalLocation = { 0.0, 0.0, 2.0 };
	FContext.ImageWidth = InitialWindowWidth;
	FContext.ImageHeight = InitialWindowHeight;
	Global::Initialized = true;

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &IO = ImGui::GetIO();
	IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	ImGui::StyleColorsDark();
	ImGui_ImplSDL3_InitForOpenGL(Window, GLContext);
	ImGui_ImplOpenGL3_Init("#version 150");

	bool Running = true;
	long Frame = 0;
	while (Running) {
		ImGui_ImplSDL3_NewFrame();
		ImGui_ImplOpenGL3_NewFrame();
		ImGui::NewFrame();

		SDL_Event Event;
		while (SDL_PollEvent(&Event)) {
			ImGui_ImplSDL3_ProcessEvent(&Event);
			Running = HandleEvent(Event) && Running;
			if (Event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && Event.button.button == SDL_BUTTON_LEFT &&
				Event.button.clicks == 2 && !ImGui::GetIO().WantCaptureMouse && FContext.Feature) {
				UI.ShowFeature = true;
			}
		}
		if (!Running) break;

		FContext.Update();

		BeginRender();
		RenderFractal(FContext);

		SRReal X = (SRReal(MouseX) - WindowWidth * 0.5) / WindowHeight * 2.0;
		SRReal Y = -(SRReal(MouseY) / WindowHeight * 2.0 - 1.0);
		SRReal FractalX = Global::InvTransformMatrix[0][0] * X + Global::InvTransformMatrix[1][0] * Y;
		SRReal FractalY = Global::InvTransformMatrix[0][1] * X + Global::InvTransformMatrix[1][1] * Y;

		FContext.FindFeature(FractalX, FractalY);
		if (FContext.Feature) {
			HRReal FeatureX, FeatureY;
			FContext.Feature->GetCoordinate(FeatureX, FeatureY);

			FeatureX = (FeatureX - FContext.RenderLocation.X) / FContext.RenderLocation.HalfH;
			FeatureY = (FeatureY - FContext.RenderLocation.Y) / FContext.RenderLocation.HalfH;

			HRReal FeatureScreenX = Global::TransformMatrix[0][0] * FeatureX + Global::TransformMatrix[1][0] * FeatureY;
			HRReal FeatureScreenY = Global::TransformMatrix[0][1] * FeatureX + Global::TransformMatrix[1][1] * FeatureY;

			RenderLineToFeature(X, Y, (double)FeatureScreenX, (double)FeatureScreenY);
		}

		Global::Redraw = false;
		if (Global::ColorCyclingSpeed != 0.0) Global::Redraw = true;

		DrawMenuBar();
		DrawDialogs();

		ImGui::Render();
		SDL_GetWindowSizeInPixels(Window, &PixelWidth, &PixelHeight);
		glViewport(0, 0, PixelWidth, PixelHeight);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		if (ScreenshotPath && RunFrames > 0 && Frame + 1 >= RunFrames) {
			std::vector<uint8_t> Pixels(size_t(PixelWidth) * PixelHeight * 4);
			glReadPixels(0, 0, PixelWidth, PixelHeight, GL_RGBA, GL_UNSIGNED_BYTE, Pixels.data());
			SDL_Surface *Surface = SDL_CreateSurfaceFrom(PixelWidth, PixelHeight, SDL_PIXELFORMAT_RGBA32, Pixels.data(), PixelWidth * 4);
			if (Surface) {
				// OpenGL's origin is the bottom left corner.
				SDL_FlipSurface(Surface, SDL_FLIP_VERTICAL);
				if (!SDL_SaveBMP(Surface, ScreenshotPath)) { fprintf(stderr, "cannot write %s: %s", ScreenshotPath, SDL_GetError()); fputc(10, stderr); }
				SDL_DestroySurface(Surface);
			}
			Running = false;
		}

		EndRender();
		Frame++;
		if (RunFrames > 0 && Frame >= RunFrames) Running = false;
	}

	FContext.CancelAll();
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();
	SDL_GL_DestroyContext(GLContext);
	SDL_DestroyWindow(Window);
	SDL_Quit();
	return 0;
}
