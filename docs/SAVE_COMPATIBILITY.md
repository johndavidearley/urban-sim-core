# Save locations, recovery and compatibility

This is the current development-build policy. It must be reviewed against the
packaged Early Access candidate before publication.

Normal play uses the OS user-data folder listed in [M56_RELIABILITY.md](M56_RELIABILITY.md).
F5 saves the active named city (initially `City`). F9 tries that city, its three
backups, then Autosave and its three backups. The start screen's Quick Load
starts with City; **F11 / Saved Cities** selects a named save or an exact backup.
The in-game browser creates custom names and confirms replacing an existing
slot or loading over unsaved progress. A recovery toast identifies when
an older generation or autosave was selected. Autosaves occur every 200 completed
simulation ticks, not while paused without stepping. Save-and-quit must finish
saving before exiting; on failure the city remains open.

Each slot manifest points to a generation directory containing `session.json`
and `session.json.city.json`. Keep the manifest and the complete `objects/`
directory together when copying saves or submitting a bug report. Copying only
the manifest is not a usable backup. A failed load does not replace the running
city. A failed save leaves the previous complete generation available.

Supported formats in this build:

| Format | Accepted versions | Behavior |
| --- | --- | --- |
| Player save-store manifest | 1 | Validates the complete referenced generation; tries retained backups if invalid. |
| Gameplay session | 1 | Validates before applying; documented optional legacy fields use their compatibility defaults. |
| Core city snapshot | 0 and 1 | Version 0 migrates to version 1 in memory; missing legacy version is treated as 0. |

Loading does not rewrite the source. A subsequent managed save creates a new
current-format generation. The older source or retained backup stays available
until normal backup rotation reclaims it. Legacy working-directory saves are
read only as the initial import when no managed City/Autosave slot exists.
The original files are not deleted by that import.

Newer or unknown schemas are rejected with an error; do not open a newer save
in an older build and expect downgrade support. Early Access updates may require
save migrations. Until a release declares and tests a migration, compatibility
with that release is not promised. Any incompatible update must identify the
last compatible build and explain how to keep/export an older city before the
update. Never publish a silent destructive conversion.

Names permit up to 48 ASCII letters, numbers, spaces, underscores and hyphens;
leading/trailing spaces, Windows device names and the manual name Autosave are
rejected. The browser matches existing names without regard to letter case.
Names are labels for separate slots; creating a new name leaves existing cities
intact. Selecting a named city also selects its F5/save-and-quit destination.

Current limits: in-game quickload requires the running map size; use the
start-screen Saved Cities browser when changing map sizes. Concurrent writers from two
game processes are unsupported. Corrupt slot manifests conservatively suppress
orphan cleanup, so damaged stores can retain extra files. Application-write
recovery does not replace an external backup or protect against hardware failure.

Regression coverage includes version-0 snapshot migration, future-version
rejection, legacy optional fields, invalid-load atomicity, paired-generation
validation, backup recovery and relocation. Retained candidate results belong in
[M56_RELIABILITY.md](M56_RELIABILITY.md).
