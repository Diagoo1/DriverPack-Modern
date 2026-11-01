# Beta validation

Version **0.22.0**. Six fixture suites pass **229 checks**: catalog/selection 64, transfer data 24, independence 37, software adapter 62, cleanup 35, activity-log parser 7. Fixture tests do not install software or modify a production library.

The x86 executable links Qt and transport statically. PE auditing found only Windows system DLLs and no known post-XP imports among 662 imported functions. The embedded x64 installation helper is generated from included source.

Native UI capture verifies selection, toggles, alternatives, navigation, scrolling, languages and themes on the development machine. The public gallery uses illustrative data, in English/Arabic and light/dark modes. Its installation view is explicitly a preview.

**Unverified:** a complete XP–11 x86/x64 device/VM matrix; actual driver/software installations, restore-point creation and reboot; public peer-transfer behavior; every original DriverPack matching exception. This Beta should not be described as production-ready or fully equivalent to DriverPack. No actual installation was performed to produce screenshots.
