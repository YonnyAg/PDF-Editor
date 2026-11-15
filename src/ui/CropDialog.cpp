#include "CropDialog.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <algorithm>

namespace pdfeditor {

class CropPreview final : public QWidget
{
public:
    using QWidget::QWidget;
    void setImage(QImage image) { image_ = std::move(image); update(); }
    void setArea(QRectF area) { area_ = area; update(); }
    void setMessage(QString text) { message_ = std::move(text); image_ = {}; update(); }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().dark());
        if (image_.isNull()) {
            painter.setPen(palette().color(QPalette::WindowText));
            painter.drawText(rect().adjusted(12, 12, -12, -12), Qt::AlignCenter | Qt::TextWordWrap, message_);
            return;
        }
        auto size = QSizeF(image_.size());
        size.scale(QSizeF(width() - 24, height() - 24), Qt::KeepAspectRatio);
        const QRectF page(QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size);
        painter.drawImage(page, image_);
        const QRectF kept(page.left() + area_.left() * page.width(), page.top() + area_.top() * page.height(),
            area_.width() * page.width(), area_.height() * page.height());
        QPainterPath shade;
        shade.setFillRule(Qt::OddEvenFill);
        shade.addRect(page);
        if (kept.isValid()) { shade.addRect(kept); }
        painter.fillPath(shade, QColor(0, 0, 0, 125));
        if (kept.isValid()) {
            painter.setPen(QPen(QColor(40, 140, 255), 2, Qt::DashLine));
            painter.drawRect(kept);
        }
    }
private:
    QImage image_;
    QRectF area_{0, 0, 1, 1};
    QString message_;
};

CropDialog::CropDialog(std::shared_ptr<const PdfDocument> document, qsizetype page, bool reset, QWidget* parent)
    : QDialog(parent), document_(std::move(document)), reset_(reset)
{
    setObjectName(QStringLiteral("cropDialog"));
    setWindowTitle(reset ? tr("Restaurar área completa") : tr("Recortar páginas"));
    auto* layout = new QVBoxLayout(this);
    auto* explanation = new QLabel(reset
        ? tr("Se mostrará el área completa definida por el tamaño de cada página (MediaBox).")
        : tr("Los márgenes se miden como porcentaje del área visible actual. El recorte oculta contenido, pero no lo elimina del PDF."), this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    auto* form = new QFormLayout;
    first_ = new QSpinBox(this);
    last_ = new QSpinBox(this);
    first_->setObjectName(QStringLiteral("cropFirstPage"));
    last_->setObjectName(QStringLiteral("cropLastPage"));
    first_->setRange(1, static_cast<int>(document_->pageCount()));
    first_->setValue(static_cast<int>(page + 1));
    last_->setRange(first_->value(), static_cast<int>(document_->pageCount()));
    last_->setValue(first_->value());
    first_->setKeyboardTracking(false);
    last_->setKeyboardTracking(false);
    connect(first_, &QSpinBox::valueChanged, last_, &QSpinBox::setMinimum);
    form->addRow(tr("&Desde la página"), first_);
    form->addRow(tr("&Hasta la página"), last_);
    if (!reset) {
        const char* names[] = {"cropLeft", "cropTop", "cropRight", "cropBottom"};
        const QString labels[] = {tr("Margen &izquierdo"), tr("Margen &superior"), tr("Margen &derecho"), tr("Margen i&nferior")};
        for (int i = 0; i < 4; ++i) {
            auto* spin = new QDoubleSpinBox(this);
            margins_[i] = spin;
            spin->setObjectName(QString::fromLatin1(names[i]));
            spin->setRange(0, 99.9);
            spin->setDecimals(1);
            spin->setSuffix(QStringLiteral(" %"));
            spin->setValue(5);
            form->addRow(labels[i], spin);
            connect(spin, &QDoubleSpinBox::valueChanged, this, [this] { updateCrop(); });
        }
    }
    layout->addLayout(form);
    if (!reset) {
        preview_ = new CropPreview(this);
        preview_->setObjectName(QStringLiteral("cropPreview"));
        preview_->setAccessibleName(tr("Vista previa del recorte de la primera página del intervalo"));
        preview_->setMinimumSize(300, 240);
        layout->addWidget(preview_, 1);
        auto* hint = new QLabel(tr("Vista previa de la primera página del intervalo. Se conservará el área delimitada en azul."), this);
        hint->setWordWrap(true);
        layout->addWidget(hint);
        watcher_ = new QFutureWatcher<PdfRenderResult>(this);
        connect(watcher_, &QFutureWatcher<PdfRenderResult>::finished, this, [this] {
            if (activePage_ != firstPage()) { renderPreview(); return; }
            const auto result = watcher_->result();
            ready_ = !result.image.isNull();
            if (ready_) { preview_->setImage(result.image); }
            else { preview_->setMessage(result.error); }
            updateCrop();
        });
        connect(first_, &QSpinBox::valueChanged, this, [this] { renderPreview(); });
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    apply_ = buttons->button(QDialogButtonBox::Apply);
    apply_->setObjectName(QStringLiteral("cropApplyButton"));
    apply_->setText(reset ? tr("Restaurar") : tr("Recortar"));
    apply_->setDefault(true);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    connect(apply_, &QPushButton::clicked, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(500, reset ? 220 : 670);
    if (!reset) { renderPreview(); }
}

qsizetype CropDialog::firstPage() const { return first_->value() - 1; }
qsizetype CropDialog::lastPage() const { return last_->value() - 1; }
QMarginsF CropDialog::margins() const
{
    return reset_ ? QMarginsF{} : QMarginsF(margins_[0]->value() / 100, margins_[1]->value() / 100,
        margins_[2]->value() / 100, margins_[3]->value() / 100);
}

void CropDialog::updateCrop()
{
    const auto value = margins();
    const QRectF area(value.left(), value.top(), 1 - value.left() - value.right(), 1 - value.top() - value.bottom());
    preview_->setArea(area);
    apply_->setEnabled(ready_ && area.isValid() && !value.isNull());
}

void CropDialog::renderPreview()
{
    ready_ = false;
    preview_->setMessage(tr("Preparando vista previa…"));
    updateCrop();
    if (watcher_->isRunning()) { return; }
    activePage_ = firstPage();
    const auto document = document_;
    const auto page = activePage_;
    watcher_->setFuture(QtConcurrent::run([document, page] {
        const auto size = document->pageSize(page);
        const auto zoom = size.isEmpty() ? 1.0 : std::min({1.0, 600.0 / size.width(), 440.0 / size.height()});
        return document->renderPage(page, zoom);
    }));
}
} // namespace pdfeditor
