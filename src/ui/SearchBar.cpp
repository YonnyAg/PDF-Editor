#include "SearchBar.h"

#include <QAction>
#include <QActionGroup>
#include <QFutureWatcher>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QToolButton>
#include <QtConcurrentRun>

#include <exception>
#include <utility>

namespace pdfeditor {

SearchBar::SearchBar(QWidget* parent)
    : QToolBar(tr("Búsqueda"), parent), query_(new QLineEdit(this)), status_(new QLabel(this))
{
    setObjectName(QStringLiteral("searchBar"));
    setMovable(false);
    setFloatable(false);
    query_->setObjectName(QStringLiteral("searchQuery"));
    query_->setAccessibleName(tr("Texto a buscar"));
    query_->setPlaceholderText(tr("Buscar texto en el PDF"));
    query_->setMaximumWidth(300);
    addWidget(query_);
    auto* find = addAction(tr("Buscar"));
    previous_ = addAction(tr("Coincidencia anterior"));
    previous_->setShortcut(QKeySequence(QStringLiteral("Shift+F3")));
    next_ = addAction(tr("Coincidencia siguiente"));
    next_->setShortcut(QKeySequence(Qt::Key_F3));
    auto* markupButton = new QToolButton(this);
    markupButton->setObjectName(QStringLiteral("textMarkupTool"));
    markupButton->setPopupMode(QToolButton::MenuButtonPopup);
    auto* menu = new QMenu(markupButton);
    markupActions_ = new QActionGroup(this);
    markupActions_->setExclusive(false);
    const auto addMarkup = [this, menu](const QString& text, const QString& name, PdfTextMarkup type) {
        auto* action = menu->addAction(text);
        action->setObjectName(name);
        action->setData(static_cast<int>(type));
        markupActions_->addAction(action);
        return action;
    };
    auto* highlight = addMarkup(tr("Resaltar coincidencia"), QStringLiteral("highlightMatchAction"), PdfTextMarkup::Highlight);
    addMarkup(tr("Subrayar coincidencia"), QStringLiteral("underlineMatchAction"), PdfTextMarkup::Underline);
    addMarkup(tr("Tachar coincidencia"), QStringLiteral("strikeoutMatchAction"), PdfTextMarkup::StrikeOut);
    addMarkup(tr("Subrayado ondulado"), QStringLiteral("squigglyMatchAction"), PdfTextMarkup::Squiggly);
    markupButton->setMenu(menu);
    markupButton->setDefaultAction(highlight);
    markupButton->setToolTip(tr("Marcar la coincidencia activa. Abre la flecha para elegir el tipo de marca."));
    addWidget(markupButton);
    connect(markupActions_, &QActionGroup::triggered, this, [this](QAction* action) {
        if (editingEnabled_ && selected_ >= 0 && selected_ < result_.matches.size()) {
            emit markupRequested(result_.matches[selected_], static_cast<PdfTextMarkup>(action->data().toInt()));
        }
    });
    status_->setObjectName(QStringLiteral("searchStatus"));
    addWidget(status_);
    auto* close = addAction(tr("Cerrar búsqueda"));

    connect(query_, &QLineEdit::textChanged, this, [this, find] {
        clearResults();
        find->setEnabled(!query_->text().trimmed().isEmpty());
    });
    find->setEnabled(false);
    connect(query_, &QLineEdit::returnPressed, this, &SearchBar::search);
    connect(find, &QAction::triggered, this, &SearchBar::search);
    connect(previous_, &QAction::triggered, this, [this] { step(-1); });
    connect(next_, &QAction::triggered, this, [this] { step(1); });
    connect(close, &QAction::triggered, this, &QWidget::hide);
    clearResults();
}

void SearchBar::setDocument(std::shared_ptr<const PdfDocument> document)
{
    if (document_ == document) { return; }
    document_ = std::move(document);
    clearResults();
    if (!document_) { hide(); }
}

void SearchBar::focusQuery()
{
    show();
    query_->setFocus(Qt::ShortcutFocusReason);
    query_->selectAll();
}

void SearchBar::setEditingEnabled(bool enabled)
{
    editingEnabled_ = enabled;
    markupActions_->setEnabled(enabled && selected_ >= 0 && selected_ < result_.matches.size());
}

void SearchBar::hideEvent(QHideEvent* event)
{
    clearResults();
    QToolBar::hideEvent(event);
}

void SearchBar::clearResults()
{
    if (cancellation_) {
        cancellation_->store(true);
        cancellation_.reset();
    }
    result_ = {};
    selected_ = -1;
    markupActions_->setEnabled(false);
    previous_->setEnabled(false);
    next_->setEnabled(false);
    status_->clear();
    emit matchCleared();
}

void SearchBar::search()
{
    const auto text = query_->text().trimmed();
    if (!document_ || text.isEmpty()) { return; }
    if (!result_.matches.isEmpty()) { step(1); return; }
    clearResults();
    status_->setText(tr("Buscando…"));
    cancellation_ = std::make_shared<std::atomic_bool>(false);
    auto* watcher = new QFutureWatcher<PdfSearchResult>(this);
    const auto cancelled = cancellation_;
    connect(watcher, &QFutureWatcher<PdfSearchResult>::finished, this, [this, watcher, cancelled] {
        watcher->deleteLater();
        if (cancelled->load()) { return; }
        cancellation_.reset();
        try {
            result_ = watcher->result();
        } catch (const std::exception&) {
            status_->setText(tr("No se pudo buscar en el PDF."));
            return;
        }
        if (result_.matches.isEmpty()) {
            status_->setText(result_.skippedPages == 0 ? tr("Sin coincidencias")
                : tr("Sin coincidencias · %n página(s) sin procesar", nullptr, static_cast<int>(result_.skippedPages)));
            return;
        }
        const bool canStep = result_.matches.size() > 1;
        previous_->setEnabled(canStep);
        next_->setEnabled(canStep);
        step(1);
    });
    watcher->setFuture(QtConcurrent::run([document = document_, text, cancelled] {
        return document->findText(text, [cancelled] { return cancelled->load(); });
    }));
}

void SearchBar::step(qsizetype offset)
{
    const auto count = result_.matches.size();
    if (count == 0) { return; }
    selected_ = (selected_ + offset + count) % count;
    markupActions_->setEnabled(editingEnabled_);
    const auto total = result_.limited ? tr("%1+").arg(count) : QString::number(count);
    auto status = tr("%1 de %2").arg(selected_ + 1).arg(total);
    if (result_.skippedPages > 0) {
        status += tr(" · %n página(s) sin procesar", nullptr, static_cast<int>(result_.skippedPages));
    }
    status_->setText(status);
    emit matchSelected(result_.matches[selected_]);
}

} // namespace pdfeditor
