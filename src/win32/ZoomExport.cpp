#include "Includes.h"
#include "MainWindow.h"
#include "ZoomExport.h"
#include "Localise.h"

#include "File.h"
#include "RawMapCodec.h"
#include "Resource.h"

#include <CommCtrl.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <cstdio>

// ---------------------------------------------------------------------------
// Zoom sequence export
//
// Exporting a zoom sequence writes one raw map file per key frame (a plain dump
// of the iteration values, exactly like "Save raw pixel data") while zooming
// into the current centre. The key frames are listed in an export record
// (*.imz) together with the location they were computed at, so an interrupted
// export can be continued later: key frames which are already recorded (and
// whose raw file is still on disk) are skipped.
//
// The computation is driven from the dialog timer instead of the main loop.
// Nothing in the export path depends on presenting the window or on
// SwapBuffers(), and the display is kept on with SetThreadExecutionState(), so
// switching the display off cannot stop the export or invalidate its data.
// ---------------------------------------------------------------------------

namespace {

constexpr UINT_PTR ExportTimer = 1;
constexpr UINT ExportTimerInterval = 20;

HWND ExportDialog = nullptr;

// Whether the export dialog starts a new sequence from the displayed view or
// continues the record which was chosen from the file menu.
enum class ExportDialogMode {
	New,
	Resume,
};
ExportDialogMode DialogMode = ExportDialogMode::New;

// The record "Resume zoom sequence" chose, shown and continued by the dialog.
std::wstring ResumeRecordPath;

// The view which was displayed before the export started.
struct SavedViewState {
	HPReal CenterX, CenterY;
	RelLocation Location;
	uint64_t Precision = 64;
	size_t ImageWidth = 0, ImageHeight = 0;
	size_t ItLim = 0;
	bool Valid = false;
};

SavedViewState SavedView;

// Settings are remembered between invocations of the dialog.
size_t DefaultWidth = 0, DefaultHeight = 0;
double DefaultStep = 2.0;
ZoomType DefaultTargetType = ZoomType::Depth;
std::string DefaultTargetText = "0";
std::wstring DefaultRecordPath;

// The depth of a view, log2(2 / half height).  It is derived from the exponent
// of the half height, which keeps it finite for any zoom level.

// The size of a view the way the location and zoom dialogs name it: a
// magnification of 2 / half height, which is easier to read than the depth.

// The key frames run from the current view outwards, so the first one is the
// deepest: it decides how precise the centre coordinate, the default precision
// of the arbitrary precision arithmetic and the reference orbit have to be.
// (Looking at the last frame instead would work with a precision which is far
// too low for the deep frames, and every pixel of them would come out as if it
// sat exactly on the reference point.)

// The key frames run from the current centre outwards, so the sequence ends at
// the frame whose depth (log2(2 / half height)) has come down to zero, that is
// at the view which shows the whole 2 x 2 fractal.  A frame which would be wider
// than that is not written any more, which is the same as stopping as soon as
// the depth of the next frame would be negative.

// ---------------------------------------------------------------------------
// Export record (*.imz)
// ---------------------------------------------------------------------------

// Returns false if the file does not contain a usable record. Settings and
// already exported frames are read from the file; frames whose raw file has
// disappeared are marked as pending again.

// ---------------------------------------------------------------------------
// Export driver
// ---------------------------------------------------------------------------

bool FrameComplete() {
	return FContext.pixelManager.Completed()
		&& !FContext.EvaluationTaskContext
		&& !FContext.ReferenceTaskContext
		&& !FContext.ComputePixel
		&& !FContext.RecomputeReference
		&& !FContext.ParameterChanged
		&& !FContext.EvaluationPending
		&& !FContext.LocationChanged
		&& !FContext.Zooming;
}

enum class ExportPhase {
	Reference,
	Pixels,
	Prepare,
};

// ---------------------------------------------------------------------------
// Iteration limit of a key frame
//
// The limit of the session is what the deepest key frame needs, but a key frame
// which is only a few times wider than that shows a much coarser structure and
// does not have to resolve it with the same number of iterations.  Leaving the
// limit alone is what makes the wide key frames crawl: the pixels which are
// inside the set run every iteration of the limit, and the linear approximation
// cannot skip them there because the period of a wide view is short.
//
// The period of a view is read from the orbit of the reference point, so it
// follows the scale the way the interactive zoom does (which gives a view 256
// iterations per period of its cycle, see Evaluator::Feature::ItLimForZoomLevel
// and MainWindow): it is the first iteration at which the orbit satisfies
//
//	2 * view radius * |dz/dc| > |z|,
//
// the test the reference generation uses to call a reference periodic, so a
// wider view matches it earlier (see HInfLAEvaluator, CalculateOrbit).
// ---------------------------------------------------------------------------

// Iterations the interactive zoom allows for one period of a cycle.
constexpr size_t IterationsPerPeriod = 256;

// A key frame is never given fewer iterations than this.  The limit is also the
// point where a pixel counts as inside the set, and a limit which is too tight
// would grow black patches which the neighbouring key frames do not have.
constexpr size_t MinFrameIterations = 1024;

// Longest orbit the period of a view is looked for in.  The walk is over the
// orbit of one point at the precision of the view, so it is bounded: a longer
// period would give a limit above the session limit anyway.
constexpr size_t MaxPeriodSearch = 1 << 16;

// Works out the iteration limit of every key frame in one pass over the orbit of
// the centre, and stores it in the frames.  Walking the orbit once per frame is
// what made an export with a deep location appear to hang: the walk is O(limit)
// high precision iterations, and a deep view has a long period (see
// HInfLAEvaluator, CalculateOrbit).  The period of a view shrinks as the view
// gets wider, so a single walk which records, for every iteration, the radius at
// which that iteration counts as the period is enough for the whole sequence;
// the result is written into the record (see WriteRecordFile) so that a
// continued export does not have to work it out again.

ExportPhase GetExportPhase(SRReal &Fraction) {
	if (FContext.ReferenceTaskContext) {
		Fraction = 0.0;
		return ExportPhase::Reference;
	}
	if (FContext.EvaluationTaskContext) {
		SRReal Numerator = 0.0, Denominator = 0.0;
		if (FContext.pixelManager.GetProgress(Numerator, Denominator) && Denominator > 0.0) {
			Fraction = std::clamp<SRReal>(Numerator / Denominator, 0.0, 1.0);
		} else {
			Fraction = 0.0;
		}
		return ExportPhase::Pixels;
	}
	Fraction = 0.0;
	return ExportPhase::Prepare;
}

void StartFrame(size_t Cursor) {
	Session.Cursor = Cursor;
	ExportFrame &Frame = Session.Frames[Cursor];

	// The iteration limit of a key frame follows its scale, so that the wide
	// frames do not spend the limit of the deepest one on the pixels which are
	// inside the set.  The limits of the whole sequence are worked out in one
	// pass, see ComputeFrameIterations.
	Global::ItLim = Frame.Iterations;

	// Move the view the way the interactive zoom does: the centre stays where it
	// is and only the visible size changes, so the reference orbit which the
	// application already computed for this centre is reused instead of being
	// computed again for every key frame (Update() still recomputes it when a
	// frame really needs a more precise orbit, for example when the export was
	// resumed at a different location).  The frame is drawn from a clean pixel
	// state, so its content does not depend on the frames before it.
	RelLocation View;
	View.HalfH = Frame.HalfH;
	View.X = HRReal(Session.CenterX - HPReal(FContext.CenterCoordinate.X));
	View.Y = HRReal(Session.CenterY - HPReal(FContext.CenterCoordinate.Y));
	FContext.CurrentLocation = View;
	FContext.RenderLocation = View;
	FContext.EvalLocation = View;
	FContext.ComputePixel = true;

	FContext.pixelManager.CurrentLocation = RelLocation{};
	FContext.pixelManager.ResetPassLayout();

	Session.FrameStart = std::chrono::steady_clock::now();
}

void RestoreView() {
	if (!SavedView.Valid) return;

	FContext.CancelPixelComputation();
	SetDefaultPrecision(size_t(SavedView.Precision));

	// Back to the view which was displayed before the export, again without
	// invalidating the reference: it is the orbit of the centre which the
	// export used, so it is still the right one for this view.
	FContext.CenterCoordinate.X = SavedView.CenterX;
	FContext.CenterCoordinate.Y = SavedView.CenterY;
	FContext.CurrentLocation = SavedView.Location;
	FContext.RenderLocation = SavedView.Location;
	FContext.EvalLocation = SavedView.Location;
	FContext.ComputePixel = true;
	Global::ItLim = SavedView.ItLim;
	FContext.SetResolution(int32_t(SavedView.ImageWidth), int32_t(SavedView.ImageHeight));
	Global::Redraw = true;
	SavedView.Valid = false;
}

void EndExport() {
	Session.Active = false;
	if (ExportDialog) KillTimer(ExportDialog, ExportTimer);
	SetThreadExecutionState(ES_CONTINUOUS);
	Global::SuppressTextureUpdate = false;
	Global::DisablePixelReuse = false;
	RestoreView();
}

void SaveCurrentFrame() {
	const ExportFrame &Frame = Session.Frames[Session.Cursor];
	std::wstring Path = FramePath(Frame.Index);

	std::vector<float> Pixels;
	int32_t Width = 0, Height = 0;
	if (!GetRawPixelData(Pixels, Width, Height)) {
		Session.Active = false;
		MessageBoxW(ExportDialog, TrW("The pixels of the frame cannot be read.").c_str(), TrW("Export zoom sequence").c_str(),
			MB_OK | MB_ICONERROR | MB_TASKMODAL);
		return;
	}

	std::vector<uint8_t> Compressed;
	RawMapCodec::Compress(Pixels.data(), uint32_t(Width), uint32_t(Height), Compressed);
	{
		std::ofstream File(std::filesystem::path(Path), std::ofstream::out | std::ofstream::binary | std::ofstream::trunc);
		File.write(reinterpret_cast<const char *>(Compressed.data()), std::streamsize(Compressed.size()));
	}

	// The file is read back and expanded again: a key frame is only recorded when
	// the file really holds the pixels of this frame, which keeps the record (and
	// everything which renders the sequence later) from trusting a broken file.
	std::vector<float> Verified;
	uint32_t VerifiedWidth = 0, VerifiedHeight = 0;
	std::string Error;
	bool Valid = RawMapCodec::ReadFile(Path, Verified, VerifiedWidth, VerifiedHeight, Error)
		&& VerifiedWidth == uint32_t(Width) && VerifiedHeight == uint32_t(Height)
		&& Verified.size() == Pixels.size()
		&& memcmp(Verified.data(), Pixels.data(), Pixels.size() * sizeof(float)) == 0;

	if (!Valid) {
		Session.Active = false;
		if (Error.empty()) Error = "the stored pixels differ from the frame";
		MessageBoxW(ExportDialog,
			(L"Writing or reading back the compressed raw map failed:\n" + Path + L"\n\n"
				+ std::wstring(Error.begin(), Error.end()) + L"\n\nThe export has been stopped.").c_str(),
			L"Export zoom sequence", MB_OK | MB_ICONERROR | MB_TASKMODAL);
		return;
	}

	Session.Frames[Session.Cursor].Done = true;
	Session.DoneCount++;
	Session.LastFrameSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Session.FrameStart).count();
	Session.HaveTiming = true;
	WriteRecordFile();
}

void UpdateExportUI(HWND hWndDlg);
void SetControlsEnabled(HWND hWndDlg, bool Enabled);

// Called for every timer tick while an export is running.
void TickExport(HWND hWndDlg) {
	if (!Session.Active) return;

	// The frame is finished when all pending work has been carried out.
	if (FrameComplete()) {
		SaveCurrentFrame();
		if (!Session.Active) { EndExport(); UpdateExportUI(hWndDlg); return; }

		size_t Next = Session.Cursor + 1;
		while (Next < Session.Frames.size() && Session.Frames[Next].Done) Next++;

		if (Next >= Session.Frames.size()) {
			EndExport();
			SendMessage(GetDlgItem(hWndDlg, IDC_EXPORT_PROGRESS), PBM_SETPOS, 1000, 0);
			SetDlgItemTextA(hWndDlg, IDC_EXPORT_STATUS,
				("Finished: " + std::to_string(Session.DoneCount) + " of " + std::to_string(Session.Frames.size())
					+ " key frames.").c_str());
			SetWindowTextW(hWndDlg, L"Export zoom sequence");
			SetControlsEnabled(hWndDlg, true);
			return;
		}

		StartFrame(Next);
		UpdateExportUI(hWndDlg);
		return;
	}

	// Update() drives the reference and the pixel computation. It is cheap to
	// call once the workers are busy, so the timer ticks often enough to keep
	// the progress display alive.
	for (int i = 0; i < 4 && !FrameComplete(); i++) FContext.Update();
	UpdateExportUI(hWndDlg);
}

std::string CurrentFrameStatus(SRReal &Fraction) {
	ExportPhase Phase = GetExportPhase(Fraction);
	const ExportFrame &Frame = Session.Frames[Session.Cursor];

	std::stringstream Stream;
	Stream << "Frame " << (Session.DoneCount + 1) << "/" << Session.Frames.size()
		<< ", " << MagnificationToString(Frame.HalfH) << " zooms"
		<< ", " << Frame.Iterations << " iterations\n";
	switch (Phase) {
		case ExportPhase::Reference: Stream << "reference orbit"; break;
		case ExportPhase::Pixels: {
			Stream << "pixels " << std::fixed << std::setprecision(0) << (Fraction * 100.0) << "%";
			break;
		}
		default: Stream << "starting"; break;
	}

	double Elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - Session.SessionStart).count();
	Stream << std::defaultfloat << ", " << FormatDuration(Elapsed);
	if (Session.HaveTiming && Session.DoneCount) {
		double PerFrame = Session.LastFrameSeconds;
		size_t Remaining = Session.Frames.size() - Session.DoneCount - 1;
		if (Remaining) Stream << ", ~" << FormatDuration(PerFrame * double(Remaining)) << " left";
	}
	return Stream.str();
}

// ---------------------------------------------------------------------------
// Dialog
// ---------------------------------------------------------------------------

std::wstring GetDlgItemText(HWND hWndDlg, int ID) {
	int Length = GetWindowTextLengthW(GetDlgItem(hWndDlg, ID));
	std::wstring Text(size_t(Length) + 1, L'\0');
	if (Length) GetDlgItemTextW(hWndDlg, ID, Text.data(), Length + 1);
	Text.resize(wcslen(Text.c_str()));
	return Text;
}

// Keeps a name short enough for the info area of the dialog.
std::string Shorten(const std::string &Text, size_t Maximum = 20) {
	if (Text.size() <= Maximum) return Text;
	return Text.substr(0, Maximum - 3) + "...";
}

std::string GetDlgItemTextA(HWND hWndDlg, int ID) {
	return ToUtf8(GetDlgItemText(hWndDlg, ID));
}

// Shows what the settings currently entered would export, without changing the
// session: used while no export record is loaded yet.
bool PreviewSequence(HWND hWndDlg, size_t &Count, HRReal &First, HRReal &Last) {
	BOOL Translated = FALSE;
	UINT Width = GetDlgItemInt(hWndDlg, IDC_EXPORT_WIDTH, &Translated, FALSE);
	if (!Translated || Width < MinExportSize || Width > 100000) return false;
	UINT Height = GetDlgItemInt(hWndDlg, IDC_EXPORT_HEIGHT, &Translated, FALSE);
	if (!Translated || Height < MinExportSize || Height > 100000) return false;

	double Step = strtod(GetDlgItemTextA(hWndDlg, IDC_EXPORT_STEP).c_str(), nullptr);
	if (!(Step > 1.0) || Step > 1e9) return false;

	HRReal TargetHalfH;
	try {
		ZoomType TargetType = ZoomType(int(SendMessage(GetDlgItem(hWndDlg, IDC_EXPORT_TARGET_TYPE), CB_GETCURSEL, 0, 0)));
		TargetHalfH = ParseZoom(TargetType, GetDlgItemTextA(hWndDlg, IDC_EXPORT_TARGET_VALUE), 2.0_hr);
	} catch (...) {
		return false;
	}
	if (!(TargetHalfH > 0.0_hr)) return false;

	std::vector<ExportFrame> Frames;
	if (!BuildFrames(FContext.CurrentLocation.HalfH, Step, TargetHalfH, Frames)) return false;
	Count = Frames.size();
	First = Frames.front().HalfH;
	Last = Frames.back().HalfH;
	return true;
}

void UpdateIdleUI(HWND hWndDlg) {
	std::stringstream Stream;

	size_t Width = size_t(GetDlgItemInt(hWndDlg, IDC_EXPORT_WIDTH, nullptr, FALSE));
	size_t Height = size_t(GetDlgItemInt(hWndDlg, IDC_EXPORT_HEIGHT, nullptr, FALSE));
	Stream << Width << " x " << Height << ", iteration limit " << Global::ItLim << "\n";

	if (Session.Frames.empty()) {
		size_t Count = 0;
		HRReal First, Last;
		if (PreviewSequence(hWndDlg, Count, First, Last)) {
			Stream << Count << (Count == 1 ? " key frame: " : " key frames outwards: ")
				<< MagnificationToString(First) << " to " << MagnificationToString(Last) << " zooms\n";
			// The record file which is entered right now decides the frame names.
			std::wstring Directory, FileBase;
			std::wstring RecordPath = GetDlgItemText(hWndDlg, IDC_EXPORT_FILE);
			SplitRecordPath(RecordPath, Directory, FileBase);
			std::string OutputName = FileNameOf(FileBase);
			if (OutputName.empty()) {
				Stream << "Choose the output file.";
			} else {
				Stream << "Output: " << Shorten(OutputName) << "-0000" << ToUtf8(RawMapCodec::FileExtension);
			}
			if (!RecordPath.empty() && FileExists(RecordPath)) {
				// The export only starts over: the record and its key frames are
				// replaced ("Resume zoom sequence" continues one instead).
				Stream << "\nThe record exists and is overwritten.";
			}
			if (Count < 2) {
				Stream << "\nThe current view is already that wide.";
			}
		} else {
			Stream << "Fill in resolution, zoom step and stop.";
		}
	} else if (!Session.DoneCount) {
		Stream << Session.Frames.size() << " key frames outwards: "
			<< MagnificationToString(Session.Frames.front().HalfH) << " to "
			<< MagnificationToString(Session.Frames.back().HalfH) << " zooms\n";
		Stream << "Output: " << Shorten(FileNameOf(Session.FileBase)) << "-0000" << ToUtf8(RawMapCodec::FileExtension) << " ...";
	} else {
		size_t Next = 0;
		while (Next < Session.Frames.size() && Session.Frames[Next].Done) Next++;
		Stream << Session.Frames.size() << " key frames, " << Session.DoneCount << " already exported\n";
		Stream << "Continuing at frame " << Next << " ("
			<< MagnificationToString(Session.Frames[std::min(Next, Session.Frames.size() - 1)].HalfH) << " zooms)";
	}

	SetDlgItemTextA(hWndDlg, IDC_EXPORT_INFO, Stream.str().c_str());
}

void UpdateExportUI(HWND hWndDlg) {
	if (Session.Frames.empty()) return;

	SRReal PhaseFraction = 0.0;
	std::string Status = CurrentFrameStatus(PhaseFraction);
	double Overall = (double(Session.DoneCount) + double(PhaseFraction)) / double(Session.Frames.size());
	SendMessage(GetDlgItem(hWndDlg, IDC_EXPORT_PROGRESS), PBM_SETPOS, WPARAM(std::clamp(Overall, 0.0, 1.0) * 1000.0), 0);
	SetDlgItemTextA(hWndDlg, IDC_EXPORT_STATUS, Status.c_str());
}

// Reads the record file named in the path edit field and updates the settings
// and the info text accordingly. Returns false if there is no usable record.
bool LoadRecordFromEdit(HWND hWndDlg) {
	std::wstring Path = GetDlgItemText(hWndDlg, IDC_EXPORT_FILE);
	if (Path.empty() || !FileExists(Path)) return false;

	Session.Active = false;
	Session.RecordPath = Path;
	SetRecordPath(Path);
	if (!ReadRecordFile()) return false;

	char Buffer[64];
	SetDlgItemInt(hWndDlg, IDC_EXPORT_WIDTH, UINT(Session.Width), FALSE);
	SetDlgItemInt(hWndDlg, IDC_EXPORT_HEIGHT, UINT(Session.Height), FALSE);
	sprintf_s(Buffer, "%.17g", Session.Step);
	SetDlgItemTextA(hWndDlg, IDC_EXPORT_STEP, Buffer);
	SendMessage(GetDlgItem(hWndDlg, IDC_EXPORT_TARGET_TYPE), CB_SETCURSEL, WPARAM(int(Session.TargetType)), 0);
	SetDlgItemTextA(hWndDlg, IDC_EXPORT_TARGET_VALUE, Session.TargetText.c_str());
	return true;
}

bool ParseSettings(HWND hWndDlg) {
	BOOL Translated = FALSE;
	UINT Width = GetDlgItemInt(hWndDlg, IDC_EXPORT_WIDTH, &Translated, FALSE);
	if (!Translated || Width < MinExportSize) {
		MessageBoxW(hWndDlg, TrW("Invalid width.").c_str(), TrW("Export zoom sequence").c_str(), MB_OK | MB_ICONERROR);
		return false;
	}
	UINT Height = GetDlgItemInt(hWndDlg, IDC_EXPORT_HEIGHT, &Translated, FALSE);
	if (!Translated || Height < MinExportSize) {
		MessageBoxW(hWndDlg, TrW("Invalid height.").c_str(), TrW("Export zoom sequence").c_str(), MB_OK | MB_ICONERROR);
		return false;
	}
	if (uint64_t(Width) * uint64_t(Height) > MaxExportPixels) {
		MessageBoxW(hWndDlg, TrW("The image size is too large (more than 100 megapixels).").c_str(), TrW("Export zoom sequence").c_str(), MB_OK | MB_ICONERROR);
		return false;
	}

	double Step = strtod(GetDlgItemTextA(hWndDlg, IDC_EXPORT_STEP).c_str(), nullptr);
	if (!(Step > 1.0) || Step > 1e9) {
		MessageBoxW(hWndDlg, TrW("Invalid zoom step (it has to be larger than 1).").c_str(), TrW("Export zoom sequence").c_str(), MB_OK | MB_ICONERROR);
		return false;
	}

	ZoomType TargetType = ZoomType(SendMessage(GetDlgItem(hWndDlg, IDC_EXPORT_TARGET_TYPE), CB_GETCURSEL, 0, 0));
	std::string TargetText = GetDlgItemTextA(hWndDlg, IDC_EXPORT_TARGET_VALUE);
	HRReal TargetValue;
	try {
		TargetValue = ParseZoom(TargetType, TargetText, 2.0_hr);
	} catch (...) {
		MessageBoxW(hWndDlg, TrW("Invalid zoom target.").c_str(), TrW("Export zoom sequence").c_str(), MB_OK | MB_ICONERROR);
		return false;
	}
	if (!(TargetValue > 0.0_hr)) {
		MessageBoxW(hWndDlg, TrW("Invalid zoom target.").c_str(), TrW("Export zoom sequence").c_str(), MB_OK | MB_ICONERROR);
		return false;
	}

	Session.Width = Width;
	Session.Height = Height;
	Session.Iterations = Global::ItLim;
	Session.Step = Step;
	Session.TargetType = TargetType;
	Session.TargetText = TargetText;
	Session.TargetValue = TargetValue;
	return true;
}

void CaptureView() {
	SavedView.CenterX = FContext.CenterCoordinate.X;
	SavedView.CenterY = FContext.CenterCoordinate.Y;
	SavedView.Location = FContext.CurrentLocation;
	SavedView.Precision = PrecisionForHalfH(FContext.CurrentLocation.HalfH);
	SavedView.Precision = std::max<uint64_t>(SavedView.Precision, uint64_t(FContext.CenterCoordinate.X.get_prec()));
	SavedView.ImageWidth = FContext.ImageWidth;
	SavedView.ImageHeight = FContext.ImageHeight;
	SavedView.ItLim = Global::ItLim;
	SavedView.Valid = true;
}

void SetControlsEnabled(HWND hWndDlg, bool Enabled) {
	for (int ID : { IDC_EXPORT_FILE, IDC_EXPORT_BROWSE, IDC_EXPORT_WIDTH, IDC_EXPORT_HEIGHT, IDC_EXPORT_STEP,
			IDC_EXPORT_TARGET_TYPE, IDC_EXPORT_TARGET_VALUE, IDC_EXPORT_START }) {
		EnableWindow(GetDlgItem(hWndDlg, ID), BOOL(Enabled));
	}
	SetDlgItemTextW(hWndDlg, IDCANCEL, Enabled ? L"Close" : L"Cancel");
}

// Sets up a new export from the settings in the dialog and the location which is
// displayed right now, and writes the location file which holds the coordinates.
// Returns false when the dialog asks something else first or the settings do not
// produce a sequence.
bool StartNewExport(HWND hWndDlg) {
	if (!ParseSettings(hWndDlg)) return false;

	std::wstring Path = GetDlgItemText(hWndDlg, IDC_EXPORT_FILE);
	if (Path.empty()) {
		MessageBoxW(hWndDlg, TrW("Choose the output file (*.imz) first.").c_str(), TrW("Export zoom sequence").c_str(), MB_OK | MB_ICONERROR);
		return false;
	}
	SetRecordPath(Path);

	Session.Cursor = 0;
	Session.DoneCount = 0;
	Session.Frames.clear();
	Session.HaveTiming = false;
	Session.LastFrameSeconds = 0.0;

	// A record which is already there can only be started over from here; the
	// "Resume zoom sequence" command of the file menu continues it instead.
	if (FileExists(Path)) {
		std::wstring Message = L"The file\n\n" + Path
			+ L"\n\nalready exists.\nExport all key frames again and overwrite it?\n\n"
			  L"To continue the export instead, use \"Resume zoom sequence\" in the file menu.";
		if (MessageBoxW(hWndDlg, Message.c_str(), TrW("Export zoom sequence").c_str(), MB_OKCANCEL | MB_ICONWARNING) != IDOK) {
			Session.Frames.clear();
			Session.DoneCount = 0;
			return false;
		}
	}

	// The location is the one which is displayed right now.
	Session.BaseHalfH = FContext.CurrentLocation.HalfH;
	if (!BuildFrames()) {
		MessageBoxW(hWndDlg, TrW("The selected stop condition does not produce any key frame.").c_str(), TrW("Export zoom sequence").c_str(), MB_OK | MB_ICONERROR);
		return false;
	}
	// The deepest key frame (the current view) needs the most precision, and the
	// default precision must not be lowered below what the application already
	// uses: the deltas of the deep pixels would disappear from the arithmetic and
	// every pixel would be the reference point.
	uint64_t Precision = std::max(PrecisionForHalfH(DeepestFrameHalfH(Session.Frames)),
		uint64_t(mpf_get_default_prec()));
	SetDefaultPrecision(size_t(Precision));
	Session.CenterX = HPReal(FContext.CenterCoordinate.X, Precision);
	Session.CenterX += HPReal(FContext.CurrentLocation.X);
	Session.CenterY = HPReal(FContext.CenterCoordinate.Y, Precision);
	Session.CenterY += HPReal(FContext.CurrentLocation.Y);
	// One pass over the orbit gives every key frame its iteration limit.
	ComputeFrameIterations();

	// The scene the export starts at -- the coordinates, the iteration limit, the
	// reference orbit -- is written into the location file the record names, with
	// the application's own save, so that a continued export can open it again
	// (see ReadRecordFile) and get exactly this state back.
	HPReal OffsetX = HPReal(Session.CenterX - FContext.CenterCoordinate.X) / HPReal(Session.BaseHalfH);
	HPReal OffsetY = HPReal(Session.CenterY - FContext.CenterCoordinate.Y) / HPReal(Session.BaseHalfH);
	Session.ViewOffsetX = mpf_get_d(OffsetX.get_mpf_t());
	Session.ViewOffsetY = mpf_get_d(OffsetY.get_mpf_t());
	{
		std::wstring LocationPath = LocationPathOf(Session.RecordPath);
		std::vector<wchar_t> Buffer(LocationPath.begin(), LocationPath.end());
		Buffer.push_back(0);
		SaveFile(std::filesystem::path(Buffer.data()), FileType::Imagina);
	}
	return true;
}

// Sets up the export described by the record which is entered in the dialog,
// including the location file it names.
bool ResumeExport(HWND hWndDlg) {
	std::wstring Path = GetDlgItemText(hWndDlg, IDC_EXPORT_FILE);
	if (Path.empty() || !FileExists(Path)) {
		MessageBoxW(hWndDlg, L"Choose the export record (*.imz) which is to be continued.",
			L"Resume zoom sequence", MB_OK | MB_ICONERROR);
		return false;
	}
	SetRecordPath(Path);
	// Opening the location file below changes the displayed view, so the view the
	// user is looking at is remembered first (EndExport puts it back).
	if (!SavedView.Valid) CaptureView();
	Session.Frames.clear();
	Session.DoneCount = 0;
	Session.HaveTiming = false;
	Session.LastFrameSeconds = 0.0;
	if (!ReadRecordFile()) {
		std::wstring Message = L"The record\n\n" + Path
			+ L"\n\nand the location file it names cannot be read.";
		MessageBoxW(hWndDlg, Message.c_str(), TrW("Resume zoom sequence").c_str(), MB_OK | MB_ICONERROR);
		return false;
	}
	// A record which does not list the iteration limits of its key frames (one
	// written before they were recorded) gets them worked out here, once for the
	// whole sequence.
	if (!Session.Frames.empty() && Session.Frames.back().Iterations == 0) {
		SetDefaultPrecision(size_t(std::max<uint64_t>(PrecisionForHalfH(DeepestFrameHalfH(Session.Frames)),
			uint64_t(mpf_get_default_prec()))));
		ComputeFrameIterations();
	}
	// The dialog shows the settings of the record which is being continued.
	SetDlgItemInt(hWndDlg, IDC_EXPORT_WIDTH, UINT(Session.Width), FALSE);
	SetDlgItemInt(hWndDlg, IDC_EXPORT_HEIGHT, UINT(Session.Height), FALSE);
	char Buffer[64];
	sprintf_s(Buffer, "%.17g", Session.Step);
	SetDlgItemTextA(hWndDlg, IDC_EXPORT_STEP, Buffer);
	SendMessage(GetDlgItem(hWndDlg, IDC_EXPORT_TARGET_TYPE), CB_SETCURSEL, WPARAM(int(Session.TargetType)), 0);
	SetDlgItemTextA(hWndDlg, IDC_EXPORT_TARGET_VALUE, Session.TargetText.c_str());
	return true;
}

// Starts the export which has been set up (see StartNewExport and ResumeExport).
void BeginExport(HWND hWndDlg) {
	if (Session.Active) return;

	size_t Pending = 0;
	for (const ExportFrame &Frame : Session.Frames) if (!Frame.Done) Pending++;
	if (!Pending) {
		MessageBoxW(hWndDlg,
			(TrW("All key frames are already exported.\n") + TrW("Pick another output file to export them again.")).c_str(),
			TrW("Export zoom sequence").c_str(), MB_OK | MB_ICONINFORMATION);
		return;
	}

	// Remember the settings for the next time the dialog is opened.
	DefaultWidth = Session.Width;
	DefaultHeight = Session.Height;
	DefaultStep = Session.Step;
	DefaultTargetType = Session.TargetType;
	DefaultTargetText = Session.TargetText;
	DefaultRecordPath = Session.RecordPath;

	if (!SavedView.Valid) CaptureView();

	// Only the pixel computation is stopped: a reference orbit which is being
	// computed (or already finished) is what the export wants to use.
	FContext.CancelPixelComputation();
	Global::ItLim = Session.Iterations;
	Global::SuppressTextureUpdate = true;
	Global::DisablePixelReuse = true;
	FContext.SetResolution(int32_t(Session.Width), int32_t(Session.Height));
	SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
	SetControlsEnabled(hWndDlg, false);
	SetWindowTextW(hWndDlg, L"Exporting zoom sequence");
	Session.Active = true;
	Session.SessionStart = std::chrono::steady_clock::now();

	size_t First = 0;
	while (First < Session.Frames.size() && Session.Frames[First].Done) First++;
	StartFrame(First);

	SetTimer(hWndDlg, ExportTimer, ExportTimerInterval, nullptr);
	UpdateExportUI(hWndDlg);
}

void StartExport(HWND hWndDlg) {
	if (Session.Active) return;
	if (DialogMode == ExportDialogMode::Resume) {
		if (!ResumeExport(hWndDlg)) return;
	} else if (!StartNewExport(hWndDlg)) {
		return;
	}
	BeginExport(hWndDlg);
}

void FinishDialog(HWND hWndDlg, bool Cancelled) {
	bool WasActive = Session.Active;
	EndExport();
	Session.Frames.clear();
	Session.DoneCount = 0;

	if (Cancelled) {
		if (WasActive) {
			MessageBoxW(hWndDlg, L"Cancelled. The frames written so far are kept.",
				L"Export zoom sequence", MB_OK | MB_ICONINFORMATION | MB_TASKMODAL);
		}
		EndDialog(hWndDlg, IDCANCEL);
		return;
	}

	SetWindowTextW(hWndDlg, L"Export zoom sequence");
	SetControlsEnabled(hWndDlg, true);
	UpdateIdleUI(hWndDlg);
}

bool BrowseForRecord(HWND hWndDlg) {
	OPENFILENAMEW OpenFileName = {};
	wchar_t FileName[MAX_PATH] = L"";
	std::wstring Current = GetDlgItemText(hWndDlg, IDC_EXPORT_FILE);
	if (!Current.empty()) wcsncpy_s(FileName, Current.c_str(), _TRUNCATE);

	OpenFileName.lStructSize = sizeof(OPENFILENAMEW);
	OpenFileName.hwndOwner = hWndDlg;
	OpenFileName.lpstrFilter = L"Zoom sequence export record(*.imz)\0*.imz\0All Files(*.*)\0*.*\0";
	OpenFileName.nFilterIndex = 1;
	OpenFileName.lpstrFile = FileName;
	OpenFileName.nMaxFile = sizeof(FileName) / sizeof(wchar_t);
	OpenFileName.lpstrTitle = L"Export record";
	OpenFileName.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
	OpenFileName.lpstrDefExt = L"imz";

	if (!GetSaveFileNameW(&OpenFileName)) return false;

	SetDlgItemTextW(hWndDlg, IDC_EXPORT_FILE, FileName);
	// The chosen file is only where the export is written: it always starts from
	// the location which is displayed right now, and an existing record is
	// overwritten.  "Resume zoom sequence" continues one instead.
	SetRecordPath(FileName);
	Session.Frames.clear();
	Session.DoneCount = 0;
	return true;
}

void HandleCancelOrClose(HWND hWndDlg) {
	if (Session.Active) {
		if (MessageBoxW(hWndDlg, L"Cancel the running export?", L"Export zoom sequence", MB_OKCANCEL | MB_ICONQUESTION | MB_TASKMODAL) != IDOK) return;
		FinishDialog(hWndDlg, true);
		return;
	}
	EndDialog(hWndDlg, IDCANCEL);
}

INT_PTR ZoomExportProc(HWND hWndDlg, UINT Message, WPARAM wParam, LPARAM /*lParam*/) {
	switch (Message) {
		case WM_INITDIALOG: {
			ExportDialog = hWndDlg;
			LocaliseWindow(hWndDlg);
			INITCOMMONCONTROLSEX Controls = { sizeof(INITCOMMONCONTROLSEX), ICC_PROGRESS_CLASS };
			InitCommonControlsEx(&Controls);
			SendMessage(GetDlgItem(hWndDlg, IDC_EXPORT_PROGRESS), PBM_SETRANGE32, 0, 1000);

			HWND Combo = GetDlgItem(hWndDlg, IDC_EXPORT_TARGET_TYPE);
			SendMessageA(Combo, CB_ADDSTRING, 0, (LPARAM)"Height");
			SendMessageA(Combo, CB_ADDSTRING, 0, (LPARAM)"Half height");
			SendMessageA(Combo, CB_ADDSTRING, 0, (LPARAM)"Magnification");
			SendMessageA(Combo, CB_ADDSTRING, 0, (LPARAM)"Depth");
			SendMessage(Combo, CB_SETCURSEL, WPARAM(int(DefaultTargetType)), 0);

			if (!DefaultWidth || !DefaultHeight) {
				DefaultWidth = FContext.ImageWidth;
				DefaultHeight = FContext.ImageHeight;
			}
			SetDlgItemInt(hWndDlg, IDC_EXPORT_WIDTH, UINT(DefaultWidth), FALSE);
			SetDlgItemInt(hWndDlg, IDC_EXPORT_HEIGHT, UINT(DefaultHeight), FALSE);
			char Buffer[64];
			sprintf_s(Buffer, "%.17g", DefaultStep);
			SetDlgItemTextA(hWndDlg, IDC_EXPORT_STEP, Buffer);
			SetDlgItemTextA(hWndDlg, IDC_EXPORT_TARGET_VALUE, DefaultTargetText.c_str());
			SetDlgItemTextW(hWndDlg, IDC_EXPORT_FILE, DefaultRecordPath.c_str());
			SetDlgItemTextA(hWndDlg, IDC_EXPORT_STATUS,
				"Press Start to export.");

			Session.Frames.clear();
			Session.DoneCount = 0;
			Session.Width = DefaultWidth;
			Session.Height = DefaultHeight;
			Session.Step = DefaultStep;
			Session.TargetType = DefaultTargetType;
			Session.BaseHalfH = FContext.CurrentLocation.HalfH;

			if (DialogMode == ExportDialogMode::Resume) {
				// Opened from "Resume zoom sequence": the record is already
				// chosen, so the export continues right away.  Reading it opens
				// the location file it names, which puts the application into the
				// state the export was started at; the view which is displayed
				// now is remembered first, so that it comes back when the export
				// ends.
				SetDlgItemTextW(hWndDlg, IDC_EXPORT_FILE, ResumeRecordPath.c_str());
				SetDlgItemTextW(hWndDlg, IDC_EXPORT_START, L"Continue");
				if (!SavedView.Valid) CaptureView();
				StartExport(hWndDlg);
				return TRUE;
			}
			UpdateIdleUI(hWndDlg);
			return TRUE;
		}
		case WM_TIMER: {
			if (wParam == ExportTimer) {
				TickExport(hWndDlg);
				return TRUE;
			}
			break;
		}
		case WM_COMMAND: {
			switch (LOWORD(wParam)) {
				case IDC_EXPORT_BROWSE: {
					if (HIWORD(wParam) == BN_CLICKED) {
						BrowseForRecord(hWndDlg);
						UpdateIdleUI(hWndDlg);
					}
					return TRUE;
				}
				case IDC_EXPORT_FILE: {
					if (HIWORD(wParam) == EN_KILLFOCUS) {
						// A file which is already there is only overwritten, it is not
						// read back here ("Resume zoom sequence" continues a record).
						Session.Frames.clear();
						Session.DoneCount = 0;
						UpdateIdleUI(hWndDlg);
					}
					return TRUE;
				}
				case IDC_EXPORT_WIDTH:
				case IDC_EXPORT_HEIGHT:
				case IDC_EXPORT_STEP:
				case IDC_EXPORT_TARGET_VALUE: {
					if (HIWORD(wParam) == EN_KILLFOCUS) UpdateIdleUI(hWndDlg);
					return TRUE;
				}
				case IDC_EXPORT_TARGET_TYPE: {
					if (HIWORD(wParam) == CBN_SELCHANGE) UpdateIdleUI(hWndDlg);
					return TRUE;
				}
				case IDC_EXPORT_START: {
					if (HIWORD(wParam) == BN_CLICKED) StartExport(hWndDlg);
					return TRUE;
				}
				case IDCANCEL: {
					HandleCancelOrClose(hWndDlg);
					return TRUE;
				}
			}
			break;
		}
		case WM_CLOSE: {
			HandleCancelOrClose(hWndDlg);
			return TRUE;
		}
		case WM_DESTROY: {
			if (Session.Active) {
				EndExport();
				Session.Frames.clear();
			}
			ExportDialog = nullptr;
			return TRUE;
		}
	}
	return FALSE;
}

}

void ShowZoomExportDialog() {
	if (Session.Active) return;
	DialogMode = ExportDialogMode::New;
	DialogBox(nullptr, MAKEINTRESOURCE(IDD_ZOOM_EXPORT), HWnd, ZoomExportProc);
}

// File -> "Resume zoom sequence...": asks for the record of the export which is
// to be continued and then opens the export dialog, which carries on with the
// pending key frames at once (StartNewExport is bypassed, see StartExport).
void ResumeZoomExport() {
	if (Session.Active) return;

	wchar_t FileName[MAX_PATH] = L"";
	if (!DefaultRecordPath.empty()) wcsncpy_s(FileName, DefaultRecordPath.c_str(), _TRUNCATE);

	OPENFILENAMEW OpenFileName = {};
	OpenFileName.lStructSize = sizeof(OPENFILENAMEW);
	OpenFileName.hwndOwner = HWnd;
	OpenFileName.lpstrFilter = L"Zoom sequence export record(*.imz)\0*.imz\0All Files(*.*)\0*.*\0";
	OpenFileName.nFilterIndex = 1;
	OpenFileName.lpstrFile = FileName;
	OpenFileName.nMaxFile = sizeof(FileName) / sizeof(wchar_t);
	OpenFileName.lpstrTitle = L"Continue which zoom sequence?";
	OpenFileName.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
	OpenFileName.lpstrDefExt = L"imz";
	if (!GetOpenFileNameW(&OpenFileName)) return;

	ResumeRecordPath = FileName;
	DialogMode = ExportDialogMode::Resume;
	DialogBox(nullptr, MAKEINTRESOURCE(IDD_ZOOM_EXPORT), HWnd, ZoomExportProc);
	DialogMode = ExportDialogMode::New;
}

// Writes the iteration limit of every key frame of an export record, so that the
// limits an export would use can be inspected without running it (see
// ComputeFrameIterations).  Driven by
// "Imagina.exe --period <record.imz> [<out.txt>]".
