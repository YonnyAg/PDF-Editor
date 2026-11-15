#include "core/PdfDocument.h"
#include "PdfTestData.h"
#include "VisualPdfData.h"
#include <QDir>
#include <QFontDatabase>
#include <QTemporaryDir>
#include <QTest>

class RemainingFeaturesTest final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
            for (const auto* face : {"arial.ttf", "arialbd.ttf", "ariali.ttf", "arialbi.ttf"}) { QVERIFY(QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts/") + QString::fromLatin1(face))) >= 0); }
        }
#endif
    }
    void createsAndFillsRadioAndMultipleChoice_data()
    {
        QTest::addColumn<bool>("radio"); QTest::newRow("radio") << true; QTest::newRow("multiple") << false;
    }
    void createsAndFillsRadioAndMultipleChoice()
    {
        QFETCH(bool, radio);
        QTemporaryDir directory; const auto path = directory.filePath("source.pdf"); const auto output = directory.filePath("saved.pdf");
        QVERIFY(testdata::writeVisualPdf(path, 90, 2)); const auto hash = testdata::digest(path);
        const auto source = pdfeditor::PdfDocument::open(path).document; QVERIFY(source);
        const auto type = radio ? pdfeditor::PdfFormFieldType::RadioGroup : pdfeditor::PdfFormFieldType::MultiSelectList;
        const auto created = source->createFormField(0, {.1, .1, .7, .55}, QStringLiteral("Choices"), type, {QStringLiteral("España"), QStringLiteral("México"), QStringLiteral("Chile")});
        QVERIFY2(created.document, qPrintable(created.error));
        const auto fields = created.document->pageFormFields(0); QCOMPARE(fields.size(), 1); QCOMPARE(fields[0].type, type); QVERIFY(fields[0].editable); QCOMPARE(fields[0].options.size(), 3);
        const auto before = created.document->renderPage(0).image;
        const QVariant selected = radio ? QVariant(fields[0].exportValues[1]) : QVariant(QStringList{fields[0].exportValues[0], fields[0].exportValues[2]});
        const auto filled = created.document->setFormField(0, fields[0].index, selected); QVERIFY2(filled.document, qPrintable(filled.error));
        QCOMPARE(filled.document->pageFormFields(0)[0].value, selected);
        const auto after = filled.document->renderPage(0); QVERIFY2(!after.image.isNull(), qPrintable(after.error)); QVERIFY(after.warnings.isEmpty()); QVERIFY(after.image != before);
        QVERIFY(filled.document->save(pdfeditor::PdfSaveMode::NewCopy, output).document);
        const auto reopened = pdfeditor::PdfDocument::open(output).document; QVERIFY(reopened); QCOMPARE(reopened->pageFormFields(0)[0].value, selected); QCOMPARE(reopened->renderPage(0).image, after.image);
        QVERIFY(!reopened->setFormField(0, fields[0].index, radio ? QVariant(QStringLiteral("missing")) : QVariant(QStringList{QStringLiteral("missing")})).document);
        if (!radio) {
            QVERIFY(!reopened->setFormField(0, fields[0].index, QStringList{fields[0].exportValues[0], fields[0].exportValues[0]}).document);
            const auto empty = reopened->setFormField(0, fields[0].index, QStringList{}); QVERIFY(empty.document); QVERIFY(empty.document->pageFormFields(0)[0].value.toStringList().isEmpty());
        }
        QCOMPARE(testdata::digest(path), hash);
    }
    void localizedRedactionKeepsOtherTextAndVectors_data()
    {
        QTest::addColumn<int>("rotation"); for (int angle : {0, 90, 180, 270}) { QTest::newRow(qPrintable(QString::number(angle))) << angle; }
    }
    void localizedRedactionKeepsOtherTextAndVectors()
    {
        QFETCH(int, rotation);
        QTemporaryDir directory; const auto path = directory.filePath("source.pdf"); const auto output = directory.filePath("localized.pdf");
        QVERIFY(testdata::writeVisualPdf(path, rotation, 2)); const auto hash = testdata::digest(path);
        const auto source = pdfeditor::PdfDocument::open(path).document; QVERIFY(source);
        const auto matches = source->findText(QStringLiteral("PDF Editor")).matches; QCOMPARE(matches.size(), 1);
        auto region = matches[0].highlight.boundingRect().intersected(QRectF(0, 0, 1, 1)); QVERIFY(region.isValid());
        const auto result = source->localizedRedactedCopy(output, {{0, region}}); QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        const auto copy = pdfeditor::PdfDocument::open(output).document; QVERIFY(copy); QCOMPARE(copy->pageSize(0), source->pageSize(0));
        QVERIFY(copy->findText(QStringLiteral("PDF Editor")).matches.isEmpty()); QVERIFY(copy->attachments().isEmpty()); QVERIFY(copy->metadata().title.isEmpty());
        const auto content = copy->pageContent(0); QVERIFY2(content.error.isEmpty(), qPrintable(content.error));
        QVERIFY(std::any_of(content.items.begin(), content.items.end(), [](const auto& item) { return item.type == pdfeditor::PdfContentType::Image; }));
        const auto pixels = copy->renderPage(0).image; QVERIFY(!pixels.isNull());
        const auto center = region.center(); QCOMPARE(pixels.pixelColor(qRound(center.x() * pixels.width()), qRound(center.y() * pixels.height())), QColor(Qt::black));
        QCOMPARE(testdata::digest(path), hash);
    }
    void preservesSearchableTextOnUnselectedPagesAndDropsAttachments()
    {
        QTemporaryDir directory; const auto path = directory.filePath("source.pdf"); const auto output = directory.filePath("copy.pdf");
        QVERIFY(testdata::writeOutputPdf(path)); auto source = pdfeditor::PdfDocument::open(path).document; QVERIFY(source);
        const auto attached = source->addAttachment(QStringLiteral("private.txt"), QByteArray("PRIVATE-ORIGINAL-ATTACHMENT")); QVERIFY(attached.document); source = attached.document;
        const auto match = source->selectText(0, {0, 0}, {1, 1}); QVERIFY(!match.match.text.isEmpty());
        const auto result = source->localizedRedactedCopy(output, {{0, {0, 0, 1, 1}}}); QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        const auto copy = pdfeditor::PdfDocument::open(output).document; QVERIFY(copy); QCOMPARE(copy->pageCount(), 3);
        QVERIFY(copy->selectText(0, {0, 0}, {1, 1}).match.text.isEmpty());
        QVERIFY(!copy->selectText(1, {0, 0}, {1, 1}).match.text.isEmpty());
        QVERIFY(copy->attachments().isEmpty()); QFile bytes(output); QVERIFY(bytes.open(QIODevice::ReadOnly)); QVERIFY(!bytes.readAll().contains("PRIVATE-ORIGINAL-ATTACHMENT"));
    }
    void rejectsUnsupportedLocalizedDocumentsAndExistingDestinations()
    {
        QTemporaryDir directory; const auto path = directory.filePath("source.pdf"); const auto output = directory.filePath("copy.pdf");
        QVERIFY(testdata::writeVisualPdf(path, 0, 1, testdata::VisualPdfExtras::AnnotationsAndForm));
        auto source = pdfeditor::PdfDocument::open(path).document; QVERIFY(source);
        QVERIFY(!source->localizedRedactedCopy(output, {{0, {0, 0, 1, 1}}}).error.isEmpty()); QVERIFY(!QFileInfo::exists(output));
        QVERIFY(testdata::writeVisualPdf(path)); source = pdfeditor::PdfDocument::open(path).document; QVERIFY(source);
        const auto hash = testdata::digest(path); QVERIFY(!source->localizedRedactedCopy(path, {{0, {0, 0, 1, 1}}}).error.isEmpty()); QCOMPARE(testdata::digest(path), hash);
        QVERIFY(!source->localizedRedactedCopy(output, {{99, {0, 0, 1, 1}}}).error.isEmpty());
    }
};
QTEST_MAIN(RemainingFeaturesTest)
#include "RemainingFeaturesTest.moc"
