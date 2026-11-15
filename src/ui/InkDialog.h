#pragma once

#include "core/PdfDocument.h"
#include <QDialog>

class QDoubleSpinBox;
class QPushButton;

namespace pdfeditor {
class InkCanvas;
class InkDialog final : public QDialog
{
public:
    InkDialog(std::shared_ptr<const PdfDocument> document, qsizetype page, QWidget* parent = nullptr);
    QList<QPolygonF> strokes() const;
    QColor strokeColor() const;
    qreal strokeWidth() const;
private:
    InkCanvas* canvas_;
    QPushButton* color_;
    QDoubleSpinBox* width_;
};
} // namespace pdfeditor
