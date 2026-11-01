#include "theme.h"
#include "ui_font.h"
#include <QWidget>
#include <QVariant>

bool Theme::isDark(const QWidget *widget)
{
    for(const QWidget *w=widget;w;w=w->parentWidget())
        if(w->property("darkMode").isValid())return w->property("darkMode").toBool();
    return widget && widget->palette().color(QPalette::Base).lightness()<130;
}

QString Theme::styleSheet(bool dark)
{
    // One palette for all Qt Widgets and dialogs: intentionally low border density.
    const QString background=color("background",dark).name(),surface=color("surface",dark).name(),elevated=color("elevated",dark).name();
    const QString primary=color("text",dark).name(),secondary=color("muted",dark).name(),stroke=color("border",dark).name(),thumb=color("scrollbar",dark).name();
    const QString comboArrow=dark?QStringLiteral(":/sdio/assets/icons/chevron-down-light.svg"):
                                  QStringLiteral(":/sdio/assets/icons/chevron-down-dark.svg");
    QString result=QString::fromLatin1(R"QSS(
QWidget {font-family:"Segoe UI";}
QWidget#root {background:transparent;}
QScrollArea {background:transparent;border:0;}
QScrollArea>QWidget>QWidget {background:transparent;}
QSpinBox,QDateTimeEdit {background:%2;color:%4;border:1px solid %6;border-radius:9px;padding:8px 25px 8px 11px;min-height:20px;}
QSpinBox::up-button,QSpinBox::down-button,QDateTimeEdit::up-button,QDateTimeEdit::down-button {background:transparent;border:0;width:22px;}
QSpinBox::up-button:hover,QSpinBox::down-button:hover,QDateTimeEdit::up-button:hover,QDateTimeEdit::down-button:hover {background:%3;}
QTabWidget::pane {border:0; background:transparent;}
QTabBar::tab {color:%5; background:transparent; padding:10px 18px; border-bottom:2px solid transparent;}
QTabBar::tab:selected {color:%4; border-bottom:2px solid #00AFA5;}
QComboBox[projectCombo="true"] {background:%2;color:%4;border:1px solid %6;border-radius:10px;padding:10px 38px 10px 13px;min-height:24px;font-size:13px;}
QComboBox[projectCombo="true"]:hover,QComboBox[projectCombo="true"]:focus {background:%3;border-color:#00AFA5;}
QComboBox[projectCombo="true"]:disabled {color:%5;background:%1;}
QComboBox[projectCombo="true"]::drop-down {background:transparent;border:0;width:34px;}
QComboBox[projectCombo="true"]::down-arrow {image:url(%8);width:14px;height:14px;}
QComboBox#candidatePicker {background:%2;color:%4;border:1px solid %6;border-radius:10px;padding:10px 38px 10px 13px;min-height:24px;}
QComboBox#candidatePicker::drop-down {border:0; width:28px;}
QComboBox#candidatePicker::down-arrow {image:url(%8);width:12px;height:12px;}
QComboBox#candidatePicker QAbstractItemView {background:%2;color:%4;selection-background-color:%3;}

QWidget#page {background:%1; border-bottom-right-radius:20px;}
QWidget#root[rtl="true"] QWidget#page {border-bottom-right-radius:0;border-bottom-left-radius:20px;}
QWidget#page[standalone="true"] {border-bottom-left-radius:20px;}
QWidget#root[maximized="true"] QWidget#page {border-radius:0;}
QWidget#settingsBody {background:transparent;}
QLabel#settingsHeroTitle {color:white;font-size:23px;font-weight:700;background:transparent;}
QLabel#settingsHeroSubtitle {color:#C1EEEA;font-size:13px;background:transparent;}
QWidget#settingsSectionHeader {background:transparent;}
QWidget#settingsCard {background:transparent;border:0;border-radius:16px;}
QWidget#softSurface { background: transparent; border: 0; border-radius: 16px; }
QWidget#driverTableCard,QWidget#driverTableToolbar {background:transparent;border:0;}
QAbstractButton[workspaceCommand="true"] {background:transparent;border:0;padding:0;min-height:0;font-size:13px;font-weight:500;}
QWidget#driverSummaryStrip { background:transparent; border:0; }
QFrame#metricCard {background:transparent; border:0; border-radius:10px; min-height:54px;}
QWidget#transferTile {background:transparent;border:0;}
QLabel[transferMetric="true"] {font-size:18px;font-weight:600;color:%4;background:transparent;}
QLabel#metricValue {font-size:17px; font-weight:700; color:%4; background:transparent;}
QLabel#metricCaption {font-size:12px; font-weight:500; color:%5; background:transparent;}
QLabel#driverStats {color:%4; background:%3; padding:8px 13px; border:0; border-radius:6px;
 font-size:12px; font-weight:600;}
QToolButton#quietTool {background:%3; color:%4; border:1px solid %6; border-radius:6px;
 padding:6px 11px; min-height:25px; font-size:12px;}
QToolButton#quietTool:hover {color:%4;}
QLabel#diagnosticsHeroTitle {color:white;font-size:23px;font-weight:700;background:transparent;}
QLabel#diagnosticsHeroSubtitle {color:#C4EEEB;font-size:12px;background:transparent;}
QWidget#diagnosticSectionHeader {background:transparent;}
QLabel#diagnosticSymbol {border-radius:11px;background:%3;}
QProgressBar#diagnosticUsage {background:%3;border:0;border-radius:3px;}
QProgressBar#diagnosticUsage::chunk {background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #167DAB,stop:1 #14AA9D);border-radius:3px;}
QWidget#diagnosticBody {background:%1; border:0;}
QLabel#diagnosticSectionTitle {color:%4; font-size:17px; font-weight:600; background:transparent;}
QLabel#diagCaption {color:%5; font-size:11px; font-weight:500; background:transparent;}
QWidget#customTitleBar,QWidget#sidebar {background:transparent;}
QFrame#diagnosticCard,QFrame#detailSection {
    background: transparent; border: 0; border-radius: 14px;
}
QScrollArea#diagnosticScroll {background:%1;}
QWidget#driverDetailsDialog,QWidget#projectDialog {background:transparent; color:%4;}
QWidget#comparisonBody {background:%1; color:%4; border-bottom-left-radius:22px; border-bottom-right-radius:22px;}
QMessageBox#safeDialog {background:%1; color:%4;}
QLabel#comparisonTitle { color:#E8FAFA; font-size:15px; font-weight:600; background:transparent; }
QToolButton#comparisonClose { background:transparent; border:none; padding:0; border-radius:10px; }
QToolButton#comparisonClose:hover {background:transparent;}
QToolButton#comparisonClose:pressed { background:#A82E41; }
QWidget#toolsDrawerPanel,QWidget#toolsDrawerContent,QScrollArea#toolsDrawerScroll {
 background:#043B47; border:0; color:#EDF8F9;
}
QWidget#toolsDrawerHeader { background:#063943; border:0; }
QWidget#toolsDrawerPanel {border-radius:20px;}
QWidget#toolsDrawerHeader {border-top-left-radius:20px; border-top-right-radius:20px;}
QLabel#toolsDrawerTitle { color:#E8FAFA; font-size:16px; font-weight:600; background:transparent; }
QLabel#toolsDrawerGroup { color:#84B9BF; font-size:10px; font-weight:700; background:transparent; }
QToolButton#toolsDrawerRow { background:transparent; color:#DDF3F1; border:0; border-radius:12px;
 text-align:left; padding:0 19px 0 30px; font-size:13px; }
QToolButton#toolsDrawerRow:hover {color:#FFFFFF;}
QToolButton#toolsDrawerRow:pressed { background:#08717A; }
QToolButton#toolsDrawerRow:disabled { color:#7799A0; background:transparent; }
QToolButton#toolsDrawerClose { border:0; padding:0; background:transparent; }
QToolButton#toolsDrawerClose:hover {background:transparent;}
QFrame#softSeparator { background: %6; height: 1px; border: 0; }
QLabel#headerTitle {color: #EFFCFC; font-size: 18px; font-weight: 600; background: transparent;}
QLabel#headerSub {color: #C0D9DE; font-size: 12px; background: transparent;}
QLabel#headerStatus {color: #C1DEE2; font-size: 12px; background: transparent;}
QLabel#sidebarFooter {color: #B8DDE7; font-size: 11px; background: transparent;}
QLabel#pageTitle {color: %4; font-size: 25px; font-weight: 500;}
QLabel#sectionTitle {color: %4; font-size: 17px; font-weight: 500;}
QLabel#dialogTitle {color: %4; font-size: 21px; font-weight: 600;}
QLabel#detailHeading {color: #10B6A9; font-size: 12px; font-weight: 600; }
QLabel#diagnosticValue {color: %4; font-size: 13px; font-weight: 600; background:transparent;}
QLabel#bodyText {color: %4; font-size: 13px;}
QLabel#mutedText,QLabel#subText {color: %5; font-size: 12px;}
QLabel#demoText {color: %5; font-size: 12px;}
QLabel#simulationBadge {color:#CAFDFA; background:#0B777B; border-radius:11px; padding:6px 12px; font-weight:600;}
QLabel#stageItem {color:%5; background:%3; border:1px solid %6; border-radius:12px; font-size:13px; padding:14px 10px;}
QLabel#activeStage {color:%4; background:rgba(0,181,166,27); border:1px solid #00B5A6; font-weight:600; border-radius:12px; font-size:13px; padding:14px 10px;}
QToolButton#titleAction,QToolButton#windowAction,QToolButton#windowClose {
 background:transparent; border:none; border-radius:10px; margin:5px 1px; padding:0;
}
QToolButton#titleAction:hover,QToolButton#windowAction:hover,QToolButton#windowClose:hover {background:transparent;}
QToolButton#titleAction:pressed,QToolButton#windowAction:pressed {background:#003B54;}
QToolButton#windowClose:pressed {background:#9F2336;}
QPushButton#primaryButton {
 background:#00AFA5; color:white; border:none; border-radius:12px; padding:10px 20px;
 font-size:13px; font-weight:600; min-height:23px;
}
QPushButton#primaryButton:hover {background:#00AFA5;}
QPushButton#primaryButton:pressed {background:#008B85;}
QPushButton#primaryButton:disabled {background:%3; color:%5;}
QPushButton#outlineButton,QToolButton#secondaryTool {
 background:%3; color:%4; border:none; border-radius:12px; padding:9px 14px; min-height:22px;
}
QPushButton#outlineButton:hover,QToolButton#secondaryTool:hover {color:%4;}
QPushButton#textLinkButton { background:%3; color:%4; border:1px solid %6; border-radius:10px;
 padding:6px 9px; font-weight:600; font-size:12px; }
QPushButton#textLinkButton:hover {color:#08D2C3;}
QPushButton#textLinkButton:disabled { color:%5; background:transparent; }
QPushButton#outlineButton:disabled,QToolButton#secondaryTool:disabled {color:%5;}
QLineEdit {
 background:%2; color:%4; border:1px solid %6; border-radius:6px;
 padding:9px 13px; selection-background-color:#0EAAA7;
}
QLineEdit:focus {border:1px solid #09B5A8;}
QPlainTextEdit,QTextEdit {background:%2;color:%4;border:1px solid %6;border-radius:12px;padding:12px;selection-background-color:%3;selection-color:%4;}
QListWidget#installConsentList {background:%2;color:%4;border:1px solid %6;border-radius:14px;padding:8px;}
QListWidget#installConsentList::item {padding:8px;border-bottom:1px solid %6;}
QLabel#installOptionTag {background:%3;color:%4;border:1px solid %6;border-radius:12px;padding:11px;font-weight:600;}
QWidget#reinstallNotice QLabel {background:transparent;color:%4;border:0;font-size:13px;}
QLabel#installNotice {background:%3;color:%4;border:1px solid %6;border-radius:13px;padding:14px;font-size:13px;}
QLabel#installNotice[tone="error"] {background:rgba(208,71,94,20);color:#D0475E;border:1px solid rgba(208,71,94,90);}
QWidget#installationHero QProgressBar#mainProgress {background:rgba(255,255,255,32);color:white;min-height:28px;border-radius:10px;}
QWidget#installationHero QProgressBar#mainProgress::chunk {background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #27B4BF,stop:1 #42D9BC);border-radius:10px;}
QTreeWidget#driverTree,QTreeWidget#installQueue,QScrollArea#diagnosticScroll,QScrollArea#dialogScroll,QScrollArea#onlineBodyScroll {
 background:%2; color:%4; border:0; border-radius:9px; outline:0;
}
QTreeWidget#driverTree::item {border:0; padding:0px;}
QTreeWidget#installQueue::item {border:0; padding:0px;}
QTreeWidget#driverTree::item:hover {background:transparent;}
QTreeWidget#driverTree QHeaderView::section {background:%3;color:%4;border:0;border-bottom:1px solid %6;padding:12px 10px;font-weight:600;}
QTreeWidget#driverTree::item:selected {background:transparent; color:%4;}
QTreeWidget#installQueue::item:selected {background:transparent; color:%4;}
QTreeWidget#driverTree::indicator:unchecked { width:18px; height:18px; border-radius:3px; background:%2; border:1px solid %7; }
QTreeWidget#driverTree::indicator:checked { width:18px; height:18px; border-radius:3px; background:#12AAB5; border:1px solid #12AAB5; image:url(:/sdio/assets/icons/check.svg); }
QHeaderView::section {background:%1; color:%5; border:0; border-bottom:1px solid %6;
 padding:10px 11px; font-size:12px; font-weight:600;}
QScrollBar:vertical {width:9px; background:transparent; margin:0; border:none;}
QScrollBar::handle:vertical {background:%7; min-height:32px; border-radius:4px; margin:1px 1px;}
QScrollBar::handle:vertical:hover {background:#12ADA4;}
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical {height:0px; background:transparent; border:0;}
QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical {background:transparent;}
QScrollBar:horizontal {height:8px; background:transparent; border:none;}
QScrollBar::handle:horizontal {background:%7; border-radius:4px; min-width:35px;}
QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal {width:0; background:transparent; border:0;}
QCheckBox {color:%4; background:transparent; font-size:13px; spacing:9px;}
QCheckBox:disabled {color:%5;}
QCheckBox::indicator {width:18px; height:18px;}
QCheckBox::indicator:unchecked {border:1px solid %7; border-radius:4px; background:%2;}
QCheckBox::indicator:checked {border:1px solid #00AFA5; border-radius:4px; background:#00AFA5;
 image:url(:/sdio/assets/icons/check.svg);}
QCheckBox::indicator:disabled {border:1px solid %6; background:%3;}
QComboBox#premiumCombo {background:%3; color:%4; border:1px solid transparent; border-radius:9px;
 padding:9px 14px; padding-right:39px; min-height:30px; font-size:12px;}
QComboBox#premiumCombo:on {background:%2; border:1px solid %6;}
QComboBox#premiumCombo:focus {outline:none; border:1px solid %6;}
QComboBox#premiumCombo::drop-down {background:transparent; border:0; width:34px;}
QComboBox#premiumCombo::down-arrow {image:url(%8); width:17px; height:17px;}
QComboBox#premiumCombo QAbstractItemView {
 background:%2; color:%4; selection-background-color:%3;
 selection-color:%4; border:0; outline:none; padding:6px;}
QFrame#premiumPopover {background:transparent; color:%4; border:0;}
QWidget#languagePopup {background:transparent; color:%4; border:0;}
QLineEdit#languageSearch {background:%3; color:%4; border:1px solid %6; border-radius:7px;
 min-height:23px; padding:7px 11px; font-size:12px;}
QListWidget#languageChoices {background:%2; color:%4; border:0; outline:0; font-size:13px;}
QListWidget#languageChoices::item {padding:7px 10px; border-radius:6px;}
QListWidget#languageChoices::item:disabled {color:%5;background:transparent;}

QFrame#premiumPopupCard {background:transparent; color:%4; border:none;}
QLabel#popoverSection {color:%5; background:transparent; font-size:10px; font-weight:700;}
QCheckBox#popoverCheck, QCheckBox#recommendedView {color:%4; background:transparent; border:0; border-radius:9px;
 padding:10px 8px; spacing:12px; font-size:12px;}
QFrame#premiumPopover[compact="true"] QCheckBox#popoverCheck, QFrame#premiumPopover[compact="true"] QCheckBox#recommendedView {padding:6px 8px;}
QCheckBox#popoverCheck:hover, QCheckBox#recommendedView:hover {background:transparent;}
QCheckBox#popoverCheck::indicator, QCheckBox#recommendedView::indicator {height:18px; width:18px;}
QPushButton#popoverLink {background:%3; border:0; border-radius:7px; padding:10px;
 color:#03AFA7; font-size:12px; font-weight:600;}
QPushButton#popoverLink:hover {background:%3;}
QFrame#popoverDivider {background:%6; border:0;}
QMenu {background:%2; color:%4; border:1px solid %6; padding:6px;}
QMenu::item {padding:8px 26px 8px 22px;}
QMenu::item:selected {background:%3; border-radius:4px;}
QMenu::item:disabled {color:%5;}
QMenu::separator {height:1px; background:%6; margin:6px;}
QMenu::section {color:%5; font-size:10px; padding:7px 12px;}
QProgressBar#mainProgress {background:%3; color:%4; border:0; border-radius:7px; text-align:center;}
QProgressBar#mainProgress::chunk {background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #147CA7,stop:1 #00BDB1); border-radius:7px;}
QWidget#aboutBody {background:transparent;}
QTreeWidget#onlineFiles {background:%2;alternate-background-color:%1;color:%4;border:0;border-radius:13px;outline:0;padding:8px;selection-background-color:%3;selection-color:%4;}
QTreeWidget#onlineFiles::item {padding:11px 8px;border:0;min-height:24px;}
QTreeWidget#onlineFiles::item:hover {background:transparent;}
QTreeWidget#onlineFiles::indicator:unchecked {width:18px;height:18px;background:%2;border:1px solid %7;border-radius:4px;}
QTreeWidget#onlineFiles::indicator:checked {width:18px;height:18px;background:#10AAA0;border:1px solid #10AAA0;border-radius:4px;image:url(:/sdio/assets/icons/check.svg);}
QTreeWidget#onlineFiles QHeaderView::section {background:%2;color:%5;padding:12px 10px;}
QComboBox#candidatePicker::drop-down {background:transparent;border:0;width:34px;}
QComboBox#candidatePicker::down-arrow {image:url(%8);width:14px;height:14px;}
QMessageBox QLabel {color:%4;}
)QSS").arg(background).arg(surface).arg(elevated).arg(primary).arg(secondary).arg(stroke).arg(thumb).arg(comboArrow);
    const QString accent=color("accent",dark).name(),hover=color("accentHover",dark).name(),link=color("link",dark).name();
    const QColor base=color("surface",dark),tint=color("link",dark);
    auto blend=[base,tint](qreal amount){return QColor(qRound(base.red()*(1-amount)+tint.red()*amount),qRound(base.green()*(1-amount)+tint.green()*amount),qRound(base.blue()*(1-amount)+tint.blue()*amount)).name();};
    result+=QStringLiteral("QListWidget#languageChoices::item:hover:enabled {background:%1;color:%3;} QListWidget#languageChoices::item:selected:enabled {background:%2;color:%3;border:1px solid %4;} QListWidget#languageChoices::item:selected:hover:enabled {background:%2;color:%3;}")
        .arg(blend(dark?.17:.09)).arg(blend(dark?.29:.16)).arg(primary).arg(blend(dark?.50:.38));
    result+=QStringLiteral("QTabWidget#comparisonTabs::pane {background:transparent;border:0;} QTabWidget#comparisonTabs QTabBar::tab {background:%1;color:%2;border:1px solid %5;border-radius:11px;margin:0 4px 8px 4px;padding:12px 8px;font-weight:500;} QTabWidget#comparisonTabs QTabBar::tab:hover {color:%3;background:%4;} QTabWidget#comparisonTabs QTabBar::tab:selected {background:%4;color:%3;border-color:%5;} QWidget#comparisonPage,QWidget#candidateHost {background:transparent;} QLabel#detailVersion {font-size:24px;font-weight:600;color:%3;background:transparent;} QLabel#detailHeading {font-size:11px;font-weight:700;background:transparent;} QWidget#comparisonPickerSurface {background:%1;border:1px solid %5;border-radius:13px;}")
        .arg(surface).arg(secondary).arg(primary).arg(blend(dark?.19:.09)).arg(stroke);
    result+=QStringLiteral("QTabWidget#comparisonTabs QScrollArea#dialogScroll,QScrollArea#onlineBodyScroll {background:transparent;border:0;} QWidget#comparisonViewport {background:transparent;}");
    for(const char *value:{"#00AFA5","#00BDB1","#00A99E","#12AAB5","#0EAAA7","#09B5A8"})result.replace(QLatin1String(value),accent,Qt::CaseInsensitive);
    for(const char *value:{"#008B85","#008C85"})result.replace(QLatin1String(value),hover,Qt::CaseInsensitive);
    for(const char *value:{"#0BAEA5","#03AFA7","#08D2C3","#009A92"})result.replace(QLatin1String(value),link,Qt::CaseInsensitive);
    result.replace(QStringLiteral("Segoe UI"),UiFont::family());
    return result;
}
