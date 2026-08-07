#pragma once

#include <string>

namespace offscreen {

/// 返回当前运行实例的 run_id（YYYYMMDD_HHMMSS）。
/// RunLifecycleBegin 未调用时返回空字符串。
/// @return run_id 文本。
std::string RunLifecycleId();

/// 返回当前运行实例的启动时刻（ISO 8601）。
/// @return 例如 "2026-08-06T15:54:29"。
std::string RunLifecycleStartTime();

/// 开始一次运行的记录：生成 run_id 与 run_start，并在可执行文件目录的
/// offscreen_run_lifecycle_YYYYMMDD_HH.csv 追加一行
/// run_id,run_start,,,0（run_end/exit_code 留空，shutdown_completed=0）。
/// 文件为空时先写 UTF-8 BOM + 表头。
void RunLifecycleBegin();

/// 结束一次运行的记录：追加一行
/// run_id,run_start,run_end,exit_code,shutdown_completed。
/// 供 main 退出路径调用，用于闭环审计（clean shutdown 证据）。
/// @param exit_code 进程退出码。
/// @param shutdown_completed CefRuntime::Shutdown 是否完成。
void RunLifecycleEnd(int exit_code, bool shutdown_completed);

}  // namespace offscreen
