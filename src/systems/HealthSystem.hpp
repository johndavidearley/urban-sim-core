#pragma once

struct HealthParams {
  float baseIllnessRate = 0.06f;          // ambient rate with no aggravating/mitigating factors
  float densityWeight = 0.35f;            // housing crowding (population/residentialCapacity) raises illness
  float pollutionWeight = 0.5f;           // residential-weighted pollution raises illness
  float healthCoverageReduction = 0.8f;   // hospital coverage cuts illness by up to this fraction
};

struct HealthSummary {
  float illnessRate = 0.0f;  // 0-1 city-wide illness rate this tick
};

// A pure read-out, like CrimeSystem: disease has no side effects on the map
// or entity store by itself. CitySimulator feeds HealthSummary::illnessRate
// into the same migration-desirability formula that crime/pollution/
// congestion/service satisfaction already feed - a sicker city is less
// attractive to move into, rather than directly killing residents (that
// level of fidelity belongs to the opt-in, destructive disaster systems).
class HealthSystem {
public:
  // housingDensity is population/residentialCapacity (0-1, crowding proxy);
  // averagePollution is the same residential-weighted pollution figure
  // CitySimulator already computes for desirability; healthCoverage is
  // ServiceCoverageSummary::healthCoverage, already computed by ServiceSystem
  // every tick.
  static HealthSummary evaluate(
    float housingDensity,
    float averagePollution,
    float healthCoverage,
    const HealthParams& params = {}
  );
};
