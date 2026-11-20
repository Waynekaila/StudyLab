#include "ui/theme/ThemeManager.h"

#include "core/Log.h"
#include "services/settings/SettingsService.h"

#include <QApplication>
#include <QFontDatabase>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleHints>

#include <cmath>

namespace studylab::ui {

ThemeManager::ThemeManager(SettingsService& settings, QObject* parent) : QObject(parent), m_settings(settings)
{
    // Avant toute modification de la palette : mémoriser la préférence du système (Qt < 6.5).
    const QPalette sys = QGuiApplication::palette();
    m_initialSystemDark = sys.color(QPalette::Window).lightness() < sys.color(QPalette::WindowText).lightness();

    m_fontFamily = loadBundledFonts();

    connect(&m_settings, &SettingsService::changed, this, [this](const QString& key) {
        if (key == SettingKeys::Theme || key == SettingKeys::UiScale)
            apply();
    });
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (m_settings.themeMode() == ThemeMode::System)
            apply();
    });
#endif
}

QString ThemeManager::loadBundledFonts()
{
    static QString family;
    if (!family.isEmpty())
        return family;
    for (const char* file : {":/fonts/Inter-Regular.otf", ":/fonts/Inter-Medium.otf", ":/fonts/Inter-SemiBold.otf"}) {
        const int id = QFontDatabase::addApplicationFont(QLatin1String(file));
        if (id >= 0 && family.isEmpty()) {
            const QStringList families = QFontDatabase::applicationFontFamilies(id);
            if (!families.isEmpty())
                family = families.first();
        }
    }
    if (family.isEmpty()) {
        qCWarning(lcUi) << "Police Inter non chargée, police système utilisée";
        family = QApplication::font().family();
    }
    return family;
}

bool ThemeManager::systemPrefersDark() const
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme != Qt::ColorScheme::Unknown)
        return scheme == Qt::ColorScheme::Dark;
#endif
    return m_initialSystemDark;
}

int ThemeManager::px(int base) const { return int(std::lround(base * m_scale)); }

void ThemeManager::apply()
{
    const ThemeMode mode = m_settings.themeMode();
    const bool dark = mode == ThemeMode::Dark || (mode == ThemeMode::System && systemPrefersDark());
    m_palette = dark ? Palette::darkPalette() : Palette::light();
    m_scale = m_settings.uiScale();

    auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (!app)
        return;

    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        QApplication::setStyle(fusion);

    QPalette pal;
    pal.setColor(QPalette::Window, m_palette.bg);
    pal.setColor(QPalette::WindowText, m_palette.text);
    pal.setColor(QPalette::Base, m_palette.surface);
    pal.setColor(QPalette::AlternateBase, m_palette.surface2);
    pal.setColor(QPalette::Text, m_palette.text);
    pal.setColor(QPalette::PlaceholderText, m_palette.text3);
    pal.setColor(QPalette::Button, m_palette.surface);
    pal.setColor(QPalette::ButtonText, m_palette.text);
    pal.setColor(QPalette::Highlight, m_palette.primary);
    pal.setColor(QPalette::HighlightedText, m_palette.onPrimary);
    pal.setColor(QPalette::ToolTipBase, m_palette.surface);
    pal.setColor(QPalette::ToolTipText, m_palette.text);
    pal.setColor(QPalette::Link, m_palette.primaryStrong);
    pal.setColor(QPalette::Mid, m_palette.border);
    pal.setColor(QPalette::Midlight, m_palette.borderSoft);
    pal.setColor(QPalette::Light, m_palette.surface);
    pal.setColor(QPalette::Dark, m_palette.text3);
    pal.setColor(QPalette::Disabled, QPalette::Text, m_palette.text3);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, m_palette.text3);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, m_palette.text3);
    QApplication::setPalette(pal);

    QFont font(m_fontFamily);
    font.setPixelSize(px(Metrics::BaseFontPx));
    font.setHintingPreference(QFont::PreferNoHinting);
    QApplication::setFont(font);

    m_styleSheet = buildStyleSheet(m_palette, m_scale, m_fontFamily);
    app->setStyleSheet(m_styleSheet);

    emit themeChanged();
}

QString ThemeManager::buildStyleSheet(const Palette& p, double scale, const QString& fontFamily)
{
    auto s = [scale](int v) { return QString::number(int(std::lround(v * scale))) + QStringLiteral("px"); };

    QString qss = QStringLiteral(R"QSS(
* { font-family: "%{font}"; }
QWidget { color: %{text}; font-size: %{fs13}; }
QMainWindow, QWidget#AppRoot { background: %{bg}; }
QWidget#PageBody, QScrollArea#PageScroll, QScrollArea#PageScroll > QWidget > QWidget { background: %{bg}; }
QScrollArea { border: none; background: transparent; }

QFrame#Sidebar { background: %{surface2}; border: none; border-right: 1px solid %{borderSoft}; }
QLabel#Brand { font-size: %{fs15}; font-weight: 600; }
QToolButton[nav="true"] {
    background: transparent; border: none; border-radius: 8px;
    color: %{text2}; font-weight: 500; text-align: left;
    padding: %{p7} %{p10}; font-size: %{fs13};
}
QToolButton[nav="true"]:hover { background: %{sunken}; }
QToolButton[nav="true"]:checked { background: %{primarySoft}; color: %{primaryStrong}; }
QToolButton[nav="true"]:focus { outline: none; border: 1px solid %{primary}; }
QLabel#SidebarFooter { color: %{text3}; font-size: %{fs12}; }
QFrame#SidebarFooterLine { background: %{borderSoft}; max-height: 1px; border: none; }

QLabel[role="h1"] { font-size: %{fs22}; font-weight: 600; }
QLabel[role="h2"] { font-size: %{fs15}; font-weight: 600; }
QLabel[role="h3"] { font-size: %{fs13}; font-weight: 600; }
QLabel[role="subtitle"] { color: %{text2}; }
QLabel[role="caption"] { color: %{text3}; font-size: %{fs11}; font-weight: 600; letter-spacing: 1px; }
QLabel[role="muted"] { color: %{text2}; }
QLabel[role="faint"] { color: %{text3}; font-size: %{fs12}; }
QLabel[role="big"] { font-size: %{fs30}; font-weight: 600; }
QLabel[role="mono"] { font-family: "JetBrains Mono", "DejaVu Sans Mono", "Consolas", monospace; color: %{text2}; font-size: %{fs12}; }
QLabel[status="ok"] { color: %{sageText}; font-weight: 500; }
QLabel[status="error"] { color: %{danger}; font-weight: 500; }
QLabel a { color: %{primaryStrong}; }

QFrame[card="true"] { background: %{surface}; border: 1px solid %{borderSoft}; border-radius: 12px; }
QFrame[separator="true"] { background: %{borderSoft}; border: none; max-height: 1px; min-height: 1px; }

QPushButton {
    background: %{surface}; color: %{text}; border: 1px solid %{border}; border-radius: 8px;
    padding: %{p6} %{p12}; font-weight: 500; min-height: %{h20};
}
QPushButton:hover { background: %{sunken}; }
QPushButton:pressed { background: %{border}; }
QPushButton:focus { border: 1px solid %{primary}; }
QPushButton:disabled { color: %{text3}; }
QPushButton[variant="primary"] { background: %{primary}; border-color: %{primary}; color: %{onPrimary}; }
QPushButton[variant="primary"]:hover { background: %{primaryStrong}; border-color: %{primaryStrong}; }
QPushButton[variant="ghost"] { background: transparent; border-color: transparent; color: %{text2}; }
QPushButton[variant="ghost"]:hover { background: %{sunken}; }

QFrame[segmented="true"] { background: %{sunken}; border: none; border-radius: 8px; }
QPushButton[segment="true"] {
    background: transparent; border: 1px solid transparent; border-radius: 6px;
    color: %{text2}; padding: %{p4} %{p12}; min-height: %{h18};
}
QPushButton[segment="true"]:hover { color: %{text}; }
QPushButton[segment="true"]:checked { background: %{surface}; color: %{text}; border: 1px solid %{borderSoft}; }

QLineEdit, QComboBox {
    background: %{surface}; border: 1px solid %{border}; border-radius: 8px;
    padding: %{p5} %{p10}; min-height: %{h20}; selection-background-color: %{primary}; selection-color: %{onPrimary};
}
QLineEdit:focus, QComboBox:focus { border: 1px solid %{primary}; }
QComboBox::drop-down { border: none; width: %{p24}; }
QComboBox::down-arrow { image: url(%{chevron}); width: 12px; height: 12px; }
QComboBox QAbstractItemView {
    background: %{surface}; border: 1px solid %{border}; border-radius: 8px; padding: 4px; outline: none;
    selection-background-color: %{primarySoft}; selection-color: %{primaryStrong};
}

QListWidget#Categories { background: transparent; border: none; outline: none; }
QListWidget#Categories::item { padding: %{p7} %{p10}; border-radius: 6px; color: %{text2}; margin: 1px 0; }
QListWidget#Categories::item:hover { background: %{sunken}; }
QListWidget#Categories::item:selected { background: %{primarySoft}; color: %{primaryStrong}; }
QPushButton#SidebarSearch {
    text-align: left; background: %{surface}; color: %{text3}; border: 1px solid %{borderSoft}; border-radius: 8px;
    padding: %{p6} %{p10}; font-weight: 400;
}
QPushButton#SidebarSearch:hover { border-color: %{border}; color: %{text2}; }
QPushButton#HomeRow { text-align: left; background: transparent; border: none; border-radius: 8px; padding: 0; }
QPushButton#HomeRow:hover { background: %{surface2}; }
QPushButton#HomeRow:focus { background: %{primarySoft}; }
QPushButton#CardLink { background: transparent; border: none; color: %{primary}; padding: 0 %{p2}; font-weight: 500; }
QPushButton#CardLink:hover { color: %{primaryStrong}; text-decoration: underline; }
QPushButton#NoteChip {
    background: %{sunken}; color: %{text2}; border: none; border-radius: 8px; padding: %{p2} %{p10};
    font-size: %{fs12}; font-weight: 400;
}
QPushButton#NoteChip:hover { background: %{primarySoft}; color: %{primaryStrong}; }
QToolButton#FavoriteCover { background: transparent; border: none; border-radius: 6px; padding: 2px; }
QToolButton#FavoriteCover:hover { background: %{surface2}; }
QLabel#HomeRowCount { font-weight: 600; }
QLabel#ResumeQuote { background: %{surface2}; border-left: 3px solid %{accYellow}; border-radius: 6px; padding: %{p6} %{p10}; }
QFrame#SearchPalette { background: %{surface}; border: 1px solid %{border}; border-radius: 14px; }
QListWidget#SearchResults { background: transparent; border: none; outline: none; }
QListWidget#SearchResults::item { border-radius: 8px; }
QListWidget#SearchResults::item:selected { background: %{primarySoft}; }
QFrame#SubSidebar { background: %{surface2}; border: none; border-right: 1px solid %{borderSoft}; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: %{border}; border-radius: 3px; min-height: 32px; }
QScrollBar::handle:vertical:hover { background: %{text3}; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: %{border}; border-radius: 3px; min-width: 32px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QToolTip { background: %{surface}; color: %{text}; border: 1px solid %{border}; border-radius: 6px; padding: 4px 8px; }

QPlainTextEdit, QSpinBox {
    background: %{surface}; border: 1px solid %{border}; border-radius: 8px; padding: %{p5} %{p8};
    selection-background-color: %{primary}; selection-color: %{onPrimary};
}
QPlainTextEdit:focus, QSpinBox:focus { border: 1px solid %{primary}; }
QSpinBox { min-height: %{h20}; }
QDialog { background: %{bg}; }
QMenu { background: %{surface}; border: 1px solid %{border}; border-radius: 8px; padding: 4px; }
QMenu::item { padding: %{p6} %{p12}; border-radius: 6px; }
QMenu::item:selected { background: %{primarySoft}; color: %{primaryStrong}; }
QPushButton::menu-indicator { image: none; width: 0; }

QListView#LibraryView { background: %{bg}; border: none; outline: none; }
QFrame#DetailPanel { background: %{surface}; border: none; border-left: 1px solid %{borderSoft}; }
QWidget#DetailContent { background: %{surface}; }
QLabel[chip="true"] {
    background: %{sunken}; color: %{text2}; border-radius: 6px; padding: %{p4} %{p8}; font-size: %{fs12};
}
QPushButton[chipButton="true"] { border-radius: 8px; }
QPushButton[chipButton="true"]:checked { background: %{primarySoft}; color: %{primaryStrong}; border-color: %{primarySoft}; }
)QSS"  // MSVC : une chaîne littérale de plus de 16 Ko est refusée, la feuille est coupée en deux
    R"QSS(
QToolButton#ReaderMoreButton::menu-indicator { image: none; width: 0; }
QToolButton#FavoriteButton {
    background: %{surface}; border: 1px solid %{border}; border-radius: 8px; color: %{amberText};
    font-size: %{fs15}; min-width: %{h30}; min-height: %{h30};
}
QToolButton#FavoriteButton:hover { background: %{sunken}; }
QFrame#ImportBanner { background: %{primarySoft}; border: 1px solid %{primarySoft}; border-radius: 10px; }
QFrame#ImportBanner[state="done"] { background: %{sageSoft}; border-color: %{sageSoft}; }
QFrame#ImportBanner[state="warning"] { background: %{amberSoft}; border-color: %{amberSoft}; }
QFrame#ReaderToolbar { background: %{surface}; border: none; border-bottom: 1px solid %{borderSoft}; }
QFrame#ReaderSearchBar { background: %{surface}; border: none; border-bottom: 1px solid %{borderSoft}; }
QFrame#ReaderSide { background: %{surface2}; border: none; border-right: 1px solid %{borderSoft}; }
QListView#Thumbnails, QTreeView#Outline { background: transparent; border: none; outline: none; }
QTreeView#Outline::item { padding: %{p4} %{p4}; border-radius: 6px; color: %{text2}; }
QTreeView#Outline::item:hover { background: %{sunken}; }
QTreeView#Outline::item:selected { background: %{primarySoft}; color: %{primaryStrong}; }
QToolButton[readerTool="true"] { background: transparent; border: 1px solid transparent; border-radius: 6px; padding: %{p4}; }
QToolButton[readerTool="true"]:hover { background: %{sunken}; }
QToolButton[readerTool="true"]:checked { background: %{primarySoft}; }
QToolButton[readerTool="true"]:disabled { background: transparent; }
QLineEdit#PageEdit { padding: %{p4} %{p4}; }
QTabWidget#ReaderTabs::pane { border: none; }
QTabWidget#ReaderTabs QTabBar { background: %{surface2}; }
QTabBar::tab {
    background: transparent; color: %{text2}; border: none; border-right: 1px solid %{borderSoft};
    padding: %{p7} %{p12}; min-width: 60px;
}
QTabBar::tab:selected { background: %{surface}; color: %{text}; }
QTabBar::tab:hover:!selected { background: %{sunken}; }
QToolButton#TabClose { border: none; border-radius: 4px; padding: 2px; background: transparent; }
QToolButton#TabClose:hover { background: %{sunken}; }
QFrame#ToolGroup { background: %{surface2}; border: 1px solid %{borderSoft}; border-radius: 8px; }
QToolButton[swatch="true"] { padding: %{p2}; }
QToolButton[swatch="true"]:checked { background: transparent; }
QFrame#AnnotationPopup { background: %{surface}; border: 1px solid %{border}; border-radius: 10px; }
QFrame#TypographyPanel { background: %{surface}; border: 1px solid %{border}; border-radius: 12px; }
QToolButton[readingTheme="true"] { border: 2px solid transparent; border-radius: 9px; padding: 1px; background: transparent; }
QToolButton[readingTheme="true"]:checked { border-color: %{primary}; }
QLabel#EpubChapter { font-weight: 600; }
QFrame#AnnotationsPane { background: %{surface2}; border: none; border-left: 1px solid %{borderSoft}; }
QListView#AnnotationList { background: transparent; border: none; outline: none; }
QFrame#CommentsPane { background: %{surface2}; border: none; border-left: 1px solid %{borderSoft}; border-top: 1px solid %{borderSoft}; }
QSplitter#ReaderSidePanel::handle { background: %{surface2}; }
QSplitter#ReaderSidePanel::handle:hover { background: %{borderSoft}; }
QFrame#CommentEditor { background: %{surface}; border: 1px solid %{border}; border-radius: 12px; }
QPlainTextEdit#CommentText { background: transparent; border: none; padding: 0; selection-background-color: %{primarySoft}; }
QLabel#CountBadge { background: %{sunken}; color: %{text2}; border-radius: 9px; padding: 1px %{p7}; font-size: %{fs11}; }
QToolButton#FilterChip {
    background: %{surface}; color: %{text2}; border: 1px solid %{borderSoft}; border-radius: 12px;
    padding: %{p2} %{p7}; font-size: %{fs11};
}
QToolButton#FilterChip:hover { background: %{sunken}; }
QToolButton#SmartValues {
    background: %{surface}; border: 1px solid %{border}; border-radius: 8px; color: %{text};
    padding: %{p5} %{p10}; min-height: %{h20}; text-align: left;
}
QToolButton#SmartValues::menu-indicator { subcontrol-position: right center; right: %{p7}; }
QFrame#StatTile QProgressBar { background: %{sunken}; border: none; border-radius: 3px; }
QFrame#StatTile QProgressBar::chunk { background: %{primary}; border-radius: 3px; }
QToolButton#FilterChip:checked { background: %{primarySoft}; color: %{primaryStrong}; border-color: %{primarySoft}; }
QFrame#UndoBar { background: %{text}; border-radius: 8px; }
QFrame#UndoBar QLabel { color: %{surface}; }
QFrame#UndoBar QPushButton { color: %{lavender}; font-weight: 600; }
QProgressBar { background: %{sunken}; border: none; border-radius: 3px; }
QProgressBar::chunk { background: %{primary}; border-radius: 3px; }
QMessageBox { background: %{surface}; }
QFrame#NotesNav { background: %{surface2}; border: none; border-right: 1px solid %{borderSoft}; }
QListWidget#ReferenceList { background: transparent; border: none; outline: none; }
QListWidget#ReferenceList::item { padding: %{p6} %{p8}; border-radius: 6px; color: %{text}; margin: 1px 0; }
QListWidget#ReferenceList::item:hover { background: %{sunken}; }
QListWidget#ReferenceList::item:selected { background: %{primarySoft}; color: %{primaryStrong}; }
QLabel#ReferenceEntry { background: %{surface2}; border: 1px solid %{borderSoft}; border-radius: 8px; padding: %{p10} %{p12}; }
QToolButton#GraphZoomIn, QToolButton#GraphZoomOut, QToolButton#GraphFit, QToolButton#GraphShowAll {
    background: %{surface}; color: %{text2}; border: 1px solid %{border}; border-radius: 8px; padding: %{p6};
}
QToolButton#GraphZoomIn:hover, QToolButton#GraphZoomOut:hover, QToolButton#GraphFit:hover, QToolButton#GraphShowAll:hover {
    background: %{sunken};
}
QTreeWidget#NoteTree { background: transparent; border: none; outline: none; }
QTreeWidget#NoteTree::item { padding: %{p4} %{p2}; border-radius: 6px; color: %{text}; }
QTreeWidget#NoteTree::item:hover { background: %{sunken}; }
QTreeWidget#NoteTree::item:selected { background: %{primarySoft}; color: %{primaryStrong}; }
QFrame#NoteEditorPane { background: %{surface}; border: none; }
QFrame#NoteBar { background: %{surface}; border: none; }
QLineEdit#NoteTitle { background: transparent; border: none; border-radius: 0; font-size: %{fs22}; font-weight: 600; padding: %{p4} 0; }
QPlainTextEdit#NoteText {
    font-family: "JetBrains Mono", "DejaVu Sans Mono", "Consolas", monospace; font-size: %{fs13};
    background: transparent; border: none; border-radius: 0; padding: %{p8} 0;
    selection-background-color: %{primarySoft}; selection-color: %{text};
}
QToolButton#NoteTypeChip {
    background: %{primarySoft}; color: %{primaryStrong}; border: none; border-radius: 8px;
    padding: %{p4} %{p10}; font-weight: 600;
}
QToolButton#NoteTypeChip::menu-indicator { image: none; width: 0; }
QToolButton#NewNoteButton::menu-indicator { image: none; width: 0; }
QLabel#SaveState { color: %{text3}; }
QSplitter#NoteSplit::handle { background: %{borderSoft}; }
QFrame#NoteContext { background: %{surface2}; border: none; border-left: 1px solid %{borderSoft}; }
QWidget#NoteContextBody { background: %{surface2}; }
QPushButton#NoteLinkRow { text-align: left; background: %{surface}; border: 1px solid %{borderSoft}; border-radius: 10px; padding: 0; }
QPushButton#NoteLinkRow:hover { border-color: %{border}; }
QPushButton#NoteLinkRow:focus { border-color: %{primary}; }
QPushButton#OutlineItem {
    text-align: left; background: transparent; border: none; border-radius: 6px; color: %{text2};
    padding: %{p4}; font-weight: 400; min-height: 0;
}
QPushButton#OutlineItem:hover { color: %{primaryStrong}; background: %{sunken}; }
QPushButton#CreateLinkedNote { color: %{primaryStrong}; padding: %{p2} %{p6}; min-height: 0; }
QFrame#DraftBanner { background: %{amberSoft}; border: none; border-bottom: 1px solid %{borderSoft}; }
QLabel#DraftBannerText { color: %{amberText}; }
QLabel#NavBadge {
    background: %{lavenderSoft}; color: %{lavenderText}; border-radius: 9px; padding: 0 %{p6};
    font-size: %{fs11}; font-weight: 600; min-width: 10px; min-height: 18px; max-height: 18px;
}
QToolButton[nav="true"]:checked QLabel#NavBadge { background: %{surface}; color: %{primaryStrong}; }
QListWidget#DeckList { background: transparent; border: none; outline: none; }
QTreeWidget#CardList { background: %{surface}; border: 1px solid %{borderSoft}; border-radius: 10px; padding: 4px; outline: none; }
QTreeWidget#CardList::item { padding: %{p4} %{p2}; }
QTreeWidget#CardList::item:selected { background: %{primarySoft}; color: %{primaryStrong}; }
QProgressBar#StudyProgress { background: %{sunken}; border: none; border-radius: 3px; }
QProgressBar#StudyProgress::chunk { background: %{lavender}; border-radius: 3px; }
QWidget#StudySession, QWidget#StudyFinished { background: %{bg}; }
QPushButton#RateButton { background: %{surface}; border: 1px solid %{borderSoft}; border-radius: 10px; padding: 0; min-height: %{h50}; }
QPushButton#RateButton:hover { border-color: %{border}; background: %{surface2}; }
QPushButton#RateButton[rating="1"]:hover { border-color: %{danger}; }
QPushButton#RateButton[rating="3"]:hover, QPushButton#RateButton:focus { border-color: %{primary}; }
QLabel#RateTitle { font-weight: 600; font-size: %{fs13}; }
QLabel#RateTitle[rating="1"] { color: %{danger}; }
QLabel#RateTitle[rating="2"] { color: %{amberText}; }
QLabel#RateTitle[rating="3"] { color: %{primaryStrong}; }
QLabel#RateTitle[rating="4"] { color: %{sageText}; }
)QSS");

    const QColor sageText = p.dark ? p.sage : p.sage.darker(140);
    const double t = p.dark ? 0.22 : 0.18;  // ambre très atténué sur la surface
    const QColor amberSoft = QColor::fromRgbF(float(p.surface.redF() * (1 - t) + p.amber.redF() * t),
                                              float(p.surface.greenF() * (1 - t) + p.amber.greenF() * t),
                                              float(p.surface.blueF() * (1 - t) + p.amber.blueF() * t));
    const QList<QPair<QString, QString>> tokens = {
        {QStringLiteral("font"), fontFamily},
        {QStringLiteral("chevron"), p.dark ? QStringLiteral(":/theme/chevron-down-dark.svg") : QStringLiteral(":/theme/chevron-down-light.svg")},
        {QStringLiteral("bg"), p.bg.name()},
        {QStringLiteral("surface2"), p.surface2.name()},
        {QStringLiteral("surface"), p.surface.name()},
        {QStringLiteral("sunken"), p.sunken.name()},
        {QStringLiteral("borderSoft"), p.borderSoft.name()},
        {QStringLiteral("border"), p.border.name()},
        {QStringLiteral("text2"), p.text2.name()},
        {QStringLiteral("text3"), p.text3.name()},
        {QStringLiteral("text"), p.text.name()},
        {QStringLiteral("primarySoft"), p.primarySoft.name()},
        {QStringLiteral("primaryStrong"), p.primaryStrong.name()},
        {QStringLiteral("primary"), p.primary.name()},
        {QStringLiteral("onPrimary"), p.onPrimary.name()},
        {QStringLiteral("sageText"), sageText.name()},
        {QStringLiteral("sageSoft"), p.sageSoft.name()},
        {QStringLiteral("amberSoft"), amberSoft.name()},
        {QStringLiteral("amberText"), (p.dark ? p.amber : p.amber.darker(130)).name()},
        {QStringLiteral("danger"), p.danger.name()},
        {QStringLiteral("lavenderSoft"), p.lavenderSoft.name()},
        {QStringLiteral("lavenderText"), (p.dark ? p.lavender : p.lavender.darker(150)).name()},
        {QStringLiteral("lavender"), p.lavender.name()},
        {QStringLiteral("accYellow"), p.accYellow.name()},
    };
    for (const auto& [key, value] : tokens)
        qss.replace(QStringLiteral("%{") + key + QLatin1Char('}'), value);
    for (int v : {2, 4, 5, 6, 7, 8, 10, 12, 24})
        qss.replace(QStringLiteral("%{p%1}").arg(v), s(v));
    for (int v : {18, 20, 30, 50})
        qss.replace(QStringLiteral("%{h%1}").arg(v), s(v));
    for (int v : {11, 12, 13, 15, 22, 30})
        qss.replace(QStringLiteral("%{fs%1}").arg(v), s(v));
    return qss;
}

}  // namespace studylab::ui
