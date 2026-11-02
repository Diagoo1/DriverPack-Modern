# DriverPack Modern 0.22.9 Beta

Consolidated Beta containing the previous releases and subsequent fixes.

- Fixes a GUI stack overflow caused by recursive tooltips on empty table cells/tabs.
- Adds readable per-session diagnostics and Windows crash minidumps, stored locally outside the library/ISO, with bounded retention and no automatic upload.
- Adds manual log reset and download-cache cleanup, preserving driver archives, indexes, software installers and settings. Clearing saved blocks requires redownloading them for resume.
- Places activity, log clearing and cache clearing together beneath the General settings toggles, with consistent icons, spacing and equal-width buttons.
- Hardens restore-consent retry and result lifetime; disabled System Restore can be bypassed only after explicit confirmation.
- Distinguishes confirmed software cancellation from other installer exit codes and records setup identity/results.
- Includes earlier exact-device installation, package extraction, torrent connectivity, optical-media scanning and smooth-progress improvements.

**Validation:** 308 fixture checks, synthetic thread/retry scenarios, loopback peer transport, intentional crash/dump tests, native startup, and English/Arabic light/dark UI checks. User-supplied logs report successful 1-, 2- and 13-driver batches on one computer; they do not establish compatibility with every device or Windows version. See docs/VALIDATION.md for limits.

**العربية:** إصدار بيتا موحّد يتضمن إصلاح كراش تلميحات الجداول، وتنظيم السجلات خارج الأسطوانة مع احتفاظ محدود وتقارير كراش، وإضافة مسح السجلات والكاش تحت خيارات الإعدادات. حُسّنت متابعة التثبيت عند تعطيل استعادة النظام ورسائل إلغاء مثبتات البرامج. سجلات تجربة مستخدم أكدت نجاح دفعات من التعريفات على جهاز واحد؛ ما زال التوافق الشامل يحتاج تجارب أوسع.

Original DriverPack and third-party components retain their owners' rights. Interface, development and emblem: Tarek Sadek · Diagoo1. Public Beta; no universal compatibility or full original-engine equivalence is claimed.
