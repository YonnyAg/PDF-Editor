#pragma once

#include <QImage>
#include <QList>
#include <QRectF>
#include <QString>
#include <functional>
#include <memory>

namespace pdfeditor {
class PdfDocument;
struct OcrLanguage { QString tag; QString name; };
struct PdfOcrWord { QString text; QRectF area; };
struct PdfOcrPage
{
    qsizetype page = -1;
    QList<PdfOcrWord> words;
    QString text;
    qreal angle = 0;
};
struct OcrResult
{
    QList<PdfOcrWord> words;
    QString text;
    QString error;
    qreal angle = 0;
};
struct OcrBatchResult
{
    QList<PdfOcrPage> pages;
    QString text;
    QString error;
    qsizetype skippedPages = 0;
    bool cancelled = false;
};
class OcrEngine final
{
public:
    static QList<OcrLanguage> languages();
    static OcrResult recognize(const QImage& image, const QString& language);
    static OcrBatchResult recognizePdf(const std::shared_ptr<const PdfDocument>& document, qsizetype first, qsizetype last,
        const QString& language, const std::function<bool()>& cancelled = {});
};
} // namespace pdfeditor
