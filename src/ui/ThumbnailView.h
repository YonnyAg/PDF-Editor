#pragma once

#include <QListView>

#include <memory>

class QImage;
template<typename T> class QFutureWatcher;

namespace pdfeditor {

class PdfDocument;
class ThumbnailModel;

class ThumbnailView final : public QListView
{
    Q_OBJECT

public:
    explicit ThumbnailView(QWidget* parent = nullptr);
    void setDocument(std::shared_ptr<const PdfDocument> document);
    void setCurrentPage(int pageIndex);

signals:
    void pageSelected(int pageIndex);

protected:
    bool event(QEvent* event) override;

private:
    void requestThumbnail(int row);

    std::shared_ptr<const PdfDocument> document_;
    ThumbnailModel* thumbnails_ = nullptr;
    QFutureWatcher<QImage>* watcher_ = nullptr;
    quint64 generation_ = 0;
    quint64 activeGeneration_ = 0;
    int activeRow_ = -1;
};

} // namespace pdfeditor
