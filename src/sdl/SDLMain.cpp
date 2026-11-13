// SDL3 + Dear ImGui front-end.
//
// This replaces the Win32 window, menu and dialog code of the Windows build so
// that the same user interface runs on Linux, macOS and Windows.  Everything
// below the front-end (Computation, FractalContext, PixelManager, Render) is
// shared with the Win32 build; the front-end only creates the window and the
// OpenGL context, feeds input into FractalContext and draws the UI.

#include "Includes.h"
#include "File.h"
#include "Language.h"
#include "WindowIcon.h"
#include "ZoomExport.h"
#include "RawMapCodec.h"

#include <SDL3/SDL.h>
#ifndef _WIN32
#include <dlfcn.h>
#endif
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

bool UseDE = true;
bool UseLinearApproximation = true;

static bool UsePalleteMipmaps = false;
static bool Panning = false;

static SDL_Window *Window = nullptr;
static SDL_GLContext GLContext = nullptr;

// --- platform hooks ---------------------------------------------------------
#ifdef _WIN32
#include <windows.h>
#endif

void *PlatformGetGLProcAddress(const char *Name) {
	void *Address = (void *)SDL_GL_GetProcAddress(Name);
	if (!Address) {
		// SDL resolves through EGL/GLX, which does not necessarily return the
		// core entry points; the GL library we link against does.
#ifdef _WIN32
		static HMODULE OpenGL = LoadLibraryA("opengl32.dll");
		if (OpenGL) Address = (void *)GetProcAddress(OpenGL, Name);
#else
		Address = dlsym(RTLD_DEFAULT, Name);
		if (getenv("IMAGINA_GL_DEBUG")) {
			fprintf(stderr, "gl proc %s: SDL=%p dlsym=%p", Name, (void *)SDL_GL_GetProcAddress(Name), Address);
			fputc(10, stderr);
		}
#endif
	}
	return Address;
}

void PlatformPresent() {
	SDL_GL_SwapWindow(Window);
}

void PlatformKeepDisplayOn(bool KeepOn) {
	// The screen saver and the display sleep would otherwise stop the export from
	// being watched; SDL does not touch the DPMS state by itself.
	if (KeepOn) SDL_DisableScreenSaver();
	else SDL_EnableScreenSaver();
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

// --- file dialogs -----------------------------------------------------------
struct FileDialogResult {
	bool Done = false;
	std::string Path;
	int Filter = -1;
};

void FileDialogCallback(void *UserData, const char *const *FileList, int Filter) {
	auto *Result = static_cast<FileDialogResult *>(UserData);
	Result->Done = true;
	Result->Filter = Filter;
	if (FileList && FileList[0]) Result->Path = FileList[0];
}

// Filters are the Win32 style "Description(*.ext)|*.ext|..." pairs.
bool ShowFileDialog(bool Save, const char *Filters, std::string &Path, int &FilterIndex) {
	std::vector<std::string> Parts;
	std::string Current;
	for (const char *Character = Filters;; Character++) {
		if (*Character == '|' || *Character == 0) {
			Parts.push_back(Current);
			Current.clear();
			if (!*Character) break;
		} else {
			Current.push_back(*Character);
		}
	}

	std::vector<SDL_DialogFileFilter> SdlFilters;
	SdlFilters.reserve(Parts.size() / 2);
	for (size_t i = 0; i + 1 < Parts.size(); i += 2) {
		SdlFilters.push_back({ Parts[i].c_str(), Parts[i + 1].c_str() });
	}

	FileDialogResult Result;
	if (Save) {
		SDL_ShowSaveFileDialog(FileDialogCallback, &Result, Window, SdlFilters.data(), int(SdlFilters.size()), nullptr);
	} else {
		SDL_ShowOpenFileDialog(FileDialogCallback, &Result, Window, SdlFilters.data(), int(SdlFilters.size()), nullptr, false);
	}

	// The callback is invoked while events are being processed.
	while (!Result.Done) {
		SDL_PumpEvents();
		SDL_Delay(10);
	}

	if (Result.Path.empty()) return false;
	Path = Result.Path;
	FilterIndex = Result.Filter >= 0 ? Result.Filter + 1 : 1;
	return true;
}

// --- small helpers ----------------------------------------------------------
std::string Narrow(const std::wstring_view Text) {
	// FeatureFinder builds wide strings; the UI draws UTF-8, and SDL converts
	// between the two without depending on the C locale.
	if (Text.empty()) return std::string();
	size_t Size = Text.size() * sizeof(wchar_t);
	char *Converted = SDL_iconv_string("UTF-8", "WCHAR_T", reinterpret_cast<const char *>(Text.data()), Size + sizeof(wchar_t));
	if (!Converted) return std::string(Text.begin(), Text.end());
	std::string Result(Converted);
	SDL_free(Converted);
	return Result;
}

// The system locale as SDL reports it; the core does not depend on SDL, so this
// stays in the front-end.
Language DetectLanguageFromSdl() {
	int Count = 0;
	SDL_Locale **Locales = SDL_GetPreferredLocales(&Count);
	Language Result = Language::English;
	if (Locales) {
		for (int i = 0; i < Count && Result == Language::English; i++) {
			if (!Locales[i] || !Locales[i]->language) continue;
			Language Candidate;
			if (LanguageFromCode(Locales[i]->language, Candidate)) Result = Candidate;
			if (Result == Language::English && Locales[i]->country) {
				std::string Code = std::string(Locales[i]->language) + "-" + Locales[i]->country;
				if (LanguageFromCode(Code, Candidate)) Result = Candidate;
			}
		}
		SDL_free(Locales);
	}
	return Result;
}

// The CJK font from fonts/ (see fonts/README.md).  Dear ImGui's built-in font has
// no CJK glyphs, so the UI falls back to it with a warning when the file is
// missing instead of refusing to start.
bool LoadFont(ImGuiIO &IO, float Size) {
	const char *Name = "SarasaMonoSC-Regular.ttf";
	std::vector<std::filesystem::path> Candidates;

	const char *Base = SDL_GetBasePath();
	if (Base) {
		std::filesystem::path Directory(Base);
		Candidates.push_back(Directory / Name);
		Candidates.push_back(Directory / "fonts" / Name);
		// SDL owns the string SDL_GetBasePath() returns (it is cached and freed
		// by SDL_Quit), so it must not be released here.
	}
	Candidates.push_back(std::filesystem::path("fonts") / Name);
	Candidates.push_back(std::filesystem::path(Name));

	for (const std::filesystem::path &Candidate : Candidates) {
		std::error_code Error;
		if (!std::filesystem::exists(Candidate, Error)) continue;
		ImFontConfig Config;
		Config.OversampleH = 2;
		Config.OversampleV = 1;
		if (IO.Fonts->AddFontFromFileTTF(Candidate.string().c_str(), Size, &Config)) {
			fprintf(stderr, "using the font %s", Candidate.string().c_str());
			fputc(10, stderr);
			return true;
		}
	}

	fprintf(stderr, "font %s not found, CJK text will not render (see fonts/README.md)", Name);
	fputc(10, stderr);
	IO.Fonts->AddFontDefault();
	return false;
}

std::string FormatMpf(const mpf_class &Value, int Digits, char Format = 'f') {
	std::string Buffer(size_t(Digits) + 64, '\0');
	gmp_sprintf(Buffer.data(), ("%." + std::to_string(Digits) + "F" + Format).c_str(), Value.get_mpf_t());
	Buffer.resize(Buffer.find('\0'));
	return Buffer;
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
	bool ShowExport = false;

	// Zoom sequence export; the session itself lives in the core (ZoomExport.h).
	std::string ExportPath;
	int ExportWidth = 640, ExportHeight = 360;
	float ExportStep = 2.0f;
	int ExportTargetType = 2;      // ZoomType::Magnification
	std::string ExportTargetText = "2";
	std::string ExportError;

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

bool ZoomExportActive() {
	return Session.Active;
}

void OpenExportDialog() {
	if (!Session.Active) {
		UI.ExportWidth = int(FContext.ImageWidth);
		UI.ExportHeight = int(FContext.ImageHeight);
		UI.ExportTargetType = int(Session.TargetType);
		UI.ExportTargetText = Session.TargetText;
		UI.ExportError.clear();
	}
	UI.ShowExport = true;
}

void StartExport() {
	if (UI.ExportPath.empty()) {
		UI.ExportError = "Choose the output file (*.imz) first.";
		return;
	}
	std::string Error;
	if (!ZoomExportPrepareNew(std::filesystem::path(UI.ExportPath), size_t(std::max(UI.ExportWidth, 4)),
			size_t(std::max(UI.ExportHeight, 4)), double(UI.ExportStep), ZoomType(UI.ExportTargetType), UI.ExportTargetText, Error)) {
		UI.ExportError = Error;
		return;
	}
	UI.ExportError.clear();
	ZoomExportCaptureView();
	ZoomExportBegin();
}

void ResumeExport() {
	std::string Path;
	int FilterIndex = 1;
	if (!PlatformOpenFileDialog("Resume zoom sequence", "Imagina export record(*.imz)|*.imz|All Files(*.*)|*.*|", Path, FilterIndex)) return;
	std::string Error;
	if (!ZoomExportPrepareResume(std::filesystem::path(Path), Error)) {
		UI.ExportError = Error;
		UI.ShowExport = true;
		return;
	}
	UI.ExportPath = Path;
	UI.ShowExport = true;
	UI.ExportError.clear();
	ZoomExportBegin();
}

void OpenTransformationDialog() {
	UI.FlipImaginary = Global::FlipImaginaryAxis;
	UI.Rotation = float(Global::Rotation);
	UI.StretchAngle = float(Global::StretchAngle);
	UI.StretchRatio = float(Global::StretchRatio);
	UI.ShowTransformation = true;
}

} // namespace

bool PlatformOpenFileDialog(const char *Title, const char *Filters, std::string &Path, int &FilterIndex) {
	(void)Title; // SDL uses the platform's default dialog title
	return ShowFileDialog(false, Filters, Path, FilterIndex);
}

bool PlatformSaveFileDialog(const char *Title, const char *Filters, std::string &Path, int &FilterIndex) {
	(void)Title;
	return ShowFileDialog(true, Filters, Path, FilterIndex);
}

void SetFractalType(FractalTypeEnum Type) {
	CurrentFractalType = Type;
	FContext.ChangeFractalType(Type, UseLinearApproximation && Type == FractalTypeEnum::Mandelbrot);
}

namespace {

void ResetLocation() {
	FContext.ResetLocation();
}

static const char *OpenFilters =
	"All supported formats(*.im;*.imt;*.kfr;*.kfp)|*.im;*.imt;*.kfr;*.kfp|"
	"Imagina(*.im)|*.im|"
	"Imagina text(*.imt)|*.imt|"
	"Kalle's fraktaler(*.kfr)|*.kfr|"
	"Kalle's fraktaler palette(*.kfp)|*.kfp|"
	"All Files(*.*)|*.*|";

static const char *SaveFilters =
	"Imagina(*.im)|*.im|"
	"Imagina text(*.imt)|*.imt|"
	"Kalle's fraktaler(*.kfr)|*.kfr|";

void OpenFilePrompt() {
	std::string Path;
	int FilterIndex = 1;
	if (!PlatformOpenFileDialog("Open", OpenFilters, Path, FilterIndex)) return;
	OpenFile(std::filesystem::path(Path));
}

void SaveFilePrompt() {
	std::string Path;
	int FilterIndex = 1;
	if (!PlatformSaveFileDialog("Save", SaveFilters, Path, FilterIndex)) return;

	std::filesystem::path FileName(Path);
	if (!FileName.has_extension()) {
		const char *Extensions[] = { ".im", ".imt", ".kfr" };
		if (FilterIndex >= 1 && FilterIndex <= 3) FileName += Extensions[FilterIndex - 1];
	}

	FileType Type = FilterIndex == 2 ? FileType::ImaginaText : FilterIndex == 3 ? FileType::Kfr : FileType::Imagina;
	SaveFile(FileName, Type);
}

void SaveImagePrompt(bool Raw) {
	if (!FContext.pixelManager.Completed()) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, Tr("Save image"), Tr("Please wait for computations to finish."), Window);
		return;
	}

	std::string Path;
	int FilterIndex = 1;
	const char *Filters = Raw ? "All Files(*.*)|*.*|" : "PNG(*.png)|*.png|All Files(*.*)|*.*|";
	if (!PlatformSaveFileDialog(Raw ? "Save raw pixel data" : "Save image", Filters, Path, FilterIndex)) return;

	std::filesystem::path FileName(Path);
	if (!Raw && !FileName.has_extension()) FileName += ".png";

	if (Raw) SaveRawPixelData(FileName);
	else SaveImage(FileName);
}

// --- menu bar ---------------------------------------------------------------
void DrawMenuBar() {
	if (!ImGui::BeginMainMenuBar()) return;

	if (ImGui::BeginMenu(Tr("File"))) {
		if (ImGui::MenuItem(Tr("Open..."), "Ctrl+O")) OpenFilePrompt();
		if (ImGui::MenuItem(Tr("Save..."), "Ctrl+S")) SaveFilePrompt();
		if (ImGui::MenuItem(Tr("Save image..."), "Ctrl+Shift+S")) SaveImagePrompt(false);
		if (ImGui::MenuItem(Tr("Save raw pixel data..."))) SaveImagePrompt(true);
		ImGui::Separator();
		if (ImGui::MenuItem(Tr("Export zoom sequence..."), nullptr, false, !ZoomExportActive())) OpenExportDialog();
		if (ImGui::MenuItem(Tr("Resume zoom sequence..."), nullptr, false, !ZoomExportActive())) ResumeExport();
		ImGui::Separator();
		if (ImGui::MenuItem(Tr("Quit"), "Ctrl+Q")) {
			SDL_Event Quit = {};
			Quit.type = SDL_EVENT_QUIT;
			SDL_PushEvent(&Quit);
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu(Tr("Fractal"))) {
		if (ImGui::BeginMenu(Tr("Formula"))) {
			if (ImGui::MenuItem(Tr("Mandelbrot"), nullptr, CurrentFractalType == FractalTypeEnum::Mandelbrot)) SetFractalType(FractalTypeEnum::Mandelbrot);
			if (ImGui::MenuItem(Tr("Tricorn"), nullptr, CurrentFractalType == FractalTypeEnum::Tricorn)) SetFractalType(FractalTypeEnum::Tricorn);
			if (ImGui::MenuItem(Tr("Burning ship"), nullptr, CurrentFractalType == FractalTypeEnum::BurningShip)) SetFractalType(FractalTypeEnum::BurningShip);
			if (ImGui::MenuItem(Tr("Nova"), nullptr, CurrentFractalType == FractalTypeEnum::Nova)) SetFractalType(FractalTypeEnum::Nova);
			ImGui::EndMenu();
		}

		bool DistanceEstimation = UseDE;
		bool SupportsDE = CurrentFractalType == FractalTypeEnum::Nova ||
			(CurrentFractalType == FractalTypeEnum::Mandelbrot && UseLinearApproximation);
		if (ImGui::MenuItem(Tr("Distance estimation"), nullptr, &DistanceEstimation, SupportsDE)) {
			UseDE = DistanceEstimation;
			FContext.SetUseDE(UseDE);
		}

		bool HighQuality = Global::HighQuality;
		bool SupportsHQ = CurrentFractalType == FractalTypeEnum::Mandelbrot && UseLinearApproximation;
		if (ImGui::MenuItem(Tr("High quality (Experimental)"), nullptr, &HighQuality, SupportsHQ)) {
			Global::HighQuality = HighQuality;
			FContext.InvalidatePixel();
		}

		ImGui::Separator();
		if (ImGui::MenuItem(Tr("Location..."))) OpenLocationDialog();
		if (ImGui::MenuItem(Tr("Iteration limit..."))) OpenIterationLimitDialog();
		if (ImGui::MenuItem(Tr("Transformation..."))) OpenTransformationDialog();
		ImGui::Separator();
		if (ImGui::MenuItem(Tr("Increase color density"), "A")) {
			Global::ItDiv = std::max(8.0_sr, Global::ItDiv / 2);
			if (Global::PreModulo && Global::ItDiv * Global::MaxPreModMultiplier < Global::ItMod) {
				Global::ItMod = Global::ItDiv * Global::DefaultPreModMultiplier;
				FContext.InvalidatePixel();
			}
		}
		if (ImGui::MenuItem(Tr("Decrease color density"), "S")) {
			Global::ItDiv = std::min(0x1p48_sr, Global::ItDiv * 2);
			if (Global::PreModulo && Global::ItDiv > Global::ItMod) {
				Global::ItMod = Global::ItDiv * Global::DefaultPreModMultiplier;
				FContext.InvalidatePixel();
			}
		}
		if (ImGui::MenuItem(Tr("Increase iteration limit"), "D")) {
			Global::ItLim = std::min(UINT64_C(1) << 48, Global::ItLim * 2);
			FContext.ParameterChanged = true;
		}
		if (ImGui::MenuItem(Tr("Decrease iteration limit"), "F")) {
			Global::ItLim = std::max(UINT64_C(8), Global::ItLim / 2);
			FContext.ParameterChanged = true;
		}
		ImGui::Separator();
		if (ImGui::MenuItem(Tr("Reset location"))) ResetLocation();

		if (FContext.Feature && ImGui::MenuItem(Tr("Feature finder..."))) UI.ShowFeature = true;
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu(Tr("Computation"))) {
		if (ImGui::BeginMenu(Tr("Algorithm"))) {
			bool CanChange = CurrentFractalType == FractalTypeEnum::Mandelbrot;
			if (ImGui::MenuItem(Tr("Perturbation"), nullptr, !UseLinearApproximation, CanChange)) {
				UseLinearApproximation = false;
				FContext.ChangeFractalType(CurrentFractalType, false);
			}
			if (ImGui::MenuItem(Tr("Linear approximation"), nullptr, UseLinearApproximation, CanChange)) {
				UseLinearApproximation = true;
				FContext.ChangeFractalType(CurrentFractalType, true);
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu(Tr("Recompute"))) {
			if (ImGui::MenuItem(Tr("Reference"))) FContext.InvalidateReference();
			if (ImGui::MenuItem(Tr("Pixel"))) FContext.InvalidatePixel();
			if (ImGui::MenuItem(Tr("All"))) FContext.InvalidateAll();
			ImGui::EndMenu();
		}
		ImGui::MenuItem(Tr("Lock reference"), nullptr, &FContext.LockReference);
		ImGui::MenuItem(Tr("Tasks..."), nullptr, &UI.ShowTasks);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu(Tr("Image"))) {
		if (ImGui::MenuItem(Tr("Image size..."))) OpenImageSizeDialog();
		ImGui::MenuItem(Tr("Bilinear filter"), nullptr, &Global::UseBilinearFilter);
		if (ImGui::MenuItem(Tr("Pre-modulo (workaround for precision loss)"), nullptr, &Global::PreModulo)) FContext.InvalidatePixel();
		if (ImGui::MenuItem(Tr("Palette mipmaps"), nullptr, &UsePalleteMipmaps)) EnablePaletteMipmap(UsePalleteMipmaps);
		ImGui::MenuItem(Tr("Gamma correction"), nullptr, &Global::GammaCorrection);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu(Tr("Help"))) {
		if (ImGui::MenuItem(Tr("About / License..."))) {
			if (!UI.LicenceLoaded) {
				// An installed copy carries LICENSE.txt next to the executable; a
				// build tree falls back to the file the configure step found.
				std::stringstream Contents;
				bool Loaded = false;
				if (const char *Base = SDL_GetBasePath()) {
					std::ifstream File(std::filesystem::path(Base) / "LICENSE.txt");
					if (File) {
						Contents << File.rdbuf();
						Loaded = true;
					}
				}
				if (!Loaded) {
					std::ifstream File(IMAGINA_LICENSE_PATH);
					Contents << File.rdbuf();
				}
				UI.Licence = Contents.str();
				UI.LicenceLoaded = true;
			}
			UI.ShowAbout = true;
		}
		ImGui::EndMenu();
	}

	// Its own menu bar entry, after Help.
	if (ImGui::BeginMenu(Tr("Language"))) {
		if (ImGui::MenuItem(Tr("English"), nullptr, CurrentLanguage() == Language::English)) SetLanguage(Language::English);
		if (ImGui::MenuItem(Tr("Chinese (Simplified)"), nullptr, CurrentLanguage() == Language::Chinese)) SetLanguage(Language::Chinese);
		ImGui::EndMenu();
	}

	ImGui::EndMainMenuBar();
}

// --- dialog windows ---------------------------------------------------------
void DrawDialogs() {
	if (UI.ShowImageSize) {
		ImGui::SetNextWindowPos(ImVec2(40, 80), ImGuiCond_Appearing);
		ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_Appearing);
		if (ImGui::Begin(Tr("Image size"), &UI.ShowImageSize, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Checkbox(Tr("Follow window size"), &UI.ImageFollowWindow);
			ImGui::InputInt(Tr("Width"), &UI.ImageWidth);
			ImGui::InputInt(Tr("Height"), &UI.ImageHeight);
			ImGui::Checkbox(Tr("Set window size"), &UI.ImageSetWindowSize);
			ImGui::Separator();
			if (ImGui::Button(Tr("Apply"), ImVec2(100, 0))) {
				int Width = std::max(16, UI.ImageWidth), Height = std::max(16, UI.ImageHeight);
				Global::ImageSizeFollowWindowSize = UI.ImageFollowWindow;
				FContext.SetResolution(Width, Height);
				UI.ImageWidth = Width;
				UI.ImageHeight = Height;
				Global::Redraw = true;
				if (UI.ImageSetWindowSize) SDL_SetWindowSize(Window, Width, Height);
			}
			ImGui::SameLine();
			if (ImGui::Button(Tr("Close"), ImVec2(100, 0))) UI.ShowImageSize = false;
		}
		ImGui::End();
	}

	if (UI.ShowIterationLimit) {
		ImGui::SetNextWindowPos(ImVec2(400, 80), ImGuiCond_Appearing);
		if (ImGui::Begin(Tr("Iteration limit"), &UI.ShowIterationLimit, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::InputInt(Tr("Iterations"), &UI.IterationLimit);
			if (ImGui::Button(Tr("Apply"), ImVec2(100, 0))) {
				Global::ItLim = std::max(UINT64_C(2), uint64_t(std::max(2, UI.IterationLimit)));
				FContext.ParameterChanged = true;
			}
			ImGui::SameLine();
			if (ImGui::Button(Tr("Close"), ImVec2(100, 0))) UI.ShowIterationLimit = false;
		}
		ImGui::End();
	}

	if (UI.ShowLocation) {
		ImGui::SetNextWindowPos(ImVec2(40, 270), ImGuiCond_Appearing);
		ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_Appearing);
		if (ImGui::Begin(Tr("Location"), &UI.ShowLocation, ImGuiWindowFlags_AlwaysAutoResize)) {
			const char *ZoomTypes[] = { Tr("Height"), Tr("Half height"), Tr("Magnification"), Tr("Depth") };
			if (ImGui::Combo(Tr("Unit"), &UI.ZoomTypeIndex, ZoomTypes, 4)) {
				UI.LocationZoom = ZoomToString(ZoomType(UI.ZoomTypeIndex), FContext.CurrentLocation.HalfH, 2.0_hr);
			}
			ImGui::InputText(Tr("Real"), &UI.LocationReal);
			ImGui::InputText(Tr("Imaginary"), &UI.LocationImaginary);
			ImGui::InputText(Tr("Zoom"), &UI.LocationZoom);
			ImGui::InputInt(Tr("Iterations"), &UI.LocationIterations);
			ImGui::Separator();
			if (ImGui::Button(Tr("Apply"), ImVec2(100, 0))) {
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
					UI.Error = Tr("Value invalid.");
				} catch (...) {
					UI.Error = Tr("Value invalid.");
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
		ImGui::SetNextWindowPos(ImVec2(560, 270), ImGuiCond_Appearing);
		if (ImGui::Begin(Tr("Transformation"), &UI.ShowTransformation, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Checkbox(Tr("Flip imaginary axis"), &UI.FlipImaginary);
			ImGui::InputFloat(Tr("Rotation"), &UI.Rotation);
			ImGui::InputFloat(Tr("Stretch angle"), &UI.StretchAngle);
			ImGui::InputFloat(Tr("Stretch ratio"), &UI.StretchRatio);
			ImGui::Separator();
			if (ImGui::Button(Tr("Apply"), ImVec2(100, 0))) {
				if (UI.StretchRatio == 0.0f) {
					UI.Error = Tr("Value invalid.");
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
		ImGui::SetNextWindowPos(ImVec2(40, 520), ImGuiCond_Appearing);
		if (ImGui::Begin(Tr("Tasks"), &UI.ShowTasks, ImGuiWindowFlags_AlwaysAutoResize)) {
			SRReal Numerator = 0, Denominator = 1;
			auto &Manager = FContext.pixelManager;
			if (Manager.GetProgress(Numerator, Denominator) && Denominator > 0) {
				ImGui::ProgressBar(float(Numerator / Denominator), ImVec2(220, 0));
			}
			ImGui::Text("%s: %s", Tr("Reference"), Tr(FContext.reference ? "ready" : "not computed"));
			ImGui::Text("%s: %s", Tr("Pixels"), Tr(Manager.Completed() ? "complete" : "computing"));
			ImGui::Text("%s: %zu", Tr("Threads"), size_t(FContext.ThreadCount));
			if (ImGui::Button(Tr("Cancel"))) FContext.CancelAll();
		}
		ImGui::End();
	}

	if (UI.ShowExport) {
		ImGui::SetNextWindowPos(ImVec2(40, 600), ImGuiCond_Appearing);
		ImGui::SetNextWindowSize(ImVec2(620, 0), ImGuiCond_Appearing);
		if (ImGui::Begin(Tr("Export zoom sequence"), &UI.ShowExport, ImGuiWindowFlags_AlwaysAutoResize)) {
			if (Session.Active || Session.Finished) {
				SRReal Fraction = 0.0;
				std::string Status = ZoomExportStatus(Fraction);
				ImGui::ProgressBar(float(Fraction), ImVec2(-1, 0));
				ImGui::TextUnformatted(Status.c_str());
				if (!Session.Error.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", Session.Error.c_str());
				if (Session.Active) {
					if (ImGui::Button(Tr("Cancel"), ImVec2(120, 0))) ZoomExportEnd();
				} else if (Session.Finished) {
					ImGui::Text("%s: %zu / %zu %s", Tr("Finished"), Session.DoneCount, Session.Frames.size(), Tr("key frames"));
				}
				if (ImGui::Button(Tr("Close"), ImVec2(120, 0))) UI.ShowExport = false;
			} else {
				ImGui::InputText(Tr("Record"), &UI.ExportPath);
				ImGui::SameLine();
				if (ImGui::Button(Tr("Browse..."))) {
					std::string Path;
					int FilterIndex = 1;
					if (PlatformSaveFileDialog("Export zoom sequence", "Imagina export record(*.imz)|*.imz|", Path, FilterIndex)) {
						std::filesystem::path Chosen(Path);
						if (!Chosen.has_extension()) Chosen += ".imz";
						UI.ExportPath = Chosen.string();
					}
				}
				ImGui::InputInt(Tr("Width"), &UI.ExportWidth);
				ImGui::InputInt(Tr("Height"), &UI.ExportHeight);
				ImGui::InputFloat(Tr("Step"), &UI.ExportStep, 0.1f, 1.0f, "%.3f");
				const char *TargetTypes[] = { Tr("Height"), Tr("Half height"), Tr("Magnification"), Tr("Depth") };
				ImGui::Combo(Tr("Unit"), &UI.ExportTargetType, TargetTypes, 4);
				ImGui::InputText(Tr("Zoom"), &UI.ExportTargetText);
				if (!UI.ExportError.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", UI.ExportError.c_str());
				ImGui::Separator();
				if (ImGui::Button(Tr("Start"), ImVec2(120, 0))) StartExport();
				ImGui::SameLine();
				if (ImGui::Button(Tr("Close"), ImVec2(120, 0))) UI.ShowExport = false;
			}
		}
		ImGui::End();
	}

	if (UI.ShowFeature && FContext.Feature) {
		if (ImGui::Begin(Tr("Feature"), &UI.ShowFeature, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextUnformatted(Narrow(FContext.Feature->Name()).c_str());
			ImGui::Separator();
			ImGui::TextUnformatted(Narrow(FContext.Feature->Information()).c_str());
			ImGui::Separator();
			if (ImGui::Button(Tr("Go to feature"))) {
				HRReal X, Y;
				FContext.Feature->GetCoordinate(X, Y);
				FContext.SetLocation({ X, Y }, 128, FContext.CurrentLocation.HalfH / 8);
				UI.ShowFeature = false;
			}
		}
		ImGui::End();
	}

	if (UI.ShowAbout) {
		ImGui::SetNextWindowPos(ImVec2(820, 80), ImGuiCond_Appearing);
		ImGui::SetNextWindowSize(ImVec2(740, 500), ImGuiCond_Appearing);
		if (ImGui::Begin(Tr("About Imagina"), &UI.ShowAbout)) {
			ImGui::TextUnformatted(Tr("Imagina - fast fractal renderer & explorer"));
			ImGui::TextUnformatted("Modified version (2026): zoom sequence export with lossless raw map files.");
			ImGui::Separator();
			ImGui::TextUnformatted("Source of the original program:  https://github.com/5E-324/Imagina");
			ImGui::TextUnformatted("Source of this version:          https://github.com/haveyouwantto/Imagina");
			ImGui::TextUnformatted("This fork (SDL3 front-end, zoom export): https://github.com/MarchBeta2087/Imagina");
			ImGui::Separator();
			ImGui::TextUnformatted(Tr("This build uses the SDL3 + Dear ImGui front-end."));
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::BeginChild("Licence", ImVec2(0, 0), ImGuiChildFlags_Borders);
			ImGui::TextUnformatted(UI.Licence.c_str());
			ImGui::EndChild();
		}
		ImGui::End();
	}
}

// --- crash reporting --------------------------------------------------------
#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>

static LONG WINAPI CrashHandler(EXCEPTION_POINTERS *Info) {
	EXCEPTION_RECORD *Record = Info->ExceptionRecord;
	uintptr_t Address = uintptr_t(Record->ExceptionAddress);
	HMODULE Module = nullptr;
	char ModuleName[MAX_PATH] = "?";
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR)Address, &Module)) {
		GetModuleFileNameA(Module, ModuleName, sizeof(ModuleName));
	}

	fprintf(stderr, "crash: code %08lx at %p (%s+0x%llx)", (unsigned long)Record->ExceptionCode,
		(void *)Address, ModuleName, (unsigned long long)(Address - uintptr_t(Module)));
	fputc(10, stderr);

	CONTEXT *Context = Info->ContextRecord;
	fprintf(stderr, "  rsp=%p rbp=%p rip=%p", (void *)Context->Rsp, (void *)Context->Rbp, (void *)Context->Rip);
	fputc(10, stderr);

	if (Record->NumberParameters >= 2) {
		uintptr_t FaultAddress = uintptr_t(Record->ExceptionInformation[1]);
		MEMORY_BASIC_INFORMATION Info2 = {};
		if (VirtualQuery((LPCVOID)FaultAddress, &Info2, sizeof(Info2))) {
			fprintf(stderr, "  fault at %p state=0x%lx protect=0x%lx type=0x%lx base=%p size=0x%llx", (void *)FaultAddress,
				(unsigned long)Info2.State, (unsigned long)Info2.Protect, (unsigned long)Info2.Type,
				Info2.BaseAddress, (unsigned long long)Info2.RegionSize);
			fputc(10, stderr);
		}
		NT_TIB *Teb = (NT_TIB *)NtCurrentTeb();
		fprintf(stderr, "  stack base=%p limit=%p fault=%p (%s)", (void *)Teb->StackBase, (void *)Teb->StackLimit,
			(void *)FaultAddress, FaultAddress < uintptr_t(Teb->StackLimit) ? "below stack limit" : "inside stack");
		fputc(10, stderr);
	}

	// Walk the stack of the faulting thread; the addresses can be resolved with
	// addr2line against the executable.
	HANDLE Process = GetCurrentProcess();
	HANDLE Thread = GetCurrentThread();
	SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
	if (SymInitialize(Process, nullptr, TRUE)) {
		CONTEXT Context = *Info->ContextRecord;
		STACKFRAME64 Frame = {};
		Frame.AddrPC.Offset = Context.Rip;
		Frame.AddrPC.Mode = AddrModeFlat;
		Frame.AddrFrame.Offset = Context.Rbp;
		Frame.AddrFrame.Mode = AddrModeFlat;
		Frame.AddrStack.Offset = Context.Rsp;
		Frame.AddrStack.Mode = AddrModeFlat;

		for (int i = 0; i < 24; i++) {
			if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, Process, Thread, &Frame, &Context, nullptr,
					SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
				break;
			}
			if (!Frame.AddrPC.Offset) break;

			char SymbolBuffer[sizeof(SYMBOL_INFO) + 256] = {};
			SYMBOL_INFO *Symbol = (SYMBOL_INFO *)SymbolBuffer;
			Symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
			Symbol->MaxNameLen = 255;
			DWORD64 Displacement = 0;
			const char *Name = "<unknown>";
			if (SymFromAddr(Process, Frame.AddrPC.Offset, &Displacement, Symbol)) Name = Symbol->Name;

			IMAGEHLP_LINE64 Line = {};
			Line.SizeOfStruct = sizeof(Line);
			DWORD LineDisplacement = 0;
			if (SymGetLineFromAddr64(Process, Frame.AddrPC.Offset, &LineDisplacement, &Line)) {
				fprintf(stderr, "  #%02d %s+0x%llx  %s:%lu", i, Name, (unsigned long long)Displacement, Line.FileName, (unsigned long)Line.LineNumber);
			} else {
				fprintf(stderr, "  #%02d %s+0x%llx  %p", i, Name, (unsigned long long)Displacement, (void *)Frame.AddrPC.Offset);
			}
			fputc(10, stderr);
		}
		SymCleanup(Process);
	}
	fflush(stderr);
	return EXCEPTION_EXECUTE_HANDLER;
}
#endif

// --- window/input -----------------------------------------------------------
void ResizeWindow(int32_t Width, int32_t Height) {
	WindowWidth = size_t(Width);
	WindowHeight = size_t(Height);
	ResizeViewport(Width, Height);
	// A running export owns the resolution (it sets the size of the key frames), so
	// following the window would change the size of the frames mid-export.
	if (Global::ImageSizeFollowWindowSize && !Session.Active) FContext.SetResolution(Width, Height);
	Global::Redraw = true;
}

// The ImGui font is 18 pixels at 100%, and the display scale multiplies it.
constexpr float BaseFontSize = 18.0f;
float AppScale = 1.0f;

// Rebuilds the font atlas after the window moved to a display with another scale.
void ReloadFonts(float Scale) {
	ImGuiIO &IO = ImGui::GetIO();
	ImGui_ImplOpenGL3_DestroyDeviceObjects();
	IO.Fonts->Clear();
	LoadFont(IO, BaseFontSize * Scale);
	ImGui::GetStyle().ScaleAllSizes(Scale / AppScale);
	AppScale = Scale;
	ImGui_ImplOpenGL3_CreateDeviceObjects();
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

		case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: {
			float Scale = SDL_GetWindowDisplayScale(Window);
			if (Scale > 0.0f && Scale != AppScale) ReloadFonts(Scale);
			break;
		}

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
			bool Shift = (Event.key.mod & SDL_KMOD_SHIFT) != 0;
			if (Control) {
				if (Event.key.key == SDLK_O) OpenFilePrompt();
				if (Event.key.key == SDLK_S) {
					if (Shift) SaveImagePrompt(false);
					else SaveFilePrompt();
				}
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
// Imagina --decode <frame.rawz> [<frame.raw>]
// The same command the Win32 front-end has: expand a compressed raw map into the
// plain layout "Save raw pixel data" writes, without opening a window (so it
// works on a machine without a display and from scripts).
static int DecodeRawMap(int argc, char **argv) {
	if (argc < 3) {
		fprintf(stderr, "usage: Imagina --decode <frame.rawz> [<frame.raw>]\n");
		return 1;
	}

	std::filesystem::path Input(argv[2]);
	std::filesystem::path Output = argc >= 4 ? std::filesystem::path(argv[3]) : Input;
	if (argc < 4) Output.replace_extension(".raw");

	std::vector<float> Values;
	uint32_t Width = 0, Height = 0;
	std::string Error;
	if (!RawMapCodec::ReadFile(Input.wstring(), Values, Width, Height, Error)) {
		fprintf(stderr, "cannot expand %s: %s\n", Input.string().c_str(), Error.c_str());
		return 1;
	}
	if (!RawMapCodec::WriteRawFile(Output.wstring(), Values, Width, Height, Error)) {
		fprintf(stderr, "cannot write %s: %s\n", Output.string().c_str(), Error.c_str());
		return 2;
	}
	fprintf(stderr, "%s: %u x %u float32 values written to %s\n", Input.string().c_str(), Width, Height,
		Output.string().c_str());
	return 0;
}

// Imagina --period <record.imz> [<out.txt>]
// Writes the iteration limit every key frame of an export record would be
// computed with, which is the same command the Win32 front-end has.
static int PrintPeriod(int argc, char **argv) {
	if (argc < 3) {
		fprintf(stderr, "usage: Imagina --period <record.imz> [<out.txt>]");
		fputc(10, stderr);
		return 1;
	}
	std::filesystem::path Record(argv[2]);
	std::filesystem::path Out = argc >= 4 ? std::filesystem::path(argv[3]) : Record;
	if (argc < 4) Out += ".period.txt";
	PrintFrameIterations(Record, Out);
	fprintf(stderr, "%s: iteration limits written to %s", Record.string().c_str(), Out.string().c_str());
	fputc(10, stderr);
	return 0;
}

int main(int argc, char **argv) {
	const char *ExportPath = nullptr;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--zoom-export") && i + 1 < argc) ExportPath = argv[++i];
	}

	if (argc >= 2 && !strcmp(argv[1], "--decode")) return DecodeRawMap(argc, argv);
	if (argc >= 2 && !strcmp(argv[1], "--period")) return PrintPeriod(argc, argv);
	if (argc >= 2 && (!strcmp(argv[1], "--version") || !strcmp(argv[1], "-v"))) {
		printf("Imagina %s (SDL front-end)", IMAGINA_VERSION);
		fputc(10, stdout);
		return 0;
	}

	int RunFrames = 0;
	const char *ScreenshotPath = nullptr;
	const char *OpenPath = nullptr;
	const char *SavePath = nullptr;
	bool ShowAllWindows = false;
	bool Stress = false;
	bool LanguageGiven = false;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--frames") && i + 1 < argc) RunFrames = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) ScreenshotPath = argv[++i];
		else if (!strcmp(argv[i], "--open") && i + 1 < argc) OpenPath = argv[++i];
		else if (!strcmp(argv[i], "--save") && i + 1 < argc) SavePath = argv[++i];
		else if (!strcmp(argv[i], "--windows")) ShowAllWindows = true;
		else if (!strcmp(argv[i], "--stress")) Stress = true;
		else if (!strcmp(argv[i], "--lang") && i + 1 < argc) {
			Language Chosen;
			if (!LanguageFromCode(argv[++i], Chosen)) {
				fprintf(stderr, "unknown language '%s' (use en or zh-CN)", argv[i]);
				fputc(10, stderr);
				return 2;
			}
			SetLanguage(Chosen);
			LanguageGiven = true;
		}
	}

#ifdef _WIN32
	if (getenv("IMAGINA_CRASH_REPORT")) SetUnhandledExceptionFilter(CrashHandler);
#endif

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

	// The window icon is compiled in (see tools/make-icon.py), so the program does
	// not have to find a data file next to the executable.
	{
		SDL_Surface *Icon = SDL_CreateSurfaceFrom(WindowIconWidth, WindowIconHeight,
			SDL_PIXELFORMAT_RGBA32, const_cast<uint8_t *>(WindowIconPixels), WindowIconWidth * 4);
		if (Icon) {
			SDL_SetWindowIcon(Window, Icon);
			SDL_DestroySurface(Icon);
		}
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

	if (ShowAllWindows) {
		// Opens every dialog, which lets a --screenshot run cover the UI code.
		OpenImageSizeDialog();
		OpenIterationLimitDialog();
		OpenLocationDialog();
		OpenTransformationDialog();
		UI.ShowTasks = true;
		UI.ShowAbout = true;
	}

	if (OpenPath) OpenFile(std::filesystem::path(OpenPath));

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &IO = ImGui::GetIO();
	IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	ImGui::StyleColorsDark();
	// HiDPI: the font is rasterised at the display scale and the style follows it,
	// otherwise the UI is half the size it should be on a scaled display.
	AppScale = SDL_GetWindowDisplayScale(Window);
	if (!(AppScale > 0.0f)) AppScale = 1.0f;
	ImGui::GetStyle().ScaleAllSizes(AppScale);
	if (!LanguageGiven) SetLanguage(DetectLanguageFromSdl());
	LoadFont(IO, BaseFontSize * AppScale);
	ImGui_ImplSDL3_InitForOpenGL(Window, GLContext);
	ImGui_ImplOpenGL3_Init("#version 150");

	if (ExportPath) {
		// Headless export with the dialog defaults, which is how CI exercises the
		// whole pipeline (export, record, key frames, --decode).
		std::string Error;
		if (!ZoomExportPrepareNew(std::filesystem::path(ExportPath), size_t(std::max(UI.ExportWidth, 4)),
				size_t(std::max(UI.ExportHeight, 4)), double(UI.ExportStep), ZoomType(UI.ExportTargetType), UI.ExportTargetText, Error)) {
			fprintf(stderr, "cannot start the export: %s", Error.c_str());
			fputc(10, stderr);
			return 1;
		}
		UI.ExportPath = ExportPath;
		ZoomExportCaptureView();
		ZoomExportBegin();
	}

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

		if (Stress) {
			// Exercises the cancel/recompute paths: zooms in and out and moves the
			// view, which invalidates the reference and the pixels over and over.
			uint32_t Hash = uint32_t(Frame) * 2654435761u;
			SRReal X = SRReal(Hash & 0xFFFF) / 65536.0 * 1.5 - 0.75;
			SRReal Y = SRReal((Hash >> 16) & 0xFFFF) / 65536.0 * 1.5 - 0.75;
			if ((Frame / 3) % 2 == 0) FContext.ZoomIn(X, Y);
			else FContext.ZoomOut(X, Y);
			if (Frame % 25 == 24) FContext.ResetLocation();
			Global::Redraw = true;
		}

		if (Session.Active) ZoomExportTick();
		else if (!ExportPath) FContext.Update();
		if (ExportPath && !Session.Active) Running = false;

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

		if (SavePath && FContext.pixelManager.Completed()) {
			// The extension decides what to write, which also makes the file
			// formats testable without a dialog.
			std::filesystem::path Output(SavePath);
			std::string Extension = Output.extension().string();
			for (char &Character : Extension) Character = char(tolower((unsigned char)Character));
			if (Extension == ".im") SaveFile(Output, FileType::Imagina);
			else if (Extension == ".imt") SaveFile(Output, FileType::ImaginaText);
			else if (Extension == ".kfr") SaveFile(Output, FileType::Kfr);
			else if (Extension == ".raw") SaveRawPixelData(Output);
			else SaveImage(Output);
			Running = false;
		}

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
	Computation::Shutdown();
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();
	SDL_GL_DestroyContext(GLContext);
	SDL_DestroyWindow(Window);
	SDL_Quit();
	return 0;
}
