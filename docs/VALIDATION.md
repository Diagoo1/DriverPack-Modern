# Beta validation

Version **0.22.5**. Seven fixture suites pass **289 checks**, plus a loopback-only peer transport suite: catalog/selection 95, transfer data 34, independence 42, software adapter 62, cleanup 35, activity-log parser 7, mocked exact-device installer 14. Fixture tests do not install software or modify a production library.

The x86 executable links Qt and transport statically. PE auditing found only Windows system DLLs and no known post-XP imports among 662 imported functions. The embedded x64 installation helper is generated from included source.

Native UI capture verifies selection, toggles, alternatives, navigation, scrolling, languages and themes on the development machine. The public gallery uses illustrative data, in English/Arabic and light/dark modes. Its installation view is explicitly a preview.

**Unverified:** a complete XP–11 x86/x64 device/VM matrix; actual driver/software installations and reboot; restore behavior across all target systems (one authorized Windows 11 creation test is documented below); compatibility across all public networks/ISPs; every original DriverPack matching exception. This Beta should not be described as production-ready or fully equivalent to DriverPack. No actual installation was performed to produce screenshots.

## 0.22.1 optical-media regression checks

A deliberately delayed SQLite query exceeds the former five-second deadline and completes. Processing work remains bounded; cancellation is propagated outside SQLite callbacks. Relocated Unicode/read-only fixtures retain their hash. An actual mounted CDFS library and its disk-folder copy produce equal normalized reports on the development machine. A separate ISO containing the new EXE and a compatible index starts directly from read-only media with stale settings, detects its adjacent library, completes the splash and writes its log outside the ISO. These are Windows 11 development-machine tests, not a full device/OS matrix or installation test.

The user-provided cleaned ISO was also mounted read-only: its database and prior EXE matched the published 0.22.0 library. The corrected EXE completes repeated scans and startup against it. At identical mounted-device state, normalized ISO/folder scan reports match exactly. Mounting an extra optical drive changes Windows device count, so comparisons use the same device inventory state.

## 0.22.2 download regression

On the same current torrent and network, the old build repeatedly reached zero peers and the idle limit. The corrected peer-discovery configuration reached multiple peers and saved both DP_TV_Beholder_26040.7z (1,482,964 bytes) and DP_LAN_Realtek-XP_26040.7z (5,265,587 bytes), independently verifying their torrent block hashes. Both 7-Zip archive integrity tests passed. Native discovery reached up to nine peers; the HTTPS fallback was not needed during these successful live transfers. HTTPS tracker URL encoding, compact peer responses, malformed replies and bounded response sizes have fixture coverage. A local-only BitTorrent test pauses after a verified block, resumes missing blocks, verifies every byte, then serves the verified cache to a second independent downloader and stops sharing cleanly. No installer was executed.

This validates two real public package downloads, not availability of every torrent/file or operation on every network. Metadata hashes verify consistency with the selected catalog; they do not certify publisher identity or driver compatibility.

## 0.22.3 installation preparation regression

The supplied latest Chipset archive reproduces the former 512 MiB aggregate decoder allocation failure. The archive independently passes an integrity test. With the bounded decoder budget corrected, actual Intel Participant and Updater entries extract and pass Windows compatible-node enumeration for their exact device instance and version. Voice Clarity also passes using the exact catalog INF contained in the newer same-family archive. These probes explicitly disable staging and installation. Mocked installer tests cover exact-instance dispatch, selected-version/INF checks, missing devices, failed installs and unavailable APIs. XP-era systems retain the unique-hardware-ID guard; modern systems use dynamically resolved DiInstallDevice. No shared-ID-wide fallback occurs after an instance install fails.

The library still references DP_xVirtual_26044.7z, which is absent from the supplied library; it is not replaced with an unrelated package. Actual installation on the reported second device remains to be confirmed by the user.

## 0.22.4 restore-point diagnostics and result page

The DriverPack adapter's restore callback initializer was a no-op. It now initializes process-wide COM security before the UI, with explicit access for the current user, Administrators, System, LocalService and NetworkService. A typed failure preserves the immediate System Restore/Win32 code across cleanup and logging. Five pure regression checks cover reported errors, stale last-error and false calls with error zero.

An explicitly user-authorized elevated restore-only test was performed on Windows 11. COM security initialized successfully, but the actual restore call returned Windows 1058 (System Restore disabled). The test did not install a driver or alter System Protection settings. After the user explicitly authorized enabling protection only for the Windows drive, a second real restore-only test succeeded. Windows Application event 8194 independently confirms creation of the named test restore point. No driver was installed, no restore points were deleted, and other drives were not changed.

The result page distinguishes a blocked restore point from completed installation errors. Result actions share equal width, 52-pixel height, typography and spacing; the table height is bounded by item count. Native capture asserts result-action geometry and includes a labelled simulated restore-failure page in English/Arabic and light/dark themes. A sidebar animation check waits for its state with a bounded deadline rather than assuming a busy frame finishes within exactly 320 ms.

For Windows error 1058 only, the UI now offers a yellow explicit Continue/Cancel dialog before any driver starts. Cancel/Escape/window-close do not retry. Confirmed continuation retries the same validated plan with a per-operation flag; the backend allows this exception only for disabled System Restore. Other restore errors remain blocking. Pure policy/workflow tests cover consent, error classification, already-started items and cancellation. The bypass itself was tested through policy fixtures and simulated UI, not by disabling protection again or installing a driver.

## 0.22.5 progress presentation

The active shell's installation, transfer, scan/update activity and portable-copy bars use the shared rounded renderer. Underlying progress values/signals remain exact; only the fill interpolates over 360-800 ms. Unknown-duration Windows install/extract/restore phases use indeterminate movement rather than manufactured percentages. Timers stop when hidden or determinate/idle. Showing a hidden bar snaps to its current value rather than replaying animation; reduced-motion mode snaps immediately.

The native UI probe checks actual value immediately after an update, intermediate increasing visual samples, final convergence, reset, continuous retargeting, hidden behavior and reduced motion. The same probes and English/Arabic light/dark installation captures pass at 100% and 150%. These are simulated progress/UI tests, not actual installations. Successful operations may keep the already-completed result visible for at most the remaining bounded fill animation (at most 850 ms); failures/cancellation are not painted as 100% success.

The installation page has a project-color hero, phase chips, visible table header, selected-item count and elapsed time. Restore-point success is emitted explicitly by the native adapter, so its row no longer remains in Creating while installation begins.
