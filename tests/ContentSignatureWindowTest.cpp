#include "ui/MainWindow.h"
#include "ui/ContentDialog.h"
#include "PdfTestData.h"
#include "VisualPdfData.h"
#include <pdfcertificatemanager.h>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

class ContentSignatureWindowTest final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
            for (const auto* face : {"arial.ttf", "arialbd.ttf", "ariali.ttf", "arialbi.ttf"}) { QVERIFY(QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts/") + QString::fromLatin1(face))) >= 0); }
        }
#endif
    }
    void editsOriginalTextWithHistoryAndSave()
    {
        QTemporaryDir directory; const auto path = directory.filePath("source.pdf"); QVERIFY(testdata::writeVisualPdf(path));
        pdfeditor::MainWindow window; window.show(); window.openDocument(path);
        auto* edit = window.findChild<QAction*>(QStringLiteral("editContentAction")); QVERIFY(edit); QTRY_VERIFY(edit->isEnabled());
        bool selected = false;
        QTimer driver; driver.setInterval(30);
        connect(&driver, &QTimer::timeout, &window, [&] {
            auto* dialog = window.findChild<QDialog*>(QStringLiteral("contentDialog")); if (!dialog) { return; }
            auto* list = dialog->findChild<QListWidget*>(QStringLiteral("contentItems")); if (!list || list->count() == 0) { return; }
            for (int row = 0; row < list->count(); ++row) { if (list->item(row)->text().contains(QStringLiteral("PDF Editor"))) { list->setCurrentRow(row); selected = true; break; } }
            if (!selected) { return; }
            auto* operation = dialog->findChild<QComboBox*>(QStringLiteral("contentOperation")); operation->setCurrentIndex(operation->findData(static_cast<int>(pdfeditor::PdfContentChange::ReplaceText)));
            dialog->findChild<QLineEdit*>(QStringLiteral("contentReplacementText"))->setText(QStringLiteral("PDF Nuevo"));
            const auto artifacts = qEnvironmentVariable("PDF_EDITOR_TEST_ARTIFACT_DIR");
            if (!artifacts.isEmpty() && QDir().mkpath(artifacts)) { dialog->grab().save(QDir(artifacts).filePath(QStringLiteral("content-dialog.png"))); }
            driver.stop(); dialog->findChild<QPushButton*>(QStringLiteral("contentApply"))->click();
        });
        QTimer timeout; timeout.setSingleShot(true); connect(&timeout, &QTimer::timeout, &window, [&] { if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("contentDialog"))) { dialog->reject(); } });
        timeout.start(10000); driver.start(); edit->trigger(); driver.stop(); timeout.stop(); QVERIFY(selected);
        QTRY_VERIFY(window.isWindowModified() && edit->isEnabled());
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction")); auto* redo = window.findChild<QAction*>(QStringLiteral("redoAction"));
        QVERIFY(undo && redo); undo->trigger(); QVERIFY(!window.isWindowModified()); redo->trigger();
        window.findChild<QAction*>(QStringLiteral("saveAction"))->trigger(); QTRY_VERIFY(!window.isWindowModified() && edit->isEnabled());
        const auto reopened = pdfeditor::PdfDocument::open(path).document; QVERIFY(reopened); QVERIFY(!reopened->findText(QStringLiteral("PDF Nuevo")).matches.isEmpty());
    }
    void changesSavedFreehandStyleWithUndo()
    {
        QTemporaryDir directory; const auto sourcePath = directory.filePath("source.pdf"); const auto path = directory.filePath("ink.pdf");
        QVERIFY(testdata::writeVisualPdf(sourcePath)); const auto source = pdfeditor::PdfDocument::open(sourcePath).document; QVERIFY(source);
        const auto ink = source->addInk(0, {QPolygonF{{.1, .1}, {.3, .2}, {.5, .1}}}, Qt::red, 2); QVERIFY(ink.document);
        QVERIFY(ink.document->save(pdfeditor::PdfSaveMode::NewCopy, path).document);
        pdfeditor::MainWindow window; window.show(); window.openDocument(path);
        auto* shapes = window.findChild<QAction*>(QStringLiteral("pageShapesAction")); QVERIFY(shapes); QTRY_VERIFY(shapes->isEnabled());
        const auto before = window.sessionDocument(QStringLiteral("test")).document->renderPage(0).image;
        bool styled = false;
        QTimer driver; driver.setInterval(30);
        connect(&driver, &QTimer::timeout, &window, [&] {
            if (auto* style = window.findChild<QDialog*>(QStringLiteral("shapeStyleDialog")); style && style->isVisible()) {
                style->findChild<QDoubleSpinBox*>(QStringLiteral("restyleWidth"))->setValue(7);
                style->findChild<QPushButton*>(QStringLiteral("restyleStroke"))->setProperty("color", QColor(Qt::blue));
                styled = true; driver.stop(); style->findChild<QPushButton*>(QStringLiteral("restyleApply"))->click(); return;
            }
            if (auto* list = window.findChild<QDialog*>(QStringLiteral("pageShapesDialog")); list && list->isVisible()) {
                list->findChild<QListWidget*>(QStringLiteral("pageShapesList"))->setCurrentRow(0);
                list->findChild<QPushButton*>(QStringLiteral("restyleShapeButton"))->click();
            }
        });
        QTimer timeout; timeout.setSingleShot(true); connect(&timeout, &QTimer::timeout, &window, [] { if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) { modal->reject(); } });
        timeout.start(10000); driver.start(); shapes->trigger(); driver.stop(); timeout.stop(); QVERIFY(styled);
        QTRY_VERIFY(window.isWindowModified() && shapes->isEnabled());
        QVERIFY(window.sessionDocument(QStringLiteral("test")).document->renderPage(0).image != before);
        window.findChild<QAction*>(QStringLiteral("undoAction"))->trigger();
        QCOMPARE(window.sessionDocument(QStringLiteral("test")).document->renderPage(0).image, before);
    }
    void signsACopyAndShowsSeparateIntegrityAndTrustResults()
    {
        QTemporaryDir directory; const auto path = directory.filePath("source.pdf"); const auto output = directory.filePath("signed.pdf");
        QVERIFY(testdata::writeVisualPdf(path)); const auto hash = testdata::digest(path);
        pdf::PDFCertificateManager::NewCertificateInfo info; info.fileName = directory.filePath("test.p12"); info.privateKeyPasword = QStringLiteral("test-password"); info.certCommonName = QStringLiteral("Synthetic UI Test"); info.rsaKeyLength = 2048;
        pdf::PDFCertificateManager manager; manager.createCertificate(info); QVERIFY(QFileInfo::exists(info.fileName));
        pdfeditor::MainWindow window; window.show(); window.openDocument(path);
        auto* sign = window.findChild<QAction*>(QStringLiteral("signCopyAction")); QVERIFY(sign); QTRY_VERIFY(sign->isEnabled());
        bool configured = false; bool pickerSeen = false;
        QTimer driver; driver.setInterval(30);
        connect(&driver, &QTimer::timeout, &window, [&] {
            if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("signCopyDialog")); dialog && dialog->isVisible() && !configured) {
                dialog->findChild<QLineEdit*>(QStringLiteral("signCertificate"))->setText(info.fileName);
                dialog->findChild<QLineEdit*>(QStringLiteral("signPassword"))->setText(info.privateKeyPasword);
                dialog->findChild<QLineEdit*>(QStringLiteral("signReason"))->setText(QStringLiteral("UI signing test"));
                configured = true; dialog->findChild<QPushButton*>(QStringLiteral("signApply"))->click(); return;
            }
            if (auto* picker = qobject_cast<QFileDialog*>(QApplication::activeModalWidget()); picker && !pickerSeen) {
                pickerSeen = true; picker->setDirectory(directory.path()); picker->selectFile(output);
                QTimer::singleShot(200, picker, [picker, output] { if (auto* name = picker->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"))) { name->setText(output); } picker->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click(); });
            }
        });
        QTimer timeout; timeout.setSingleShot(true); connect(&timeout, &QTimer::timeout, &window, [] { if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) { modal->reject(); } });
        timeout.start(10000); driver.start(); sign->trigger(); driver.stop(); timeout.stop(); QVERIFY(configured && pickerSeen);
        QTRY_VERIFY(QFileInfo::exists(output) && sign->isEnabled());
        QCOMPARE(window.currentFilePath(), path); QVERIFY(!window.isWindowModified()); QCOMPARE(testdata::digest(path), hash);
        window.openDocument(output); QTRY_COMPARE(window.currentFilePath(), output);
        auto* verify = window.findChild<QAction*>(QStringLiteral("verifySignaturesAction")); QVERIFY(verify); QTRY_VERIFY(verify->isEnabled());
        bool reviewed = false;
        connect(&driver, &QTimer::timeout, &window, [&] {
            auto* dialog = window.findChild<QDialog*>(QStringLiteral("signatureReportDialog")); if (!dialog) { return; }
            auto* report = dialog->findChild<QPlainTextEdit*>(QStringLiteral("signatureReport"));
            if (!report->property("verificationFinished").toBool()) { return; }
            reviewed = report->toPlainText().contains(QStringLiteral("Correcta")) && report->toPlainText().contains(QStringLiteral("no verificadas"));
            const auto artifacts = qEnvironmentVariable("PDF_EDITOR_TEST_ARTIFACT_DIR");
            if (!artifacts.isEmpty() && QDir().mkpath(artifacts)) { dialog->grab().save(QDir(artifacts).filePath(QStringLiteral("signature-report.png"))); }
            driver.stop(); dialog->reject();
        });
        timeout.start(10000); driver.start(); verify->trigger(); driver.stop(); timeout.stop(); QVERIFY(reviewed);
    }
};
QTEST_MAIN(ContentSignatureWindowTest)
#include "ContentSignatureWindowTest.moc"
