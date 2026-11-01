# Beta validation

Version **0.22.1**. Six fixture suites pass **248 checks**: catalog/selection 83, transfer data 24, independence 37, software adapter 62, cleanup 35, activity-log parser 7. Fixture tests do not install software or modify a production library.

The x86 executable links Qt and transport statically. PE auditing found only Windows system DLLs and no known post-XP imports among 662 imported functions. The embedded x64 installation helper is generated from included source.

Native UI capture verifies selection, toggles, alternatives, navigation, scrolling, languages and themes on the development machine. The public gallery uses illustrative data, in English/Arabic and light/dark modes. Its installation view is explicitly a preview.

**Unverified:** a complete XP–11 x86/x64 device/VM matrix; actual driver/software installations, restore-point creation and reboot; public peer-transfer behavior; every original DriverPack matching exception. This Beta should not be described as production-ready or fully equivalent to DriverPack. No actual installation was performed to produce screenshots.

## 0.22.1 optical-media regression checks

A deliberately delayed SQLite query exceeds the former five-second deadline and completes. Processing work remains bounded; cancellation is propagated outside SQLite callbacks. Relocated Unicode/read-only fixtures retain their hash. An actual mounted CDFS library and its disk-folder copy produce equal normalized reports on the development machine. A separate ISO containing the new EXE and a compatible index starts directly from read-only media with stale settings, detects its adjacent library, completes the splash and writes its log outside the ISO. These are Windows 11 development-machine tests, not a full device/OS matrix or installation test.

The user-provided cleaned ISO was also mounted read-only: its database and prior EXE matched the published 0.22.0 library. The corrected EXE completes repeated scans and startup against it. At identical mounted-device state, normalized ISO/folder scan reports match exactly. Mounting an extra optical drive changes Windows device count, so comparisons use the same device inventory state.
