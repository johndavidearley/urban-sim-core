#include "src/cli/CliUtils.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

bool parseBenchmarkFocus(const std::string& raw, BenchmarkFocus& out) {
  std::string value = raw;
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });

  if (value == "ALL") {
    out = BenchmarkFocus::All;
    return true;
  }
  if (value == "GROWTH") {
    out = BenchmarkFocus::Growth;
    return true;
  }
  if (value == "POPULATION") {
    out = BenchmarkFocus::Population;
    return true;
  }
  if (value == "TRAFFIC") {
    out = BenchmarkFocus::Traffic;
    return true;
  }
  if (value == "ECONOMY") {
    out = BenchmarkFocus::Economy;
    return true;
  }
  if (value == "SERVICE") {
    out = BenchmarkFocus::Service;
    return true;
  }
  return false;
}

const char* benchmarkFocusToString(BenchmarkFocus focus) {
  switch (focus) {
    case BenchmarkFocus::All:
      return "ALL";
    case BenchmarkFocus::Growth:
      return "GROWTH";
    case BenchmarkFocus::Population:
      return "POPULATION";
    case BenchmarkFocus::Traffic:
      return "TRAFFIC";
    case BenchmarkFocus::Economy:
      return "ECONOMY";
    case BenchmarkFocus::Service:
      return "SERVICE";
    default:
      return "ALL";
  }
}

std::string csvEscape(const std::string& raw) {
  std::string escaped;
  escaped.reserve(raw.size() + 4);
  escaped.push_back('"');
  for (char c : raw) {
    if (c == '"') {
      escaped.push_back('"');
    }
    escaped.push_back(c);
  }
  escaped.push_back('"');
  return escaped;
}

std::vector<std::string> parseCSVLine(const std::string& line) {
  std::vector<std::string> fields;
  std::string current;
  bool inQuotes = false;

  for (size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (c == '"') {
      if (inQuotes && i + 1 < line.size() && line[i + 1] == '"') {
        current.push_back('"');
        ++i;
      } else {
        inQuotes = !inQuotes;
      }
      continue;
    }

    if (c == ',' && !inQuotes) {
      fields.push_back(current);
      current.clear();
      continue;
    }

    current.push_back(c);
  }

  fields.push_back(current);
  return fields;
}

std::string trimString(const std::string& raw) {
  size_t begin = 0;
  while (begin < raw.size() && std::isspace(static_cast<unsigned char>(raw[begin])) != 0) {
    ++begin;
  }

  size_t end = raw.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(raw[end - 1])) != 0) {
    --end;
  }

  return raw.substr(begin, end - begin);
}

bool parseUint32List(const std::string& raw, std::vector<uint32_t>& outValues) {
  outValues.clear();

  std::stringstream ss(raw);
  std::string token;
  while (std::getline(ss, token, ',')) {
    const std::string trimmed = trimString(token);
    if (trimmed.empty()) {
      continue;
    }

    try {
      const unsigned long value = std::stoul(trimmed);
      outValues.push_back(static_cast<uint32_t>(value));
    } catch (...) {
      return false;
    }
  }

  return !outValues.empty();
}

bool parseInt64List(const std::string& raw, std::vector<int64_t>& outValues) {
  outValues.clear();

  std::stringstream ss(raw);
  std::string token;
  while (std::getline(ss, token, ',')) {
    const std::string trimmed = trimString(token);
    if (trimmed.empty()) {
      continue;
    }

    try {
      const long long value = std::stoll(trimmed);
      outValues.push_back(static_cast<int64_t>(value));
    } catch (...) {
      return false;
    }
  }

  return !outValues.empty();
}

bool parseFloatList(const std::string& raw, std::vector<float>& outValues) {
  outValues.clear();

  std::stringstream ss(raw);
  std::string token;
  while (std::getline(ss, token, ',')) {
    const std::string trimmed = trimString(token);
    if (trimmed.empty()) {
      continue;
    }

    try {
      const float value = std::stof(trimmed);
      outValues.push_back(value);
    } catch (...) {
      return false;
    }
  }

  return !outValues.empty();
}

std::string sanitizeToken(const std::string& raw) {
  std::string out;
  out.reserve(raw.size());
  for (char c : raw) {
    if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_') {
      out.push_back(c);
    } else {
      out.push_back('_');
    }
  }
  return out;
}
