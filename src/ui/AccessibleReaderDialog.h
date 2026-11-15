#pragma once
#include "core/PdfDocument.h"
#include <QDialog>
namespace pdfeditor {
class AccessibleReaderDialog final : public QDialog
{
public:
    AccessibleReaderDialog(std::shared_ptr<const PdfDocument> document, qsizetype page, QWidget* parent = nullptr);
};
}
