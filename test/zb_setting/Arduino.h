#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
using std::isnan;
using std::isinf;
#include <string>
typedef std::string String;
#define NVS_STUB 1
struct SerialStub {
  template <typename... A> void printf(const char *f, A... a) { std::printf(f, a...); }
  void println(const char *s = "") { std::printf("%s\n", s); }
};
static SerialStub Serial;
