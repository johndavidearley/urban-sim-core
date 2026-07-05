#include "src/cli/SnapshotAudit.hpp"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <system_error>
#include <vector>

#include "src/persistence/SaveLoadSystem.hpp"

int runSnapshotAudit(const std::string& directoryPath) {
  namespace fs = std::filesystem;

  std::error_code ec;
  if (!fs::is_directory(directoryPath, ec) || ec) {
    std::cerr << "Error: '" << directoryPath << "' is not a readable directory\n";
    return 1;
  }

  std::vector<fs::path> files;
  for (const auto& entry : fs::directory_iterator(directoryPath, ec)) {
    if (ec) break;
    if (entry.is_regular_file() && entry.path().extension() == ".json") {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());

  if (files.empty()) {
    std::cout << "No .json snapshot files found in '" << directoryPath << "'\n";
    return 0;
  }

  std::cout << "Auditing " << files.size() << " snapshot file" << (files.size() == 1 ? "" : "s")
            << " in '" << directoryPath << "':\n\n";

  size_t okCount = 0;
  size_t migratedCount = 0;
  size_t failedCount = 0;

  for (const fs::path& file : files) {
    CitySnapshot snapshot;
    SnapshotLoadDiagnostics diagnostics;
    const bool loaded = SaveLoadSystem::loadSnapshotFromFile(file.string(), snapshot, &diagnostics);

    std::cout << "  " << std::left << std::setw(40) << file.filename().string() << std::right;

    if (!loaded) {
      std::cout << "FAILED";
      if (diagnostics.sourceVersion >= 0) {
        std::cout << " (parsed as version " << diagnostics.sourceVersion << ", failed migration/validation)";
      } else {
        std::cout << " (unreadable or malformed)";
      }
      std::cout << "\n";
      ++failedCount;
      continue;
    }

    std::cout << "OK";
    if (diagnostics.migrationApplied) {
      std::cout << "  (migrated " << diagnostics.sourceVersion << " -> " << diagnostics.targetVersion
                << " via " << diagnostics.migrationPath << ")";
      ++migratedCount;
    }
    std::cout << "  tiles=" << snapshot.tiles.size()
              << " buildings=" << snapshot.buildings.size()
              << " populationGroups=" << snapshot.populationGroups.size()
              << " roads=" << snapshot.roads.size() << "\n";
    ++okCount;
  }

  std::cout << "\nSummary: " << okCount << " OK";
  if (migratedCount > 0) {
    std::cout << " (" << migratedCount << " on an older schema, auto-migrated)";
  }
  std::cout << ", " << failedCount << " failed, " << files.size() << " total\n";

  return failedCount > 0 ? 1 : 0;
}
