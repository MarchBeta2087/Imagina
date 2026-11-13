#include "Language.h"

#include <cstring>
#include <string>

#if __has_include(<SDL3/SDL.h>)
#include <SDL3/SDL.h>
#define IMAGINA_HAS_SDL 1
#endif

namespace {

Language Active = Language::English;

struct Translation {
	Language Target;
	const char *English;
	const char *Translated;
};

// One row per translated string.  Adding a language means adding a column of
// rows here, nothing else: Tr() falls back to the English text.
const Translation Translations[] = {
	// Menus
	{ Language::Chinese, "File", "文件" },
	{ Language::Chinese, "Open...", "打开..." },
	{ Language::Chinese, "Save...", "保存..." },
	{ Language::Chinese, "Save image...", "保存图片..." },
	{ Language::Chinese, "Save raw pixel data...", "保存原始像素数据..." },
	{ Language::Chinese, "Export zoom sequence...", "导出缩放序列..." },
	{ Language::Chinese, "Resume zoom sequence...", "继续缩放序列..." },
	{ Language::Chinese, "Quit", "退出" },

	{ Language::Chinese, "Fractal", "分形" },
	{ Language::Chinese, "Formula", "公式" },
	{ Language::Chinese, "Mandelbrot", "曼德勃罗集" },
	{ Language::Chinese, "Tricorn", "三角兽" },
	{ Language::Chinese, "Burning ship", "燃烧的船" },
	{ Language::Chinese, "Nova", "新星" },
	{ Language::Chinese, "Distance estimation", "距离估计" },
	{ Language::Chinese, "High quality (Experimental)", "高质量（实验性）" },
	{ Language::Chinese, "Location...", "位置..." },
	{ Language::Chinese, "Iteration limit...", "迭代上限..." },
	{ Language::Chinese, "Transformation...", "变换..." },
	{ Language::Chinese, "Increase color density", "提高色彩密度" },
	{ Language::Chinese, "Decrease color density", "降低色彩密度" },
	{ Language::Chinese, "Increase iteration limit", "提高迭代上限" },
	{ Language::Chinese, "Decrease iteration limit", "降低迭代上限" },
	{ Language::Chinese, "Reset location", "重置位置" },
	{ Language::Chinese, "Feature finder...", "特征查找..." },

	{ Language::Chinese, "Computation", "计算" },
	{ Language::Chinese, "Algorithm", "算法" },
	{ Language::Chinese, "Perturbation", "扰动法" },
	{ Language::Chinese, "Linear approximation", "线性近似" },
	{ Language::Chinese, "Recompute", "重新计算" },
	{ Language::Chinese, "Reference", "参考轨道" },
	{ Language::Chinese, "Pixel", "像素" },
	{ Language::Chinese, "All", "全部" },
	{ Language::Chinese, "Lock reference", "锁定参考轨道" },
	{ Language::Chinese, "Tasks...", "任务..." },

	{ Language::Chinese, "Image", "图像" },
	{ Language::Chinese, "Image size...", "图像尺寸..." },
	{ Language::Chinese, "Bilinear filter", "双线性过滤" },
	{ Language::Chinese, "Pre-modulo (workaround for precision loss)", "预取模（精度丢失的变通办法）" },
	{ Language::Chinese, "Palette mipmaps", "调色板 mipmap" },
	{ Language::Chinese, "Gamma correction", "伽马校正" },

	{ Language::Chinese, "Help", "帮助" },
	{ Language::Chinese, "About / License...", "关于 / 许可证..." },
	{ Language::Chinese, "Language", "语言" },
	{ Language::Chinese, "English", "English" },
	{ Language::Chinese, "Chinese (Simplified)", "简体中文" },

	// Dialogs
	{ Language::Chinese, "Image size", "图像尺寸" },
	{ Language::Chinese, "Follow window size", "跟随窗口尺寸" },
	{ Language::Chinese, "Width", "宽度" },
	{ Language::Chinese, "Height", "高度" },
	{ Language::Chinese, "Set window size", "设置窗口尺寸" },
	{ Language::Chinese, "Iteration limit", "迭代上限" },
	{ Language::Chinese, "Iterations", "迭代次数" },
	{ Language::Chinese, "Location", "位置" },
	{ Language::Chinese, "Unit", "单位" },
	{ Language::Chinese, "Half height", "半高" },
	{ Language::Chinese, "Magnification", "放大倍数" },
	{ Language::Chinese, "Depth", "深度" },
	{ Language::Chinese, "Real", "实部" },
	{ Language::Chinese, "Imaginary", "虚部" },
	{ Language::Chinese, "Zoom", "缩放" },
	{ Language::Chinese, "Transformation", "变换" },
	{ Language::Chinese, "Flip imaginary axis", "翻转虚轴" },
	{ Language::Chinese, "Rotation", "旋转" },
	{ Language::Chinese, "Stretch angle", "拉伸角度" },
	{ Language::Chinese, "Stretch ratio", "拉伸比例" },
	{ Language::Chinese, "Apply", "应用" },
	{ Language::Chinese, "Close", "关闭" },
	{ Language::Chinese, "Cancel", "取消" },
	{ Language::Chinese, "Value invalid.", "数值无效。" },

	{ Language::Chinese, "Tasks", "任务" },
	{ Language::Chinese, "Pixels", "像素" },
	{ Language::Chinese, "Threads", "线程" },
	{ Language::Chinese, "computing", "计算中" },
	{ Language::Chinese, "complete", "完成" },
	{ Language::Chinese, "not computed", "未计算" },
	{ Language::Chinese, "ready", "就绪" },

	{ Language::Chinese, "Feature", "特征" },
	{ Language::Chinese, "Go to feature", "跳转到特征" },

	{ Language::Chinese, "About Imagina", "关于 Imagina" },
	{ Language::Chinese, "Imagina - fast fractal renderer & explorer", "Imagina —— 快速分形渲染与探索工具" },
	{ Language::Chinese, "This build uses the SDL3 + Dear ImGui front-end.", "本构建使用 SDL3 + Dear ImGui 前端。" },

	// Messages and command line
	{ Language::Chinese, "Save image", "保存图片" },
	{ Language::Chinese, "Please wait for computations to finish.", "请等待计算完成。" },
	{ Language::Chinese, "Save raw pixel data", "保存原始像素数据" },
	{ Language::Chinese, "Cannot open file", "无法打开文件" },
	{ Language::Chinese, "Cannot write file", "无法写入文件" },
	{ Language::Chinese, "Cannot write image", "无法写入图片" },
	{ Language::Chinese, "Error while loading the file", "加载文件时出错" },
	{ Language::Chinese, "File format not recognized.", "无法识别的文件格式。" },
	{ Language::Chinese, "Unsupported", "不支持" },
	{ Language::Chinese, "The pixels of the frame cannot be read.", "无法读取该帧的像素。" },
};

} // namespace

Language CurrentLanguage() {
	return Active;
}

void SetLanguage(Language Language) {
	Active = Language;
}

const char *LanguageCode(Language Language) {
	switch (Language) {
		case Language::Chinese: return "zh-CN";
		default: return "en";
	}
}

bool LanguageFromCode(std::string_view Code, Language &Result) {
	if (Code == "en" || Code == "en-US" || Code == "en-GB" || Code.empty()) {
		Result = Language::English;
		return true;
	}
	if (Code == "zh" || Code == "zh-CN" || Code == "zh-Hans" || Code == "zh-SG") {
		Result = Language::Chinese;
		return true;
	}
	return false;
}

Language DetectSystemLanguage() {
#ifdef IMAGINA_HAS_SDL
	int Count = 0;
	SDL_Locale **Locales = SDL_GetPreferredLocales(&Count);
	Language Result = Language::English;
	if (Locales) {
		for (int i = 0; i < Count; i++) {
			if (!Locales[i] || !Locales[i]->language) continue;
			std::string Code = Locales[i]->language;
			if (Locales[i]->country) {
				Code += "-";
				Code += Locales[i]->country;
			}
			Language Candidate;
			if (LanguageFromCode(Code, Candidate) && Candidate != Language::English) {
				Result = Candidate;
				break;
			}
			if (LanguageFromCode(Locales[i]->language, Candidate) && Candidate != Language::English) {
				Result = Candidate;
				break;
			}
		}
		SDL_free(Locales);
	}
	return Result;
#else
	return Language::English;
#endif
}

const char *Tr(const char *English) {
	if (!English) return "";
	if (Active == Language::English) return English;
	for (const Translation &Row : Translations) {
		if (Row.Target == Active && std::strcmp(Row.English, English) == 0) return Row.Translated;
	}
	return English;
}
