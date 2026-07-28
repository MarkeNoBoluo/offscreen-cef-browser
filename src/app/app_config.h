#pragma once

#include <string>

namespace offscreen {

/// 应用启动配置，集中保存可由命令行覆盖的选项。
struct AppConfig {
  /// 首个标签页加载的绝对地址。
  std::string initial_url = "https://baidu.com/";

  /// 从进程参数解析启动配置。
  /// @param argc 参数个数。
  /// @param argv 参数字符串数组。
  /// @return 解析成功或保留默认值后的配置。
  static AppConfig FromArgs(int argc, const char* const argv[]);
};

}  // namespace offscreen
