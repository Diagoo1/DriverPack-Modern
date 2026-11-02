# Crash diagnosis and bounded sessions — 0.22.8

The supplied 0.22.6 session and matching minidump identify exception 0xC00000FD
(stack overflow) in the GUI thread. The stack contains thousands of repeated
QHelpEvent, item-view viewport and application-notify calls. The driver worker
was extracting files; an earlier installation in the same session completed
successfully after explicit consent to skip disabled System Restore.

Table and tab handlers synchronously re-sent tooltip events to their own widget.
For empty text the global filter had nothing to consume, causing recursive entry.
They now call the project tooltip presenter directly. No tooltip event is re-sent.
Verified with 4,000 events on the actual production table/tab classes, including
empty/populated cells, Arabic/English and light/dark. This fixes that crash path;
full installation on the other computer still needs retesting.

## Location and retention

GUI logs always go to the user's local application data:
%LOCALAPPDATA%/TarekSadek/DriverPackModern/Logs/session-<UTC>-<PID>/

They are not written into the library or beside the EXE. A mounted ISO therefore
requires no writable media. A local temporary-directory fallback is available.
Isolated tests use their own workspace directory.

Each session contains session.txt, native-events.jsonl (and possibly its previous
rotation), crash.dmp when captured, a session marker and an active-process lock.
Settings → View activity log → Open session folder opens the actual current folder.
Send that folder privately for diagnosis. Dumps contain process diagnostic memory.
Nothing is automatically uploaded.

At startup, retain five recent normal sessions (including current) and three crash
sessions. A newly crashing current session stays protected until the next startup
prunes older crashes. A 64 MiB aggregate target prunes older sessions sooner; the
current session and newest crash stay protected even if one dump exceeds it.
Live sessions are skipped. Unknown files, nested folders, links and unmarked
folders are never recursively deleted. Existing legacy logs beside the EXE remain
intact; new GUI sessions no longer add to them.

The readable trace stops detailed additions around 4 MiB but still records fatal
exceptions and dump results. The engine log rotates around 2 MiB with one previous
file. Long sessions therefore cannot accumulate unlimited text logs.

## Recording and verification

A pre-created dedicated Windows thread writes minidumps using the system copy of
dbghelp.dll loaded dynamically. The faulting thread does not allocate Qt objects
or take UI locks. Checkpoints flush immediately. Exception address, image base,
thread ID, Qt fatal, abort and terminate are captured. Matching linker maps remain
in private build artifacts.

This cannot guarantee a dump after power loss, external process termination, disk
failure, or every form of severe process corruption.

Tests cover retention fixtures (active/unknown/unmarked/nested data preserved),
real subprocess restarts and crashes, capped-log crash recording, startup and
fallback storage. No driver installation, restore-point creation or System
Protection change is performed by these tests.

Reference: [Microsoft MiniDumpWriteDump](https://learn.microsoft.com/en-us/windows/win32/api/minidumpapiset/nf-minidumpapiset-minidumpwritedump).

## Manual cleanup (0.22.8)

Settings / General now provides Clear logs and Clear download cache actions.
Both require confirmation and refuse to run during installation or active online work.
Clearing logs removes recognized finished sessions except the newest crash and resets
the current text/engine logs while preserving the active session lock and recording.
Cache cleanup removes known catalog/events files and saved torrent blocks only;
driver archives, indexes, installers, settings and unknown files are preserved.
Removing blocks means those blocks need downloading again for resume.

Software cancellation codes 1223 and 1602 are shown as cancelled. Other non-success
installer exit codes, including 2, are shown as a stopped installer with its code:
the code alone cannot distinguish cancellation from an installer failure.
