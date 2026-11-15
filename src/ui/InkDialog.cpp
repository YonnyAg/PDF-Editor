#include "InkDialog.h"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include <algorithm>

namespace pdfeditor {
class InkCanvas final : public QWidget
{
public:
    using QWidget::QWidget;
    QImage image;
    QList<QPolygonF> strokes;
    QPolygonF current;
    QColor color = QColor(200, 35, 35);
    qreal normalizedWidth = 0.005;
    QString error;
    std::function<void()> changed;

    QRectF pageRect() const
    {
        if (image.isNull()) { return {}; }
        auto size = QSizeF(image.size());
        size.scale(QSizeF(std::max(1, width() - 20), std::max(1, height() - 20)), Qt::KeepAspectRatio);
        return QRectF(QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size);
    }

    void clearDrawing() { strokes.clear(); current.clear(); error.clear(); update(); if (changed) { changed(); } }
    void undoStroke() { current.clear(); if (!strokes.isEmpty()) { strokes.removeLast(); } error.clear(); update(); if (changed) { changed(); } }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().brush(QPalette::Dark));
        const auto box = pageRect();
        if (box.isEmpty()) { return; }
        painter.drawImage(box, image);
        painter.setClipRect(box);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(color, std::max(0.5, normalizedWidth * box.width()), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const auto draw = [&](const QPolygonF& normalized) {
            QPolygonF polygon;
            for (const auto& point : normalized) { polygon.append(box.topLeft() + QPointF(point.x() * box.width(), point.y() * box.height())); }
            painter.drawPolyline(polygon);
        };
        for (const auto& stroke : strokes) { draw(stroke); }
        draw(current);
    }

    QPointF normalizedPoint(QPointF point) const
    {
        const auto box = pageRect();
        return {std::clamp((point.x() - box.left()) / box.width(), 0.0, 1.0), std::clamp((point.y() - box.top()) / box.height(), 0.0, 1.0)};
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || image.isNull() || !pageRect().contains(event->position())) { return; }
        if (strokes.size() >= 32) { error = tr("Máximo 32 trazos. Deshaz uno para seguir dibujando."); if (changed) { changed(); } return; }
        error.clear();
        current = QPolygonF{normalizedPoint(event->position())};
        update();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (current.isEmpty()) { return; }
        const auto point = normalizedPoint(event->position());
        if (point == current.back()) { return; }
        qsizetype count = current.size();
        for (const auto& stroke : strokes) { count += stroke.size(); }
        if (count >= 10000) {
            current.clear(); error = tr("Se descartó el último trazo al superar 10000 puntos. Deshaz trazos para continuar.");
            update(); if (changed) { changed(); } return;
        }
        current.append(point);
        update();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || current.isEmpty()) { return; }
        mouseMoveEvent(event);
        if (current.size() >= 2) { strokes.append(current); }
        current.clear();
        update();
        if (changed) { changed(); }
    }
};

InkDialog::InkDialog(std::shared_ptr<const PdfDocument> document, qsizetype page, QWidget* parent)
    : QDialog(parent), canvas_(new InkCanvas(this)), color_(new QPushButton(this)), width_(new QDoubleSpinBox(this))
{
    setObjectName(QStringLiteral("inkDialog"));
    setWindowTitle(tr("Dibujo a mano alzada"));
    auto* layout = new QVBoxLayout(this);
    auto* hint = new QLabel(tr("Cargando página…"), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto* tools = new QHBoxLayout;
    color_->setObjectName(QStringLiteral("inkColor"));
    color_->setProperty("color", canvas_->color);
    color_->setText(tr("Color: %1…").arg(canvas_->color.name()));
    tools->addWidget(color_);
    width_->setObjectName(QStringLiteral("inkLineWidth"));
    width_->setRange(0.5, 20);
    width_->setValue(2);
    width_->setSuffix(tr(" pt"));
    width_->setAccessibleName(tr("Grosor del trazo"));
    tools->addWidget(width_);
    auto* undo = new QPushButton(tr("Deshacer trazo"), this);
    undo->setObjectName(QStringLiteral("inkUndo"));
    auto* clear = new QPushButton(tr("Borrar dibujo"), this);
    clear->setObjectName(QStringLiteral("inkClear"));
    tools->addWidget(undo); tools->addWidget(clear);
    layout->addLayout(tools);
    canvas_->setObjectName(QStringLiteral("inkCanvas"));
    canvas_->setAccessibleName(tr("Dibuja sobre la página manteniendo pulsado el botón izquierdo"));
    canvas_->setMinimumSize(360, 300);
    canvas_->setCursor(Qt::CrossCursor);
    layout->addWidget(canvas_, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    auto* apply = buttons->button(QDialogButtonBox::Apply);
    apply->setObjectName(QStringLiteral("inkApply"));
    apply->setText(tr("Insertar dibujo"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    layout->addWidget(buttons);
    const auto update = [this, apply, undo, clear, hint, logicalWidth = document->pageSize(page).width()] {
        canvas_->color = color_->property("color").value<QColor>();
        canvas_->normalizedWidth = width_->value() * (96.0 / 72.0) / std::max(1.0, logicalWidth);
        apply->setEnabled(!canvas_->image.isNull() && !canvas_->strokes.isEmpty());
        undo->setEnabled(!canvas_->strokes.isEmpty());
        clear->setEnabled(!canvas_->strokes.isEmpty());
        if (!canvas_->image.isNull()) { hint->setText(canvas_->error.isEmpty() ? tr("Dibuja sobre la página. %1 de 32 trazos. Todo el dibujo se insertará como una sola edición.").arg(canvas_->strokes.size()) : canvas_->error); }
        canvas_->update();
    };
    canvas_->changed = update;
    connect(color_, &QPushButton::clicked, this, [this, update] {
        const auto color = QColorDialog::getColor(strokeColor(), this, tr("Color del dibujo"));
        if (color.isValid()) { color_->setProperty("color", color); color_->setText(tr("Color: %1…").arg(color.name())); update(); }
    });
    connect(width_, &QDoubleSpinBox::valueChanged, this, update);
    connect(undo, &QPushButton::clicked, this, [this] { canvas_->undoStroke(); });
    connect(clear, &QPushButton::clicked, this, [this] { canvas_->clearDrawing(); });
    connect(apply, &QPushButton::clicked, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* watcher = new QFutureWatcher<PdfRenderResult>(this);
    connect(watcher, &QFutureWatcher<PdfRenderResult>::finished, this, [this, watcher, hint, update] {
        const auto result = watcher->result();
        canvas_->image = result.image;
        canvas_->setProperty("previewReady", !result.image.isNull());
        if (result.image.isNull()) { hint->setText(result.error); }
        update();
    });
    update();
    resize(760, 680);
    const auto size = document->pageSize(page);
    const auto zoom = std::min({1.0, 1000 / std::max(1.0, size.width()), 800 / std::max(1.0, size.height())});
    watcher->setFuture(QtConcurrent::run([document, page, zoom] { return document->renderPage(page, zoom); }));
}

QList<QPolygonF> InkDialog::strokes() const { return canvas_->strokes; }
QColor InkDialog::strokeColor() const { return color_->property("color").value<QColor>(); }
qreal InkDialog::strokeWidth() const { return width_->value(); }
} // namespace pdfeditor
