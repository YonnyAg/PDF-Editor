#include "core/PdfDocument.h"
#include "PdfTestData.h"
#include "VisualPdfData.h"
#include <pdfcertificatemanager.h>
#include <QDir>
#include <QFontDatabase>
#include <QTemporaryDir>
#include <QTest>
#include <QProcess>
#include <algorithm>

class ContentSignatureTest final : public QObject
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
    void replacesExistingTextAndPreservesOriginalFont_data()
    {
        QTest::addColumn<int>("rotation");
        for (int angle : {0, 90, 180, 270}) { QTest::newRow(qPrintable(QString::number(angle))) << angle; }
    }
    void replacesExistingTextAndPreservesOriginalFont()
    {
        QFETCH(int, rotation);
        QTemporaryDir directory; const auto path = directory.filePath("original.pdf");
        QVERIFY(testdata::writeVisualPdf(path, rotation, 2, testdata::VisualPdfExtras::AnnotationsAndForm));
        const auto hash = testdata::digest(path);
        const auto source = pdfeditor::PdfDocument::open(path).document; QVERIFY(source);
        const auto list = source->pageContent(0); QVERIFY2(list.error.isEmpty(), qPrintable(list.error));
        auto item = std::find_if(list.items.begin(), list.items.end(), [](const auto& value) { return value.type == pdfeditor::PdfContentType::Text; });
        QVERIFY(item != list.items.end()); QVERIFY(item->replaceable);
        pdfeditor::PdfContentUpdate update; update.change = pdfeditor::PdfContentChange::ReplaceText; update.text = QStringLiteral("PDF Nuevo");
        const auto edited = source->editContent(0, item->index, update);
        QVERIFY2(edited.document, qPrintable(edited.error));
        QVERIFY(edited.document->findText(update.text).matches.size() > 0);
        QVERIFY(edited.document->findText(QStringLiteral("PDF Editor")).matches.isEmpty());
        QCOMPARE(edited.document->pageFormFields(0).size(), source->pageFormFields(0).size());
        QCOMPARE(edited.document->pageNotes(0).size(), source->pageNotes(0).size());
        const auto saved = directory.filePath("changed.pdf"); QVERIFY(edited.document->save(pdfeditor::PdfSaveMode::NewCopy, saved).document);
        const auto reopened = pdfeditor::PdfDocument::open(saved).document; QVERIFY(reopened);
        QVERIFY(!reopened->findText(update.text).matches.isEmpty());
        QCOMPARE(testdata::digest(path), hash);
        update.text = QString::fromUtf8("漢字");
        QVERIFY(!source->editContent(0, item->index, update).document);
    }
    void replacesMovesRestylesAndDeletesObjects()
    {
        QTemporaryDir directory; const auto path = directory.filePath("source.pdf");
        QVERIFY(testdata::writeVisualPdf(path)); const auto source = pdfeditor::PdfDocument::open(path).document; QVERIFY(source);
        const auto list = source->pageContent(0); QVERIFY2(list.error.isEmpty(), qPrintable(list.error));
        auto image = std::find_if(list.items.begin(), list.items.end(), [](const auto& item) { return item.type == pdfeditor::PdfContentType::Image; });
        auto pathItem = std::find_if(list.items.begin(), list.items.end(), [](const auto& item) { return item.type == pdfeditor::PdfContentType::Path; });
        QVERIFY(image != list.items.end() && pathItem != list.items.end());
        pdfeditor::PdfContentUpdate update; update.change = pdfeditor::PdfContentChange::ReplaceImage;
        update.image = QImage(80, 80, QImage::Format_RGB32); update.image.fill(Qt::magenta);
        auto result = source->editContent(0, image->index, update); QVERIFY2(result.document, qPrintable(result.error));
        const auto pixels = result.document->renderPage(0).image;
        QCOMPARE(pixels.pixelColor(qRound(image->area.center().x() * pixels.width()), qRound(image->area.center().y() * pixels.height())), QColor(Qt::magenta));
        update.change = pdfeditor::PdfContentChange::Transform; update.offset = {.1, .1}; update.scale = .8; update.rotation = 10;
        result = source->editContent(0, image->index, update); QVERIFY2(result.document, qPrintable(result.error));
        update.change = pdfeditor::PdfContentChange::PathStyle; update.fill = Qt::yellow; update.stroke = Qt::blue;
        result = source->editContent(0, pathItem->index, update); QVERIFY2(result.document, qPrintable(result.error));
        update.change = pdfeditor::PdfContentChange::Delete;
        result = source->editContent(0, image->index, update); QVERIFY2(result.document, qPrintable(result.error));
        const auto newList = result.document->pageContent(0); QVERIFY(newList.error.isEmpty());
        QVERIFY(std::none_of(newList.items.begin(), newList.items.end(), [](const auto& item) { return item.type == pdfeditor::PdfContentType::Image; }));
    }
    void editsInsertedContentAfterSavingAndReopening()
    {
        QTemporaryDir directory; const auto path = directory.filePath("source.pdf"); const auto saved = directory.filePath("inserted.pdf");
        QVERIFY(testdata::writeVisualPdf(path)); const auto source = pdfeditor::PdfDocument::open(path).document; QVERIFY(source);
        const auto inserted = source->insertText(0, {.1, .05, .8, .15}, QStringLiteral("EDITOR"), 12); QVERIFY2(inserted.document, qPrintable(inserted.error));
        QVERIFY(inserted.document->save(pdfeditor::PdfSaveMode::NewCopy, saved).document);
        const auto reopened = pdfeditor::PdfDocument::open(saved).document; QVERIFY(reopened);
        const auto list = reopened->pageContent(0); QVERIFY2(list.error.isEmpty(), qPrintable(list.error));
        const auto item = std::find_if(list.items.begin(), list.items.end(), [](const auto& item) { return item.type == pdfeditor::PdfContentType::Text && item.text.contains(QStringLiteral("EDITOR")); });
        QVERIFY(item != list.items.end());
        pdfeditor::PdfContentUpdate move; move.offset = {0, .1};
        const auto result = reopened->editContent(0, item->index, move); QVERIFY2(result.document, qPrintable(result.error));
        QVERIFY(!result.document->findText(QStringLiteral("EDITOR")).matches.isEmpty());
        pdfeditor::PdfContentUpdate replacement; replacement.change = pdfeditor::PdfContentChange::ReplaceText; replacement.text = QStringLiteral("EDIT");
        const auto changed = reopened->editContent(0, item->index, replacement); QVERIFY2(changed.document, qPrintable(changed.error));
        QVERIFY(!changed.document->findText(QStringLiteral("EDIT")).matches.isEmpty());
    }
    void signsWithATestCertificateAndDetectsTampering()
    {
        QTemporaryDir directory; const auto path = directory.filePath("original.pdf"); const auto output = directory.filePath("signed.pdf");
        QVERIFY(testdata::writeVisualPdf(path, 0, 1, testdata::VisualPdfExtras::AnnotationsAndForm));
        pdf::PDFCertificateManager::NewCertificateInfo info;
        info.fileName = directory.filePath("synthetic.p12"); info.privateKeyPasword = QStringLiteral("test-password");
        info.certCommonName = QStringLiteral("PDF Editor Synthetic Test"); info.rsaKeyLength = 2048;
        pdf::PDFCertificateManager manager; manager.createCertificate(info); QVERIFY(QFileInfo::exists(info.fileName));
        const auto hash = testdata::digest(path); const auto source = pdfeditor::PdfDocument::open(path).document; QVERIFY(source);
        QVERIFY(source->verifySignatures().signatures.isEmpty());
        QVERIFY(!source->signedCopy(output, info.fileName, QStringLiteral("wrong")).error.isEmpty()); QVERIFY(!QFileInfo::exists(output));
        const auto pending = source->rotatePage(0); QVERIFY(pending.document);
        const auto result = pending.document->signedCopy(output, info.fileName, info.privateKeyPasword, QStringLiteral("Synthetic test"));
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
#ifdef Q_OS_WIN
        QProcess independent;
        independent.start(QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("System32/WindowsPowerShell/v1.0/powershell.exe")),
            {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"), QStringLiteral("-File"),
                QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("VerifyDetachedSignature.ps1")), QStringLiteral("-Pdf"), output});
        QVERIFY(independent.waitForFinished(30000));
        QVERIFY2(independent.exitCode() == 0, independent.readAllStandardError().constData());
#endif
        const auto signedDoc = pdfeditor::PdfDocument::open(output).document; QVERIFY(signedDoc);
        const auto report = signedDoc->verifySignatures(); QVERIFY2(report.error.isEmpty(), qPrintable(report.error)); QCOMPARE(report.signatures.size(), 1);
        QVERIFY(report.signatures[0].integrityValid); QVERIFY(report.signatures[0].coversWholeFile); QVERIFY(!report.signatures[0].certificateTrusted);
        QCOMPARE(signedDoc->renderPage(0).image, pending.document->renderPage(0).image);
        QCOMPARE(signedDoc->pageFormFields(0).size(), source->pageFormFields(0).size());
        QVERIFY(!signedDoc->rotatePage(0).document);
        QFile bytes(output); QVERIFY(bytes.open(QIODevice::ReadOnly)); auto raw = bytes.readAll(); bytes.close();
        QVERIFY(!raw.contains("test-password"));
        const auto appendedPath = directory.filePath("appended.pdf");
        QFile appended(appendedPath); QVERIFY(appended.open(QIODevice::WriteOnly)); QVERIFY(appended.write(raw + "\n% appended bytes\n") > raw.size()); appended.close();
        const auto appendedDoc = pdfeditor::PdfDocument::open(appendedPath).document; QVERIFY(appendedDoc);
        const auto partial = appendedDoc->verifySignatures(); QVERIFY2(partial.error.isEmpty(), qPrintable(partial.error)); QCOMPARE(partial.signatures.size(), 1);
        QVERIFY(partial.signatures[0].integrityValid); QVERIFY(!partial.signatures[0].coversWholeFile);
        const auto modifiedPath = directory.filePath("modified.pdf");
        const auto position = raw.indexOf("Synthetic test"); QVERIFY(position >= 0); raw[position] = 'X';
        QFile modified(modifiedPath); QVERIFY(modified.open(QIODevice::WriteOnly)); QCOMPARE(modified.write(raw), raw.size()); modified.close();
        const auto changed = pdfeditor::PdfDocument::open(modifiedPath).document; QVERIFY(changed);
        const auto invalid = changed->verifySignatures(); QVERIFY(invalid.error.isEmpty()); QCOMPARE(invalid.signatures.size(), 1); QVERIFY(!invalid.signatures[0].integrityValid);
        QCOMPARE(testdata::digest(path), hash);
        QVERIFY(!source->signedCopy(output, info.fileName, info.privateKeyPasword).error.isEmpty());
    }
    void restylesSavedInkWithoutChangingItsPoints()
    {
        QTemporaryDir directory; const auto path = directory.filePath("source.pdf"); QVERIFY(testdata::writeVisualPdf(path, 90, 2));
        const auto source = pdfeditor::PdfDocument::open(path).document; QVERIFY(source);
        const auto ink = source->addInk(0, {QPolygonF{{.1, .1}, {.4, .3}, {.6, .1}}}, Qt::red, 2); QVERIFY(ink.document);
        const auto shapes = ink.document->pageShapes(0); QCOMPARE(shapes.size(), 1);
        const auto before = ink.document->renderPage(0).image;
        const auto changed = ink.document->restyleShape(0, shapes[0].index, Qt::blue, {}, 6); QVERIFY2(changed.document, qPrintable(changed.error));
        QCOMPARE(changed.document->pageShapes(0).size(), 1);
        QVERIFY(changed.document->renderPage(0).image != before);
        const auto saved = directory.filePath("restyled.pdf"); QVERIFY(changed.document->save(pdfeditor::PdfSaveMode::NewCopy, saved).document);
        const auto reopened = pdfeditor::PdfDocument::open(saved).document; QVERIFY(reopened);
        QCOMPARE(reopened->renderPage(0).image, changed.document->renderPage(0).image);
        QVERIFY(!ink.document->restyleShape(0, 999, Qt::blue, {}, 6).document);
        QVERIFY(!ink.document->restyleShape(0, shapes[0].index, Qt::blue, {}, 100).document);
    }
    void rejectsMalformedSignatureTreesAndByteRanges()
    {
        QTemporaryDir directory; const auto path = directory.filePath("bad.pdf");
        QVERIFY(testdata::writeSyntheticPdf(path, {"<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [4 0 R] >> >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] >>", "<< /FT /Sig /T (Cycle) /Kids [4 0 R] >>"}));
        auto source = pdfeditor::PdfDocument::open(path).document; QVERIFY(source); QVERIFY(!source->verifySignatures().error.isEmpty());
        QVERIFY(testdata::writeSyntheticPdf(path, {"<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] >>", "<< /Type /Sig /ByteRange [0 9223372036854775807 1 9223372036854775807] /Contents <0000> >>"}));
        source = pdfeditor::PdfDocument::open(path).document; QVERIFY(source); QVERIFY(!source->verifySignatures().error.isEmpty());
    }
};
QTEST_MAIN(ContentSignatureTest)
#include "ContentSignatureTest.moc"

