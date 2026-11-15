#pragma once

#include "core/PdfDocument.h"
#include <QDialog>

class QDoubleSpinBox;
class QPushButton;
class QSpinBox;
template<typename T> class QFutureWatcher;

namespace pdfeditor {
class CropPreview;

class CropDialog final : public QDialog
{
public:
    CropDialog(std::shared_ptr<const PdfDocument> document, qsizetype page, bool reset, QWidget* parent = nullptr);
    qsizetype firstPage() const;
    qsizetype lastPage() const;
    QMarginsF margins() const;

private:
    void updateCrop();
    void renderPreview();
    std::shared_ptr<const PdfDocument> document_;
    QSpinBox* first_ = nullptr;
    QSpinBox* last_ = nullptr;
    QDoubleSpinBox* margins_[4]{};
    QPushButton* apply_ = nullptr;
    CropPreview* preview_ = nullptr;
    QFutureWatcher<PdfRenderResult>* watcher_ = nullptr;
    qsizetype activePage_ = -1;
    bool ready_ = false;
    bool reset_ = false;
};
} // namespace pdfeditor
