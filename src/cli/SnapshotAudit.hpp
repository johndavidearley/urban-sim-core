#pragma once

#include <string>

// Scans `directoryPath` for save snapshot files (*.json, non-recursive) and
// attempts to load + validate each one via SaveLoadSystem::loadSnapshotFromFile,
// printing a per-file status line plus an aggregate summary. Meant for
// auditing a batch of saves at once (e.g. a directory of autosaves or CI
// fixture snapshots) - --inspect-snapshot already covers a single file in
// detail. Returns the process exit code: 0 if every file loaded and
// validated cleanly (including zero files found), 1 if the directory
// couldn't be read or any file failed.
int runSnapshotAudit(const std::string& directoryPath);
