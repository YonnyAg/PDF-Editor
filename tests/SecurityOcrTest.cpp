#include "core/PdfDocument.h"
#include "core/OcrEngine.h"
#include "session/SessionStore.h"
#include "PdfTestData.h"
#include "VisualPdfData.h"

#include <QDir>
#include <QFontDatabase>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <limits>

namespace {
QImage scannedImage()
{
    QImage image(1280, 720, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    QFont font(QStringLiteral("Arial"));
    font.setPixelSize(62);
    painter.setFont(font);
    painter.setPen(Qt::black);
    painter.drawText(70, 180, QStringLiteral("PDF EDITOR LOCAL 2026"));
    painter.drawText(70, 330, QStringLiteral("Documento escaneado"));
    painter.drawText(70, 480, QStringLiteral("Prueba de texto"));
    return image;
}
}

class SecurityOcrTest final : public QObject
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

    void protectsCopiesWithVerifiedPasswordsAndPermissions_data()
    {
        QTest::addColumn<bool>("copy");
        QTest::addColumn<bool>("print");
        QTest::newRow("allowed") << true << true;
        QTest::newRow("restricted") << false << false;
    }

    void protectsCopiesWithVerifiedPasswordsAndPermissions()
    {
        QFETCH(bool, copy);
        QFETCH(bool, print);
        QTemporaryDir directory;
        const auto path = directory.filePath("original.pdf");
        const auto output = directory.filePath("protected.pdf");
        QVERIFY(testdata::writeVisualPdf(path, 90, 2, testdata::VisualPdfExtras::AnnotationsAndForm));
        const auto digest = testdata::digest(path);
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const QByteArray secret("CONFIDENTIAL-ATTACHMENT-2026");
        const auto changed = source->addAttachment(QStringLiteral("private.txt"), secret);
        QVERIFY2(changed.document, qPrintable(changed.error));
        const auto protectedResult = changed.document->protectedCopy(output, QStringLiteral("user-password-2026"), QStringLiteral("owner-password-2026"), copy, print);
        QVERIFY2(protectedResult.error.isEmpty(), qPrintable(protectedResult.error));
        const auto missing = pdfeditor::PdfDocument::open(output);
        QVERIFY(!missing.document && missing.passwordRequired);
        const auto wrong = pdfeditor::PdfDocument::open(output, QStringLiteral("incorrect"));
        QVERIFY(!wrong.document && wrong.passwordRequired);
        const auto user = pdfeditor::PdfDocument::open(output, QStringLiteral("user-password-2026"));
        QVERIFY2(user.document, qPrintable(user.error));
        QVERIFY(user.document->isEncrypted());
        QCOMPARE(user.document->canCopyText(), copy);
        QCOMPARE(user.document->printResolutionLimit() > 0, print);
        QCOMPARE(user.document->renderPage(0).image, changed.document->renderPage(0).image);
        QCOMPARE(user.document->pageFormFields(0).size(), 2);
        QCOMPARE(user.document->attachments().size(), 1);
        QVERIFY(!user.document->rotatePage(0).document);
        QVERIFY(!user.document->save(pdfeditor::PdfSaveMode::NewCopy, directory.filePath("not-decrypted.pdf")).document);
        const auto owner = pdfeditor::PdfDocument::open(output, QStringLiteral("owner-password-2026"));
        QVERIFY2(owner.document, qPrintable(owner.error));
        QVERIFY(owner.document->canCopyText());
        QVERIFY(owner.document->printResolutionLimit() > 0);
        const auto attachment = directory.filePath("export.txt");
        QCOMPARE(user.document->exportAttachment(user.document->attachments()[0].key, attachment).error.isEmpty(), copy);
        QFile bytes(output);
        QVERIFY(bytes.open(QIODevice::ReadOnly));
        const auto encryptedBytes = bytes.readAll();
        QVERIFY(!encryptedBytes.contains(secret));
        QVERIFY(!encryptedBytes.contains("user-password-2026"));
        QVERIFY(!encryptedBytes.contains("owner-password-2026"));
        QVERIFY(encryptedBytes.contains("/AESV3"));
        bytes.close();
        const auto encryptedHash = testdata::digest(output);
        QVERIFY(!changed.document->protectedCopy(output, QStringLiteral("other-user-password"), QStringLiteral("other-owner-password")).error.isEmpty());
        QCOMPARE(testdata::digest(output), encryptedHash);
        QCOMPARE(testdata::digest(path), digest);
        pdfeditor::SessionStore sessions(directory.filePath("session"));
        pdfeditor::SessionDocument entry;
        entry.entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        entry.document = user.document;
        QVERIFY(sessions.save({entry}, {}, {output}).isEmpty());
        QVERIFY(sessions.read().entries.isEmpty());
        QVERIFY(QDir(directory.filePath("session")).entryList({QStringLiteral("*.pdf")}, QDir::Files).isEmpty());
    }

    void invalidProtectionDoesNotPublishAnOutput()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("original.pdf");
        const auto output = directory.filePath("bad.pdf");
        QVERIFY(testdata::writeVisualPdf(path));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        QVERIFY(!source->protectedCopy(output, QStringLiteral("short"), QStringLiteral("owner-password")).error.isEmpty());
        QVERIFY(!source->protectedCopy(output, QStringLiteral("same-password"), QStringLiteral("same-password")).error.isEmpty());
        QVERIFY(!QFileInfo::exists(output));
        QVERIFY(!source->protectedCopy(path, QStringLiteral("user-password"), QStringLiteral("owner-password")).error.isEmpty());
    }

    void hiddenOcrTextKeepsRendering_data()
    {
        QTest::addColumn<int>("rotation");
        for (int angle : {0, 90, 180, 270}) { QTest::newRow(qPrintable(QString::number(angle))) << angle; }
    }

    void hiddenOcrTextKeepsRendering()
    {
        QFETCH(int, rotation);
        QTemporaryDir directory;
        const auto path = directory.filePath("scan.pdf");
        QVERIFY(testdata::writeSyntheticPdf(path, {
            "<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [100 200 400 600] /CropBox [110 220 390 580] /UserUnit 2 /Rotate " + QByteArray::number(rotation) + " >>"
        }));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const auto before = source->renderPage(0).image;
        pdfeditor::PdfOcrPage page;
        page.page = 0;
        page.words = {{QStringLiteral("EDITOR"), QRectF(0.1, 0.2, 0.3, 0.07)}, {QStringLiteral("España"), QRectF(0.5, 0.2, 0.3, 0.07)}};
        const auto result = source->addOcrLayer({page});
        QVERIFY2(result.document, qPrintable(result.error));
        QCOMPARE(result.document->renderPage(0).image, before);
        QCOMPARE(result.document->findText(QStringLiteral("EDITOR")).matches.size(), 1);
        QCOMPARE(result.document->findText(QStringLiteral("España")).matches.size(), 1);
        const auto bounds = result.document->findText(QStringLiteral("EDITOR")).matches[0].highlight.boundingRect();
        QVERIFY(qAbs(bounds.center().x() - 0.25) < 0.1);
        QVERIFY(qAbs(bounds.center().y() - 0.235) < 0.05);
        const auto saved = result.document->save(pdfeditor::PdfSaveMode::NewCopy, directory.filePath("searchable.pdf"));
        QVERIFY2(saved.document, qPrintable(saved.error));
        const auto reopened = pdfeditor::PdfDocument::open(saved.document->filePath()).document;
        QVERIFY(reopened);
        QCOMPARE(reopened->renderPage(0).image, before);
        QCOMPARE(reopened->findText(QStringLiteral("EDITOR")).matches.size(), 1);
        QVERIFY(!reopened->addOcrLayer({page}).document);
        page.words[0].area.setX(-0.1);
        QVERIFY(!source->addOcrLayer({page}).document);
    }

    void recognizesActualImagesLocallyAndBuildsASearchablePdf()
    {
        const auto languages = pdfeditor::OcrEngine::languages();
        if (languages.isEmpty()) { QSKIP("No Windows OCR languages are installed on this test host"); }
        auto tag = languages.front().tag;
        for (const auto& language : languages) { if (language.tag.startsWith(QStringLiteral("en"))) { tag = language.tag; break; } }
        const auto image = scannedImage();
        const auto recognition = pdfeditor::OcrEngine::recognize(image, tag);
        QVERIFY2(recognition.error.isEmpty(), qPrintable(recognition.error));
        QVERIFY2(recognition.text.contains(QStringLiteral("EDITOR"), Qt::CaseInsensitive), qPrintable(recognition.text));
        QVERIFY2(recognition.text.contains(QStringLiteral("2026")), qPrintable(recognition.text));
        QVERIFY(!recognition.words.isEmpty());
        QTemporaryDir directory;
        const auto path = directory.filePath("image-only.pdf");
        QVERIFY(testdata::writeSyntheticPdf(path, {
            "<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 640 360] >>"
        }));
        const auto blank = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(blank);
        const auto source = blank->insertImage(0, QRectF(0, 0, 1, 1), image);
        QVERIFY2(source.document, qPrintable(source.error));
        QVERIFY(source.document->findText(QStringLiteral("EDITOR")).matches.isEmpty());
        const auto batch = pdfeditor::OcrEngine::recognizePdf(source.document, 0, 0, tag);
        QVERIFY2(batch.error.isEmpty(), qPrintable(batch.error));
        QCOMPARE(batch.pages.size(), 1);
        const auto layered = source.document->addOcrLayer(batch.pages);
        QVERIFY2(layered.document, qPrintable(layered.error));
        QCOMPARE(layered.document->renderPage(0).image, source.document->renderPage(0).image);
        QVERIFY(!layered.document->findText(QStringLiteral("EDITOR")).matches.isEmpty());
        const auto skipped = pdfeditor::OcrEngine::recognizePdf(layered.document, 0, 0, tag);
        QCOMPARE(skipped.skippedPages, 1);
        QVERIFY(skipped.pages.isEmpty());
        const auto cancelled = pdfeditor::OcrEngine::recognizePdf(source.document, 0, 0, tag, [] { return true; });
        QVERIFY(cancelled.cancelled && cancelled.pages.isEmpty());
        QVERIFY(!pdfeditor::OcrEngine::recognize({}, tag).error.isEmpty());
        QVERIFY(!pdfeditor::OcrEngine::recognize(image, QStringLiteral("invalid-language-tag-xyz")).error.isEmpty());
    }
};

QTEST_MAIN(SecurityOcrTest)
#include "SecurityOcrTest.moc"
