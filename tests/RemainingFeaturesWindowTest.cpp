#include "ui/MainWindow.h"
#include "PdfTestData.h"
#include "VisualPdfData.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

class RemainingFeaturesWindowTest final : public QObject
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
    void createsFillsAndRestoresAdvancedFields_data()
    {
        QTest::addColumn<int>("type"); QTest::newRow("radio") << 3; QTest::newRow("multiple") << 4;
    }
    void createsFillsAndRestoresAdvancedFields()
    {
        QFETCH(int, type);
        QTemporaryDir directory; const auto path = directory.filePath("source.pdf"); QVERIFY(testdata::writeVisualPdf(path, 0, 2));
        pdfeditor::MainWindow window; window.show(); window.openDocument(path);
        auto* create = window.findChild<QAction*>(QStringLiteral("createFormFieldAction")); QVERIFY(create); QTRY_VERIFY(create->isEnabled());
        bool ready = false;
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = window.findChild<QDialog*>(QStringLiteral("createFormFieldDialog")); if (!dialog) { return; }
            dialog->findChild<QLineEdit*>(QStringLiteral("fieldName"))->setText(QStringLiteral("Choices")); dialog->findChild<QComboBox*>(QStringLiteral("fieldType"))->setCurrentIndex(type);
            dialog->findChild<QPlainTextEdit*>(QStringLiteral("fieldOptions"))->setPlainText(QStringLiteral("España\nMéxico\nChile"));
            dialog->findChild<QDoubleSpinBox*>(QStringLiteral("fieldHeight"))->setValue(50);
            auto* apply = dialog->findChild<QPushButton*>(QStringLiteral("fieldApply")); ready = apply->isEnabled(); if (ready) { apply->click(); } else { dialog->reject(); }
        });
        create->trigger(); QVERIFY(ready); QTRY_VERIFY(window.isWindowModified() && create->isEnabled());
        bool filled = false;
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); if (!dialog) { return; }
            if (type == 3) { if (auto* choice = dialog->findChild<QComboBox*>(QStringLiteral("formChoice"))) { choice->setCurrentIndex(1); filled = true; } }
            else { if (auto* list = dialog->findChild<QListWidget*>(QStringLiteral("formMultiChoice"))) { list->item(0)->setSelected(true); list->item(2)->setSelected(true); filled = true; } }
            const auto artifacts = qEnvironmentVariable("PDF_EDITOR_TEST_ARTIFACT_DIR"); if (!artifacts.isEmpty() && QDir().mkpath(artifacts)) { dialog->grab().save(QDir(artifacts).filePath(type == 3 ? "radio-form.png" : "multiple-form.png")); }
            if (auto* buttons = dialog->findChild<QDialogButtonBox*>()) { buttons->button(QDialogButtonBox::Save)->click(); }
        });
        window.findChild<QAction*>(QStringLiteral("fillFormAction"))->trigger(); QVERIFY(filled); QTRY_VERIFY(create->isEnabled());
        window.findChild<QAction*>(QStringLiteral("undoAction"))->trigger(); window.findChild<QAction*>(QStringLiteral("redoAction"))->trigger();
        window.findChild<QAction*>(QStringLiteral("saveAction"))->trigger(); QTRY_VERIFY(!window.isWindowModified() && create->isEnabled());
        const auto copy = pdfeditor::PdfDocument::open(path).document; QVERIFY(copy); const auto fields = copy->pageFormFields(0); QCOMPARE(fields.size(), 1);
        if (type == 3) { QCOMPARE(fields[0].value.toString(), QStringLiteral("1")); } else { QCOMPARE(fields[0].value.toStringList(), QStringList({QStringLiteral("España"), QStringLiteral("Chile")})); }
    }
    void readsTextWithAccessibleInterfacesKeyboardAndPermissionChecks()
    {
        QTemporaryDir directory; const auto path = directory.filePath("source.pdf"); QVERIFY(testdata::writeOutputPdf(path));
        pdfeditor::MainWindow window; window.show(); window.openDocument(path);
        auto* reader = window.findChild<QAction*>(QStringLiteral("accessibleReaderAction")); QVERIFY(reader); QTRY_VERIFY(reader->isEnabled());
        bool first = false; bool second = false; bool interfaces = false; bool copied = false; bool enlarged = false;
        QString original;
        QTimer driver; driver.setInterval(30);
        connect(&driver, &QTimer::timeout, &window, [&] {
            auto* dialog = window.findChild<QDialog*>(QStringLiteral("accessibleReaderDialog")); if (!dialog) { return; }
            auto* text = dialog->findChild<QPlainTextEdit*>(QStringLiteral("accessiblePageText")); if (text->toPlainText().isEmpty()) { return; }
            auto* accessible = QAccessible::queryAccessibleInterface(text);
            interfaces = accessible && accessible->textInterface() && accessible->state().readOnly && accessible->text(QAccessible::Name).contains(first ? QStringLiteral("2") : QStringLiteral("1"));
            if (!first) {
                original = text->toPlainText(); text->setFocus(); QTest::keyClick(text, Qt::Key_A, Qt::ControlModifier); QTest::keyClick(text, Qt::Key_C, Qt::ControlModifier);
                copied = QApplication::clipboard()->text() == original;
                const auto oldSize = text->font().pointSizeF(); dialog->findChild<QPushButton*>(QStringLiteral("readerLarger"))->click(); enlarged = text->font().pointSizeF() > oldSize;
                first = true; QTest::keyClick(text, Qt::Key_Right, Qt::AltModifier); return;
            }
            if (text->toPlainText() == original) { return; }
            second = true; const auto artifacts = qEnvironmentVariable("PDF_EDITOR_TEST_ARTIFACT_DIR");
            if (!artifacts.isEmpty() && QDir().mkpath(artifacts)) { dialog->grab().save(QDir(artifacts).filePath("accessible-reader.png")); }
            driver.stop(); dialog->reject();
        });
        QTimer timeout; timeout.setSingleShot(true); connect(&timeout, &QTimer::timeout, &window, [&] { if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("accessibleReaderDialog"))) { dialog->reject(); } });
        timeout.start(10000); driver.start(); reader->trigger(); driver.stop(); timeout.stop();
        QVERIFY(first && second && interfaces && copied && enlarged); QVERIFY(!window.isWindowModified()); QCOMPARE(window.currentFilePath(), path);
        const auto document = window.sessionDocument(QStringLiteral("test")).document;
        const auto protectedPath = directory.filePath("protected.pdf"); QVERIFY(document->protectedCopy(protectedPath, QStringLiteral("user-password"), QStringLiteral("owner-password"), false, true).error.isEmpty());
        QTimer password; password.setInterval(30); connect(&password, &QTimer::timeout, &window, [] { if (auto* input = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) { input->setTextValue(QStringLiteral("user-password")); input->accept(); } });
        password.start(); window.openDocument(protectedPath); QTRY_COMPARE(window.currentFilePath(), protectedPath); password.stop(); QVERIFY(!reader->isEnabled());
    }
    void exportsLocalizedCopyKeepingOtherPagesSearchable()
    {
        QTemporaryDir directory; const auto path = directory.filePath("source.pdf"); const auto output = directory.filePath("localized.pdf");
        QVERIFY(testdata::writeOutputPdf(path)); const auto hash = testdata::digest(path);
        pdfeditor::MainWindow window; window.show(); window.openDocument(path);
        auto* action = window.findChild<QAction*>(QStringLiteral("localizedRedactAction")); QVERIFY(action); QTRY_VERIFY(action->isEnabled());
        bool configured = false; bool pickerSeen = false;
        QTimer driver; driver.setInterval(30);
        connect(&driver, &QTimer::timeout, &window, [&] {
            if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("redactionDialog")); dialog && dialog->isVisible() && !configured) {
                auto* add = dialog->findChild<QPushButton*>(QStringLiteral("redactionAdd")); if (!add->isEnabled()) { return; }
                dialog->findChild<QDoubleSpinBox*>(QStringLiteral("redactionX"))->setValue(0); dialog->findChild<QDoubleSpinBox*>(QStringLiteral("redactionY"))->setValue(0);
                dialog->findChild<QDoubleSpinBox*>(QStringLiteral("redactionWidth"))->setValue(100); dialog->findChild<QDoubleSpinBox*>(QStringLiteral("redactionHeight"))->setValue(100);
                add->click(); dialog->findChild<QCheckBox*>(QStringLiteral("redactionConsent"))->setChecked(true);
                configured = true; const auto artifacts = qEnvironmentVariable("PDF_EDITOR_TEST_ARTIFACT_DIR");
                if (!artifacts.isEmpty() && QDir().mkpath(artifacts)) { dialog->grab().save(QDir(artifacts).filePath("localized-redaction.png")); }
                dialog->findChild<QPushButton*>(QStringLiteral("redactionApply"))->click(); return;
            }
            if (auto* picker = qobject_cast<QFileDialog*>(QApplication::activeModalWidget()); picker && !pickerSeen) {
                pickerSeen = true; picker->setDirectory(directory.path()); picker->selectFile(output);
                QTimer::singleShot(200, picker, [picker, output] { if (auto* name = picker->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"))) { name->setText(output); } picker->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click(); });
            }
        });
        QTimer timeout; timeout.setSingleShot(true); connect(&timeout, &QTimer::timeout, &window, [] { if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) { modal->reject(); } });
        timeout.start(10000); driver.start(); action->trigger(); driver.stop(); timeout.stop(); QVERIFY(configured && pickerSeen);
        QTRY_VERIFY(QFileInfo::exists(output) && action->isEnabled()); QCOMPARE(window.currentFilePath(), path); QVERIFY(!window.isWindowModified()); QCOMPARE(testdata::digest(path), hash);
        const auto copy = pdfeditor::PdfDocument::open(output).document; QVERIFY(copy); QVERIFY(copy->selectText(0, {0, 0}, {1, 1}).match.text.isEmpty()); QVERIFY(!copy->selectText(1, {0, 0}, {1, 1}).match.text.isEmpty());
    }
};
QTEST_MAIN(RemainingFeaturesWindowTest)
#include "RemainingFeaturesWindowTest.moc"
