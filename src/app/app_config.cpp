#include "app/app_config.h"

#include <string>

namespace offscreen {
namespace {

constexpr char kUrlPrefix[] = "--url=";

bool StartsWith(const std::string& value, const std::string& prefix) {
  return value.compare(0, prefix.size(), prefix) == 0;
}

}  // namespace

AppConfig AppConfig::FromArgs(int argc, const char* const argv[]) {
  AppConfig config;

  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i] ? argv[i] : "";
    if (!StartsWith(argument, kUrlPrefix)) {
      continue;
    }

    const std::string url = argument.substr(std::string(kUrlPrefix).size());
    if (!url.empty()) {
      config.initial_url = url;
    }
  }

  return config;
}

}  // namespace offscreen
