#include "app/browser_window_title.h"

namespace offscreen {

std::string BrowserCreationFailedTitle(const std::string& error_detail) {
  return "Offscreen CEF Browser - Browser creation failed: " + error_detail;
}

}  // namespace offscreen
