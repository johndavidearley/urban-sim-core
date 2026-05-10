#pragma once

#include <random>
#include <cstdint>

class DeterministicRandom {
private:
  std::mt19937 generator;
  
public:
  explicit DeterministicRandom(uint32_t seed = 0) 
    : generator(seed) {}
  
  float uniform(float min = 0.0f, float max = 1.0f) {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(generator);
  }
  
  uint32_t integer(uint32_t min = 0, uint32_t max = UINT32_MAX) {
    std::uniform_int_distribution<uint32_t> dist(min, max);
    return dist(generator);
  }
  
  bool chance(float probability) {
    return uniform() < probability;
  }
};
