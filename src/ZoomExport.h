#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Types.h"

// Zoom sequence export.
//
// src/ZoomExport.cpp holds the part that does not need a window: the export
// record (*.imz), the key frame list, the per frame iteration limits, and the
// helper that prints the limits of a record for "Imagina --period".  The
// front-ends own the dialogs and the loop that drives the export
// (src/win32/ZoomExport.cpp and src/sdl/SDLMain.cpp).

enum class ZoomType {
	Height = 0,
	HalfHeight = 1,
	Magnification = 2,
	Depth = 3,
};

std::string ZoomToString(ZoomType type, HRReal halfH, HRReal OriginalHalfH);
HRReal ParseZoom(ZoomType type, std::string_view string, HRReal OriginalHalfH);

const char *TargetTypeName(ZoomType Type);
bool TargetTypeFromName(const std::string &Name, ZoomType &Type);

// Limits of a sequence: the exporter refuses to go beyond these.
constexpr size_t MaxExportFrames = 100000;
constexpr uint64_t MaxExportPixels = 100ull * 1000ull * 1000ull;
constexpr size_t MinExportSize = 4;

struct ExportFrame {
	size_t Index = 0;
	HRReal HalfH;
	// Iterations this frame is computed with (see ComputeFrameIterations), kept
	// so that the progress display can show it.
	size_t Iterations = 0;
	bool Done = false;
};

// One (possibly resumed) export.  The coordinates are kept at the precision the
// deepest key frame needs.
struct ExportSession {
	bool Active = false;
	std::wstring RecordPath;
	std::wstring FileBase;
	// The *.im file the record names, which holds the coordinates of the view the
	// sequence starts at (see File.cpp).  Empty for a record which carries the
	// coordinates itself, that is one of an older version.
	std::wstring LocationPath;
	// Where the centre of the key frames sits relative to the centre of that
	// location, in units of the half height of the sequence.
	double ViewOffsetX = 0.0, ViewOffsetY = 0.0;
	size_t Width = 0, Height = 0;
	size_t Iterations = 0;
	double Step = 2.0;
	ZoomType TargetType = ZoomType::Height;
	std::string TargetText = "10";
	HRReal TargetValue = 10.0_hr;
	HPReal CenterX, CenterY;
	HRReal BaseHalfH = 2.0_hr;
	std::vector<ExportFrame> Frames;
	size_t Cursor = 0;
	size_t DoneCount = 0;
	std::chrono::steady_clock::time_point SessionStart, FrameStart;
	double LastFrameSeconds = 0.0;
	bool HaveTiming = false;
};

extern ExportSession Session;

// Path helpers: the record stores the *.im file next to it by name.
void SplitRecordPath(const std::wstring &Path, std::wstring &Directory, std::wstring &FileBase);
std::string FileNameOf(const std::wstring &Path);
std::wstring LocationPathOf(const std::wstring &RecordPath);
std::wstring FramePath(size_t Index);
bool FileExists(const std::wstring &Path);
void SetRecordPath(const std::wstring &Path);

// UTF-8 <-> wide string, for the paths the record stores.
std::string ToUtf8(const std::wstring &Text);
std::wstring FromUtf8(const std::string &Text);

// Formatting used by the record and the dialogs.
uint64_t PrecisionForHalfH(HRReal HalfH);
double DepthOf(HRReal HalfH);
std::string DepthToString(HRReal HalfH);
std::string MagnificationToString(HRReal HalfH);
std::string FormatDouble(double Value);
std::string FormatDuration(double Seconds);
HRReal DeepestFrameHalfH(const std::vector<ExportFrame> &Frames);

// The key frame list of a sequence, from BaseHalfH outwards in Step factors.
bool BuildFrames(HRReal BaseHalfH, double Step, HRReal StopHalfH, std::vector<ExportFrame> &Frames);
bool BuildFrames();

// Export record (*.imz) input and output; ReadRecordFile also restores the view
// the record starts at (it opens the location file it names).
bool WriteRecordFile();
bool ReadRecordFile();

// Works out the iteration limit of every key frame in one walk over the orbit of
// the centre (see the comment in ZoomExport.cpp).
void ComputeFrameIterations();

// Writes the per frame iteration limits of a record, for
// "Imagina --period <record.imz> [<out.txt>]".
void PrintFrameIterations(const std::filesystem::path &RecordPath, const std::filesystem::path &OutPath);

// --- front-end entry points -------------------------------------------------
// Opens the zoom sequence export dialog.  While it is open, the main window is
// blocked, so the location and the zoom cannot be changed by accident.
void ShowZoomExportDialog();

// Asks for the record of an interrupted export and continues it ("File -> Resume
// zoom sequence...").
void ResumeZoomExport();
