#pragma once

#include "core/PdfDocument.h"

#include <QToolBar>
#include <atomic>

class QLabel;
class QLineEdit;
class QHideEvent;
class QActionGroup;
template<typename T> class QFutureWatcher;

namespace pdfeditor {

class SearchBar final : public QToolBar
{
    Q_OBJECT

public:
    explicit SearchBar(QWidget* parent = nullptr);
    void setDocument(std::shared_ptr<const PdfDocument> document);
    void focusQuery();
    void setEditingEnabled(bool enabled);

signals:
    void matchSelected(const PdfSearchMatch& match);
    void matchCleared();
    void markupRequested(const PdfSearchMatch& match, PdfTextMarkup type);

protected:
    void hideEvent(QHideEvent* event) override;

private:
    void clearResults();
    void search();
    void step(qsizetype offset);

    std::shared_ptr<const PdfDocument> document_;
    std::shared_ptr<std::atomic_bool> cancellation_;
    QLineEdit* query_;
    QLabel* status_;
    QAction* previous_;
    QAction* next_;
    QActionGroup* markupActions_;
    bool editingEnabled_ = false;
    PdfSearchResult result_;
    qsizetype selected_ = -1;
};

} // namespace pdfeditor
