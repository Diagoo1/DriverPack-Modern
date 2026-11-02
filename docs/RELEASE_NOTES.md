# DriverPack Modern 0.22.3 Beta - installation preparation fixes

- Installs on the explicitly selected device instance on Windows Vista and later, using its exact Windows-compatible INF/version node. Shared hardware IDs no longer block legitimate separate devices. The legacy unique-ID safety check remains for XP-era systems.
- Uses the same validated device-instance protocol in the embedded native x64 helper. Never falls back to changing every device with a shared ID after a modern install error.
- Corrects the aggregate decoder memory budget for 512 MiB solid DriverPack blocks and reports actual memory failures clearly. The supplied archive reproduced the old failure and now extracts successfully.
- Recognizes an exact catalog INF carried in a newer archive of the same package family. Hardware/version metadata is still verified after extraction; an unrelated archive is never substituted.
- Records preparation errors and restore-point failures in the local journal, including failures before the installation loop.
- Adds an explicitly non-installing Windows preflight diagnostic and 14 mocked installer checks. Intel Participant, Intel Updater and Voice Clarity pass real compatible-node preflight on the development computer. No actual driver installation or restore-point creation was performed for validation.

**العربية:** إصلاح استهداف الجهاز عند اشتراك أكثر من جهاز في معرّف العتاد، وإصلاح فشل فك ضغط حزم DriverPack الكبيرة والتعرف على ملفات التعريف المطابقة داخل الحزم الأحدث. الفحص التحضيري نجح، ويبقى تأكيد التثبيت الفعلي على الجهاز الذي ظهر عليه العطل.

Original DriverPack, third-party drivers and logos retain their owners' rights. UI/development/emblem: Tarek Sadek · Diagoo1. This remains a Beta; full XP-11 runtime and installation coverage is not claimed.
