#ifndef NVX_PRINTER_H_
#define NVX_PRINTER_H_

/**
 * This code is based on Google benchmark's colorprint, commit 05d8c1c.
 * github.com/google/benchmark/blob/master/src/colorprint.cc
 */

#include <assert.h>
#include <cstdarg>
#include <iostream>
#include <memory>
#include <string>

namespace __nvx {

enum LogColor {
  COLOR_DEFAULT,
  COLOR_RED,
  COLOR_GREEN,
  COLOR_YELLOW,
  COLOR_BLUE,
  COLOR_MAGENTA,
  COLOR_CYAN,
  COLOR_WHITE
};

class NVXPrinter {
public:
  void ColorPrintf(std::ostream &out, LogColor color, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ColorPrintf(out, color, fmt, args);
    va_end(args);
  }

  // NVXPrinter (std::string prefix, uint32_t debug_level)
  // : _prefix(prefix), _debug_level(debug_level) {}

private:
  // std::string _prefix;
  // uint32_t _debug_level;

  const char *GetPlatformColorCode(LogColor color) {
    switch (color) {
    case COLOR_RED:
      return "1";
    case COLOR_GREEN:
      return "2";
    case COLOR_YELLOW:
      return "3";
    case COLOR_BLUE:
      return "4";
    case COLOR_MAGENTA:
      return "5";
    case COLOR_CYAN:
      return "6";
    case COLOR_WHITE:
      return "7";
    default:
      return nullptr;
    };
  }

  std::string FormatString(const char *msg, va_list args) {
    // we might need a second shot at this, so pre-emptivly make a copy
    va_list args_cp;
    va_copy(args_cp, args);

    std::size_t size = 256;
    char local_buff[256];
    auto ret = vsnprintf(local_buff, size, msg, args_cp);

    va_end(args_cp);

    // currently there is no error handling for failure, so this is hack.
    assert(ret >= 0);

    if (ret == 0) // handle empty expansion
      return {};
    else if (static_cast<size_t>(ret) < size)
      return local_buff;
    else {
      // we did not provide a long enough buffer on our first attempt.
      size = (size_t)ret + 1; // + 1 for the null byte
      std::unique_ptr<char[]> buff(new char[size]);
      ret = vsnprintf(buff.get(), size, msg, args);
      assert(ret > 0 && ((size_t)ret) < size);
      return buff.get();
    }
  }

  std::string FormatString(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    auto fmtstr = FormatString(msg, args);
    va_end(args);
    return fmtstr;
  }

  void ColorPrintf(std::ostream &out, LogColor color, const char *fmt,
                   va_list args) {
    auto color_code = GetPlatformColorCode(color);
    if (color_code)
      out << FormatString("\033[0;3%sm", color_code);
    out << FormatString(fmt, args) << "\033[m";
  }
}; // class NVXPrinter

} // namespace __nvx

#endif // NVX_PRINTER_H_
