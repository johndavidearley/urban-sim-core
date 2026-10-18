# School seats and hospital beds (M40)

A normal fully funded Education facility supplies 800 student seats; a Health
facility supplies 400 hospital beds. Existing facility quality and department
funding scale these capacities. Zero funding supplies none. Capacity is derived
from the funded facilities in the service cache, independently of road reach.
No seat or bed fields are added to saves.

## Education

School-age demand is 20% of housed population. Effective education coverage is
`min(road education coverage, seats / school-age demand)`, with zero seats always
producing zero effective coverage. Seats used are school demand times effective
coverage. Sufficient seats preserve the reach-only labor result exactly; zero
seats preserve the uneducated `{50, 35, 15}` income mix. As before, allocation
reads the previous tick's education value. The existing saved education lag now
contains effective coverage, so paused tool edits do not advance that lag.

## Hospitals

Patient demand is housed population times the population-weighted city illness
rate times a patient share of 0.2, floored at one when illness is positive. The
illness rate is a severity index, about 0.5 even under full care, so a fifth of
it needs a bed. At typical illness (about 0.65) one funded 400-bed hospital
serves about 3,000 residents. M45 added the share: before it, half the city
always needed a bed and hospitals bound for the whole game. Hospital capacity scales every
M20 local hospital-distance strength by `min(1, beds / patient demand)` before
illness is evaluated. Pollution, sanitation, crowding, crime and deathcare keep
their existing roles. Beds used are the smaller of beds and city patient demand;
this is a city capacity allocation, without per-patient assignment.

Illness also determines patient demand. NeighborhoodSystem resolves this
coupling on the current residential population: it first evaluates the original
M20 rates, returning those exact rates when beds cover demand. Otherwise, 32
bounded bisection probes solve the shared capacity factor using compact cached
per-building coefficients. There is no new tick lag, BFS, or randomness. A city
with no residents has zero illness and no patient demand. Spare beds do not
extend road reach or remove its distance taper.

## Inspection and continuation

The full and compact HUD show seats/beds used and supplied, and beds also show
patient demand as NEED. Seats and beds each have their own row in the full HUD. CAP means city
capacity binds; REACH means spare capacity leaves road coverage as the limit.
The tile inspector labels these counters CITY, not local assignments.

MCP summary metrics and CLI CSV reports add `school_seats`, `school_demand`,
`school_seats_used`, `effective_education_coverage`, `hospital_beds`,
`patient_demand`, `hospital_beds_used`, and `hospital_capacity_factor`. Existing
`education_coverage` and `health_coverage` remain road-reach measurements. MCP
facility inspection adds `funded_capacity` for schools and hospitals.

Legacy sessions retain their saved building rates and education lag on load;
capacity counters are derived without modifying the checkpoint. The next tick
uses the shared capacity rules in both the CLI and visualizer. Paused ordinary
service/funding edits refresh current rates, while leaving education's lag alone.

## Local validation — October 18, 2026

Strict MSVC Debug builds (core, CLI and Vulkan visualizer) and the strict Release
CLI build passed without warnings. All 604 C++ tests and seven Python unit tests
passed. New tests cover funded capacity, reach limits, zero seats, the patient
floor, office employment, actual-rate hospital demand, sufficient-bed M20 parity,
and the playable education lag. All 36 rendered playtests passed. The new rendered 5,000-resident scenario checks
shortages, funding cuts, added capacity, save/load continuation, and minimum-size
HUD/inspector screenshots under `build/m40-acceptance/`.

Two 64×64, 400-tick Release CLI runs with seed 7 produced byte-identical CSV
reports, including all new capacity counters. Three 500×500, 1,000-tick Release
benchmark trials reported 6.330 ms/tick median (range 6.188–6.448), as a smoke
measurement without a before/after comparison.

Hosted CI, sanitizer jobs and distribution packaging have not been run for M40.
