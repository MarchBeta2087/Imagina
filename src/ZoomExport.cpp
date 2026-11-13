// The parts of the zoom sequence export that do not need a window: the export
// record (*.imz), the key frame list, the per frame iteration limits and the
// command line helper that prints them.  The front-ends drive this with their own
// timer/loop and their own dialogs (Win32: src/win32/ZoomExport.cpp, SDL:
// src/sdl/SDLMain.cpp).

#include "Includes.h"
#include "ZoomExport.h"

#include "File.h"
#include "RawMapCodec.h"
#include "Language.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// Written into the export record so that a record shows which encoding its raw
// map files use (see RawMapCodec.h for the format).
constexpr const char *FrameEncoding = "rawz-zlib1";

// The key frames run from the current view outwards (the view gets wider with
// every frame), which is what the export record stores so that an interrupted
// export continues with the same sequence.
constexpr const char *SequenceDirection = "outwards";

ExportSession Session;
// The export record stores paths as UTF-8 while the program keeps them as wide
// strings (that is what the Windows file dialogs hand out).
std::string ToUtf8(const std::wstring &Text) {
	std::u8string Converted = std::filesystem::path(Text).u8string();
	return std::string(reinterpret_cast<const char *>(Converted.data()), Converted.size());
}

std::wstring FromUtf8(const std::string &Text) {
	return std::filesystem::path(Text).wstring();
}

uint64_t PrecisionForHalfH(HRReal HalfH) {
	return uint64_t(-std::min<int64_t>(0, HalfH.Exponent) + 64);
}

double DepthOf(HRReal HalfH) {
	return 1.0 - double(HalfH.Exponent) - log2(HalfH.Mantissa);
}

std::string DepthToString(HRReal HalfH) {
	char Buffer[32];
	snprintf(Buffer, sizeof(Buffer), "%.4g", DepthOf(HalfH));
	return Buffer;
}

std::string MagnificationToString(HRReal HalfH) {
	return ZoomToString(ZoomType::Magnification, HalfH, 2.0_hr);
}

std::string FormatDouble(double Value) {
	char Buffer[32];
	snprintf(Buffer, sizeof(Buffer), "%.17g", Value);
	return Buffer;
}

std::string FormatDuration(double Seconds) {
	char Buffer[64];
	if (!(Seconds > 0.0)) Seconds = 0.0;
	size_t Total = size_t(Seconds + 0.5);
	size_t Minutes = Total / 60, Rest = Total % 60;
	if (Minutes >= 60) snprintf(Buffer, sizeof(Buffer), "%zu:%02zu:%02zu", Minutes / 60, Minutes % 60, Rest);
	else if (Minutes) snprintf(Buffer, sizeof(Buffer), "%zu:%02zu", Minutes, Rest);
	else snprintf(Buffer, sizeof(Buffer), "%.1fs", Seconds);
	return Buffer;
}

std::wstring FramePath(size_t Index) {
	wchar_t Suffix[32];
	// %ls: in a wide printf %s is a *narrow* string (C99/UCRT).
	swprintf(Suffix, 32, L"-%04zu%ls", Index, RawMapCodec::FileExtension);
	return Session.FileBase + Suffix;
}

HRReal DeepestFrameHalfH(const std::vector<ExportFrame> &Frames) {
	HRReal Deepest = Frames.empty() ? Session.BaseHalfH : Frames.front().HalfH;
	for (const ExportFrame &Frame : Frames) {
		if (Frame.HalfH < Deepest) Deepest = Frame.HalfH;
	}
	return Deepest;
}

bool FileExists(const std::wstring &Path) {
	std::error_code Error;
	return std::filesystem::is_regular_file(std::filesystem::path(Path), Error);
}

// Defined with the dialog helpers below; the record needs the split to name the
// location file, which holds the coordinates of the view the sequence starts at
// (an *.im file next to the record, see File.cpp).
void SplitRecordPath(const std::wstring &Path, std::wstring &Directory, std::wstring &FileBase);
std::string FileNameOf(const std::wstring &Path);

std::wstring LocationPathOf(const std::wstring &RecordPath) {
	std::wstring Directory, FileBase;
	SplitRecordPath(RecordPath, Directory, FileBase);
	return FileBase + L".im";
}

const char *TargetTypeName(ZoomType Type) {
	switch (Type) {
		case ZoomType::Height: return "Height";
		case ZoomType::HalfHeight: return "HalfHeight";
		case ZoomType::Magnification: return "Magnification";
		case ZoomType::Depth: return "Depth";
	}
	return "Magnification";
}

bool TargetTypeFromName(const std::string &Name, ZoomType &Type) {
	if (Name == "Height") { Type = ZoomType::Height; return true; }
	if (Name == "HalfHeight" || Name == "Half height") { Type = ZoomType::HalfHeight; return true; }
	if (Name == "Magnification") { Type = ZoomType::Magnification; return true; }
	if (Name == "Depth") { Type = ZoomType::Depth; return true; }
	return false;
}

bool BuildFrames(HRReal BaseHalfH, double Step, HRReal StopHalfH, std::vector<ExportFrame> &Frames) {
	Frames.clear();
	if (!(Step > 1.0)) return false;

	HRReal HalfH = BaseHalfH;
	for (size_t i = 0; i < MaxExportFrames; i++) {
		ExportFrame Frame;
		Frame.Index = i;
		Frame.HalfH = HalfH;
		Frames.push_back(Frame);
		if (HalfH >= StopHalfH) break;
		HRReal Next = HalfH * HRReal(Step);
		if (Next > StopHalfH) break;
		HalfH = Next;
	}
	return !Frames.empty();
}

bool BuildFrames() {
	return BuildFrames(Session.BaseHalfH, Session.Step, Session.TargetValue, Session.Frames);
}

bool WriteRecordFile() {
	std::ofstream File(std::filesystem::path(Session.RecordPath), std::ofstream::out | std::ofstream::trunc);
	if (!File) return false;

	File << "ImaginaZoomExport: 1\n";
	File << "Width: " << Session.Width << "\n";
	File << "Height: " << Session.Height << "\n";
	File << "Step: " << FormatDouble(Session.Step) << "\n";
	File << "Encoding: " << FrameEncoding << "\n";
	File << "Direction: " << SequenceDirection << "\n";
	File << "TargetType: " << TargetTypeName(Session.TargetType) << "\n";
	File << "TargetValue: " << Session.TargetText << "\n";
	// The coordinates of the key frames, the iteration limit and the precision
	// they need are the ones of the view the sequence starts at, so they are
	// kept in the location file next to this record (the format of
	// "File -> Save") instead of being repeated here.
	std::wstring LocationFile = Session.LocationPath.empty()
		? LocationPathOf(Session.RecordPath) : Session.LocationPath;
	std::wstring LocationDirectory, LocationBase;
	SplitRecordPath(Session.RecordPath, LocationDirectory, LocationBase);
	// A name is enough while the file sits next to the record, which keeps the
	// pair portable; a location somewhere else is written in full.
	File << "Location: " << (LocationFile.rfind(LocationDirectory, 0) == 0
		? FileNameOf(LocationFile) : ToUtf8(LocationFile)) << "\n";
	// How far the centre of the key frames is from the centre of that location.
	// A location file holds one point (the one its reference orbit belongs to),
	// the view may have been panned away from it, and a continued export has to
	// put the view back exactly (see ResumeExport).  Left out when the view was
	// not moved away from it.
	if (Session.ViewOffsetX != 0.0 || Session.ViewOffsetY != 0.0) {
		File << "ViewOffset: " << FormatDouble(Session.ViewOffsetX) << " "
			<< FormatDouble(Session.ViewOffsetY) << "\n";
	}

	File << "# the half height of frame i is the size in the location file times Step^i\n";
	File << "# (frame 0 is the view the export started at, the frames get wider),\n";
	File << "# magnification = 2 / half height, depth = log2(2 / half height);\n";
	File << "# frames are written while their depth stays >= 0, that is while the view is not\n";
	File << "# wider than the whole 2 x 2 fractal;\n";
	File << "# the file of a key frame is <record name>-<four digit index>" << ToUtf8(RawMapCodec::FileExtension) << "\n";

	size_t NumberOfFrames = Session.Frames.size();
	File << "FrameCount: " << NumberOfFrames << "\n";
	// The iteration limit every key frame is computed with, as pairs of "from
	// this frame on, this limit" (the limits only fall, so a few entries describe
	// the whole sequence).  They are written down so that a continued export does
	// not have to work the periods out again, which is a walk over the orbit of
	// the centre for every frame otherwise (see ComputeFrameIterations).
	if (!Session.Frames.empty()) {
		File << "Limits:";
		size_t Previous = 0;
		bool Any = false;
		for (const ExportFrame &Frame : Session.Frames) {
			if (!Any || Frame.Iterations != Previous) {
				File << " " << Frame.Index << " " << Frame.Iterations;
				Previous = Frame.Iterations;
				Any = true;
			}
		}
		File << "\n";
	}
	for (const ExportFrame &Frame : Session.Frames) {
		if (!Frame.Done) continue;
		// The size of a key frame is not written down: with the index it follows
		// from the size in the location file and the step.
		File << "Frame: " << Frame.Index << "\n";
	}
	File.flush();
	return File.good();
}

bool ReadRecordFile() {
	std::ifstream File(std::filesystem::path(Session.RecordPath), std::ifstream::in);
	if (!File) return false;

	try {
		std::unordered_map<size_t, bool> ExportedFrames;
		size_t Width = 0, Height = 0;
		double Step = 0.0;
		ZoomType TargetType = ZoomType::Magnification;
		std::string TargetText, LocationText;
		double ViewOffsetX = 0.0, ViewOffsetY = 0.0;
		std::vector<std::pair<size_t, size_t>> Limits;
		bool HaveDirection = false;
		bool Valid = false;

		std::string Line;
		while (std::getline(File, Line)) {
			size_t Separator = Line.find(':');
			if (Separator == std::string::npos) continue;
			std::string Key = Line.substr(0, Separator);
			std::string Value = Line.substr(Separator + 1);
			while (!Value.empty() && (Value.front() == ' ' || Value.front() == '\t')) Value.erase(0, 1);
			while (!Key.empty() && (Key.back() == ' ' || Key.back() == '\t')) Key.pop_back();

			if (Key == "ImaginaZoomExport") {
				Valid = true;
			} else if (Key == "Width") {
				Width = size_t(strtoull(Value.c_str(), nullptr, 10));
			} else if (Key == "Height") {
				Height = size_t(strtoull(Value.c_str(), nullptr, 10));
			} else if (Key == "Step") {
				Step = strtod(Value.c_str(), nullptr);
			} else if (Key == "TargetType") {
				TargetTypeFromName(Value, TargetType);
			} else if (Key == "TargetValue") {
				TargetText = Value;
			} else if (Key == "ViewOffset") {
				// How far the centre of the key frames was panned away from the
				// centre of the location file (see WriteRecordFile).
				sscanf(Value.c_str(), "%lf %lf", &ViewOffsetX, &ViewOffsetY);
			} else if (Key == "Limits") {
				// The iteration limits of the whole sequence, as pairs of "from
				// this frame on, this limit" (see WriteRecordFile).
				std::stringstream Text(Value);
				size_t Index = 0, Limit = 0;
				while (Text >> Index >> Limit) Limits.emplace_back(Index, Limit);
			} else if (Key == "Location") {
				LocationText = Value;
			} else if (Key == "Direction") {
				// Only the outward sequence exists; a record from an older
				// version (or a different direction) cannot be continued.
				if (Value != SequenceDirection) return false;
				HaveDirection = true;
			} else if (Key == "Frame") {
				size_t Index = 0;
				if (sscanf(Value.c_str(), "%zu", &Index) == 1) ExportedFrames[Index] = true;
			}
		}

		if (!Valid || !Width || !Height || !(Step > 1.0) || !HaveDirection) return false;

		// The location of the sequence lives in the *.im file the record names:
		// it is opened here the way the file menu opens one, which restores the
		// centre, the half height, the iteration limit, the precision and the
		// reference orbit, exactly the state the export was started at.
		if (LocationText.empty()) return false;
		std::wstring Directory, FileBase;
		SplitRecordPath(Session.RecordPath, Directory, FileBase);
		std::wstring Location = FromUtf8(LocationText);
		bool Absolute = Location.size() > 1
			&& (Location[1] == L':' || Location[0] == L'\\' || Location[0] == L'/');
		std::wstring LocationPath = Absolute ? Location : Directory + Location;
		OpenFile(std::filesystem::path(LocationPath));

		HRReal HalfH = FContext.CurrentLocation.HalfH;
		if (!(HalfH > 0.0_hr)) return false;
		// The frames are centred where the location is, plus the offset the view
		// was panned by when the export was started (the location file holds the
		// point the reference belongs to).
		HPReal CenterX = HPReal(FContext.CenterCoordinate.X) + HPReal(ViewOffsetX) * HPReal(HalfH);
		HPReal CenterY = HPReal(FContext.CenterCoordinate.Y) + HPReal(ViewOffsetY) * HPReal(HalfH);
		size_t Iterations = std::max<size_t>(2, std::min<size_t>(size_t(Global::ItLim), UINT64_C(1) << 32));

		Session.Width = Width;
		Session.Height = Height;
		Session.Iterations = Iterations;
		Session.Step = Step;
		Session.TargetType = TargetType;
		Session.TargetText = TargetText;
		Session.TargetValue = ParseZoom(TargetType, TargetText, 2.0_hr);
		Session.BaseHalfH = HalfH;
		Session.ViewOffsetX = ViewOffsetX;
		Session.ViewOffsetY = ViewOffsetY;

		// Rebuild the key frame list and mark the frames listed in the record as
		// done, unless their raw file is gone.
		if (!BuildFrames()) return false;

		if (!Limits.empty()) {
			size_t Limit = Session.Iterations;
			size_t Position = 0;
			for (ExportFrame &Frame : Session.Frames) {
				while (Position < Limits.size() && Limits[Position].first <= Frame.Index) {
					Limit = Limits[Position].second;
					Position++;
				}
				Frame.Iterations = Limit;
			}
		}

		// The coordinate has to be kept with the precision of the deepest key frame
		// (and not lower than the application already uses), otherwise the export
		// would continue at a slightly different location.
		uint64_t Precision = std::max<uint64_t>(PrecisionForHalfH(DeepestFrameHalfH(Session.Frames)),
			uint64_t(mpf_get_default_prec()));
		Session.CenterX = HPReal(CenterX, Precision);
		Session.CenterY = HPReal(CenterY, Precision);

		for (ExportFrame &Frame : Session.Frames) {
			if (ExportedFrames.contains(Frame.Index) && FileExists(FramePath(Frame.Index))) Frame.Done = true;
		}
		Session.DoneCount = 0;
		for (const ExportFrame &Frame : Session.Frames) if (Frame.Done) Session.DoneCount++;
		return true;
	} catch (...) {
		// A record file with unexpected content is simply not usable.
		Session.Frames.clear();
		Session.DoneCount = 0;
		return false;
	}
}

std::string ZoomToString(ZoomType Type, HRReal HalfH, HRReal OriginalHalfH) {
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

HRReal ParseZoom(ZoomType Type, std::string_view Text, HRReal OriginalHalfH) {
	HRReal Zoom = mpf_class(std::string(Text).data(), 52);
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

// Iterations the interactive zoom allows for one period of a cycle.
constexpr size_t IterationsPerPeriod = 256;

// A key frame is never given fewer iterations than this.  The limit is also the
// point where a pixel counts as inside the set, and a limit which is too tight
// would grow black patches which the neighbouring key frames do not have.
constexpr size_t MinFrameIterations = 1024;

// Longest orbit the period of a view is looked for in.
constexpr size_t MaxPeriodSearch = 1 << 16;

void ComputeFrameIterations() {
	if (Session.Frames.empty()) return;

	// A longer period than this would give a limit above the session limit
	// anyway, so the orbit is not walked further than that.
	size_t Search = std::clamp<size_t>(Session.Iterations / IterationsPerPeriod + 2, 64, MaxPeriodSearch);

	// Critical radius of every iteration: the view of that radius (its height),
	// or wider, is a periodic region from this iteration on.
	std::vector<HRReal> Critical;
	Critical.reserve(Search);

	HPReal CR = Session.CenterX, CI = Session.CenterY;
	HPReal ZR = CR, ZI = CI;
	std::complex<HRReal> z{ convert<HRReal>(ZR), convert<HRReal>(ZI) };
	std::complex<HRReal> dzdc = HRReal(1.0);
	OMITemp<HPReal> Temp;

	for (size_t i = 1; i <= Search; i++) {
		HRReal ChebyZ = ChebyshevNorm(z);
		HRReal ChebyDzdc = ChebyshevNorm(dzdc);
		Critical.push_back(ChebyZ / (HRReal(2.0) * ChebyDzdc));

		OMI(ZR, ZI, CR, CI, Temp);
		dzdc = HRReal(2.0) * z * dzdc + HRReal(1.0);
		z = std::complex<HRReal>{ convert<HRReal>(ZR), convert<HRReal>(ZI) };

		// The orbit escaped: the views which are still waiting for their period
		// are wider than anything the period describes, they keep the session
		// limit.
		if (std::norm(z) > HRReal(5.0)) break;
	}

	// A view is periodic from the first iteration whose critical radius is below
	// its radius, so a running minimum turns the sequence into one which only
	// falls.
	for (size_t i = 1; i < Critical.size(); i++) {
		if (Critical[i] > Critical[i - 1]) Critical[i] = Critical[i - 1];
	}

	// The key frames run from the deepest to the widest one, so their periods
	// only get shorter: walk the frames from the last one backwards and let one
	// cursor move through the critical radii.
	size_t Cursor = 0;
	for (size_t Index = Session.Frames.size(); Index-- > 0;) {
		ExportFrame &Frame = Session.Frames[Index];
		HRReal Radius = Frame.HalfH * 2.0_hr;
		while (Cursor < Critical.size() && !(Critical[Cursor] < Radius)) Cursor++;
		Frame.Iterations = Cursor < Critical.size()
			? std::min(Session.Iterations, std::max(MinFrameIterations, (Cursor + 1) * IterationsPerPeriod))
			: Session.Iterations;
	}
}

void SplitRecordPath(const std::wstring &Path, std::wstring &Directory, std::wstring &FileBase) {
	size_t Separator = Path.find_last_of(L"\\/");
	if (Separator == std::wstring::npos) {
		Directory.clear();
	} else {
		Directory = Path.substr(0, Separator + 1);
	}
	std::wstring FileName = Separator == std::wstring::npos ? Path : Path.substr(Separator + 1);
	size_t Extension = FileName.find_last_of(L'.');
	if (Extension == std::wstring::npos) Extension = FileName.size();
	FileBase = Directory + FileName.substr(0, Extension);
}

void SetRecordPath(const std::wstring &Path) {
	std::wstring Directory, FileBase;
	SplitRecordPath(Path, Directory, FileBase);
	Session.RecordPath = Path;
	Session.FileBase = FileBase;
}

std::string FileNameOf(const std::wstring &Path) {
	size_t Separator = Path.find_last_of(L"\\/");
	return ToUtf8(Separator == std::wstring::npos ? Path : Path.substr(Separator + 1));
}

void PrintFrameIterations(const std::filesystem::path &RecordPath, const std::filesystem::path &OutPath) {
	std::ofstream Out(OutPath, std::ofstream::out | std::ofstream::trunc);
	Session.RecordPath = RecordPath.wstring();
	SetRecordPath(Session.RecordPath);
	if (!ReadRecordFile()) {
		Out << "not an export record\n";
		return;
	}
	if (Session.Frames.empty()) return;
	SetDefaultPrecision(size_t(std::max(PrecisionForHalfH(DeepestFrameHalfH(Session.Frames)),
		uint64_t(mpf_get_default_prec()))));
	// A record which does not list the limits (one of an older version) gets them
	// worked out here, once.
	if (Session.Frames.back().Iterations == 0) ComputeFrameIterations();
	Out << "session limit " << Session.Iterations << ", precision " << mpf_get_default_prec() << "\n";
	for (const ExportFrame &Frame : Session.Frames) {
		Out << Frame.Index << " " << Frame.Iterations << " "
			<< MagnificationToString(Frame.HalfH) << " zooms\n";
	}
}

// ---------------------------------------------------------------------------
// Driving an export
//
// The front-ends call ZoomExportTick() from their own loop/timer while an export
// runs; everything the export does to the computation happens here.
// ---------------------------------------------------------------------------

bool ZoomExportFrameComplete() {
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

ZoomExportPhase ZoomExportCurrentPhase(SRReal &Fraction) {
	if (FContext.ReferenceTaskContext) {
		Fraction = 0.0;
		return ZoomExportPhase::Reference;
	}
	if (FContext.EvaluationTaskContext) {
		SRReal Numerator = 0.0, Denominator = 0.0;
		if (FContext.pixelManager.GetProgress(Numerator, Denominator) && Denominator > 0.0) {
			Fraction = std::clamp<SRReal>(Numerator / Denominator, 0.0, 1.0);
		} else {
			Fraction = 0.0;
		}
		return ZoomExportPhase::Pixels;
	}
	Fraction = 0.0;
	return ZoomExportPhase::Prepare;
}

void ZoomExportCaptureView() {
	Session.SavedView.CenterX = FContext.CenterCoordinate.X;
	Session.SavedView.CenterY = FContext.CenterCoordinate.Y;
	Session.SavedView.Location = FContext.CurrentLocation;
	Session.SavedView.Precision = PrecisionForHalfH(FContext.CurrentLocation.HalfH);
	Session.SavedView.Precision = std::max<uint64_t>(Session.SavedView.Precision, uint64_t(FContext.CenterCoordinate.X.get_prec()));
	Session.SavedView.ImageWidth = FContext.ImageWidth;
	Session.SavedView.ImageHeight = FContext.ImageHeight;
	Session.SavedView.ItLim = Global::ItLim;
	Session.SavedView.Valid = true;
}

bool ZoomExportStartFrame(size_t Cursor) {
	if (Cursor >= Session.Frames.size()) return false;
	Session.Cursor = Cursor;
	ExportFrame &Frame = Session.Frames[Cursor];

	// The iteration limit of a key frame follows its scale, so that the wide
	// frames do not spend the limit of the deepest one on the pixels which are
	// inside the set (see ComputeFrameIterations).
	Global::ItLim = Frame.Iterations;

	// Move the view the way the interactive zoom does: the centre stays where it
	// is and only the visible size changes, so the reference orbit the
	// application already computed for this centre is reused instead of being
	// computed again for every key frame.  The frame is drawn from a clean pixel
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
	return true;
}

void ZoomExportRestoreView() {
	if (!Session.SavedView.Valid) return;

	FContext.CancelPixelComputation();
	SetDefaultPrecision(size_t(Session.SavedView.Precision));

	// Back to the view which was displayed before the export, again without
	// invalidating the reference: it is the orbit of the centre the export used,
	// so it is still the right one for this view.
	FContext.CenterCoordinate.X = Session.SavedView.CenterX;
	FContext.CenterCoordinate.Y = Session.SavedView.CenterY;
	FContext.CurrentLocation = Session.SavedView.Location;
	FContext.RenderLocation = Session.SavedView.Location;
	FContext.EvalLocation = Session.SavedView.Location;
	FContext.ComputePixel = true;
	Global::ItLim = Session.SavedView.ItLim;
	FContext.SetResolution(int32_t(Session.SavedView.ImageWidth), int32_t(Session.SavedView.ImageHeight));
	Global::Redraw = true;
	Session.SavedView.Valid = false;
}

void ZoomExportEnd() {
	if (!Session.Active) return;
	Session.Active = false;
	PlatformKeepDisplayOn(false);
	Global::SuppressTextureUpdate = false;
	Global::DisablePixelReuse = false;
	ZoomExportRestoreView();
}

bool ZoomExportSaveFrame() {
	const ExportFrame &Frame = Session.Frames[Session.Cursor];
	std::wstring Path = FramePath(Frame.Index);


	std::vector<float> Pixels;
	int32_t Width = 0, Height = 0;
	if (!GetRawPixelData(Pixels, Width, Height)) {
		Session.Error = Tr("The pixels of the frame cannot be read.");
		Session.Active = false;
		return false;
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
		if (Error.empty()) Error = Tr("the stored pixels differ from the frame");
		Session.Error = std::string(Tr("Writing or reading back the compressed raw map failed: ")) + ToUtf8(Path) + " (" + Error + ")";
		Session.Active = false;
		return false;
	}

	Session.Frames[Session.Cursor].Done = true;
	Session.DoneCount++;
	Session.LastFrameSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Session.FrameStart).count();
	Session.HaveTiming = true;
	WriteRecordFile();
	return true;
}

bool ZoomExportTick() {
	if (!Session.Active) return false;

	if (ZoomExportFrameComplete()) {
		if (!ZoomExportSaveFrame()) {
			ZoomExportEnd();
			return false;
		}

		size_t Next = Session.Cursor + 1;
		while (Next < Session.Frames.size() && Session.Frames[Next].Done) Next++;

		if (Next >= Session.Frames.size()) {
			Session.Finished = true;
			ZoomExportEnd();
			return false;
		}

		ZoomExportStartFrame(Next);
		return true;
	}

	// Update() drives the reference and the pixel computation; it is cheap once
	// the workers are busy, so calling it a few times per tick keeps the progress
	// display alive.
	for (int i = 0; i < 4 && !ZoomExportFrameComplete(); i++) FContext.Update();
	return true;
}

std::string ZoomExportStatus(SRReal &Fraction) {
	std::stringstream Stream;
	if (!Session.Active || Session.Frames.empty()) {
		Fraction = Session.Finished ? 1.0 : 0.0;
		return Session.Error.empty() ? std::string() : Session.Error;
	}

	ZoomExportPhase Phase = ZoomExportCurrentPhase(Fraction);
	const ExportFrame &Frame = Session.Frames[Session.Cursor];

	// The whole line is one format string per language, because the order of the
	// numbers and the words around them differs between languages.
	{
		char Buffer[512];
		std::string Zoom = MagnificationToString(Frame.HalfH);
		snprintf(Buffer, sizeof Buffer, Tr("Frame %zu/%zu, %s zooms, %zu iterations"),
			Session.DoneCount + 1, Session.Frames.size(), Zoom.c_str(), Frame.Iterations);
		Stream << Buffer << "\n";
	}

	switch (Phase) {
		case ZoomExportPhase::Reference: Stream << Tr("reference orbit"); break;
		case ZoomExportPhase::Pixels: {
			char Buffer[128];
			snprintf(Buffer, sizeof Buffer, Tr("pixels %d%%"), int(Fraction * 100.0 + 0.5));
			Stream << Buffer;
			break;
		}
		default: Stream << Tr("starting"); break;
	}

	double Elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - Session.SessionStart).count();
	Stream << ", " << FormatDuration(Elapsed);
	if (Session.HaveTiming && Session.DoneCount) {
		size_t Remaining = Session.Frames.size() - Session.DoneCount - 1;
		if (Remaining) {
			char Buffer[256];
			snprintf(Buffer, sizeof Buffer, Tr(", ~%s left"),
				FormatDuration(Session.LastFrameSeconds * double(Remaining)).c_str());
			Stream << Buffer;
		}
	}
	return Stream.str();
}


bool ZoomExportBegin() {
	if (Session.Frames.empty()) return false;

	// The deepest key frame decides the precision of the centre coordinate.
	uint64_t Precision = std::max<uint64_t>(PrecisionForHalfH(DeepestFrameHalfH(Session.Frames)),
		uint64_t(mpf_get_default_prec()));
	Session.CenterX = HPReal(Session.CenterX, Precision);
	Session.CenterY = HPReal(Session.CenterY, Precision);
	SetDefaultPrecision(size_t(Precision));
	ComputeFrameIterations();

	Session.Active = true;
	Session.Finished = false;
	Session.Error.clear();
	Session.DoneCount = 0;
	for (const ExportFrame &Frame : Session.Frames) if (Frame.Done) Session.DoneCount++;
	Session.SessionStart = std::chrono::steady_clock::now();
	Session.HaveTiming = false;
	Session.LastFrameSeconds = 0.0;

	// While an export runs the display must not depend on the window: the frame is
	// read from the pixel manager directly, and the textures are not uploaded.
	Global::SuppressTextureUpdate = true;
	Global::DisablePixelReuse = true;
	PlatformKeepDisplayOn(true);
	// The key frames have the size the export was configured with, not the window.
	FContext.SetResolution(int32_t(Session.Width), int32_t(Session.Height));

	size_t First = 0;
	while (First < Session.Frames.size() && Session.Frames[First].Done) First++;
	if (First >= Session.Frames.size()) {
		Session.Finished = true;
		ZoomExportEnd();
		return false;
	}
	return ZoomExportStartFrame(First);
}

// Sets the session up for a new sequence from the view that is displayed now:
// the location file next to the record gets that view (the format of
// "File -> Save"), so a resumed export finds the same coordinates again.
bool ZoomExportPrepareNew(const std::filesystem::path &RecordPath, size_t Width, size_t Height, double Step,
	ZoomType TargetType, const std::string &TargetText, std::string &Error) {
	if (Width < MinExportSize || Height < MinExportSize) {
		Error = Tr("The image size is too small.");
		return false;
	}
	if (Width * Height > MaxExportPixels) {
		Error = Tr("The image size is too large (more than 100 megapixels).");
		return false;
	}
	if (!(Step > 1.0)) {
		Error = Tr("The zoom step has to be larger than 1.");
		return false;
	}
	HRReal TargetValue;
	try {
		TargetValue = ParseZoom(TargetType, TargetText, 2.0_hr);
	} catch (...) {
		Error = Tr("The zoom target is invalid.");
		return false;
	}
	if (!(TargetValue > 0.0_hr) || !(TargetValue <= 16.0_hr)) {
		Error = Tr("The zoom target is out of range.");
		return false;
	}

	SetRecordPath(RecordPath.wstring());
	Session.Cursor = 0;
	Session.DoneCount = 0;
	Session.Frames.clear();
	Session.HaveTiming = false;
	Session.LastFrameSeconds = 0.0;
	Session.Finished = false;
	Session.Error.clear();

	Session.Width = Width;
	Session.Height = Height;
	Session.Step = Step;
	Session.TargetType = TargetType;
	Session.TargetText = TargetText;
	Session.TargetValue = TargetValue;
	Session.Iterations = std::max<size_t>(2, std::min<size_t>(size_t(Global::ItLim), UINT64_C(1) << 32));
	Session.BaseHalfH = FContext.CurrentLocation.HalfH;
	Session.LocationPath.clear();
	if (!BuildFrames()) {
		Error = Tr("The selected stop condition does not produce any key frame.");
		return false;
	}

	uint64_t Precision = std::max(PrecisionForHalfH(DeepestFrameHalfH(Session.Frames)),
		uint64_t(mpf_get_default_prec()));
	SetDefaultPrecision(size_t(Precision));
	Session.CenterX = HPReal(FContext.CenterCoordinate.X, Precision);
	Session.CenterX += HPReal(FContext.CurrentLocation.X);
	Session.CenterY = HPReal(FContext.CenterCoordinate.Y, Precision);
	Session.CenterY += HPReal(FContext.CurrentLocation.Y);
	ComputeFrameIterations();

	HPReal OffsetX = HPReal(Session.CenterX - FContext.CenterCoordinate.X) / HPReal(Session.BaseHalfH);
	HPReal OffsetY = HPReal(Session.CenterY - FContext.CenterCoordinate.Y) / HPReal(Session.BaseHalfH);
	Session.ViewOffsetX = mpf_get_d(OffsetX.get_mpf_t());
	Session.ViewOffsetY = mpf_get_d(OffsetY.get_mpf_t());

	SaveFile(std::filesystem::path(LocationPathOf(Session.RecordPath)), FileType::Imagina);
	WriteRecordFile();
	return true;
}

// Sets the session up from an existing record; ReadRecordFile opens the location
// file it names, so the view is captured before that.
bool ZoomExportPrepareResume(const std::filesystem::path &RecordPath, std::string &Error) {
	if (!FileExists(RecordPath.wstring())) {
		Error = Tr("The record does not exist.");
		return false;
	}
	if (!Session.SavedView.Valid) ZoomExportCaptureView();
	SetRecordPath(RecordPath.wstring());
	Session.Frames.clear();
	Session.DoneCount = 0;
	Session.HaveTiming = false;
	Session.LastFrameSeconds = 0.0;
	Session.Finished = false;
	Session.Error.clear();
	if (!ReadRecordFile()) {
		Error = Tr("The record, or the location file it names, cannot be read.");
		return false;
	}
	if (Session.Frames.empty()) {
		Error = Tr("The record does not list any key frame.");
		return false;
	}
	if (Session.Frames.back().Iterations == 0) {
		SetDefaultPrecision(size_t(std::max<uint64_t>(PrecisionForHalfH(DeepestFrameHalfH(Session.Frames)),
			uint64_t(mpf_get_default_prec()))));
		ComputeFrameIterations();
	}
	return true;
}
