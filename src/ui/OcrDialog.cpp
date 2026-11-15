#include "OcrDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryFile>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <atomic>

namespace pdfeditor {
OcrDialog::OcrDialog(std::shared_ptr<const PdfDocument> document, qsizetype currentPage, QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("ocrDialog"));
    setWindowTitle(tr("Reconocer texto localmente (OCR)"));
    auto* layout = new QVBoxLayout(this);
    auto* hint = new QLabel(tr("Reconoce hasta 50 páginas usando los idiomas OCR instalados en Windows. Se omiten páginas con texto o campos interactivos. Revisa el resultado: el reconocimiento puede contener errores. La capa nueva será invisible y buscable."), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto* form = new QFormLayout;
    auto* language = new QComboBox(this);
    language->setObjectName(QStringLiteral("ocrLanguage"));
    for (const auto& available : OcrEngine::languages()) { language->addItem(available.name, available.tag); }
    form->addRow(tr("Idioma"), language);
    auto* first = new QSpinBox(this);
    auto* last = new QSpinBox(this);
    first->setObjectName(QStringLiteral("ocrFirstPage"));
    last->setObjectName(QStringLiteral("ocrLastPage"));
    first->setRange(1, static_cast<int>(document->pageCount()));
    last->setRange(1, static_cast<int>(document->pageCount()));
    first->setValue(static_cast<int>(currentPage + 1));
    last->setValue(static_cast<int>(currentPage + 1));
    form->addRow(tr("Desde la página"), first);
    form->addRow(tr("Hasta la página"), last);
    auto* recognize = new QPushButton(tr("Reconocer"), this);
    recognize->setObjectName(QStringLiteral("ocrRecognize"));
    form->addRow(recognize);
    layout->addLayout(form);
    auto* status = new QLabel(this);
    status->setObjectName(QStringLiteral("ocrStatus"));
    status->setWordWrap(true);
    layout->addWidget(status);
    auto* text = new QPlainTextEdit(this);
    text->setObjectName(QStringLiteral("ocrText"));
    text->setReadOnly(true);
    text->setAccessibleName(tr("Texto reconocido para revisar"));
    layout->addWidget(text, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    auto* insert = buttons->button(QDialogButtonBox::Apply);
    insert->setObjectName(QStringLiteral("ocrApply"));
    insert->setText(tr("Añadir texto buscable"));
    insert->setEnabled(false);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    auto* exportText = buttons->addButton(tr("Exportar texto…"), QDialogButtonBox::ActionRole);
    exportText->setObjectName(QStringLiteral("ocrExportText"));
    exportText->setEnabled(false);
    layout->addWidget(buttons);
    auto* watcher = new QFutureWatcher<OcrBatchResult>(this);
    const auto cancelled = std::make_shared<std::atomic_bool>(false);
    auto update = [first, last, language, recognize, watcher] {
        recognize->setEnabled(!watcher->isRunning() && language->count() > 0 && first->value() <= last->value() && last->value() - first->value() < 50);
    };
    connect(first, &QSpinBox::valueChanged, this, update);
    connect(last, &QSpinBox::valueChanged, this, update);
    connect(recognize, &QPushButton::clicked, this, [this, document, first, last, language, recognize, status, text, insert, exportText, watcher, cancelled] {
        pages_.clear(); text->clear();
        insert->setEnabled(false); exportText->setEnabled(false);
        recognize->setEnabled(false); first->setEnabled(false); last->setEnabled(false); language->setEnabled(false);
        status->setText(tr("Reconociendo en este equipo… Cancelar descarta el resultado cuando termina la página actual."));
        cancelled->store(false);
        watcher->setFuture(QtConcurrent::run([document, firstPage = first->value() - 1, lastPage = last->value() - 1, tag = language->currentData().toString(), cancelled] {
            return OcrEngine::recognizePdf(document, firstPage, lastPage, tag, [cancelled] { return cancelled->load(); });
        }));
    });
    connect(watcher, &QFutureWatcher<OcrBatchResult>::finished, this, [this, first, last, language, status, text, insert, exportText, watcher, update] {
        first->setEnabled(true); last->setEnabled(true); language->setEnabled(true);
        const auto result = watcher->result();
        if (!result.error.isEmpty() || result.cancelled) { pages_.clear(); status->setText(result.cancelled ? tr("Reconocimiento cancelado") : result.error); update(); return; }
        pages_ = result.pages;
        text->setPlainText(result.text);
        insert->setEnabled(!pages_.isEmpty());
        exportText->setEnabled(!result.text.isEmpty());
        status->setText(tr("%1 páginas reconocidas; %2 omitidas. Revisa el texto antes de añadirlo.").arg(pages_.size()).arg(result.skippedPages));
        update();
    });
    connect(exportText, &QPushButton::clicked, this, [this, document, text, status] {
        const auto destination = QFileDialog::getSaveFileName(this, tr("Exportar texto OCR a un archivo nuevo"),
            QFileInfo(document->filePath()).dir().filePath(QFileInfo(document->filePath()).completeBaseName() + QStringLiteral(" - OCR.txt")), tr("Texto UTF-8 (*.txt)"));
        if (destination.isEmpty()) { return; }
        const QFileInfo target(destination);
        if (target.exists() || !target.dir().exists()) { status->setText(tr("Elige un archivo de destino nuevo.")); return; }
        QTemporaryFile output(target.dir().filePath(QStringLiteral(".pdf-editor-ocr-XXXXXX")));
        const auto bytes = text->toPlainText().toUtf8();
        if (!output.open() || output.write(bytes) != bytes.size() || !output.flush() || !output.rename(target.absoluteFilePath())) {
            status->setText(tr("No se pudo guardar el texto OCR.")); return;
        }
        output.setAutoRemove(false);
        status->setText(tr("Texto OCR exportado. El PDF todavía no se ha modificado."));
    });
    connect(insert, &QPushButton::clicked, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, [this, cancelled] { cancelled->store(true); reject(); });
    connect(this, &QDialog::rejected, this, [cancelled] { cancelled->store(true); });
    if (language->count() == 0) { status->setText(tr("No hay idiomas OCR disponibles. Instala un idioma de reconocimiento en la configuración de Windows.")); }
    update();
    resize(650, 620);
}
} // namespace pdfeditor
