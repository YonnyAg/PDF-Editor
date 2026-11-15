#include "PageView.h"

#include <QEvent>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QRubberBand>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace pdfeditor {

namespace { constexpr int PageMargin = 24; }

class PageImage final : public QLabel
{
public:
    using QLabel::QLabel;

    void setHighlight(QPainterPath path)
    {
        highlight_ = std::move(path);
        update();
    }

    QRectF highlightBounds() const
    {
        return QTransform::fromScale(width(), height()).mapRect(highlight_.boundingRect());
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QLabel::paintEvent(event);
        if (highlight_.isEmpty()) { return; }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(176, 102, 0), 1.5));
        painter.setBrush(QColor(255, 183, 38, 88));
        painter.drawPath(QTransform::fromScale(width(), height()).map(highlight_));
    }

private:
    QPainterPath highlight_;
};

PageView::PageView(QWidget* parent)
    : QScrollArea(parent), debounce_(this)
{
    setObjectName(QStringLiteral("pageView"));
    setAccessibleName(tr("Vista de página"));
    setFocusPolicy(Qt::StrongFocus);
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    // Reserve the scrollbar width so fit-to-width does not oscillate on layout changes.
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    auto* canvas = new QWidget(this);
    canvas->setAutoFillBackground(true);
    canvas->setBackgroundRole(QPalette::Dark);
    auto* layout = new QVBoxLayout(canvas);
    layout->setContentsMargins(PageMargin, PageMargin, PageMargin, PageMargin);
    page_ = new PageImage(canvas);
    page_->setObjectName(QStringLiteral("pageImage"));
    page_->setAccessibleName(tr("Página del documento"));
    page_->setAccessibleDescription(tr("Imagen de la página. Para recorrer su texto con el teclado o un lector de pantalla, usa Control Alt L."));
    page_->installEventFilter(this);
    page_->setCursor(Qt::IBeamCursor);
    layout->addWidget(page_, 0, Qt::AlignHCenter);
    layout->addStretch();
    setWidget(canvas);
    clearImage();

    debounce_.setSingleShot(true);
    debounce_.setInterval(60);
    connect(&debounce_, &QTimer::timeout, this, &PageView::startRender);
    watcher_ = new QFutureWatcher<PdfRenderResult>(this);
    selectionWatcher_ = new QFutureWatcher<PdfTextSelectionResult>(this);
    connect(watcher_, &QFutureWatcher<PdfRenderResult>::finished, this, [this] {
        active_ = false;
        // A close, replacement or newer zoom invalidates this result.
        if (document_ && activeGeneration_ == generation_) {
            const auto result = watcher_->result();
            if (result.image.isNull()) {
                clearImage();
                emit renderFailed(result.error);
            } else {
                imageSize_ = result.image.size();
                page_->setPixmap(QPixmap::fromImage(result.image));
                page_->setFixedSize(result.image.deviceIndependentSize().toSize());
                page_->show();
                page_->setAccessibleName(tr("Página %1 de %2, zoom %3 por ciento").arg(pageIndex_ + 1).arg(document_->pageCount()).arg(qRound(zoom_ * 100)));
                revealSearchMatch();
                emit pageRendered(imageSize_);
                if (!result.warnings.isEmpty()) {
                    emit renderWarning(tr("La vista puede no incluir todos los elementos de esta página."));
                }
            }
        }
        if (pending_) { debounce_.start(0); }
    });
}

void PageView::clearImage()
{
    page_->clear();
    page_->setAccessibleName(tr("Página no disponible"));
    page_->setFixedSize(0, 0);
    page_->hide();
    imageSize_ = {};
}

void PageView::setDocument(std::shared_ptr<const PdfDocument> document, bool keepView)
{
    if (document_ == document) { return; }
    clearTextSelection();
    clearSearchMatch();
    document_ = std::move(document);
    if (!keepView || !document_) {
        pageIndex_ = document_ ? 0 : -1;
        fitWidth_ = true;
        zoom_ = 1.0;
    } else {
        pageIndex_ = std::clamp(pageIndex_, qsizetype{0}, document_->pageCount() - 1);
    }
    clearImage();
    verticalScrollBar()->setValue(0);
    horizontalScrollBar()->setValue(0);
    scheduleRender();
    emit zoomChanged(zoom_, fitWidth_);
}

void PageView::setPage(qsizetype pageIndex)
{
    if (!document_ || pageIndex < 0 || pageIndex >= document_->pageCount() || pageIndex == pageIndex_) { return; }
    clearTextSelection();
    clearSearchMatch();
    pageIndex_ = pageIndex;
    clearImage();
    verticalScrollBar()->setValue(0);
    horizontalScrollBar()->setValue(0);
    scheduleRender();
    emit pageChanged(pageIndex_);
}

void PageView::setZoom(qreal zoom)
{
    if (!document_ || !std::isfinite(zoom)) { return; }
    zoom = std::clamp(zoom, 0.1, 4.0);
    if (!fitWidth_ && qFuzzyCompare(zoom_, zoom)) { return; }
    zoom_ = zoom;
    fitWidth_ = false;
    scheduleRender();
    emit zoomChanged(zoom_, fitWidth_);
}

void PageView::fitToWidth()
{
    if (!document_) { return; }
    fitWidth_ = true;
    scheduleRender();
}

bool PageView::hasPageImage() const { return !imageSize_.isEmpty(); }
QSize PageView::imageSize() const { return imageSize_; }

void PageView::showSearchMatch(const PdfSearchMatch& match)
{
    clearTextSelection();
    setPage(match.page);
    page_->setHighlight(match.highlight);
    revealSearchMatch();
}

void PageView::clearSearchMatch()
{
    clearTextSelection();
    page_->setHighlight({});
}

void PageView::revealSearchMatch()
{
    const auto bounds = page_->highlightBounds();
    if (!hasPageImage() || bounds.isEmpty()) { return; }
    const auto point = page_->mapTo(widget(), bounds.center().toPoint());
    ensureVisible(point.x(), point.y(), qMin(viewport()->width() / 3, qCeil(bounds.width() / 2) + 16),
        qMin(viewport()->height() / 3, qCeil(bounds.height() / 2) + 16));
}

void PageView::setPointSelectionEnabled(bool enabled)
{
    clearTextSelection();
    selectingPoint_ = enabled;
    if (enabled) {
        page_->setCursor(Qt::CrossCursor);
        setFocus(Qt::ShortcutFocusReason);
    } else {
        page_->setCursor(textSelectionEnabled_ ? Qt::IBeamCursor : Qt::ArrowCursor);
    }
}

void PageView::setTextSelectionEnabled(bool enabled)
{
    textSelectionEnabled_ = enabled;
    if (!enabled) { clearTextSelection(); }
    if (!selectingPoint_) { page_->setCursor(enabled ? Qt::IBeamCursor : Qt::ArrowCursor); }
}

void PageView::clearTextSelection()
{
    ++selectionGeneration_;
    draggingText_ = false;
    if (auto* band = page_->findChild<QRubberBand*>()) { band->hide(); }
    const bool hadSelection = !selection_.text.isEmpty();
    selection_ = {-1, {}, {}, {}};
    if (hadSelection) {
        page_->setHighlight({});
        emit textSelectionChanged();
    }
}

void PageView::selectText(QPointF end)
{
    if (!document_ || selectionWatcher_->isRunning()) { return; }
    const auto document = document_;
    const auto pageIndex = pageIndex_;
    const auto generation = selectionGeneration_;
    const auto start = selectionStart_;
    disconnect(selectionWatcher_, nullptr, this, nullptr);
    connect(selectionWatcher_, &QFutureWatcher<PdfTextSelectionResult>::finished, this, [this, generation] {
        if (generation != selectionGeneration_) { return; }
        const auto result = selectionWatcher_->result();
        if (!result.error.isEmpty()) { emit textSelectionFailed(result.error); return; }
        selection_ = result.match;
        page_->setHighlight(selection_.highlight);
        emit textSelectionChanged();
    });
    selectionWatcher_->setFuture(QtConcurrent::run([document, pageIndex, start, end] {
        return document->selectText(pageIndex, start, end);
    }));
}

void PageView::selectPoint(QPointF point)
{
    emit pointSelected(QPointF(point.x() / page_->width(), point.y() / page_->height()));
}

bool PageView::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == page_ && !selectingPoint_ && textSelectionEnabled_ && hasPageImage() && !isRendering()) {
        const auto normalized = [this](QPointF point) {
            return QPointF(std::clamp(point.x() / page_->width(), 0.0, 1.0),
                std::clamp(point.y() / page_->height(), 0.0, 1.0));
        };
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                clearTextSelection();
                clearSearchMatch();
                draggingText_ = true;
                selectionStart_ = normalized(mouse->position());
                setFocus(Qt::MouseFocusReason);
                auto* band = page_->findChild<QRubberBand*>();
                if (!band) { band = new QRubberBand(QRubberBand::Rectangle, page_); }
                band->setGeometry(QRect(mouse->position().toPoint(), QSize()));
                band->show();
                return true;
            }
        } else if (event->type() == QEvent::MouseMove && draggingText_) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            const QPoint start(qRound(selectionStart_.x() * page_->width()), qRound(selectionStart_.y() * page_->height()));
            page_->findChild<QRubberBand*>()->setGeometry(QRect(start, mouse->position().toPoint()).normalized().intersected(page_->rect()));
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease && draggingText_) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                draggingText_ = false;
                page_->findChild<QRubberBand*>()->hide();
                const auto end = normalized(mouse->position());
                if (QLineF(selectionStart_, end).length() > 0.002) { selectText(end); }
                return true;
            }
        }
    }
    if (watched == page_ && selectingPoint_ && hasPageImage() && event->type() == QEvent::MouseButtonRelease) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton && QRectF(page_->rect()).contains(mouse->position())) {
            selectPoint(mouse->position());
            return true;
        }
    }
    return QScrollArea::eventFilter(watched, event);
}

void PageView::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && (draggingText_ || selectionWatcher_->isRunning() || !selection_.text.isEmpty())) {
        clearTextSelection();
        event->accept();
        return;
    }
    if (selectingPoint_ && hasPageImage() && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        const auto origin = page_->mapTo(viewport(), QPoint());
        const auto visible = viewport()->rect().intersected(QRect(origin, page_->size()));
        if (!visible.isEmpty()) {
            event->accept();
            selectPoint(visible.center() - origin);
            return;
        }
    }
    QScrollArea::keyPressEvent(event);
}

void PageView::scheduleRender()
{
    // Coordinates of an in-flight drag no longer describe the displayed page.
    if (draggingText_) { clearTextSelection(); }
    ++generation_;
    pending_ = static_cast<bool>(document_);
    if (pending_) { debounce_.start(60); }
    else { debounce_.stop(); }
}

void PageView::startRender()
{
    if (active_ || !pending_ || !document_) { return; }
    pending_ = false;
    if (fitWidth_) {
        const auto width = document_->pageSize(pageIndex_).width();
        zoom_ = width > 0 ? std::min(4.0, std::max(1, viewport()->width() - 2 * PageMargin - 2) / width) : 1.0;
        emit zoomChanged(zoom_, fitWidth_);
    }
    active_ = true;
    activeGeneration_ = generation_;
    const auto document = document_;
    const auto pageIndex = pageIndex_;
    const auto zoom = zoom_;
    const auto ratio = devicePixelRatioF();
    emit renderStarted();
    watcher_->setFuture(QtConcurrent::run([document, pageIndex, zoom, ratio] {
        return document->renderPage(pageIndex, zoom, ratio);
    }));
}

void PageView::resizeEvent(QResizeEvent* event)
{
    QScrollArea::resizeEvent(event);
    if (document_ && fitWidth_ && event->size().width() != event->oldSize().width()) { scheduleRender(); }
}

bool PageView::event(QEvent* event)
{
    const auto result = QScrollArea::event(event);
    if (event->type() == QEvent::DevicePixelRatioChange && document_) { scheduleRender(); }
    return result;
}

} // namespace pdfeditor
