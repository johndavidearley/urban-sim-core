#pragma once

struct CrimeParams {
  float baseCrimeRate = 0.10f;          // ambient rate with no aggravating/mitigating factors
  float unemploymentWeight = 0.6f;      // higher unemployment raises crime
  float povertyWeight = 0.4f;           // below-reference land value (a poverty proxy) raises crime
  float referenceLandValue = 200.0f;    // land value at/above which the poverty factor is zero
  float policeCoverageReduction = 0.75f;  // police coverage cuts crime by up to this fraction
};

struct CrimeSummary {
  float overallRate = 0.0f;  // 0-1 city-wide crime rate this tick
};

// A pure read-out, unlike FireSystem: crime has no side effects on the map
// or entity store by itself. CitySimulator feeds CrimeSummary::overallRate
// into the same migration-desirability formula that pollution/congestion/
// service satisfaction already feed - high crime makes a city less
// attractive to move into, exactly the way high pollution or bad traffic
// already do, rather than destroying anything directly.
class CrimeSystem {
public:
  // unemploymentRate and policeCoverage are both already-computed 0-1
  // fractions (PopulationSummary-style unemployment, ServiceCoverageSummary
  // ::policeCoverage); averageLandValue is LandValueSystem's city-wide mean,
  // used as a poverty proxy - crime concentrates where land value is low.
  static CrimeSummary evaluate(
    float unemploymentRate,
    float policeCoverage,
    float averageLandValue,
    const CrimeParams& params = {}
  );
};
