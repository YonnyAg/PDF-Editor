#pragma once

#include "core/PdfDocument.h"

#include <QScrollArea>
#include <QTimer>

class QLabel;
template<typename T> class QFutureWatcher;

namespace pdfeditor {

class PageImage;

class PageView final : public QScrollArea
{
    Q_OBJECT

public:
    explicit PageView(QWidget* parent = nullptr);
    void setDocument(std::shared_ptr<const PdfDocument> document, bool keepView = false);
    void setPage(qsizetype pageIndex);
    qsizetype currentPage() const { return pageIndex_; }
    void setZoom(qreal zoom);
    void fitToWidth();
    void setPointSelectionEnabled(bool enabled);
    void setTextSelectionEnabled(bool enabled);
    void clearTextSelection();
    const PdfSearchMatch& textSelection() const { return selection_; }
    void showSearchMatch(const PdfSearchMatch& match);
    void clearSearchMatch();
    qreal zoom() const { return zoom_; }
    bool fitsWidth() const { return fitWidth_; }
    bool hasPageImage() const;
    QSize imageSize() const;
    bool isRendering() const { return active_ || pending_; }

signals:
    void pageChanged(qsizetype pageIndex);
    void pageRendered(const QSize& pixelSize);
    void renderFailed(const QString& message);
    void renderWarning(const QString& message);
    void renderStarted();
    void zoomChanged(qreal zoom, bool fitsWidth);
    void pointSelected(QPointF position);
    void textSelectionChanged();
    void textSelectionFailed(const QString& message);

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void scheduleRender();
    void startRender();
    void clearImage();
    void selectPoint(QPointF point);
    void revealSearchMatch();
    void selectText(QPointF end);

    std::shared_ptr<const PdfDocument> document_;
    qsizetype pageIndex_ = -1;
    PageImage* page_ = nullptr;
    QFutureWatcher<PdfRenderResult>* watcher_ = nullptr;
    QFutureWatcher<PdfTextSelectionResult>* selectionWatcher_ = nullptr;
    PdfSearchMatch selection_{-1, {}, {}, {}};
    quint64 selectionGeneration_ = 0;
    bool textSelectionEnabled_ = true;
    bool draggingText_ = false;
    QPointF selectionStart_;
    QTimer debounce_;
    quint64 generation_ = 0;
    quint64 activeGeneration_ = 0;
    bool active_ = false;
    bool pending_ = false;
    bool fitWidth_ = true;
    bool selectingPoint_ = false;
    qreal zoom_ = 1.0;
    QSize imageSize_;
};

} // namespace pdfeditor
