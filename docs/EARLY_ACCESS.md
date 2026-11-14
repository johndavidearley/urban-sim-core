# Minimum Steam Early Access milestones

Created November 12, 2026. Updated November 14. Status: **M55 verified locally on Windows; M56/M57 in progress; M58 pending**.
These are project acceptance criteria, not Valve certification requirements.

Ship a playable single-player city builder using the systems already implemented.
Pause feature expansion until these four gates pass. A new system is necessary
only when it fixes a demonstrated blocker in this plan.

Provisional launch scope: **Windows 64-bit, English, keyboard and mouse**, one
vetted starter map/seed, and a complete journey through 10,000 residents.
Windows is a planning assumption, not a verified shipping claim. List additional
operating systems only after their packaged builds pass the same gates.

Local baseline: 764 C++ tests, seven Python tests, targeted Vulkan utility
acceptance and the fresh M55 native player journey pass. Roads, bridges,
currents, pipes, sewer lifts and water boosters are implemented locally.
Windows release readiness and Steam approval remain unverified. Historical
M54 evidence is supplemented by the fresh journey with mandatory pipes.

## M55 — Complete the player journey (verified locally on Windows)

**Owner:** engineering. **Depends on:** existing game systems.

Implementation and evidence are tracked in [M55_PLAYER_JOURNEY.md](M55_PLAYER_JOURNEY.md).
The fresh native journey passes with mandatory pipes, sustainable operations and
actual quit/relaunch/load/resume. November 14 Windows acceptance also passes
exact 720p/1080p layouts and real 144-DPI (150%) scaling on the earned 10k city;
all 13 final captures were visually reviewed. Evidence/build hashes are linked above.

- [x] Update onboarding for roads, zoning, power, water/sewer pipes, budgets,
      and saving. Explain shortages and the action needed to recover; essential
      actions must be discoverable in the interface, not only in a key list.
- [x] Finish basic player settings: display mode/resolution, readable UI at
      1280×720 and 1920×1080 including 150% display scaling, and a controls guide.
      Expose volume controls if audio ships; a new audio system is not a gate.
- [x] Record a fresh native journey on the launch map from an empty city to
      10,000 residents, without debug money, edited saves, CLI construction,
      or developer intervention. Include jobs, functioning utilities and waste
      handling, a recoverable service problem, and a sustainable budget.

**Exit:** the journey reaches 10,000 residents, holds nonnegative operating
cash flow for 100 consecutive ticks, and saves/quits/reloads/resumes successfully.
Retain the itinerary, checkpoints, budget history, and build identifier.
M54 is a reference, not evidence that this new journey already passes.

## M56 — Protect cities and meet a measured performance floor (in progress)

**Owner:** engineering/QA. **Depends on:** M55's representative city saves.

The first save-protection slice is tracked in [M56_RELIABILITY.md](M56_RELIABILITY.md).
M55's Windows display acceptance is locally verified as of November 14;
its earned 10k Windows city is available for performance and soak qualification.

- [x] Provide user-writable save locations, named manual saves, rotating
      autosaves/backups, and clear load errors. Interrupted writes and invalid
      saves must preserve the last good city. Test supported save migrations
      and disclose the update compatibility policy.
      Locally verified November 13; native browser/recovery, schema-migration
      tests and candidate evidence are linked above. Windows qualification is
      still pending; this check does not close the M56 release gate.
- [ ] Run the full automated suite, relevant native construction/utility and
      save-continuation journeys, and sanitizer checks on the release candidate.
      Publish fresh CI results tied to its source revision.
      Local full rendered (57), ASan/UBSan (762) and TSan (762) suites pass on
      November 13; [qualification evidence](M56_QUALIFICATION.md) retains scope
      and logs. This item stays open for fresh hosted CI and platform/leak
      qualification.
- [ ] On a recorded proposed minimum Windows machine, sustain at least 30 FPS
      at 1080p in the 10,000-resident city (95th-percentile frame time ≤33.3 ms),
      with normal-speed simulation keeping up and no accumulating tick backlog.
      Measure rendering and simulation together; historical headless timings
      are insufficient. Publish the tested hardware and supported city/map size.
- [ ] Complete three two-hour packaged-build sessions covering growth,
      utility failures/recovery, save/load, resize, and exit/relaunch, with no
      crashes, lost progress, softlocks, or unbounded memory growth.

**Exit:** no known release-blocking defects; save recovery and the performance
floor have retained evidence on the candidate build. Cosmetic and minor bugs
may remain when documented with workarounds.

## M57 — Make the Steam product installable and accurate (in progress)

**Owner:** engineering for builds; publisher for account/store work.
**Depends on:** M55 for truthful scope; overlaps M55/M56 for account/store setup.

The Windows candidate pipeline, offline Steam preview and outstanding consumer/
publisher gates are tracked in [M57_STEAM_PRODUCT.md](M57_STEAM_PRODUCT.md).
Implementation and local packaging tests do not establish a working Windows
artifact, Steam installation or store approval.

- [ ] Produce a versioned Windows release artifact and Steam depot/launch
      configuration. A clean consumer PC must install and launch from Steam,
      play, save, update, and relaunch without a compiler, Vulkan SDK, Python,
      or repository checkout. Bundle required distributable dependencies and
      license notices; explain unsupported hardware/driver failures clearly.
- [ ] Complete Steamworks onboarding, app setup, pricing, required surveys,
      and applicable disclosure fields. Confirm asset/dependency rights and
      declare only supported languages, platforms, and implemented features.
- [ ] Prepare the required store/library art, real gameplay screenshots,
      description, Early Access questionnaire, current limitations, save
      compatibility policy, support contact, and a short public development
      direction. Marketing must describe the current playable build and avoid
      guarantees about future features or completion dates. See Valve's
      [Early Access rules](https://partner.steamgames.com/doc/store/earlyaccess).
- [ ] Obtain store-page approval and publish Coming Soon. Track its public
      start date and the account/app's fee-payment eligibility date separately.
      New products need at least two weeks of public Coming Soon visibility;
      account onboarding/waiting requirements also apply. Verify dates in
      Steamworks, using [Coming Soon](https://partner.steamgames.com/doc/store/coming_soon)
      and [Steam Direct](https://partner.steamgames.com/steamdirect) as references.

**Exit:** a fresh Steam install works on the declared minimum machine; the
approved store page matches the tested product, and required dates are recorded.
This milestone creates a release candidate, not authorization to publish it.

## M58 — Validate with outside players and release

**Owner:** QA/publisher. **Depends on:** M55–M57.

- [ ] Test the candidate with five players unfamiliar with the project.
      At least four independently establish a serviced, growing settlement
      within 30 minutes using the in-game guide. At least three complete a
      two-hour session and save/reload without developer intervention.
- [ ] Fix every observed blocker: failed install/launch, lost saves, crashes,
      progression dead ends, or instructions that prevent the core loop.
      Recheck affected journeys after fixes; record remaining minor issues.
- [ ] Obtain Valve build approval, resolve review feedback, and confirm all
      Steamworks release checklist items and waiting periods are satisfied.
      Valve recommends allowing at least seven business days for review;
      approval is not guaranteed within that buffer. See the
      [review process](https://partner.steamgames.com/doc/store/review_process).
- [ ] Prepare launch notes, known issues, a bug-report route requesting build
      ID/log/save, and an internally tested hotfix/rollback procedure that
      protects save compatibility. Assign launch support responsibility.

**Exit:** all four gates have linked evidence, the candidate and depot build
IDs are recorded, and the publisher makes the final go/no-go decision before
releasing Early Access. Account actions, submissions, and publication remain
explicit publisher actions; this document does not perform them.

## Critical path and scope limit

Engineering: **M55 → M56 → M57 packaged-build verification → M58**.
Start M57 account/store preparation alongside M55 so Steam's waiting periods
can run while the game is validated. Launch requires both the engineering gates
and Steam eligibility/approvals; no calendar release date is committed here.

Deferred: additional simulation systems, dynamic flooding, more road/utility
variants, extra maps, premium art passes, multiplayer, modding/Workshop,
achievements, Steam Cloud, controller/Deck support, and additional languages or
platforms. Add one only if required to fix a launch blocker or support a feature
actually advertised. Existing advanced systems may ship with documented limits.

Track each milestone as: **open → in progress → verified**, with candidate build,
artifact links, remaining blockers, and sign-off. Automated test totals alone do
not close a milestone. These four gates replace feature-count growth as the
pre-release priority; historical roadmap items remain reference material.
