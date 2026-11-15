#include "ContentDialog.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <algorithm>

namespace pdfeditor {
namespace {
struct LoadedContent { PdfContentList content; PdfRenderResult preview; };
class ContentPreview final : public QWidget
{
public:
    using QWidget::QWidget;
    QImage image;
    QRectF selected;
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this); painter.fillRect(rect(), palette().dark());
        if (image.isNull()) { return; }
        auto size = QSizeF(image.size()); size.scale(QSizeF(width() - 16, height() - 16), Qt::KeepAspectRatio);
        const QRectF page(QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size);
        painter.drawImage(page, image);
        painter.setPen(QPen(QColor(30, 130, 255), 2, Qt::DashLine));
        painter.drawRect(QRectF(page.x() + selected.x() * size.width(), page.y() + selected.y() * size.height(), selected.width() * size.width(), selected.height() * size.height()));
    }
};
}
ContentDialog::ContentDialog(std::shared_ptr<const PdfDocument> document, qsizetype page, QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("contentDialog")); setWindowTitle(tr("Editar contenido de esta página"));
    auto* layout = new QVBoxLayout(this);
    auto* hint = new QLabel(tr("Selecciona un bloque existente o insertado. Puedes sustituir texto simple usando su fuente original, cambiar una imagen, transformar o eliminar un objeto y ajustar trazados. Se rechazan páginas que no puedan reconstruirse fielmente. Aplica un cambio por vez; puedes deshacerlo. Eliminar un objeto aquí no es una herramienta de supresión de datos sensibles."), this);
    hint->setWordWrap(true); layout->addWidget(hint);
    auto* status = new QLabel(tr("Leyendo objetos…"), this); status->setObjectName(QStringLiteral("contentStatus")); status->setWordWrap(true); layout->addWidget(status);
    auto* list = new QListWidget(this); list->setObjectName(QStringLiteral("contentItems")); list->setMaximumHeight(110); layout->addWidget(list);
    auto* preview = new ContentPreview(this); preview->setObjectName(QStringLiteral("contentPreview")); preview->setAccessibleName(tr("Objeto actual delimitado en azul"));
    preview->setMinimumSize(320, 200); layout->addWidget(preview, 1);
    auto* form = new QFormLayout;
    auto* operation = new QComboBox(this); operation->setObjectName(QStringLiteral("contentOperation")); form->addRow(tr("Operación"), operation);
    auto* text = new QLineEdit(this); text->setObjectName(QStringLiteral("contentReplacementText")); text->setMaxLength(1000); form->addRow(tr("Texto nuevo"), text);
    auto* imageButton = new QPushButton(tr("Elegir imagen nueva…"), this); imageButton->setObjectName(QStringLiteral("contentReplacementImage")); form->addRow(tr("Imagen nueva"), imageButton);
    auto replacement = std::make_shared<QImage>();
    auto* dx = new QDoubleSpinBox(this); auto* dy = new QDoubleSpinBox(this); auto* scale = new QDoubleSpinBox(this); auto* rotation = new QDoubleSpinBox(this);
    dx->setObjectName(QStringLiteral("contentOffsetX")); dy->setObjectName(QStringLiteral("contentOffsetY")); scale->setObjectName(QStringLiteral("contentScale")); rotation->setObjectName(QStringLiteral("contentRotation"));
    for (auto* spin : {dx, dy}) { spin->setRange(-100, 100); spin->setSuffix(QStringLiteral(" %")); spin->setDecimals(1); }
    scale->setRange(10, 400); scale->setValue(100); scale->setSuffix(QStringLiteral(" %"));
    rotation->setRange(-180, 180); rotation->setSuffix(QStringLiteral(" °"));
    form->addRow(tr("Mover horizontalmente"), dx); form->addRow(tr("Mover verticalmente"), dy); form->addRow(tr("Escala desde el centro"), scale); form->addRow(tr("Giro desde el centro"), rotation);
    auto* stroke = new QComboBox(this); auto* fill = new QComboBox(this);
    stroke->setObjectName(QStringLiteral("contentStroke")); fill->setObjectName(QStringLiteral("contentFill"));
    fill->addItem(tr("Sin relleno"), QColor(Qt::transparent));
    for (const auto& color : {QColor(Qt::black), QColor(Qt::blue), QColor(Qt::red), QColor(Qt::darkGreen), QColor(Qt::yellow), QColor(Qt::white)}) {
        stroke->addItem(color.name(), color); fill->addItem(color.name(), color);
    }
    auto* width = new QDoubleSpinBox(this); width->setObjectName(QStringLiteral("contentWidth")); width->setRange(.5, 20); width->setValue(2); width->setSuffix(tr(" pt"));
    form->addRow(tr("Color del borde"), stroke); form->addRow(tr("Relleno"), fill); form->addRow(tr("Grosor"), width);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    auto* apply = buttons->button(QDialogButtonBox::Apply); apply->setObjectName(QStringLiteral("contentApply")); apply->setText(tr("Aplicar cambio")); apply->setEnabled(false);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar")); layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto items = std::make_shared<QList<PdfContentItem>>();
    auto update = [=] {
        const auto kind = static_cast<PdfContentChange>(operation->currentData().toInt());
        form->setRowVisible(text, kind == PdfContentChange::ReplaceText);
        form->setRowVisible(imageButton, kind == PdfContentChange::ReplaceImage);
        for (auto* spin : {dx, dy, scale, rotation}) { form->setRowVisible(spin, kind == PdfContentChange::Transform); }
        form->setRowVisible(stroke, kind == PdfContentChange::PathStyle); form->setRowVisible(fill, kind == PdfContentChange::PathStyle); form->setRowVisible(width, kind == PdfContentChange::PathStyle);
        apply->setEnabled(list->currentRow() >= 0 && operation->count() > 0 && (kind != PdfContentChange::ReplaceText || !text->text().trimmed().isEmpty()) && (kind != PdfContentChange::ReplaceImage || !replacement->isNull()));
    };
    connect(operation, &QComboBox::currentIndexChanged, this, update);
    connect(text, &QLineEdit::textChanged, this, update);
    connect(imageButton, &QPushButton::clicked, this, [this, replacement, imageButton, update] {
        const auto path = QFileDialog::getOpenFileName(this, tr("Sustituir imagen"), {}, tr("Imágenes (*.png *.jpg *.jpeg *.bmp)"));
        if (path.isEmpty()) { return; }
        QImageReader reader(path); const auto size = reader.size();
        if (size.isEmpty() || size.width() > 8192 || size.height() > 8192 || qint64(size.width()) * size.height() > 16000000) { imageButton->setText(tr("Imagen no admitida. Elige otra…")); *replacement = {}; update(); return; }
        *replacement = reader.read(); imageButton->setText(replacement->isNull() ? tr("No se pudo leer. Elige otra…") : tr("Imagen lista (%1 × %2)").arg(replacement->width()).arg(replacement->height())); update();
    });
    connect(list, &QListWidget::currentRowChanged, this, [=](int row) {
        operation->clear();
        if (row < 0 || row >= items->size()) { update(); return; }
        const auto& item = items->at(row); preview->selected = item.area; preview->update(); text->setText(item.text);
        if (item.type == PdfContentType::Text && item.replaceable) { operation->addItem(tr("Sustituir texto"), static_cast<int>(PdfContentChange::ReplaceText)); }
        if (item.type == PdfContentType::Image) { operation->addItem(tr("Sustituir imagen (ajustada a su caja)"), static_cast<int>(PdfContentChange::ReplaceImage)); }
        operation->addItem(tr("Mover, escalar o girar"), static_cast<int>(PdfContentChange::Transform));
        if (item.type == PdfContentType::Path) { operation->addItem(tr("Cambiar colores y grosor"), static_cast<int>(PdfContentChange::PathStyle)); }
        operation->addItem(tr("Eliminar objeto"), static_cast<int>(PdfContentChange::Delete)); update();
    });
    connect(apply, &QPushButton::clicked, this, [=, this] {
        const int row = list->currentRow(); if (row < 0 || row >= items->size()) { return; }
        index_ = items->at(row).index; change_.change = static_cast<PdfContentChange>(operation->currentData().toInt());
        change_.text = text->text(); change_.image = *replacement; change_.offset = {dx->value() / 100, dy->value() / 100}; change_.scale = scale->value() / 100; change_.rotation = rotation->value();
        change_.stroke = stroke->currentData().value<QColor>(); change_.fill = fill->currentData().value<QColor>(); change_.width = width->value(); accept();
    });
    auto* watcher = new QFutureWatcher<LoadedContent>(this);
    connect(watcher, &QFutureWatcher<LoadedContent>::finished, this, [=] {
        const auto result = watcher->result();
        if (!result.content.error.isEmpty() || result.preview.image.isNull()) { status->setText(result.content.error.isEmpty() ? tr("No se pudo preparar la vista previa.") : result.content.error); return; }
        *items = result.content.items; preview->image = result.preview.image; preview->update();
        for (const auto& item : *items) {
            list->addItem(item.type == PdfContentType::Text ? tr("Texto: %1").arg(item.text.left(100)) : item.type == PdfContentType::Image ? tr("Imagen #%1").arg(item.index + 1) : tr("Trazado #%1").arg(item.index + 1));
        }
        status->setText(tr("%1 objetos visibles. La fuente original debe contener los caracteres nuevos. La vista previa muestra el objeto actual.").arg(items->size()));
        if (!items->isEmpty()) { list->setCurrentRow(0); } update();
    });
    watcher->setFuture(QtConcurrent::run([document, page] {
        const auto size = document->pageSize(page);
        return LoadedContent{document->pageContent(page), document->renderPage(page, std::min({1.0, 650.0 / size.width(), 380.0 / size.height()}))};
    }));
    update(); resize(690, 800);
}
}
