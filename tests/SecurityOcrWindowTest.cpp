#include "ui/MainWindow.h"
#include "ui/WorkspaceWindow.h"
#include "ui/OcrDialog.h"
#include "PdfTestData.h"
#include "VisualPdfData.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QFontDatabase>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

namespace {
QImage scanImage()
{
    QImage image(1280, 720, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    QFont font(QStringLiteral("Arial")); font.setPixelSize(62);
    painter.setFont(font); painter.setPen(Qt::black);
    painter.drawText(70, 180, QStringLiteral("PDF EDITOR LOCAL 2026"));
    painter.drawText(70, 330, QStringLiteral("Documento escaneado"));
    return image;
}
bool writeScan(const QString& path)
{
    const auto empty = path + QStringLiteral(".empty.pdf");
    if (!testdata::writeSyntheticPdf(empty, {"<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 640 360] >>"})) { return false; }
    const auto blank = pdfeditor::PdfDocument::open(empty).document;
    if (!blank) { return false; }
    const auto image = blank->insertImage(0, QRectF(0, 0, 1, 1), scanImage());
    return image.document && image.document->save(pdfeditor::PdfSaveMode::NewCopy, path).document;
}
QString englishLanguage()
{
    const auto languages = pdfeditor::OcrEngine::languages();
    for (const auto& language : languages) { if (language.tag.startsWith(QStringLiteral("en"))) { return language.tag; } }
    return languages.isEmpty() ? QString{} : languages.front().tag;
}
}

class SecurityOcrWindowTest final : public QObject
{
    Q_OBJECT
public:
    static void initMain() { QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs); }
private:
    static QAction* action(pdfeditor::MainWindow& window, const char* name) { return window.findChild<QAction*>(QString::fromLatin1(name)); }

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

    void protectsAndOpensWithPasswordRetriesInReadOnlyMode()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("original.pdf");
        const auto output = directory.filePath("protected.pdf");
        QVERIFY(testdata::writeVisualPdf(path));
        const auto originalHash = testdata::digest(path);
        pdfeditor::MainWindow window;
        window.show(); window.openDocument(path);
        auto* protect = action(window, "protectPdfAction");
        auto* rotate = action(window, "rotatePageAction");
        QVERIFY(protect && rotate);
        QTRY_VERIFY(protect->isEnabled());
        rotate->trigger();
        QTRY_VERIFY(window.isWindowModified() && protect->isEnabled());
        bool dialogSeen = false;
        bool invalidRejected = false;
        bool pickerSeen = false;
        QTimer driver;
        driver.setInterval(30);
        connect(&driver, &QTimer::timeout, &window, [&] {
            if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("protectPdfDialog")); dialog && !dialogSeen) {
                dialogSeen = true;
                auto* apply = dialog->findChild<QPushButton*>(QStringLiteral("protectApply"));
                invalidRejected = !apply->isEnabled();
                for (const auto* name : {"protectUserPassword", "protectUserConfirm"}) { dialog->findChild<QLineEdit*>(QString::fromLatin1(name))->setText(QStringLiteral("user-password-2026")); }
                for (const auto* name : {"protectOwnerPassword", "protectOwnerConfirm"}) { dialog->findChild<QLineEdit*>(QString::fromLatin1(name))->setText(QStringLiteral("owner-password-2026")); }
                dialog->findChild<QCheckBox*>(QStringLiteral("protectAllowCopy"))->setChecked(false);
                dialog->findChild<QCheckBox*>(QStringLiteral("protectAllowPrint"))->setChecked(false);
                const auto artifacts = qEnvironmentVariable("PDF_EDITOR_TEST_ARTIFACT_DIR");
                if (!artifacts.isEmpty() && QDir().mkpath(artifacts)) { dialog->grab().save(QDir(artifacts).filePath("protect-dialog.png")); }
                apply->click();
            } else if (auto* picker = qobject_cast<QFileDialog*>(QApplication::activeModalWidget()); picker && !pickerSeen) {
                pickerSeen = true;
                picker->setDirectory(directory.path()); picker->selectFile(output);
                QTimer::singleShot(300, picker, [picker, output] {
                    if (auto* edit = picker->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"))) { edit->setText(output); }
                    if (auto* buttons = picker->findChild<QDialogButtonBox*>()) { buttons->button(QDialogButtonBox::Save)->click(); }
                });
            }
        });
        QTimer::singleShot(10000, &window, [] { if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) { dialog->reject(); } });
        driver.start(); protect->trigger(); driver.stop();
        QVERIFY(dialogSeen && pickerSeen && invalidRejected);
        QTRY_VERIFY(QFileInfo::exists(output) && protect->isEnabled());
        QVERIFY(window.isWindowModified());
        QCOMPARE(window.currentFilePath(), path);
        QCOMPARE(testdata::digest(path), originalHash);
        int attempts = 0;
        QTimer passwordDriver;
        passwordDriver.setInterval(30);
        connect(&passwordDriver, &QTimer::timeout, &window, [&] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                for (auto* button : box->buttons()) { if (box->buttonRole(button) == QMessageBox::DestructiveRole) { button->click(); return; } }
            } else if (auto* input = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) {
                input->setTextValue(attempts++ == 0 ? QStringLiteral("wrong-password") : QStringLiteral("user-password-2026")); input->accept();
            }
        });
        passwordDriver.start(); window.openDocument(output);
        QTRY_COMPARE(window.currentFilePath(), output);
        passwordDriver.stop();
        QCOMPARE(attempts, 2);
        QVERIFY(window.sessionDocument(QStringLiteral("test")).document->isEncrypted());
        QVERIFY(!rotate->isEnabled() && !protect->isEnabled());
        QVERIFY(!action(window, "ocrAction")->isEnabled());
        QVERIFY(!action(window, "exportPageAction")->isEnabled());
        QVERIFY(!action(window, "printAction")->isEnabled());
        QVERIFY(!window.isWindowModified());
    }

    void cancelledPasswordLeavesThePreviousDocumentOpen()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("source.pdf");
        const auto output = directory.filePath("protected.pdf");
        QVERIFY(testdata::writeVisualPdf(path));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        QVERIFY(source->protectedCopy(output, QStringLiteral("user-password"), QStringLiteral("owner-password")).error.isEmpty());
        pdfeditor::MainWindow window;
        window.show(); window.openDocument(path);
        QTRY_COMPARE(window.currentFilePath(), path);
        QTimer driver;
        driver.setInterval(30);
        connect(&driver, &QTimer::timeout, &window, [] { if (auto* input = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) { input->reject(); } });
        QSignalSpy failed(&window, &pdfeditor::MainWindow::documentOpenFailed);
        driver.start(); window.openDocument(output);
        QTRY_COMPARE(failed.size(), 1);
        driver.stop();
        QCOMPARE(window.currentFilePath(), path);
    }

    void encryptedDocumentsAreNotWrittenToSessionSnapshots()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("source.pdf");
        const auto output = directory.filePath("protected.pdf");
        const auto sessionPath = directory.filePath("session");
        QVERIFY(testdata::writeVisualPdf(path));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        QVERIFY(source->protectedCopy(output, QStringLiteral("user-password"), QStringLiteral("owner-password")).error.isEmpty());
        {
            pdfeditor::WorkspaceWindow window(sessionPath);
            window.show();
            QTimer driver;
            driver.setInterval(30);
            connect(&driver, &QTimer::timeout, &window, [] {
                if (auto* input = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) { input->setTextValue(QStringLiteral("user-password")); input->accept(); }
            });
            driver.start(); window.openDocument(output);
            QTRY_COMPARE(window.documentCount(), 1);
            driver.stop();
            QSignalSpy saved(&window, &pdfeditor::WorkspaceWindow::sessionSaved);
            window.flushSession(); QTRY_VERIFY(!saved.isEmpty());
            QVERIFY2(saved.back()[0].toString().isEmpty(), qPrintable(saved.back()[0].toString()));
        }
        pdfeditor::SessionStore store(sessionPath);
        QVERIFY(store.read().entries.isEmpty());
        QVERIFY(QDir(sessionPath).entryList({QStringLiteral("*.pdf")}, QDir::Files).isEmpty());
        QFile index(QDir(sessionPath).filePath(QStringLiteral("session.json")));
        QVERIFY(index.open(QIODevice::ReadOnly));
        const auto bytes = index.readAll();
        QVERIFY(!bytes.contains("user-password") && !bytes.contains("owner-password"));
    }

    void exportsRecognizedTextWithoutChangingThePdf()
    {
        const auto tag = englishLanguage();
        if (tag.isEmpty()) { QSKIP("No OCR language is installed on this host"); }
        QTemporaryDir directory;
        const auto path = directory.filePath("scan.pdf");
        const auto output = directory.filePath("recognized.txt");
        QVERIFY(writeScan(path));
        const auto hash = testdata::digest(path);
        const auto document = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(document);
        pdfeditor::OcrDialog dialog(document, 0);
        bool started = false;
        bool exporting = false;
        bool pickerSeen = false;
        QTimer driver;
        driver.setInterval(30);
        connect(&driver, &QTimer::timeout, &dialog, [&] {
            if (auto* picker = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
                if (pickerSeen) { return; }
                pickerSeen = true;
                picker->setDirectory(directory.path()); picker->selectFile(output);
                QTimer::singleShot(100, picker, [picker, output] {
                    if (auto* edit = picker->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"))) { edit->setText(output); }
                    if (auto* buttons = picker->findChild<QDialogButtonBox*>()) { buttons->button(QDialogButtonBox::Save)->click(); }
                });
                return;
            }
            if (!started) {
                started = true;
                auto* language = dialog.findChild<QComboBox*>(QStringLiteral("ocrLanguage"));
                language->setCurrentIndex(language->findData(tag));
                dialog.findChild<QPushButton*>(QStringLiteral("ocrRecognize"))->click(); return;
            }
            auto* exportButton = dialog.findChild<QPushButton*>(QStringLiteral("ocrExportText"));
            if (!exporting && exportButton->isEnabled()) {
                exporting = true;
                QTimer::singleShot(0, exportButton, &QPushButton::click);
                return;
            }
            if (QFileInfo::exists(output)) { dialog.reject(); }
        });
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, &dialog, [&] {
            if (auto* picker = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) { picker->reject(); }
            dialog.reject();
        });
        timeout.start(15000); driver.start();
        QCOMPARE(dialog.exec(), QDialog::Rejected);
        driver.stop(); timeout.stop();
        QVERIFY(started && exporting && pickerSeen);
        QFile text(output);
        QVERIFY(text.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromUtf8(text.readAll()).contains(QStringLiteral("EDITOR"), Qt::CaseInsensitive));
        QCOMPARE(testdata::digest(path), hash);
        QVERIFY(document->findText(QStringLiteral("EDITOR")).matches.isEmpty());
    }

    void recognizesAndAddsASearchableLayerWithHistory()
    {
        const auto tag = englishLanguage();
        if (tag.isEmpty()) { QSKIP("No OCR language is installed on this host"); }
        QTemporaryDir directory;
        const auto path = directory.filePath("scan.pdf");
        QVERIFY(writeScan(path));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const auto before = source->renderPage(0).image;
        pdfeditor::MainWindow window;
        window.show(); window.openDocument(path);
        auto* ocr = action(window, "ocrAction");
        auto* undo = action(window, "undoAction");
        auto* redo = action(window, "redoAction");
        auto* save = action(window, "saveAction");
        QVERIFY(ocr && undo && redo && save);
        QTRY_VERIFY(ocr->isEnabled());
        bool started = false;
        bool reviewed = false;
        QTimer driver;
        driver.setInterval(30);
        connect(&driver, &QTimer::timeout, &window, [&] {
            auto* dialog = window.findChild<QDialog*>(QStringLiteral("ocrDialog"));
            if (!dialog) { return; }
            auto* apply = dialog->findChild<QPushButton*>(QStringLiteral("ocrApply"));
            if (!started) {
                auto* language = dialog->findChild<QComboBox*>(QStringLiteral("ocrLanguage"));
                language->setCurrentIndex(language->findData(tag));
                started = true; dialog->findChild<QPushButton*>(QStringLiteral("ocrRecognize"))->click(); return;
            }
            if (!apply->isEnabled()) { return; }
            reviewed = dialog->findChild<QPlainTextEdit*>(QStringLiteral("ocrText"))->toPlainText().contains(QStringLiteral("EDITOR"), Qt::CaseInsensitive);
            const auto artifacts = qEnvironmentVariable("PDF_EDITOR_TEST_ARTIFACT_DIR");
            if (!artifacts.isEmpty() && QDir().mkpath(artifacts)) { dialog->grab().save(QDir(artifacts).filePath("ocr-dialog.png")); }
            driver.stop(); apply->click();
        });
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, &window, [&] { if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("ocrDialog"))) { dialog->reject(); } });
        timeout.start(15000); driver.start(); ocr->trigger(); driver.stop(); timeout.stop();
        QVERIFY(started && reviewed);
        QTRY_VERIFY(window.isWindowModified() && ocr->isEnabled());
        auto document = window.sessionDocument(QStringLiteral("test")).document;
        QCOMPARE(document->renderPage(0).image, before);
        QVERIFY2(!document->findText(QStringLiteral("EDITOR")).matches.isEmpty(), qPrintable(document->selectText(0, {0, 0}, {1, 1}).match.text));
        undo->trigger(); QVERIFY(!window.isWindowModified());
        redo->trigger(); save->trigger();
        QTRY_VERIFY(!window.isWindowModified() && ocr->isEnabled());
        const auto reopened = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(reopened);
        QVERIFY(!reopened->findText(QStringLiteral("EDITOR")).matches.isEmpty());
        QCOMPARE(reopened->renderPage(0).image, before);
    }
};

QTEST_MAIN(SecurityOcrWindowTest)
#include "SecurityOcrWindowTest.moc"
