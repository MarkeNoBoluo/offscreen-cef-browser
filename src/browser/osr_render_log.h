#pragma once

#include <cstdint>
#include <string>

namespace offscreen {

// OSR 渲染热路径事件的宽表日志记录。每行对应一条渲染事件；
// timestamp 由写入器在落盘时生成，不进结构体。
struct OsrRenderLogRecord {
  std::string event;  // SetViewRect/GetViewRect/GetScreenInfo/OnPopupShow/OnPopupSize/OnPaint
  int browser_id = -1;
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;  // 主矩形；OnPaint 的 w,h = buffer 物理尺寸
  double scale = 0.0;  // 设备缩放
  std::string type;    // OnPaint: PET_VIEW/PET_POPUP；其他空
  std::string show;    // OnPopupShow: true/false；其他空
  int dirty_count = 0;
  int64_t dirty_area_px = 0;
  std::string detail;  // OnPaint 的 first_dirty 等附加信息
};

/// 设置 OSR 渲染 CSV 日志文件路径。底层 OPEN_ALWAYS 打开；文件为空时
/// 先写 UTF-8 BOM（Excel 兼容）+ 表头行。
/// @param path 目标文件完整路径。
void SetOsrRenderLogFile(const std::wstring& path);

/// 在可执行文件目录创建或追加 offscreen_osr_render.csv。
void SetOsrRenderLogFileToApplicationDirectory();

/// 追加一条 OSR 渲染事件记录（互斥锁 + 常开句柄，追加一行 CSV）。
/// @param record 待写入的宽表记录。
void OsrRenderLogWrite(const OsrRenderLogRecord& record);

/// 返回 CSV 表头行文本（13 列，不含末尾换行）。
/// @return 表头行文本。
std::string FormatOsrRenderCsvHeader();

/// 将记录格式化为单行 CSV（含末尾换行），并做 CSV 转义。
/// @param record 待格式化的记录。
/// @return 单行 UTF-8 CSV 文本。
std::string FormatOsrRenderCsvRow(const OsrRenderLogRecord& record);

}  // namespace offscreen
