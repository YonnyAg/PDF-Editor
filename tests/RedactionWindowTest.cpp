#include "ui/MainWindow.h"
#include "ui/RedactionDialog.h"
#include "PdfTestData.h"
#include "VisualPdfData.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

class RedactionWindowTest final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
            QVERIFY(QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts/arial.ttf"))) >= 0);
        }
#endif
    }
    void reviewsAndRemovesAreasBeforeExport()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("source.pdf");
        QVERIFY(testdata::writeVisualPdf(path));
        const auto document = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(document);
        pdfeditor::RedactionDialog dialog(document, 0);
        dialog.show();
        auto* add = dialog.findChild<QPushButton*>(QStringLiteral("redactionAdd"));
        auto* apply = dialog.findChild<QPushButton*>(QStringLiteral("redactionApply"));
        auto* remove = dialog.findChild<QPushButton*>(QStringLiteral("redactionRemove"));
        auto* consent = dialog.findChild<QCheckBox*>(QStringLiteral("redactionConsent"));
        auto* list = dialog.findChild<QListWidget*>(QStringLiteral("redactionAreas"));
        QVERIFY(add && apply && remove && consent && list);
        QTRY_VERIFY(add->isEnabled());
        QVERIFY(!apply->isEnabled());
        auto* x = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("redactionX"));
        x->setValue(99); QVERIFY(!add->isEnabled());
        x->setValue(10); QVERIFY(add->isEnabled());
        add->click(); QCOMPARE(dialog.areas().size(), 1);
        QVERIFY(!apply->isEnabled()); consent->setChecked(true); QVERIFY(apply->isEnabled());
        add->click(); QCOMPARE(dialog.areas().size(), 2);
        QVERIFY(!consent->isChecked() && !apply->isEnabled());
        list->setCurrentRow(1); remove->click(); QCOMPARE(dialog.areas().size(), 1);
        list->setCurrentRow(0); remove->click(); QVERIFY(dialog.areas().isEmpty());
        consent->setChecked(true); QVERIFY(!apply->isEnabled());
        auto* preview = dialog.findChild<QWidget*>(QStringLiteral("redactionPreview"));
        QVERIFY(preview);
        const auto start = preview->rect().center();
        QTest::mousePress(preview, Qt::LeftButton, {}, start);
        QTest::mouseMove(preview, start + QPoint(30, 30));
        QTest::mouseRelease(preview, Qt::LeftButton, {}, start + QPoint(30, 30));
        QVERIFY(add->isEnabled()); add->click();
        QCOMPARE(dialog.areas().size(), 1);
        QVERIFY(dialog.areas()[0].area.width() < .3);
        dialog.reject();
        QVERIFY(document->findText(QStringLiteral("PDF Editor")).matches.size() > 0);
    }
    void exportsPendingViewToANewFileWithoutChangingHistory()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("source.pdf");
        const auto output = directory.filePath("redacted.pdf");
        QVERIFY(testdata::writeVisualPdf(path, 0, 1, testdata::VisualPdfExtras::AnnotationsAndForm));
        const auto hash = testdata::digest(path);
        pdfeditor::MainWindow window;
        window.show(); window.openDocument(path);
        auto* redact = window.findChild<QAction*>(QStringLiteral("redactCopyAction"));
        auto* rotate = window.findChild<QAction*>(QStringLiteral("rotatePageAction"));
        QVERIFY(redact && rotate);
        QTRY_VERIFY(rotate->isEnabled()); rotate->trigger();
        QTRY_VERIFY(window.isWindowModified() && redact->isEnabled());
        bool reviewed = false;
        bool pickerSeen = false;
        QTimer driver;
        driver.setInterval(30);
        connect(&driver, &QTimer::timeout, &window, [&] {
            if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("redactionDialog")); dialog && dialog->isVisible() && !reviewed) {
                auto* add = dialog->findChild<QPushButton*>(QStringLiteral("redactionAdd"));
                if (!add->isEnabled()) { return; }
                dialog->findChild<QDoubleSpinBox*>(QStringLiteral("redactionX"))->setValue(0);
                dialog->findChild<QDoubleSpinBox*>(QStringLiteral("redactionY"))->setValue(0);
                dialog->findChild<QDoubleSpinBox*>(QStringLiteral("redactionWidth"))->setValue(100);
                dialog->findChild<QDoubleSpinBox*>(QStringLiteral("redactionHeight"))->setValue(100);
                add->click(); dialog->findChild<QCheckBox*>(QStringLiteral("redactionConsent"))->setChecked(true);
                auto* apply = dialog->findChild<QPushButton*>(QStringLiteral("redactionApply"));
                reviewed = apply->isEnabled();
                const auto artifacts = qEnvironmentVariable("PDF_EDITOR_TEST_ARTIFACT_DIR");
                if (!artifacts.isEmpty() && QDir().mkpath(artifacts)) { dialog->grab().save(QDir(artifacts).filePath(QStringLiteral("redaction-dialog.png"))); }
                apply->click(); return;
            }
            if (auto* picker = qobject_cast<QFileDialog*>(QApplication::activeModalWidget()); picker && !pickerSeen) {
                pickerSeen = true; picker->setDirectory(directory.path()); picker->selectFile(output);
                QTimer::singleShot(200, picker, [picker, output] {
                    if (auto* name = picker->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"))) { name->setText(output); }
                    if (auto* buttons = picker->findChild<QDialogButtonBox*>()) { buttons->button(QDialogButtonBox::Save)->click(); }
                });
            }
        });
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, &window, [] { if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) { modal->reject(); } });
        timeout.start(10000); driver.start(); redact->trigger(); driver.stop(); timeout.stop();
        QVERIFY(reviewed && pickerSeen);
        QTRY_VERIFY(QFileInfo::exists(output) && redact->isEnabled());
        QVERIFY(window.isWindowModified()); QCOMPARE(window.currentFilePath(), path);
        QCOMPARE(testdata::digest(path), hash);
        const auto current = window.sessionDocument(QStringLiteral("test")).document;
        QVERIFY(!current->findText(QStringLiteral("PDF Editor")).matches.isEmpty());
        const auto copy = pdfeditor::PdfDocument::open(output).document;
        QVERIFY(copy);
        QCOMPARE(copy->pageSize(0), current->pageSize(0));
        QVERIFY(copy->selectText(0, {0, 0}, {1, 1}).match.text.isEmpty());
        const auto pixels = copy->renderPage(0).image;
        QVERIFY(!pixels.isNull()); QCOMPARE(pixels.pixelColor(pixels.width() / 2, pixels.height() / 2), QColor(Qt::black));
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        QVERIFY(undo && undo->isEnabled()); undo->trigger(); QVERIFY(!window.isWindowModified());
    }
    void cancellingKeepsTheActiveDocument()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("source.pdf");
        QVERIFY(testdata::writeVisualPdf(path));
        const auto hash = testdata::digest(path);
        pdfeditor::MainWindow window;
        window.show(); window.openDocument(path);
        auto* redact = window.findChild<QAction*>(QStringLiteral("redactCopyAction"));
        QVERIFY(redact); QTRY_VERIFY(redact->isEnabled());
        QTimer::singleShot(100, &window, [&] { if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("redactionDialog"))) { dialog->reject(); } });
        redact->trigger();
        QCOMPARE(window.currentFilePath(), path); QVERIFY(!window.isWindowModified());
        QCOMPARE(testdata::digest(path), hash);
    }
};
QTEST_MAIN(RedactionWindowTest)
#include "RedactionWindowTest.moc"
