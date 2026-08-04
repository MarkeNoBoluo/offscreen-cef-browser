#include "browser/osr_render_log.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include <windows.h>

namespace {

template <typename A, typename B>
void expect_eq(A actual, B expected, const char* label) {
  if (actual != expected) {
    std::cerr << label << " expected [" << expected << "] but got [" << actual
              << "]\n";
    std::exit(1);
  }
}

void expect_true(bool actual, const char* label) {
  if (!actual) {
    std::cerr << label << " expected true but got false\n";
    std::exit(1);
  }
}

// --- 表头 ---

void test_format_header() {
  const std::string header = offscreen::FormatOsrRenderCsvHeader();
  expect_eq(header,
            "timestamp,event,browser_id,x,y,w,h,scale,type,show,dirty_count,"
            "dirty_area_px,detail",
            "csv header");
}

// --- 单行格式化：数值列、type/show 列 ---

void test_format_row_onpaint() {
  offscreen::OsrRenderLogRecord record;
  record.event = "OnPaint";
  record.browser_id = 7;
  record.x = 0;
  record.y = 0;
  record.w = 1920;
  record.h = 1080;
  record.scale = 1.0;
  record.type = "PET_VIEW";
  record.dirty_count = 3;
  record.dirty_area_px = 5000;
  record.detail = "first_dirty=10,20 100x50";

  const std::string row = offscreen::FormatOsrRenderCsvRow(record);
  expect_true(row.find("OnPaint") != std::string::npos, "row contains event");
  expect_true(row.find(",7,") != std::string::npos, "row contains browser_id");
  expect_true(row.find("1920,1080") != std::string::npos, "row contains w,h");
  expect_true(row.find("PET_VIEW") != std::string::npos, "row contains type");
  expect_true(row.find("3,5000") != std::string::npos,
              "row contains dirty columns");
  // 时间戳前缀为 HH:MM:SS.mmm（第 3 个字符是冒号）。
  expect_true(row.size() > 12, "row has timestamp prefix");
  expect_true(row[2] == ':', "timestamp format HH:MM:SS.mmm");
  // detail 含逗号 → 双引号包裹。
  expect_true(row.find("\"first_dirty=10,20 100x50\"") != std::string::npos,
              "detail quoted");
}

// --- 单行格式化：默认空列 ---

void test_format_row_empty_columns() {
  offscreen::OsrRenderLogRecord record;  // 全默认
  const std::string row = offscreen::FormatOsrRenderCsvRow(record);
  // 默认值：event/type/show/detail 空，browser_id=-1，x/y/w/h=0，scale=0，dirty 列=0。
  // 行形如 HH:MM:SS.mmm,,-1,0,0,0,0,0,,,0,0,\n。
  expect_true(row.find(",-1,0,0,0,0,0,,,") != std::string::npos,
              "numeric defaults and empty type/show");
  expect_true(row.find("0,0,\n") != std::string::npos, "empty detail tail");
}

// --- 单行格式化：引号转义 ---

void test_format_row_escaping() {
  offscreen::OsrRenderLogRecord record;
  record.event = "OnPaint";
  record.detail = "a,b\"c";
  const std::string row = offscreen::FormatOsrRenderCsvRow(record);
  expect_true(row.find("\"a,b\"\"c\"") != std::string::npos,
              "detail escaped with doubled quote");
}

// --- 落盘：表头 + 行数 + 内容，结束后清理临时文件 ---

void test_write_to_disk() {
  wchar_t temp_dir[MAX_PATH] = {};
  ::GetTempPathW(MAX_PATH, temp_dir);
  wchar_t temp_file[MAX_PATH] = {};
  ::GetTempFileNameW(temp_dir, L"osr", 0, temp_file);
  const std::wstring path(temp_file);

  offscreen::SetOsrRenderLogFile(path);

  offscreen::OsrRenderLogRecord record;
  record.event = "OnPaint";
  record.browser_id = 1;
  record.w = 100;
  record.h = 50;
  record.scale = 1.0;
  record.type = "PET_VIEW";
  record.dirty_count = 2;
  record.dirty_area_px = 500;
  offscreen::OsrRenderLogWrite(record);
  offscreen::OsrRenderLogWrite(record);

  std::ifstream in(path, std::ios::binary);
  expect_true(in.is_open(), "log file opened for read");

  std::string header_line;
  std::getline(in, header_line);
  if (header_line.size() >= 3 &&
      static_cast<unsigned char>(header_line[0]) == 0xEF &&
      static_cast<unsigned char>(header_line[1]) == 0xBB &&
      static_cast<unsigned char>(header_line[2]) == 0xBF) {
    header_line = header_line.substr(3);
  }
  expect_eq(header_line, offscreen::FormatOsrRenderCsvHeader(),
            "file header line");

  std::string row1;
  std::string row2;
  std::getline(in, row1);
  std::getline(in, row2);
  expect_true(row1.find("OnPaint") != std::string::npos, "row1 contains event");
  expect_true(row2.find("OnPaint") != std::string::npos, "row2 contains event");
  expect_true(row1.find("100,50") != std::string::npos, "row1 contains size");
  expect_true(row1.find("2,500") != std::string::npos, "row1 dirty columns");

  std::string extra;
  expect_true(!std::getline(in, extra), "no extra lines after two rows");

  in.close();
  ::DeleteFileW(path.c_str());
}

}  // namespace

int main() {
  test_format_header();
  test_format_row_onpaint();
  test_format_row_empty_columns();
  test_format_row_escaping();
  test_write_to_disk();
  return 0;
}
