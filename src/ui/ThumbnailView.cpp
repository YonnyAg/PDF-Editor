#include "ThumbnailView.h"
#include "core/PdfDocument.h"

#include <QAbstractListModel>
#include <QCache>
#include <QEvent>
#include <QFutureWatcher>
#include <QIcon>
#include <QItemSelectionModel>
#include <QPixmap>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QtConcurrentRun>

#include <algorithm>
#include <functional>
#include <utility>

namespace pdfeditor {

namespace {
constexpr QSize PreviewSize(96, 120);
constexpr int CacheSize = 64;

class ThumbnailDelegate final : public QStyledItemDelegate
{
public:
    ThumbnailDelegate(std::function<void(int)> requestThumbnail, QListView* view)
        : QStyledItemDelegate(view), requestThumbnail_(std::move(requestThumbnail)),
          placeholder_(view->style()->standardIcon(QStyle::SP_FileIcon))
    {
    }

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        return {PreviewSize.width() + 16, PreviewSize.height() + 26};
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        requestThumbnail_(index.row());
        QStyledItemDelegate::paint(painter, option, index);
    }

protected:
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        option->decorationPosition = QStyleOptionViewItem::Top;
        option->decorationAlignment = Qt::AlignCenter;
        option->displayAlignment = Qt::AlignCenter;
        if (option->icon.isNull()) {
            option->icon = placeholder_;
            option->features |= QStyleOptionViewItem::HasDecoration;
            option->decorationSize = QSize(24, 24);
        }
    }

private:
    std::function<void(int)> requestThumbnail_;
    QIcon placeholder_;
};
} // namespace

class ThumbnailModel final : public QAbstractListModel
{
public:
    explicit ThumbnailModel(QObject* parent) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : pageCount_; }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() >= pageCount_) { return {}; }
        if (role == Qt::DisplayRole) { return QString::number(index.row() + 1); }
        if (role == Qt::AccessibleTextRole) { return ThumbnailView::tr("Página %1").arg(index.row() + 1); }
        if (role == Qt::DecorationRole) {
            if (const auto* icon = previews_.object(index.row())) { return *icon; }
        }
        return {};
    }

    void reset(int pageCount)
    {
        beginResetModel();
        previews_.clear();
        pageCount_ = pageCount;
        endResetModel();
    }

    bool hasPreview(int row) const { return previews_.contains(row); }

    void setPreview(int row, const QImage& image)
    {
        // A null icon caches failed renders too, so repainting does not retry them.
        previews_.insert(row, new QIcon(image.isNull() ? QIcon{} : QIcon(QPixmap::fromImage(image))));
        const auto item = index(row);
        emit dataChanged(item, item, {Qt::DecorationRole});
    }

    void clearPreviews() { previews_.clear(); }

private:
    int pageCount_ = 0;
    QCache<int, QIcon> previews_{CacheSize};
};

ThumbnailView::ThumbnailView(QWidget* parent)
    : QListView(parent), thumbnails_(new ThumbnailModel(this)), watcher_(new QFutureWatcher<QImage>(this))
{
    setObjectName(QStringLiteral("pageThumbnails"));
    setAccessibleName(tr("Miniaturas de páginas"));
    setIconSize(PreviewSize);
    setUniformItemSizes(true);
    setVerticalScrollMode(ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setEditTriggers(NoEditTriggers);
    setModel(thumbnails_);
    setItemDelegate(new ThumbnailDelegate([this](int row) { requestThumbnail(row); }, this));

    connect(selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& current) {
        if (current.isValid()) { emit pageSelected(current.row()); }
    });
    connect(watcher_, &QFutureWatcher<QImage>::finished, this, [this] {
        const auto row = activeRow_;
        activeRow_ = -1;
        if (activeGeneration_ == generation_) { thumbnails_->setPreview(row, watcher_->result()); }
        viewport()->update(); // Request the next visible thumbnail on repaint.
    });
}

void ThumbnailView::setDocument(std::shared_ptr<const PdfDocument> document)
{
    if (document_ == document) { return; }
    document_ = std::move(document);
    ++generation_;
    thumbnails_->reset(document_ ? static_cast<int>(document_->pageCount()) : 0);
}

void ThumbnailView::setCurrentPage(int pageIndex)
{
    const auto item = thumbnails_->index(pageIndex);
    if (item.isValid() && currentIndex() != item) {
        setCurrentIndex(item);
        scrollTo(item);
    }
}

bool ThumbnailView::event(QEvent* event)
{
    if (event->type() == QEvent::DevicePixelRatioChange && document_) {
        ++generation_;
        thumbnails_->clearPreviews();
        viewport()->update();
    }
    return QListView::event(event);
}

void ThumbnailView::requestThumbnail(int row)
{
    if (!document_ || activeRow_ >= 0 || thumbnails_->hasPreview(row)) { return; }
    activeRow_ = row;
    activeGeneration_ = generation_;
    const auto document = document_;
    const auto ratio = devicePixelRatioF();
    watcher_->setFuture(QtConcurrent::run([document, row, ratio] {
        const auto size = document->pageSize(row);
        if (size.isEmpty()) { return QImage{}; }
        const auto zoom = std::min({4.0, PreviewSize.width() / size.width(), PreviewSize.height() / size.height()});
        return document->renderPage(row, zoom, ratio).image;
    }));
}

} // namespace pdfeditor
