#pragma once
#include "core/PdfDocument.h"
#include <QDialog>

namespace pdfeditor {
class OcrDialog final : public QDialog
{
public:
    OcrDialog(std::shared_ptr<const PdfDocument> document, qsizetype currentPage, QWidget* parent = nullptr);
    QList<PdfOcrPage> pages() const { return pages_; }
private:
    QList<PdfOcrPage> pages_;
};
} // namespace pdfeditor
