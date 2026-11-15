#pragma once
#include "OcrEngine.h"

#include <QString>
#include <QStringList>
#include <QImage>
#include <QList>
#include <QMarginsF>
#include <QPointF>
#include <QPainterPath>
#include <QPolygonF>
#include <QSizeF>
#include <QVariant>
#include <QColor>

#include <functional>
#include <memory>
#include <mutex>

namespace pdf { class PDFDocument; }

namespace pdfeditor {

struct PdfOpenResult;
struct PdfRenderResult;
struct PdfPageImport;
struct PdfOcrPage;
enum class PdfSaveMode { Original, NewCopy };
enum class PdfRenderTarget { View, Print };
enum class PdfPageOperation { Duplicate, Delete, InsertBlankAfter };
enum class PdfTextMarkup { Highlight, Underline, StrikeOut, Squiggly };
enum class PdfBookmarkChange { Add, Rename, Retarget, Delete, MoveUp, MoveDown, Indent, Outdent };

struct PdfTextStyle
{
    QString family = QStringLiteral("Arial");
    qreal pointSize = 12;
    QColor color = Qt::black;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    Qt::Alignment alignment = Qt::AlignLeft;
};

struct PdfPageDecoration
{
    QString text;
    QRectF area{0.1, 0.87, 0.8, 0.1};
    PdfTextStyle style;
    qreal rotation = 0;
    qreal opacity = 1;
    bool behind = false;
    bool numbering = false; // {n}: sequential number; {total}: selected page count.
    int firstNumber = 1;
};

struct PdfMetadata
{
    QString title;
    QString author;
    QString subject;
    QString keywords;
};

struct PdfLink
{
    qsizetype index = -1;
    QRectF area;
    qsizetype targetPage = -1;
    QString url;
    bool editable = false;
};

struct PdfSearchMatch
{
    qsizetype page;
    QPainterPath highlight;
    QString text;
    QPolygonF quadrilaterals;
};

struct PdfSearchResult
{
    QList<PdfSearchMatch> matches;
    qsizetype skippedPages = 0;
    bool limited = false;
};

struct PdfTextSelectionResult
{
    PdfSearchMatch match{-1, {}, {}, {}};
    QString error;
};

struct PdfBookmark
{
    QString title;
    qsizetype page = -1;
    int depth = 0;
    QList<qsizetype> path;
};

struct PdfOutputResult
{
    QString error;
    QStringList warnings;
};

struct PdfNote
{
    qsizetype index;
    QString text;
    bool editable;
};

struct PdfTextMark
{
    qsizetype index;
    PdfTextMarkup type;
    QString text;
    bool editable;
    QPainterPath area;
};

enum class PdfFormFieldType { Text, CheckBox, ComboBox, RadioGroup, MultiSelectList };

struct PdfFormField
{
    qsizetype index;
    QString name;
    PdfFormFieldType type;
    QVariant value;
    bool editable;
    bool multiline = false;
    qsizetype maxLength = 0;
    QStringList options;
    QStringList exportValues;
};

enum class PdfShapeType { Rectangle, Ellipse, Line, Arrow, Freehand };
struct PdfShape
{
    qsizetype index;
    PdfShapeType type;
    QRectF area;
    QString text;
    bool editable;
};

struct PdfAttachment
{
    QByteArray key;
    QString name;
    QString description;
    qint64 size = -1;
};

struct PdfRedactionArea
{
    qsizetype page = -1;
    QRectF area; // Fraction of the visible, rotated crop, measured from top left.
};

enum class PdfContentType { Text, Image, Path };
struct PdfContentItem
{
    qsizetype index = -1;
    PdfContentType type = PdfContentType::Text;
    QRectF area;
    QString text;
    bool replaceable = false;
};
struct PdfContentList { QList<PdfContentItem> items; QString error; };
enum class PdfContentChange { ReplaceText, ReplaceImage, Transform, Delete, PathStyle };
struct PdfContentUpdate
{
    PdfContentChange change = PdfContentChange::Transform;
    QString text;
    QImage image;
    QPointF offset;
    qreal scale = 1;
    qreal rotation = 0;
    QColor stroke = Qt::black;
    QColor fill = Qt::transparent;
    qreal width = 2;
};
struct PdfSignatureInfo
{
    QString name;
    bool integrityValid = false;
    bool certificateTrusted = false;
    bool coversWholeFile = false;
    QStringList errors;
    QStringList warnings;
    QStringList algorithms;
};
struct PdfSignatureReport { QList<PdfSignatureInfo> signatures; QString error; };

class PdfDocument final
{
public:
    static PdfOpenResult open(const QString& filePath, const QString& password = {});

    const QString& filePath() const { return filePath_; }
    const QByteArray& sourceHash() const { return sourceHash_; }
    bool isEncrypted() const;
    static PdfOpenResult recover(const QString& snapshotPath, const QString& originalPath, const QByteArray& originalHash);
    qsizetype pageCount() const;
    // Visible crop rectangle after rotation, in logical pixels at 96 DPI.
    QSizeF pageSize(qsizetype pageIndex) const;
    PdfRenderResult renderPage(qsizetype pageIndex, qreal zoom = 1.0, qreal devicePixelRatio = 1.0,
        PdfRenderTarget target = PdfRenderTarget::View) const;
    PdfSearchResult findText(const QString& text, const std::function<bool()>& cancelled = {}) const;
    bool canCopyText() const;
    PdfTextSelectionResult selectText(qsizetype pageIndex, QPointF start, QPointF end) const;
    QList<PdfBookmark> bookmarks() const;
    PdfOpenResult changeBookmark(PdfBookmarkChange change, const QList<qsizetype>& path,
        const QString& title = {}, qsizetype pageIndex = -1) const;
    PdfOutputResult exportPng(qsizetype pageIndex, const QString& path) const;
    int printResolutionLimit() const;
    PdfRenderResult renderForOutput(qsizetype pageIndex, int dpi, PdfRenderTarget target) const;
    QList<PdfNote> pageNotes(qsizetype pageIndex) const;
    // Position is relative to the displayed crop, from the upper left.
    PdfOpenResult addNote(qsizetype pageIndex, QPointF position, const QString& text) const;
    PdfOpenResult editNote(qsizetype pageIndex, qsizetype noteIndex, const QString& text) const;
    PdfOpenResult deleteNote(qsizetype pageIndex, qsizetype noteIndex) const;
    PdfOpenResult highlightMatch(const PdfSearchMatch& match) const;
    PdfOpenResult markMatch(const PdfSearchMatch& match, PdfTextMarkup type) const;
    QList<PdfTextMark> pageTextMarks(qsizetype pageIndex) const;
    PdfOpenResult deleteTextMark(qsizetype pageIndex, qsizetype markIndex) const;
    QList<PdfFormField> pageFormFields(qsizetype pageIndex) const;
    PdfOpenResult setFormField(qsizetype pageIndex, qsizetype fieldIndex, const QVariant& value) const;
    PdfOpenResult createFormField(qsizetype pageIndex, const QRectF& area, const QString& name,
        PdfFormFieldType type, const QStringList& options = {}, bool multiline = false) const;
    QList<PdfShape> pageShapes(qsizetype pageIndex) const;
    PdfOpenResult addShape(qsizetype pageIndex, const QRectF& area, PdfShapeType type,
        const QColor& stroke, const QColor& fill = {}, qreal width = 2, const QString& text = {}) const;
    PdfOpenResult deleteShape(qsizetype pageIndex, qsizetype index) const;
    PdfOpenResult restyleShape(qsizetype pageIndex, qsizetype index, const QColor& stroke, const QColor& fill, qreal width) const;
    PdfOpenResult addInk(qsizetype pageIndex, const QList<QPolygonF>& strokes, const QColor& color, qreal width = 2) const;
    QList<PdfAttachment> attachments() const;
    PdfOpenResult addAttachment(const QString& name, const QByteArray& bytes, const QString& description = {}) const;
    PdfOpenResult deleteAttachment(const QByteArray& key) const;
    PdfOutputResult exportAttachment(const QByteArray& key, const QString& destination) const;
    PdfOpenResult optimizedCopy(const QString& destination) const;
    PdfOutputResult protectedCopy(const QString& destination, const QString& userPassword, const QString& ownerPassword,
        bool allowCopy = true, bool allowPrint = true) const;
    PdfOpenResult addOcrLayer(const QList<PdfOcrPage>& pages) const;
    PdfContentList pageContent(qsizetype page) const;
    PdfOpenResult editContent(qsizetype page, qsizetype index, const PdfContentUpdate& update) const;
    PdfSignatureReport verifySignatures() const;
    PdfOutputResult signedCopy(const QString& destination, const QString& certificatePath, const QString& password,
        const QString& reason = {}) const;
    PdfOutputResult redactedCopy(const QString& destination, const QList<PdfRedactionArea>& areas,
        int dpi = 150, const std::function<bool()>& cancelled = {}) const;
    PdfOutputResult localizedRedactedCopy(const QString& destination, const QList<PdfRedactionArea>& areas) const;
    PdfOpenResult rotatePage(qsizetype pageIndex) const;
    PdfOpenResult movePage(qsizetype from, qsizetype to) const;
    PdfOpenResult changePage(qsizetype pageIndex, PdfPageOperation operation) const;
    // Margins are fractions of the currently visible page, measured from the displayed edges.
    PdfOpenResult cropPages(qsizetype first, qsizetype last, const QMarginsF& margins) const;
    PdfOpenResult resetPageCrop(qsizetype first, qsizetype last) const;
    PdfOpenResult insertPages(qsizetype position, const QList<PdfPageImport>& sources) const;
    PdfOpenResult extractPages(qsizetype first, qsizetype last, const QString& copyPath) const;
    PdfOutputResult splitPages(qsizetype pagesPerPart, const QString& outputDirectory) const;
    PdfOpenResult insertText(qsizetype pageIndex, const QRectF& area, const QString& text, qreal pointSize = 12) const;
    PdfOpenResult insertStyledText(qsizetype pageIndex, const QRectF& area, const QString& text, const PdfTextStyle& style) const;
    PdfOpenResult insertImage(qsizetype pageIndex, const QRectF& area, const QImage& image) const;
    PdfOpenResult decoratePages(qsizetype first, qsizetype last, const PdfPageDecoration& decoration) const;
    PdfMetadata metadata() const;
    PdfOpenResult setMetadata(const PdfMetadata& metadata) const;
    QList<PdfLink> pageLinks(qsizetype pageIndex) const;
    PdfOpenResult addLink(qsizetype pageIndex, const QRectF& area, qsizetype targetPage, const QString& url = {}) const;
    PdfOpenResult deleteLink(qsizetype pageIndex, qsizetype linkIndex) const;
    PdfOpenResult save(PdfSaveMode mode, const QString& copyPath = {}) const;

private:
    PdfOpenResult appendPageOverlays(qsizetype first, qsizetype last, const PdfPageDecoration& decoration, const QImage& image = {}, const QList<PdfOcrPage>& ocrPages = {}) const;
    PdfOpenResult editPageCrop(qsizetype first, qsizetype last, const QMarginsF* margins) const;
    PdfDocument(QString filePath, std::shared_ptr<pdf::PDFDocument> document, QByteArray sourceHash);

    QString filePath_;
    std::shared_ptr<pdf::PDFDocument> document_;
    QByteArray sourceHash_;
    mutable std::mutex renderMutex_;
};

struct PdfPageImport
{
    std::shared_ptr<const PdfDocument> document;
    qsizetype first = 0;
    qsizetype last = -1;
};

struct PdfOpenResult
{
    std::shared_ptr<const PdfDocument> document;
    QString error;
    QStringList warnings;
    bool passwordRequired = false;
};

struct PdfRenderResult
{
    QImage image;
    QString error;
    QStringList warnings;
};

} // namespace pdfeditor
