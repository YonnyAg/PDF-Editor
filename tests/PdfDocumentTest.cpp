#include "core/PdfDocument.h"
#include "PdfTestData.h"
#include "VisualPdfData.h"

#include <pdfannotation.h>
#include <pdfdocumentbuilder.h>
#include <pdfdocumentreader.h>
#include <pdfdocumentwriter.h>
#include <pdfoutline.h>

#include <QFileInfo>
#include <QFontDatabase>
#include <QDir>
#include <QTemporaryDir>
#include <QTest>
#include <limits>

class PdfDocumentTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
            for (const auto* face : {"arial.ttf", "arialbd.ttf", "ariali.ttf", "arialbi.ttf"}) {
                QVERIFY(QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts/") + QString::fromLatin1(face))) >= 0);
            }
        }
#endif
    }

    void insertsTextAndImagesOnRotatedPages_data()
    {
        QTest::addColumn<int>("rotation");
        for (int angle : {0, 90, 180, 270}) { QTest::newRow(qPrintable(QString::number(angle))) << angle; }
    }

    void decoratesPageRangesAsOneImmutableEdit()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("numbering.pdf");
        QVERIFY(testdata::writeReorderPdf(path));
        const auto digest = testdata::digest(path);
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        pdfeditor::PdfPageDecoration options;
        options.text = QStringLiteral("Folio {n} / {total}");
        options.numbering = true;
        options.firstNumber = 7;
        options.style.pointSize = 8;
        options.style.alignment = Qt::AlignHCenter;
        const auto result = source->decoratePages(1, 2, options);
        QVERIFY2(result.document, qPrintable(result.error));
        QCOMPARE(result.document->findText(QStringLiteral("Folio 7 / 2")).matches.size(), 1);
        QCOMPARE(result.document->findText(QStringLiteral("Folio 8 / 2")).matches.size(), 1);
        QCOMPARE(result.document->findText(QStringLiteral("Folio")).matches.front().page, 1);
        QCOMPARE(result.document->renderPage(0).image, source->renderPage(0).image);
        QCOMPARE(result.document->pageNotes(1).size(), source->pageNotes(1).size());
        QCOMPARE(result.document->bookmarks().size(), source->bookmarks().size());
        const auto saved = result.document->save(pdfeditor::PdfSaveMode::NewCopy, directory.filePath("numbered.pdf"));
        QVERIFY2(saved.document, qPrintable(saved.error));
        const auto reopened = pdfeditor::PdfDocument::open(saved.document->filePath()).document;
        QVERIFY(reopened);
        QCOMPARE(reopened->findText(QStringLiteral("Folio")).matches.size(), 2);
        QCOMPARE(reopened->renderPage(2).image, result.document->renderPage(2).image);
        QVERIFY(source->findText(QStringLiteral("Folio")).matches.isEmpty());
        QCOMPARE(testdata::digest(path), digest);
        QVERIFY(!source->decoratePages(2, 1, options).document);
        options.opacity = std::numeric_limits<double>::quiet_NaN();
        QVERIFY(!source->decoratePages(0, 1, options).document);
        options.opacity = 1;
        options.style.alignment = Qt::AlignJustify;
        QVERIFY(!source->decoratePages(0, 1, options).document);
        options.style.alignment = Qt::AlignLeft;
        options.style.color = QColor(0, 0, 0, 0);
        QVERIFY(!source->decoratePages(0, 1, options).document);
    }

    void styledWatermarksRespectRotation_data() { insertsTextAndImagesOnRotatedPages_data(); }

    void styledWatermarksRespectRotation()
    {
        QFETCH(int, rotation);
        QTemporaryDir directory;
        const auto path = directory.filePath("styled.pdf");
        QVERIFY(testdata::writeVisualPdf(path, rotation, 2, testdata::VisualPdfExtras::AnnotationsAndForm));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        pdfeditor::PdfPageDecoration options;
        options.text = QStringLiteral("Marca visible");
        options.area = QRectF(0.1, 0.2, 0.8, 0.6);
        options.style.pointSize = 14;
        options.style.bold = true;
        options.style.italic = true;
        options.style.underline = true;
        options.style.color = QColor(200, 0, 0);
        options.style.alignment = Qt::AlignHCenter;
        options.opacity = 0.4;
        options.rotation = -20;
        const auto result = source->decoratePages(0, 0, options);
        QVERIFY2(result.document, qPrintable(result.error));
        const auto extracted = result.document->selectText(0, QPointF(0, 0), QPointF(1, 1));
        const auto matches = result.document->findText(QStringLiteral("Marca visible"));
        QVERIFY2(matches.matches.size() == 1, qPrintable(QStringLiteral("Extraído: %1; error: %2; omitidas: %3")
            .arg(extracted.match.text, extracted.error).arg(matches.skippedPages)));
        QCOMPARE(result.document->findText(QStringLiteral("PDF Editor")).matches.size(), 1);
        const auto render = result.document->renderPage(0);
        QVERIFY2(render.error.isEmpty(), qPrintable(render.error));
        QVERIFY2(render.warnings.isEmpty(), qPrintable(render.warnings.join('\n')));
        QVERIFY(render.image != source->renderPage(0).image);
        int changedRedPixels = 0;
        const auto before = source->renderPage(0).image;
        for (int y = 0; y < render.image.height(); ++y) {
            for (int x = 0; x < render.image.width(); ++x) {
                const auto color = render.image.pixelColor(x, y);
                if (color != before.pixelColor(x, y) && color.red() > color.green() + 20 && color.red() > color.blue() + 20) { ++changedRedPixels; }
            }
        }
        QVERIFY(changedRedPixels > 15);
        const auto saved = result.document->save(pdfeditor::PdfSaveMode::NewCopy, directory.filePath("watermark.pdf"));
        QVERIFY2(saved.document, qPrintable(saved.error));
        const auto reopened = pdfeditor::PdfDocument::open(saved.document->filePath()).document;
        QVERIFY(reopened);
        QCOMPARE(reopened->renderPage(0).image, render.image);
        QCOMPARE(reopened->pageFormFields(0).size(), source->pageFormFields(0).size());
    }

    void backgroundWatermarksAndBatchFailurePreserveOriginal()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("opaque.pdf");
        const QByteArray commands = "1 0 0 rg 0 0 200 300 re f\n";
        QVERIFY(testdata::writeSyntheticPdf(path, {
            "<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] /Contents 4 0 R >>",
            "<< /Length " + QByteArray::number(commands.size()) + " >>\nstream\n" + commands + "endstream"
        }));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        pdfeditor::PdfPageDecoration options;
        options.text = QStringLiteral("SELLO");
        options.area = QRectF(0.1, 0.4, 0.8, 0.2);
        options.style.pointSize = 20;
        options.behind = true;
        const auto background = source->decoratePages(0, 0, options);
        QVERIFY2(background.document, qPrintable(background.error));
        QCOMPARE(background.document->renderPage(0).image, source->renderPage(0).image);
        options.behind = false;
        const auto foreground = source->decoratePages(0, 0, options);
        QVERIFY2(foreground.document, qPrintable(foreground.error));
        QVERIFY(foreground.document->renderPage(0).image != source->renderPage(0).image);
        const auto mixed = directory.filePath("mixed.pdf");
        QVERIFY(testdata::writeSyntheticPdf(mixed, {
            "<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 10 10] >>"
        }));
        const auto small = pdfeditor::PdfDocument::open(mixed).document;
        QVERIFY(small);
        const auto failed = small->decoratePages(0, 1, options);
        QVERIFY(!failed.document);
        QVERIFY(!failed.error.isEmpty());
        QVERIFY(small->findText(QStringLiteral("SELLO")).matches.isEmpty());
    }

    void updatesInfoAndXmpWithoutLosingOtherMetadata()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("metadata.pdf");
        const QByteArray xmp = "<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description xmlns:dc='http://purl.org/dc/elements/1.1/' xmlns:custom='urn:custom' dc:title='OLD TITLE' custom:keep='retained'>"
            "<custom:value>Keep this too</custom:value></rdf:Description></rdf:RDF></x:xmpmeta>";
        QVERIFY(testdata::writeSyntheticPdf(path, {
            "<< /Type /Catalog /Pages 2 0 R /Metadata 4 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] >>",
            "<< /Type /Metadata /Subtype /XML /Length " + QByteArray::number(xmp.size()) + " >>\nstream\n" + xmp + "\nendstream"
        }));
        const auto digest = testdata::digest(path);
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const pdfeditor::PdfMetadata values{QStringLiteral("Informe & revisión"), QStringLiteral("José Pérez"), QStringLiteral("Asunto <nuevo>"), QStringLiteral("PDF, ejemplo")};
        const auto result = source->setMetadata(values);
        QVERIFY2(result.document, qPrintable(result.error));
        QCOMPARE(result.document->metadata().title, values.title);
        QCOMPARE(result.document->metadata().author, values.author);
        QCOMPARE(result.document->renderPage(0).image, source->renderPage(0).image);
        const auto output = directory.filePath("saved.pdf");
        QVERIFY(result.document->save(pdfeditor::PdfSaveMode::NewCopy, output).document);
        const auto reopened = pdfeditor::PdfDocument::open(output).document;
        QVERIFY(reopened);
        QCOMPARE(reopened->metadata().subject, values.subject);
        QCOMPARE(reopened->metadata().keywords, values.keywords);
        auto noPassword = [](bool* okay) { *okay = false; return QString{}; };
        pdf::PDFDocumentReader reader(nullptr, noPassword, false, false);
        const auto raw = reader.readFromFile(output);
        const auto& metadata = raw.getObject(raw.getCatalog()->getMetadata());
        QVERIFY(metadata.isStream());
        const auto storedXmp = raw.getDecodedStream(metadata.getStream());
        QVERIFY(storedXmp.contains("retained"));
        QVERIFY(storedXmp.contains("Keep this too"));
        QVERIFY(!storedXmp.contains("OLD TITLE"));
        QVERIFY(storedXmp.contains("Informe &amp;"));
        QCOMPARE(testdata::digest(path), digest);
        auto invalid = values;
        invalid.title = QString(4097, 'x');
        QVERIFY(!source->setMetadata(invalid).document);
        invalid.title = QString(QChar(1));
        QVERIFY(!source->setMetadata(invalid).document);
    }

    void malformedXmpAndProtectedDocumentsRejectNewEdits()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("broken-xmp.pdf");
        QVERIFY(testdata::writeSyntheticPdf(path, {
            "<< /Type /Catalog /Pages 2 0 R /Metadata 4 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] >>",
            "<< /Type /Metadata /Subtype /XML /Length 6 >>\nstream\n<bad/>\nendstream"
        }));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        QVERIFY(!source->setMetadata({QStringLiteral("Title"), {}, {}, {}}).document);
        for (const auto kind : {testdata::VisualPdfExtras::XfaForm, testdata::VisualPdfExtras::SignatureMarker}) {
            const auto protectedPath = directory.filePath(QString::number(int(kind)) + ".pdf");
            QVERIFY(testdata::writeVisualPdf(protectedPath, 0, 1, kind));
            const auto protectedDocument = pdfeditor::PdfDocument::open(protectedPath).document;
            QVERIFY(protectedDocument);
            QVERIFY(!protectedDocument->setMetadata({QStringLiteral("Title"), {}, {}, {}}).document);
            pdfeditor::PdfPageDecoration decoration;
            decoration.text = QStringLiteral("No");
            QVERIFY(!protectedDocument->decoratePages(0, 0, decoration).document);
            QVERIFY(!protectedDocument->addLink(0, QRectF(0.1, 0.1, 0.2, 0.2), 0).document);
        }
    }

    void createsAndDeletesLinksAcrossPageGeometry_data() { insertsTextAndImagesOnRotatedPages_data(); }

    void resolvesMovedDestinationsAndHonorsLockedLinks()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("move-links.pdf");
        QVERIFY(testdata::writeReorderPdf(path));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const auto linked = source->addLink(0, QRectF(0.1, 0.1, 0.3, 0.1), 2);
        QVERIFY2(linked.document, qPrintable(linked.error));
        const auto moved = linked.document->movePage(2, 0);
        QVERIFY2(moved.document, qPrintable(moved.error));
        QCOMPARE(moved.document->pageLinks(1).size(), 1);
        QCOMPARE(moved.document->pageLinks(1)[0].targetPage, 0);
        const auto restrictedPath = directory.filePath("locked-links.pdf");
        QVERIFY(testdata::writeSyntheticPdf(restrictedPath, {
            "<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] /Annots [4 0 R 5 0 R 6 0 R] >>",
            "<< /Type /Annot /Subtype /Link /P 3 0 R /Rect [10 10 40 40] /F 128 /Dest [3 0 R /Fit] >>",
            "<< /Type /Annot /Subtype /Link /P 3 0 R /Rect [50 50 80 80] /A << /S /URI /URI (file:///C:/private.txt) >> >>",
            "<< /Type /Annot /Subtype /Link /P 3 0 R /Rect [90 90 120 120] /A << /S /URI /URI (https://example.com) /Next << /S /JavaScript /JS (alert) >> >> >>"
        }));
        const auto restricted = pdfeditor::PdfDocument::open(restrictedPath).document;
        QVERIFY(restricted);
        const auto links = restricted->pageLinks(0);
        QCOMPARE(links.size(), 3);
        QVERIFY(!links[0].editable);
        QVERIFY(!restricted->deleteLink(0, links[0].index).document);
        QVERIFY(links[1].url.isEmpty() && links[1].targetPage < 0);
        QVERIFY(links[2].url.isEmpty() && links[2].targetPage < 0);
    }

    void createsAndDeletesLinksAcrossPageGeometry()
    {
        QFETCH(int, rotation);
        QTemporaryDir directory;
        const auto path = directory.filePath("links.pdf");
        QVERIFY(testdata::writeVisualPdf(path, rotation, 2, testdata::VisualPdfExtras::AnnotationsAndForm));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const QRectF area(0.1, 0.2, 0.4, 0.15);
        const auto internal = source->addLink(0, area, 0);
        QVERIFY2(internal.document, qPrintable(internal.error));
        const auto internalLinks = internal.document->pageLinks(0);
        QCOMPARE(internalLinks.size(), 1);
        QCOMPARE(internalLinks[0].targetPage, 0);
        QVERIFY(qAbs(internalLinks[0].area.x() - area.x()) < 0.0001);
        QVERIFY(qAbs(internalLinks[0].area.y() - area.y()) < 0.0001);
        QVERIFY(qAbs(internalLinks[0].area.width() - area.width()) < 0.0001);
        QVERIFY(qAbs(internalLinks[0].area.height() - area.height()) < 0.0001);
        const auto web = internal.document->addLink(0, QRectF(0.5, 0.5, 0.3, 0.1), -1, QStringLiteral("https://example.com/informe?q=uno"));
        QVERIFY2(web.document, qPrintable(web.error));
        QCOMPARE(web.document->pageLinks(0).size(), 2);
        QCOMPARE(web.document->pageLinks(0)[1].url, QStringLiteral("https://example.com/informe?q=uno"));
        QCOMPARE(web.document->pageNotes(0).size(), source->pageNotes(0).size());
        QCOMPARE(web.document->pageFormFields(0).size(), source->pageFormFields(0).size());
        const auto saved = web.document->save(pdfeditor::PdfSaveMode::NewCopy, directory.filePath("linked.pdf"));
        QVERIFY2(saved.document, qPrintable(saved.error));
        const auto reopened = pdfeditor::PdfDocument::open(saved.document->filePath()).document;
        QVERIFY(reopened);
        QCOMPARE(reopened->pageLinks(0).size(), 2);
        const auto deleted = reopened->deleteLink(0, reopened->pageLinks(0)[0].index);
        QVERIFY2(deleted.document, qPrintable(deleted.error));
        QCOMPARE(deleted.document->pageLinks(0).size(), 1);
        QCOMPARE(deleted.document->pageLinks(0)[0].url, QStringLiteral("https://example.com/informe?q=uno"));
        QVERIFY(!source->addLink(0, area, -1, QStringLiteral("file:///C:/Windows/test")).document);
        QVERIFY(!source->addLink(0, area, -1, QStringLiteral("javascript:alert(1)")).document);
        QVERIFY(!source->addLink(0, area, -1, QStringLiteral("https://user:password@example.com/")).document);
        QVERIFY(!source->addLink(0, area, 99).document);
        QVERIFY(!source->deleteLink(0, 0).document);
    }

    void insertsTextAndImagesOnRotatedPages()
    {
        QFETCH(int, rotation);
        QTemporaryDir directory;
        const auto path = directory.filePath("overlay.pdf");
        QVERIFY(testdata::writeVisualPdf(path, rotation, 2, testdata::VisualPdfExtras::AnnotationsAndForm));
        const auto digest = testdata::digest(path);
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const auto text = source->insertText(0, QRectF(0.05, 0.55, 0.9, 0.35), QStringLiteral("Nota nueva"), 10);
        QVERIFY2(text.document, qPrintable(text.error));
        QCOMPARE(text.document->findText(QStringLiteral("Nota nueva")).matches.size(), 1);
        QCOMPARE(text.document->findText(QStringLiteral("Editor")).matches.size(), 1);
        QImage image(40, 40, QImage::Format_ARGB32);
        image.fill(QColor(255, 0, 255));
        const auto inserted = text.document->insertImage(0, QRectF(0.2, 0.1, 0.4, 0.3), image);
        QVERIFY2(inserted.document, qPrintable(inserted.error));
        QCOMPARE(inserted.document->pageSize(0), source->pageSize(0));
        QCOMPARE(inserted.document->pageNotes(0).size(), source->pageNotes(0).size());
        QCOMPARE(inserted.document->pageFormFields(0).size(), source->pageFormFields(0).size());
        const auto rendered = inserted.document->renderPage(0);
        QVERIFY2(!rendered.image.isNull(), qPrintable(rendered.error));
        QVERIFY2(rendered.warnings.isEmpty(), qPrintable(rendered.warnings.join('\n')));
        const auto color = rendered.image.pixelColor(qRound(rendered.image.width() * 0.4), qRound(rendered.image.height() * 0.25));
        QVERIFY(color.red() > 220 && color.blue() > 220 && color.green() < 50);
        const auto saved = inserted.document->save(pdfeditor::PdfSaveMode::NewCopy, directory.filePath("inserted.pdf"));
        QVERIFY2(saved.document, qPrintable(saved.error));
        const auto reopened = pdfeditor::PdfDocument::open(saved.document->filePath()).document;
        QVERIFY(reopened);
        QCOMPARE(reopened->findText(QStringLiteral("Nota nueva")).matches.size(), 1);
        QCOMPARE(reopened->renderPage(0).image, rendered.image);
        QCOMPARE(testdata::digest(path), digest);
        QVERIFY(!source->insertText(0, QRectF(0, 0, 0.01, 0.01), QStringLiteral("No cabe"), 144).document);
        QVERIFY(!source->insertText(-1, QRectF(0, 0, 1, 1), QStringLiteral("No")).document);
        QVERIFY(!source->insertImage(0, QRectF(-0.1, 0, 1, 1), image).document);
        QVERIFY(!source->insertImage(0, QRectF(0, 0, 1, 1), {}).document);
    }

    void splitsPendingEditsIntoVerifiedParts()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("split.pdf");
        QVERIFY(testdata::writeReorderPdf(path));
        const auto digest = testdata::digest(path);
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const auto changed = source->rotatePage(1).document;
        QVERIFY(changed);
        const auto target = directory.filePath("parts");
        const auto output = changed->splitPages(2, target);
        QVERIFY2(output.error.isEmpty(), qPrintable(output.error));
        const auto files = QDir(target).entryList({QStringLiteral("*.pdf")}, QDir::Files, QDir::Name);
        QCOMPARE(files.size(), 2);
        const auto first = pdfeditor::PdfDocument::open(QDir(target).filePath(files[0])).document;
        const auto last = pdfeditor::PdfDocument::open(QDir(target).filePath(files[1])).document;
        QVERIFY(first && last);
        QCOMPARE(first->pageCount(), 2);
        QCOMPARE(last->pageCount(), 1);
        QCOMPARE(first->renderPage(1).image, changed->renderPage(1).image);
        QCOMPARE(last->renderPage(0).image, changed->renderPage(2).image);
        QCOMPARE(first->pageNotes(1).size(), changed->pageNotes(1).size());
        QVERIFY(!source->splitPages(1, target).error.isEmpty());
        QCOMPARE(QDir(target).entryList({QStringLiteral("*.pdf")}, QDir::Files).size(), 2);
        QVERIFY(!source->splitPages(0, directory.filePath("invalid")).error.isEmpty());
        QVERIFY(!QFileInfo::exists(directory.filePath("invalid")));
        QCOMPARE(testdata::digest(path), digest);
    }

    void keepsSplitDestinationAbsentWhenAPartFails()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("linked-split.pdf");
        QVERIFY(testdata::writeSyntheticPdf(path, {
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R] /Count 3 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] /Annots [6 0 R] >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] >>",
            "<< /Type /Annot /Subtype /Link /P 4 0 R /Rect [10 10 40 40] /Dest [5 0 R /Fit] >>"
        }));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const auto target = directory.filePath("parts");
        const auto result = source->splitPages(1, target);
        QVERIFY(!result.error.isEmpty());
        QVERIFY(!QFileInfo::exists(target));
        QVERIFY(QDir(directory.path()).entryList({QStringLiteral(".pdf-editor-split-*")}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
    }

    void isolatesOverlayGraphicsWithNonzeroMediaOrigin()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("origin.pdf");
        const QByteArray commands = "1 0 0 rg 100 200 300 300 re f\n2 0 0 2 12 9 cm\n";
        QVERIFY(testdata::writeSyntheticPdf(path, {
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [100 200 400 500] /Resources << >> >>",
            "<< /Type /Page /Parent 2 0 R /CropBox [120 220 380 480] /Rotate 90 /Contents 4 0 R >>",
            "<< /Length " + QByteArray::number(commands.size()) + " >>\nstream\n" + commands + "endstream"
        }));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        QImage image(40, 40, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        { QPainter painter(&image); painter.fillRect(10, 10, 20, 20, Qt::green); }
        const auto inserted = source->insertImage(0, QRectF(0.2, 0.2, 0.4, 0.4), image);
        QVERIFY2(inserted.document, qPrintable(inserted.error));
        const auto rendered = inserted.document->renderPage(0).image;
        QVERIFY(!rendered.isNull());
        const auto center = rendered.pixelColor(qRound(rendered.width() * 0.4), qRound(rendered.height() * 0.4));
        QVERIFY(center.green() > 200 && center.red() < 40);
        const auto transparent = rendered.pixelColor(qRound(rendered.width() * 0.22), qRound(rendered.height() * 0.22));
        QVERIFY(transparent.red() > 200 && transparent.green() < 40);
    }

    void reordersBookmarkBranchesAndChangesTheirLevel()
    {
        using Change = pdfeditor::PdfBookmarkChange;
        QTemporaryDir directory;
        const auto path = directory.filePath("bookmarks.pdf");
        QVERIFY(testdata::writeReorderPdf(path));
        auto document = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(document);
        document = document->changeBookmark(Change::Add, {}, QStringLiteral("Second"), 2).document;
        document = document->changeBookmark(Change::Add, {1}, QStringLiteral("Child"), 0).document;
        QVERIFY(document);
        auto moved = document->changeBookmark(Change::MoveUp, {1});
        QVERIFY2(moved.document, qPrintable(moved.error));
        QCOMPARE(moved.document->bookmarks()[0].title, QStringLiteral("Second"));
        QCOMPARE(moved.document->bookmarks()[1].title, QStringLiteral("Child"));
        moved = moved.document->changeBookmark(Change::MoveDown, {0});
        QVERIFY(moved.document);
        QCOMPARE(moved.document->bookmarks()[0].title, QStringLiteral("Page B"));
        moved = moved.document->changeBookmark(Change::Indent, {1});
        QVERIFY2(moved.document, qPrintable(moved.error));
        QCOMPARE(moved.document->bookmarks()[1].path, (QList<qsizetype>{0, 0}));
        QCOMPARE(moved.document->bookmarks()[2].path, (QList<qsizetype>{0, 0, 0}));
        moved = moved.document->changeBookmark(Change::Outdent, {0, 0});
        QVERIFY2(moved.document, qPrintable(moved.error));
        QCOMPARE(moved.document->bookmarks()[1].path, (QList<qsizetype>{1}));
        QCOMPARE(moved.document->bookmarks()[2].page, 0);
        QVERIFY(!moved.document->changeBookmark(Change::MoveUp, {0}).document);
        QVERIFY(!moved.document->changeBookmark(Change::MoveDown, {1}).document);
        QVERIFY(!moved.document->changeBookmark(Change::Outdent, {0}).document);
        QVERIFY(!moved.document->changeBookmark(Change::Indent, {0}).document);
        const auto saved = moved.document->save(pdfeditor::PdfSaveMode::NewCopy, directory.filePath("ordered.pdf"));
        QVERIFY2(saved.document, qPrintable(saved.error));
        QCOMPARE(pdfeditor::PdfDocument::open(saved.document->filePath()).document->bookmarks()[2].title, QStringLiteral("Child"));
    }

    void cropsAndRestoresRotatedPages_data()
    {
        QTest::addColumn<int>("rotation");
        QTest::addColumn<int>("unit");
        for (int angle : {0, 90, 180, 270}) {
            for (int unit : {1, 2}) {
                QTest::newRow(qPrintable(QStringLiteral("%1-unit%2").arg(angle).arg(unit))) << angle << unit;
            }
        }
    }

    void cropsAndRestoresRotatedPages()
    {
        QFETCH(int, rotation);
        QFETCH(int, unit);
        QTemporaryDir directory;
        const auto path = directory.filePath("original.pdf");
        const auto output = directory.filePath("cropped.pdf");
        QVERIFY(testdata::writeVisualPdf(path, rotation, unit, testdata::VisualPdfExtras::AnnotationsAndForm));
        const auto digest = testdata::digest(path);
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const auto cropped = source->cropPages(0, 0, QMarginsF(0.1, 0.2, 0.15, 0.1));
        QVERIFY2(cropped.document, qPrintable(cropped.error));
        QVERIFY(qAbs(cropped.document->pageSize(0).width() - source->pageSize(0).width() * 0.75) < 0.00001);
        QVERIFY(qAbs(cropped.document->pageSize(0).height() - source->pageSize(0).height() * 0.7) < 0.00001);
        QCOMPARE(cropped.document->pageNotes(0).size(), source->pageNotes(0).size());
        QCOMPARE(cropped.document->pageFormFields(0).size(), source->pageFormFields(0).size());
        const auto rendered = cropped.document->renderPage(0);
        QVERIFY2(!rendered.image.isNull(), qPrintable(rendered.error));
        const auto saved = cropped.document->save(pdfeditor::PdfSaveMode::NewCopy, output);
        QVERIFY2(saved.document, qPrintable(saved.error));
        const auto reopened = pdfeditor::PdfDocument::open(output).document;
        QVERIFY(reopened);
        QVERIFY(qAbs(reopened->pageSize(0).width() - cropped.document->pageSize(0).width()) < 0.00001);
        const auto noPassword = [](bool* accepted) { *accepted = false; return QString{}; };
        pdf::PDFDocumentReader reader(nullptr, noPassword, false, false);
        const auto raw = reader.readFromFile(output);
        QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);
        const QRectF expected[] = {QRectF(36, 62, 120, 154), QRectF(52, 62, 112, 165),
            QRectF(44, 84, 120, 154), QRectF(36, 73, 112, 165)};
        const auto actual = raw.getCatalog()->getPage(0)->getCropBox();
        const auto difference = actual.topLeft() - expected[rotation / 90].topLeft();
        QVERIFY(qAbs(difference.x()) < 0.00001 && qAbs(difference.y()) < 0.00001);
        QCOMPARE(raw.getCatalog()->getPage(0)->getMediaBox(), QRectF(0, 0, 200, 300));
        const auto restored = reopened->resetPageCrop(0, 0);
        QVERIFY2(restored.document, qPrintable(restored.error));
        const auto physical = (rotation == 90 || rotation == 270) ? QSizeF(300, 200) : QSizeF(200, 300);
        const auto expectedSize = physical * unit * (96.0 / 72.0);
        QCOMPARE(restored.document->pageSize(0), expectedSize);
        QCOMPARE(restored.document->findText(QStringLiteral("Editor")).matches.size(), 1);
        QVERIFY(restored.document->save(pdfeditor::PdfSaveMode::NewCopy, directory.filePath("restored.pdf")).document);
        QCOMPARE(testdata::digest(path), digest);
    }

    void cropsRangesAtomicallyAndRejectsInvalidMargins()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("range.pdf");
        QVERIFY(testdata::writeReorderPdf(path));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const auto changed = source->cropPages(1, 2, QMarginsF(0.1, 0.1, 0.1, 0.1));
        QVERIFY2(changed.document, qPrintable(changed.error));
        QCOMPARE(changed.document->pageCount(), 3);
        QCOMPARE(changed.document->pageSize(0), source->pageSize(0));
        for (qsizetype index : {1, 2}) {
            QVERIFY(qAbs(changed.document->pageSize(index).width() - source->pageSize(index).width() * 0.8) < 0.00001);
        }
        QCOMPARE(changed.document->bookmarks()[0].page, 1);
        QCOMPARE(changed.document->pageNotes(1).size(), source->pageNotes(1).size());
        QVERIFY(!source->cropPages(-1, 1, QMarginsF(0.1, 0, 0, 0)).document);
        QVERIFY(!source->cropPages(0, 3, QMarginsF(0.1, 0, 0, 0)).document);
        QVERIFY(!source->resetPageCrop(2, 1).document);
        for (const auto& margins : {QMarginsF{}, QMarginsF(-0.1, 0, 0, 0), QMarginsF(0.5, 0, 0.5, 0),
                QMarginsF(0, 0.6, 0, 0.5), QMarginsF(std::numeric_limits<double>::quiet_NaN(), 0, 0, 0),
                QMarginsF(0, std::numeric_limits<double>::infinity(), 0, 0)}) {
            QVERIFY(!source->cropPages(0, 1, margins).document);
        }
        const auto protectedPath = directory.filePath("signed.pdf");
        QVERIFY(testdata::writeVisualPdf(protectedPath, 0, 1, testdata::VisualPdfExtras::SignatureMarker));
        const auto signedDocument = pdfeditor::PdfDocument::open(protectedPath).document;
        QVERIFY(signedDocument);
        QVERIFY(!signedDocument->cropPages(0, 0, QMarginsF(0.1, 0, 0, 0)).document);
        QVERIFY(!signedDocument->resetPageCrop(0, 0).document);
    }

    void createsEditsAndDeletesNestedBookmarks()
    {
        using Change = pdfeditor::PdfBookmarkChange;
        QTemporaryDir directory;
        const auto path = directory.filePath("bookmarks.pdf");
        const auto copy = directory.filePath("edited-bookmarks.pdf");
        QVERIFY(testdata::writeReorderPdf(path));
        const auto digest = testdata::digest(path);
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        auto added = source->changeBookmark(Change::Add, {}, QStringLiteral("Conclusión técnica"), 2);
        QVERIFY2(added.document, qPrintable(added.error));
        QCOMPARE(added.document->bookmarks().size(), 2);
        QCOMPARE(added.document->bookmarks()[1].path, QList<qsizetype>{1});
        auto nested = added.document->changeBookmark(Change::Add, {1}, QStringLiteral("Detalle"), 0);
        QVERIFY2(nested.document, qPrintable(nested.error));
        QCOMPARE(nested.document->bookmarks()[2].path, (QList<qsizetype>{1, 0}));
        auto renamed = nested.document->changeBookmark(Change::Rename, {1, 0}, QStringLiteral("  Detalle revisado  "));
        QVERIFY2(renamed.document, qPrintable(renamed.error));
        QCOMPARE(renamed.document->bookmarks()[2].title, QStringLiteral("Detalle revisado"));
        auto redirected = renamed.document->changeBookmark(Change::Retarget, {1}, {}, 1);
        QVERIFY2(redirected.document, qPrintable(redirected.error));
        QCOMPARE(redirected.document->bookmarks()[1].page, 1);
        auto saved = redirected.document->save(pdfeditor::PdfSaveMode::NewCopy, copy);
        QVERIFY2(saved.document, qPrintable(saved.error));
        const auto reopened = pdfeditor::PdfDocument::open(copy).document;
        QVERIFY(reopened);
        QCOMPARE(reopened->bookmarks()[2].title, QStringLiteral("Detalle revisado"));
        QCOMPARE(reopened->bookmarks()[1].page, 1);
        auto removed = reopened->changeBookmark(Change::Delete, {1});
        QVERIFY2(removed.document, qPrintable(removed.error));
        QCOMPARE(removed.document->bookmarks().size(), 1);
        QCOMPARE(removed.document->bookmarks()[0].title, QStringLiteral("Page B"));
        removed = removed.document->changeBookmark(Change::Delete, {0});
        QVERIFY(removed.document && removed.document->bookmarks().isEmpty());
        const auto emptySaved = removed.document->save(pdfeditor::PdfSaveMode::NewCopy, directory.filePath("without-bookmarks.pdf"));
        QVERIFY2(emptySaved.document, qPrintable(emptySaved.error));
        QVERIFY(pdfeditor::PdfDocument::open(emptySaved.document->filePath()).document->bookmarks().isEmpty());
        QCOMPARE(testdata::digest(path), digest);
        for (qsizetype page = 0; page < source->pageCount(); ++page) {
            QCOMPARE(removed.document->renderPage(page).image, source->renderPage(page).image);
        }
        QVERIFY(!source->changeBookmark(Change::Rename, {}, QStringLiteral("Root")).document);
        QVERIFY(!source->changeBookmark(Change::Delete, {8}).document);
        QVERIFY(!source->changeBookmark(Change::Retarget, {0}, {}, -1).document);
        QVERIFY(!source->changeBookmark(Change::Add, {}, QStringLiteral("  "), 0).document);
        QVERIFY(!source->changeBookmark(Change::Add, {}, QString(4097, u'x'), 0).document);
        const auto plain = directory.filePath("plain.pdf");
        QVERIFY(testdata::writeVisualPdf(plain));
        const auto first = pdfeditor::PdfDocument::open(plain).document->changeBookmark(Change::Add, {}, QStringLiteral("Inicio"), 0);
        QVERIFY2(first.document, qPrintable(first.error));
        QCOMPARE(first.document->bookmarks().front().page, 0);
    }

    void rejectsMalformedAndSignedBookmarkEdits()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("cyclic-bookmarks.pdf");
        QVERIFY(testdata::writeSyntheticPdf(path, {
            "<< /Type /Catalog /Pages 2 0 R /Outlines 4 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] >>",
            "<< /Type /Outlines /First 5 0 R /Last 5 0 R >>",
            "<< /Title (Cycle) /Parent 4 0 R /Next 5 0 R /Dest [3 0 R /Fit] >>"
        }));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        QVERIFY(!source->changeBookmark(pdfeditor::PdfBookmarkChange::Rename, {0}, QStringLiteral("Renamed")).document);
        const auto signedPath = directory.filePath("signed.pdf");
        QVERIFY(testdata::writeVisualPdf(signedPath, 0, 1, testdata::VisualPdfExtras::SignatureMarker));
        const auto signedDocument = pdfeditor::PdfDocument::open(signedPath).document;
        QVERIFY(signedDocument);
        QVERIFY(!signedDocument->changeBookmark(pdfeditor::PdfBookmarkChange::Add, {}, QStringLiteral("New"), 0).document);
    }

    void preservesRawBookmarkActionsAndClosedGroups()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("external-bookmark.pdf");
        QVERIFY(testdata::writeSyntheticPdf(path, {
            "<< /Type /Catalog /Pages 2 0 R /Outlines 4 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] >>",
            "<< /Type /Outlines /First 5 0 R /Last 5 0 R /Count 1 >>",
            "<< /Title (Closed group) /Parent 4 0 R /First 6 0 R /Last 6 0 R /Count -1 /C [1 0 0] /F 3 /SE 8 0 R /Custom (preserve) >>",
            "<< /Title (External) /Parent 5 0 R /A 7 0 R >>",
            "<< /S /URI /URI (https://example.com) /Next << /S /Named /N /NextPage >> >>",
            "<< /Type /StructElem /S /Sect >>"
        }));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const auto changed = source->changeBookmark(pdfeditor::PdfBookmarkChange::Rename, {0}, QStringLiteral("Grupo"));
        QVERIFY2(changed.document, qPrintable(changed.error));
        const auto output = directory.filePath("preserved.pdf");
        QVERIFY(changed.document->save(pdfeditor::PdfSaveMode::NewCopy, output).document);
        const auto noPassword = [](bool* accepted) { *accepted = false; return QString{}; };
        pdf::PDFDocumentReader reader(nullptr, noPassword, false, false);
        const auto document = reader.readFromFile(output);
        QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);
        const auto* catalog = document.getDictionaryFromObject(document.getStorage().getTrailerDictionary().getDictionary()->get("Root"));
        const auto* outline = document.getDictionaryFromObject(catalog->get("Outlines"));
        const auto* group = document.getDictionaryFromObject(outline->get("First"));
        QCOMPARE(group->get("Count").getInteger(), -1);
        QCOMPARE(group->get("F").getInteger(), 3);
        QVERIFY(group->hasKey("SE") && group->hasKey("Custom") && group->hasKey("C"));
        const auto* child = document.getDictionaryFromObject(group->get("First"));
        const auto* action = document.getDictionaryFromObject(child->get("A"));
        QCOMPARE(action->get("S").getString(), QByteArray("URI"));
        QCOMPARE(action->get("URI").getString(), QByteArray("https://example.com"));
        QVERIFY(action->hasKey("Next"));
    }

    void selectsTextOnRotatedPages_data()
    {
        QTest::addColumn<int>("rotation");
        for (const int angle : {0, 90, 180, 270}) {
            QTest::newRow(qPrintable(QString::number(angle))) << angle;
        }
    }

    void selectsTextOnRotatedPages()
    {
        QFETCH(int, rotation);
        QTemporaryDir directory;
        const auto path = directory.filePath("select.pdf");
        QVERIFY(testdata::writeVisualPdf(path, rotation, 2));
        const auto document = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(document && document->canCopyText());
        const auto bounds = document->findText(QStringLiteral("Editor")).matches.front().highlight.boundingRect();
        const auto selected = document->selectText(0, bounds.topLeft(), bounds.bottomRight());
        QVERIFY2(selected.error.isEmpty(), qPrintable(selected.error));
        QCOMPARE(selected.match.text, QStringLiteral("Editor"));
        QCOMPARE(selected.match.quadrilaterals.size(), 4);
        QVERIFY(document->markMatch(selected.match, pdfeditor::PdfTextMarkup::Underline).document);
        const auto reverse = document->selectText(0, bounds.bottomRight(), bounds.topLeft());
        QCOMPARE(reverse.match.text, selected.match.text);
        QCOMPARE(reverse.match.highlight, selected.match.highlight);
        const bool horizontal = bounds.width() > bounds.height();
        const auto first = horizontal ? QPointF(bounds.left(), bounds.center().y()) : QPointF(bounds.center().x(), bounds.top());
        const auto last = horizontal ? QPointF(bounds.right(), bounds.center().y()) : QPointF(bounds.center().x(), bounds.bottom());
        QCOMPARE(document->selectText(0, first, last).match.text, QStringLiteral("Editor"));
        QVERIFY(document->selectText(-1, {}, {1, 1}).error.size() > 0);
        QVERIFY(document->selectText(0, {-1, 0}, {1, 1}).error.size() > 0);
        QVERIFY(document->selectText(0, {0.9, 0.9}, {1, 1}).match.text.isEmpty());
    }

    void resolvesBookmarksAfterPageEdits()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("bookmarks.pdf");
        QVERIFY(testdata::writeReorderPdf(path));
        const auto document = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(document);
        const auto bookmarks = document->bookmarks();
        QCOMPARE(bookmarks.size(), 1);
        QCOMPARE(bookmarks[0].title, QStringLiteral("Page B"));
        QCOMPARE(bookmarks[0].page, 1);
        const auto moved = document->movePage(1, 0);
        QVERIFY(moved.document);
        QCOMPARE(moved.document->bookmarks()[0].page, 0);
        const auto deleted = document->changePage(1, pdfeditor::PdfPageOperation::Delete);
        QVERIFY2(deleted.document, qPrintable(deleted.error));
        for (const auto& bookmark : deleted.document->bookmarks()) { QVERIFY(bookmark.page != 1); }
    }

    void selectsSeveralLinesWithoutLosingLineBreaks()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("lines.pdf");
        const QByteArray commands = "BT /F1 16 Tf 36 240 Td (Primera linea) Tj 0 -30 Td (Segunda linea) Tj ET\n";
        QVERIFY(testdata::writeSyntheticPdf(path, {
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
            "<< /Length " + QByteArray::number(commands.size()) + " >>\nstream\n" + commands + "endstream",
            "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"
        }));
        const auto document = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(document);
        const auto selected = document->selectText(0, {0.1, 0.1}, {0.95, 0.5});
        QVERIFY2(selected.error.isEmpty(), qPrintable(selected.error));
        QCOMPARE(selected.match.text, QStringLiteral("Primera linea\nSegunda linea"));
        QCOMPARE(selected.match.quadrilaterals.size(), 8);
        const auto marked = document->markMatch(selected.match, pdfeditor::PdfTextMarkup::Highlight);
        QVERIFY2(marked.document, qPrintable(marked.error));
        QCOMPARE(marked.document->pageTextMarks(0).front().text, selected.match.text);
    }

    void resolvesNestedNamedBookmarksAndIgnoresExternalActions()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("named-bookmarks.pdf");
        QVERIFY(testdata::writeSyntheticPdf(path, {
            "<< /Type /Catalog /Pages 2 0 R /Outlines 4 0 R /Names << /Dests << /Names [(target) [3 0 R /Fit] (cycle) (cycle)] >> >> >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] >>",
            "<< /Type /Outlines /First 5 0 R /Last 8 0 R /Count 4 >>",
            "<< /Title (Chapter) /Parent 4 0 R /First 6 0 R /Last 6 0 R /Count 1 /Next 7 0 R >>",
            "<< /Title (Named page) /Parent 5 0 R /Dest (target) >>",
            "<< /Title (External) /Parent 4 0 R /Prev 5 0 R /Next 8 0 R /A << /S /URI /URI (https://example.com) >> >>",
            "<< /Title (Cycle) /Parent 4 0 R /Prev 7 0 R /Dest (cycle) >>"
        }));
        const auto document = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(document);
        const auto bookmarks = document->bookmarks();
        QCOMPARE(bookmarks.size(), 4);
        QCOMPARE(bookmarks[0].page, -1);
        QCOMPARE(bookmarks[1].page, 0);
        QCOMPARE(bookmarks[1].depth, 1);
        QCOMPARE(bookmarks[2].page, -1);
        QCOMPARE(bookmarks[2].depth, 0);
        QCOMPARE(bookmarks[3].page, -1);
    }

    void savesAndDeletesTextMarksOnRotatedPages_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<int>("rotation");
        for (const auto kind : {pdfeditor::PdfTextMarkup::Highlight, pdfeditor::PdfTextMarkup::Underline,
                 pdfeditor::PdfTextMarkup::StrikeOut, pdfeditor::PdfTextMarkup::Squiggly}) {
            for (const int rotation : {0, 90, 180, 270}) {
                const auto name = QByteArray::number(static_cast<int>(kind)) + "-" + QByteArray::number(rotation);
                QTest::newRow(name.constData()) << static_cast<int>(kind) << rotation;
            }
        }
    }

    void savesAndDeletesTextMarksOnRotatedPages()
    {
        QFETCH(int, kind);
        QFETCH(int, rotation);
        const auto type = static_cast<pdfeditor::PdfTextMarkup>(kind);
        QTemporaryDir directory;
        const auto path = directory.filePath("source.pdf");
        const auto copy = directory.filePath("marked.pdf");
        QVERIFY(testdata::writeVisualPdf(path, rotation));
        const auto digest = testdata::digest(path);
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const auto matches = source->findText(QStringLiteral("Editor"));
        QCOMPARE(matches.matches.size(), 1);
        const auto& match = matches.matches[0];
        QCOMPARE(match.text, QStringLiteral("Editor"));
        QCOMPARE(match.quadrilaterals.size(), 4);
        QVERIFY(match.quadrilaterals[0].y() < match.quadrilaterals[2].y());
        const auto marked = source->markMatch(match, type);
        QVERIFY2(marked.document, qPrintable(marked.error));
        const auto rendered = marked.document->renderPage(0);
        QVERIFY2(!rendered.image.isNull(), qPrintable(rendered.error));
        QVERIFY2(rendered.warnings.isEmpty(), qPrintable(rendered.warnings.join('\n')));
        QVERIFY(rendered.image != source->renderPage(0).image);
        const auto saved = marked.document->save(pdfeditor::PdfSaveMode::NewCopy, copy);
        QVERIFY2(saved.document, qPrintable(saved.error));
        const auto reopened = pdfeditor::PdfDocument::open(copy).document;
        QVERIFY(reopened);
        const auto marks = reopened->pageTextMarks(0);
        QCOMPARE(marks.size(), 1);
        QCOMPARE(marks[0].type, type);
        QCOMPARE(marks[0].text, QStringLiteral("Editor"));
        QVERIFY(marks[0].editable);
        const auto actual = marks[0].area.boundingRect();
        const auto expected = match.highlight.boundingRect();
        QVERIFY(qAbs(actual.center().x() - expected.center().x()) < 0.005);
        QVERIFY(qAbs(actual.center().y() - expected.center().y()) < 0.005);
        QCOMPARE(reopened->renderPage(0).image, rendered.image);
        const auto removed = reopened->deleteTextMark(0, marks[0].index);
        QVERIFY2(removed.document, qPrintable(removed.error));
        QVERIFY(removed.document->pageTextMarks(0).isEmpty());
        QCOMPARE(removed.document->renderPage(0).image, source->renderPage(0).image);
        QCOMPARE(testdata::digest(path), digest);
    }

    void orientsMarksAlongAngledText()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("angled.pdf");
        const QByteArray commands = "BT /F1 18 Tf 0.8660254 0.5 -0.5 0.8660254 50 80 Tm (Revision) Tj ET\n";
        QVERIFY(testdata::writeSyntheticPdf(path, {
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 240 220] /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
            "<< /Length " + QByteArray::number(commands.size()) + " >>\nstream\n" + commands + "endstream",
            "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"
        }));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const auto selection = source->selectText(0, {0, 0}, {1, 1});
        QCOMPARE(selection.match.text, QStringLiteral("Revision"));
        const auto found = source->findText(QStringLiteral("Revision"));
        QCOMPARE(found.matches.size(), 1);
        const auto& quads = found.matches[0].quadrilaterals;
        QCOMPARE(quads.size(), 4);
        const auto baseline = quads[1] - quads[0];
        QVERIFY(baseline.x() > 0);
        QVERIFY(qAbs(baseline.y() / baseline.x() - 0.577350269) < 0.01);
        const auto marked = source->markMatch(found.matches[0], pdfeditor::PdfTextMarkup::Underline);
        QVERIFY2(marked.document, qPrintable(marked.error));
        QVERIFY(marked.document->renderPage(0).image != source->renderPage(0).image);
    }

    void respectsLockedMarksAndAnnotationReplies()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("protected-marks.pdf");
        QVERIFY(testdata::writeSyntheticPdf(path, {
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 200 200] >>",
            "<< /Type /Page /Parent 2 0 R /Annots [4 0 R 5 0 R 6 0 R 7 0 R 8 0 R] >>",
            "<< /Type /Annot /Subtype /Underline /Rect [20 40 80 60] /QuadPoints [20 40 80 40 20 60 80 60] /F 128 >>",
            "<< /Type /Annot /Subtype /Highlight /Rect [20 80 80 100] /Contents (With reply) >>",
            "<< /Type /Annot /Subtype /Text /Rect [90 80 110 100] /IRT 5 0 R /Contents (Reply) >>",
            "<< /Type /Annot /Subtype /StrikeOut /Rect [20 120 80 140] /Popup 8 0 R >>",
            "<< /Type /Annot /Subtype /Popup /Rect [90 120 160 180] /Parent 7 0 R >>"
        }));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const auto marks = source->pageTextMarks(0);
        QCOMPARE(marks.size(), 3);
        QVERIFY(!marks[0].editable);
        QVERIFY(!source->deleteTextMark(0, marks[0].index).document);
        const auto replies = source->deleteTextMark(0, marks[1].index);
        QVERIFY(!replies.document && replies.error.contains(QStringLiteral("respuestas")));
        const auto removed = source->deleteTextMark(0, marks[2].index);
        QVERIFY2(removed.document, qPrintable(removed.error));
        QCOMPARE(removed.document->pageTextMarks(0).size(), 2);
        const auto copy = directory.filePath("removed.pdf");
        QVERIFY(removed.document->save(pdfeditor::PdfSaveMode::NewCopy, copy).document);
        QCOMPARE(pdfeditor::PdfDocument::open(copy).document->pageTextMarks(0).size(), 2);
    }

    void importsIntervalsWithIndependentNotesAndPreservesExistingPages_data()
    {
        QTest::addColumn<int>("position");
        QTest::newRow("beginning") << 0;
        QTest::newRow("middle") << 1;
        QTest::newRow("end") << 3;
    }

    void importsIntervalsWithIndependentNotesAndPreservesExistingPages()
    {
        QFETCH(int, position);
        QTemporaryDir directory;
        const auto targetPath = directory.filePath("target.pdf");
        const auto sourcePath = directory.filePath("source.pdf");
        const auto resultPath = directory.filePath("result.pdf");
        QVERIFY(testdata::writeOutputPdf(targetPath));
        QVERIFY(testdata::writeReorderPdf(sourcePath));
        const auto targetDigest = testdata::digest(targetPath);
        const auto sourceDigest = testdata::digest(sourcePath);
        const auto target = pdfeditor::PdfDocument::open(targetPath).document;
        const auto source = pdfeditor::PdfDocument::open(sourcePath).document;
        QVERIFY(target && source);
        const auto pending = target->rotatePage(1).document;
        const auto noted = source->addNote(1, {0.4, 0.4}, QStringLiteral("Nota de origen")).document;
        QVERIFY(pending && noted);
        const auto result = pending->insertPages(position, {{noted, 1, 2}});
        QVERIFY2(result.document, qPrintable(result.error));
        QCOMPARE(result.document->pageCount(), qsizetype{5});
        QCOMPARE(result.document->filePath(), targetPath);
        for (qsizetype index = 0; index < result.document->pageCount(); ++index) {
            const auto expected = index >= position && index < position + 2
                ? noted->renderPage(index - position + 1)
                : pending->renderPage(index < position ? index : index - 2);
            const auto actual = result.document->renderPage(index);
            QVERIFY2(!actual.image.isNull(), qPrintable(actual.error));
            QCOMPARE(actual.image, expected.image);
        }
        const auto notes = result.document->pageNotes(position);
        QCOMPARE(notes.size(), 2);
        const auto edited = result.document->editNote(position, notes[1].index, QStringLiteral("Nota importada"));
        QVERIFY2(edited.document, qPrintable(edited.error));
        QCOMPARE(noted->pageNotes(1)[1].text, QStringLiteral("Nota de origen"));
        const auto saved = edited.document->save(pdfeditor::PdfSaveMode::NewCopy, resultPath);
        QVERIFY2(saved.document, qPrintable(saved.error));
        const auto reopened = pdfeditor::PdfDocument::open(resultPath).document;
        QVERIFY(reopened);
        QCOMPARE(reopened->pageCount(), qsizetype{5});
        QCOMPARE(reopened->pageNotes(position)[1].text, QStringLiteral("Nota importada"));
        for (qsizetype index = 0; index < reopened->pageCount(); ++index) {
            QCOMPARE(reopened->renderPage(index).image, edited.document->renderPage(index).image);
        }
        QCOMPARE(testdata::digest(targetPath), targetDigest);
        QCOMPARE(testdata::digest(sourcePath), sourceDigest);
    }

    void importsNamedLinksAndRejectsReferencesOutsideTheInterval()
    {
        QTemporaryDir directory;
        const auto targetPath = directory.filePath("target.pdf");
        const auto sourcePath = directory.filePath("linked.pdf");
        const auto resultPath = directory.filePath("result.pdf");
        QVERIFY(testdata::writeVisualPdf(targetPath));
        QVERIFY(testdata::writeSyntheticPdf(sourcePath, {
            "<< /Type /Catalog /Pages 2 0 R /Names << /Dests << /Names [(same-name) [4 0 R /Fit]] >> >> >>",
            "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 /MediaBox [0 0 120 180] >>",
            "<< /Type /Page /Parent 2 0 R /Annots [5 0 R 6 0 R] >>",
            "<< /Type /Page /Parent 2 0 R >>",
            "<< /Type /Annot /Subtype /Link /P 3 0 R /Rect [5 5 25 25] /Dest (same-name) >>",
            "<< /Type /Annot /Subtype /Link /P 3 0 R /Rect [30 5 50 25] /A << /S /GoTo /D (same-name) >> >>"
        }));
        const auto target = pdfeditor::PdfDocument::open(targetPath).document;
        const auto source = pdfeditor::PdfDocument::open(sourcePath).document;
        QVERIFY(target && source);
        const auto rejected = target->insertPages(0, {{source, 0, 0}});
        QVERIFY(!rejected.document);
        QVERIFY(rejected.error.contains(QStringLiteral("referencias")));
        const auto result = target->insertPages(0, {{source}});
        QVERIFY2(result.document, qPrintable(result.error));
        const auto saved = result.document->save(pdfeditor::PdfSaveMode::NewCopy, resultPath);
        QVERIFY2(saved.document, qPrintable(saved.error));
        const auto cancelPassword = [](bool* accepted) { *accepted = false; return QString{}; };
        pdf::PDFDocumentReader reader(nullptr, cancelPassword, false, false);
        const auto stored = reader.readFromFile(resultPath);
        QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);
        const auto* catalog = stored.getCatalog();
        const auto& annotations = catalog->getPage(0)->getAnnotations();
        QCOMPARE(annotations.size(), size_t{2});
        const auto* first = stored.getDictionaryFromObject(pdf::PDFObject::createReference(annotations[0]));
        const auto* second = stored.getDictionaryFromObject(pdf::PDFObject::createReference(annotations[1]));
        QVERIFY(first && second);
        const auto* action = stored.getDictionaryFromObject(second->get("A"));
        QVERIFY(action);
        for (const auto& object : {first->get("Dest"), action->get("D")}) {
            const auto destination = pdf::PDFDestination::parse(&stored.getStorage(), object);
            QVERIFY(destination.isValid() && !destination.isNamedDestination());
            QCOMPARE(catalog->getPageIndexFromPageReference(destination.getPageReference()), size_t{1});
        }
    }

    void mergesMultipleSourcesAtomicallyAndCanCopyItsOwnPages()
    {
        QTemporaryDir directory;
        const auto targetPath = directory.filePath("target.pdf");
        const auto sourcePath = directory.filePath("source.pdf");
        const auto formPath = directory.filePath("form.pdf");
        QVERIFY(testdata::writeOutputPdf(targetPath));
        QVERIFY(testdata::writeVisualPdf(sourcePath));
        QVERIFY(testdata::writeVisualPdf(formPath, 0, 1, testdata::VisualPdfExtras::AnnotationsAndForm));
        const auto target = pdfeditor::PdfDocument::open(targetPath).document;
        const auto source = pdfeditor::PdfDocument::open(sourcePath).document;
        const auto form = pdfeditor::PdfDocument::open(formPath).document;
        QVERIFY(target && source && form);
        const auto digest = testdata::digest(targetPath);
        QVERIFY(!target->insertPages(-1, {{source}}).document);
        QVERIFY(!target->insertPages(4, {{source}}).document);
        QVERIFY(!target->insertPages(0, {}).document);
        QVERIFY(!target->insertPages(0, {{nullptr}}).document);
        QVERIFY(!target->insertPages(0, {{source, 1, 0}}).document);
        QVERIFY(!target->insertPages(0, {{source}, {form}}).document);
        const auto result = target->insertPages(3, {{source}, {target, 1, 2}, {source}});
        QVERIFY2(result.document, qPrintable(result.error));
        QCOMPARE(result.document->pageCount(), qsizetype{7});
        QCOMPARE(result.document->renderPage(3).image, source->renderPage(0).image);
        QCOMPARE(result.document->renderPage(4).image, target->renderPage(1).image);
        QCOMPARE(result.document->renderPage(5).image, target->renderPage(2).image);
        QCOMPARE(result.document->renderPage(6).image, source->renderPage(0).image);
        QCOMPARE(target->pageCount(), qsizetype{3});
        QCOMPARE(testdata::digest(targetPath), digest);
    }

    void changesPageStructureWithoutSharingAnnotations_data()
    {
        QTest::addColumn<bool>("nested");
        QTest::newRow("flat") << false;
        QTest::newRow("nested") << true;
    }

    void changesPageStructureWithoutSharingAnnotations()
    {
        QFETCH(bool, nested);
        using Operation = pdfeditor::PdfPageOperation;
        QTemporaryDir directory;
        const auto path = directory.filePath("pages.pdf");
        const auto copy = directory.filePath("organized.pdf");
        QVERIFY(testdata::writeReorderPdf(path, nested));
        const auto digest = testdata::digest(path);
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const auto rotated = source->rotatePage(1).document;
        QVERIFY(rotated);
        const auto noted = rotated->addNote(1, {0.3, 0.4}, QStringLiteral("Nota para duplicar")).document;
        QVERIFY(noted);
        const auto duplicated = noted->changePage(1, Operation::Duplicate);
        QVERIFY2(duplicated.document, qPrintable(duplicated.error));
        QCOMPARE(duplicated.document->pageCount(), qsizetype{4});
        QCOMPARE(duplicated.document->renderPage(1).image, noted->renderPage(1).image);
        QCOMPARE(duplicated.document->renderPage(2).image, noted->renderPage(1).image);
        const auto originalNotes = duplicated.document->pageNotes(1);
        const auto cloneNotes = duplicated.document->pageNotes(2);
        QCOMPARE(originalNotes.size(), 2);
        QCOMPARE(cloneNotes.size(), 2);
        const auto edited = duplicated.document->editNote(2, cloneNotes[1].index, QStringLiteral("Solo la copia")).document;
        QVERIFY(edited);
        QCOMPARE(edited->pageNotes(1)[1].text, QStringLiteral("Nota para duplicar"));
        QCOMPARE(edited->pageNotes(2)[1].text, QStringLiteral("Solo la copia"));

        const auto inserted = edited->changePage(2, Operation::InsertBlankAfter);
        QVERIFY2(inserted.document, qPrintable(inserted.error));
        QCOMPARE(inserted.document->pageCount(), qsizetype{5});
        QCOMPARE(inserted.document->pageSize(3), edited->pageSize(2));
        QVERIFY(inserted.document->pageNotes(3).isEmpty());
        const auto blank = inserted.document->renderPage(3);
        QVERIFY2(!blank.image.isNull(), qPrintable(blank.error));
        QCOMPARE(blank.image.pixelColor(blank.image.rect().center()), QColor(Qt::white));

        // The bookmark targets the original B page, which is removed.
        const auto removed = inserted.document->changePage(1, Operation::Delete);
        QVERIFY2(removed.document, qPrintable(removed.error));
        QCOMPARE(removed.document->pageCount(), qsizetype{4});
        QCOMPARE(removed.document->pageNotes(1)[1].text, QStringLiteral("Solo la copia"));
        QCOMPARE(removed.document->renderPage(0).image, source->renderPage(0).image);
        QCOMPARE(removed.document->renderPage(3).image, source->renderPage(2).image);
        const auto saved = removed.document->save(pdfeditor::PdfSaveMode::NewCopy, copy);
        QVERIFY2(saved.document, qPrintable(saved.error));
        const auto reopened = pdfeditor::PdfDocument::open(copy).document;
        QVERIFY(reopened);
        QCOMPARE(reopened->pageCount(), qsizetype{4});
        QCOMPARE(reopened->pageNotes(1)[1].text, QStringLiteral("Solo la copia"));
        for (qsizetype index = 0; index < reopened->pageCount(); ++index) {
            QCOMPARE(reopened->renderPage(index).image, removed.document->renderPage(index).image);
        }
        QCOMPARE(testdata::digest(path), digest);
        QCOMPARE(source->pageCount(), qsizetype{3});
    }

    void rejectsUnsupportedPageChangesAndPreservesFormFields()
    {
        using Operation = pdfeditor::PdfPageOperation;
        QTemporaryDir directory;
        const auto path = directory.filePath("forms.pdf");
        QVERIFY(testdata::writeVisualPdf(path, 90, 2, testdata::VisualPdfExtras::AnnotationsAndForm));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        QVERIFY(!source->changePage(-1, Operation::Duplicate).document);
        QVERIFY(!source->changePage(0, Operation::Delete).document);
        const auto rejected = source->changePage(0, Operation::Duplicate);
        QVERIFY(!rejected.document);
        QVERIFY(rejected.error.contains(QStringLiteral("formulario")));
        const auto inserted = source->changePage(0, Operation::InsertBlankAfter);
        QVERIFY2(inserted.document, qPrintable(inserted.error));
        QCOMPARE(inserted.document->pageCount(), qsizetype{2});
        QCOMPARE(inserted.document->pageSize(1), source->pageSize(0));
        QCOMPARE(inserted.document->pageFormFields(0).size(), 2);
        QCOMPARE(inserted.document->renderPage(0).image, source->renderPage(0).image);
        const auto removal = inserted.document->changePage(0, Operation::Delete);
        QVERIFY(!removal.document && removal.error.contains(QStringLiteral("formulario")));
    }

    void fillsTextAndCheckboxFieldsAndPreservesTheirAppearances()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("forms.pdf");
        const auto copy = directory.filePath("filled.pdf");
        QVERIFY(testdata::writeVisualPdf(path, 90, 1, testdata::VisualPdfExtras::AnnotationsAndForm));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const auto digest = testdata::digest(path);
        const auto fields = source->pageFormFields(0);
        QCOMPARE(fields.size(), 2);
        QCOMPARE(fields[0].type, pdfeditor::PdfFormFieldType::Text);
        QCOMPARE(fields[0].value.toString(), QStringLiteral("Ana"));
        QCOMPARE(fields[1].type, pdfeditor::PdfFormFieldType::CheckBox);
        QVERIFY(fields[1].value.toBool());
        const auto text = source->setFormField(0, fields[0].index, QStringLiteral("José"));
        QVERIFY2(text.document, qPrintable(text.error));
        QCOMPARE(text.document->pageFormFields(0)[0].value.toString(), QStringLiteral("José"));
        QCOMPARE(source->pageFormFields(0)[0].value.toString(), QStringLiteral("Ana"));
        QVERIFY(text.document->renderPage(0).image != source->renderPage(0).image);
        const auto unchecked = text.document->setFormField(0, fields[1].index, false);
        QVERIFY2(unchecked.document, qPrintable(unchecked.error));
        QVERIFY(!unchecked.document->pageFormFields(0)[1].value.toBool());
        const auto rendered = unchecked.document->renderPage(0);
        QVERIFY2(!rendered.image.isNull(), qPrintable(rendered.error));
        QVERIFY(rendered.warnings.isEmpty());
        QVERIFY(rendered.image != source->renderPage(0).image);
        const auto saved = unchecked.document->save(pdfeditor::PdfSaveMode::NewCopy, copy);
        QVERIFY2(saved.document, qPrintable(saved.error));
        const auto reopened = pdfeditor::PdfDocument::open(copy).document;
        QVERIFY(reopened);
        QCOMPARE(reopened->pageFormFields(0)[0].value.toString(), QStringLiteral("José"));
        QVERIFY(!reopened->pageFormFields(0)[1].value.toBool());
        QCOMPARE(reopened->renderPage(0).image, rendered.image);
        const auto rechecked = reopened->setFormField(0, fields[1].index, true);
        QVERIFY2(rechecked.document, qPrintable(rechecked.error));
        QVERIFY(rechecked.document->pageFormFields(0)[1].value.toBool());
        QCOMPARE(testdata::digest(path), digest);
    }

    void fillsInheritedFieldsAndChecksTheirLimits()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("inherited.pdf");
        QVERIFY(testdata::writeSyntheticPdf(path, {
            "<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [5 0 R] /DA (/F1 12 Tf 0 g) /DR << /Font << /F1 6 0 R >> >> >> >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 200 300] >>",
            "<< /Type /Page /Parent 2 0 R /Annots [4 0 R] >>",
            "<< /Type /Annot /Subtype /Widget /Parent 5 0 R /Rect [20 80 160 110] >>",
            "<< /FT /Tx /T (Name) /V (Ana) /MaxLen 5 /Kids [4 0 R] >>",
            "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"
        }));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const auto fields = source->pageFormFields(0);
        QCOMPARE(fields.size(), 1);
        QCOMPARE(fields[0].maxLength, 5);
        QVERIFY(!source->setFormField(0, fields[0].index, QStringLiteral("demasiado")).document);
        QVERIFY(!source->setFormField(0, fields[0].index, QStringLiteral("A\nB")).document);
        QVERIFY(!source->setFormField(0, fields[0].index, true).document);
        const auto edited = source->setFormField(0, fields[0].index, QStringLiteral("Lucía"));
        QVERIFY2(edited.document, qPrintable(edited.error));
        const auto rendered = edited.document->renderPage(0);
        QVERIFY2(!rendered.image.isNull(), qPrintable(rendered.error));
        QVERIFY2(rendered.warnings.isEmpty(), qPrintable(rendered.warnings.join('\n')));
        const auto cleared = edited.document->setFormField(0, fields[0].index, QString{});
        QVERIFY2(cleared.document, qPrintable(cleared.error));
        QCOMPARE(cleared.document->pageFormFields(0)[0].value.toString(), QString{});
    }

    void savesSearchHighlightsAndDeletesNotesWithTheirPopups()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("annotations.pdf");
        const auto copy = directory.filePath("edited.pdf");
        QVERIFY(testdata::writeVisualPdf(path, 270));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const auto digest = testdata::digest(path);
        const auto matches = source->findText(QStringLiteral("Editor"));
        QCOMPARE(matches.matches.size(), 1);
        const auto highlighted = source->highlightMatch(matches.matches[0]);
        QVERIFY2(highlighted.document, qPrintable(highlighted.error));
        const auto visual = highlighted.document->renderPage(0);
        QVERIFY2(!visual.image.isNull(), qPrintable(visual.error));
        QVERIFY(visual.image != source->renderPage(0).image);
        const auto added = highlighted.document->addNote(0, {0.5, 0.5}, QStringLiteral("Nota"));
        QVERIFY2(added.document, qPrintable(added.error));
        const auto notes = added.document->pageNotes(0);
        QCOMPARE(notes.size(), 1);
        const auto removed = added.document->deleteNote(0, notes[0].index);
        QVERIFY2(removed.document, qPrintable(removed.error));
        QVERIFY(removed.document->pageNotes(0).isEmpty());
        QCOMPARE(removed.document->renderPage(0).image, visual.image);
        QCOMPARE(added.document->pageNotes(0).size(), 1);
        const auto saved = removed.document->save(pdfeditor::PdfSaveMode::NewCopy, copy);
        QVERIFY2(saved.document, qPrintable(saved.error));
        const auto reopened = pdfeditor::PdfDocument::open(copy).document;
        QVERIFY(reopened && reopened->pageNotes(0).isEmpty());
        QCOMPARE(reopened->renderPage(0).image, visual.image);
        const auto cancelPassword = [](bool* accepted) { *accepted = false; return QString{}; };
        pdf::PDFDocumentReader reader(nullptr, cancelPassword, false, false);
        const auto document = reader.readFromFile(copy);
        const auto& annotations = document.getCatalog()->getPage(0)->getAnnotations();
        QCOMPARE(annotations.size(), size_t{1});
        const auto annotation = pdf::PDFAnnotation::parse(&document.getStorage(), annotations[0]);
        QVERIFY(annotation);
        QCOMPARE(annotation->getType(), pdf::AnnotationType::Highlight);
        QVERIFY(annotation->getFlags().testFlag(pdf::PDFAnnotation::Print));
        QCOMPARE(testdata::digest(path), digest);
    }

    void findsTextOnTheVisibleCropAndExportsAnEditedPage()
    {
        QTemporaryDir directory;
        const auto original = directory.filePath("output.pdf");
        const auto imagePath = directory.filePath(QStringLiteral("página.png"));
        QVERIFY(testdata::writeOutputPdf(original));
        const auto digest = testdata::digest(original);
        const auto source = pdfeditor::PdfDocument::open(original).document;
        QVERIFY(source);
        const auto result = source->findText(QStringLiteral(" INFORME "));
        QCOMPARE(result.skippedPages, 0);
        QCOMPARE(result.matches.size(), 2);
        QCOMPARE(result.matches[0].page, 0);
        QCOMPARE(result.matches[1].page, 2);
        const auto highlight = result.matches[0].highlight.boundingRect();
        QVERIFY(highlight.isValid() && QRectF(0, 0, 1, 1).contains(highlight));
        QVERIFY(highlight.center().y() < 0.3);
        QVERIFY(source->findText(QStringLiteral("missing")).matches.isEmpty());
        QVERIFY(source->findText(QStringLiteral("Informe"), [] { return true; }).matches.isEmpty());

        const auto rotated = source->rotatePage(0);
        QVERIFY2(rotated.document, qPrintable(rotated.error));
        const auto rotatedSearch = rotated.document->findText(QStringLiteral("Informe"));
        QCOMPARE(rotatedSearch.matches.size(), 2);
        const auto rotatedCenter = rotatedSearch.matches[0].highlight.boundingRect().center();
        QVERIFY(qAbs(rotatedCenter.x() - (1 - highlight.center().y())) < 0.001);
        QVERIFY(qAbs(rotatedCenter.y() - highlight.center().x()) < 0.001);

        const auto exported = rotated.document->exportPng(0, imagePath);
        QVERIFY2(exported.error.isEmpty(), qPrintable(exported.error));
        const QImage png(imagePath);
        QCOMPARE(png.size(), QSize(450, 300));
        QCOMPARE(png.pixelColor(225, 150), QColor(Qt::red));
        QCOMPARE(png.dotsPerMeterX(), 5906);
        const auto pngDigest = testdata::digest(imagePath);
        QVERIFY(!source->exportPng(2, imagePath).error.isEmpty());
        QCOMPARE(testdata::digest(imagePath), pngDigest);
        QCOMPARE(testdata::digest(original), digest);
    }

    void addsAndEditsNotesWithoutLosingExistingAnnotations()
    {
        QTemporaryDir directory;
        const auto original = directory.filePath("original.pdf");
        const auto destination = directory.filePath("notes.pdf");
        QVERIFY(testdata::writeSyntheticPdf(original, {
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 200 300] /Rotate 90 >>",
            "<< /Type /Page /Parent 2 0 R /CropBox [20 40 180 260] /Annots 4 0 R >>",
            "[5 0 R 6 0 R 7 0 R]",
            "<< /Type /Annot /Subtype /Text /Rect [35 65 55 85] /F 512 /Contents (Locked) >>",
            "<< /Type /Annot /Subtype /Text /Rect [35 95 55 115] /F 2 /Contents (Hidden) >>",
            "<< /Type /Annot /Subtype /Text /Rect [65 65 85 85] /Contents (Existing) >>"
        }));
        const auto digest = testdata::digest(original);
        const auto source = pdfeditor::PdfDocument::open(original).document;
        QVERIFY(source);
        const auto existing = source->pageNotes(0);
        QCOMPARE(existing.size(), 2);
        QVERIFY(!existing[0].editable && existing[1].editable);
        QVERIFY(!source->editNote(0, existing[0].index, QStringLiteral("New")).document);
        QVERIFY(!source->editNote(0, 1, QStringLiteral("New")).document);
        QVERIFY(!source->addNote(0, {0.4, 1.5}, QStringLiteral("New")).document);
        QVERIFY(!source->addNote(0, {0.4, 0.5}, QStringLiteral("  \n")).document);

        const auto added = source->addNote(0, {0.4, 0.5}, QStringLiteral("Revisión técnica\nSegunda línea"));
        QVERIFY2(added.document, qPrintable(added.error));
        const auto notes = added.document->pageNotes(0);
        QCOMPARE(notes.size(), 3);
        QCOMPARE(notes[1].text, QStringLiteral("Existing"));
        QCOMPARE(notes[2].text, QStringLiteral("Revisión técnica\nSegunda línea"));
        QCOMPARE(source->pageNotes(0).size(), 2);
        const auto before = source->renderPage(0);
        const auto rendered = added.document->renderPage(0);
        QVERIFY2(!before.image.isNull(), qPrintable(before.error + before.warnings.join('\n')));
        QVERIFY2(!rendered.image.isNull(), qPrintable(rendered.error + rendered.warnings.join('\n')));
        const auto& after = rendered.image;
        const QRect noteArea(qRound(after.width() * 0.4), qRound(after.height() * 0.5), 44, 44);
        QVERIFY(before.image.copy(noteArea) != after.copy(noteArea));

        const auto changedText = QStringLiteral("Versión final\nConfirmación");
        const auto edited = added.document->editNote(0, notes[2].index, changedText);
        QVERIFY2(edited.document, qPrintable(edited.error));
        QCOMPARE(edited.document->renderPage(0).image, after);
        QCOMPARE(testdata::digest(original), digest);
        const auto saved = edited.document->save(pdfeditor::PdfSaveMode::NewCopy, destination);
        QVERIFY2(saved.document, qPrintable(saved.error));

        const auto cancelPassword = [](bool* accepted) { *accepted = false; return QString{}; };
        pdf::PDFDocumentReader reader(nullptr, cancelPassword, false, false);
        const auto stored = reader.readFromFile(destination);
        QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);
        const auto* page = stored.getCatalog()->getPage(0);
        QCOMPARE(page->getAnnotations().size(), size_t{5});
        const auto storedNote = pdf::PDFAnnotation::parse(&stored.getStorage(),
            page->getAnnotations()[static_cast<size_t>(notes[2].index)]);
        QVERIFY(storedNote);
        QCOMPARE(storedNote->getContents(), changedText);
        QCOMPARE(storedNote->getRectangle().bottomLeft(), QPointF(100, 128));
        QCOMPARE(testdata::digest(original), digest);
    }

    void extractsAnIntervalFromTheEditedDocument()
    {
        QTemporaryDir directory;
        const auto original = directory.filePath("original.pdf");
        const auto destination = directory.filePath(QStringLiteral("extracción.pdf"));
        QVERIFY(testdata::writeReorderPdf(original));
        const auto digest = testdata::digest(original);
        const auto source = pdfeditor::PdfDocument::open(original).document;
        QVERIFY(source);
        const auto single = source->extractPages(0, 0, directory.filePath("first.pdf"));
        QVERIFY2(single.document, qPrintable(single.error));
        QCOMPARE(single.document->pageCount(), qsizetype{1});
        QCOMPARE(single.document->renderPage(0).image, source->renderPage(0).image);

        const auto rotated = source->rotatePage(1).document;
        QVERIFY(rotated);
        const auto edited = rotated->movePage(1, 0).document;
        QVERIFY(edited);

        QVERIFY(!edited->extractPages(-1, 1, destination).document);
        QVERIFY(!edited->extractPages(1, 0, destination).document);
        QVERIFY(!edited->extractPages(0, 3, destination).document);
        QVERIFY(!edited->extractPages(0, 1, original).document);
        QVERIFY(!QFileInfo::exists(destination));

        const auto result = edited->extractPages(0, 1, destination);
        QVERIFY2(result.document, qPrintable(result.error));
        const auto reopened = pdfeditor::PdfDocument::open(destination).document;
        QVERIFY(reopened);
        QCOMPARE(reopened->pageCount(), qsizetype{2});
        QCOMPARE(reopened->renderPage(0).image, edited->renderPage(0).image);
        QCOMPARE(reopened->renderPage(1).image, source->renderPage(0).image);
        QCOMPARE(edited->pageCount(), qsizetype{3});
        QCOMPARE(testdata::digest(original), digest);
        const auto copyDigest = testdata::digest(destination);
        QVERIFY(!edited->extractPages(2, 2, destination).document);
        QCOMPARE(testdata::digest(destination), copyDigest);
    }

    void rejectsAnIntervalWithReferencesToExcludedPages()
    {
        QTemporaryDir directory;
        const auto original = directory.filePath("links.pdf");
        const auto destination = directory.filePath("extracted.pdf");
        QVERIFY(testdata::writeSyntheticPdf(original, {
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 /MediaBox [0 0 120 180] >>",
            "<< /Type /Page /Parent 2 0 R /Annots [5 0 R] >>",
            "<< /Type /Page /Parent 2 0 R >>",
            "<< /Type /Annot /Subtype /Link /Rect [5 5 30 30] /Dest [4 0 R /Fit] >>"
        }));
        const auto source = pdfeditor::PdfDocument::open(original).document;
        QVERIFY(source);
        const auto rejected = source->extractPages(0, 0, destination);
        QVERIFY(!rejected.document);
        QVERIFY(rejected.error.contains(QStringLiteral("referencias a otras páginas")));
        QVERIFY(!QFileInfo::exists(destination));
        const auto referenced = source->changePage(1, pdfeditor::PdfPageOperation::Delete);
        QVERIFY(!referenced.document);
        QVERIFY(referenced.error.contains(QStringLiteral("referencias")));
        const auto result = source->extractPages(1, 1, destination);
        QVERIFY2(result.document, qPrintable(result.error));
        QCOMPARE(result.document->pageCount(), qsizetype{1});
    }

    void preservesPageContentAndDestinationsWhenMoving_data()
    {
        QTest::addColumn<bool>("nested");
        QTest::newRow("flat") << false;
        QTest::newRow("nested-inheritance") << true;
    }

    void preservesPageContentAndDestinationsWhenMoving()
    {
        QFETCH(bool, nested);
        QTemporaryDir directory;
        const auto path = directory.filePath("pages.pdf");
        const auto copy = directory.filePath("reordered.pdf");
        QVERIFY(testdata::writeReorderPdf(path, nested));
        const auto digest = testdata::digest(path);
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        QVERIFY(!source->movePage(-1, 0).document);
        QVERIFY(!source->movePage(0, 3).document);
        QVERIFY(!source->movePage(0, 0).document);

        const auto rotated = source->rotatePage(1);
        QVERIFY2(rotated.document, qPrintable(rotated.error));
        const auto moved = rotated.document->movePage(1, 0);
        QVERIFY2(moved.document, qPrintable(moved.error));
        QCOMPARE(moved.document->pageCount(), qsizetype{3});
        const QList<QImage> expected{rotated.document->renderPage(1).image, source->renderPage(0).image,
            source->renderPage(2).image};
        for (qsizetype index = 0; index < expected.size(); ++index) {
            QVERIFY(!expected[index].isNull());
            QCOMPARE(moved.document->renderPage(index).image, expected[index]);
        }
        QCOMPARE(testdata::digest(path), digest);
        QVERIFY(!QFileInfo::exists(copy));

        const auto saved = moved.document->save(pdfeditor::PdfSaveMode::NewCopy, copy);
        QVERIFY2(saved.document, qPrintable(saved.error));
        const auto reopened = pdfeditor::PdfDocument::open(copy).document;
        QVERIFY(reopened);
        for (qsizetype index = 0; index < expected.size(); ++index) {
            QCOMPARE(reopened->renderPage(index).image, expected[index]);
        }
        QCOMPARE(testdata::digest(path), digest);

        const auto cancelPassword = [](bool* accepted) { *accepted = false; return QString{}; };
        pdf::PDFDocumentReader reader(nullptr, cancelPassword, false, false);
        const auto stored = reader.readFromFile(copy);
        QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);
        const auto outline = stored.getCatalog()->getOutlineRootPtr();
        QVERIFY(outline && outline->getChildCount() == 1);
        const auto* goTo = dynamic_cast<const pdf::PDFActionGoTo*>(outline->getChild(0)->getAction());
        QVERIFY(goTo);
        QCOMPARE(stored.getCatalog()->getPageIndexFromPageReference(goTo->getDestination().getPageReference()), size_t{0});
    }

    void savesTwoPendingRotationsInOneWrite_data()
    {
        QTest::addColumn<bool>("saveOriginal");
        QTest::newRow("new-copy") << false;
        QTest::newRow("original") << true;
    }

    void savesTwoPendingRotationsInOneWrite()
    {
        QFETCH(bool, saveOriginal);
        QTemporaryDir directory;
        const auto original = directory.filePath("original.pdf");
        const auto destination = saveOriginal ? original : directory.filePath(QStringLiteral("copia girada ñ.pdf"));
        const auto expected = directory.filePath("reference-180.pdf");
        QVERIFY(testdata::writeVisualPdf(original));
        QVERIFY(testdata::writeVisualPdf(expected, 180));
        const auto digest = testdata::digest(original);
        const auto source = pdfeditor::PdfDocument::open(original).document;
        const auto reference = pdfeditor::PdfDocument::open(expected).document;
        QVERIFY(source && reference);
        const auto first = source->rotatePage(0);
        QVERIFY2(first.document, qPrintable(first.error));
        QCOMPARE(first.document->pageSize(0), source->pageSize(0).transposed());
        const auto second = first.document->rotatePage(0);
        QVERIFY2(second.document, qPrintable(second.error));
        QCOMPARE(second.document->renderPage(0).image, reference->renderPage(0).image);
        QCOMPARE(testdata::digest(original), digest);
        if (!saveOriginal) { QVERIFY(!QFileInfo::exists(destination)); }

        using Mode = pdfeditor::PdfSaveMode;
        const auto saved = saveOriginal ? second.document->save(Mode::Original)
            : second.document->save(Mode::NewCopy, destination);
        QVERIFY2(saved.document, qPrintable(saved.error));
        QCOMPARE(saved.document->filePath(), destination);
        const auto reopened = pdfeditor::PdfDocument::open(destination);
        QVERIFY2(reopened.document, qPrintable(reopened.error));
        QCOMPARE(reopened.document->pageCount(), qsizetype{1});
        QCOMPARE(reopened.document->renderPage(0).image, reference->renderPage(0).image);
        if (saveOriginal) { QVERIFY(testdata::digest(original) != digest); }
        else { QCOMPARE(testdata::digest(original), digest); }

        // The successful save establishes a new baseline for the next save.
        const auto next = saved.document->rotatePage(0);
        QVERIFY2(next.document, qPrintable(next.error));
        const auto resaved = next.document->save(Mode::Original);
        QVERIFY2(resaved.document, qPrintable(resaved.error));
        QCOMPARE(resaved.document->pageSize(0), source->pageSize(0).transposed());
    }

    void preservesAnnotationsAndWrapsRotation()
    {
        QTemporaryDir directory;
        const auto original = directory.filePath("rotated270.pdf");
        const auto expected = directory.filePath("expected.pdf");
        const auto destination = directory.filePath("copy.pdf");
        using Extras = testdata::VisualPdfExtras;
        QVERIFY(testdata::writeVisualPdf(original, 270, 1, Extras::AnnotationsAndForm));
        QVERIFY(testdata::writeVisualPdf(expected, 0, 1, Extras::AnnotationsAndForm));
        const auto source = pdfeditor::PdfDocument::open(original).document;
        const auto reference = pdfeditor::PdfDocument::open(expected).document;
        QVERIFY(source && reference);
        const auto edited = source->rotatePage(0);
        QVERIFY2(edited.document, qPrintable(edited.error));
        const auto saved = edited.document->save(pdfeditor::PdfSaveMode::NewCopy, destination);
        QVERIFY2(saved.document, qPrintable(saved.error));
        const auto result = saved.document->renderPage(0);
        QVERIFY2(!result.image.isNull(), qPrintable(result.error));
        QCOMPARE(result.image, reference->renderPage(0).image);
        QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join('\n')));
    }

    void rejectsExistingOrInvalidDestinationsAndRestrictedDocuments()
    {
        QTemporaryDir directory;
        const auto original = directory.filePath("original.pdf");
        const auto existing = directory.filePath("other.pdf");
        const auto missing = directory.filePath("missing/copy.pdf");
        const auto destination = directory.filePath("copy.pdf");
        QVERIFY(testdata::writePdf(original, 2));
        QVERIFY(testdata::writePdf(existing, 1));
        const auto originalDigest = testdata::digest(original);
        const auto existingDigest = testdata::digest(existing);
        const auto source = pdfeditor::PdfDocument::open(original).document;
        QVERIFY(source);
        QVERIFY(!source->rotatePage(-1).document);
        QVERIFY(!source->rotatePage(2).document);
        const auto edited = source->rotatePage(0);
        QVERIFY2(edited.document, qPrintable(edited.error));
        using Mode = pdfeditor::PdfSaveMode;
        QVERIFY(!edited.document->save(Mode::NewCopy, original).document);
        QVERIFY(!edited.document->save(Mode::NewCopy, existing).document);
        QVERIFY(!edited.document->save(Mode::NewCopy, missing).document);
        QCOMPARE(testdata::digest(original), originalDigest);
        QCOMPARE(testdata::digest(existing), existingDigest);
        QVERIFY(!QFileInfo::exists(missing));

        using Extras = testdata::VisualPdfExtras;
        for (const auto extras : {Extras::SignatureMarker, Extras::XfaForm}) {
            const auto path = directory.filePath(QStringLiteral("restricted-%1.pdf").arg(static_cast<int>(extras)));
            QVERIFY(testdata::writeVisualPdf(path, 0, 1, extras));
            const auto restricted = pdfeditor::PdfDocument::open(path).document;
            QVERIFY(restricted);
            const auto failed = restricted->rotatePage(0);
            QVERIFY(!failed.document);
            QVERIFY(failed.error.contains(extras == Extras::XfaForm ? QStringLiteral("XFA") : QStringLiteral("firma")));
            QVERIFY(!QFileInfo::exists(destination));
        }

        // A change made elsewhere must not be overwritten by Guardar.
        QVERIFY(testdata::writePdf(original, 3));
        const auto externalDigest = testdata::digest(original);
        const auto conflict = edited.document->save(Mode::Original);
        QVERIFY(!conflict.document);
        QVERIFY(conflict.error.contains(QStringLiteral("Guardar como")));
        QCOMPARE(testdata::digest(original), externalDigest);
        QVERIFY(edited.document->save(Mode::NewCopy, destination).document);
    }

    void readsPagesWithoutModifyingTheOriginal()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("documento español.pdf"));
        QVERIFY(testdata::writePdf(path));
        const auto before = testdata::digest(path);
        QVERIFY(!before.isEmpty());

        const auto result = pdfeditor::PdfDocument::open(path);
        QVERIFY2(result.document != nullptr, qPrintable(result.error));
        QCOMPARE(result.document->pageCount(), qsizetype{2});
        QCOMPARE(result.document->filePath(), path);
        QVERIFY(result.error.isEmpty());
        QCOMPARE(testdata::digest(path), before);
    }

    void rejectsMissingFilesAndDirectories()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        for (const auto& path : {QString{}, directory.path(), directory.filePath("missing.pdf")}) {
            const auto result = pdfeditor::PdfDocument::open(path);
            QVERIFY(!result.document);
            QVERIFY(!result.error.isEmpty());
        }
    }

    void rejectsMalformedFiles_data()
    {
        QTest::addColumn<QByteArray>("contents");
        QTest::newRow("empty") << QByteArray{};
        QTest::newRow("text") << QByteArray("This is not a PDF document.");
        QTest::newRow("truncated") << QByteArray("%PDF-1.7\n1 0 obj\n<< /Type /Catalog >>\n");
    }

    void rejectsMalformedFiles()
    {
        QFETCH(QByteArray, contents);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("invalid.pdf");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(contents), contents.size());
        file.close();
        const auto before = testdata::digest(path);

        const auto result = pdfeditor::PdfDocument::open(path);
        QVERIFY(!result.document);
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(testdata::digest(path), before);
    }

    void reportsPasswordProtectedDocuments_data()
    {
        QTest::addColumn<bool>("passwordRequired");
        QTest::newRow("password-required") << true;
        QTest::newRow("opens-without-password") << false;
    }

    void reportsPasswordProtectedDocuments()
    {
        QFETCH(bool, passwordRequired);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto plainPath = directory.filePath("plain.pdf");
        const auto protectedPath = directory.filePath("protected.pdf");
        QVERIFY(testdata::writePdf(plainPath, 1));
        const auto noPassword = [](bool* accepted) { *accepted = false; return QString{}; };
        pdf::PDFDocumentReader reader(nullptr, noPassword, false, false);
        auto source = reader.readFromFile(plainPath);
        QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);

        pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
        settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
        if (passwordRequired) { settings.userPassword = QStringLiteral("test-user-password"); }
        settings.ownerPassword = QStringLiteral("test-owner-password");
        settings.id = source.getIdPart(0);
        auto handler = pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings);
        QVERIFY(handler != nullptr);
        pdf::PDFDocumentBuilder builder(&source);
        builder.setSecurityHandler(handler);
        auto encrypted = builder.build();
        pdf::PDFDocumentWriter writer(nullptr);
        QVERIFY(writer.write(protectedPath, &encrypted, false));
        const auto before = testdata::digest(protectedPath);

        const auto result = pdfeditor::PdfDocument::open(protectedPath);
        if (passwordRequired) {
            QVERIFY(!result.document);
            QVERIFY(result.error.contains(QStringLiteral("contraseña")));
        } else {
            QVERIFY2(result.document, qPrintable(result.error));
            QVERIFY(!result.document->canCopyText());
            QVERIFY(result.document->selectText(0, {}, {1, 1}).error.contains(QStringLiteral("permite copiar")));
            const auto destination = directory.filePath("encrypted-copy.pdf");
            const auto saved = result.document->rotatePage(0);
            QVERIFY(!saved.document);
            QVERIFY(saved.error.contains(QStringLiteral("cifrado")));
            QVERIFY(!QFileInfo::exists(destination));
        }
        QCOMPARE(testdata::digest(protectedPath), before);
    }
};

QTEST_MAIN(PdfDocumentTest)
#include "PdfDocumentTest.moc"
