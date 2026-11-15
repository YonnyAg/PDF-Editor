#pragma once
#include "core/PdfDocument.h"
#include <QDialog>

class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class QListWidget;
class QCheckBox;
class QPushButton;
template<typename T> class QFutureWatcher;
namespace pdfeditor {
class RedactionPreview;
class RedactionDialog final : public QDialog
{
public:
    RedactionDialog(std::shared_ptr<const PdfDocument> document, qsizetype page, QWidget* parent = nullptr, bool localized = false);
    const QList<PdfRedactionArea>& areas() const { return areas_; }
    int resolution() const;
private:
    QRectF candidate() const;
    void updateControls();
    void renderPreview();
    void updateAreas();
    std::shared_ptr<const PdfDocument> document_;
    QList<PdfRedactionArea> areas_;
    QSpinBox* page_ = nullptr;
    QDoubleSpinBox* box_[4]{};
    QComboBox* dpi_ = nullptr;
    QListWidget* list_ = nullptr;
    QCheckBox* consent_ = nullptr;
    QPushButton* add_ = nullptr;
    QPushButton* apply_ = nullptr;
    RedactionPreview* preview_ = nullptr;
    QFutureWatcher<PdfRenderResult>* watcher_ = nullptr;
    qsizetype renderedPage_ = -1;
    bool ready_ = false;
};
}
