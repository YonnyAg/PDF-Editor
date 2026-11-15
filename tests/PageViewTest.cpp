#include "ui/PageView.h"
#include "VisualPdfData.h"
#include "PdfTestData.h"

#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class PageViewTest final : public QObject
{
    Q_OBJECT

private slots:
    void selectsTextWithTheMouseAndDiscardsStaleResults()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("selection.pdf");
        QVERIFY(testdata::writeVisualPdf(path, 90));
        const auto document = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(document);
        pdfeditor::PageView view;
        view.resize(650, 650);
        view.show();
        view.setDocument(document);
        view.setZoom(1.5);
        QTRY_VERIFY(view.hasPageImage() && !view.isRendering());
        auto* image = view.findChild<QWidget*>(QStringLiteral("pageImage"));
        QVERIFY(image);
        const auto bounds = document->findText(QStringLiteral("Editor")).matches.front().highlight.boundingRect();
        const auto pixel = [image](QPointF point) {
            return QPoint(qRound(point.x() * image->width()), qRound(point.y() * image->height()));
        };
        const auto drag = [&] {
            QTest::mousePress(image, Qt::LeftButton, Qt::NoModifier, pixel(bounds.topLeft()));
            QTest::mouseMove(image, pixel(bounds.bottomRight()));
            QTest::mouseRelease(image, Qt::LeftButton, Qt::NoModifier, pixel(bounds.bottomRight()));
        };
        drag();
        QTRY_COMPARE(view.textSelection().text, QStringLiteral("Editor"));
        view.setZoom(2);
        QTRY_VERIFY(!view.isRendering());
        QCOMPARE(view.textSelection().text, QStringLiteral("Editor"));
        QTest::keyClick(&view, Qt::Key_Escape);
        QVERIFY(view.textSelection().text.isEmpty());
        drag();
        view.setDocument(nullptr);
        QTest::qWait(150);
        QVERIFY(view.textSelection().text.isEmpty());
        QVERIFY(!view.hasPageImage());
    }

    void discardsThePreviousPageWhenNavigatingDuringRender()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("mixed-orientation.pdf");
        QVERIFY(testdata::writePdf(path, 2, true));
        const auto document = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(document);
        pdfeditor::PageView view;
        QSignalSpy rendered(&view, &pdfeditor::PageView::pageRendered);
        connect(&view, &pdfeditor::PageView::renderStarted, &view, [&] {
            if (view.currentPage() == 0) { view.setPage(1); }
        });
        view.setDocument(document);
        view.setZoom(0.5);

        QTRY_VERIFY_WITH_TIMEOUT(view.hasPageImage() && !view.isRendering(), 10000);
        QCOMPARE(view.currentPage(), qsizetype{1});
        QCOMPARE(rendered.size(), 1);
        QVERIFY(view.imageSize().width() > view.imageSize().height());
        QCOMPARE(view.zoom(), 0.5);
        view.setPage(-1);
        view.setPage(2);
        QCOMPARE(view.currentPage(), qsizetype{1});
    }

    void zoomsFitsAndRefitsOnResize()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("page.pdf");
        QVERIFY(testdata::writeVisualPdf(path));
        const auto document = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(document);
        pdfeditor::PageView view;
        view.resize(640, 480);
        view.show();
        QSignalSpy rendered(&view, &pdfeditor::PageView::pageRendered);
        QSignalSpy failed(&view, &pdfeditor::PageView::renderFailed);
        view.setDocument(document);
        view.setZoom(1.0);
        QTRY_VERIFY_WITH_TIMEOUT(view.hasPageImage() && !view.isRendering(), 10000);
        const auto normalSize = view.imageSize();
        view.setZoom(2.0);
        QTRY_VERIFY_WITH_TIMEOUT(!view.isRendering() && view.imageSize().width() > normalSize.width(), 10000);
        view.fitToWidth();
        QTRY_VERIFY_WITH_TIMEOUT(!view.isRendering(), 10000);
        QVERIFY(view.fitsWidth());
        QCOMPARE(view.horizontalScrollBar()->maximum(), 0);
        const auto wide = view.imageSize();
        view.resize(400, 480);
        QTRY_VERIFY_WITH_TIMEOUT(!view.isRendering() && view.imageSize().width() < wide.width(), 10000);
        QCOMPARE(view.horizontalScrollBar()->maximum(), 0);
        view.setZoom(20);
        QCOMPARE(view.zoom(), 4.0);
        view.setZoom(0.01);
        QCOMPARE(view.zoom(), 0.1);
        QTRY_VERIFY_WITH_TIMEOUT(!view.isRendering() && view.imageSize().width() < normalSize.width(), 10000);
        QCOMPARE(failed.size(), 0);
        QVERIFY(rendered.size() >= 4);
    }

    void discardsResultsWhenClosedOrReplaced()
    {
        QTemporaryDir directory;
        const auto firstPath = directory.filePath("first.pdf");
        const auto secondPath = directory.filePath("second.pdf");
        QVERIFY(testdata::writeVisualPdf(firstPath));
        QVERIFY(testdata::writeVisualPdf(secondPath, 0, 2));
        const auto first = pdfeditor::PdfDocument::open(firstPath).document;
        const auto second = pdfeditor::PdfDocument::open(secondPath).document;
        QVERIFY(first && second);
        pdfeditor::PageView view;
        QSignalSpy rendered(&view, &pdfeditor::PageView::pageRendered);
        bool closeOnStart = true;
        const auto connection = connect(&view, &pdfeditor::PageView::renderStarted, &view, [&] {
            if (closeOnStart) { closeOnStart = false; view.setDocument(nullptr); }
        });
        view.setDocument(first);
        view.setZoom(1.0);
        QTRY_VERIFY_WITH_TIMEOUT(!closeOnStart && !view.isRendering(), 10000);
        QVERIFY(!view.hasPageImage());
        QCOMPARE(rendered.size(), 0);
        disconnect(connection);

        bool replaceOnStart = true;
        connect(&view, &pdfeditor::PageView::renderStarted, &view, [&] {
            if (replaceOnStart) {
                replaceOnStart = false;
                view.setDocument(second);
                view.setZoom(1.0);
            }
        });
        view.setDocument(first);
        view.setZoom(1.0);
        QTRY_VERIFY_WITH_TIMEOUT(!replaceOnStart && !view.isRendering() && view.hasPageImage(), 10000);
        QCOMPARE(rendered.size(), 1);
        QCOMPARE(view.imageSize(), second->renderPage(0).image.size());
    }
};

QTEST_MAIN(PageViewTest)
#include "PageViewTest.moc"
