#pragma once
#include "core/PdfDocument.h"
#include <QDialog>
namespace pdfeditor {
class ContentDialog final : public QDialog
{
public:
    ContentDialog(std::shared_ptr<const PdfDocument> document, qsizetype page, QWidget* parent = nullptr);
    qsizetype selectedIndex() const { return index_; }
    const PdfContentUpdate& change() const { return change_; }
private:
    qsizetype index_ = -1;
    PdfContentUpdate change_;
};
}
