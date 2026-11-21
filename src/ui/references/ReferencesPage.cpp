#include "ui/references/ReferencesPage.h"

#include "services/library/LibraryService.h"
#include "services/notes/NoteService.h"
#include "ui/components/PanelSplitter.h"
#include "ui/components/Widgets.h"
#include "ui/pages/PageScaffold.h"
#include "ui/theme/Icons.h"
#include "ui/theme/ThemeManager.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>

namespace studylab::ui {

namespace {

constexpr int kIdRole = Qt::UserRole;

QList<PersonName> namesFrom(const QString& text)
{
    QList<PersonName> out;
    for (const QString& line : text.split(u'\n')) {
        const PersonName n = parsePersonName(line);
        if (!n.isEmpty())
            out << n;
    }
    return out;
}

QString namesText(const QList<PersonName>& names)
{
    QStringList lines;
    for (const PersonName& n : names)
        lines << (n.literal.isEmpty() ? n.sortName() : u'{' + n.literal + u'}');
    return lines.join(u'\n');
}

void clearRows(QVBoxLayout* layout)
{
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }
}

}  // namespace

ReferencesPage::ReferencesPage(AppContext& ctx, QWidget* parent)
    : QWidget(parent), m_ctx(ctx), m_refs(ctx.db, ctx.notes)
{
    setObjectName(QStringLiteral("ReferencesPage"));
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    m_panels = new PanelSplitter(ctx.settings, ctx.theme, QStringLiteral("references"), this);
    h->addWidget(m_panels, 1);
    buildNav();
    buildEditor();

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(700);
    connect(&m_saveTimer, &QTimer::timeout, this, [this] { saveNow(); });
    const auto stale = [this] {
        m_stale = true;
        if (isVisible())
            refresh();
    };
    connect(&m_ctx.library, &LibraryService::documentsChanged, this, stale);
    // Un import de coffre (page Notes) peut ajouter des références depuis ses fichiers .bib.
    connect(&m_ctx.notes, &NoteService::linksChanged, this, stale);
    connect(&m_ctx.notes, &NoteService::noteChanged, this, [this] {
        if (m_current.id > 0 && isVisible())
            renderCiting();
    });
    connect(&m_ctx.theme, &ThemeManager::themeChanged, this, [this] { renderPreview(); });
    m_stale = true;  // chargé au premier affichage
}

void ReferencesPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_stale)
        refresh();
}

void ReferencesPage::buildNav()
{
    ThemeManager& theme = m_ctx.theme;
    auto* nav = new QFrame(this);
    nav->setObjectName(QStringLiteral("NotesNav"));
    auto* v = new QVBoxLayout(nav);
    v->setContentsMargins(12, 14, 12, 12);
    v->setSpacing(10);

    auto* head = new QHBoxLayout;
    head->setContentsMargins(6, 0, 0, 0);
    head->addWidget(makeLabel(tr("Références"), "h2", nav), 1);
    auto* add = new QToolButton(nav);
    add->setObjectName(QStringLiteral("NewReferenceButton"));
    add->setProperty("readerTool", true);
    add->setIcon(Icons::get(QStringLiteral("plus"), theme.palette().text2, 18));
    add->setToolTip(tr("Nouvelle référence"));
    add->setAccessibleName(add->toolTip());
    connect(add, &QToolButton::clicked, this, [this] { createReference(); });
    head->addWidget(add);
    m_navHead = head;
    v->addLayout(head);

    m_filter = new QLineEdit(nav);
    m_filter->setObjectName(QStringLiteral("NoteFilter"));
    m_filter->setPlaceholderText(tr("Filtrer : auteur, titre, clé, année…"));
    m_filter->setClearButtonEnabled(true);
    connect(m_filter, &QLineEdit::textChanged, this, [this] { fillList(); });
    v->addWidget(m_filter);

    m_list = new QListWidget(nav);
    m_list->setObjectName(QStringLiteral("ReferenceList"));
    m_list->setAccessibleName(tr("Références"));
    m_list->setWordWrap(true);
    m_list->setTextElideMode(Qt::ElideRight);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (item && item->data(kIdRole).toLongLong() != m_current.id)
            showReference(item->data(kIdRole).toLongLong());
    });
    v->addWidget(m_list, 1);

    m_navStatus = makeLabel(QString(), "faint", nav);
    m_navStatus->setObjectName(QStringLiteral("ReferenceImportStatus"));
    m_navStatus->setWordWrap(true);
    v->addWidget(m_navStatus);

    auto* io = new QHBoxLayout;
    io->setSpacing(6);
    auto* importBtn = new QPushButton(tr("Importer…"), nav);
    importBtn->setObjectName(QStringLiteral("ImportReferences"));
    importBtn->setToolTip(tr("BibTeX (.bib) ou CSL-JSON (.json), depuis Zotero, JabRef, Mendeley…"));
    connect(importBtn, &QPushButton::clicked, this, &ReferencesPage::chooseImport);
    auto* exportBtn = new QPushButton(tr("Exporter…"), nav);
    exportBtn->setObjectName(QStringLiteral("ExportReferences"));
    exportBtn->setToolTip(tr("Toutes les références, en BibTeX ou en CSL-JSON"));
    connect(exportBtn, &QPushButton::clicked, this, &ReferencesPage::chooseExport);
    io->addWidget(importBtn, 1);
    io->addWidget(exportBtn, 1);
    v->addLayout(io);
    m_panels->addPanel(nav, tr("Références"), theme.px(280), theme.px(220), theme.px(480));
    if (m_navHead)
        m_navHead->addWidget(m_panels->makeCollapseButton(nav, nav));
}

void ReferencesPage::buildEditor()
{
    ThemeManager& theme = m_ctx.theme;
    m_center = new QStackedWidget(this);

    m_empty = new QWidget(m_center);
    auto* ev = new QVBoxLayout(m_empty);
    ev->addStretch(1);
    auto* et = makeLabel(tr("Citez vos sources dans vos notes"), "h2", m_empty);
    et->setAlignment(Qt::AlignCenter);
    ev->addWidget(et);
    auto* ed = makeLabel(tr("Importez un fichier BibTeX ou CSL-JSON (Zotero, JabRef…), créez la fiche d'un document "
                            "depuis la bibliothèque, ou saisissez une référence. Dans une note, [[@clé]] la cite."),
                         "muted", m_empty);
    ed->setWordWrap(true);
    ed->setAlignment(Qt::AlignCenter);
    ed->setMaximumWidth(theme.px(480));
    // Centré par une rangée, pas par un alignement : un texte replié aligné perd sa hauteur et sa fin est coupée.
    auto* edRow = new QHBoxLayout;
    edRow->addStretch(1);
    edRow->addWidget(ed, 6);
    edRow->addStretch(1);
    ev->addLayout(edRow);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto* imp = new QPushButton(tr("Importer…"), m_empty);
    imp->setProperty("variant", QStringLiteral("primary"));
    connect(imp, &QPushButton::clicked, this, &ReferencesPage::chooseImport);
    auto* create = new QPushButton(tr("Nouvelle référence"), m_empty);
    connect(create, &QPushButton::clicked, this, [this] { createReference(); });
    buttons->addWidget(imp);
    buttons->addWidget(create);
    buttons->addStretch(1);
    ev->addLayout(buttons);
    ev->addStretch(2);
    m_center->addWidget(m_empty);

    auto* scaffold = new PageScaffold(m_center, 820);
    QVBoxLayout* body = scaffold->body();
    body->setSpacing(16);

    m_kind = makeLabel(QString(), "caption");
    body->addWidget(m_kind);
    m_citation = makeLabel(QString(), "h1");
    m_citation->setObjectName(QStringLiteral("ReferenceCitation"));
    m_citation->setWordWrap(true);
    body->addWidget(m_citation);
    m_entry = makeLabel(QString());
    m_entry->setObjectName(QStringLiteral("ReferenceEntry"));
    m_entry->setTextFormat(Qt::RichText);
    m_entry->setWordWrap(true);
    m_entry->setTextInteractionFlags(Qt::TextSelectableByMouse);
    body->addWidget(m_entry);

    auto* actions = new QHBoxLayout;
    actions->setSpacing(8);
    auto* copyCite = new QPushButton(tr("Copier [[@clé]]"));
    copyCite->setObjectName(QStringLiteral("CopyCitation"));
    copyCite->setToolTip(tr("À coller dans une note pour citer cette référence"));
    connect(copyCite, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(QStringLiteral("[[@%1]]").arg(m_current.citekey));
    });
    auto* copyEntry = new QPushButton(tr("Copier l'entrée"));
    connect(copyEntry, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(formatBibliography(m_current));
    });
    auto* copyBib = new QPushButton(tr("Copier en BibTeX"));
    connect(copyBib, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(writeBibtex({m_current})); });
    m_openDocument = new QPushButton(tr("Ouvrir le document"));
    m_openDocument->setProperty("variant", QStringLiteral("primary"));
    connect(m_openDocument, &QPushButton::clicked, this, [this] {
        if (m_current.documentId > 0)
            emit openDocumentRequested(m_current.documentId);
    });
    actions->addWidget(m_openDocument);
    actions->addWidget(copyCite);
    actions->addWidget(copyEntry);
    actions->addWidget(copyBib);
    actions->addStretch(1);
    body->addLayout(actions);

    // Fiche
    auto* card = makeCard();
    auto* cl = static_cast<QVBoxLayout*>(card->layout());
    cl->addWidget(makeLabel(tr("Fiche"), "h3"));
    auto* form = new QFormLayout;
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(8);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto line = [this](const QString& placeholder = {}) {
        auto* e = new QLineEdit;
        e->setPlaceholderText(placeholder);
        connect(e, &QLineEdit::textEdited, this, &ReferencesPage::markDirty);
        return e;
    };

    auto* keyRow = new QHBoxLayout;
    keyRow->setSpacing(6);
    m_key = new QLineEdit;
    m_key->setObjectName(QStringLiteral("ReferenceKey"));
    m_key->setToolTip(tr("Lettres, chiffres et « _ : . - ». La changer met à jour les notes qui la citent."));
    connect(m_key, &QLineEdit::editingFinished, this, &ReferencesPage::commitKey);
    m_generateKey = new QToolButton;
    m_generateKey->setObjectName(QStringLiteral("GenerateKey"));
    m_generateKey->setProperty("readerTool", true);
    m_generateKey->setIcon(Icons::get(QStringLiteral("sparkle"), theme.palette().text2, 16));
    m_generateKey->setToolTip(tr("Clé tirée du premier auteur, de l'année et du titre"));
    m_generateKey->setAccessibleName(m_generateKey->toolTip());
    connect(m_generateKey, &QToolButton::clicked, this, [this] {
        QSet<QString> taken;
        for (const Reference& r : std::as_const(m_all))
            if (r.id != m_current.id)
                taken.insert(r.citekey.toLower());
        m_key->setText(makeCitekey(formValue(), taken));
        commitKey();
    });
    keyRow->addWidget(m_key, 1);
    keyRow->addWidget(m_generateKey);
    form->addRow(tr("Clé"), keyRow);

    m_type = new QComboBox;
    m_type->setObjectName(QStringLiteral("ReferenceType"));
    for (const QString& t : referenceTypes())
        m_type->addItem(referenceTypeLabel(t), t);
    connect(m_type, &QComboBox::activated, this, &ReferencesPage::markDirty);
    form->addRow(tr("Type"), m_type);
    m_title = line();
    m_title->setObjectName(QStringLiteral("ReferenceTitle"));
    form->addRow(tr("Titre"), m_title);
    auto names = [this](const QString& placeholder) {
        auto* e = new QPlainTextEdit;
        e->setPlaceholderText(placeholder);
        e->setTabChangesFocus(true);
        e->setFixedHeight(m_ctx.theme.px(64));
        connect(e, &QPlainTextEdit::textChanged, this, [this] {
            if (!m_loading)
                markDirty();
        });
        return e;
    };
    m_authors = names(tr("Un par ligne : « Çengel, Yunus A. » ou « Yunus A. Çengel » ; institution entre accolades"));
    m_authors->setObjectName(QStringLiteral("ReferenceAuthors"));
    form->addRow(tr("Auteurs"), m_authors);
    m_editors = names(tr("Directeurs de l'ouvrage, un par ligne"));
    form->addRow(tr("Direction"), m_editors);
    m_year = line(tr("2019"));
    m_year->setValidator(new QIntValidator(0, 9999, m_year));
    m_year->setMaximumWidth(theme.px(90));
    form->addRow(tr("Année"), m_year);
    m_container = line(tr("Revue, ouvrage collectif, actes, site"));
    form->addRow(tr("Paru dans"), m_container);
    m_publisher = line(tr("Éditeur, université, institution"));
    form->addRow(tr("Éditeur"), m_publisher);
    m_place = line();
    form->addRow(tr("Lieu"), m_place);
    auto* numbers = new QHBoxLayout;
    numbers->setSpacing(8);
    m_volume = line(tr("Volume"));
    m_issue = line(tr("Numéro"));
    m_pages = line(tr("Pages"));
    m_edition = line(tr("Édition"));
    for (QLineEdit* e : {m_volume, m_issue, m_pages, m_edition})
        numbers->addWidget(e, 1);
    form->addRow(tr("Vol. · n° · p. · éd."), numbers);
    m_doi = line(tr("10.1000/xyz123"));
    form->addRow(tr("DOI"), m_doi);
    m_isbn = line();
    form->addRow(tr("ISBN"), m_isbn);
    m_url = line(tr("https://…"));
    form->addRow(tr("URL"), m_url);
    m_language = line(tr("fr, en…"));
    m_language->setMaximumWidth(theme.px(120));
    form->addRow(tr("Langue"), m_language);
    m_document = new QComboBox;
    m_document->setObjectName(QStringLiteral("ReferenceDocument"));
    m_document->setMinimumContentsLength(24);
    m_document->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    connect(m_document, &QComboBox::activated, this, &ReferencesPage::markDirty);
    form->addRow(tr("Document"), m_document);
    cl->addLayout(form);
    m_formStatus = makeLabel(QString(), "faint");
    m_formStatus->setObjectName(QStringLiteral("ReferenceStatus"));
    m_formStatus->setWordWrap(true);
    cl->addWidget(m_formStatus);
    body->addWidget(card);

    // Notes qui la citent
    auto* citing = makeCard();
    auto* ci = static_cast<QVBoxLayout*>(citing->layout());
    ci->addWidget(makeLabel(tr("Citée dans"), "h3"));
    m_citingList = new QVBoxLayout;
    m_citingList->setSpacing(2);
    ci->addLayout(m_citingList);
    m_citingEmpty = makeLabel(QString(), "muted");
    m_citingEmpty->setWordWrap(true);
    ci->addWidget(m_citingEmpty);
    body->addWidget(citing);

    auto* danger = new QHBoxLayout;
    danger->addStretch(1);
    m_remove = new QPushButton(tr("Supprimer la référence"));
    m_remove->setProperty("variant", QStringLiteral("ghost"));
    connect(m_remove, &QPushButton::clicked, this, &ReferencesPage::confirmRemove);
    danger->addWidget(m_remove);
    body->addLayout(danger);
    body->addStretch(1);

    m_center->addWidget(scaffold);
    m_panels->addWorkArea(m_center);
}

void ReferencesPage::refresh()
{
    m_stale = false;
    auto all = m_refs.list();
    m_all = all ? all.value() : QList<Reference>{};
    fillDocuments();
    fillList();
    if (m_current.id > 0) {
        auto fresh = m_refs.get(m_current.id);
        if (fresh) {
            if (!m_dirty) {
                m_current = fresh.value();
                fillForm();
            }
            return;
        }
        m_current = {};
    }
    m_center->setCurrentWidget(m_empty);
}

void ReferencesPage::fillList()
{
    const QSignalBlocker block(m_list);
    m_list->clear();
    const QString filter = foldForSort(m_filter->text().trimmed());
    QListWidgetItem* current = nullptr;
    for (const Reference& r : std::as_const(m_all)) {
        if (!filter.isEmpty()) {
            QStringList hay{r.citekey, r.title, r.containerTitle, QString::number(r.year)};
            for (const PersonName& n : r.authors)
                hay << n.sortName();
            if (!foldForSort(hay.join(u' ')).contains(filter))
                continue;
        }
        auto* item = new QListWidgetItem(formatCitation(r) + u'\n' + (r.title.isEmpty() ? tr("Sans titre") : r.title), m_list);
        item->setData(kIdRole, r.id);
        item->setToolTip(formatBibliography(r));
        if (r.id == m_current.id)
            current = item;
    }
    if (current)
        m_list->setCurrentItem(current);
    if (m_navStatus->property("report").toString().isEmpty())
        m_navStatus->setText(m_all.isEmpty() ? QString() : countLabel(m_all.size(), tr("référence"), tr("références")));
}

void ReferencesPage::fillDocuments()
{
    const QSignalBlocker block(m_document);
    m_document->clear();
    m_document->addItem(tr("Aucun document de la bibliothèque"), qint64(0));
    db::DocumentQuery q;
    q.sort = db::DocumentQuery::Sort::Title;
    auto docs = m_ctx.library.documents(q);
    if (docs) {
        for (const DocumentRecord& d : docs.value()) {
            const QString label = d.authors.isEmpty() ? d.title : d.title + QStringLiteral(" — ") + d.authors.first();
            m_document->addItem(label, d.id);
        }
    }
}

bool ReferencesPage::showReference(qint64 id)
{
    if (m_dirty)
        saveNow();
    auto r = m_refs.get(id);
    if (!r)
        return false;
    m_current = r.value();
    m_formStatus->clear();
    fillForm();
    fillList();
    m_center->setCurrentIndex(1);
    return true;
}

bool ReferencesPage::showCitekey(const QString& citekey)
{
    auto r = m_refs.byCitekey(citekey);
    return r && showReference(r.value().id);
}

void ReferencesPage::fillForm()
{
    m_loading = true;
    const Reference& r = m_current;
    m_key->setText(r.citekey);
    int t = m_type->findData(r.type);
    if (t < 0) {
        // Type CSL importé hors de la liste : proposé tel quel pour ne pas le perdre.
        m_type->addItem(referenceTypeLabel(r.type), r.type);
        t = m_type->count() - 1;
    }
    m_type->setCurrentIndex(t);
    m_title->setText(r.title);
    m_authors->setPlainText(namesText(r.authors));
    m_editors->setPlainText(namesText(r.editors));
    m_year->setText(r.year > 0 ? QString::number(r.year) : QString());
    m_container->setText(r.containerTitle);
    m_publisher->setText(r.publisher);
    m_place->setText(r.place);
    m_volume->setText(r.volume);
    m_issue->setText(r.issue);
    m_pages->setText(r.pages);
    m_edition->setText(r.edition);
    m_doi->setText(r.doi);
    m_isbn->setText(r.isbn);
    m_url->setText(r.url);
    m_language->setText(r.language);
    const int d = m_document->findData(r.documentId);
    m_document->setCurrentIndex(d >= 0 ? d : 0);
    m_loading = false;
    m_dirty = false;
    renderPreview();
    renderCiting();
}

Reference ReferencesPage::formValue() const
{
    Reference r = m_current;
    r.type = m_type->currentData().toString();
    r.title = m_title->text().trimmed();
    r.authors = namesFrom(m_authors->toPlainText());
    r.editors = namesFrom(m_editors->toPlainText());
    r.year = m_year->text().toInt();
    r.containerTitle = m_container->text().trimmed();
    r.publisher = m_publisher->text().trimmed();
    r.place = m_place->text().trimmed();
    r.volume = m_volume->text().trimmed();
    r.issue = m_issue->text().trimmed();
    r.pages = m_pages->text().trimmed();
    r.edition = m_edition->text().trimmed();
    r.doi = m_doi->text().trimmed();
    r.isbn = m_isbn->text().trimmed();
    r.url = m_url->text().trimmed();
    r.language = m_language->text().trimmed();
    r.documentId = m_document->currentData().toLongLong();
    return r;
}

void ReferencesPage::renderPreview()
{
    if (m_current.id <= 0)
        return;
    const Reference r = m_dirty ? formValue() : m_current;
    m_kind->setText(referenceTypeLabel(r.type).toUpper() + QStringLiteral(" · @") + m_current.citekey);
    m_citation->setText(formatCitation(r));
    m_entry->setText(formatBibliography(r, true));
    m_openDocument->setVisible(r.documentId > 0);
}

void ReferencesPage::renderCiting()
{
    clearRows(m_citingList);
    m_citingRows.clear();
    auto notes = m_refs.citingNotes(m_current.citekey);
    const auto list = notes ? notes.value() : QList<QPair<qint64, QString>>{};
    for (const auto& [id, title] : list) {
        auto* b = new QPushButton(title);
        b->setObjectName(QStringLiteral("NoteLinkRow"));
        b->setIcon(Icons::get(QStringLiteral("note"), m_ctx.theme.palette().text2, 16));
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, this, [this, id = id] { emit openNoteRequested(id); });
        m_citingList->addWidget(b);
        m_citingRows << b;
    }
    m_citingEmpty->setText(list.isEmpty() ? tr("Aucune note ne la cite. Écrivez [[@%1]] dans une note.").arg(m_current.citekey)
                                          : QString());
    m_citingEmpty->setVisible(list.isEmpty());
}

void ReferencesPage::markDirty()
{
    if (m_loading || m_current.id <= 0)
        return;
    m_dirty = true;
    m_formStatus->setText(tr("Modifications en cours…"));
    renderPreview();
    m_saveTimer.start();
}

bool ReferencesPage::saveNow()
{
    m_saveTimer.stop();
    if (!m_dirty || m_current.id <= 0)
        return true;
    Reference r = formValue();
    r.citekey = m_current.citekey;  // la clé s'enregistre à part (commitKey)
    auto saved = m_refs.update(r);
    if (!saved) {
        m_formStatus->setText(tr("Enregistrement impossible : %1").arg(saved.error().message));
        return false;
    }
    m_current = saved.value();
    m_dirty = false;
    m_formStatus->setText(tr("Enregistré."));
    for (Reference& x : m_all) {
        if (x.id == m_current.id)
            x = m_current;
    }
    // Liste mise à jour sans perdre la ligne courante.
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* item = m_list->item(i);
        if (item->data(kIdRole).toLongLong() == m_current.id) {
            item->setText(formatCitation(m_current) + u'\n' + (m_current.title.isEmpty() ? tr("Sans titre") : m_current.title));
            item->setToolTip(formatBibliography(m_current));
        }
    }
    renderPreview();
    return true;
}

void ReferencesPage::commitKey()
{
    if (m_loading || m_current.id <= 0)
        return;
    const QString key = m_key->text().trimmed();
    if (key == m_current.citekey)
        return;
    saveNow();
    Reference r = m_current;
    r.citekey = key;
    int rewritten = 0;
    auto saved = m_refs.update(r, &rewritten);
    if (!saved) {
        m_formStatus->setText(saved.error().message);
        m_loading = true;
        m_key->setText(m_current.citekey);
        m_loading = false;
        return;
    }
    m_current = saved.value();
    m_formStatus->setText(rewritten == 0 ? tr("Clé changée.")
                                         : tr("Clé changée ; %1 mise à jour.")
                                               .arg(countLabel(rewritten, tr("note"), tr("notes"))));
    refresh();
    renderPreview();
    renderCiting();
}

qint64 ReferencesPage::createReference()
{
    saveNow();
    Reference r;
    r.type = QStringLiteral("book");
    auto created = m_refs.create(r);
    if (!created) {
        m_navStatus->setText(created.error().message);
        return 0;
    }
    m_current = created.value();
    refresh();
    showReference(m_current.id);
    m_title->setFocus();
    return m_current.id;
}

qint64 ReferencesPage::showForDocument(qint64 documentId)
{
    saveNow();
    auto r = m_refs.ensureForDocument(documentId);
    if (!r) {
        m_navStatus->setText(r.error().message);
        return 0;
    }
    refresh();
    showReference(r.value().id);
    return r.value().id;
}

bool ReferencesPage::importFrom(const QString& path)
{
    saveNow();
    auto report = m_refs.importFile(path);
    if (!report) {
        m_navStatus->setProperty("report", QString());
        m_navStatus->setText(tr("Import impossible : %1").arg(report.error().message));
        return false;
    }
    const ReferenceImportReport& rep = report.value();
    QStringList parts{tr("%1 importée(s)").arg(rep.added)};
    if (rep.linked > 0)
        parts << tr("%1 reliée(s) à un document").arg(rep.linked);
    if (rep.duplicates > 0)
        parts << tr("%1 déjà présente(s)").arg(rep.duplicates);
    if (rep.renamed > 0)
        parts << tr("%1 clé(s) renommée(s)").arg(rep.renamed);
    if (!rep.warnings.isEmpty())
        parts << tr("%1 entrée(s) ignorée(s)").arg(rep.warnings.size());
    const QString text = parts.join(QStringLiteral(" · ")) + u'.';
    m_navStatus->setProperty("report", text);
    m_navStatus->setText(text);
    m_navStatus->setToolTip(rep.warnings.join(u'\n'));
    refresh();
    if (!rep.addedIds.isEmpty())
        showReference(rep.addedIds.first());
    return true;
}

bool ReferencesPage::exportTo(const QString& path)
{
    saveNow();
    auto n = m_refs.exportFile(path);
    m_navStatus->setProperty("report", QString());
    m_navStatus->setText(n ? tr("%1 exportée(s) dans %2.").arg(n.value()).arg(QFileInfo(path).fileName())
                           : tr("Export impossible : %1").arg(n.error().message));
    return n.ok();
}

void ReferencesPage::chooseImport()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Importer des références"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        tr("Références (*.bib *.bibtex *.json);;BibTeX (*.bib *.bibtex);;CSL-JSON (*.json)"));
    if (!path.isEmpty())
        importFrom(path);
}

void ReferencesPage::chooseExport()
{
    QString selected;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Exporter les références"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/references.bib"),
        tr("BibTeX (*.bib);;CSL-JSON (*.json)"), &selected);
    if (path.isEmpty())
        return;
    QString file = path;
    if (QFileInfo(file).suffix().isEmpty())
        file += selected.contains(QLatin1String("json")) ? QStringLiteral(".json") : QStringLiteral(".bib");
    exportTo(file);
}

void ReferencesPage::confirmRemove()
{
    if (m_current.id <= 0)
        return;
    auto citing = m_refs.citingNotes(m_current.citekey);
    const int n = citing ? int(citing.value().size()) : 0;
    QMessageBox box(QMessageBox::Question, tr("Supprimer la référence"),
                    tr("Supprimer « %1 » ?").arg(formatCitation(m_current)), QMessageBox::NoButton, this);
    if (n > 0)
        box.setInformativeText(tr("%1 la cite : la citation y restera, signalée comme introuvable.")
                                   .arg(countLabel(n, tr("note"), tr("notes"))));
    QPushButton* yes = box.addButton(tr("Supprimer"), QMessageBox::AcceptRole);
    box.addButton(tr("Annuler"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != yes)
        return;
    m_saveTimer.stop();
    m_dirty = false;
    if (auto r = m_refs.remove(m_current.id); !r) {
        m_formStatus->setText(r.error().message);
        return;
    }
    m_current = {};
    refresh();
}

}  // namespace studylab::ui
