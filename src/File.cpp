#include "Includes.h"
#include "File.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <png.h>
#include <stack>
#include <string>

// Temporary file format
constexpr uint64_t IMMagicNumber = 0x000A0D56504D49FF;

struct IMFileHeader {
	uint64_t Magic;
	uint64_t Reserved;
	uint64_t LocationOffset;
	uint64_t ReferenceOffset;
};

// The .im file stores GMP integers the way mpz_out_raw() does: a four byte big
// endian byte count (negated when the value is negative) followed by the
// magnitude, most significant byte first.  MPIR produces the same layout, so
// files written by either library can be read.
static size_t WriteRawInteger(std::ostream &File, mpz_srcptr Value) {
	// GMP writes no payload at all for zero, so the count is zero as well.
	size_t Bytes = mpz_sgn(Value) ? size_t((mpz_sizeinbase(Value, 2) + 7) / 8) : 0;
	int32_t Count = int32_t(Bytes);
	if (mpz_sgn(Value) < 0) Count = -Count;

	uint8_t Header[4] = {
		uint8_t(uint32_t(Count) >> 24), uint8_t(uint32_t(Count) >> 16),
		uint8_t(uint32_t(Count) >> 8), uint8_t(uint32_t(Count))
	};
	File.write((const char *)Header, sizeof(Header));

	std::vector<uint8_t> Magnitude(Bytes, 0);
	if (Bytes) {
		size_t Written = 0;
		mpz_export(Magnitude.data(), &Written, 1, 1, 1, 0, Value);
	}
	File.write((const char *)Magnitude.data(), std::streamsize(Bytes));
	return Bytes + 4;
}

static size_t ReadRawInteger(std::istream &File, mpz_ptr Value) {
	uint8_t Header[4] = {};
	File.read((char *)Header, sizeof(Header));
	int32_t Count = int32_t(uint32_t(Header[0]) << 24 | uint32_t(Header[1]) << 16 |
		uint32_t(Header[2]) << 8 | uint32_t(Header[3]));

	bool Negative = Count < 0;
	size_t Bytes = Negative ? size_t(-int64_t(Count)) : size_t(Count);

	std::vector<uint8_t> Magnitude(Bytes, 0);
	if (Bytes) File.read((char *)Magnitude.data(), std::streamsize(Bytes));
	mpz_import(Value, Bytes, 1, 1, 1, 0, Magnitude.data());
	if (Negative) mpz_neg(Value, Value);
	return Bytes + 4;
}

static size_t WriteRawFloat(std::ostream &File, mpf_srcptr Value) {
	long int Exponent = Value->_mp_exp;
	File.write((char *)&Exponent, sizeof(Exponent));

	int Size = Value->_mp_size;
	mpz_t Mantissa;
	Mantissa->_mp_alloc = std::abs(Size);
	Mantissa->_mp_size = Size;
	Mantissa->_mp_d = Value->_mp_d;

	return WriteRawInteger(File, Mantissa) + sizeof(Exponent);
}

static void ReadRawFloat(std::istream &File, mpf_ptr Value) {
	long int Exponent = 0;
	File.read((char *)&Exponent, sizeof(Exponent));

	mpz_t Mantissa;
	mpz_init(Mantissa);
	ReadRawInteger(File, Mantissa);
	mpf_set_z(Value, Mantissa);
	Value->_mp_exp = Exponent;
	mpz_clear(Mantissa);
}

// Lower case extension without the leading dot, used to tell Kalle's
// fraktaler files apart.
static std::string FileExtension(const std::filesystem::path &FileName) {
	std::string Extension = FileName.extension().string();
	if (!Extension.empty() && Extension[0] == '.') Extension.erase(0, 1);
	for (char &Character : Extension) Character = char(std::tolower((unsigned char)Character));
	return Extension;
}

FileType DetermineFileType(std::istream &File, std::string_view Extension) {
	using enum FileType;
	char temp[64];
	File.seekg(0, std::ios::beg);
	File.get(temp, 64);
	File.seekg(0, std::ios::beg);

	uint64_t Magic;
	File.read((char *)&Magic, sizeof(uint64_t));
	File.seekg(0, std::ios::beg);

	FileType Type = Other;

	if (Magic == IMMagicNumber) {
		Type = Imagina;
	} else {
		for (char *p = temp; *p; p++) if (*p == ':') Type = ImaginaText;
		if (Type == ImaginaText) {
			if (Extension == "kfp") Type = Kfp;
			if (Extension == "kfr") Type = Kfr;
		}
	}

	return Type;
}

void LoadImFile(std::istream &File) {
	FContext.InvalidatePixel();
	IMFileHeader Header;
	File.read((char *)&Header, sizeof(IMFileHeader));

	File.seekg(Header.LocationOffset);

	File.read((char *)&FContext.CurrentLocation.HalfH, sizeof(HRReal));
	File.read((char *)&Global::ItLim, sizeof(size_t));

	FContext.CurrentLocation.X = 0.0_hr;
	FContext.CurrentLocation.Y = 0.0_hr;
	FContext.RenderLocation = FContext.CurrentLocation;
	FContext.EvalLocation = FContext.CurrentLocation;

	uint64_t Precision = -std::min<int64_t>(0, FContext.CurrentLocation.HalfH.Exponent) + 64;
	SetDefaultPrecision(Precision);
	FContext.CenterCoordinate.X.set_prec(Precision);
	FContext.CenterCoordinate.Y.set_prec(Precision);
	FContext.pixelManager.Clear();
	FContext.ComputePixel = true;

	ReadRawFloat(File, FContext.CenterCoordinate.X.get_mpf_t());
	ReadRawFloat(File, FContext.CenterCoordinate.Y.get_mpf_t());

	if (Header.ReferenceOffset) {
		File.seekg(Header.ReferenceOffset);

		//FContext.evaluator->LoadReference(OpenFile, FContext.reference);
		if (FContext.reference) FContext.evaluator->FreeReference(FContext.reference);
		FContext.reference = nullptr;

		FContext.evaluator->LoadReference(File, FContext.reference);
	} else {
		FContext.InvalidateReference();
	}
}

const std::unordered_map<std::string, FractalTypeEnum> FormulaNameMap = {
	{ "Mandelbrot", FractalTypeEnum::Mandelbrot },
	{ "Tricorn", FractalTypeEnum::Tricorn },
	{ "Burning ship", FractalTypeEnum::BurningShip },
	{ "Nova", FractalTypeEnum::Nova },
};

// Workaround for a bug of MPIR
void CorrectMpf(mpf_ptr mp) {
	bool sign = mp->_mp_size < 0;
	mp->_mp_size = abs(mp->_mp_size);
	while (mp->_mp_size && !mp->_mp_d[mp->_mp_size - 1]) { mp->_mp_size--; mp->_mp_exp--; }
	mp->_mp_size = sign ? -mp->_mp_size : mp->_mp_size;
}

void LoadImtFile(std::istream &File) {
	FContext.InvalidateAll();
	std::unordered_map<std::string, std::string> Parameters;
	std::string KeyAndIndentation, Key, Value;
	struct BlockInfo {
		std::string Indentation;
		std::string Prefix;
	};
	std::stack<BlockInfo> Blocks;
	while (std::getline(File, KeyAndIndentation, ':').good() && std::getline(File, Value).good()) {
		size_t IndentationSize = 0;
		while (IndentationSize < KeyAndIndentation.size() && (KeyAndIndentation[IndentationSize] == ' ' || KeyAndIndentation[IndentationSize] == '\t')) {
			IndentationSize++;
		}
		std::string_view Indentation = std::string_view(KeyAndIndentation).substr(0, IndentationSize);
		std::string_view BlockIndentation;
		if (Blocks.empty()) {
			BlockIndentation = std::string_view();
		} else {
			BlockIndentation = std::string_view(Blocks.top().Indentation);
		}
		size_t BlockIndentationSize = BlockIndentation.size();
		if (IndentationSize > BlockIndentationSize) {
			if (Indentation.substr(0, BlockIndentationSize) != BlockIndentation) throw;
			Blocks.push(BlockInfo{ std::string(Indentation), Key + '.' });
		} else if (IndentationSize < BlockIndentationSize) {
			while (!Blocks.empty() && Blocks.top().Indentation.size() > IndentationSize) Blocks.pop();
			if (Blocks.empty()) {
				if (Indentation.size() != 0) throw;
			} else if (Indentation != Blocks.top().Indentation) throw;
		} else if (Indentation != BlockIndentation) {
			throw;
		}

		size_t WhitespaceCount = 0;
		while (WhitespaceCount < Value.size() && (Value[WhitespaceCount] == ' ' || Value[WhitespaceCount] == '\t')) {
			WhitespaceCount++;
		}
		Key = Blocks.empty() ? std::string() : Blocks.top().Prefix;
		Key.append(std::string_view(KeyAndIndentation).substr(IndentationSize));
		Parameters.try_emplace(Key, Value.substr(WhitespaceCount));
	}
	if (auto FormulaText = Parameters.find("Formula"); FormulaText != Parameters.end()) {
		if (auto Formula = FormulaNameMap.find(FormulaText->second); Formula != FormulaNameMap.end()) {
		} else {
			Global::CustomFormula = FormulaText->second;
			SetFractalType(FractalTypeEnum::Custom);
		}
	}
	// TODO: Restore previous values in case of exception
	if (Parameters.contains("Location")) {
		try {
			HRReal HalfH = HRReal(mpf_class(Parameters.at("Location.Size"), 52, -16));

			if (HalfH > 16.0_hr) HalfH = 16.0_hr;
			uint64_t Precision = -std::min<int64_t>(0, HalfH.Exponent) + 64;

			Coordinate NewCoordinate{ mpf_class(Parameters.at("Location.Re"), Precision, 16), mpf_class(Parameters.at("Location.Im"), Precision, -16) };

			CorrectMpf(NewCoordinate.X.get_mpf_t()); // Workaround for a bug of MPIR
			CorrectMpf(NewCoordinate.Y.get_mpf_t());

			//FContext.ZoomToAnimated(NewCoordinate, Precision, HalfH);
			FContext.SetLocation(NewCoordinate, Precision, HalfH);

			size_t NewItLim;
			char Dummy;
			if (sscanf(Parameters.at("Location.Iterations").c_str(), "%zu%c", &NewItLim, &Dummy) == 1) {
				Global::ItLim = std::max(UINT64_C(2), std::min(UINT64_C(1) << 32, NewItLim));
			}

		} catch (std::out_of_range) {
			ErrorMessage("Error while loading location", "Argument missing");
		} catch (std::invalid_argument) {
			ErrorMessage("Error while loading location", "Invalid argument");
		}
	}
}

void LoadKfFile(std::istream &File, bool IsKfr) {
	FContext.InvalidateAll();
	std::unordered_map<std::string, std::string> Parameters;
	std::string Key, Value;
	while (std::getline(File, Key, ':').good() && std::getline(File, Value).good()) {
		Parameters.try_emplace(Key, Value);
	}
	// TODO: Restore previous values in case of exception
	if (IsKfr) {
		if (auto Zoom = Parameters.find("Zoom"); Zoom != Parameters.end()) {
			try {
				HRReal HalfH = 2.0_hr / HRReal(mpf_class(Zoom->second, 52));

				if (HalfH > 16.0_hr) HalfH = 16.0_hr;
				uint64_t Precision = -std::min<int64_t>(0, HalfH.Exponent) + 64;

				Coordinate NewCoordinate{ mpf_class(Parameters.at("Re"), Precision), mpf_class(Parameters.at("Im"), Precision) };

				CorrectMpf(NewCoordinate.X.get_mpf_t()); // Workaround for a bug of MPIR
				CorrectMpf(NewCoordinate.Y.get_mpf_t());

				FContext.SetLocation(NewCoordinate, Precision, HalfH);

				size_t NewItLim;
				char Dummy;
				if (sscanf(Parameters.at("Iterations").c_str(), "%zu%c", &NewItLim, &Dummy) == 1) {
					Global::ItLim = std::max(UINT64_C(2), std::min(UINT64_C(1) << 32, NewItLim));
				}

			} catch (std::out_of_range) { // Parameter not found
				// TODO
			} catch (std::invalid_argument) {

			}
		}
	}

	if (auto Colors = Parameters.find("Colors"); Colors != Parameters.end()) {
		const char *ColorsString = Colors->second.c_str();
		int NumberOfArguments, NumberOfCharacters;
		RGBA Color;
		Color.A = 1.0;
		Global::Palette.clear();
		while ((NumberOfArguments = sscanf(ColorsString, "%f,%f,%f,%n", &Color.B, &Color.G, &Color.R, &NumberOfCharacters)) == 3) {
			Color.R *= 1.0f / 255.0f;
			Color.G *= 1.0f / 255.0f;
			Color.B *= 1.0f / 255.0f;
			Global::Palette.push_back(Color);
			ColorsString += NumberOfCharacters;
		}
		if (Global::Palette.size() == 0) { // TODO: Restore default palette
			Global::Palette.push_back(RGBA{ 0.0, 0.0, 0.0, 1.0 });
		}
		Global::GammaCorrection = false;
		SetPalette(Global::Palette);
	}
	if (auto IterDiv = Parameters.find("IterDiv"); IterDiv != Parameters.end()) {
		double ItDiv;
		sscanf(IterDiv->second.c_str(), "%lf", &ItDiv);
		Global::ItDiv = ItDiv * (UseDE ? 8192.0 : 1024.0);
	}
}

void OpenFile(const std::filesystem::path &FileName) {
	using enum FileType;
	std::ifstream Input(FileName, std::ifstream::in | std::ifstream::binary);
	if (!Input) {
		ErrorMessage("Cannot open file", FileName.string().c_str());
		return;
	}

	FileType Type = DetermineFileType(Input, FileExtension(FileName));

	if (Type != Imagina) {
		Input.close();
		Input.open(FileName, std::ifstream::in);
	}

	try {
		switch (Type) {
			case Imagina:	  LoadImFile(Input);	   break;
			case ImaginaText: LoadImtFile(Input);	   break;
			case Kfr:		  LoadKfFile(Input, true);  break;
			case Kfp:		  LoadKfFile(Input, false); break;
			default: {
				ErrorMessage("File format not recognized.");
				break;
			}
		}
	} catch (const std::exception &Error) {
		ErrorMessage("Error while loading the file", Error.what());
	} catch (...) {
		ErrorMessage("Error while loading the file");
	}

	Input.close();
}

void SaveKfrFile(std::ostream &File) {

	size_t NumberOfDigits = (size_t)std::max(0.0, -log10(FContext.CurrentLocation.HalfH)) + 10;

	File.precision(NumberOfDigits);
	File << std::fixed;

	// GMP only streams mpf_class, MPIR also streams the expression types.
	mpf_class Real = FContext.CenterCoordinate.X + HPReal(FContext.CurrentLocation.X);
	mpf_class Imaginary = FContext.CenterCoordinate.Y + HPReal(FContext.CurrentLocation.Y);
	File << "Re: " << Real << std::endl;
	File << "Im: " << Imaginary << std::endl;

	File.precision(12);
	File << std::defaultfloat;

	File << "Zoom: " << (2.0_hr / FContext.CurrentLocation.HalfH).to_mpf_class(53) << std::endl;
	File << "Iterations: " << Global::ItLim << std::endl;

	File << "IterDiv: " << Global::ItDiv / (UseDE ? 8192.0 : 1024.0) << std::endl;

	if (UseDE) {
		File << "ColorMethod: 7" << std::endl;
		File << "Differences: 7" << std::endl;
	} else {
		File << "ColorMethod: 0" << std::endl;
		File << "Differences: 0" << std::endl;
	}
}

// Temporary file format
void SaveImFile(std::ostream &File) {
	IMFileHeader Header = {};
	Header.Magic = IMMagicNumber;
	File.write((char *)&Header, sizeof(IMFileHeader));
	Header.LocationOffset = File.tellp();

	File.write((char *)&FContext.CurrentLocation.HalfH, sizeof(HRReal));
	File.write((char *)&Global::ItLim, sizeof(size_t));
	WriteRawFloat(File, FContext.CenterCoordinate.X.get_mpf_t());
	WriteRawFloat(File, FContext.CenterCoordinate.Y.get_mpf_t());

	uint64_t ReferenceOffset = File.tellp();
	// Saving before the reference task finished used to dereference a null
	// pointer here.
	if (Global::SaveReference && FContext.reference && FContext.evaluator->SaveReference(File, FContext.reference)) {
		Header.ReferenceOffset = ReferenceOffset;
	} else {
		Header.ReferenceOffset = 0;
	}

	File.seekp(0);
	File.write((char *)&Header, sizeof(IMFileHeader));
}

void SaveImtFile(std::ostream &File) {
	size_t NumberOfDigits = (size_t)std::max<int64_t>(0, -FContext.CurrentLocation.HalfH.Exponent / 4) + 10;

	File << "Formula: ";
	switch (CurrentFractalType) {
		case FractalTypeEnum::Mandelbrot: {
			File << "Mandelbrot";
			break;
		}
		case FractalTypeEnum::Tricorn: {
			File << "Tricorn";
			break;
		}
		case FractalTypeEnum::BurningShip: {
			File << "Burning ship";
			break;
		}
		case FractalTypeEnum::Nova: {
			File << "Nova";
			break;
		}
		case FractalTypeEnum::Custom: {
			File << Global::CustomFormula;
			break;
		}
		default: {
			break;
		}
	}
	File << std::endl;

	File.precision(NumberOfDigits);
	//File << std::hexfloat;
	File.setf(std::ios_base::hex | std::ios_base::fixed, std::ios_base::basefield | std::ios_base::floatfield);
	File << "Location:\n";
	mpf_class Real = FContext.CenterCoordinate.X + HPReal(FContext.CurrentLocation.X);
	mpf_class Imaginary = FContext.CenterCoordinate.Y + HPReal(FContext.CurrentLocation.Y);
	File << "\tRe: " << Real << std::endl;
	File << "\tIm: " << Imaginary << std::endl;

	File.precision(12);
	File.unsetf(std::ios_base::floatfield);
	File << "\tSize: " << (FContext.CurrentLocation.HalfH).to_mpf_class(53) << std::endl;

	File.unsetf(std::ios_base::basefield);
	File << "\tIterations: " << Global::ItLim << std::endl;
}

void SaveFile(const std::filesystem::path &FileName, FileType Type) {
	using enum FileType;

	// Write next to the target and rename into place only when the whole file has
	// been written.  Opening the target directly truncated it before any data
	// was written, so a crash or a full disk destroyed the previous save.
	std::filesystem::path TempName = FileName;
	TempName += ".tmp";

	std::ofstream File;
	bool Binary = Type == Imagina;
	File.open(TempName, std::ofstream::out | (Binary ? std::ofstream::binary : std::ofstream::openmode(0)));
	if (!File) {
		ErrorMessage("Cannot write file", FileName.string().c_str());
		return;
	}

	switch (Type) {
		case Imagina:	  SaveImFile(File);	  break;
		case ImaginaText: SaveImtFile(File);	  break;
		case Kfr:		  SaveKfrFile(File);  break;
		default: break;
	}

	File.close();
	std::error_code Error;
	if (!File) {
		std::filesystem::remove(TempName, Error);
		ErrorMessage("Cannot write file", FileName.string().c_str());
		return;
	}

	std::filesystem::rename(TempName, FileName, Error);
	if (Error) {
		std::filesystem::remove(TempName, Error);
		ErrorMessage("Cannot write file", FileName.string().c_str());
	}
}

static void WritePngData(png_structp Png, png_bytep Data, png_size_t Length) {
	auto *Stream = static_cast<std::ostream *>(png_get_io_ptr(Png));
	Stream->write((const char *)Data, std::streamsize(Length));
}

void SaveImage(const std::filesystem::path &FileName) {
#ifdef USE_BASIC_PIXEL_MANAGER
	ErrorMessage("Unsupported");
#else
	int32_t width = FContext.pixelManager.Width, height = FContext.pixelManager.Height;

	std::ofstream Output(FileName, std::ofstream::out | std::ofstream::binary);
	if (!Output) {
		ErrorMessage("Cannot write image", FileName.string().c_str());
		return;
	}

	png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
	if (!png) {
		ErrorMessage("Cannot write image", "libpng failed to initialise");
		return;
	}

	png_infop info = png_create_info_struct(png);
	if (!info) {
		png_destroy_write_struct(&png, nullptr);
		ErrorMessage("Cannot write image", "libpng failed to initialise");
		return;
	}

	if (setjmp(png_jmpbuf(png))) {
		png_destroy_write_struct(&png, &info);
		ErrorMessage("Cannot write image", FileName.string().c_str());
		return;
	}

	png_set_write_fn(png, &Output, WritePngData, nullptr);

	// Output is 8bit depth, RGBA format.
	png_set_IHDR(
		png,
		info,
		width, height,
		8,
		PNG_COLOR_TYPE_RGBA,
		PNG_INTERLACE_NONE,
		PNG_COMPRESSION_TYPE_DEFAULT,
		PNG_FILTER_TYPE_DEFAULT
	);
	png_write_info(png, info);

	png_bytep *row_pointers = new png_bytep[height];
	png_bytep pixData = new png_byte[width * height * 4]{ 128 };
	memset(pixData, 128, width * height * 4);

	for (int32_t i = 0; i < height; i++) {
		row_pointers[height - 1 - i] = pixData + i * width * 4;
	}

	struct RGB {
		float r, g, b;
	};
	float ItMul = 1.0 / Global::ItDiv;
	float FMaxIt = Global::MaxIt;
	float PaletteSize = Global::Palette.size();
	size_t PaletteSizeI = Global::Palette.size();
	auto ValToRGBA = [&](float val) {
		if (val >= FMaxIt) return RGBA{ 0.0, 0.0, 0.0, 1.0 };
		val *= ItMul;
		val += Global::ColoringValueOffset;
		val -= floor(val);
		val *= PaletteSize;
		size_t index1 = floor(val);
		size_t index2 = index1 + 1;
		if (index2 == PaletteSizeI) index2 = 0;
		val -= floor(val);
		float weight1 = 1.0 - val;
		float weight2 = val;
		RGBA result;
		result.R = Global::Palette[index1].R * weight1 + Global::Palette[index2].R * weight2;
		result.G = Global::Palette[index1].G * weight1 + Global::Palette[index2].G * weight2;
		result.B = Global::Palette[index1].B * weight1 + Global::Palette[index2].B * weight2;
		result.A = Global::Palette[index1].A * weight1 + Global::Palette[index2].A * weight2;
		return result;
	};
	size_t X = FContext.pixelManager.Passes[FContext.pixelManager.PassCount - 1].X;
	size_t Y = FContext.pixelManager.Passes[FContext.pixelManager.PassCount - 1].Y;
	size_t TextureWidth = FContext.pixelManager.TextureWidth;
	for (size_t y = Y, i = 0; y < Y + height; y++) {
		for (size_t x = X; x < X + width; x++, i++) {
			RGBA rgba = ValToRGBA(FContext.pixelManager.Data[y * TextureWidth + x]);
			if (Global::GammaCorrection) {
				rgba.R = sqrt(rgba.R);
				rgba.G = sqrt(rgba.G);
				rgba.B = sqrt(rgba.B);
			}
			pixData[i * 4] = round(rgba.R * 255.0);
			pixData[i * 4 + 1] = round(rgba.G * 255.0);
			pixData[i * 4 + 2] = round(rgba.B * 255.0);
			pixData[i * 4 + 3] = round(rgba.A * 255.0);
		}
	}

	png_write_image(png, row_pointers);
	png_write_end(png, nullptr);

	delete[] row_pointers;
	delete[] pixData;

	Output.close();

	png_destroy_write_struct(&png, &info);
#endif
}

void SaveRawPixelData(const std::filesystem::path &FileName) {
#ifdef USE_BASIC_PIXEL_MANAGER
	ErrorMessage("Unsupported");
#else
	std::ofstream File(FileName, std::ofstream::out | std::ofstream::binary);
	if (!File) {
		ErrorMessage("Cannot write file", FileName.string().c_str());
		return;
	}

	int32_t width = FContext.pixelManager.Width, height = FContext.pixelManager.Height;

	size_t X = FContext.pixelManager.Passes[FContext.pixelManager.PassCount - 1].X;
	size_t Y = FContext.pixelManager.Passes[FContext.pixelManager.PassCount - 1].Y;
	size_t TextureWidth = FContext.pixelManager.TextureWidth;

	for (size_t y = Y; y < Y + height; y++) {
		File.write((const char *)&FContext.pixelManager.Data[y * TextureWidth + X], width * sizeof(float));
	}

	File.close();
#endif
}

bool GetRawPixelData(std::vector<float> &Pixels, int32_t &Width, int32_t &Height) {
#ifdef USE_BASIC_PIXEL_MANAGER
	return false;
#else
	int32_t width = FContext.pixelManager.Width, height = FContext.pixelManager.Height;

	size_t X = FContext.pixelManager.Passes[FContext.pixelManager.PassCount - 1].X;
	size_t Y = FContext.pixelManager.Passes[FContext.pixelManager.PassCount - 1].Y;
	size_t TextureWidth = FContext.pixelManager.TextureWidth;

	Pixels.resize(size_t(width) * size_t(height));
	for (int32_t y = 0; y < height; y++) {
		memcpy(&Pixels[size_t(y) * size_t(width)],
			&FContext.pixelManager.Data[(Y + size_t(y)) * TextureWidth + X], size_t(width) * sizeof(float));
	}

	Width = width;
	Height = height;
	return true;
#endif
}
