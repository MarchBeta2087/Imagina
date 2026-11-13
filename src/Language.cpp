#include "Language.h"

#include <cstring>
#include <string>

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

	// The Win32 front-end builds its menus and dialogs from these labels too;
	// LocaliseMenu() drops the "	Ctrl+O" part before the lookup and
	// LocaliseWindow() walks a dialog's controls and translates their text.
	{ Language::Chinese, "Open", "打开" },
	{ Language::Chinese, "Save", "保存" },
	{ Language::Chinese, "Tasks", "任务" },
	{ Language::Chinese, "Reference saving settings", "参考轨道保存设置" },
	{ Language::Chinese, "Custom (Experimental)", "自定义（实验性）" },
	{ Language::Chinese, "Image size", "图像尺寸" },
	{ Language::Chinese, "Low", "低" },
	{ Language::Chinese, "High", "高" },
	{ Language::Chinese, "Quality", "质量" },
	{ Language::Chinese, "Include compressed reference in .im files", "在 .im 文件中包含压缩参考轨道" },
	{ Language::Chinese, "OK", "确定" },
	{ Language::Chinese, "Real:", "实部：" },
	{ Language::Chinese, "Imaginary:", "虚部：" },
	{ Language::Chinese, "Iterations:", "迭代次数：" },
	{ Language::Chinese, "Custom formula", "自定义公式" },
	{ Language::Chinese, "(No information)", "（无信息）" },
	{ Language::Chinese, "Zoom target", "缩放目标" },
	{ Language::Chinese, "Relative", "相对" },
	{ Language::Chinese, "Center", "居中" },
	{ Language::Chinese, "Export record", "导出记录" },
	{ Language::Chinese, "Browse", "浏览" },
	{ Language::Chinese, "Zoom step", "缩放步长" },
	{ Language::Chinese, "Stop at", "停止于" },
	{ Language::Chinese, "Record", "记录" },
	{ Language::Chinese, "Step", "步长" },
	{ Language::Chinese, "Browse...", "浏览..." },
	{ Language::Chinese, "Finished", "已完成" },
	{ Language::Chinese, "key frames", "个关键帧" },
	{ Language::Chinese, "The record does not exist.", "记录文件不存在。" },
	{ Language::Chinese, "The record, or the location file it names, cannot be read.", "记录文件或它指定的位置文件无法读取。" },
	{ Language::Chinese, "The record does not list any key frame.", "记录文件没有列出任何关键帧。" },
	{ Language::Chinese, "The selected stop condition does not produce any key frame.", "选定的停止条件不会产生任何关键帧。" },
	{ Language::Chinese, "Start", "开始" },
	{ Language::Chinese, "Export zoom sequence", "导出缩放序列" },
	{ Language::Chinese, "Resume zoom sequence", "继续缩放序列" },
	{ Language::Chinese, "Exporting zoom sequence", "正在导出缩放序列" },
	{ Language::Chinese, "Error", "错误" },
	{ Language::Chinese, "ChoosePixelFormat failed", "ChoosePixelFormat 失败" },
	{ Language::Chinese, "SetPixelFormat failed", "SetPixelFormat 失败" },

	// Messages of the Win32 export dialogs
	{ Language::Chinese, "All key frames are already exported.\n", "所有关键帧都已导出。\n" },
	{ Language::Chinese, "Cancel the running export?", "取消正在运行的导出？" },
	{ Language::Chinese, "Pick another output file to export them again.", "请选择另一个输出文件以重新导出。" },
	{ Language::Chinese, "Invalid width.", "宽度无效。" },
	{ Language::Chinese, "Invalid height.", "高度无效。" },
	{ Language::Chinese, "Invalid zoom step (it has to be larger than 1).", "缩放步长无效（必须大于 1）。" },
	{ Language::Chinese, "Invalid zoom target.", "缩放目标无效。" },
	{ Language::Chinese, "Value out of range.", "数值超出范围。" },
	{ Language::Chinese, "Zoom target value invalid.", "缩放目标数值无效。" },
	{ Language::Chinese, "Zoom target value out of range.", "缩放目标数值超出范围。" },
	{ Language::Chinese, "Cancelled. The frames written so far are kept.", "已取消。已写入的帧会保留。" },
	{ Language::Chinese, "Choose the export record (*.imz) which is to be continued.", "选择要继续的导出记录（*.imz）。" },
	{ Language::Chinese, "Choose the output file (*.imz) first.", "请先选择输出文件（*.imz）。" },

	// Export status and errors (see ZoomExportStatus)
	{ Language::Chinese, "Frame %zu/%zu, %s zooms, %zu iterations", "第 %zu/%zu 帧，放大 %s 倍，%zu 次迭代" },
	{ Language::Chinese, "reference orbit", "参考轨道" },
	{ Language::Chinese, "starting", "准备中" },
	{ Language::Chinese, "pixels %d%%", "像素 %d%%" },
	{ Language::Chinese, ", ~%s left", "，剩余约 %s" },
	{ Language::Chinese, "The image size is too small.", "图像尺寸太小。" },
	{ Language::Chinese, "The image size is too large (more than 100 megapixels).", "图像尺寸太大（超过一亿像素）。" },
	{ Language::Chinese, "The zoom step has to be larger than 1.", "缩放步长必须大于 1。" },
	{ Language::Chinese, "The zoom target is invalid.", "缩放目标无效。" },
	{ Language::Chinese, "The zoom target is out of range.", "缩放目标超出范围。" },
	{ Language::Chinese, "The record cannot be read.", "无法读取记录文件。" },
	{ Language::Chinese, "The record is empty.", "记录文件为空。" },
	{ Language::Chinese, "Writing or reading back the compressed raw map failed: ", "写入或回读压缩 raw map 失败：" },
	{ Language::Chinese, "the stored pixels differ from the frame", "存储的像素与该帧不一致" },
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
	// The core does not talk to any platform API; the front-ends know the system
	// locale (the SDL one asks SDL_GetPreferredLocales and calls SetLanguage).
	return Language::English;
}

const char *Untranslate(const char *Translated) {
	if (!Translated) return "";
	for (const Translation &Row : Translations) {
		if (std::strcmp(Row.Translated, Translated) == 0) return Row.English;
	}
	return Translated;
}

const char *Tr(const char *English) {
	if (!English) return "";
	if (Active == Language::English) return English;
	for (const Translation &Row : Translations) {
		if (Row.Target == Active && std::strcmp(Row.English, English) == 0) return Row.Translated;
	}
	return English;
}
