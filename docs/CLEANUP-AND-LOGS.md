# Library cleanup and logs

`Clean DriverPack.cmd` is a small launcher beside the EXE. It starts **the same executable** in a separate cleanup mode; there is no extra cleaner binary. It selects an available embedded language from the Windows UI preferences.

Cleanup first shows a concrete preview and requires acknowledgement. It keeps the compatible index, supported driver archives, Modern installer/resume data, software catalog and supported installers/icons, logs and legal notices. Other content inside the chosen DriverPack library is eligible for deletion, including the original launcher/UI and advertising. **Do not keep unrelated personal files inside that library.** The original interface will no longer run after conversion. Drive roots, links/junctions and an active original DriverPack process are rejected. Original legal texts are consolidated and checked before their old copies are removed. Cleanup is never automatic.

GUI diagnostics use `%LOCALAPPDATA%/TarekSadek/DriverPackModern/Logs/session-<UTC>-<PID>/`, including when the executable/library is on an ISO. A local temporary-directory fallback is available. Each session contains its own readable trace, engine activity, and a minidump if a crash was captured. At startup, older recognized sessions are pruned; the current session and newest crash are protected. See [crash diagnostics and retention](CRASH_DIAGNOSTICS.md) for exact limits and exceptions.

Use **Settings → General → Storage & diagnostics**, below the toggle options, to view activity, clear logs, or clear download cache. Log clearing resets current logs and removes recognized older sessions while retaining the newest crash. New events continue to be recorded. Cache cleanup removes known catalog metadata and saved download blocks after confirmation; those blocks need downloading again for resume. Driver archives, indexes, installers, preferences and unknown files are preserved. Cleanup is unavailable during active installation/downloads. Legacy logs copied beside the EXE are not silently deleted.

The activity viewer supports filtering, copying and HTML export. Its session-folder button opens the actual writable location. No automatic upload occurs; minidumps and detailed logs should be shared privately. The engine activity toggle controls detailed activity, while basic local crash checkpoints remain available.

## Read-only libraries / ISO

Version 0.22.1 removes the five-second wall-clock query cutoff. Lookups remain bounded by processing work and cooperate with scan cancellation. SQLite opens the source read-only and uses memory for query temporary storage. Matching results are not cached as a machine-specific list in the ISO.

GUI session logs always use the current user's local application-data folder, including for read-only media. The activity viewer resolves the active session directory. Software metadata can be read without trying to create a catalog on the disc. Downloading/editing/cleaning a library still requires a writable folder; copy the library to disk for those operations.

A valid library beside a moved EXE takes priority over a stale saved path. An explicit `--library` still wins; a user-picked folder is remembered for that executable location. To update an existing ISO, replace the EXE in the source folder and rebuild the image; installing the new EXE on another disk does not change an already-created ISO.
