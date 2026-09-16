#pragma once
#include <Arduino.h>
#include <map>
// Mirrors the real Preferences float behaviour: stored as a blob, and a miss
// leaves the caller's default (NAN) untouched.
class Preferences {
public:
  std::map<std::string, float> floats;
  int writes = 0;  // NVS is finite, so a test can insist a load wrote nothing
  size_t putFloat(const char *key, float v) { floats[key] = v; writes++; return sizeof(float); }
  float getFloat(const char *key, float def = NAN) {
    auto it = floats.find(key);
    return it == floats.end() ? def : it->second;
  }
};
