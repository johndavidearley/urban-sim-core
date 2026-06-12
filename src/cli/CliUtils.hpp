#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Phase 5 benchmark focus selection.
enum class BenchmarkFocus {
  All,
  Growth,
  Population,
  Traffic,
  Economy,
  Service
};

bool parseBenchmarkFocus(const std::string& raw, BenchmarkFocus& out);
const char* benchmarkFocusToString(BenchmarkFocus focus);

// CSV helpers shared by the growth-pressure report tooling.
std::string csvEscape(const std::string& raw);
std::vector<std::string> parseCSVLine(const std::string& line);

// String/list parsing helpers for CLI arguments.
std::string trimString(const std::string& raw);
bool parseUint32List(const std::string& raw, std::vector<uint32_t>& outValues);
bool parseInt64List(const std::string& raw, std::vector<int64_t>& outValues);
bool parseFloatList(const std::string& raw, std::vector<float>& outValues);
std::string sanitizeToken(const std::string& raw);
