// The parts of the zoom sequence export that do not need a window: the export
// record (*.imz), the key frame list, the per frame iteration limits and the
// command line helper that prints them.  The front-ends drive this with their own
// timer/loop and their own dialogs (Win32: src/win32/ZoomExport.cpp, SDL:
// src/sdl/SDLMain.cpp).

#include "Includes.h"
#include "ZoomExport.h"

#include "File.h"
#include "RawMapCodec.h"

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
	swprintf(Suffix, 32, L"-%04zu%s", Index, RawMapCodec::FileExtension);
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
