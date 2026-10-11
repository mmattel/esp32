// Host test: does every line that goes through logEvent() fit the console mirror?
//
// logEvent() prints a line and hands it to the mirror, which carries at most
// MIRROR_TEXT_LEN characters of it to the coordinator and cuts the rest. Since v3.1.4
// that is 48 - the Zigbee stack refuses a longer string outright - and a cut line
// arrives looking complete. The mirror notes a cut on the console at run time; this
// finds the cuts that are already in the wording, before anything is flashed.
//
// It reads the sources of both sketches, takes the format string of every
// logEvent("...") call, and estimates the longest line it can produce by giving each
// conversion its widest plausible value (see WIDTH below). Each sketch is checked
// against the MIRROR_TEXT_LEN in its own config.h.
//
// A line whose length is proven in code - a static_assert that knows the real
// range of its numbers, as the slot summary has - says so with the marker
// "mirror-len: checked in code" in a comment within the five lines above the call,
// and is listed but not failed.
//
// Where the text a %s inserts has a known bound - a ROM code's tail, a whole ROM
// code - the marker "mirror-len: %s<=N" in the same place says so, and every %s
// in that call counts N characters. The line is then checked like any other.
//
// A format with a %s cannot be bounded from the source - the text comes from
// elsewhere. Those are listed with their length without the strings, and fail only
// when even that is too long. The mirror's run-time note covers the rest.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// The widest plausible output of each conversion, in characters. Generous rather
// than exact: every %u in these sketches is an endpoint, a pin, a slot or a count,
// and three digits covers them all; %lu is a millisecond or a counter.
static int conversionWidth(const std::string &length, char conv, int precision) {
  switch (conv) {
    case 'd':
    case 'i':
    case 'u':
      return length == "l" || length == "ll" ? 10 : 3;
    case 'x':
    case 'X':
      return length == "l" || length == "ll" ? 8 : 4;
    case 'f':
      // Sign, three integer digits, the point and the decimals (2 when not given,
      // which is the widest any of these sketches asks for).
      return 1 + 3 + 1 + (precision >= 0 ? precision : 2);
    case 'c':
      return 1;
    default:
      return 0;  // %s and anything unknown: not bounded here
  }
}

struct Estimate {
  int length = 0;      // characters, with every bounded conversion at its widest
  bool hasString = false;  // a %s, whose length the source cannot tell
};

static Estimate estimate(const std::string &fmt, int stringWidth = -1) {
  Estimate e;
  for (size_t i = 0; i < fmt.size(); i++) {
    if (fmt[i] != '%') {
      e.length++;
      continue;
    }
    if (i + 1 < fmt.size() && fmt[i + 1] == '%') {
      e.length++;
      i++;
      continue;
    }
    // %[flags][width][.precision][length]conversion
    static const std::regex spec(R"(^%[-+ #0]*(\d+|\*)?(?:\.(\d+|\*))?(hh|h|ll|l|z)?([diuxXfcs]))");
    std::smatch m;
    std::string rest = fmt.substr(i);
    if (!std::regex_search(rest, m, spec)) {
      e.length++;
      continue;
    }
    int width = m[1].matched && m[1].str() != "*" ? std::atoi(m[1].str().c_str()) : 0;
    int precision = m[2].matched ? (m[2].str() == "*" ? 2 : std::atoi(m[2].str().c_str())) : -1;
    char conv = m[4].str()[0];
    if (conv == 's' && stringWidth >= 0) {
      e.length += width > stringWidth ? width : stringWidth;
    } else if (conv == 's') {
      e.hasString = true;
    } else {
      int w = conversionWidth(m[3].str(), conv, precision);
      e.length += width > w ? width : w;
    }
    i += m[0].length() - 1;
  }
  return e;
}

// The text of one C string literal, escapes resolved to one character each.
static std::string unescape(const std::string &lit) {
  std::string out;
  for (size_t i = 0; i < lit.size(); i++) {
    if (lit[i] == '\\' && i + 1 < lit.size()) {
      i++;
    }
    out += lit[i];
  }
  return out;
}

struct Call {
  std::string file;
  int line;
  std::string fmt;
  bool checkedInCode;  // the marker is in the five lines above the call
  int stringWidth;     // from "mirror-len: %s<=N" above the call, or -1
};

// Every logEvent("...") in a file, adjacent literals joined as the compiler joins
// them. A call whose first argument is not a literal is not a format this can read.
static std::vector<Call> findCalls(const fs::path &path) {
  std::ifstream in(path);
  std::stringstream ss;
  ss << in.rdbuf();
  const std::string src = ss.str();

  std::vector<Call> calls;
  static const std::regex call(R"(\blogEvent\(\s*")");
  static const std::regex literal(R"re(^\s*"((?:[^"\\]|\\.)*)")re");
  for (auto it = std::sregex_iterator(src.begin(), src.end(), call); it != std::sregex_iterator(); ++it) {
    size_t pos = it->position() + it->length() - 1;  // at the opening quote
    std::string fmt;
    std::smatch m;
    std::string rest = src.substr(pos);
    while (std::regex_search(rest, m, literal)) {
      fmt += unescape(m[1].str());
      rest = rest.substr(m[0].length());
    }
    int line = 1 + (int)std::count(src.begin(), src.begin() + pos, '\n');
    size_t from = it->position();
    for (int n = 0; n < 6 && from > 0; n++) {
      from = src.rfind('\n', from - 1);
      if (from == std::string::npos) {
        from = 0;
      }
    }
    std::string above = src.substr(from, it->position() - from);
    bool marked = above.find("mirror-len: checked in code") != std::string::npos;
    int stringWidth = -1;
    static const std::regex hint(R"re(mirror-len: %s<=(\d+))re");
    std::smatch h;
    if (std::regex_search(above, h, hint)) {
      stringWidth = std::atoi(h[1].str().c_str());
    }
    calls.push_back({path.filename().string(), line, fmt, marked, stringWidth});
  }
  return calls;
}

static int mirrorTextLen(const fs::path &sketch) {
  std::ifstream in(sketch / "config.h");
  std::string l;
  static const std::regex def(R"(^\s*#define\s+MIRROR_TEXT_LEN\s+(\d+))");
  std::smatch m;
  while (std::getline(in, l)) {
    if (std::regex_search(l, m, def)) {
      return std::atoi(m[1].str().c_str());
    }
  }
  return -1;
}

int main() {
  int fails = 0;
  int checked = 0;

  printf("the estimator itself\n");
  struct {
    const char *fmt;
    int length;
    bool hasString;
  } known[] = {
    {"Zigbee connected", 16, false},
    {"%u slots: %u on bus", 3 + 8 + 3 + 7, false},
    {"value %.2f", 6 + 7, false},
    {"%lu ms", 10 + 3, false},
    {"100%% done", 9, false},
    {"%s from Zigbee", 12, true},
  };
  // And with a bound on the inserted text.
  {
    Estimate e = estimate("slot %u (%s) gone", 4);
    bool ok = e.length == 5 + 3 + 2 + 4 + 6 && !e.hasString;
    printf("  %-40s %3d    %s\n", "slot %u (%s) gone   [%s<=4]", e.length, ok ? "ok" : "FAIL");
    if (!ok) fails++;
  }
  for (auto &k : known) {
    Estimate e = estimate(k.fmt);
    bool ok = e.length == k.length && e.hasString == k.hasString;
    printf("  %-40s %3d%s  %s\n", k.fmt, e.length, e.hasString ? "+s" : "  ", ok ? "ok" : "FAIL");
    if (!ok) fails++;
  }

  for (const char *dir : {"../NanoH2_DS18B20_Zigbee", "../NanoH2_WaterFlow_Zigbee"}) {
    fs::path sketch(dir);
    int limit = mirrorTextLen(sketch);
    printf("\n%s (MIRROR_TEXT_LEN %d)\n", sketch.filename().c_str(), limit);
    if (limit <= 0) {
      printf("  MIRROR_TEXT_LEN not found in config.h  FAIL\n");
      fails++;
      continue;
    }
    std::vector<fs::path> files;
    for (auto &entry : fs::directory_iterator(sketch)) {
      std::string ext = entry.path().extension().string();
      if (ext == ".ino" || ext == ".cpp" || ext == ".h") {
        files.push_back(entry.path());
      }
    }
    std::sort(files.begin(), files.end());
    for (auto &f : files) {
      for (auto &c : findCalls(f)) {
        if (c.fmt.empty()) {
          continue;  // logEvent() itself, or a call with no literal format
        }
        checked++;
        Estimate e = estimate(c.fmt, c.stringWidth);
        // Leading spaces are not carried: the mirror drops a console indent.
        size_t indent = c.fmt.find_first_not_of(' ');
        int len = e.length - (int)(indent == std::string::npos ? 0 : indent);
        bool over = len > limit && !c.checkedInCode;
        const char *verdict = over ? "FAIL" : len > limit ? "ok (in code)" : e.hasString ? "ok (+ text)" : "ok";
        if (over) fails++;
        // Only the long ones and the unbounded ones are worth a line each.
        if (len > limit || e.hasString || len > limit - 6) {
          printf("  %-24s %3d%s  %-11s %s\n", (c.file + ":" + std::to_string(c.line)).c_str(), len,
                 e.hasString ? "+s" : "  ", verdict, c.fmt.c_str());
        }
      }
    }
  }

  printf("\n%d logEvent() format(s) checked\n", checked);
  printf("\n%s\n", fails ? "FAILURES" : "ALL PASS");
  return fails ? 1 : 0;
}
