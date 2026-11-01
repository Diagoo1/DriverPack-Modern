# DriverPack Modern 0.22.1 Beta — portable/ISO reading fix

- Replaces the five-second SQLite wall-clock cutoff with a bounded instruction budget and cooperative cancellation. Slow media/PCs are not rejected solely because five seconds elapsed.
- Pins device-first indexed joins and keeps temporary query data in memory; source catalogs remain read-only.
- Uses writable local logs for read-only ISO libraries; reads software catalogs without trying to persist them to the disc.
- Detects an adjacent library after moving the EXE; stale saved paths no longer redirect portable copies. Explicit library choices remain available.
- Replaces the nested reading/error card with a full-window page and visible native titlebar with the app icon/version, a rounded activity bar, copyable technical details and consistent recovery buttons. Errors do not show a misleading 0% progress bar.

Validation: **248 fixture checks**, a delayed query over five seconds, cancellation/budget checks, matching comparison between an actual mounted ISO and disk folder, and direct EXE startup from a separately generated read-only ISO on Windows 11. No driver/software installation or restore/reboot was performed. Complete original-matcher equivalence and the XP–11 device matrix remain unfinished.

**العربية:** إصلاح قراءة المكتبات على الأجهزة الأبطأ والـISO، ومسارات السجلات والتعرف على المكتبة بعد النقل، مع إعادة تصميم شاشة التحميل والخطأ. استبدل EXE داخل مجلد الأسطوانة ثم أعد إنشاء ISO؛ الصورة القديمة لا تتحدث تلقائيًا. النسخة تظل بيتا وتحتاج إلى استكمال اختبارات التوافق والتثبيت الفعلي.

UI, development and emblem: Tarek Sadek · Diagoo1. Original DriverPack and third-party rights remain with their owners.
