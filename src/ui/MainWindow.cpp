#include "MainWindow.h"
#include "CropDialog.h"
#include "InkDialog.h"
#include "OcrDialog.h"
#include "RedactionDialog.h"
#include "ContentDialog.h"
#include "AccessibleReaderDialog.h"
#include "DocumentPrint.h"
#include "PageView.h"
#include "SearchBar.h"
#include "ThumbnailView.h"

#include <QAction>
#include <QCloseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QColorDialog>
#include <QFontComboBox>
#include <QDesktopServices>
#include <QUrl>
#include <QClipboard>
#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QImageReader>
#include <QFont>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QSet>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QShortcut>
#include <QSpinBox>
#include <QStatusBar>
#include <QStackedWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrentRun>

#include <utility>
#include <algorithm>
#include <optional>
#include <limits>

namespace pdfeditor {

namespace {
struct TextStyleControls
{
    QFontComboBox* family;
    QDoubleSpinBox* size;
    QCheckBox* bold;
    QCheckBox* italic;
    QCheckBox* underline;
    QComboBox* alignment;
    QPushButton* color;

    PdfTextStyle value() const
    {
        return {family->currentFont().family(), size->value(), color->property("textColor").value<QColor>(),
            bold->isChecked(), italic->isChecked(), underline->isChecked(),
            Qt::Alignment(alignment->currentData().toInt())};
    }
};

TextStyleControls addTextStyleControls(QFormLayout* form, QWidget* parent, const QString& prefix, QDoubleSpinBox* size = nullptr)
{
    auto* family = new QFontComboBox(parent);
    family->setObjectName(prefix + QStringLiteral("FontFamily"));
    family->setCurrentFont(QFont(QStringLiteral("Arial")));
    form->addRow(QObject::tr("&Fuente"), family);
    if (!size) {
        size = new QDoubleSpinBox(parent);
        size->setObjectName(prefix + QStringLiteral("PointSize"));
        size->setRange(4, 144);
        size->setValue(12);
        size->setSuffix(QObject::tr(" pt"));
        form->addRow(QObject::tr("Tamaño de letra"), size);
    }
    auto* formats = new QWidget(parent);
    auto* row = new QHBoxLayout(formats);
    row->setContentsMargins(0, 0, 0, 0);
    auto* bold = new QCheckBox(QObject::tr("Negrita"), formats);
    auto* italic = new QCheckBox(QObject::tr("Cursiva"), formats);
    auto* underline = new QCheckBox(QObject::tr("Subrayado"), formats);
    bold->setObjectName(prefix + QStringLiteral("Bold"));
    italic->setObjectName(prefix + QStringLiteral("Italic"));
    underline->setObjectName(prefix + QStringLiteral("Underline"));
    row->addWidget(bold); row->addWidget(italic); row->addWidget(underline);
    form->addRow(QObject::tr("Estilo"), formats);
    auto* alignment = new QComboBox(parent);
    alignment->setObjectName(prefix + QStringLiteral("Alignment"));
    alignment->addItem(QObject::tr("Izquierda"), int(Qt::AlignLeft));
    alignment->addItem(QObject::tr("Centrado"), int(Qt::AlignHCenter));
    alignment->addItem(QObject::tr("Derecha"), int(Qt::AlignRight));
    form->addRow(QObject::tr("Alineación"), alignment);
    auto* color = new QPushButton(parent);
    color->setObjectName(prefix + QStringLiteral("Color"));
    const auto setColor = [color](const QColor& value) {
        color->setProperty("textColor", value);
        color->setText(QObject::tr("Color: %1…").arg(value.name()));
        color->setStyleSheet(QStringLiteral("QPushButton { border-left: 14px solid %1; padding: 4px; }").arg(value.name()));
    };
    setColor(Qt::black);
    QObject::connect(color, &QPushButton::clicked, parent, [parent, color, setColor] {
        const auto value = QColorDialog::getColor(color->property("textColor").value<QColor>(), parent, QObject::tr("Color del texto"));
        if (value.isValid()) { setColor(value); }
    });
    form->addRow(QObject::tr("Color"), color);
    return {family, size, bold, italic, underline, alignment, color};
}

struct AreaControls
{
    QDoubleSpinBox* fields[4]{};
    QRectF value() const { return {fields[0]->value() / 100, fields[1]->value() / 100, fields[2]->value() / 100, fields[3]->value() / 100}; }
};

AreaControls addAreaControls(QFormLayout* form, QWidget* parent, const QString& prefix, const QRectF& initial)
{
    AreaControls result;
    const QString names[] = {QStringLiteral("Left"), QStringLiteral("Top"), QStringLiteral("Width"), QStringLiteral("Height")};
    const QString labels[] = {QObject::tr("Desde la izquierda"), QObject::tr("Desde arriba"), QObject::tr("Ancho"), QObject::tr("Alto")};
    const qreal values[] = {initial.x(), initial.y(), initial.width(), initial.height()};
    for (int i = 0; i < 4; ++i) {
        auto* spin = new QDoubleSpinBox(parent);
        spin->setObjectName(prefix + names[i]);
        spin->setRange(i < 2 ? 0 : 0.1, 100);
        spin->setDecimals(1);
        spin->setSuffix(QStringLiteral(" %"));
        spin->setValue(values[i] * 100);
        form->addRow(labels[i], spin);
        result.fields[i] = spin;
    }
    return result;
}

QString selectCopyPath(QWidget* parent, const QString& title, const QFileInfo& source, const QString& name,
    const QString& suffix = QStringLiteral("pdf"))
{
    const auto filter = suffix == QStringLiteral("png") ? MainWindow::tr("Imágenes PNG (*.png)")
        : MainWindow::tr("Documentos PDF (*.pdf)");
    QFileDialog dialog(parent, title, source.absolutePath(), filter);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDefaultSuffix(suffix);
    dialog.setOption(QFileDialog::DontConfirmOverwrite);
    dialog.selectFile(name);
    return dialog.exec() == QDialog::Accepted ? dialog.selectedFiles().value(0) : QString{};
}

enum class NoteAction { Cancel, Save, Delete };

struct NoteDialogResult
{
    NoteAction action = NoteAction::Cancel;
    QString text;
};

NoteDialogResult noteTextDialog(QWidget* parent, const QString& title, const QString& text = {}, bool editable = true,
    bool allowDelete = false)
{
    NoteDialogResult result;
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.resize(420, 230);
    auto* layout = new QVBoxLayout(&dialog);
    auto* editor = new QPlainTextEdit(text, &dialog);
    editor->setObjectName(QStringLiteral("noteText"));
    editor->setAccessibleName(MainWindow::tr("Texto de la nota"));
    editor->setReadOnly(!editable);
    editor->setTabChangesFocus(true);
    layout->addWidget(editor);
    const auto options = editable ? QDialogButtonBox::Save | QDialogButtonBox::Cancel
                                  : QDialogButtonBox::StandardButtons(QDialogButtonBox::Close);
    auto* buttons = new QDialogButtonBox(options, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (auto* save = buttons->button(QDialogButtonBox::Save)) {
        const auto updateSave = [save, editor] { save->setEnabled(!editor->toPlainText().trimmed().isEmpty()); };
        QObject::connect(editor, &QPlainTextEdit::textChanged, save, updateSave);
        updateSave();
    }
    if (editable && allowDelete) {
        auto* remove = buttons->addButton(MainWindow::tr("Eliminar nota"), QDialogButtonBox::DestructiveRole);
        remove->setObjectName(QStringLiteral("deleteNoteButton"));
        QObject::connect(remove, &QPushButton::clicked, &dialog, [&] {
            result.action = NoteAction::Delete;
            dialog.accept();
        });
    }
    editor->setFocus();
    if (dialog.exec() != QDialog::Accepted) { return {}; }
    if (result.action != NoteAction::Delete) {
        result.action = NoteAction::Save;
        result.text = editor->toPlainText();
    }
    return result;
}

std::optional<QVariant> formValueDialog(QWidget* parent, const PdfFormField& field)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(field.editable ? MainWindow::tr("Rellenar campo") : MainWindow::tr("Campo de solo lectura"));
    dialog.resize(420, 200);
    auto* layout = new QFormLayout(&dialog);
    auto* caption = new QLabel(field.name, &dialog);
    caption->setTextFormat(Qt::PlainText);
    caption->setWordWrap(true);
    layout->addRow(caption);
    QCheckBox* checkbox = nullptr;
    QLineEdit* singleLine = nullptr;
    QPlainTextEdit* multiline = nullptr;
    QComboBox* choice = nullptr;
    QListWidget* multiple = nullptr;
    if (field.type == PdfFormFieldType::CheckBox) {
        checkbox = new QCheckBox(MainWindow::tr("Marcada"), &dialog);
        checkbox->setObjectName(QStringLiteral("formCheckbox"));
        checkbox->setAccessibleName(field.name);
        checkbox->setChecked(field.value.toBool());
        checkbox->setEnabled(field.editable);
        layout->addRow(checkbox);
    } else if (field.type == PdfFormFieldType::MultiSelectList) {
        multiple = new QListWidget(&dialog); multiple->setObjectName(QStringLiteral("formMultiChoice")); multiple->setAccessibleName(field.name);
        multiple->setSelectionMode(QAbstractItemView::MultiSelection);
        for (qsizetype i = 0; i < field.options.size(); ++i) {
            auto* item = new QListWidgetItem(field.options[i], multiple); item->setData(Qt::UserRole, field.exportValues[i]); item->setSelected(field.value.toStringList().contains(field.exportValues[i]));
        }
        multiple->setEnabled(field.editable); layout->addRow(multiple);
        layout->addRow(new QLabel(MainWindow::tr("Usa Espacio para marcar opciones con el teclado."), &dialog));
    } else if (field.type == PdfFormFieldType::ComboBox || field.type == PdfFormFieldType::RadioGroup) {
        choice = new QComboBox(&dialog);
        choice->setObjectName(QStringLiteral("formChoice"));
        choice->setAccessibleName(field.name);
        for (qsizetype i = 0; i < field.options.size(); ++i) { choice->addItem(field.options[i], field.exportValues[i]); }
        choice->setCurrentIndex(choice->findData(field.value));
        choice->setEnabled(field.editable);
        layout->addRow(choice);
    } else if (field.multiline) {
        multiline = new QPlainTextEdit(field.value.toString(), &dialog);
        multiline->setObjectName(QStringLiteral("formText"));
        multiline->setAccessibleName(field.name);
        multiline->setReadOnly(!field.editable);
        multiline->setTabChangesFocus(true);
        layout->addRow(multiline);
    } else {
        singleLine = new QLineEdit(field.value.toString(), &dialog);
        singleLine->setObjectName(QStringLiteral("formText"));
        singleLine->setAccessibleName(field.name);
        singleLine->setReadOnly(!field.editable);
        singleLine->setMaxLength(std::numeric_limits<int>::max());
        layout->addRow(singleLine);
    }
    if (field.type == PdfFormFieldType::Text && field.maxLength > 0) {
        layout->addRow(new QLabel(MainWindow::tr("Máximo %1 caracteres.").arg(field.maxLength), &dialog));
    }
    if (field.type == PdfFormFieldType::Text && field.editable) {
        auto* appearance = new QLabel(MainWindow::tr("El texto se dibujará con una fuente del sistema."), &dialog);
        appearance->setWordWrap(true);
        layout->addRow(appearance);
    }
    const auto options = field.editable ? QDialogButtonBox::Save | QDialogButtonBox::Cancel
        : QDialogButtonBox::StandardButtons(QDialogButtonBox::Close);
    auto* buttons = new QDialogButtonBox(options, &dialog);
    layout->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (choice && field.editable) {
        auto* save = buttons->button(QDialogButtonBox::Save);
        QObject::connect(choice, &QComboBox::currentIndexChanged, &dialog, [save](int index) { save->setEnabled(index >= 0); });
        save->setEnabled(choice->currentIndex() >= 0);
    }
    if (field.type == PdfFormFieldType::Text && field.maxLength > 0 && field.editable) {
        auto* save = buttons->button(QDialogButtonBox::Save);
        const auto updateSave = [save, singleLine, multiline, &field] {
            const auto text = singleLine ? singleLine->text() : multiline->toPlainText();
            save->setEnabled(text.toUcs4().size() <= field.maxLength);
        };
        if (singleLine) { QObject::connect(singleLine, &QLineEdit::textChanged, &dialog, updateSave); }
        else { QObject::connect(multiline, &QPlainTextEdit::textChanged, &dialog, updateSave); }
        updateSave();
    }
    if (dialog.exec() != QDialog::Accepted) { return std::nullopt; }
    if (checkbox) { return QVariant(checkbox->isChecked()); }
    if (choice) { return choice->currentData(); }
    if (multiple) { QStringList values; for (const auto* item : multiple->selectedItems()) { values.append(item->data(Qt::UserRole).toString()); } return QVariant(values); }
    return QVariant(singleLine ? singleLine->text() : multiline->toPlainText());
}
} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("PDF Editor"));
    resize(1100, 720);
    setMinimumSize(640, 480);

    auto* fileMenu = menuBar()->addMenu(tr("&Archivo"));
    openAction_ = fileMenu->addAction(tr("&Abrir PDF…"));
    openAction_->setObjectName(QStringLiteral("openAction"));
    openAction_->setShortcut(QKeySequence::Open);
    connect(openAction_, &QAction::triggered, this, [this] {
        const auto filePath = QFileDialog::getOpenFileName(
            this, tr("Abrir documento"), {}, tr("Documentos PDF (*.pdf);;Todos los archivos (*)"));
        if (workspaceOpen_) { workspaceOpen_(filePath); }
        else { openDocument(filePath); }
    });
    saveAction_ = fileMenu->addAction(tr("&Guardar"));
    saveAction_->setObjectName(QStringLiteral("saveAction"));
    saveAction_->setShortcut(QKeySequence::Save);
    connect(saveAction_, &QAction::triggered, this, [this] { saveDocument(PdfSaveMode::Original); });
    saveAsAction_ = fileMenu->addAction(tr("Guardar &como…"));
    saveAsAction_->setObjectName(QStringLiteral("saveAsAction"));
    saveAsAction_->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction_, &QAction::triggered, this, &MainWindow::saveAs);
    fileMenu->addSeparator();
    exportAction_ = fileMenu->addAction(tr("&Exportar página como PNG…"));
    exportAction_->setObjectName(QStringLiteral("exportPageAction"));
    connect(exportAction_, &QAction::triggered, this, &MainWindow::exportPage);
    printAction_ = fileMenu->addAction(tr("&Imprimir…"));
    printAction_->setObjectName(QStringLiteral("printAction"));
    printAction_->setShortcut(QKeySequence::Print);
    connect(printAction_, &QAction::triggered, this, &MainWindow::printPdf);
    propertiesAction_ = fileMenu->addAction(tr("Propiedades del documento…"));
    propertiesAction_->setObjectName(QStringLiteral("documentPropertiesAction"));
    connect(propertiesAction_, &QAction::triggered, this, &MainWindow::editProperties);
    attachmentsAction_ = fileMenu->addAction(tr("Adjuntos del documento…"));
    attachmentsAction_->setObjectName(QStringLiteral("attachmentsAction"));
    connect(attachmentsAction_, &QAction::triggered, this, &MainWindow::manageAttachments);
    optimizeAction_ = fileMenu->addAction(tr("Guardar copia optimizada…"));
    optimizeAction_->setObjectName(QStringLiteral("optimizePdfAction"));
    optimizeAction_->setStatusTip(tr("Conserva la calidad de las imágenes. El tamaño final puede aumentar."));
    connect(optimizeAction_, &QAction::triggered, this, &MainWindow::exportOptimizedCopy);
    protectAction_ = fileMenu->addAction(tr("Guardar copia con contraseña…"));
    protectAction_->setObjectName(QStringLiteral("protectPdfAction"));
    connect(protectAction_, &QAction::triggered, this, &MainWindow::exportProtectedCopy);
    ocrAction_ = fileMenu->addAction(tr("Reconocer texto (OCR)…"));
    ocrAction_->setObjectName(QStringLiteral("ocrAction"));
    connect(ocrAction_, &QAction::triggered, this, &MainWindow::recognizeText);
    redactAction_ = fileMenu->addAction(tr("Eliminar contenido en una copia…"));
    redactAction_->setObjectName(QStringLiteral("redactCopyAction"));
    connect(redactAction_, &QAction::triggered, this, &MainWindow::exportRedactedCopy);
    localizedRedactAction_ = fileMenu->addAction(tr("Eliminar objetos en zonas (conservar el resto)…"));
    localizedRedactAction_->setObjectName(QStringLiteral("localizedRedactAction"));
    connect(localizedRedactAction_, &QAction::triggered, this, [this] { exportRedactedCopy(true); });
    accessibleReaderAction_ = fileMenu->addAction(tr("Lectura accesible del texto…")); accessibleReaderAction_->setObjectName(QStringLiteral("accessibleReaderAction"));
    accessibleReaderAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_L));
    connect(accessibleReaderAction_, &QAction::triggered, this, [this] {
        if (!document_ || isBusy() || !document_->canCopyText()) { return; }
        AccessibleReaderDialog dialog(document_, pageView_->currentPage(), this); dialog.exec();
    });
    signAction_ = fileMenu->addAction(tr("Firmar copia con certificado…"));
    signAction_->setObjectName(QStringLiteral("signCopyAction"));
    connect(signAction_, &QAction::triggered, this, &MainWindow::signDocumentCopy);
    verifySignaturesAction_ = fileMenu->addAction(tr("Verificar firmas digitales…"));
    verifySignaturesAction_->setObjectName(QStringLiteral("verifySignaturesAction"));
    connect(verifySignaturesAction_, &QAction::triggered, this, &MainWindow::showSignatures);
    contentEditAction_ = fileMenu->addAction(tr("Editar contenido de esta página…"));
    contentEditAction_->setObjectName(QStringLiteral("editContentAction"));
    connect(contentEditAction_, &QAction::triggered, this, &MainWindow::editPageContent);
    fileMenu->addSeparator();
    closeAction_ = fileMenu->addAction(tr("&Cerrar documento"));
    closeAction_->setObjectName(QStringLiteral("closeDocumentAction"));
    closeAction_->setShortcut(QKeySequence::Close);
    connect(closeAction_, &QAction::triggered, this, &MainWindow::closeDocument);
    fileMenu->addSeparator();
    auto* quitAction = fileMenu->addAction(tr("&Salir"));
    quitAction->setObjectName(QStringLiteral("quitAction"));
    quitAction->setShortcut(QKeySequence::Quit);
    quitAction->setStatusTip(tr("Cerrar la aplicación"));
    connect(quitAction, &QAction::triggered, this, [this] { window()->close(); });

    auto* editMenu = menuBar()->addMenu(tr("E&ditar"));
    undoAction_ = editMenu->addAction(tr("&Deshacer"));
    undoAction_->setObjectName(QStringLiteral("undoAction"));
    undoAction_->setShortcuts(QKeySequence::Undo);
    connect(undoAction_, &QAction::triggered, this, [this] { restoreHistory(undoHistory_, redoHistory_); });
    redoAction_ = editMenu->addAction(tr("&Rehacer"));
    redoAction_->setObjectName(QStringLiteral("redoAction"));
    redoAction_->setShortcuts(QKeySequence::Redo);
    connect(redoAction_, &QAction::triggered, this, [this] { restoreHistory(redoHistory_, undoHistory_); });
    editMenu->addSeparator();
    searchAction_ = editMenu->addAction(tr("&Buscar texto…"));
    searchAction_->setObjectName(QStringLiteral("searchAction"));
    searchAction_->setShortcuts(QKeySequence::Find);
    selectTextAction_ = editMenu->addAction(tr("Seleccionar texto"));
    selectTextAction_->setObjectName(QStringLiteral("selectTextAction"));
    selectTextAction_->setCheckable(true);
    selectTextAction_->setChecked(true);
    copyTextAction_ = editMenu->addAction(tr("Copiar texto seleccionado"));
    copyTextAction_->setObjectName(QStringLiteral("copyTextAction"));
    copyTextAction_->setIconText(tr("Copiar"));
    copyTextAction_->setShortcut(QKeySequence::Copy);
    copyTextAction_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(copyTextAction_, &QAction::triggered, this, [this] {
        if (!document_ || operation_ != Operation::Idle || !document_->canCopyText()) { return; }
        const auto text = pageView_->textSelection().text;
        if (!text.isEmpty()) {
            QApplication::clipboard()->setText(text);
            statusBar()->showMessage(tr("Texto copiado"), 3000);
        }
    });

    auto* pagesMenu = menuBar()->addMenu(tr("&Páginas"));
    rotateAction_ = pagesMenu->addAction(tr("Girar 90° a la &derecha"));
    rotateAction_->setObjectName(QStringLiteral("rotatePageAction"));
    connect(rotateAction_, &QAction::triggered, this, &MainWindow::rotateCurrentPage);
    pagesMenu->addSeparator();
    movePageUp_ = pagesMenu->addAction(tr("Mover página &arriba"));
    movePageUp_->setObjectName(QStringLiteral("movePageUpAction"));
    connect(movePageUp_, &QAction::triggered, this, [this] { moveCurrentPage(-1); });
    movePageDown_ = pagesMenu->addAction(tr("Mover página a&bajo"));
    movePageDown_->setObjectName(QStringLiteral("movePageDownAction"));
    connect(movePageDown_, &QAction::triggered, this, [this] { moveCurrentPage(1); });
    pagesMenu->addSeparator();
    duplicatePageAction_ = pagesMenu->addAction(tr("&Duplicar página"));
    duplicatePageAction_->setObjectName(QStringLiteral("duplicatePageAction"));
    connect(duplicatePageAction_, &QAction::triggered, this, [this] { changeCurrentPage(PdfPageOperation::Duplicate); });
    insertBlankPageAction_ = pagesMenu->addAction(tr("Insertar página en &blanco después"));
    insertBlankPageAction_->setObjectName(QStringLiteral("insertBlankPageAction"));
    connect(insertBlankPageAction_, &QAction::triggered, this, [this] { changeCurrentPage(PdfPageOperation::InsertBlankAfter); });
    deletePageAction_ = pagesMenu->addAction(tr("E&liminar página…"));
    deletePageAction_->setObjectName(QStringLiteral("deletePageAction"));
    connect(deletePageAction_, &QAction::triggered, this, [this] { changeCurrentPage(PdfPageOperation::Delete); });
    pagesMenu->addSeparator();
    insertPdfAction_ = pagesMenu->addAction(tr("Insertar páginas de &otro PDF…"));
    insertPdfAction_->setObjectName(QStringLiteral("insertPdfAction"));
    connect(insertPdfAction_, &QAction::triggered, this, &MainWindow::insertFromPdf);
    mergePdfsAction_ = pagesMenu->addAction(tr("&Unir varios PDF al final…"));
    mergePdfsAction_->setObjectName(QStringLiteral("mergePdfsAction"));
    connect(mergePdfsAction_, &QAction::triggered, this, &MainWindow::mergePdfs);
    pagesMenu->addSeparator();
    extractPagesAction_ = pagesMenu->addAction(tr("&Extraer páginas…"));
    extractPagesAction_->setObjectName(QStringLiteral("extractPagesAction"));
    connect(extractPagesAction_, &QAction::triggered, this, &MainWindow::extractPages);
    splitAction_ = pagesMenu->addAction(tr("Dividir PDF en varios archivos…"));
    splitAction_->setObjectName(QStringLiteral("splitPdfAction"));
    connect(splitAction_, &QAction::triggered, this, &MainWindow::splitDocument);
    pagesMenu->addSeparator();
    cropPagesAction_ = pagesMenu->addAction(tr("Recortar páginas…"));
    cropPagesAction_->setObjectName(QStringLiteral("cropPagesAction"));
    connect(cropPagesAction_, &QAction::triggered, this, [this] { cropPages(false); });
    resetCropAction_ = pagesMenu->addAction(tr("Restaurar área completa…"));
    resetCropAction_->setObjectName(QStringLiteral("resetCropAction"));
    connect(resetCropAction_, &QAction::triggered, this, [this] { cropPages(true); });

    auto* insertMenu = menuBar()->addMenu(tr("&Insertar"));
    insertTextAction_ = insertMenu->addAction(tr("Insertar texto nuevo…"));
    insertTextAction_->setObjectName(QStringLiteral("insertTextAction"));
    connect(insertTextAction_, &QAction::triggered, this, [this] { insertContent(false); });
    insertImageAction_ = insertMenu->addAction(tr("Insertar imagen…"));
    insertImageAction_->setObjectName(QStringLiteral("insertImageAction"));
    connect(insertImageAction_, &QAction::triggered, this, [this] { insertContent(true); });
    insertMenu->addSeparator();
    numberPagesAction_ = insertMenu->addAction(tr("Numerar páginas…"));
    numberPagesAction_->setObjectName(QStringLiteral("numberPagesAction"));
    connect(numberPagesAction_, &QAction::triggered, this, [this] { decoratePages(true); });
    watermarkAction_ = insertMenu->addAction(tr("Marca de agua o sello de texto…"));
    watermarkAction_->setObjectName(QStringLiteral("watermarkAction"));
    connect(watermarkAction_, &QAction::triggered, this, [this] { decoratePages(false); });
    insertMenu->addSeparator();
    addLinkAction_ = insertMenu->addAction(tr("Añadir enlace…"));
    addLinkAction_->setObjectName(QStringLiteral("addLinkAction"));
    connect(addLinkAction_, &QAction::triggered, this, &MainWindow::addPageLink);
    pageLinksAction_ = insertMenu->addAction(tr("Enlaces de esta página…"));
    pageLinksAction_->setObjectName(QStringLiteral("pageLinksAction"));
    connect(pageLinksAction_, &QAction::triggered, this, &MainWindow::showPageLinks);
    auto* annotateMenu = menuBar()->addMenu(tr("A&notar"));
    addNoteAction_ = annotateMenu->addAction(tr("&Añadir nota…"));
    addNoteAction_->setObjectName(QStringLiteral("addNoteAction"));
    addNoteAction_->setCheckable(true);
    pageNotesAction_ = annotateMenu->addAction(tr("Notas de esta &página…"));
    pageNotesAction_->setObjectName(QStringLiteral("pageNotesAction"));
    connect(pageNotesAction_, &QAction::triggered, this, &MainWindow::showPageNotes);
    pageTextMarksAction_ = annotateMenu->addAction(tr("&Marcas de texto de esta página…"));
    pageTextMarksAction_->setObjectName(QStringLiteral("pageTextMarksAction"));
    connect(pageTextMarksAction_, &QAction::triggered, this, &MainWindow::showPageTextMarks);
    addShapeAction_ = annotateMenu->addAction(tr("Añadir forma…"));
    addShapeAction_->setObjectName(QStringLiteral("addShapeAction"));
    connect(addShapeAction_, &QAction::triggered, this, &MainWindow::addPageShape);
    drawInkAction_ = annotateMenu->addAction(tr("Dibujar a mano alzada…"));
    drawInkAction_->setObjectName(QStringLiteral("drawInkAction"));
    connect(drawInkAction_, &QAction::triggered, this, &MainWindow::drawInk);
    pageShapesAction_ = annotateMenu->addAction(tr("Formas de esta página…"));
    pageShapesAction_->setObjectName(QStringLiteral("pageShapesAction"));
    connect(pageShapesAction_, &QAction::triggered, this, &MainWindow::showPageShapes);
    auto* bookmarkMenu = menuBar()->addMenu(tr("&Marcadores"));
    addBookmarkAction_ = bookmarkMenu->addAction(tr("Añadir marcador de la página actual…"));
    addBookmarkAction_->setObjectName(QStringLiteral("addBookmarkAction"));
    addBookmarkAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
    addChildBookmarkAction_ = bookmarkMenu->addAction(tr("Añadir submarcador de la página actual…"));
    addChildBookmarkAction_->setObjectName(QStringLiteral("addChildBookmarkAction"));
    renameBookmarkAction_ = bookmarkMenu->addAction(tr("Cambiar nombre…"));
    renameBookmarkAction_->setObjectName(QStringLiteral("renameBookmarkAction"));
    retargetBookmarkAction_ = bookmarkMenu->addAction(tr("Cambiar página de destino…"));
    retargetBookmarkAction_->setObjectName(QStringLiteral("retargetBookmarkAction"));
    deleteBookmarkAction_ = bookmarkMenu->addAction(tr("Eliminar marcador…"));
    deleteBookmarkAction_->setObjectName(QStringLiteral("deleteBookmarkAction"));
    bookmarkMenu->addSeparator();
    bookmarkUpAction_ = bookmarkMenu->addAction(tr("Mover marcador arriba"));
    bookmarkUpAction_->setObjectName(QStringLiteral("bookmarkUpAction"));
    bookmarkDownAction_ = bookmarkMenu->addAction(tr("Mover marcador abajo"));
    bookmarkDownAction_->setObjectName(QStringLiteral("bookmarkDownAction"));
    indentBookmarkAction_ = bookmarkMenu->addAction(tr("Convertir en submarcador del anterior"));
    indentBookmarkAction_->setObjectName(QStringLiteral("indentBookmarkAction"));
    outdentBookmarkAction_ = bookmarkMenu->addAction(tr("Subir un nivel"));
    outdentBookmarkAction_->setObjectName(QStringLiteral("outdentBookmarkAction"));
    connect(addBookmarkAction_, &QAction::triggered, this, [this] { changeSelectedBookmark(PdfBookmarkChange::Add); });
    connect(addChildBookmarkAction_, &QAction::triggered, this, [this] { changeSelectedBookmark(PdfBookmarkChange::Add, true); });
    connect(renameBookmarkAction_, &QAction::triggered, this, [this] { changeSelectedBookmark(PdfBookmarkChange::Rename); });
    connect(retargetBookmarkAction_, &QAction::triggered, this, [this] { changeSelectedBookmark(PdfBookmarkChange::Retarget); });
    connect(deleteBookmarkAction_, &QAction::triggered, this, [this] { changeSelectedBookmark(PdfBookmarkChange::Delete); });
    connect(bookmarkUpAction_, &QAction::triggered, this, [this] { changeSelectedBookmark(PdfBookmarkChange::MoveUp); });
    connect(bookmarkDownAction_, &QAction::triggered, this, [this] { changeSelectedBookmark(PdfBookmarkChange::MoveDown); });
    connect(indentBookmarkAction_, &QAction::triggered, this, [this] { changeSelectedBookmark(PdfBookmarkChange::Indent); });
    connect(outdentBookmarkAction_, &QAction::triggered, this, [this] { changeSelectedBookmark(PdfBookmarkChange::Outdent); });
    auto* formMenu = menuBar()->addMenu(tr("&Formularios"));
    fillFormAction_ = formMenu->addAction(tr("&Rellenar campos de esta página…"));
    fillFormAction_->setObjectName(QStringLiteral("fillFormAction"));
    connect(fillFormAction_, &QAction::triggered, this, &MainWindow::fillPageForm);
    createFormAction_ = formMenu->addAction(tr("Crear campo de formulario…"));
    createFormAction_->setObjectName(QStringLiteral("createFormFieldAction"));
    connect(createFormAction_, &QAction::triggered, this, &MainWindow::createPageFormField);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    error_ = new QLabel(central);
    error_->setObjectName(QStringLiteral("documentError"));
    error_->setTextFormat(Qt::PlainText);
    error_->setWordWrap(true);
    error_->setAccessibleName(tr("Mensaje del documento"));
    error_->hide();
    layout->addWidget(error_);

    title_ = new QLabel(central);
    title_->setTextFormat(Qt::PlainText);
    title_->setWordWrap(true);
    auto titleFont = title_->font();
    titleFont.setPointSize(24);
    titleFont.setBold(true);
    title_->setFont(titleFont);
    title_->setAlignment(Qt::AlignCenter);

    description_ = new QLabel(central);
    description_->setTextFormat(Qt::PlainText);
    description_->setWordWrap(true);
    description_->setAlignment(Qt::AlignCenter);

    content_ = new QStackedWidget(central);
    auto* placeholder = new QWidget(content_);
    auto* placeholderLayout = new QVBoxLayout(placeholder);
    placeholderLayout->addStretch();
    placeholderLayout->addWidget(title_);
    placeholderLayout->addWidget(description_);
    placeholderLayout->addStretch();
    content_->addWidget(placeholder);
    documentPane_ = new QWidget(content_);
    auto* documentLayout = new QHBoxLayout(documentPane_);
    documentLayout->setContentsMargins(0, 0, 0, 0);
    auto* sidebar = new QWidget(documentPane_);
    sidebar->setFixedWidth(210);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    auto* sidebarTabs = new QTabWidget(sidebar);
    sidebarTabs->setObjectName(QStringLiteral("navigationTabs"));
    thumbnails_ = new ThumbnailView(sidebarTabs);
    sidebarTabs->addTab(thumbnails_, tr("Páginas"));
    bookmarks_ = new QTreeWidget(sidebarTabs);
    bookmarks_->setObjectName(QStringLiteral("bookmarkTree"));
    bookmarks_->setAccessibleName(tr("Marcadores del documento"));
    bookmarks_->setHeaderHidden(true);
    bookmarks_->setWordWrap(true);
    bookmarks_->setContextMenuPolicy(Qt::ActionsContextMenu);
    for (auto* action : {addBookmarkAction_, addChildBookmarkAction_, renameBookmarkAction_, retargetBookmarkAction_, deleteBookmarkAction_,
             bookmarkUpAction_, bookmarkDownAction_, indentBookmarkAction_, outdentBookmarkAction_}) {
        bookmarks_->addAction(action);
    }
    connect(bookmarks_, &QTreeWidget::currentItemChanged, this, &MainWindow::updateBookmarkActions);
    sidebarTabs->addTab(bookmarks_, tr("Marcadores"));
    sidebarLayout->addWidget(sidebarTabs, 1);
    pageView_ = new PageView(documentPane_);
    pageView_->addAction(copyTextAction_);
    const auto navigateBookmark = [this](QTreeWidgetItem* item) {
        if (!item || operation_ != Operation::Idle) { return; }
        const auto page = item->data(0, Qt::UserRole).toLongLong();
        if (page >= 0) { pageView_->setPage(page); }
    };
    connect(bookmarks_, &QTreeWidget::currentItemChanged, this, navigateBookmark);
    connect(bookmarks_, &QTreeWidget::itemClicked, this, navigateBookmark);
    connect(bookmarks_, &QTreeWidget::itemActivated, this, navigateBookmark);
    connect(selectTextAction_, &QAction::toggled, this, [this](bool enabled) {
        if (enabled) { addNoteAction_->setChecked(false); }
        pageView_->setTextSelectionEnabled(enabled && document_ && operation_ == Operation::Idle);
    });
    connect(pageView_, &PageView::textSelectionChanged, this, &MainWindow::updateSelectionActions);
    connect(pageView_, &PageView::textSelectionFailed, this, [this](const QString& message) {
        statusBar()->showMessage(message, 6000);
    });
    documentLayout->addWidget(sidebar);
    documentLayout->addWidget(pageView_, 1);
    content_->addWidget(documentPane_);
    layout->addWidget(content_, 1);
    setCentralWidget(central);
    pendingLabel_ = new QLabel(tr("Cambios pendientes"), this);
    pendingLabel_->setObjectName(QStringLiteral("pendingEdits"));
    statusBar()->addPermanentWidget(pendingLabel_);

    viewTools_ = addToolBar(tr("Vista"));
    viewTools_->setObjectName(QStringLiteral("viewTools"));
    viewTools_->setMovable(false);
    viewTools_->addAction(selectTextAction_);
    viewTools_->addAction(copyTextAction_);
    auto* marksMenu = new QMenu(tr("Marcar selección"), this);
    annotateMenu->addMenu(marksMenu);
    const std::pair<const char*, PdfTextMarkup> markTypes[] = {
        {"Resaltar selección", PdfTextMarkup::Highlight}, {"Subrayar selección", PdfTextMarkup::Underline},
        {"Tachar selección", PdfTextMarkup::StrikeOut}, {"Ondulado en selección", PdfTextMarkup::Squiggly}
    };
    for (const auto& [label, type] : markTypes) {
        auto* action = marksMenu->addAction(tr(label));
        action->setObjectName(QStringLiteral("markSelection%1Action").arg(static_cast<int>(type)));
        selectionMarkActions_.append(action);
        pageView_->addAction(action);
        connect(action, &QAction::triggered, this, [this, type] {
            const auto match = pageView_->textSelection();
            if (!document_ || operation_ != Operation::Idle || match.text.isEmpty()) { return; }
            const auto document = document_;
            startOperation(Operation::Annotate, tr("Marcando texto seleccionado…"), [document, match, type] {
                return document->markMatch(match, type);
            });
        });
    }
    pageView_->setContextMenuPolicy(Qt::ActionsContextMenu);
    auto* markTool = new QToolButton(viewTools_);
    markTool->setObjectName(QStringLiteral("selectionMarkupTool"));
    markTool->setPopupMode(QToolButton::MenuButtonPopup);
    markTool->setMenu(marksMenu);
    markTool->setDefaultAction(selectionMarkActions_.front());
    viewTools_->addWidget(markTool);
    viewTools_->addSeparator();
    previousPage_ = viewTools_->addAction(tr("Anterior"));
    previousPage_->setObjectName(QStringLiteral("previousPageAction"));
    previousPage_->setShortcut(QKeySequence(QStringLiteral("Ctrl+PgUp")));
    pageNumber_ = new QSpinBox(viewTools_);
    pageNumber_->setObjectName(QStringLiteral("pageNumber"));
    pageNumber_->setAccessibleName(tr("Ir a la página"));
    pageNumber_->setAlignment(Qt::AlignCenter);
    pageNumber_->setMinimumWidth(88);
    pageNumber_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    pageNumber_->setKeyboardTracking(false);
    viewTools_->addWidget(pageNumber_);
    nextPage_ = viewTools_->addAction(tr("Siguiente"));
    nextPage_->setObjectName(QStringLiteral("nextPageAction"));
    nextPage_->setShortcut(QKeySequence(QStringLiteral("Ctrl+PgDown")));
    viewTools_->addSeparator();
    connect(previousPage_, &QAction::triggered, this, [this] { pageView_->setPage(pageView_->currentPage() - 1); });
    connect(nextPage_, &QAction::triggered, this, [this] { pageView_->setPage(pageView_->currentPage() + 1); });
    connect(pageNumber_, &QSpinBox::valueChanged, this, [this](int number) { pageView_->setPage(number - 1); });
    connect(thumbnails_, &ThumbnailView::pageSelected, pageView_, &PageView::setPage);
    connect(pageView_, &PageView::pageChanged, this, &MainWindow::updateNavigation);
    connect(pageView_, &PageView::pageChanged, this, &MainWindow::workspaceStateChanged);
    connect(pageView_, &PageView::zoomChanged, this, &MainWindow::workspaceStateChanged);

    auto* zoomOut = viewTools_->addAction(tr("Reducir"));
    zoomOut->setObjectName(QStringLiteral("zoomOutAction"));
    zoomOut->setShortcut(QKeySequence::ZoomOut);
    auto* zoomText = new QLabel(viewTools_);
    zoomText->setObjectName(QStringLiteral("zoomText"));
    zoomText->setMinimumWidth(64);
    zoomText->setAlignment(Qt::AlignCenter);
    zoomText->setAccessibleName(tr("Nivel de zoom"));
    viewTools_->addWidget(zoomText);
    auto* zoomIn = viewTools_->addAction(tr("Ampliar"));
    zoomIn->setObjectName(QStringLiteral("zoomInAction"));
    zoomIn->setShortcut(QKeySequence::ZoomIn);
    viewTools_->addSeparator();
    auto* actualSize = viewTools_->addAction(tr("100 %"));
    actualSize->setObjectName(QStringLiteral("actualSizeAction"));
    actualSize->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
    auto* fitWidth = viewTools_->addAction(tr("Ajustar al ancho"));
    fitWidth->setObjectName(QStringLiteral("fitWidthAction"));
    fitWidth->setCheckable(true);
    connect(zoomOut, &QAction::triggered, this, [this] { pageView_->setZoom(pageView_->zoom() / 1.25); });
    connect(zoomIn, &QAction::triggered, this, [this] { pageView_->setZoom(pageView_->zoom() * 1.25); });
    connect(actualSize, &QAction::triggered, this, [this] { pageView_->setZoom(1.0); });
    connect(fitWidth, &QAction::triggered, pageView_, &PageView::fitToWidth);
    connect(pageView_, &PageView::zoomChanged, this, [zoomText, fitWidth, zoomIn, zoomOut](qreal zoom, bool fitted) {
        zoomText->setText(tr("%1 %").arg(qRound(zoom * 100)));
        fitWidth->setChecked(fitted);
        zoomIn->setEnabled(zoom < 4.0);
        zoomOut->setEnabled(zoom > 0.1);
    });
    searchBar_ = new SearchBar(this);
    addToolBarBreak(Qt::TopToolBarArea);
    addToolBar(searchBar_);
    searchBar_->hide();
    connect(searchAction_, &QAction::triggered, this, [this] {
        addNoteAction_->setChecked(false);
        searchBar_->focusQuery();
    });
    connect(searchBar_, &SearchBar::matchSelected, pageView_, &PageView::showSearchMatch);
    connect(searchBar_, &SearchBar::matchCleared, pageView_, &PageView::clearSearchMatch);
    connect(searchBar_, &SearchBar::markupRequested, this, [this](const PdfSearchMatch& match, PdfTextMarkup type) {
        if (!document_ || operation_ != Operation::Idle) { return; }
        startOperation(Operation::Annotate, tr("Aplicando marca de texto…"), [document = document_, selected = match, type] {
            return document->markMatch(selected, type);
        });
    });
    auto* cancelTool = new QShortcut(Qt::Key_Escape, this);
    const auto updateCancel = [this, cancelTool] {
        cancelTool->setEnabled(addNoteAction_->isChecked() || searchBar_->isVisible()
            || !pageView_->textSelection().text.isEmpty());
    };
    updateCancel();
    connect(searchBar_, &QToolBar::visibilityChanged, cancelTool, updateCancel);
    connect(pageView_, &PageView::textSelectionChanged, cancelTool, updateCancel);
    connect(cancelTool, &QShortcut::activated, this, [this] {
        if (addNoteAction_->isChecked()) { addNoteAction_->setChecked(false); }
        else { pageView_->clearTextSelection(); searchBar_->hide(); }
    });
    connect(addNoteAction_, &QAction::toggled, this, [this, updateCancel](bool enabled) {
        if (enabled) { selectTextAction_->setChecked(false); }
        pageView_->setPointSelectionEnabled(enabled);
        updateCancel();
        if (enabled) {
            statusBar()->showMessage(tr("Haz clic en la página o pulsa Intro para colocar la nota. Esc cancela."));
        } else {
            statusBar()->clearMessage();
        }
    });
    connect(pageView_, &PageView::pointSelected, this, &MainWindow::addNote);

    connect(pageView_, &PageView::renderStarted, this, [this] {
        if (renderMessage_) { error_->hide(); renderMessage_ = false; }
        if (operation_ == Operation::Idle && !addNoteAction_->isChecked()) {
            statusBar()->showMessage(tr("Mostrando página %1…").arg(pageView_->currentPage() + 1));
        }
    });
    connect(pageView_, &PageView::pageRendered, this, [this] {
        if (operation_ == Operation::Idle && !addNoteAction_->isChecked()) {
            statusBar()->showMessage(tr("Página %1 de %2 · %3 %")
                .arg(pageView_->currentPage() + 1).arg(pageCount()).arg(qRound(pageView_->zoom() * 100)));
        }
    });
    const auto showRenderMessage = [this](const QString& message) {
        renderMessage_ = true;
        error_->setText(message);
        error_->show();
    };
    connect(pageView_, &PageView::renderFailed, this, [this, showRenderMessage](const QString& message) {
        showRenderMessage(message);
        if (operation_ == Operation::Idle) { statusBar()->showMessage(tr("No se pudo mostrar la página")); }
    });
    connect(pageView_, &PageView::renderWarning, this, showRenderMessage);

    watcher_ = new QFutureWatcher<PdfOpenResult>(this);
    connect(watcher_, &QFutureWatcher<PdfOpenResult>::finished, this, &MainWindow::finishOperation);
    updateDocumentView();
}

QString MainWindow::currentFilePath() const
{
    return document_ ? document_->filePath() : QString{};
}

qsizetype MainWindow::pageCount() const
{
    return document_ ? document_->pageCount() : 0;
}

SessionDocument MainWindow::sessionDocument(const QString& id, bool discardChanges) const
{
    SessionDocument result;
    result.entry.id = id;
    result.entry.page = std::max(qsizetype{0}, pageView_->currentPage());
    result.entry.zoom = pageView_->zoom();
    result.entry.fitWidth = pageView_->fitsWidth();
    result.modified = !discardChanges && isWindowModified();
    result.document = discardChanges && isWindowModified() ? savedDocument_ : document_;
    if (result.document) { result.entry.page = std::min(result.entry.page, result.document->pageCount() - 1); }
    return result;
}

void MainWindow::adoptSessionDocument(const SessionRestoredDocument& restored)
{
    if (isBusy() || !restored.document) { return; }
    document_ = restored.document;
    savedDocument_ = restored.savedDocument;
    undoHistory_.clear();
    redoHistory_.clear();
    setWindowModified(!restored.entry.snapshot.isEmpty());
    updateDocumentView();
    pageView_->setPage(std::clamp(restored.entry.page, qsizetype{0}, document_->pageCount() - 1));
    if (restored.entry.fitWidth) { pageView_->fitToWidth(); }
    else { pageView_->setZoom(restored.entry.zoom); }
    if (isWindowModified()) { statusBar()->showMessage(tr("Cambios recuperados · Revisa el documento y guárdalo para conservarlos.")); }
    emit documentOpened(document_->filePath(), document_->pageCount());
    emit workspaceStateChanged();
}

void MainWindow::configureWorkspace(std::function<void(const QString&)> open, std::function<void()> close,
    std::function<void()> restore, const QStringList& recentFiles, bool restoreAvailable,
    std::function<void()> clearRecent, std::function<void()> openRecoveryFolder)
{
    workspaceOpen_ = std::move(open);
    workspaceClose_ = std::move(close);
    if (!recentMenu_) {
        auto* fileMenu = qobject_cast<QMenu*>(openAction_->parent());
        if (!fileMenu) { return; }
        recentMenu_ = new QMenu(tr("Archivos recientes"), fileMenu);
        recentMenu_->setObjectName(QStringLiteral("recentFilesMenu"));
        fileMenu->insertMenu(saveAction_, recentMenu_);
        restoreSessionAction_ = new QAction(tr("Restaurar sesión anterior…"), fileMenu);
        restoreSessionAction_->setObjectName(QStringLiteral("restoreSessionAction"));
        fileMenu->insertAction(closeAction_, restoreSessionAction_);
        recoveryFolderAction_ = new QAction(tr("Abrir carpeta de recuperación"), fileMenu);
        recoveryFolderAction_->setObjectName(QStringLiteral("recoveryFolderAction"));
        fileMenu->insertAction(closeAction_, recoveryFolderAction_);
    }
    recentMenu_->clear();
    recentMenu_->setEnabled(!recentFiles.isEmpty());
    for (const auto& path : recentFiles) {
        auto* action = recentMenu_->addAction(QFileInfo(path).fileName());
        action->setToolTip(QDir::toNativeSeparators(path));
        action->setData(path);
        connect(action, &QAction::triggered, this, [this, path] { if (workspaceOpen_) { workspaceOpen_(path); } });
    }
    if (!recentFiles.isEmpty()) {
        recentMenu_->addSeparator();
        auto* clear = recentMenu_->addAction(tr("Vaciar lista de recientes"));
        clear->setObjectName(QStringLiteral("clearRecentFilesAction"));
        connect(clear, &QAction::triggered, this, [this, clearRecent = std::move(clearRecent)] {
            QMetaObject::invokeMethod(this, clearRecent, Qt::QueuedConnection);
        });
    }
    disconnect(restoreSessionAction_, nullptr, this, nullptr);
    restoreSessionAction_->setEnabled(restoreAvailable);
    connect(restoreSessionAction_, &QAction::triggered, this, [restore = std::move(restore)] { restore(); });
    disconnect(recoveryFolderAction_, nullptr, this, nullptr);
    connect(recoveryFolderAction_, &QAction::triggered, this, [openRecoveryFolder = std::move(openRecoveryFolder)] { openRecoveryFolder(); });
}

void MainWindow::prepareClose(std::function<void()> proceed, std::function<void()> cancelled)
{
    if (isBusy()) { if (cancelled) { cancelled(); } return; }
    confirmPendingChanges(std::move(proceed), std::move(cancelled));
}

void MainWindow::openDocument(const QString& filePath)
{
    if (filePath.isEmpty() || operation_ != Operation::Idle) { return; }
    confirmPendingChanges([this, filePath] {
        pendingOpenPath_ = filePath;
        passwordAttempts_ = 0;
        startOperation(Operation::Open, tr("Abriendo documento…"), [filePath] { return PdfDocument::open(filePath); });
    });
}

void MainWindow::startOperation(Operation operation, const QString& status, std::function<PdfOpenResult()> work)
{
    addNoteAction_->setChecked(false);
    error_->hide();
    renderMessage_ = false;
    operation_ = operation;
    updateDocumentActions();
    statusBar()->showMessage(status);
    watcher_->setFuture(QtConcurrent::run(std::move(work)));
}

void MainWindow::startOutput(const QString& status, const QString& completed, std::function<PdfOutputResult()> work)
{
    outputMessage_ = completed;
    startOperation(Operation::Output, status, [document = document_, work = std::move(work)] {
        try {
            auto result = work();
            return PdfOpenResult{result.error.isEmpty() ? document : nullptr, std::move(result.error),
                std::move(result.warnings)};
        } catch (const std::exception&) {
            return PdfOpenResult{nullptr, tr("Ocurrió un error al preparar la salida."), {}};
        }
    });
}

void MainWindow::finishOperation()
{
    const auto result = watcher_->result();
    const auto completed = operation_;
    operation_ = Operation::Idle;
    const auto destinationPage = std::exchange(pageDestination_, qsizetype{-1});
    const auto outputMessage = std::exchange(outputMessage_, QString{});
    auto followUp = std::move(afterSave_);
    afterSave_ = {};
    auto failed = std::move(afterSaveFailed_);
    afterSaveFailed_ = {};
    renderMessage_ = false;
    if (!result.document) {
        if (completed == Operation::Open && result.passwordRequired && passwordAttempts_ < 3) {
            updateDocumentActions();
            bool accepted = false;
            const auto password = QInputDialog::getText(this, tr("Contraseña del PDF"), passwordAttempts_ == 0 ? tr("Introduce la contraseña para abrir el documento.")
                : tr("La contraseña anterior no es válida. Inténtalo de nuevo."), QLineEdit::Password, {}, &accepted);
            if (accepted && !password.isEmpty()) {
                ++passwordAttempts_;
                startOperation(Operation::Open, tr("Abriendo documento protegido…"), [path = pendingOpenPath_, password] { return PdfDocument::open(path, password); });
                return;
            }
            statusBar()->showMessage(tr("Apertura cancelada"));
            emit documentOpenFailed(tr("Apertura cancelada"));
            return;
        }
        requestedBookmarkPath_.clear();
        updateDocumentActions();
        error_->setText(result.error);
        error_->show();
        if (completed == Operation::Save) { statusBar()->showMessage(tr("No se pudo guardar el PDF")); }
        if (completed == Operation::Rotate) { statusBar()->showMessage(tr("No se pudo girar la página")); }
        if (completed == Operation::Move) { statusBar()->showMessage(tr("No se pudo mover la página")); }
        if (completed == Operation::Pages) { statusBar()->showMessage(tr("No se pudo modificar la organización de páginas")); }
        if (completed == Operation::LoadImport) { statusBar()->showMessage(tr("No se pudo leer el PDF de origen")); }
        if (completed == Operation::Extract) { statusBar()->showMessage(tr("No se pudieron extraer las páginas")); }
        if (completed == Operation::Annotate) { statusBar()->showMessage(tr("No se pudo aplicar el cambio")); }
        if (completed == Operation::Output) { statusBar()->showMessage(tr("No se pudo completar la salida")); }
        if (completed == Operation::Open) {
            statusBar()->showMessage(tr("No se pudo abrir el PDF"));
            emit documentOpenFailed(result.error);
        }
        if (failed) { failed(); }
        return;
    }

    if (completed == Operation::LoadImport) {
        updateDocumentActions();
        configurePageImport(result.document);
        return;
    }

    if (completed == Operation::Extract) {
        updateDocumentActions();
        statusBar()->showMessage(tr("Páginas guardadas en «%1»").arg(QFileInfo(result.document->filePath()).fileName()));
        return;
    }
    if (completed == Operation::Output) {
        updateDocumentActions();
        statusBar()->showMessage(outputMessage);
        if (!result.warnings.isEmpty()) {
            error_->setText(result.warnings.join(u'\n'));
            error_->show();
        }
        return;
    }

    const bool edit = completed == Operation::Rotate || completed == Operation::Move ||
        completed == Operation::Pages || completed == Operation::Annotate;
    if (edit) {
        constexpr qsizetype historyLimit = 20;
        undoHistory_.append({document_, pageView_->currentPage()});
        if (undoHistory_.size() > historyLimit) { undoHistory_.removeFirst(); }
        redoHistory_.clear();
    } else {
        savedDocument_ = result.document;
        undoHistory_.clear();
        redoHistory_.clear();
    }
    document_ = result.document;
    setWindowModified(document_ != savedDocument_);
    error_->hide();
    updateDocumentView(completed != Operation::Open);
    if (completed == Operation::Save) { statusBar()->showMessage(tr("PDF guardado")); }
    if (completed == Operation::Rotate) { statusBar()->showMessage(tr("Giro aplicado · Cambios pendientes")); }
    if (completed == Operation::Annotate) { statusBar()->showMessage(tr("Edición aplicada · Cambios pendientes")); }
    if (completed == Operation::Move) {
        pageView_->setPage(destinationPage);
        statusBar()->showMessage(tr("Página movida a la posición %1 · Cambios pendientes").arg(destinationPage + 1));
    }
    if (completed == Operation::Pages) {
        pageView_->setPage(destinationPage);
        statusBar()->showMessage(tr("Páginas actualizadas · Cambios pendientes"));
    }
    if (completed == Operation::Open) { emit documentOpened(document_->filePath(), document_->pageCount()); }
    if (followUp) { followUp(); }
}

void MainWindow::restoreHistory(QList<HistoryEntry>& from, QList<HistoryEntry>& to)
{
    if (operation_ != Operation::Idle || from.isEmpty()) { return; }
    addNoteAction_->setChecked(false);
    const auto entry = from.takeLast();
    to.append({document_, pageView_->currentPage()});
    document_ = entry.document;
    setWindowModified(document_ != savedDocument_);
    error_->hide();
    renderMessage_ = false;
    updateDocumentView(true);
    pageView_->setPage(entry.page);
}

void MainWindow::rotateCurrentPage()
{
    if (!document_ || operation_ != Operation::Idle) { return; }
    const auto pageIndex = pageView_->currentPage();
    startOperation(Operation::Rotate, tr("Girando página…"), [document = document_, pageIndex] {
        return document->rotatePage(pageIndex);
    });
}

void MainWindow::moveCurrentPage(qsizetype offset)
{
    if (!document_ || operation_ != Operation::Idle) { return; }
    const auto from = pageView_->currentPage();
    const auto to = from + offset;
    if (from < 0 || to < 0 || to >= pageCount()) { return; }
    pageDestination_ = to;
    startOperation(Operation::Move, tr("Moviendo página…"), [document = document_, from, to] {
        return document->movePage(from, to);
    });
}

void MainWindow::changeCurrentPage(PdfPageOperation operation)
{
    if (!document_ || operation_ != Operation::Idle) { return; }
    const auto page = pageView_->currentPage();
    if (page < 0 || page >= pageCount()) { return; }
    if (operation == PdfPageOperation::Delete) {
        if (pageCount() < 2) { return; }
        const auto answer = QMessageBox::question(this, tr("Eliminar página"),
            tr("¿Quieres eliminar la página %1? Puedes recuperarla con Deshacer.").arg(page + 1),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) { return; }
    }
    pageDestination_ = operation == PdfPageOperation::Delete ? std::min(page, pageCount() - 2) : page + 1;
    startOperation(Operation::Pages, tr("Actualizando páginas…"), [document = document_, page, operation] {
        return document->changePage(page, operation);
    });
}

void MainWindow::cropPages(bool reset)
{
    if (!document_ || operation_ != Operation::Idle) { return; }
    addNoteAction_->setChecked(false);
    CropDialog dialog(document_, pageView_->currentPage(), reset, this);
    if (dialog.exec() != QDialog::Accepted) { return; }
    const auto document = document_;
    const auto first = dialog.firstPage();
    const auto last = dialog.lastPage();
    const auto margins = dialog.margins();
    startOperation(Operation::Annotate, reset ? tr("Restaurando área visible…") : tr("Recortando páginas…"),
        [document, first, last, margins, reset] {
            return reset ? document->resetPageCrop(first, last) : document->cropPages(first, last, margins);
        });
}

void MainWindow::insertContent(bool imageMode)
{
    if (!document_ || operation_ != Operation::Idle) { return; }
    QImage image;
    if (imageMode) {
        const auto path = QFileDialog::getOpenFileName(this, tr("Elegir imagen"), QFileInfo(currentFilePath()).absolutePath(), tr("Imágenes (*.png *.jpg *.jpeg)"));
        if (path.isEmpty()) { return; }
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const auto size = reader.size();
        if (QFileInfo(path).size() > 64 * 1024 * 1024 || !size.isValid() || size.width() > 8192 || size.height() > 8192 || qint64(size.width()) * size.height() > 16000000) {
            QMessageBox::warning(this, tr("Imagen no compatible"), tr("Elige una imagen de hasta 64 MB, 8192 píxeles por lado y 16 millones de píxeles."));
            return;
        }
        image = reader.read();
        if (image.isNull()) { QMessageBox::warning(this, tr("No se pudo leer la imagen"), reader.errorString()); return; }
    }
    addNoteAction_->setChecked(false);
    searchBar_->hide();
    const auto page = pageView_->currentPage();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("insertContentDialog"));
    dialog.setWindowTitle(imageMode ? tr("Insertar imagen") : tr("Insertar texto nuevo"));
    auto* form = new QFormLayout(&dialog);
    auto* description = new QLabel(tr("El área se mide como porcentaje de la página visible. Se muestra delimitada en el visor. Se añade contenido nuevo y se conserva el existente."), &dialog);
    description->setWordWrap(true);
    form->addRow(description);
    QPlainTextEdit* text = nullptr;
    QDoubleSpinBox* font = nullptr;
    std::optional<TextStyleControls> styleControls;
    if (!imageMode) {
        text = new QPlainTextEdit(&dialog);
        text->setObjectName(QStringLiteral("insertText"));
        text->setTabChangesFocus(true);
        text->setMaximumHeight(120);
        form->addRow(tr("&Texto"), text);
        font = new QDoubleSpinBox(&dialog);
        font->setObjectName(QStringLiteral("insertPointSize"));
        font->setRange(4, 144);
        font->setValue(12);
        font->setSuffix(tr(" pt"));
        form->addRow(tr("Tamaño de &letra"), font);
        styleControls = addTextStyleControls(form, &dialog, QStringLiteral("insert"), font);
    } else {
        auto* info = new QLabel(tr("La imagen conservará sus proporciones dentro del área indicada."), &dialog);
        info->setWordWrap(true);
        form->addRow(info);
    }
    QDoubleSpinBox* geometry[4]{};
    const char* names[] = {"insertLeft", "insertTop", "insertWidth", "insertHeight"};
    const QString labels[] = {tr("Desde el borde &izquierdo"), tr("Desde el borde &superior"), tr("&Ancho"), tr("A&lto")};
    const qreal defaults[] = {10, 10, imageMode ? 40.0 : 80.0, imageMode ? 40.0 : 25.0};
    for (int i = 0; i < 4; ++i) {
        geometry[i] = new QDoubleSpinBox(&dialog);
        geometry[i]->setObjectName(QString::fromLatin1(names[i]));
        geometry[i]->setRange(i < 2 ? 0 : 0.1, 100);
        geometry[i]->setDecimals(1);
        geometry[i]->setSuffix(QStringLiteral(" %"));
        geometry[i]->setValue(defaults[i]);
        form->addRow(labels[i], geometry[i]);
    }
    const auto area = [&geometry] { return QRectF(geometry[0]->value() / 100, geometry[1]->value() / 100,
        geometry[2]->value() / 100, geometry[3]->value() / 100); };
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel, &dialog);
    auto* apply = buttons->button(QDialogButtonBox::Apply);
    apply->setObjectName(QStringLiteral("insertContentApply"));
    apply->setText(tr("Insertar"));
    apply->setDefault(true);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    const auto update = [this, page, area, apply, text] {
        const auto box = area();
        const bool valid = box.isValid() && QRectF(0, 0, 1, 1).contains(box);
        apply->setEnabled(valid && (!text || (!text->toPlainText().trimmed().isEmpty() && text->toPlainText().size() <= 10000)));
        QPainterPath path;
        if (valid) { path.addRect(box); }
        pageView_->showSearchMatch({page, path, {}, {}});
    };
    for (auto* spin : geometry) { connect(spin, &QDoubleSpinBox::valueChanged, &dialog, update); }
    if (text) { connect(text, &QPlainTextEdit::textChanged, &dialog, update); }
    connect(apply, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    dialog.resize(480, imageMode ? 320 : 630);
    update();
    const auto accepted = dialog.exec() == QDialog::Accepted;
    pageView_->clearSearchMatch();
    if (!accepted) { return; }
    const auto document = document_;
    const auto box = area();
    const auto value = text ? text->toPlainText() : QString{};
    const auto style = styleControls ? styleControls->value() : PdfTextStyle{};
    startOperation(Operation::Annotate, tr("Insertando contenido nuevo…"), [document, page, box, value, style, image] {
        return image.isNull() ? document->insertStyledText(page, box, value, style) : document->insertImage(page, box, image);
    });
}

void MainWindow::editProperties()
{
    if (!document_ || operation_ != Operation::Idle) { return; }
    const auto original = document_->metadata();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("documentPropertiesDialog"));
    dialog.setWindowTitle(tr("Propiedades del documento"));
    auto* form = new QFormLayout(&dialog);
    auto* path = new QLabel(QDir::toNativeSeparators(currentFilePath()), &dialog);
    path->setWordWrap(true);
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(tr("Archivo"), path);
    form->addRow(tr("Páginas"), new QLabel(QString::number(pageCount()), &dialog));
    QLineEdit* fields[4]{};
    const char* names[] = {"metadataTitle", "metadataAuthor", "metadataSubject", "metadataKeywords"};
    const QString labels[] = {tr("&Título"), tr("&Autor"), tr("A&sunto"), tr("&Palabras clave")};
    const QString values[] = {original.title, original.author, original.subject, original.keywords};
    for (int i = 0; i < 4; ++i) {
        fields[i] = new QLineEdit(values[i], &dialog);
        fields[i]->setObjectName(QString::fromLatin1(names[i]));
        fields[i]->setMaxLength(4096);
        form->addRow(labels[i], fields[i]);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("metadataApply"));
    buttons->button(QDialogButtonBox::Save)->setText(tr("Aplicar"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    dialog.resize(520, 280);
    if (dialog.exec() != QDialog::Accepted) { return; }
    PdfMetadata valuesAfter{fields[0]->text(), fields[1]->text(), fields[2]->text(), fields[3]->text()};
    if (valuesAfter.title == original.title && valuesAfter.author == original.author &&
        valuesAfter.subject == original.subject && valuesAfter.keywords == original.keywords) { return; }
    startOperation(Operation::Annotate, tr("Actualizando propiedades…"), [document = document_, valuesAfter] {
        return document->setMetadata(valuesAfter);
    });
}

void MainWindow::decoratePages(bool numbering)
{
    if (!document_ || operation_ != Operation::Idle) { return; }
    addNoteAction_->setChecked(false);
    searchBar_->hide();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("pageDecorationDialog"));
    dialog.setWindowTitle(numbering ? tr("Numerar páginas") : tr("Marca de agua o sello de texto"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* scroll = new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    auto* body = new QWidget(scroll);
    auto* form = new QFormLayout(body);
    scroll->setWidget(body);
    layout->addWidget(scroll);
    auto* hint = new QLabel(numbering ? tr("Usa {n} para el número secuencial y {total} para las páginas del intervalo. Se añade numeración visible sin cambiar las etiquetas internas del PDF.")
        : tr("El texto se añade a las páginas elegidas. La opacidad y el giro permiten crear una marca de agua; al 100 % y sin giro funciona como sello."), body);
    hint->setWordWrap(true);
    form->addRow(hint);
    auto* first = new QSpinBox(body);
    auto* last = new QSpinBox(body);
    first->setObjectName(QStringLiteral("decorationFirstPage"));
    last->setObjectName(QStringLiteral("decorationLastPage"));
    first->setRange(1, static_cast<int>(pageCount()));
    last->setRange(1, static_cast<int>(pageCount()));
    last->setValue(static_cast<int>(pageCount()));
    form->addRow(tr("Desde la página"), first);
    form->addRow(tr("Hasta la página"), last);
    auto* text = new QComboBox(body);
    text->setObjectName(QStringLiteral("decorationText"));
    text->setEditable(true);
    if (numbering) { text->addItems({QStringLiteral("{n} / {total}"), tr("Página {n} de {total}"), QStringLiteral("{n}")}); }
    else { text->addItems({tr("CONFIDENCIAL"), tr("BORRADOR"), tr("APROBADO"), tr("COPIA")}); }
    text->lineEdit()->setMaxLength(10000);
    form->addRow(tr("Texto"), text);
    auto* initialNumber = new QSpinBox(body);
    initialNumber->setObjectName(QStringLiteral("decorationFirstNumber"));
    initialNumber->setRange(1, 1000000000);
    form->addRow(tr("Número inicial"), initialNumber);
    initialNumber->setVisible(numbering);
    form->labelForField(initialNumber)->setVisible(numbering);
    auto style = addTextStyleControls(form, body, QStringLiteral("decoration"));
    style.alignment->setCurrentIndex(1);
    style.size->setValue(numbering ? 12 : 18);
    style.bold->setChecked(!numbering);
    auto* rotation = new QDoubleSpinBox(body);
    rotation->setObjectName(QStringLiteral("decorationRotation"));
    rotation->setRange(-180, 180);
    rotation->setSuffix(QStringLiteral("°"));
    rotation->setValue(numbering ? 0 : -30);
    form->addRow(tr("Giro del texto"), rotation);
    auto* opacity = new QSpinBox(body);
    opacity->setObjectName(QStringLiteral("decorationOpacity"));
    opacity->setRange(1, 100);
    opacity->setSuffix(QStringLiteral(" %"));
    opacity->setValue(numbering ? 100 : 25);
    form->addRow(tr("Opacidad"), opacity);
    auto* behind = new QCheckBox(tr("Colocar detrás del contenido existente"), body);
    behind->setObjectName(QStringLiteral("decorationBehind"));
    behind->setToolTip(tr("Las imágenes o fondos opacos existentes pueden ocultar el texto."));
    form->addRow(behind);
    auto* position = new QComboBox(body);
    position->setObjectName(QStringLiteral("decorationPosition"));
    position->addItems({tr("Pie de página"), tr("Encabezado"), tr("Centro"), tr("Personalizada")});
    position->setCurrentIndex(numbering ? 0 : 2);
    form->addRow(tr("Posición"), position);
    const auto area = addAreaControls(form, body, QStringLiteral("decoration"), numbering ? QRectF(0.1, 0.87, 0.8, 0.1) : QRectF(0.1, 0.25, 0.8, 0.5));
    auto* summary = new QLabel(body);
    summary->setWordWrap(true);
    form->addRow(summary);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel, &dialog);
    auto* apply = buttons->button(QDialogButtonBox::Apply);
    apply->setObjectName(QStringLiteral("decorationApply"));
    apply->setText(tr("Aplicar"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    layout->addWidget(buttons);
    const auto update = [this, first, last, text, summary, area, apply, numbering] {
        const auto count = last->value() - first->value() + 1;
        const auto box = area.value();
        const bool valid = count > 0 && count <= 500 && box.isValid() && QRectF(0, 0, 1, 1).contains(box) &&
            !text->currentText().trimmed().isEmpty() && (!numbering || text->currentText().contains(QStringLiteral("{n}")));
        apply->setEnabled(valid);
        summary->setText(valid ? tr("Se aplicará a %1 páginas como una sola edición. El área marcada indica la posición antes del giro.").arg(count)
            : tr("Revisa el intervalo (hasta 500 páginas), el área y el texto%1.").arg(numbering ? tr(" con {n}") : QString{}));
        QPainterPath path;
        if (box.isValid() && QRectF(0, 0, 1, 1).contains(box)) { path.addRect(box); }
        pageView_->showSearchMatch({pageView_->currentPage(), path, {}, {}});
    };
    connect(first, &QSpinBox::valueChanged, &dialog, update);
    connect(last, &QSpinBox::valueChanged, &dialog, update);
    connect(text, &QComboBox::currentTextChanged, &dialog, update);
    for (auto* field : area.fields) { connect(field, &QDoubleSpinBox::valueChanged, &dialog, update); }
    connect(position, &QComboBox::currentIndexChanged, &dialog, [area](int index) {
        if (index == 3) { return; }
        const QRectF box = index == 0 ? QRectF(0.1, 0.87, 0.8, 0.1) : index == 1 ? QRectF(0.1, 0.03, 0.8, 0.1) : QRectF(0.1, 0.25, 0.8, 0.5);
        const qreal values[] = {box.x(), box.y(), box.width(), box.height()};
        for (int i = 0; i < 4; ++i) { area.fields[i]->setValue(values[i] * 100); }
    });
    connect(apply, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    update();
    dialog.resize(540, 690);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    pageView_->clearSearchMatch();
    if (!accepted) { return; }
    PdfPageDecoration decoration;
    decoration.text = text->currentText();
    decoration.area = area.value();
    decoration.style = style.value();
    decoration.rotation = rotation->value();
    decoration.opacity = opacity->value() / 100.0;
    decoration.behind = behind->isChecked();
    decoration.numbering = numbering;
    decoration.firstNumber = initialNumber->value();
    startOperation(Operation::Annotate, tr("Aplicando texto a las páginas…"), [document = document_, begin = first->value() - 1, end = last->value() - 1, decoration] {
        return document->decoratePages(begin, end, decoration);
    });
}

void MainWindow::addPageLink()
{
    if (!document_ || operation_ != Operation::Idle) { return; }
    addNoteAction_->setChecked(false);
    searchBar_->hide();
    const auto page = pageView_->currentPage();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("addLinkDialog"));
    dialog.setWindowTitle(tr("Añadir enlace"));
    auto* form = new QFormLayout(&dialog);
    auto* type = new QComboBox(&dialog);
    type->setObjectName(QStringLiteral("linkType"));
    type->addItems({tr("Página de este documento"), tr("Página web (http/https)")});
    form->addRow(tr("Destino"), type);
    auto* destination = new QSpinBox(&dialog);
    destination->setObjectName(QStringLiteral("linkTargetPage"));
    destination->setRange(1, static_cast<int>(pageCount()));
    destination->setValue(static_cast<int>(page + 1));
    form->addRow(tr("Página"), destination);
    auto* url = new QLineEdit(&dialog);
    url->setObjectName(QStringLiteral("linkUrl"));
    url->setMaxLength(2048);
    url->setPlaceholderText(QStringLiteral("https://…"));
    form->addRow(tr("Dirección web"), url);
    const auto area = addAreaControls(form, &dialog, QStringLiteral("link"), QRectF(0.1, 0.1, 0.4, 0.1));
    auto* hint = new QLabel(tr("Se añade un área con borde azul. Puedes abrir o eliminar los enlaces desde Insertar → Enlaces de esta página."), &dialog);
    hint->setWordWrap(true);
    form->addRow(hint);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel, &dialog);
    auto* apply = buttons->button(QDialogButtonBox::Apply);
    apply->setObjectName(QStringLiteral("linkApply"));
    apply->setText(tr("Añadir"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    form->addRow(buttons);
    const auto update = [this, page, type, destination, url, area, apply] {
        const bool web = type->currentIndex() == 1;
        destination->setEnabled(!web);
        url->setEnabled(web);
        const QUrl address(url->text(), QUrl::StrictMode);
        const auto box = area.value();
        const bool valid = box.isValid() && QRectF(0, 0, 1, 1).contains(box);
        apply->setEnabled(valid && (!web || (address.isValid() && !address.host().isEmpty() && address.userInfo().isEmpty() &&
            (address.scheme() == QStringLiteral("http") || address.scheme() == QStringLiteral("https")))));
        QPainterPath path;
        if (valid) { path.addRect(box); }
        pageView_->showSearchMatch({page, path, {}, {}});
    };
    connect(type, &QComboBox::currentIndexChanged, &dialog, update);
    connect(url, &QLineEdit::textChanged, &dialog, update);
    for (auto* field : area.fields) { connect(field, &QDoubleSpinBox::valueChanged, &dialog, update); }
    connect(apply, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(480, 440);
    update();
    const bool accepted = dialog.exec() == QDialog::Accepted;
    pageView_->clearSearchMatch();
    if (!accepted) { return; }
    startOperation(Operation::Annotate, tr("Añadiendo enlace…"), [document = document_, page, box = area.value(), target = type->currentIndex() == 0 ? destination->value() - 1 : -1,
        address = type->currentIndex() == 1 ? url->text() : QString{}] { return document->addLink(page, box, target, address); });
}

void MainWindow::showPageLinks()
{
    if (!document_ || operation_ != Operation::Idle) { return; }
    const auto page = pageView_->currentPage();
    const auto links = document_->pageLinks(page);
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("pageLinksDialog"));
    dialog.setWindowTitle(tr("Enlaces de la página %1").arg(page + 1));
    auto* layout = new QVBoxLayout(&dialog);
    auto* list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("pageLinksList"));
    for (const auto& link : links) {
        list->addItem(link.targetPage >= 0 ? tr("Página %1").arg(link.targetPage + 1) : !link.url.isEmpty() ? link.url : tr("Destino no compatible"));
    }
    if (links.isEmpty()) { layout->addWidget(new QLabel(tr("Esta página no tiene enlaces visibles."), &dialog)); }
    layout->addWidget(list);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto* open = buttons->addButton(tr("Abrir destino"), QDialogButtonBox::ActionRole);
    auto* remove = buttons->addButton(tr("Eliminar enlace"), QDialogButtonBox::ActionRole);
    open->setObjectName(QStringLiteral("openPageLink"));
    remove->setObjectName(QStringLiteral("deletePageLink"));
    buttons->button(QDialogButtonBox::Close)->setText(tr("Cerrar"));
    layout->addWidget(buttons);
    const auto update = [this, page, list, links, open, remove] {
        const int row = list->currentRow();
        const bool valid = row >= 0 && row < links.size();
        open->setEnabled(valid && (links[row].targetPage >= 0 || !links[row].url.isEmpty()));
        remove->setEnabled(valid && links[row].editable);
        if (valid) { QPainterPath path; path.addRect(links[row].area); pageView_->showSearchMatch({page, path, {}, {}}); }
    };
    int action = 0;
    connect(list, &QListWidget::currentRowChanged, &dialog, update);
    connect(open, &QPushButton::clicked, &dialog, [&] { action = 1; dialog.accept(); });
    connect(remove, &QPushButton::clicked, &dialog, [&] { action = 2; dialog.accept(); });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    list->setCurrentRow(links.isEmpty() ? -1 : 0);
    update();
    dialog.resize(560, 320);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    pageView_->clearSearchMatch();
    const int row = list->currentRow();
    if (!accepted || row < 0 || row >= links.size()) { return; }
    const auto link = links[row];
    if (action == 2) {
        startOperation(Operation::Annotate, tr("Eliminando enlace…"), [document = document_, page, index = link.index] { return document->deleteLink(page, index); });
    } else if (action == 1 && link.targetPage >= 0) { pageView_->setPage(link.targetPage); }
    else if (action == 1 && !link.url.isEmpty() && QMessageBox::question(this, tr("Abrir página web"), tr("¿Abrir esta dirección en el navegador?\n%1").arg(link.url),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
        if (!QDesktopServices::openUrl(QUrl(link.url))) { statusBar()->showMessage(tr("No se pudo abrir el navegador."), 5000); }
    }
}

void MainWindow::splitDocument()
{
    if (!document_ || operation_ != Operation::Idle) { return; }
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("splitDialog"));
    dialog.setWindowTitle(tr("Dividir PDF"));
    dialog.resize(520, 240);
    auto* form = new QFormLayout(&dialog);
    auto* perPart = new QSpinBox(&dialog);
    perPart->setObjectName(QStringLiteral("splitPagesPerPart"));
    perPart->setRange(1, static_cast<int>(pageCount()));
    auto* directory = new QLineEdit(&dialog);
    directory->setObjectName(QStringLiteral("splitOutputDirectory"));
    directory->setPlaceholderText(tr("Ruta de una carpeta nueva"));
    auto* browse = new QPushButton(tr("Elegir ubicación…"), &dialog);
    connect(browse, &QPushButton::clicked, &dialog, [this, &dialog, directory] {
        const auto parent = QFileDialog::getExistingDirectory(&dialog, tr("Elegir carpeta contenedora"), QFileInfo(currentFilePath()).absolutePath());
        if (!parent.isEmpty()) { directory->setText(QDir(parent).filePath(QFileInfo(currentFilePath()).completeBaseName() + tr(" - dividido"))); }
    });
    auto* summary = new QLabel(&dialog);
    summary->setWordWrap(true);
    auto* description = new QLabel(tr("Se creará una carpeta nueva con todas las partes. Se conserva el documento abierto con sus cambios pendientes. Máximo 1000 archivos."), &dialog);
    description->setWordWrap(true);
    form->addRow(description);
    form->addRow(tr("Páginas por &archivo"), perPart);
    form->addRow(tr("Carpeta &nueva"), directory);
    form->addRow(browse);
    form->addRow(summary);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("splitApplyButton"));
    buttons->button(QDialogButtonBox::Save)->setText(tr("Dividir"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    const auto update = [this, perPart, directory, summary, buttons] {
        const auto parts = (pageCount() - 1) / perPart->value() + 1;
        summary->setText(tr("Se generarán %1 archivos; el último puede tener menos páginas.").arg(parts));
        buttons->button(QDialogButtonBox::Save)->setEnabled(parts <= 1000 && !directory->text().trimmed().isEmpty());
    };
    connect(perPart, &QSpinBox::valueChanged, &dialog, update);
    connect(directory, &QLineEdit::textChanged, &dialog, update);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    update();
    if (dialog.exec() != QDialog::Accepted) { return; }
    const auto destination = directory->text().trimmed();
    const auto document = document_;
    const auto count = perPart->value();
    startOutput(tr("Preparando los archivos divididos…"), tr("PDF dividido en «%1»").arg(destination),
        [document, count, destination] { return document->splitPages(count, destination); });
}

void MainWindow::extractPages()
{
    if (!document_ || operation_ != Operation::Idle) { return; }
    const int current = static_cast<int>(pageView_->currentPage()) + 1;
    const int count = static_cast<int>(pageCount());
    QDialog range(this);
    range.setWindowTitle(tr("Extraer páginas"));
    auto* layout = new QFormLayout(&range);
    auto* first = new QSpinBox(&range);
    first->setObjectName(QStringLiteral("extractFirstPage"));
    first->setRange(1, count);
    first->setValue(current);
    auto* last = new QSpinBox(&range);
    last->setObjectName(QStringLiteral("extractLastPage"));
    last->setRange(current, count);
    connect(first, &QSpinBox::valueChanged, last, &QSpinBox::setMinimum);
    layout->addRow(tr("&Desde la página"), first);
    layout->addRow(tr("&Hasta la página"), last);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &range);
    connect(buttons, &QDialogButtonBox::accepted, &range, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &range, &QDialog::reject);
    layout->addRow(buttons);
    if (range.exec() != QDialog::Accepted) { return; }

    const QFileInfo source(currentFilePath());
    const auto firstIndex = first->value() - 1;
    const auto lastIndex = last->value() - 1;
    const auto name = tr("%1 - extracción.pdf").arg(source.completeBaseName());
    const auto path = selectCopyPath(this, tr("Guardar páginas extraídas"), source, name);
    if (path.isEmpty()) { return; }
    startOperation(Operation::Extract, tr("Extrayendo páginas…"), [document = document_, firstIndex, lastIndex, path] {
        return document->extractPages(firstIndex, lastIndex, path);
    });
}

void MainWindow::insertFromPdf()
{
    if (!document_ || operation_ != Operation::Idle) { return; }
    const auto path = QFileDialog::getOpenFileName(this, tr("Elegir PDF de origen"),
        QFileInfo(currentFilePath()).absolutePath(), tr("Documentos PDF (*.pdf)"));
    if (path.isEmpty()) { return; }
    startOperation(Operation::LoadImport, tr("Leyendo PDF de origen…"), [path] { return PdfDocument::open(path); });
}

void MainWindow::configurePageImport(std::shared_ptr<const PdfDocument> source)
{
    if (!document_ || !source || operation_ != Operation::Idle) { return; }
    const auto currentPage = pageView_->currentPage();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("pageImportDialog"));
    dialog.setWindowTitle(tr("Insertar páginas de otro PDF"));
    dialog.resize(440, 260);
    auto* layout = new QFormLayout(&dialog);
    auto* name = new QLabel(QFileInfo(source->filePath()).fileName(), &dialog);
    name->setTextFormat(Qt::PlainText);
    name->setWordWrap(true);
    layout->addRow(tr("Origen"), name);
    auto* first = new QSpinBox(&dialog);
    first->setObjectName(QStringLiteral("importFirstPage"));
    first->setRange(1, static_cast<int>(source->pageCount()));
    auto* last = new QSpinBox(&dialog);
    last->setObjectName(QStringLiteral("importLastPage"));
    last->setRange(1, static_cast<int>(source->pageCount()));
    last->setValue(static_cast<int>(source->pageCount()));
    connect(first, &QSpinBox::valueChanged, last, &QSpinBox::setMinimum);
    auto* position = new QComboBox(&dialog);
    position->setObjectName(QStringLiteral("importPosition"));
    position->addItem(tr("Antes de la primera página"), 0);
    position->addItem(tr("Después de la página actual (%1)").arg(currentPage + 1), static_cast<int>(currentPage + 1));
    position->addItem(tr("Al final"), static_cast<int>(pageCount()));
    position->setCurrentIndex(1);
    layout->addRow(tr("&Desde la página"), first);
    layout->addRow(tr("&Hasta la página"), last);
    layout->addRow(tr("&Insertar"), position);
    auto* summary = new QLabel(tr("Se incorporarán el contenido y las anotaciones del intervalo elegido."), &dialog);
    summary->setWordWrap(true);
    layout->addRow(summary);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Insertar páginas"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    statusBar()->showMessage(tr("Elige el intervalo y la posición de inserción"));
    if (dialog.exec() != QDialog::Accepted) {
        statusBar()->showMessage(tr("Importación cancelada"));
        return;
    }
    const auto destination = position->currentData().toInt();
    const PdfPageImport selection{std::move(source), first->value() - 1, last->value() - 1};
    pageDestination_ = destination;
    startOperation(Operation::Pages, tr("Importando páginas…"), [document = document_, destination, selection] {
        return document->insertPages(destination, {selection});
    });
}

void MainWindow::mergePdfs()
{
    if (!document_ || operation_ != Operation::Idle) { return; }
    const auto paths = QFileDialog::getOpenFileNames(this, tr("Elegir los PDF que se unirán al final"),
        QFileInfo(currentFilePath()).absolutePath(), tr("Documentos PDF (*.pdf)"));
    if (paths.isEmpty()) { return; }
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("mergePdfsDialog"));
    dialog.setWindowTitle(tr("Orden de los PDF"));
    dialog.resize(500, 330);
    auto* layout = new QVBoxLayout(&dialog);
    auto* label = new QLabel(tr("Los archivos se incorporarán al final en este orden."), &dialog);
    label->setWordWrap(true);
    layout->addWidget(label);
    auto* files = new QListWidget(&dialog);
    files->setObjectName(QStringLiteral("mergeFileList"));
    files->setAccessibleName(tr("Orden de los archivos PDF"));
    files->setDragDropMode(QAbstractItemView::InternalMove);
    for (const auto& path : paths) {
        auto* item = new QListWidgetItem(QFileInfo(path).fileName(), files);
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
    }
    files->setCurrentRow(0);
    layout->addWidget(files);
    auto* order = new QHBoxLayout;
    auto* up = new QPushButton(tr("Subir archivo"), &dialog);
    auto* down = new QPushButton(tr("Bajar archivo"), &dialog);
    up->setObjectName(QStringLiteral("mergeFileUp"));
    down->setObjectName(QStringLiteral("mergeFileDown"));
    order->addWidget(up);
    order->addWidget(down);
    layout->addLayout(order);
    const auto updateOrder = [files, up, down] {
        up->setEnabled(files->currentRow() > 0);
        down->setEnabled(files->currentRow() >= 0 && files->currentRow() + 1 < files->count());
    };
    connect(files, &QListWidget::currentRowChanged, &dialog, updateOrder);
    connect(files->model(), &QAbstractItemModel::rowsMoved, &dialog, updateOrder);
    const auto move = [files, updateOrder](int offset) {
        const auto from = files->currentRow();
        const auto to = from + offset;
        if (from < 0 || to < 0 || to >= files->count()) { return; }
        files->insertItem(to, files->takeItem(from));
        files->setCurrentRow(to);
        updateOrder();
    };
    connect(up, &QPushButton::clicked, &dialog, [move] { move(-1); });
    connect(down, &QPushButton::clicked, &dialog, [move] { move(1); });
    updateOrder();
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Unir archivos"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) { return; }
    QStringList ordered;
    for (int index = 0; index < files->count(); ++index) { ordered.append(files->item(index)->data(Qt::UserRole).toString()); }
    pageDestination_ = pageCount();
    startOperation(Operation::Pages, tr("Leyendo e incorporando los PDF…"), [document = document_, ordered] {
        QList<PdfPageImport> sources;
        for (const auto& path : ordered) {
            auto opened = PdfDocument::open(path);
            if (!opened.document) {
                opened.error = tr("No se pudo incorporar «%1». %2").arg(QFileInfo(path).fileName(), opened.error);
                return opened;
            }
            sources.append({std::move(opened.document)});
        }
        return document->insertPages(document->pageCount(), sources);
    });
}

void MainWindow::saveAs()
{
    if (!document_ || operation_ != Operation::Idle) { return; }
    const QFileInfo source(currentFilePath());
    const auto path = selectCopyPath(this, tr("Guardar PDF como"), source, tr("%1 - copia.pdf").arg(source.completeBaseName()));
    if (!path.isEmpty()) { saveDocument(PdfSaveMode::NewCopy, path); }
}

void MainWindow::exportPage()
{
    if (!document_ || operation_ != Operation::Idle) { return; }
    const auto page = pageView_->currentPage();
    const QFileInfo source(currentFilePath());
    const auto name = tr("%1 - página %2.png").arg(source.completeBaseName()).arg(page + 1);
    const auto path = selectCopyPath(this, tr("Exportar página como PNG"), source, name, QStringLiteral("png"));
    if (path.isEmpty()) { return; }
    startOutput(tr("Exportando página…"), tr("PNG guardado en «%1»").arg(QFileInfo(path).fileName()),
        [document = document_, page, path] { return document->exportPng(page, path); });
}

void MainWindow::printPdf()
{
    if (!document_ || operation_ != Operation::Idle) { return; }
    if (document_->printResolutionLimit() == 0) {
        QMessageBox::information(this, tr("Imprimir"), tr("El PDF restringe la impresión."));
        return;
    }
    const auto printer = std::make_shared<QPrinter>(QPrinter::HighResolution);
    printer->setDocName(QFileInfo(currentFilePath()).fileName());
    QPrintDialog dialog(printer.get(), this);
    dialog.setMinMax(1, static_cast<int>(pageCount()));
    dialog.setOption(QPrintDialog::PrintPageRange);
    dialog.setOption(QPrintDialog::PrintCurrentPage);
    dialog.setOption(QPrintDialog::PrintSelection, false);
    if (dialog.exec() != QDialog::Accepted) { return; }
    const auto current = pageView_->currentPage();
    startOutput(tr("Enviando páginas a la impresora…"), tr("Trabajo de impresión enviado"),
        [document = document_, current, printer] { return printDocument(*document, current, *printer); });
}

void MainWindow::addNote(QPointF position)
{
    if (!document_ || operation_ != Operation::Idle || !addNoteAction_->isChecked()) { return; }
    addNoteAction_->setChecked(false);
    const auto page = pageView_->currentPage();
    const auto text = noteTextDialog(this, tr("Añadir nota"));
    if (text.action != NoteAction::Save) { return; }
    startOperation(Operation::Annotate, tr("Añadiendo nota…"), [document = document_, page, position, text = text.text] {
        return document->addNote(page, position, text);
    });
}

void MainWindow::showPageNotes()
{
    if (!document_ || operation_ != Operation::Idle) { return; }
    addNoteAction_->setChecked(false);
    const auto page = pageView_->currentPage();
    const auto notes = document_->pageNotes(page);
    if (notes.isEmpty()) {
        statusBar()->showMessage(tr("Esta página no tiene notas de texto."), 5000);
        return;
    }
    qsizetype selected = 0;
    if (notes.size() > 1) {
        QStringList items;
        for (const auto& note : notes) {
            const auto preview = note.text.simplified();
            const auto label = preview.size() <= 72 ? preview : preview.left(72) + QChar(0x2026);
            items.append(tr("%1. %2").arg(items.size() + 1).arg(label.isEmpty() ? tr("(sin texto)") : label));
        }
        bool accepted = false;
        const auto item = QInputDialog::getItem(this, tr("Notas de esta página"), tr("Selecciona una nota"),
            items, 0, false, &accepted);
        if (!accepted) { return; }
        selected = items.indexOf(item);
        if (selected < 0) { return; }
    }
    const auto& note = notes[selected];
    const auto title = note.editable ? tr("Editar nota") : tr("Nota de solo lectura");
    const auto text = noteTextDialog(this, title, note.text, note.editable, true);
    if (text.action == NoteAction::Delete) {
        const auto answer = QMessageBox::question(this, tr("Eliminar nota"),
            tr("¿Quieres eliminar esta nota? Puedes recuperar la nota con Deshacer."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer == QMessageBox::Yes) {
            startOperation(Operation::Annotate, tr("Eliminando nota…"), [document = document_, page, index = note.index] {
                return document->deleteNote(page, index);
            });
        }
        return;
    }
    if (text.action != NoteAction::Save || text.text == note.text) { return; }
    startOperation(Operation::Annotate, tr("Actualizando nota…"), [document = document_, page, index = note.index, text = text.text] {
        return document->editNote(page, index, text);
    });
}

void MainWindow::showPageTextMarks()
{
    if (!document_ || operation_ != Operation::Idle) { return; }
    addNoteAction_->setChecked(false);
    searchBar_->hide();
    const auto page = pageView_->currentPage();
    const auto marks = document_->pageTextMarks(page);
    if (marks.isEmpty()) {
        statusBar()->showMessage(tr("Esta página no tiene marcas de texto compatibles."), 5000);
        return;
    }
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("pageTextMarksDialog"));
    dialog.setWindowTitle(tr("Marcas de texto de esta página"));
    dialog.resize(450, 320);
    auto* layout = new QVBoxLayout(&dialog);
    auto* list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("pageTextMarkList"));
    list->setAccessibleName(tr("Marcas de texto"));
    for (const auto& mark : marks) {
        QString type;
        switch (mark.type) {
        case PdfTextMarkup::Highlight: type = tr("Resaltado"); break;
        case PdfTextMarkup::Underline: type = tr("Subrayado"); break;
        case PdfTextMarkup::StrikeOut: type = tr("Tachado"); break;
        case PdfTextMarkup::Squiggly: type = tr("Ondulado"); break;
        }
        const auto preview = mark.text.simplified();
        const auto text = preview.isEmpty() ? tr("(sin comentario)") : preview.left(100);
        auto* item = new QListWidgetItem(tr("%1. %2 · %3").arg(list->count() + 1).arg(type, text), list);
        item->setToolTip(mark.text);
    }
    layout->addWidget(list);
    auto* contents = new QPlainTextEdit(&dialog);
    contents->setReadOnly(true);
    contents->setAccessibleName(tr("Texto de la marca seleccionada"));
    layout->addWidget(contents);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto* remove = buttons->addButton(tr("Eliminar marca"), QDialogButtonBox::DestructiveRole);
    remove->setObjectName(QStringLiteral("deleteTextMarkButton"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(list, &QListWidget::currentRowChanged, &dialog, [&, contents, remove](int row) {
        const bool valid = row >= 0 && row < marks.size();
        remove->setEnabled(valid && marks[row].editable);
        contents->setPlainText(valid ? marks[row].text : QString{});
        if (valid) { pageView_->showSearchMatch({page, marks[row].area}); }
    });
    qsizetype selected = -1;
    connect(remove, &QPushButton::clicked, &dialog, [&] {
        const auto row = list->currentRow();
        if (row < 0 || row >= marks.size() || !marks[row].editable) { return; }
        const auto answer = QMessageBox::question(&dialog, tr("Eliminar marca"),
            tr("¿Quieres eliminar esta marca? Puedes recuperarla con Deshacer."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) { return; }
        selected = row;
        dialog.accept();
    });
    list->setCurrentRow(0);
    const auto result = dialog.exec();
    pageView_->clearSearchMatch();
    if (result != QDialog::Accepted || selected < 0) { return; }
    startOperation(Operation::Annotate, tr("Eliminando marca de texto…"),
        [document = document_, page, index = marks[selected].index] {
            return document->deleteTextMark(page, index);
        });
}

void MainWindow::createPageFormField()
{
    if (!document_ || isBusy()) { return; }
    addNoteAction_->setChecked(false);
    searchBar_->hide();
    const auto page = pageView_->currentPage();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("createFormFieldDialog"));
    dialog.setWindowTitle(tr("Crear campo de formulario"));
    auto* form = new QFormLayout(&dialog);
    auto* name = new QLineEdit(&dialog);
    name->setObjectName(QStringLiteral("fieldName"));
    name->setMaxLength(128);
    form->addRow(tr("Nombre único"), name);
    auto* type = new QComboBox(&dialog);
    type->setObjectName(QStringLiteral("fieldType"));
    type->addItems({tr("Texto"), tr("Casilla"), tr("Lista desplegable"), tr("Grupo de botones de opción"), tr("Lista de selección múltiple")});
    form->addRow(tr("Tipo"), type);
    auto* multiline = new QCheckBox(tr("Permitir varias líneas"), &dialog);
    multiline->setObjectName(QStringLiteral("fieldMultiline"));
    form->addRow(multiline);
    auto* options = new QPlainTextEdit(&dialog);
    options->setObjectName(QStringLiteral("fieldOptions"));
    options->setMaximumHeight(90);
    options->setPlaceholderText(tr("Una opción por línea, hasta 500 opciones distintas."));
    form->addRow(tr("Opciones (una por línea)"), options);
    const auto area = addAreaControls(form, &dialog, QStringLiteral("field"), QRectF(0.1, 0.15, 0.6, 0.12));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel, &dialog);
    auto* apply = buttons->button(QDialogButtonBox::Apply);
    apply->setObjectName(QStringLiteral("fieldApply"));
    apply->setText(tr("Crear"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    form->addRow(buttons);
    const auto values = [options] { return options->toPlainText().split('\n', Qt::SkipEmptyParts); };
    const auto update = [this, page, type, name, options, multiline, area, values, apply] {
        options->setEnabled(type->currentIndex() >= 2);
        multiline->setEnabled(type->currentIndex() == 0);
        const auto box = area.value();
        const bool geometry = box.isValid() && QRectF(0, 0, 1, 1).contains(box);
        const auto choices = values();
        bool validOptions = !choices.isEmpty() && choices.size() <= 500;
        QSet<QString> seen;
        for (const auto& value : choices) { validOptions = validOptions && !value.trimmed().isEmpty() && value.size() <= 256 && !seen.contains(value); seen.insert(value); }
        if (type->currentIndex() == 3) { validOptions = validOptions && choices.size() >= 2 && choices.size() <= 20 && box.height() * document_->pageSize(page).height() * .75 / choices.size() >= 12; }
        apply->setEnabled(geometry && !name->text().trimmed().isEmpty() && !name->text().contains('.') && (type->currentIndex() < 2 || validOptions));
        QPainterPath path;
        if (geometry) { path.addRect(box); }
        pageView_->showSearchMatch({page, path, {}, {}});
    };
    connect(type, &QComboBox::currentIndexChanged, &dialog, update);
    connect(name, &QLineEdit::textChanged, &dialog, update);
    connect(options, &QPlainTextEdit::textChanged, &dialog, update);
    for (auto* field : area.fields) { connect(field, &QDoubleSpinBox::valueChanged, &dialog, update); }
    connect(apply, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(470, 510);
    update();
    const bool accepted = dialog.exec() == QDialog::Accepted;
    pageView_->clearSearchMatch();
    if (!accepted) { return; }
    startOperation(Operation::Annotate, tr("Creando campo…"), [document = document_, page, box = area.value(), fieldName = name->text().trimmed(),
        kind = static_cast<PdfFormFieldType>(type->currentIndex()), choices = type->currentIndex() >= 2 ? values() : QStringList{}, multi = multiline->isChecked()] {
        return document->createFormField(page, box, fieldName, kind, choices, multi);
    });
}

void MainWindow::addPageShape()
{
    if (!document_ || isBusy()) { return; }
    addNoteAction_->setChecked(false);
    searchBar_->hide();
    const auto page = pageView_->currentPage();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("addShapeDialog"));
    dialog.setWindowTitle(tr("Añadir forma"));
    auto* form = new QFormLayout(&dialog);
    auto* type = new QComboBox(&dialog);
    type->setObjectName(QStringLiteral("shapeType"));
    type->addItems({tr("Rectángulo"), tr("Elipse"), tr("Línea"), tr("Flecha")});
    form->addRow(tr("Forma"), type);
    const auto makeColor = [&](const QString& name, const QColor& initial) {
        auto* button = new QPushButton(initial.name(), &dialog);
        button->setObjectName(name);
        button->setProperty("color", initial);
        connect(button, &QPushButton::clicked, &dialog, [button, &dialog] {
            const auto color = QColorDialog::getColor(button->property("color").value<QColor>(), &dialog, tr("Elegir color"));
            if (color.isValid()) { button->setProperty("color", color); button->setText(color.name()); }
        });
        return button;
    };
    auto* stroke = makeColor(QStringLiteral("shapeStroke"), QColor(200, 35, 35));
    auto* fill = makeColor(QStringLiteral("shapeFill"), QColor(255, 230, 160));
    form->addRow(tr("Color del trazo"), stroke);
    auto* filled = new QCheckBox(tr("Rellenar interior"), &dialog);
    filled->setObjectName(QStringLiteral("shapeFilled"));
    form->addRow(filled);
    form->addRow(tr("Color del relleno"), fill);
    fill->setEnabled(false);
    auto* width = new QDoubleSpinBox(&dialog);
    width->setObjectName(QStringLiteral("shapeLineWidth"));
    width->setRange(0.5, 20);
    width->setValue(2);
    width->setSuffix(tr(" pt"));
    form->addRow(tr("Grosor"), width);
    auto* text = new QLineEdit(&dialog);
    text->setObjectName(QStringLiteral("shapeComment"));
    text->setMaxLength(4096);
    form->addRow(tr("Comentario"), text);
    const auto area = addAreaControls(form, &dialog, QStringLiteral("shape"), QRectF(0.15, 0.2, 0.5, 0.3));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel, &dialog);
    auto* apply = buttons->button(QDialogButtonBox::Apply);
    apply->setObjectName(QStringLiteral("shapeApply"));
    apply->setText(tr("Añadir"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    form->addRow(buttons);
    const auto update = [this, page, type, fill, filled, area, apply] {
        filled->setEnabled(type->currentIndex() < 2);
        fill->setEnabled(type->currentIndex() < 2 && filled->isChecked());
        const auto box = area.value();
        const bool valid = box.isValid() && QRectF(0, 0, 1, 1).contains(box);
        apply->setEnabled(valid);
        QPainterPath path;
        if (valid) { path.addRect(box); }
        pageView_->showSearchMatch({page, path, {}, {}});
    };
    connect(type, &QComboBox::currentIndexChanged, &dialog, update);
    connect(filled, &QCheckBox::toggled, &dialog, update);
    for (auto* field : area.fields) { connect(field, &QDoubleSpinBox::valueChanged, &dialog, update); }
    connect(apply, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(470, 550);
    update();
    const bool accepted = dialog.exec() == QDialog::Accepted;
    pageView_->clearSearchMatch();
    if (!accepted) { return; }
    startOperation(Operation::Annotate, tr("Añadiendo forma…"), [document = document_, page, box = area.value(), kind = static_cast<PdfShapeType>(type->currentIndex()),
        color = stroke->property("color").value<QColor>(), inside = filled->isChecked() && type->currentIndex() < 2 ? fill->property("color").value<QColor>() : QColor{},
        points = width->value(), comment = text->text()] { return document->addShape(page, box, kind, color, inside, points, comment); });
}

void MainWindow::drawInk()
{
    if (!document_ || isBusy()) { return; }
    addNoteAction_->setChecked(false);
    searchBar_->hide();
    const auto page = pageView_->currentPage();
    InkDialog dialog(document_, page, this);
    if (dialog.exec() != QDialog::Accepted) { return; }
    startOperation(Operation::Annotate, tr("Insertando dibujo…"), [document = document_, page, strokes = dialog.strokes(), color = dialog.strokeColor(), width = dialog.strokeWidth()] {
        return document->addInk(page, strokes, color, width);
    });
}

void MainWindow::showPageShapes()
{
    if (!document_ || isBusy()) { return; }
    const auto page = pageView_->currentPage();
    const auto shapes = document_->pageShapes(page);
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("pageShapesDialog"));
    dialog.setWindowTitle(tr("Formas de esta página"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("pageShapesList"));
    const QStringList names{tr("Rectángulo"), tr("Elipse"), tr("Línea"), tr("Flecha"), tr("Dibujo a mano alzada")};
    for (const auto& shape : shapes) { list->addItem(names[static_cast<int>(shape.type)] + QStringLiteral(" · ") + shape.text.left(100)); }
    if (shapes.isEmpty()) { layout->addWidget(new QLabel(tr("Esta página no tiene formas compatibles."), &dialog)); }
    layout->addWidget(list);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto* remove = buttons->addButton(tr("Eliminar forma"), QDialogButtonBox::AcceptRole);
    remove->setObjectName(QStringLiteral("deleteShapeButton"));
    auto* style = buttons->addButton(tr("Cambiar estilo…"), QDialogButtonBox::ActionRole);
    style->setObjectName(QStringLiteral("restyleShapeButton"));
    bool styling = false;
    connect(style, &QPushButton::clicked, &dialog, [&] { styling = true; dialog.accept(); });
    buttons->button(QDialogButtonBox::Close)->setText(tr("Cerrar"));
    layout->addWidget(buttons);
    const auto update = [this, page, list, remove, style, shapes] {
        const auto row = list->currentRow();
        const bool valid = row >= 0 && row < shapes.size();
        remove->setEnabled(valid && shapes[row].editable);
        style->setEnabled(valid && shapes[row].editable);
        if (valid) { QPainterPath path; path.addRect(shapes[row].area); pageView_->showSearchMatch({page, path, {}, {}}); }
    };
    connect(list, &QListWidget::currentRowChanged, &dialog, update);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    list->setCurrentRow(shapes.isEmpty() ? -1 : 0);
    update();
    dialog.resize(480, 320);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    pageView_->clearSearchMatch();
    if (!accepted || list->currentRow() < 0) { return; }
    const auto index = shapes[list->currentRow()].index;
    if (styling) {
        QDialog editor(this); editor.setObjectName(QStringLiteral("shapeStyleDialog")); editor.setWindowTitle(tr("Cambiar estilo de la forma"));
        auto* form = new QFormLayout(&editor);
        auto* stroke = new QPushButton(QStringLiteral("#0000ff"), &editor); stroke->setObjectName(QStringLiteral("restyleStroke")); stroke->setProperty("color", QColor(Qt::blue));
        auto* fill = new QPushButton(QStringLiteral("#ffff00"), &editor); fill->setProperty("color", QColor(Qt::yellow));
        auto* filled = new QCheckBox(tr("Rellenar interior"), &editor); filled->setObjectName(QStringLiteral("restyleFilled"));
        const auto type = shapes[list->currentRow()].type;
        filled->setEnabled(type == PdfShapeType::Rectangle || type == PdfShapeType::Ellipse);
        fill->setEnabled(false); connect(filled, &QCheckBox::toggled, fill, &QPushButton::setEnabled);
        for (auto* button : {stroke, fill}) { connect(button, &QPushButton::clicked, &editor, [&, button] { const auto color = QColorDialog::getColor(button->property("color").value<QColor>(), &editor); if (color.isValid()) { button->setProperty("color", color); button->setText(color.name()); } }); }
        auto* width = new QDoubleSpinBox(&editor); width->setObjectName(QStringLiteral("restyleWidth")); width->setRange(.5, 20); width->setValue(2); width->setSuffix(tr(" pt"));
        form->addRow(tr("Color del trazo"), stroke); form->addRow(filled); form->addRow(tr("Relleno"), fill); form->addRow(tr("Grosor"), width);
        auto* applyButtons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel, &editor);
        applyButtons->button(QDialogButtonBox::Apply)->setObjectName(QStringLiteral("restyleApply"));
        connect(applyButtons->button(QDialogButtonBox::Apply), &QPushButton::clicked, &editor, &QDialog::accept); connect(applyButtons, &QDialogButtonBox::rejected, &editor, &QDialog::reject); form->addRow(applyButtons);
        if (editor.exec() != QDialog::Accepted) { return; }
        startOperation(Operation::Annotate, tr("Actualizando apariencia de la forma…"), [document = document_, page, index, color = stroke->property("color").value<QColor>(), inside = filled->isEnabled() && filled->isChecked() ? fill->property("color").value<QColor>() : QColor(), points = width->value()] { return document->restyleShape(page, index, color, inside, points); });
        return;
    }
    startOperation(Operation::Annotate, tr("Eliminando forma…"), [document = document_, page, index] { return document->deleteShape(page, index); });
}

void MainWindow::manageAttachments()
{
    if (!document_ || isBusy()) { return; }
    const auto attachments = document_->attachments();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("attachmentsDialog"));
    dialog.setWindowTitle(tr("Adjuntos del documento"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* hint = new QLabel(tr("Puedes incorporar, exportar o retirar archivos de la lista. Los adjuntos se exportan sin abrirlos. Retirarlos no es una herramienta de supresión de datos sensibles."), &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto* list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("attachmentsList"));
    for (const auto& attachment : attachments) {
        auto* item = new QListWidgetItem(attachment.name + (attachment.size >= 0 ? tr(" · %1 bytes").arg(attachment.size) : QString{}), list);
        item->setToolTip(attachment.description);
    }
    layout->addWidget(list);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto* add = buttons->addButton(tr("Añadir archivo…"), QDialogButtonBox::ActionRole);
    auto* exportFile = buttons->addButton(tr("Exportar…"), QDialogButtonBox::ActionRole);
    auto* remove = buttons->addButton(tr("Retirar"), QDialogButtonBox::ActionRole);
    add->setEnabled(!document_->isEncrypted());
    add->setObjectName(QStringLiteral("addAttachmentButton"));
    exportFile->setObjectName(QStringLiteral("exportAttachmentButton"));
    remove->setObjectName(QStringLiteral("removeAttachmentButton"));
    buttons->button(QDialogButtonBox::Close)->setText(tr("Cerrar"));
    layout->addWidget(buttons);
    const auto update = [this, list, exportFile, remove] { const bool selected = list->currentRow() >= 0;
        exportFile->setEnabled(selected && document_->canCopyText()); remove->setEnabled(selected && !document_->isEncrypted()); };
    connect(list, &QListWidget::currentRowChanged, &dialog, update);
    int command = 0;
    connect(add, &QPushButton::clicked, &dialog, [&] { command = 1; dialog.accept(); });
    connect(exportFile, &QPushButton::clicked, &dialog, [&] { command = 2; dialog.accept(); });
    connect(remove, &QPushButton::clicked, &dialog, [&] { command = 3; dialog.accept(); });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    list->setCurrentRow(attachments.isEmpty() ? -1 : 0);
    update();
    dialog.resize(620, 350);
    if (dialog.exec() != QDialog::Accepted) { return; }
    if (command == 1) {
        const auto path = QFileDialog::getOpenFileName(this, tr("Añadir archivo adjunto"));
        if (path.isEmpty()) { return; }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024) { QMessageBox::warning(this, tr("No se pudo añadir"), tr("Elige un archivo legible de hasta 64 MB.")); return; }
        const auto bytes = file.read(64 * 1024 * 1024 + 1);
        if (file.error() != QFileDevice::NoError || bytes.size() > 64 * 1024 * 1024) { return; }
        startOperation(Operation::Annotate, tr("Incorporando adjunto…"), [document = document_, name = QFileInfo(path).fileName(), bytes] { return document->addAttachment(name, bytes); });
        return;
    }
    if (list->currentRow() < 0 || list->currentRow() >= attachments.size()) { return; }
    const auto attachment = attachments[list->currentRow()];
    if (command == 2) {
        auto suggested = QFileInfo(attachment.name).fileName();
        if (suggested.isEmpty() || suggested == QStringLiteral(".") || suggested == QStringLiteral("..")) { suggested = QStringLiteral("adjunto.bin"); }
        const auto destination = QFileDialog::getSaveFileName(this, tr("Exportar adjunto a un archivo nuevo"), QFileInfo(currentFilePath()).dir().filePath(suggested));
        if (destination.isEmpty()) { return; }
        startOutput(tr("Exportando adjunto…"), tr("Adjunto exportado"), [document = document_, key = attachment.key, destination] { return document->exportAttachment(key, destination); });
    } else if (command == 3) {
        startOperation(Operation::Annotate, tr("Retirando adjunto…"), [document = document_, key = attachment.key] { return document->deleteAttachment(key); });
    }
}

void MainWindow::editPageContent()
{
    if (!document_ || isBusy() || document_->isEncrypted()) { return; }
    const auto page = pageView_->currentPage();
    ContentDialog dialog(document_, page, this);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedIndex() < 0) { return; }
    startOperation(Operation::Annotate, tr("Verificando y editando contenido…"), [document = document_, page, index = dialog.selectedIndex(), update = dialog.change()] { return document->editContent(page, index, update); });
}

void MainWindow::signDocumentCopy()
{
    if (!document_ || isBusy() || document_->isEncrypted()) { return; }
    QDialog dialog(this); dialog.setObjectName(QStringLiteral("signCopyDialog")); dialog.setWindowTitle(tr("Firmar una copia con certificado"));
    auto* form = new QFormLayout(&dialog);
    auto* hint = new QLabel(tr("Firma criptográfica invisible con un certificado PFX/P12 y su clave privada. Se incluyen los cambios pendientes en un archivo nuevo. El certificado y su contraseña no se guardan en la aplicación. La confianza del certificado se comprueba por separado; esta versión no incorpora sello de tiempo ni consulta de revocación en línea."), &dialog);
    hint->setWordWrap(true); form->addRow(hint);
    auto* certificate = new QLineEdit(&dialog); certificate->setObjectName(QStringLiteral("signCertificate")); certificate->setReadOnly(true);
    auto* choose = new QPushButton(tr("Elegir PFX/P12…"), &dialog); choose->setObjectName(QStringLiteral("signChooseCertificate"));
    form->addRow(tr("Certificado"), certificate); form->addRow(choose);
    auto* password = new QLineEdit(&dialog); password->setObjectName(QStringLiteral("signPassword")); password->setEchoMode(QLineEdit::Password); password->setMaxLength(1024);
    auto* reason = new QLineEdit(&dialog); reason->setObjectName(QStringLiteral("signReason")); reason->setMaxLength(1000);
    form->addRow(tr("Contraseña del certificado"), password); form->addRow(tr("Motivo (opcional)"), reason);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    auto* save = buttons->button(QDialogButtonBox::Save); save->setObjectName(QStringLiteral("signApply")); save->setText(tr("Elegir destino de la copia…")); save->setEnabled(false);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar")); form->addRow(buttons);
    connect(choose, &QPushButton::clicked, &dialog, [&] { const auto path = QFileDialog::getOpenFileName(&dialog, tr("Elegir certificado con clave privada"), {}, tr("Certificados (*.pfx *.p12)")); if (!path.isEmpty()) { certificate->setText(path); } });
    connect(certificate, &QLineEdit::textChanged, &dialog, [=] { save->setEnabled(QFileInfo(certificate->text()).isFile()); });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(580, 350); if (dialog.exec() != QDialog::Accepted) { return; }
    const auto path = certificate->text(); const auto secret = password->text(); password->clear(); const auto why = reason->text();
    const QFileInfo source(currentFilePath());
    const auto destination = selectCopyPath(this, tr("Guardar copia firmada"), source, source.completeBaseName() + tr(" - firmado"));
    if (destination.isEmpty()) { return; }
    startOutput(tr("Firmando y verificando la copia…"), tr("Copia firmada guardada. Ábrela para verificar integridad y confianza."),
        [document = document_, destination, path, secret, why] { return document->signedCopy(destination, path, secret, why); });
}

void MainWindow::showSignatures()
{
    if (!document_ || isBusy() || isWindowModified()) { return; }
    QDialog dialog(this); dialog.setObjectName(QStringLiteral("signatureReportDialog")); dialog.setWindowTitle(tr("Verificar firmas digitales"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* text = new QPlainTextEdit(&dialog); text->setObjectName(QStringLiteral("signatureReport")); text->setReadOnly(true); text->setPlainText(tr("Verificando el archivo guardado…")); layout->addWidget(text);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); layout->addWidget(buttons);
    auto* watcher = new QFutureWatcher<PdfSignatureReport>(&dialog);
    connect(watcher, &QFutureWatcher<PdfSignatureReport>::finished, &dialog, [watcher, text] {
        const auto result = watcher->result(); if (!result.error.isEmpty()) { text->setPlainText(result.error); return; }
        QString report = tr("Verificación local del archivo guardado. Integridad y confianza son resultados distintos. No se consulta revocación en línea.\n\n");
        if (result.signatures.isEmpty()) { report += tr("No se encontraron firmas digitales AcroForm compatibles.\n"); }
        for (const auto& signature : result.signatures) {
            report += tr("Campo: %1\nIntegridad criptográfica: %2\nCertificado: %3\nCobertura del archivo completo: %4\n").arg(signature.name,
                signature.integrityValid ? tr("Correcta") : tr("No verificada o inválida"), signature.certificateTrusted ? tr("Cadena y vigencia verificadas") : tr("Confianza o vigencia no verificadas"), signature.coversWholeFile ? tr("Sí") : tr("No"));
            if (!signature.errors.isEmpty()) { report += tr("Detalles: %1\n").arg(signature.errors.join(QStringLiteral("; "))); }
            report += tr("Algoritmos: %1\n").arg(signature.algorithms.join(QStringLiteral(", ")));
            if (!signature.warnings.isEmpty()) { report += tr("Avisos: %1\n").arg(signature.warnings.join(QStringLiteral("; "))); }
            report += u'\n';
        }
        text->setPlainText(report); text->setProperty("verificationFinished", true);
    });
    watcher->setFuture(QtConcurrent::run([document = document_] { return document->verifySignatures(); }));
    dialog.resize(720, 560); dialog.exec();
}

void MainWindow::exportRedactedCopy(bool localized)
{
    if (!document_ || isBusy() || document_->isEncrypted() || document_->pageCount() > 50) { return; }
    RedactionDialog dialog(document_, pageView_->currentPage(), this, localized);
    if (dialog.exec() != QDialog::Accepted || dialog.areas().isEmpty()) { return; }
    const QFileInfo source(currentFilePath());
    const auto destination = selectCopyPath(this, tr("Guardar copia con contenido eliminado"), source, source.completeBaseName() + tr(" - contenido eliminado"));
    if (destination.isEmpty()) { return; }
    startOutput(tr("Reconstruyendo y verificando la copia…"), tr("Copia creada. Revisa el resultado antes de compartirlo."),
        [document = document_, destination, areas = dialog.areas(), dpi = dialog.resolution(), localized] { return localized ? document->localizedRedactedCopy(destination, areas) : document->redactedCopy(destination, areas, dpi); });
}

void MainWindow::recognizeText()
{
    if (!document_ || isBusy() || document_->isEncrypted()) { return; }
    OcrDialog dialog(document_, pageView_->currentPage(), this);
    if (dialog.exec() != QDialog::Accepted || dialog.pages().isEmpty()) { return; }
    startOperation(Operation::Annotate, tr("Añadiendo capa de texto OCR…"), [document = document_, pages = dialog.pages()] { return document->addOcrLayer(pages); });
}

void MainWindow::exportProtectedCopy()
{
    if (!document_ || isBusy() || document_->isEncrypted()) { return; }
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("protectPdfDialog"));
    dialog.setWindowTitle(tr("Crear copia protegida con AES-256"));
    auto* form = new QFormLayout(&dialog);
    auto* hint = new QLabel(tr("Se crea un archivo nuevo y el original sigue abierto. La contraseña de apertura permite leerlo; la de permisos permite administrarlo en lectores compatibles. Usa dos contraseñas distintas."), &dialog);
    hint->setWordWrap(true);
    form->addRow(hint);
    QLineEdit* passwords[4]{};
    const char* names[] = {"protectUserPassword", "protectUserConfirm", "protectOwnerPassword", "protectOwnerConfirm"};
    const QString labels[] = {tr("Contraseña de apertura"), tr("Repetir apertura"), tr("Contraseña de permisos"), tr("Repetir permisos")};
    for (int i = 0; i < 4; ++i) {
        passwords[i] = new QLineEdit(&dialog);
        passwords[i]->setObjectName(QString::fromLatin1(names[i]));
        passwords[i]->setEchoMode(QLineEdit::Password);
        passwords[i]->setMaxLength(64);
        form->addRow(labels[i], passwords[i]);
    }
    auto* copy = new QCheckBox(tr("Permitir copiar contenido"), &dialog);
    auto* print = new QCheckBox(tr("Permitir imprimir"), &dialog);
    copy->setObjectName(QStringLiteral("protectAllowCopy"));
    print->setObjectName(QStringLiteral("protectAllowPrint"));
    copy->setChecked(true); print->setChecked(true);
    form->addRow(copy); form->addRow(print);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    auto* save = buttons->button(QDialogButtonBox::Save);
    save->setObjectName(QStringLiteral("protectApply"));
    save->setText(tr("Elegir destino…"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    form->addRow(buttons);
    const auto update = [passwords, save] {
        const auto user = passwords[0]->text();
        const auto owner = passwords[2]->text();
        save->setEnabled(user.size() >= 8 && owner.size() >= 8 && user.toUtf8().size() <= 127 && owner.toUtf8().size() <= 127 &&
            user == passwords[1]->text() && owner == passwords[3]->text() && user != owner);
    };
    for (auto* password : passwords) { connect(password, &QLineEdit::textChanged, &dialog, update); }
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    update();
    dialog.resize(520, 360);
    if (dialog.exec() != QDialog::Accepted) { return; }
    const auto user = passwords[0]->text();
    const auto owner = passwords[2]->text();
    for (auto* password : passwords) { password->clear(); }
    const QFileInfo source(currentFilePath());
    const auto destination = selectCopyPath(this, tr("Guardar copia protegida"), source, source.completeBaseName() + tr(" - protegido"));
    if (destination.isEmpty()) { return; }
    startOutput(tr("Creando y verificando copia protegida…"), tr("Copia protegida guardada"), [document = document_, destination, user, owner, allowCopy = copy->isChecked(), allowPrint = print->isChecked()] {
        return document->protectedCopy(destination, user, owner, allowCopy, allowPrint);
    });
}

void MainWindow::exportOptimizedCopy()
{
    if (!document_ || isBusy()) { return; }
    const QFileInfo source(currentFilePath());
    const auto destination = selectCopyPath(this, tr("Guardar copia optimizada sin reducir imágenes"), source, source.completeBaseName() + tr(" - optimizado"));
    if (destination.isEmpty()) { return; }
    startOutput(tr("Optimizando copia…"), tr("Copia optimizada guardada. El tamaño final depende del documento."), [document = document_, destination] {
        auto result = document->optimizedCopy(destination);
        return PdfOutputResult{result.error, result.warnings};
    });
}

void MainWindow::fillPageForm()
{
    if (!document_ || operation_ != Operation::Idle) { return; }
    addNoteAction_->setChecked(false);
    const auto page = pageView_->currentPage();
    const auto fields = document_->pageFormFields(page);
    if (fields.isEmpty()) {
        statusBar()->showMessage(tr("Esta página no tiene campos de texto o casillas compatibles."), 5000);
        return;
    }
    qsizetype selected = 0;
    if (fields.size() > 1) {
        QStringList items;
        for (const auto& field : fields) {
            items.append(tr("%1. %2").arg(items.size() + 1).arg(field.name.simplified()));
        }
        bool accepted = false;
        const auto item = QInputDialog::getItem(this, tr("Campos de esta página"), tr("Selecciona un campo"),
            items, 0, false, &accepted);
        if (!accepted) { return; }
        selected = items.indexOf(item);
        if (selected < 0) { return; }
    }
    const auto& field = fields[selected];
    const auto value = formValueDialog(this, field);
    if (!value || *value == field.value) { return; }
    startOperation(Operation::Annotate, tr("Actualizando formulario…"),
        [document = document_, page, index = field.index, value = *value] {
            return document->setFormField(page, index, value);
        });
}

void MainWindow::saveDocument(PdfSaveMode mode, const QString& copyPath, std::function<void()> afterSave, std::function<void()> failed)
{
    if (!document_ || !isWindowModified() || operation_ != Operation::Idle) { return; }
    afterSave_ = std::move(afterSave);
    afterSaveFailed_ = std::move(failed);
    startOperation(Operation::Save, tr("Guardando PDF…"), [document = document_, mode, copyPath] {
        return document->save(mode, copyPath);
    });
}

void MainWindow::confirmPendingChanges(std::function<void()> proceed, std::function<void()> cancelled)
{
    if (!isWindowModified()) {
        proceed();
        return;
    }
    QMessageBox dialog(QMessageBox::Question, tr("Cambios pendientes"),
        tr("¿Quieres guardar los cambios en «%1»?").arg(QFileInfo(currentFilePath()).fileName()),
        QMessageBox::NoButton, this);
    dialog.setTextFormat(Qt::PlainText);
    auto* save = dialog.addButton(tr("Guardar"), QMessageBox::AcceptRole);
    auto* discard = dialog.addButton(tr("Descartar"), QMessageBox::DestructiveRole);
    auto* cancel = dialog.addButton(tr("Cancelar"), QMessageBox::RejectRole);
    dialog.setDefaultButton(save);
    dialog.setEscapeButton(cancel);
    dialog.exec();
    if (dialog.clickedButton() == save) { saveDocument(PdfSaveMode::Original, {}, std::move(proceed), std::move(cancelled)); }
    else if (dialog.clickedButton() == discard) { proceed(); }
    else if (cancelled) { cancelled(); }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (operation_ != Operation::Idle) {
        event->ignore();
        return;
    }
    if (isWindowModified()) {
        event->ignore();
        confirmPendingChanges([this] {
            clearDocument();
            QMetaObject::invokeMethod(this, [this] { close(); }, Qt::QueuedConnection);
        });
        return;
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::closeDocument()
{
    if (operation_ != Operation::Idle) { return; }
    if (workspaceClose_) { workspaceClose_(); return; }
    confirmPendingChanges([this] { clearDocument(); });
}

void MainWindow::clearDocument()
{
    addNoteAction_->setChecked(false);
    document_.reset();
    savedDocument_.reset();
    undoHistory_.clear();
    redoHistory_.clear();
    setWindowModified(false);
    error_->hide();
    renderMessage_ = false;
    updateDocumentView();
}

void MainWindow::updateSelectionActions()
{
    const bool selected = document_ && operation_ == Operation::Idle && document_->canCopyText()
        && !pageView_->textSelection().text.isEmpty();
    copyTextAction_->setEnabled(selected);
    for (auto* action : selectionMarkActions_) { action->setEnabled(selected && !document_->isEncrypted()); }
}

void MainWindow::updateBookmarkActions()
{
    const bool available = document_ && !document_->isEncrypted() && operation_ == Operation::Idle;
    auto* selected = bookmarks_->currentItem();
    const bool hasSelection = available && selected && !selected->data(0, Qt::UserRole + 1).toList().isEmpty();
    addBookmarkAction_->setEnabled(available);
    for (auto* action : {addChildBookmarkAction_, renameBookmarkAction_, retargetBookmarkAction_, deleteBookmarkAction_}) { action->setEnabled(hasSelection); }
    const auto index = selected ? (selected->parent() ? selected->parent()->indexOfChild(selected)
        : bookmarks_->indexOfTopLevelItem(selected)) : -1;
    const auto siblings = selected && selected->parent() ? selected->parent()->childCount() : bookmarks_->topLevelItemCount();
    bookmarkUpAction_->setEnabled(hasSelection && index > 0);
    bookmarkDownAction_->setEnabled(hasSelection && index + 1 < siblings);
    indentBookmarkAction_->setEnabled(hasSelection && index > 0);
    outdentBookmarkAction_->setEnabled(hasSelection && selected->parent());
}

void MainWindow::changeSelectedBookmark(PdfBookmarkChange change, bool asChild)
{
    if (!document_ || operation_ != Operation::Idle) { return; }
    QList<qsizetype> path;
    QString title;
    qsizetype page = pageView_->currentPage();
    const auto* selected = bookmarks_->currentItem();
    if (change != PdfBookmarkChange::Add || asChild) {
        if (!selected) { return; }
        const auto indices = selected->data(0, Qt::UserRole + 1).toList();
        if (indices.isEmpty()) { return; }
        for (const auto& index : indices) { path.append(index.toLongLong()); }
        title = selected->text(0);
        const auto target = selected->data(0, Qt::UserRole).toLongLong();
        if (change == PdfBookmarkChange::Retarget && target >= 0) { page = target; }
    }
    if (change == PdfBookmarkChange::Add || change == PdfBookmarkChange::Rename) {
        bool accepted = false;
        title = QInputDialog::getText(this, change == PdfBookmarkChange::Add ? tr("Añadir marcador") : tr("Cambiar nombre del marcador"),
            tr("Nombre del marcador"), QLineEdit::Normal,
            change == PdfBookmarkChange::Add ? tr("Página %1").arg(page + 1) : title, &accepted).trimmed();
        if (!accepted || title.isEmpty()) { return; }
    } else if (change == PdfBookmarkChange::Retarget) {
        bool accepted = false;
        page = QInputDialog::getInt(this, tr("Cambiar página de destino"), tr("Página de destino"),
            static_cast<int>(page + 1), 1, static_cast<int>(pageCount()), 1, &accepted) - 1;
        if (!accepted) { return; }
    } else if (change == PdfBookmarkChange::Delete) {
        QMessageBox confirmation(QMessageBox::Question, tr("Eliminar marcador"),
            tr("¿Eliminar «%1» y todos sus submarcadores? Las páginas del PDF se conservan.").arg(title),
            QMessageBox::Yes | QMessageBox::No, this);
        confirmation.setTextFormat(Qt::PlainText);
        confirmation.setDefaultButton(QMessageBox::No);
        if (confirmation.exec() != QMessageBox::Yes) { return; }
    }
    requestedBookmarkPath_.clear();
    auto destinationPath = path;
    if (change == PdfBookmarkChange::Add) {
        destinationPath.append(asChild ? selected->childCount() : (bookmarks_->topLevelItem(0)->data(0, Qt::UserRole + 1).toList().isEmpty() ? 0 : bookmarks_->topLevelItemCount()));
    } else if (change == PdfBookmarkChange::MoveUp || change == PdfBookmarkChange::MoveDown) {
        destinationPath.back() += change == PdfBookmarkChange::MoveUp ? -1 : 1;
    } else if (change == PdfBookmarkChange::Indent) {
        const auto index = destinationPath.takeLast();
        const auto* previous = selected->parent() ? selected->parent()->child(static_cast<int>(index - 1)) : bookmarks_->topLevelItem(static_cast<int>(index - 1));
        destinationPath.append(index - 1);
        destinationPath.append(previous->childCount());
    } else if (change == PdfBookmarkChange::Outdent) {
        destinationPath.removeLast();
        ++destinationPath.back();
    }
    if (change != PdfBookmarkChange::Delete) {
        for (const auto index : destinationPath) { requestedBookmarkPath_.append(static_cast<qlonglong>(index)); }
    }
    const auto document = document_;
    if (auto* tabs = findChild<QTabWidget*>(QStringLiteral("navigationTabs"))) { tabs->setCurrentWidget(bookmarks_); }
    startOperation(Operation::Annotate, tr("Actualizando marcadores…"), [document, change, path, title, page] {
        return document->changeBookmark(change, path, title, page);
    });
}

void MainWindow::updateBookmarks()
{
    const auto selectedPath = !requestedBookmarkPath_.isEmpty() ? std::exchange(requestedBookmarkPath_, QVariantList{})
        : (bookmarks_->currentItem() ? bookmarks_->currentItem()->data(0, Qt::UserRole + 1).toList() : QVariantList{});
    const QSignalBlocker blocker(bookmarks_);
    bookmarks_->clear();
    QList<QTreeWidgetItem*> parents;
    if (document_) {
        for (const auto& bookmark : document_->bookmarks()) {
            while (parents.size() > bookmark.depth) { parents.removeLast(); }
            auto* item = new QTreeWidgetItem;
            item->setText(0, bookmark.title.isEmpty() ? tr("Sin título") : bookmark.title);
            item->setData(0, Qt::UserRole, static_cast<qlonglong>(bookmark.page));
            QVariantList path;
            for (const auto index : bookmark.path) { path.append(static_cast<qlonglong>(index)); }
            item->setData(0, Qt::UserRole + 1, path);
            item->setToolTip(0, bookmark.page >= 0 ? tr("Página %1").arg(bookmark.page + 1)
                : tr("Sin destino de página compatible"));
            if (parents.isEmpty()) { bookmarks_->addTopLevelItem(item); }
            else { parents.back()->addChild(item); }
            parents.append(item);
            if (path == selectedPath) { bookmarks_->setCurrentItem(item); }
        }
    }
    if (bookmarks_->topLevelItemCount() == 0) {
        auto* empty = new QTreeWidgetItem(bookmarks_, {tr("Sin marcadores")});
        empty->setData(0, Qt::UserRole, -1);
        empty->setFlags(Qt::NoItemFlags);
    }
    bookmarks_->expandToDepth(0);
    if (auto* selected = bookmarks_->currentItem()) {
        for (auto* parent = selected->parent(); parent; parent = parent->parent()) { parent->setExpanded(true); }
        bookmarks_->scrollToItem(selected);
    }
    updateBookmarkActions();
}

void MainWindow::updateDocumentActions()
{
    const bool idle = operation_ == Operation::Idle;
    openAction_->setEnabled(idle);
    closeAction_->setEnabled(document_ && idle);
    exportAction_->setEnabled(document_ && idle && document_->canCopyText());
    printAction_->setEnabled(document_ && idle && document_->printResolutionLimit() > 0);
    searchAction_->setEnabled(document_ && idle);
    undoAction_->setEnabled(idle && !undoHistory_.isEmpty());
    redoAction_->setEnabled(idle && !redoHistory_.isEmpty());
    rotateAction_->setEnabled(document_ && idle);
    extractPagesAction_->setEnabled(document_ && idle);
    splitAction_->setEnabled(document_ && idle);
    insertTextAction_->setEnabled(document_ && idle);
    insertImageAction_->setEnabled(document_ && idle);
    numberPagesAction_->setEnabled(document_ && idle);
    watermarkAction_->setEnabled(document_ && idle);
    propertiesAction_->setEnabled(document_ && idle);
    addLinkAction_->setEnabled(document_ && idle);
    pageLinksAction_->setEnabled(document_ && idle);
    cropPagesAction_->setEnabled(document_ && idle);
    resetCropAction_->setEnabled(document_ && idle);
    duplicatePageAction_->setEnabled(document_ && idle);
    insertBlankPageAction_->setEnabled(document_ && idle);
    insertPdfAction_->setEnabled(document_ && idle);
    mergePdfsAction_->setEnabled(document_ && idle);
    deletePageAction_->setEnabled(document_ && idle && pageCount() > 1);
    addNoteAction_->setEnabled(document_ && idle);
    pageNotesAction_->setEnabled(document_ && idle);
    pageTextMarksAction_->setEnabled(document_ && idle);
    fillFormAction_->setEnabled(document_ && idle);
    for (auto* action : {createFormAction_, addShapeAction_, drawInkAction_, pageShapesAction_, attachmentsAction_, optimizeAction_}) { action->setEnabled(document_ && idle); }
    protectAction_->setEnabled(document_ && idle && !document_->isEncrypted());
    ocrAction_->setEnabled(document_ && idle && !document_->isEncrypted());
    redactAction_->setEnabled(document_ && idle && !document_->isEncrypted() && document_->pageCount() <= 50);
    localizedRedactAction_->setEnabled(document_ && idle && !document_->isEncrypted() && document_->pageCount() <= 50);
    accessibleReaderAction_->setEnabled(document_ && idle && document_->canCopyText());
    contentEditAction_->setEnabled(document_ && idle && !document_->isEncrypted());
    signAction_->setEnabled(document_ && idle && !document_->isEncrypted());
    verifySignaturesAction_->setEnabled(document_ && idle && !isWindowModified());
    if (document_ && document_->isEncrypted()) {
        for (auto* action : {rotateAction_, extractPagesAction_, splitAction_, insertTextAction_, insertImageAction_, numberPagesAction_, watermarkAction_,
                 propertiesAction_, addLinkAction_, cropPagesAction_, resetCropAction_, duplicatePageAction_, insertBlankPageAction_, insertPdfAction_, mergePdfsAction_,
                 deletePageAction_, addNoteAction_, pageNotesAction_, pageTextMarksAction_, pageShapesAction_, fillFormAction_, createFormAction_, addShapeAction_, drawInkAction_, optimizeAction_}) { action->setEnabled(false); }
    }
    searchBar_->setEditingEnabled(document_ && idle && !document_->isEncrypted());
    selectTextAction_->setEnabled(document_ && idle && document_->canCopyText());
    pageView_->setTextSelectionEnabled(selectTextAction_->isEnabled() && selectTextAction_->isChecked());
    bookmarks_->setEnabled(document_ && idle);
    updateBookmarkActions();
    updateSelectionActions();
    saveAction_->setEnabled(document_ && idle && isWindowModified());
    saveAsAction_->setEnabled(saveAction_->isEnabled());
    pendingLabel_->setVisible(isWindowModified());
    updateMoveActions();
    emit workspaceStateChanged();
}

void MainWindow::updateMoveActions()
{
    const auto page = pageView_->currentPage();
    const bool canMove = document_ && !document_->isEncrypted() && operation_ == Operation::Idle && page >= 0;
    movePageUp_->setEnabled(canMove && page > 0);
    movePageDown_->setEnabled(canMove && page < pageCount() - 1);
}

void MainWindow::updateDocumentView(bool keepView)
{
    updateDocumentActions();
    viewTools_->setEnabled(static_cast<bool>(document_));
    thumbnails_->setDocument(document_);
    pageView_->setDocument(document_, keepView);
    searchBar_->setDocument(document_);
    updateBookmarks();
    updateNavigation();
    if (document_) {
        const auto fileName = QFileInfo(document_->filePath()).fileName();
        setWindowTitle(tr("%1[*] — PDF Editor").arg(fileName));
        title_->setText(fileName);
        const auto pages = document_->pageCount() == 1
            ? tr("1 página") : tr("%1 páginas").arg(document_->pageCount());
        content_->setCurrentWidget(documentPane_);
        statusBar()->showMessage(tr("Documento cargado · %1").arg(pages));
    } else {
        content_->setCurrentIndex(0);
        setWindowTitle(tr("PDF Editor"));
        title_->setText(tr("PDF Editor"));
        description_->setText(tr("Abre un documento desde Archivo → Abrir PDF o con Ctrl+O."));
        statusBar()->showMessage(tr("Sin documento abierto"));
    }
}

void MainWindow::updateNavigation()
{
    const auto count = static_cast<int>(pageCount());
    const auto current = static_cast<int>(pageView_->currentPage()) + 1;
    const QSignalBlocker blockPageNumber(pageNumber_);
    pageNumber_->setRange(count > 0 ? 1 : 0, count);
    pageNumber_->setSuffix(count > 0 ? tr(" / %1").arg(count) : QString{});
    pageNumber_->setValue(current);
    pageNumber_->setEnabled(count > 1);
    previousPage_->setEnabled(current > 1);
    nextPage_->setEnabled(current < count);
    thumbnails_->setCurrentPage(current - 1);
    updateMoveActions();
}

} // namespace pdfeditor
