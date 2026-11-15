#include "core/PdfDocument.h"
#include "PdfTestData.h"
#include "VisualPdfData.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

#include <limits>

class PageRenderTest final : public QObject
{
    Q_OBJECT

private slots:
    void rendersCropRotationAndContent_data()
    {
        QTest::addColumn<int>("rotation");
        QTest::addColumn<QPointF>("redCenter");
        QTest::newRow("normal") << 0 << QPointF(25.0 / 160.0, 25.0 / 220.0);
        QTest::newRow("clockwise") << 90 << QPointF(195.0 / 220.0, 25.0 / 160.0);
        QTest::newRow("upside-down") << 180 << QPointF(135.0 / 160.0, 195.0 / 220.0);
        QTest::newRow("counter-clockwise") << 270 << QPointF(25.0 / 220.0, 135.0 / 160.0);
    }

    void rendersCropRotationAndContent()
    {
        QFETCH(int, rotation);
        QFETCH(QPointF, redCenter);
        QTemporaryDir directory;
        const auto path = directory.filePath("visual.pdf");
        QVERIFY(testdata::writeVisualPdf(path, rotation));
        const auto before = testdata::digest(path);
        const auto opened = pdfeditor::PdfDocument::open(path);
        QVERIFY2(opened.document, qPrintable(opened.error));
        const auto result = opened.document->renderPage(0);
        QVERIFY2(!result.image.isNull(), qPrintable(result.error));
        const bool transposed = rotation == 90 || rotation == 270;
        QCOMPARE(result.image.size(), transposed ? QSize(294, 214) : QSize(214, 294));
        const auto red = result.image.pixelColor(qRound(redCenter.x() * (result.image.width() - 1)),
            qRound(redCenter.y() * (result.image.height() - 1)));
        QVERIFY2(red.red() > 230 && red.green() < 30 && red.blue() < 30, qPrintable(red.name()));
        int bluePixels = 0;
        int darkPixels = 0;
        for (int y = 0; y < result.image.height(); ++y) {
            for (int x = 0; x < result.image.width(); ++x) {
                const auto color = result.image.pixelColor(x, y);
                bluePixels += color.blue() > 230 && color.red() < 30 && color.green() < 30;
                darkPixels += color.red() < 50 && color.green() < 50 && color.blue() < 50;
            }
        }
        QVERIFY(bluePixels > 500);
        QVERIFY(darkPixels > 30);
        QCOMPARE(testdata::digest(path), before);
        const auto artifacts = qEnvironmentVariable("PDF_EDITOR_TEST_ARTIFACT_DIR");
        if (!artifacts.isEmpty() && rotation == 0) {
            QVERIFY(QDir().mkpath(artifacts));
            QVERIFY(result.image.save(QDir(artifacts).filePath("rendered-page.png")));
        }
    }

    void respectsZoomDensityAndUserUnits()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("visual.pdf");
        QVERIFY(testdata::writeVisualPdf(path));
        const auto document = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(document);
        const auto normal = document->renderPage(0);
        const auto zoomed = document->renderPage(0, 2.0);
        const auto dense = document->renderPage(0, 1.0, 2.0);
        QVERIFY(!normal.image.isNull() && !zoomed.image.isNull() && !dense.image.isNull());
        QCOMPARE(zoomed.image.size(), dense.image.size());
        QCOMPARE(dense.image.devicePixelRatio(), 2.0);
        QVERIFY(qAbs(normal.image.width() * 2 - dense.image.width()) <= 1);
        QVERIFY(qAbs(normal.image.height() * 2 - dense.image.height()) <= 1);
        const auto unitPath = directory.filePath("units.pdf");
        QVERIFY(testdata::writeVisualPdf(unitPath, 0, 2));
        const auto units = pdfeditor::PdfDocument::open(unitPath).document;
        QVERIFY(units);
        QCOMPARE(units->renderPage(0).image.size(), zoomed.image.size());
    }

    void rejectsInvalidParametersAndExcessiveAllocation()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("visual.pdf");
        QVERIFY(testdata::writeVisualPdf(path));
        const auto document = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(document);
        QVERIFY(!document->renderPage(-1).error.isEmpty());
        QVERIFY(!document->renderPage(1).error.isEmpty());
        for (const auto zoom : {0.0, -1.0, 5.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
            const auto result = document->renderPage(0, zoom);
            QVERIFY(result.image.isNull());
            QVERIFY(!result.error.isEmpty());
        }
        QVERIFY(!document->renderPage(0, 1, 0).error.isEmpty());
        const auto largePath = directory.filePath("large.pdf");
        QVERIFY(testdata::writeVisualPdf(largePath, 0, 100));
        const auto large = pdfeditor::PdfDocument::open(largePath).document;
        QVERIFY(large);
        const auto result = large->renderPage(0);
        QVERIFY(result.image.isNull());
        QVERIFY(!result.error.isEmpty());
    }

    void rendersAnnotationsAndSavedFormAppearances_data()
    {
        QTest::addColumn<int>("rotation");
        QTest::addColumn<qreal>("zoom");
        QTest::newRow("normal") << 0 << 1.0;
        QTest::newRow("rotated") << 90 << 1.0;
        QTest::newRow("thumbnail") << 0 << 0.4;
    }

    void rendersAnnotationsAndSavedFormAppearances()
    {
        QFETCH(int, rotation);
        QFETCH(qreal, zoom);
        QTemporaryDir directory;
        const auto path = directory.filePath("annotated.pdf");
        QVERIFY(testdata::writeVisualPdf(path, rotation, 1, testdata::VisualPdfExtras::AnnotationsAndForm));
        const auto original = testdata::digest(path);
        const auto document = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(document);
        const auto result = document->renderPage(0, zoom);
        QVERIFY2(!result.image.isNull(), qPrintable(result.error));
        QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join('\n')));

        const auto pixelAt = [&result, rotation](qreal x, qreal y) {
            const qreal u = (x - 20) / 160;
            const qreal v = (260 - y) / 220;
            const auto point = rotation == 90 ? QPointF(1 - v, u) : QPointF(u, v);
            return result.image.pixelColor(qRound(point.x() * (result.image.width() - 1)),
                qRound(point.y() * (result.image.height() - 1)));
        };
        const auto note = pixelAt(44, 161);
        QVERIFY2(note.red() > 220 && note.green() > 220 && note.blue() < 80, qPrintable(note.name()));
        const auto highlight = pixelAt(110, 230);
        QVERIFY2(highlight.red() > 220 && highlight.green() > 220 && highlight.blue() < 180, qPrintable(highlight.name()));
        const auto textField = pixelAt(110, 73);
        QVERIFY2(textField.blue() > 230 && textField.red() < 210, qPrintable(textField.name()));
        const auto checked = pixelAt(47, 72);
        QVERIFY2(checked.green() > 90 && checked.green() < 170 && checked.red() < 40, qPrintable(checked.name()));
        QCOMPARE(pixelAt(158, 196), QColor(Qt::white)); // Hidden.
        QCOMPARE(pixelAt(158, 164), QColor(Qt::white)); // NoView.
        QCOMPARE(testdata::digest(path), original);

        const auto artifacts = qEnvironmentVariable("PDF_EDITOR_TEST_ARTIFACT_DIR");
        if (!artifacts.isEmpty() && rotation == 0 && zoom == 1.0) {
            QVERIFY(QDir().mkpath(artifacts));
            QVERIFY(result.image.save(QDir(artifacts).filePath("annotations-page.png")));
        }
    }

    void preservesPageWhenFormAppearanceIsUnavailable()
    {
        QTemporaryDir directory;
        const auto basePath = directory.filePath("base.pdf");
        QVERIFY(testdata::writeVisualPdf(basePath));
        const auto base = pdfeditor::PdfDocument::open(basePath).document;
        QVERIFY(base);
        const auto baseImage = base->renderPage(0).image;
        QVERIFY(!baseImage.isNull());

        using Extras = testdata::VisualPdfExtras;
        for (const auto& [extras, warning] : {
            std::pair{Extras::MissingAppearance, QStringLiteral("apariencia guardada")},
            std::pair{Extras::XfaForm, QStringLiteral("XFA")}}) {
            const auto path = directory.filePath(QStringLiteral("form-%1.pdf").arg(static_cast<int>(extras)));
            QVERIFY(testdata::writeVisualPdf(path, 0, 1, extras));
            const auto document = pdfeditor::PdfDocument::open(path).document;
            QVERIFY(document);
            const auto result = document->renderPage(0);
            QCOMPARE(result.image, baseImage);
            QVERIFY(result.warnings.join(' ').contains(warning));
        }
    }
};

QTEST_MAIN(PageRenderTest)
#include "PageRenderTest.moc"
