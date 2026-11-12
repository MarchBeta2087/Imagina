#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

enum class FileType {
	Imagina,
	ImaginaText,
	Kfr,
	Kfp,
	Other,
};

void OpenFile(const std::filesystem::path &FileName);
void SaveFile(const std::filesystem::path &FileName, FileType Type);
void SaveImage(const std::filesystem::path &FileName);
void SaveRawPixelData(const std::filesystem::path &FileName);

// Copies the image the pixel manager currently holds, one float per pixel, in
// the order the raw map files store them (row by row, first row of the buffer
// first).  Returns false when the basic pixel manager is in use.
bool GetRawPixelData(std::vector<float> &Pixels, int32_t &Width, int32_t &Height);
