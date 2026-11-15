#include "ui/MainWindow.h"
#include "ui/PageView.h"
#include "PdfTestData.h"
#include "VisualPdfData.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

class AdvancedWindowTest final : public QObject
{
    Q_OBJECT
public:
    static void initMain() { QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs); }

private:
    static bool runWithPicker(pdfeditor::MainWindow& window, QAction* action, const QString& path)
    {
        bool configured = false;
        bool submitted = false;
        QTimer driver;
        driver.setInterval(30);
        connect(&driver, &QTimer::timeout, &window, [&] {
            auto* picker = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            if (!picker || configured) { return; }
            configured = true;
            picker->setDirectory(QFileInfo(path).absolutePath());
            picker->selectFile(path);
            QTimer::singleShot(300, picker, [picker, path, &submitted] {
                if (auto* edit = picker->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"))) { edit->setText(path); }
                if (auto* buttons = picker->findChild<QDialogButtonBox*>()) {
                    auto* button = buttons->button(picker->acceptMode() == QFileDialog::AcceptSave ? QDialogButtonBox::Save : QDialogButtonBox::Open);
                    if (button && button->isEnabled()) { submitted = true; button->click(); }
                }
            });
        });
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, &window, [] { if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) { dialog->reject(); } });
        driver.start();
        timeout.start(6000);
        action->trigger();
        return configured && submitted;
    }

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

    void createsAndFillsFieldsWithHistory_data()
    {
        QTest::addColumn<int>("type");
        QTest::newRow("text") << 0;
        QTest::newRow("checkbox") << 1;
        QTest::newRow("combo") << 2;
    }

    void createsAndFillsFieldsWithHistory()
    {
        QFETCH(int, type);
        QTemporaryDir directory;
        const auto path = directory.filePath("form.pdf");
        QVERIFY(testdata::writeVisualPdf(path, 0, 2));
        pdfeditor::MainWindow window;
        window.show();
        auto* create = action(window, "createFormFieldAction");
        auto* fill = action(window, "fillFormAction");
        auto* undo = action(window, "undoAction");
        auto* redo = action(window, "redoAction");
        auto* save = action(window, "saveAction");
        QVERIFY(create && fill && undo && redo && save);
        QVERIFY(!create->isEnabled());
        window.openDocument(path);
        QTRY_VERIFY(create->isEnabled());
        bool rejectsEmpty = false;
        QTimer::singleShot(0, &window, [&] {
            if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("createFormFieldDialog"))) {
                auto* apply = dialog->findChild<QPushButton*>(QStringLiteral("fieldApply"));
                rejectsEmpty = !apply->isEnabled();
                dialog->findChild<QLineEdit*>(QStringLiteral("fieldName"))->setText(QStringLiteral("NuevoCampo"));
                dialog->findChild<QComboBox*>(QStringLiteral("fieldType"))->setCurrentIndex(type);
                dialog->findChild<QPlainTextEdit*>(QStringLiteral("fieldOptions"))->setPlainText(QStringLiteral("Uno\nDos"));
                const auto artifacts = qEnvironmentVariable("PDF_EDITOR_TEST_ARTIFACT_DIR");
                if (type == 2 && !artifacts.isEmpty() && QDir().mkpath(artifacts)) { dialog->grab().save(QDir(artifacts).filePath("create-form-dialog.png")); }
                apply->click();
            }
        });
        create->trigger();
        QVERIFY(rejectsEmpty);
        QTRY_VERIFY(create->isEnabled() && window.isWindowModified());
        QTimer::singleShot(0, &window, [&] {
            if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                if (auto* text = dialog->findChild<QLineEdit*>(QStringLiteral("formText"))) { text->setText(QStringLiteral("Contenido")); }
                if (auto* check = dialog->findChild<QCheckBox*>(QStringLiteral("formCheckbox"))) { check->setChecked(true); }
                if (auto* choice = dialog->findChild<QComboBox*>(QStringLiteral("formChoice"))) { choice->setCurrentIndex(1); }
                if (auto* buttons = dialog->findChild<QDialogButtonBox*>()) { buttons->button(QDialogButtonBox::Save)->click(); }
            }
        });
        fill->trigger();
        QTRY_VERIFY(fill->isEnabled());
        undo->trigger();
        QVERIFY(window.isWindowModified());
        undo->trigger();
        QVERIFY(!window.isWindowModified());
        redo->trigger();
        redo->trigger();
        save->trigger();
        QTRY_VERIFY(!window.isWindowModified() && create->isEnabled());
        const auto reopened = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(reopened);
        const auto fields = reopened->pageFormFields(0);
        QCOMPARE(fields.size(), 1);
        QCOMPARE(int(fields[0].type), type);
        if (type == 0) { QCOMPARE(fields[0].value.toString(), QStringLiteral("Contenido")); }
        else if (type == 1) { QVERIFY(fields[0].value.toBool()); }
        else { QCOMPARE(fields[0].value.toString(), QStringLiteral("Dos")); }
    }

    void addsDeletesAndRestoresShapes()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("shape.pdf");
        QVERIFY(testdata::writeVisualPdf(path, 90, 2));
        pdfeditor::MainWindow window;
        window.show();
        window.openDocument(path);
        auto* add = action(window, "addShapeAction");
        auto* manage = action(window, "pageShapesAction");
        auto* undo = action(window, "undoAction");
        auto* redo = action(window, "redoAction");
        auto* save = action(window, "saveAction");
        auto* view = window.findChild<pdfeditor::PageView*>();
        QVERIFY(add && manage && undo && redo && save && view);
        QTRY_VERIFY(add->isEnabled());
        bool invalidRejected = false;
        QTimer::singleShot(0, &window, [&] {
            if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("addShapeDialog"))) {
                auto* left = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("shapeLeft"));
                auto* apply = dialog->findChild<QPushButton*>(QStringLiteral("shapeApply"));
                left->setValue(90);
                invalidRejected = !apply->isEnabled();
                left->setValue(15);
                dialog->findChild<QCheckBox*>(QStringLiteral("shapeFilled"))->setChecked(true);
                dialog->findChild<QLineEdit*>(QStringLiteral("shapeComment"))->setText(QStringLiteral("Revisar esta zona"));
                apply->click();
            }
        });
        add->trigger();
        QVERIFY(invalidRejected);
        QTRY_VERIFY(window.isWindowModified() && add->isEnabled());
        undo->trigger();
        QVERIFY(!window.isWindowModified());
        redo->trigger();
        QTRY_VERIFY(view->hasPageImage() && !view->isRendering());
        const auto artifacts = qEnvironmentVariable("PDF_EDITOR_TEST_ARTIFACT_DIR");
        if (!artifacts.isEmpty() && QDir().mkpath(artifacts)) { window.grab().save(QDir(artifacts).filePath("shapes-window.png")); }
        QTimer::singleShot(0, &window, [&] {
            if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("pageShapesDialog"))) { dialog->findChild<QPushButton*>(QStringLiteral("deleteShapeButton"))->click(); }
        });
        manage->trigger();
        QTRY_VERIFY(manage->isEnabled());
        QVERIFY(window.sessionDocument(QStringLiteral("test")).document->pageShapes(0).isEmpty());
        undo->trigger();
        save->trigger();
        QTRY_VERIFY(!window.isWindowModified() && save->isEnabled() == false);
        const auto reopened = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(reopened);
        QCOMPARE(reopened->pageShapes(0).size(), 1);
    }

    void managesAttachmentFilesAndHistory()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("attachments.pdf");
        const auto input = directory.filePath("payload.bin");
        const auto output = directory.filePath("exported.bin");
        QVERIFY(testdata::writeVisualPdf(path));
        QFile file(input);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("Native attachment payload");
        file.close();
        pdfeditor::MainWindow window;
        window.show();
        window.openDocument(path);
        auto* manage = action(window, "attachmentsAction");
        auto* undo = action(window, "undoAction");
        auto* redo = action(window, "redoAction");
        auto* save = action(window, "saveAction");
        QVERIFY(manage && undo && redo && save);
        QTRY_VERIFY(manage->isEnabled());
        QTimer::singleShot(0, &window, [&] {
            if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("attachmentsDialog"))) { dialog->findChild<QPushButton*>(QStringLiteral("addAttachmentButton"))->click(); }
        });
        QVERIFY(runWithPicker(window, manage, input));
        QTRY_VERIFY(manage->isEnabled() && window.isWindowModified());
        QCOMPARE(window.sessionDocument(QStringLiteral("test")).document->attachments().size(), 1);
        undo->trigger();
        QVERIFY(!window.isWindowModified());
        redo->trigger();
        QTimer::singleShot(0, &window, [&] {
            if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("attachmentsDialog"))) {
                const auto artifacts = qEnvironmentVariable("PDF_EDITOR_TEST_ARTIFACT_DIR");
                if (!artifacts.isEmpty() && QDir().mkpath(artifacts)) { dialog->grab().save(QDir(artifacts).filePath("attachments-dialog.png")); }
                dialog->findChild<QPushButton*>(QStringLiteral("exportAttachmentButton"))->click();
            }
        });
        QVERIFY(runWithPicker(window, manage, output));
        QTRY_VERIFY(QFileInfo::exists(output) && manage->isEnabled());
        QCOMPARE(testdata::digest(output), testdata::digest(input));
        QVERIFY(window.isWindowModified());
        QTimer::singleShot(0, &window, [&] {
            if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("attachmentsDialog"))) { dialog->findChild<QPushButton*>(QStringLiteral("removeAttachmentButton"))->click(); }
        });
        manage->trigger();
        QTRY_VERIFY(manage->isEnabled());
        QVERIFY(window.sessionDocument(QStringLiteral("test")).document->attachments().isEmpty());
        undo->trigger();
        save->trigger();
        QTRY_VERIFY(!window.isWindowModified() && manage->isEnabled());
        const auto reopened = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(reopened);
        QCOMPARE(reopened->attachments().size(), 1);
    }

    void optimizedExportKeepsPendingChangesAndTheActiveDocument()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("original.pdf");
        const auto output = directory.filePath("optimized.pdf");
        QVERIFY(testdata::writeReorderPdf(path));
        const auto digest = testdata::digest(path);
        pdfeditor::MainWindow window;
        window.show();
        window.openDocument(path);
        auto* optimize = action(window, "optimizePdfAction");
        auto* rotate = action(window, "rotatePageAction");
        auto* undo = action(window, "undoAction");
        QVERIFY(optimize && rotate && undo);
        QTRY_VERIFY(rotate->isEnabled());
        rotate->trigger();
        QTRY_VERIFY(optimize->isEnabled() && window.isWindowModified());
        QVERIFY(runWithPicker(window, optimize, output));
        QTRY_VERIFY(QFileInfo::exists(output) && optimize->isEnabled());
        QCOMPARE(window.currentFilePath(), path);
        QVERIFY(window.isWindowModified() && undo->isEnabled());
        const auto reopened = pdfeditor::PdfDocument::open(output).document;
        QVERIFY(reopened);
        QCOMPARE(reopened->renderPage(0).image, window.sessionDocument(QStringLiteral("test")).document->renderPage(0).image);
        QCOMPARE(testdata::digest(path), digest);
        undo->trigger();
        QVERIFY(!window.isWindowModified());
    }

    void drawsFreehandWithPreviewHistoryAndSave()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("freehand.pdf");
        QVERIFY(testdata::writeVisualPdf(path, 0, 2));
        pdfeditor::MainWindow window;
        window.show();
        window.openDocument(path);
        auto* draw = action(window, "drawInkAction");
        auto* undo = action(window, "undoAction");
        auto* redo = action(window, "redoAction");
        auto* save = action(window, "saveAction");
        QVERIFY(draw && undo && redo && save);
        QTRY_VERIFY(draw->isEnabled());
        bool exercised = false;
        bool initialDisabled = false;
        bool undoDisabled = false;
        bool clearDisabled = false;
        QTimer driver;
        driver.setInterval(30);
        connect(&driver, &QTimer::timeout, &window, [&] {
            auto* dialog = window.findChild<QDialog*>(QStringLiteral("inkDialog"));
            if (!dialog) { return; }
            auto* canvas = dialog->findChild<QWidget*>(QStringLiteral("inkCanvas"));
            if (!canvas || !canvas->property("previewReady").toBool()) { return; }
            driver.stop();
            auto* apply = dialog->findChild<QPushButton*>(QStringLiteral("inkApply"));
            initialDisabled = !apply->isEnabled();
            const auto drawStroke = [&] {
                const auto center = canvas->rect().center();
                QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(60, 20));
                QTest::mouseMove(canvas, center - QPoint(20, -20), 10);
                QTest::mouseMove(canvas, center + QPoint(20, -20), 10);
                QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, center + QPoint(60, 20));
            };
            drawStroke();
            dialog->findChild<QPushButton*>(QStringLiteral("inkUndo"))->click();
            undoDisabled = !apply->isEnabled();
            drawStroke();
            dialog->findChild<QPushButton*>(QStringLiteral("inkClear"))->click();
            clearDisabled = !apply->isEnabled();
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("inkLineWidth"))->setValue(4);
            drawStroke();
            const auto artifacts = qEnvironmentVariable("PDF_EDITOR_TEST_ARTIFACT_DIR");
            if (!artifacts.isEmpty() && QDir().mkpath(artifacts)) { dialog->grab().save(QDir(artifacts).filePath("freehand-dialog.png")); }
            exercised = apply->isEnabled();
            apply->click();
        });
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, &window, [&] { if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("inkDialog"))) { dialog->reject(); } });
        timeout.start(10000);
        driver.start();
        draw->trigger();
        driver.stop();
        timeout.stop();
        QVERIFY(exercised && initialDisabled && undoDisabled && clearDisabled);
        QTRY_VERIFY(draw->isEnabled() && window.isWindowModified());
        auto document = window.sessionDocument(QStringLiteral("test")).document;
        QCOMPARE(document->pageShapes(0).size(), 1);
        QCOMPARE(document->pageShapes(0)[0].type, pdfeditor::PdfShapeType::Freehand);
        undo->trigger();
        QVERIFY(!window.isWindowModified());
        redo->trigger();
        save->trigger();
        QTRY_VERIFY(!window.isWindowModified() && draw->isEnabled());
        const auto reopened = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(reopened);
        QCOMPARE(reopened->pageShapes(0).size(), 1);
        QCOMPARE(reopened->renderPage(0).image, document->renderPage(0).image);
        QTimer::singleShot(0, &window, [&] { if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("inkDialog"))) { dialog->reject(); } });
        draw->trigger();
        QVERIFY(!window.isWindowModified());
    }
};

QTEST_MAIN(AdvancedWindowTest)
#include "AdvancedWindowTest.moc"
