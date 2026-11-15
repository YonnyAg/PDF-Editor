#include "RedactionDialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <algorithm>

namespace pdfeditor {
class RedactionPreview final : public QWidget
{
public:
    using QWidget::QWidget;
    std::function<void(QRectF)> selected;
    QImage image;
    QList<QRectF> areas;
    QRectF candidate;
    QString message;
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().dark());
        if (image.isNull()) {
            painter.setPen(palette().color(QPalette::WindowText));
            painter.drawText(rect().adjusted(12, 12, -12, -12), Qt::AlignCenter | Qt::TextWordWrap, message);
            return;
        }
        const auto page = pageRect();
        painter.drawImage(page, image);
        for (const auto& area : areas) { painter.fillRect(map(area, page), Qt::black); }
        painter.setPen(QPen(QColor(30, 140, 255), 2, Qt::DashLine));
        painter.drawRect(map(candidate, page));
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        dragging_ = event->button() == Qt::LeftButton && !image.isNull() && pageRect().contains(event->position());
        if (dragging_) { anchor_ = normalized(event->position()); }
    }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!dragging_) { return; }
        candidate = QRectF(anchor_, normalized(event->position())).normalized();
        if (selected && candidate.width() >= 0.001 && candidate.height() >= 0.001) { selected(candidate); }
        update();
    }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && dragging_) { mouseMoveEvent(event); dragging_ = false; }
    }
private:
    QRectF pageRect() const
    {
        auto size = QSizeF(image.size());
        size.scale(QSizeF(width() - 24, height() - 24), Qt::KeepAspectRatio);
        return {QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size};
    }
    QPointF normalized(QPointF point) const
    {
        const auto page = pageRect();
        return {std::clamp((point.x() - page.left()) / page.width(), 0.0, 1.0), std::clamp((point.y() - page.top()) / page.height(), 0.0, 1.0)};
    }
    static QRectF map(const QRectF& area, const QRectF& page)
    {
        return {page.left() + area.left() * page.width(), page.top() + area.top() * page.height(), area.width() * page.width(), area.height() * page.height()};
    }
    bool dragging_ = false;
    QPointF anchor_;
};

RedactionDialog::RedactionDialog(std::shared_ptr<const PdfDocument> document, qsizetype page, QWidget* parent, bool localized)
    : QDialog(parent), document_(std::move(document))
{
    setObjectName(QStringLiteral("redactionDialog"));
    setWindowTitle(localized ? tr("Eliminar objetos conservando el resto del contenido") : tr("Eliminar contenido en una copia"));
    auto* layout = new QVBoxLayout(this);
    auto* explanation = new QLabel(localized ? tr("Se elimina COMPLETO cada objeto que toque una zona negra, aunque parte quede fuera de ella. Los demás textos y gráficos se conservan. Esta modalidad admite páginas simples con fuentes estándar, sin anotaciones ni campos; elimina metadatos, marcadores y adjuntos de la copia. Revisa el resultado antes de compartirlo.") : tr("Se reconstruyen TODAS las páginas como imágenes y se borran los píxeles de las zonas negras. La copia pierde texto buscable, formularios, enlaces, marcadores, adjuntos y metadatos originales. El original se conserva. Revisa cada zona y la copia final antes de compartirla."), this);
    explanation->setWordWrap(true); layout->addWidget(explanation);
    auto* form = new QFormLayout;
    page_ = new QSpinBox(this); page_->setObjectName(QStringLiteral("redactionPage"));
    page_->setRange(1, static_cast<int>(document_->pageCount())); page_->setValue(static_cast<int>(page + 1));
    form->addRow(tr("Página"), page_);
    const char* names[] = {"redactionX", "redactionY", "redactionWidth", "redactionHeight"};
    const QString labels[] = {tr("Izquierda"), tr("Desde arriba"), tr("Ancho"), tr("Alto")};
    const double defaults[] = {10, 10, 30, 15};
    for (int i = 0; i < 4; ++i) {
        box_[i] = new QDoubleSpinBox(this); box_[i]->setObjectName(QString::fromLatin1(names[i]));
        box_[i]->setRange(i < 2 ? 0 : 0.1, 100); box_[i]->setDecimals(1); box_[i]->setSuffix(QStringLiteral(" %")); box_[i]->setValue(defaults[i]);
        form->addRow(labels[i], box_[i]);
        connect(box_[i], &QDoubleSpinBox::valueChanged, this, [this] { updateControls(); });
    }
    dpi_ = new QComboBox(this); dpi_->setObjectName(QStringLiteral("redactionResolution"));
    for (int dpi : {150, 200, 300}) { dpi_->addItem(tr("%1 ppp").arg(dpi), dpi); }
    form->addRow(tr("Resolución de toda la copia"), dpi_);
    if (localized) { form->setRowVisible(dpi_, false); }
    layout->addLayout(form);
    preview_ = new RedactionPreview(this); preview_->setObjectName(QStringLiteral("redactionPreview"));
    preview_->setAccessibleName(tr("Vista previa. Arrastra para delimitar una zona y pulsa Añadir zona."));
    preview_->setMinimumSize(300, 230); layout->addWidget(preview_, 1);
    preview_->selected = [this](QRectF area) {
        const double values[] = {area.x() * 100, area.y() * 100, area.width() * 100, area.height() * 100};
        for (int i = 0; i < 4; ++i) { box_[i]->setValue(values[i]); }
    };
    add_ = new QPushButton(tr("Añadir zona negra"), this); add_->setObjectName(QStringLiteral("redactionAdd")); layout->addWidget(add_);
    list_ = new QListWidget(this); list_->setObjectName(QStringLiteral("redactionAreas")); list_->setMaximumHeight(85);
    list_->setAccessibleName(tr("Zonas que se eliminarán de la copia")); layout->addWidget(list_);
    auto* remove = new QPushButton(tr("Quitar zona seleccionada"), this); remove->setObjectName(QStringLiteral("redactionRemove"));
    remove->setEnabled(false); layout->addWidget(remove);
    consent_ = new QCheckBox(localized ? tr("Entiendo que se eliminan objetos completos y he revisado las zonas.") : tr("Entiendo que toda la copia se convierte en imágenes y he revisado las zonas."), this);
    consent_->setObjectName(QStringLiteral("redactionConsent")); layout->addWidget(consent_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    apply_ = buttons->button(QDialogButtonBox::Save); apply_->setObjectName(QStringLiteral("redactionApply")); apply_->setText(tr("Elegir destino de la copia…"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar")); layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(consent_, &QCheckBox::toggled, this, [this] { updateControls(); });
    connect(add_, &QPushButton::clicked, this, [this] { areas_.append({page_->value() - 1, candidate()}); consent_->setChecked(false); updateAreas(); });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (list_->currentRow() < 0) { return; }
        areas_.removeAt(list_->currentRow()); consent_->setChecked(false); updateAreas();
    });
    connect(list_, &QListWidget::currentRowChanged, this, [this, remove](int row) { remove->setEnabled(row >= 0 && row < areas_.size()); });
    connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem*) {
        if (list_->currentRow() >= 0) { page_->setValue(static_cast<int>(areas_[list_->currentRow()].page + 1)); }
    });
    watcher_ = new QFutureWatcher<PdfRenderResult>(this);
    connect(watcher_, &QFutureWatcher<PdfRenderResult>::finished, this, [this] {
        if (renderedPage_ != page_->value() - 1) { renderPreview(); return; }
        const auto result = watcher_->result(); ready_ = !result.image.isNull() && result.warnings.isEmpty();
        preview_->image = ready_ ? result.image : QImage();
        preview_->message = ready_ ? QString() : tr("No se pudo mostrar íntegramente esta página.");
        updateControls();
    });
    connect(page_, &QSpinBox::valueChanged, this, [this] { renderPreview(); updateControls(); });
    resize(660, 870); renderPreview(); updateControls();
}
QRectF RedactionDialog::candidate() const { return {box_[0]->value() / 100, box_[1]->value() / 100, box_[2]->value() / 100, box_[3]->value() / 100}; }
int RedactionDialog::resolution() const { return dpi_->currentData().toInt(); }
void RedactionDialog::updateControls()
{
    preview_->candidate = candidate(); preview_->areas.clear();
    for (const auto& area : areas_) { if (area.page == page_->value() - 1) { preview_->areas.append(area.area); } }
    add_->setEnabled(ready_ && QRectF(0, 0, 1, 1).contains(candidate()) && areas_.size() < 100);
    apply_->setEnabled(ready_ && !areas_.isEmpty() && consent_->isChecked()); preview_->update();
}
void RedactionDialog::updateAreas()
{
    list_->clear();
    for (const auto& area : areas_) {
        list_->addItem(tr("Página %1 — izquierda %2 %, arriba %3 %, ancho %4 %, alto %5 %").arg(area.page + 1)
            .arg(area.area.x() * 100, 0, 'f', 1).arg(area.area.y() * 100, 0, 'f', 1).arg(area.area.width() * 100, 0, 'f', 1).arg(area.area.height() * 100, 0, 'f', 1));
    }
    updateControls();
}
void RedactionDialog::renderPreview()
{
    ready_ = false; preview_->image = {}; preview_->message = tr("Preparando vista previa…"); preview_->update(); updateControls();
    if (watcher_->isRunning()) { return; }
    renderedPage_ = page_->value() - 1;
    watcher_->setFuture(QtConcurrent::run([document = document_, page = renderedPage_] {
        const auto size = document->pageSize(page);
        return document->renderPage(page, std::min({1.0, 650.0 / size.width(), 440.0 / size.height()}));
    }));
}
}
