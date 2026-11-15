#include "AccessibleReaderDialog.h"
#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeySequence>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtConcurrentRun>

namespace pdfeditor {
AccessibleReaderDialog::AccessibleReaderDialog(std::shared_ptr<const PdfDocument> document, qsizetype currentPage, QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("accessibleReaderDialog")); setWindowTitle(tr("Lectura accesible del texto"));
    auto* layout = new QVBoxLayout(this);
    auto* hint = new QLabel(tr("Texto de la capa del PDF en un control estándar para teclado y lectores de pantalla. El orden depende del documento; no sustituye su estructura semántica. Los escaneos requieren OCR."), this);
    hint->setWordWrap(true); layout->addWidget(hint);
    auto* navigation = new QHBoxLayout;
    auto* previous = new QPushButton(tr("&Anterior"), this); previous->setObjectName(QStringLiteral("readerPrevious")); previous->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Left));
    auto* page = new QSpinBox(this); page->setObjectName(QStringLiteral("readerPage")); page->setRange(1, static_cast<int>(document->pageCount())); page->setValue(static_cast<int>(currentPage + 1));
    page->setKeyboardTracking(false); page->setAccessibleName(tr("Página para lectura accesible"));
    auto* next = new QPushButton(tr("&Siguiente"), this); next->setObjectName(QStringLiteral("readerNext")); next->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Right));
    navigation->addWidget(previous); navigation->addWidget(page); navigation->addWidget(new QLabel(tr("de %1").arg(document->pageCount()), this)); navigation->addWidget(next); layout->addLayout(navigation);
    auto* status = new QLabel(this); status->setObjectName(QStringLiteral("readerStatus")); status->setWordWrap(true); layout->addWidget(status);
    auto* text = new QPlainTextEdit(this); text->setObjectName(QStringLiteral("accessiblePageText")); text->setReadOnly(true); text->setTabChangesFocus(true);
    text->setAccessibleName(tr("Texto de la página %1").arg(page->value())); text->setAccessibleDescription(tr("Solo lectura. Usa Control A para seleccionar todo y Control C para copiar. Alt izquierda y Alt derecha cambian de página."));
    layout->addWidget(text, 1);
    auto* larger = new QPushButton(tr("Aumentar texto"), this); larger->setObjectName(QStringLiteral("readerLarger")); larger->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Plus));
    auto* smaller = new QPushButton(tr("Reducir texto"), this); smaller->setObjectName(QStringLiteral("readerSmaller")); smaller->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus));
    auto* sizeControls = new QHBoxLayout; sizeControls->addWidget(smaller); sizeControls->addWidget(larger); layout->addLayout(sizeControls);
    connect(larger, &QPushButton::clicked, text, [text] { if (text->font().pointSizeF() < 36) { text->zoomIn(1); } });
    connect(smaller, &QPushButton::clicked, text, [text] { if (text->font().pointSizeF() > 8) { text->zoomOut(1); } });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this); buttons->button(QDialogButtonBox::Close)->setText(tr("Cerrar"));
    auto* copy = buttons->addButton(tr("Copiar toda la página"), QDialogButtonBox::ActionRole); copy->setObjectName(QStringLiteral("readerCopyAll")); copy->setEnabled(false); layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(copy, &QPushButton::clicked, this, [text] { QApplication::clipboard()->setText(text->toPlainText()); });
    connect(previous, &QPushButton::clicked, this, [page] { page->stepDown(); }); connect(next, &QPushButton::clicked, this, [page] { page->stepUp(); });
    auto* watcher = new QFutureWatcher<PdfTextSelectionResult>(this);
    auto activePage = std::make_shared<qsizetype>(-1);
    auto load = std::make_shared<std::function<void()>>();
    *load = [=] {
        text->clear(); copy->setEnabled(false); status->setText(tr("Preparando texto de la página %1…").arg(page->value()));
        text->setAccessibleName(tr("Texto de la página %1").arg(page->value())); previous->setEnabled(page->value() > 1); next->setEnabled(page->value() < page->maximum());
        if (watcher->isRunning()) { return; }
        *activePage = page->value() - 1;
        watcher->setFuture(QtConcurrent::run([document, index = *activePage] { return document->selectText(index, {0, 0}, {1, 1}); }));
    };
    connect(watcher, &QFutureWatcher<PdfTextSelectionResult>::finished, this, [=] {
        if (*activePage != page->value() - 1) { (*load)(); return; }
        const auto result = watcher->result(); if (!result.error.isEmpty()) { status->setText(result.error); return; }
        text->setPlainText(result.match.text);
        status->setText(result.match.text.trimmed().isEmpty() ? tr("Esta página no tiene texto accesible. Puedes aplicar OCR si es un escaneo.") : tr("Texto listo. Puedes seleccionarlo, copiarlo o recorrerlo con el teclado."));
        copy->setEnabled(!result.match.text.isEmpty() && document->canCopyText());
    });
    connect(page, &QSpinBox::valueChanged, this, [load] { (*load)(); });
    QWidget::setTabOrder(page, text); QWidget::setTabOrder(text, copy); QWidget::setTabOrder(copy, buttons->button(QDialogButtonBox::Close));
    (*load)(); text->setFocus(); resize(740, 600);
}
}
