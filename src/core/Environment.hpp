#pragma once
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

// Own the value so callers do not retain pointers into the process environment.
inline std::optional<std::string> environmentValue(const char* name) {
#ifdef _MSC_VER
  char* value=nullptr;
  size_t length=0;
  if(_dupenv_s(&value,&length,name)!=0) return std::nullopt;
  const std::unique_ptr<char,decltype(&std::free)> owned(value,&std::free);
#else
  const char* value=std::getenv(name);
#endif
  if(!value) return std::nullopt;
  return std::string(value);
}
