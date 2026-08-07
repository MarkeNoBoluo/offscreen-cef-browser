#pragma once

namespace utils {
    namespace base {
        class NoCopyable {
        protected:
            NoCopyable() = default;
            ~NoCopyable() = default;

            NoCopyable(const NoCopyable&) = delete;
            NoCopyable& operator=(const NoCopyable&) = delete;
        };
    }
}