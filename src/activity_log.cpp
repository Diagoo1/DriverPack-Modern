#include "activity_log.h"
#include "crash_diagnostics.h"
#include "offline_paths.h"
#include "appearance.h"
#include "theme.h"
#include "ui_language.h"
#include <QtWidgets>
namespace ActivityLog
{
void show(QWidget *parent, const QString &library, bool arabic, bool preview)
{
    auto tr = [](const char *en, const char *ar)
    { return UiLanguage::text(QString::fromUtf8(en), QString::fromUtf8(ar)); };
    RoundedDialog panel(parent);
    panel.setObjectName(QStringLiteral("activityLogDialog"));
    panel.resize(qMin(1020, parent->width() - 40), qMin(670, parent->height() - 40));
    panel.setWindowTitle(tr("Activity log", "سجل النشاط"));
    auto frame = new QVBoxLayout(&panel);
    frame->setContentsMargins(0, 0, 0, 0);
    frame->setSpacing(0);
    frame->addWidget(new DialogHeader(panel.windowTitle(), QStringLiteral("activity"), &panel, arabic));
    auto body = new QWidget;
    auto box = new QVBoxLayout(body);
    box->setContentsMargins(22, 18, 22, 22);
    box->setSpacing(12);
    frame->addWidget(body, 1);
    auto caption = new QLabel(
        preview ? tr("Sample log — no installation was performed.", "سجل توضيحي — لم يُنفذ أي تثبيت.")
                : tr("Recent local scan and installation events. Nothing is uploaded automatically.",
                     "آخر أحداث الفحص والتثبيت المحلية. لا تُرفع البيانات تلقائيًا."));
    caption->setWordWrap(true);
    box->addWidget(caption);
    auto filters = new QHBoxLayout;
    auto search = new QLineEdit;
    search->setPlaceholderText(tr("Search events…", "البحث في الأحداث…"));
    filters->addWidget(search, 1);
    auto errors = new QCheckBox(tr("Errors only", "الأخطاء فقط"));
    filters->addWidget(errors);
    auto refresh = new QPushButton(tr("Refresh", "تحديث"));
    filters->addWidget(refresh);
    box->addLayout(filters);
    auto table = new QTreeWidget;
    table->setObjectName(QStringLiteral("activityLogTable"));
    table->setRootIsDecorated(false);
    table->setAlternatingRowColors(false);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setUniformRowHeights(true);
    table->setColumnCount(6);
    table->setHeaderLabels({tr("Time", "الوقت"), tr("Stage", "المرحلة"), tr("Status", "الحالة"),
                            tr("Message", "الرسالة"), tr("Error code", "كود الخطأ"),
                            tr("Progress", "التقدم")});
    table->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    table->setMinimumHeight(160);
    box->addWidget(table, 1);
    auto detail = new QPlainTextEdit;
    detail->setReadOnly(true);
    detail->setMaximumHeight(105);
    detail->setPlaceholderText(
        tr("Select an event to copy its full details.", "اختر حدثًا لقراءة تفاصيله كاملة ونسخها."));
    box->addWidget(detail);
    auto footer = new QHBoxLayout;
    auto count = new QLabel;
    footer->addWidget(count, 1);
    auto save = new QPushButton(tr("Save readable report…", "حفظ تقرير مقروء…"));
    footer->addWidget(save);
    auto diagnostics = new QPushButton(tr("Open session folder", "فتح مجلد الجلسة"));
    diagnostics->setCursor(Qt::PointingHandCursor);
    diagnostics->setMinimumHeight(38);
    diagnostics->setToolTip(CrashDiagnostics::directory());
    diagnostics->setEnabled(!CrashDiagnostics::directory().isEmpty());
    QObject::connect(diagnostics,&QPushButton::clicked,&panel,[](){
        QDesktopServices::openUrl(QUrl::fromLocalFile(CrashDiagnostics::directory()));
    });
    footer->addWidget(diagnostics);
    box->addLayout(footer);
    const bool dark = Theme::isDark(parent);
    const QString ink = Theme::color("text", dark).name(), surface = Theme::color("surface", dark).name(),
                  border = Theme::color("border", dark).name();
    table->setStyleSheet(
        QStringLiteral(
            "QTreeWidget{background:%1;color:%2;border:1px solid "
            "%3;border-radius:13px;padding:8px;outline:0;} QTreeWidget::item{padding:10px 8px;border:0;} "
            "QTreeWidget::item:selected{background:%4;color:%2;} "
            "QHeaderView::section{background:#07576b;color:white;border:0;padding:12px 8px;font-weight:600;}")
            .arg(surface, ink, border, dark ? QStringLiteral("#275565") : QStringLiteral("#e1f1ef")));
    detail->setStyleSheet(
        QStringLiteral("background:%1;color:%2;border:1px solid %3;border-radius:12px;padding:8px;")
            .arg(surface, ink, border));
    for (auto button : {refresh, save, diagnostics})
    {
        button->setCursor(Qt::PointingHandCursor);
        button->setMinimumHeight(38);
        button->setStyleSheet(
            QStringLiteral(
                "QPushButton{background:%1;color:%2;border:1px solid %3;border-radius:11px;padding:7px "
                "15px;} QPushButton:hover{border-color:#00aaa9;background:%4;}")
                .arg(surface, ink, border, dark ? QStringLiteral("#315d70") : QStringLiteral("#e2f3f1")));
    }
    QVector<QJsonObject> records;
    auto populate = [&]()
    {
        table->clear();
        detail->clear();
        records.clear();
        if (preview)
        {
            for (int n = 0; n < 3; ++n)
            {
                QJsonObject event;
                event.insert(QStringLiteral("time"), QStringLiteral("2026-11-01T10:00:0%1Z").arg(n));
                event.insert(QStringLiteral("stage"), n);
                event.insert(QStringLiteral("outcome"), n == 2 ? 2 : 1);
                event.insert(QStringLiteral("message"),
                             n == 0   ? tr("Reading devices", "قراءة الأجهزة")
                             : n == 1 ? tr("Reading local indexes", "قراءة الفهارس المحلية")
                                      : tr("Matching completed", "اكتملت المطابقة"));
                event.insert(QStringLiteral("win32Error"), 0);
                event.insert(QStringLiteral("completed"), n == 2 ? 12 : n);
                event.insert(QStringLiteral("total"), 12);
                records << event;
            }
        }
        else
            records = readRecent(QDir(CrashDiagnostics::directory().isEmpty()?DriverPackLibrary::logDirectory(library):CrashDiagnostics::directory()).filePath(QStringLiteral("native-events.jsonl")));
        const QStringList stages = {tr("Scan", "فحص"),
                                    tr("Indexes", "الفهارس"),
                                    tr("Matching", "المطابقة"),
                                    tr("Restore point", "نقطة الاستعادة"),
                                    tr("Extraction", "الاستخراج"),
                                    tr("Installation", "التثبيت"),
                                    tr("Finished", "انتهى")};
        const QStringList outcomes = {tr("Pending", "بالانتظار"), tr("Working…", "جارٍ العمل…"),
                                      tr("Success", "نجاح"),      tr("Failed", "فشل"),
                                      tr("Skipped", "تم التخطي"), tr("Cancelled", "أُلغي")};
        for (int n = records.size() - 1; n >= 0; --n)
        {
            const auto event = records[n];
            auto item = new QTreeWidgetItem(table);
            const auto date =
                QDateTime::fromString(event.value(QStringLiteral("time")).toString(), Qt::ISODate);
            item->setText(0, date.isValid()
                                 ? date.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                                 : event.value(QStringLiteral("time")).toString());
            item->setText(
                1, stages.value(event.value(QStringLiteral("stage")).toInt(-1), tr("Unknown", "غير معروف")));
            const int outcome = event.value(QStringLiteral("outcome")).toInt(-1);
            item->setText(2, outcomes.value(outcome, tr("Unknown", "غير معروف")));
            item->setText(3, event.value(QStringLiteral("message")).toString());
            const quint32 code = quint32(event.value(QStringLiteral("win32Error")).toDouble());
            item->setText(4, code ? QStringLiteral("%1 / 0x%2").arg(code).arg(code, 8, 16, QLatin1Char('0'))
                                  : QStringLiteral("—"));
            item->setText(5, QStringLiteral("%1 / %2")
                                 .arg(event.value(QStringLiteral("completed")).toDouble(), 0, 'f', 0)
                                 .arg(event.value(QStringLiteral("total")).toDouble(), 0, 'f', 0));
            item->setData(0, Qt::UserRole, n);
            item->setData(2, Qt::UserRole, outcome == 3 || code != 0);
            if (outcome == 3 || code)
                item->setForeground(2, QBrush(QColor(dark ? "#ffb0b5" : "#b43c4c")));
            for (int col = 0; col < 6; ++col)
                item->setToolTip(col, item->text(col));
        }
    };
    auto filter = [&]()
    {
        int visible = 0;
        for (int n = 0; n < table->topLevelItemCount(); ++n)
        {
            auto item = table->topLevelItem(n);
            QString text;
            for (int c = 0; c < 6; ++c)
                text += item->text(c) + QLatin1Char(' ');
            const bool show = (!errors->isChecked() || item->data(2, Qt::UserRole).toBool()) &&
                              text.contains(search->text(), Qt::CaseInsensitive);
            item->setHidden(!show);
            visible += show;
        }
        count->setText(tr("Events: %1 / %2", "الأحداث: %1 / %2").arg(visible).arg(records.size()));
    };
    QObject::connect(search, &QLineEdit::textChanged, &panel, [&]() { filter(); });
    QObject::connect(errors, &QCheckBox::toggled, &panel, [&]() { filter(); });
    QObject::connect(refresh, &QPushButton::clicked, &panel,
                     [&]()
                     {
                         populate();
                         filter();
                     });
    QObject::connect(table, &QTreeWidget::itemSelectionChanged, &panel,
                     [&]()
                     {
                         if (auto item = table->currentItem())
                         {
                             const int index = item->data(0, Qt::UserRole).toInt();
                             if (index >= 0 && index < records.size())
                                 detail->setPlainText(QString::fromUtf8(
                                     QJsonDocument(records[index]).toJson(QJsonDocument::Indented)));
                         }
                     });
    QObject::connect(
        save, &QPushButton::clicked, &panel,
        [&]()
        {
            if (parent->property("readOnlyPreview").toBool())
                return;
            const QString path = QFileDialog::getSaveFileName(&panel, tr("Save report", "حفظ التقرير"),
                                                              QStringLiteral("DriverPack-activity.html"),
                                                              QStringLiteral("HTML (*.html)"));
            if (path.isEmpty())
                return;
            QString html = QStringLiteral("<!doctype html><meta charset=\"utf-8\"><title>DriverPack Modern "
                                          "Activity</title><style>body{font:15px "
                                          "sans-serif;color:#123648;margin:32px}table{border-collapse:"
                                          "collapse;width:100%}td,th{padding:12px;border-bottom:1px solid "
                                          "#dce7ea;text-align:start}th{background:#07576b;color:white}</"
                                          "style><h1>DriverPack Modern</h1><table><tr>");
            for (int c = 0; c < 6; ++c)
                html += QStringLiteral("<th>") + table->headerItem()->text(c).toHtmlEscaped() +
                        QStringLiteral("</th>");
            html += QStringLiteral("</tr>");
            for (int n = 0; n < table->topLevelItemCount(); ++n)
            {
                auto item = table->topLevelItem(n);
                if (item->isHidden())
                    continue;
                html += QStringLiteral("<tr>");
                for (int c = 0; c < 6; ++c)
                    html += QStringLiteral("<td>") + item->text(c).toHtmlEscaped() + QStringLiteral("</td>");
                html += QStringLiteral("</tr>");
            }
            html += QStringLiteral("</table>");
            QSaveFile output(path);
            const QByteArray bytes = html.toUtf8();
            if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
                projectMessage(&panel, tr("Error", "خطأ"),
                               tr("Could not save the report.", "تعذر حفظ التقرير."), false, arabic);
        });
    populate();
    filter();
    panel.exec();
    for (QObject *child : panel.findChildren<QObject *>())
        QObject::disconnect(child, nullptr, &panel, nullptr);
}
} // namespace ActivityLog
