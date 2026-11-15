#include "core/PdfDocument.h"
#include "VisualPdfData.h"
#include "PdfTestData.h"
#include <QDir>
#include <QFontDatabase>
#include <QTemporaryDir>
#include <QTest>
#include <limits>

class RedactionTest final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
            QVERIFY(QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts/arial.ttf"))) >= 0);
        }
#endif
    }
    void rebuildsPixelsWithoutSourceObjects_data()
    {
        QTest::addColumn<int>("rotation");
        for (int angle : {0, 90, 180, 270}) { QTest::newRow(qPrintable(QString::number(angle))) << angle; }
    }
    void rebuildsPixelsWithoutSourceObjects()
    {
        QFETCH(int, rotation);
        QTemporaryDir directory;
        const auto path = directory.filePath("original.pdf");
        const auto output = directory.filePath("redacted.pdf");
        QVERIFY(testdata::writeVisualPdf(path, rotation, 2, testdata::VisualPdfExtras::AnnotationsAndForm));
        const auto hash = testdata::digest(path);
        auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        auto edited = source->addAttachment(QStringLiteral("secret.txt"), QByteArray("PRIVATE-HIDDEN-PAYLOAD-2026"));
        QVERIFY2(edited.document, qPrintable(edited.error));
        edited = edited.document->setMetadata({QStringLiteral("PRIVATE-TITLE"), QStringLiteral("PRIVATE-AUTHOR"), {}, {}});
        QVERIFY(edited.document);
        edited = edited.document->changeBookmark(pdfeditor::PdfBookmarkChange::Add, {}, QStringLiteral("PRIVATE-BOOKMARK"), 0);
        QVERIFY(edited.document);
        source = edited.document;
        const auto before = source->renderPage(0, 150.0 / 96).image;
        const auto result = source->redactedCopy(output, {{0, {0.1, 0.2, 0.3, 0.4}}});
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        const auto copy = pdfeditor::PdfDocument::open(output).document;
        QVERIFY(copy);
        QVERIFY(copy->findText(QStringLiteral("PDF Editor")).matches.isEmpty());
        QVERIFY(copy->selectText(0, {0, 0}, {1, 1}).match.text.isEmpty());
        QVERIFY(copy->pageFormFields(0).isEmpty());
        QVERIFY(copy->pageNotes(0).isEmpty());
        QVERIFY(copy->pageTextMarks(0).isEmpty());
        QVERIFY(copy->pageShapes(0).isEmpty());
        QVERIFY(copy->pageLinks(0).isEmpty());
        QVERIFY(copy->attachments().isEmpty());
        QVERIFY(copy->bookmarks().isEmpty());
        QVERIFY(copy->metadata().title.isEmpty());
        QVERIFY(copy->metadata().author.isEmpty());
        const auto after = copy->renderPage(0, 150.0 / 96).image;
        QVERIFY(!after.isNull());
        QVERIFY(qAbs(after.width() - before.width()) <= 2 && qAbs(after.height() - before.height()) <= 2);
        const QRect region(qRound(after.width() * .12), qRound(after.height() * .22), qRound(after.width() * .26), qRound(after.height() * .36));
        for (int y = region.top(); y <= region.bottom(); ++y) {
            for (int x = region.left(); x <= region.right(); ++x) { QCOMPARE(after.pixelColor(x, y), QColor(Qt::black)); }
        }
        // A location outside the erased region retains the original appearance.
        const auto outside = after.pixelColor(qRound(after.width() * .8), qRound(after.height() * .8));
        const auto expected = before.pixelColor(qRound(before.width() * .8), qRound(before.height() * .8));
        QVERIFY(qAbs(outside.red() - expected.red()) < 10 && qAbs(outside.green() - expected.green()) < 10 && qAbs(outside.blue() - expected.blue()) < 10);
        QFile bytes(output); QVERIFY(bytes.open(QIODevice::ReadOnly));
        const auto raw = bytes.readAll();
        for (const auto* secret : {"PRIVATE-HIDDEN-PAYLOAD-2026", "PRIVATE-TITLE", "PRIVATE-AUTHOR", "PRIVATE-BOOKMARK", "Synthetic test note"}) { QVERIFY(!raw.contains(secret)); }
        QCOMPARE(testdata::digest(path), hash);
        QVERIFY(!source->attachments().isEmpty());
        QVERIFY(!source->findText(QStringLiteral("PDF Editor")).matches.isEmpty());
    }
    void preservesMixedPageSizesAndErasesOnlyRequestedPages()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("source.pdf");
        const auto output = directory.filePath("copy.pdf");
        QVERIFY(testdata::writeOutputPdf(path));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const auto result = source->redactedCopy(output, {{1, {0, 0, 1, 1}}}, 200);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        const auto copy = pdfeditor::PdfDocument::open(output).document;
        QVERIFY(copy); QCOMPARE(copy->pageCount(), source->pageCount());
        for (qsizetype page = 0; page < 3; ++page) {
            const auto difference = copy->pageSize(page) - source->pageSize(page);
            QVERIFY(qAbs(difference.width()) < 1 && qAbs(difference.height()) < 1);
            const auto image = copy->renderPage(page).image;
            QVERIFY(!image.isNull());
            const auto pixel = image.pixelColor(image.width() / 2, image.height() / 2);
            if (page == 1) { QCOMPARE(pixel, QColor(Qt::black)); }
            else { const auto original = source->renderPage(page).image; QCOMPARE(pixel, original.pixelColor(original.width() / 2, original.height() / 2)); }
        }
    }
    void rejectsInvalidUnsafeAndExistingDestinations()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("source.pdf");
        const auto output = directory.filePath("new.pdf");
        QVERIFY(testdata::writeVisualPdf(path));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const auto hash = testdata::digest(path);
        QVERIFY(!source->redactedCopy(path, {{0, {0, 0, 1, 1}}}).error.isEmpty());
        QVERIFY(!source->redactedCopy(output, {}).error.isEmpty());
        QVERIFY(!source->redactedCopy(output, {{1, {0, 0, 1, 1}}}).error.isEmpty());
        QVERIFY(!source->redactedCopy(output, {{0, {-0.1, 0, 1, 1}}}).error.isEmpty());
        QVERIFY(!source->redactedCopy(output, {{0, {0, 0, std::numeric_limits<double>::quiet_NaN(), 1}}}).error.isEmpty());
        QVERIFY(!source->redactedCopy(output, {{0, {0, 0, 1, 1}}}, 600).error.isEmpty());
        QVERIFY(!QFileInfo::exists(output));
        QCOMPARE(testdata::digest(path), hash);
        for (auto type : {testdata::VisualPdfExtras::SignatureMarker, testdata::VisualPdfExtras::XfaForm, testdata::VisualPdfExtras::MissingAppearance}) {
            const auto fixture = directory.filePath("restricted.pdf");
            QVERIFY(testdata::writeVisualPdf(fixture, 0, 1, type));
            const auto restricted = pdfeditor::PdfDocument::open(fixture).document;
            QVERIFY(restricted);
            QVERIFY(!restricted->redactedCopy(output, {{0, {0, 0, 1, 1}}}).error.isEmpty());
            QVERIFY(!QFileInfo::exists(output));
        }
        const auto encryptedPath = directory.filePath("encrypted.pdf");
        QVERIFY(source->protectedCopy(encryptedPath, QStringLiteral("user-password"), QStringLiteral("owner-password")).error.isEmpty());
        const auto encrypted = pdfeditor::PdfDocument::open(encryptedPath, QStringLiteral("owner-password")).document;
        QVERIFY(encrypted);
        QVERIFY(!encrypted->redactedCopy(output, {{0, {0, 0, 1, 1}}}).error.isEmpty());
        QVERIFY(!QFileInfo::exists(output));
    }
    void cancellationDiscardsPartialOutput()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("source.pdf");
        const auto output = directory.filePath("copy.pdf");
        QVERIFY(testdata::writeOutputPdf(path));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        int calls = 0;
        const auto result = source->redactedCopy(output, {{0, {0, 0, 1, 1}}}, 150, [&] { return ++calls >= 2; });
        QVERIFY(!result.error.isEmpty()); QVERIFY(calls >= 2);
        QVERIFY(!QFileInfo::exists(output));
        QVERIFY(QDir(directory.path()).entryList({QStringLiteral(".pdf-editor-redacted-*")}, QDir::Files | QDir::Hidden).isEmpty());
    }
    void rejectsExcessivePageAndPixelBudgetsBeforeRendering()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("source.pdf");
        const auto output = directory.filePath("copy.pdf");
        QVERIFY(testdata::writePdf(path, 51));
        auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        QVERIFY(!source->redactedCopy(output, {{0, {0, 0, 1, 1}}}).error.isEmpty());
        QVERIFY(testdata::writePdf(path, 50));
        source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        QVERIFY(!source->redactedCopy(output, {{0, {0, 0, 1, 1}}}, 300).error.isEmpty());
        QVERIFY(testdata::writeVisualPdf(path, 0, 100));
        source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        QVERIFY(!source->redactedCopy(output, {{0, {0, 0, 1, 1}}}).error.isEmpty());
        QVERIFY(!QFileInfo::exists(output));
    }
};
QTEST_MAIN(RedactionTest)
#include "RedactionTest.moc"
