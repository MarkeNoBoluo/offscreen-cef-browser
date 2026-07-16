#pragma once

#include <string>

namespace offscreen {

struct AppConfig {
  std::string initial_url = "https://example.com/";

  static AppConfig FromArgs(int argc, const char* const argv[]);
};

}  // namespace offscreen
