# Library cleanup and logs

`Clean DriverPack.cmd` is a small launcher beside the EXE. It starts **the same executable** in a separate cleanup mode; there is no extra cleaner binary. It selects an available embedded language from the Windows UI preferences.

Cleanup first shows a concrete preview and requires acknowledgement. It keeps the compatible index, supported driver archives, Modern installer/resume data, software catalog and supported installers/icons, logs and legal notices. Other content inside the chosen DriverPack library is eligible for deletion, including the original launcher/UI and advertising. **Do not keep unrelated personal files inside that library.** The original interface will no longer run after conversion. Drive roots, links/junctions and an active original DriverPack process are rejected. Original legal texts are consolidated and checked before their old copies are removed. Cleanup is never automatic.

`logs/native-events.jsonl` records local native scan/install progress and error codes when logging is enabled. JSON Lines allows incremental writing and recovery of complete records if a process stops. It is a diagnostic record, not an index or an installation dependency; no automatic upload occurs.

Use **Tools → Activity log**, or **Settings → General → View activity log**, for a readable view with local timestamps, stages, status, error codes, search and an errors-only filter. Select/copy details or save the filtered report as HTML. The viewer reads a bounded tail and skips incomplete or malformed records. Keep the original file when investigating a failure; review local paths/device details before sharing a report publicly. Disabling logging does not remove existing logs.

## Read-only libraries / ISO

Version 0.22.1 removes the five-second wall-clock query cutoff. Lookups remain bounded by processing work and cooperate with scan cancellation. SQLite opens the source read-only and uses memory for query temporary storage. Matching results are not cached as a machine-specific list in the ISO.

When a library is on read-only media, logs go to a per-library directory under the current user's local application-data folder. The activity viewer resolves that same directory. Software metadata can be read without trying to create a catalog on the disc. Downloading/editing/cleaning a library still requires a writable folder; copy the library to disk for those operations.

A valid library beside a moved EXE takes priority over a stale saved path. An explicit `--library` still wins; a user-picked folder is remembered for that executable location. To update an existing ISO, replace the EXE in the source folder and rebuild the image; installing the new EXE on another disk does not change an already-created ISO.
