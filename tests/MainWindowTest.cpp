#include "ui/MainWindow.h"
#include "ui/DocumentPrint.h"
#include "ui/PageView.h"
#include "ui/ThumbnailView.h"
#include "PdfTestData.h"
#include "VisualPdfData.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QTabWidget>

class MainWindowTest final : public QObject
{
    Q_OBJECT

public:
    static void initMain()
    {
        // Set before QApplication so file pickers remain controllable by QtTest.
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    }

private:

    static void choosePendingAction(pdfeditor::MainWindow& window, QMessageBox::ButtonRole role)
    {
        QTimer::singleShot(0, &window, [&window, role] {
            if (auto* dialog = window.findChild<QMessageBox*>()) {
                for (auto* button : dialog->buttons()) {
                    if (dialog->buttonRole(button) == role) { button->click(); return; }
                }
                dialog->reject();
            }
        });
    }

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

    void splitsDocumentWithoutClearingPendingEdits()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("split-source.pdf");
        const auto target = directory.filePath("parts");
        QVERIFY(testdata::writeReorderPdf(path));
        pdfeditor::MainWindow window;
        window.show();
        auto* split = window.findChild<QAction*>(QStringLiteral("splitPdfAction"));
        auto* rotate = window.findChild<QAction*>(QStringLiteral("rotatePageAction"));
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        QVERIFY(split && rotate && undo);
        QVERIFY(!split->isEnabled());
        window.openDocument(path);
        QTRY_VERIFY(rotate->isEnabled());
        rotate->trigger();
        QTRY_VERIFY(window.isWindowModified() && split->isEnabled());
        QTimer::singleShot(0, &window, [&] {
            if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("splitDialog"))) {
                dialog->findChild<QSpinBox*>(QStringLiteral("splitPagesPerPart"))->setValue(2);
                dialog->findChild<QLineEdit*>(QStringLiteral("splitOutputDirectory"))->setText(target);
                dialog->findChild<QPushButton*>(QStringLiteral("splitApplyButton"))->click();
            }
        });
        split->trigger();
        QTRY_VERIFY(split->isEnabled() && QFileInfo::exists(target));
        QCOMPARE(QDir(target).entryList({QStringLiteral("*.pdf")}, QDir::Files).size(), 2);
        QVERIFY(window.isWindowModified() && undo->isEnabled());
        QCOMPARE(window.currentFilePath(), QFileInfo(path).absoluteFilePath());
        undo->trigger();
        QVERIFY(!window.isWindowModified());
        window.closeDocument();
    }

    void numbersAnIntervalWithUndoRedoAndSave()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("numbering.pdf");
        QVERIFY(testdata::writeReorderPdf(path));
        pdfeditor::MainWindow window;
        window.show();
        auto* action = window.findChild<QAction*>(QStringLiteral("numberPagesAction"));
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        auto* redo = window.findChild<QAction*>(QStringLiteral("redoAction"));
        auto* save = window.findChild<QAction*>(QStringLiteral("saveAction"));
        QVERIFY(action && undo && redo && save);
        QVERIFY(!action->isEnabled());
        window.openDocument(path);
        QTRY_VERIFY(action->isEnabled());
        bool configured = false;
        bool invalidRangeRejected = false;
        QTimer::singleShot(0, &window, [&] {
            if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("pageDecorationDialog"))) {
                dialog->findChild<QSpinBox*>(QStringLiteral("decorationFirstPage"))->setValue(2);
                auto* last = dialog->findChild<QSpinBox*>(QStringLiteral("decorationLastPage"));
                auto* apply = dialog->findChild<QPushButton*>(QStringLiteral("decorationApply"));
                last->setValue(1);
                invalidRangeRejected = !apply->isEnabled();
                last->setValue(3);
                dialog->findChild<QSpinBox*>(QStringLiteral("decorationFirstNumber"))->setValue(7);
                dialog->findChild<QComboBox*>(QStringLiteral("decorationText"))->setEditText(QStringLiteral("Folio {n} / {total}"));
                dialog->findChild<QDoubleSpinBox*>(QStringLiteral("decorationPointSize"))->setValue(8);
                configured = true;
                apply->click();
            }
        });
        action->trigger();
        QVERIFY(configured && invalidRangeRejected);
        QTRY_VERIFY(window.isWindowModified() && action->isEnabled());
        undo->trigger();
        QVERIFY(!window.isWindowModified());
        redo->trigger();
        QVERIFY(window.isWindowModified());
        save->trigger();
        QTRY_VERIFY(!window.isWindowModified() && action->isEnabled());
        const auto reopened = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(reopened);
        QCOMPARE(reopened->findText(QStringLiteral("Folio 7 / 2")).matches.size(), 1);
        QCOMPARE(reopened->findText(QStringLiteral("Folio 8 / 2")).matches.size(), 1);
        window.closeDocument();
    }

    void insertsStyledTextAndWatermarkWithHistory()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("styled-watermark.pdf");
        QVERIFY(testdata::writeVisualPdf(path, 0, 2));
        pdfeditor::MainWindow window;
        window.resize(1100, 850);
        window.show();
        window.openDocument(path);
        auto* insert = window.findChild<QAction*>(QStringLiteral("insertTextAction"));
        auto* watermark = window.findChild<QAction*>(QStringLiteral("watermarkAction"));
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        auto* redo = window.findChild<QAction*>(QStringLiteral("redoAction"));
        auto* save = window.findChild<QAction*>(QStringLiteral("saveAction"));
        auto* view = window.findChild<pdfeditor::PageView*>();
        QVERIFY(insert && watermark && undo && redo && save && view);
        QTRY_VERIFY(insert->isEnabled());
        QTimer::singleShot(0, &window, [&] {
            if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("insertContentDialog"))) {
                dialog->findChild<QPlainTextEdit*>(QStringLiteral("insertText"))->setPlainText(QStringLiteral("Rojo nuevo"));
                dialog->findChild<QDoubleSpinBox*>(QStringLiteral("insertPointSize"))->setValue(12);
                dialog->findChild<QCheckBox*>(QStringLiteral("insertBold"))->setChecked(true);
                dialog->findChild<QCheckBox*>(QStringLiteral("insertItalic"))->setChecked(true);
                dialog->findChild<QCheckBox*>(QStringLiteral("insertUnderline"))->setChecked(true);
                dialog->findChild<QComboBox*>(QStringLiteral("insertAlignment"))->setCurrentIndex(1);
                dialog->findChild<QPushButton*>(QStringLiteral("insertColor"))->setProperty("textColor", QColor(200, 0, 0));
                dialog->findChild<QPushButton*>(QStringLiteral("insertContentApply"))->click();
            }
        });
        insert->trigger();
        QTRY_VERIFY(window.isWindowModified() && watermark->isEnabled());
        QTimer::singleShot(0, &window, [&] {
            if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("pageDecorationDialog"))) {
                dialog->findChild<QComboBox*>(QStringLiteral("decorationText"))->setEditText(QStringLiteral("BORRADOR"));
                dialog->findChild<QDoubleSpinBox*>(QStringLiteral("decorationPointSize"))->setValue(20);
                dialog->findChild<QDoubleSpinBox*>(QStringLiteral("decorationRotation"))->setValue(-25);
                dialog->findChild<QSpinBox*>(QStringLiteral("decorationOpacity"))->setValue(30);
                const auto artifacts = qEnvironmentVariable("PDF_EDITOR_TEST_ARTIFACT_DIR");
                if (!artifacts.isEmpty() && QDir().mkpath(artifacts)) { dialog->grab().save(QDir(artifacts).filePath("watermark-dialog.png")); }
                dialog->findChild<QPushButton*>(QStringLiteral("decorationApply"))->click();
            }
        });
        watermark->trigger();
        QTRY_VERIFY(watermark->isEnabled());
        undo->trigger();
        QVERIFY(window.isWindowModified());
        undo->trigger();
        QVERIFY(!window.isWindowModified());
        redo->trigger();
        redo->trigger();
        save->trigger();
        QTRY_VERIFY(!window.isWindowModified() && watermark->isEnabled());
        const auto reopened = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(reopened);
        QCOMPARE(reopened->findText(QStringLiteral("Rojo nuevo")).matches.size(), 1);
        QCOMPARE(reopened->findText(QStringLiteral("BORRADOR")).matches.size(), 1);
        QCOMPARE(reopened->findText(QStringLiteral("PDF Editor")).matches.size(), 1);
        view->setZoom(1.15);
        QTRY_VERIFY(view->hasPageImage() && !view->isRendering());
        const auto artifacts = qEnvironmentVariable("PDF_EDITOR_TEST_ARTIFACT_DIR");
        if (!artifacts.isEmpty() && QDir().mkpath(artifacts)) { window.grab().save(QDir(artifacts).filePath("styled-watermark-window.png")); }
        window.closeDocument();
    }

    void editsPropertiesWithCancelUndoAndPersistence()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("properties.pdf");
        QVERIFY(testdata::writePdf(path));
        pdfeditor::MainWindow window;
        window.show();
        auto* action = window.findChild<QAction*>(QStringLiteral("documentPropertiesAction"));
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        auto* redo = window.findChild<QAction*>(QStringLiteral("redoAction"));
        auto* save = window.findChild<QAction*>(QStringLiteral("saveAction"));
        QVERIFY(action && undo && redo && save);
        window.openDocument(path);
        QTRY_VERIFY(action->isEnabled());
        QTimer::singleShot(0, &window, [&] {
            if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("documentPropertiesDialog"))) {
                dialog->findChild<QLineEdit*>(QStringLiteral("metadataTitle"))->setText(QStringLiteral("Cancelado"));
                dialog->reject();
            }
        });
        action->trigger();
        QVERIFY(!window.isWindowModified());
        QTimer::singleShot(0, &window, [&] {
            if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("documentPropertiesDialog"))) {
                dialog->findChild<QLineEdit*>(QStringLiteral("metadataTitle"))->setText(QStringLiteral("Informe final"));
                dialog->findChild<QLineEdit*>(QStringLiteral("metadataAuthor"))->setText(QStringLiteral("José Pérez"));
                dialog->findChild<QLineEdit*>(QStringLiteral("metadataSubject"))->setText(QStringLiteral("Revisión"));
                dialog->findChild<QLineEdit*>(QStringLiteral("metadataKeywords"))->setText(QStringLiteral("informe, PDF"));
                dialog->findChild<QPushButton*>(QStringLiteral("metadataApply"))->click();
            }
        });
        action->trigger();
        QTRY_VERIFY(window.isWindowModified() && action->isEnabled());
        undo->trigger();
        QVERIFY(!window.isWindowModified());
        redo->trigger();
        save->trigger();
        QTRY_VERIFY(!window.isWindowModified() && action->isEnabled());
        const auto reopened = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(reopened);
        QCOMPARE(reopened->metadata().title, QStringLiteral("Informe final"));
        QCOMPARE(reopened->metadata().author, QStringLiteral("José Pérez"));
        window.closeDocument();
    }

    void createsNavigatesAndDeletesPageLinks()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("links.pdf");
        QVERIFY(testdata::writeReorderPdf(path));
        pdfeditor::MainWindow window;
        window.show();
        window.openDocument(path);
        auto* add = window.findChild<QAction*>(QStringLiteral("addLinkAction"));
        auto* links = window.findChild<QAction*>(QStringLiteral("pageLinksAction"));
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        auto* redo = window.findChild<QAction*>(QStringLiteral("redoAction"));
        auto* save = window.findChild<QAction*>(QStringLiteral("saveAction"));
        auto* view = window.findChild<pdfeditor::PageView*>();
        QVERIFY(add && links && undo && redo && save && view);
        QTRY_VERIFY(add->isEnabled());
        QTimer::singleShot(0, &window, [&] {
            if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("addLinkDialog"))) {
                dialog->findChild<QSpinBox*>(QStringLiteral("linkTargetPage"))->setValue(3);
                dialog->findChild<QPushButton*>(QStringLiteral("linkApply"))->click();
            }
        });
        add->trigger();
        QTRY_VERIFY(window.isWindowModified() && links->isEnabled());
        QTimer::singleShot(0, &window, [&] {
            if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("pageLinksDialog"))) { dialog->findChild<QPushButton*>(QStringLiteral("openPageLink"))->click(); }
        });
        links->trigger();
        QCOMPARE(view->currentPage(), 2);
        view->setPage(0);
        bool badSchemeRejected = false;
        QTimer::singleShot(0, &window, [&] {
            if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("addLinkDialog"))) {
                dialog->findChild<QComboBox*>(QStringLiteral("linkType"))->setCurrentIndex(1);
                auto* address = dialog->findChild<QLineEdit*>(QStringLiteral("linkUrl"));
                auto* apply = dialog->findChild<QPushButton*>(QStringLiteral("linkApply"));
                address->setText(QStringLiteral("file:///C:/Windows"));
                badSchemeRejected = !apply->isEnabled();
                address->setText(QStringLiteral("https://example.com/informe"));
                apply->click();
            }
        });
        add->trigger();
        QVERIFY(badSchemeRejected);
        QTRY_VERIFY(add->isEnabled());
        QTimer::singleShot(0, &window, [&] {
            if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("pageLinksDialog"))) {
                dialog->findChild<QListWidget*>(QStringLiteral("pageLinksList"))->setCurrentRow(1);
                dialog->findChild<QPushButton*>(QStringLiteral("deletePageLink"))->click();
            }
        });
        links->trigger();
        QTRY_VERIFY(links->isEnabled());
        undo->trigger();
        redo->trigger();
        save->trigger();
        QTRY_VERIFY(!window.isWindowModified() && links->isEnabled());
        const auto reopened = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(reopened);
        QCOMPARE(reopened->pageLinks(0).size(), 1);
        QCOMPARE(reopened->pageLinks(0)[0].targetPage, 2);
        window.closeDocument();
    }

    void createsAndMovesBookmarkBranchesFromThePanel()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("branches.pdf");
        QVERIFY(testdata::writeReorderPdf(path));
        pdfeditor::MainWindow window;
        window.show();
        window.openDocument(path);
        auto* add = window.findChild<QAction*>(QStringLiteral("addBookmarkAction"));
        auto* child = window.findChild<QAction*>(QStringLiteral("addChildBookmarkAction"));
        auto* indent = window.findChild<QAction*>(QStringLiteral("indentBookmarkAction"));
        auto* outdent = window.findChild<QAction*>(QStringLiteral("outdentBookmarkAction"));
        auto* up = window.findChild<QAction*>(QStringLiteral("bookmarkUpAction"));
        auto* down = window.findChild<QAction*>(QStringLiteral("bookmarkDownAction"));
        auto* tree = window.findChild<QTreeWidget*>(QStringLiteral("bookmarkTree"));
        auto* save = window.findChild<QAction*>(QStringLiteral("saveAction"));
        QVERIFY(add && child && indent && outdent && up && down && tree && save);
        QTRY_VERIFY(add->isEnabled());
        const auto nameNext = [&](const QString& name) {
            QTimer::singleShot(0, &window, [&window, name] {
                if (auto* dialog = window.findChild<QInputDialog*>()) { dialog->setTextValue(name); dialog->accept(); }
            });
        };
        nameNext(QStringLiteral("Anexo"));
        add->trigger();
        QTRY_COMPARE(tree->topLevelItemCount(), 2);
        QCOMPARE(tree->currentItem()->text(0), QStringLiteral("Anexo"));
        nameNext(QStringLiteral("Detalle"));
        child->trigger();
        QTRY_COMPARE(tree->topLevelItem(1)->childCount(), 1);
        QCOMPARE(tree->currentItem()->text(0), QStringLiteral("Detalle"));
        outdent->trigger();
        QTRY_COMPARE(tree->topLevelItemCount(), 3);
        QCOMPARE(tree->currentItem()->text(0), QStringLiteral("Detalle"));
        up->trigger();
        QTRY_COMPARE(tree->topLevelItem(1)->text(0), QStringLiteral("Detalle"));
        down->trigger();
        QTRY_COMPARE(tree->topLevelItem(2)->text(0), QStringLiteral("Detalle"));
        indent->trigger();
        QTRY_COMPARE(tree->topLevelItemCount(), 2);
        QCOMPARE(tree->currentItem()->parent()->text(0), QStringLiteral("Anexo"));
        save->trigger();
        QTRY_VERIFY(!window.isWindowModified() && add->isEnabled());
        const auto reopened = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(reopened);
        QCOMPARE(reopened->bookmarks().size(), 3);
        QCOMPARE(reopened->bookmarks()[2].title, QStringLiteral("Detalle"));
        window.closeDocument();
    }

    void insertsNewTextAndAnImageWithHistoryAndSave()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("new-content.pdf");
        const auto imagePath = directory.filePath("image.png");
        QVERIFY(testdata::writeVisualPdf(path));
        QImage image(60, 40, QImage::Format_ARGB32);
        image.fill(Qt::magenta);
        QVERIFY(image.save(imagePath));
        pdfeditor::MainWindow window;
        window.show();
        auto* textAction = window.findChild<QAction*>(QStringLiteral("insertTextAction"));
        auto* imageAction = window.findChild<QAction*>(QStringLiteral("insertImageAction"));
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        auto* save = window.findChild<QAction*>(QStringLiteral("saveAction"));
        QVERIFY(textAction && imageAction && undo && save);
        window.openDocument(path);
        QTRY_VERIFY(textAction->isEnabled());
        QTimer::singleShot(0, &window, [&window] {
            if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("insertContentDialog"))) {
                dialog->findChild<QPlainTextEdit*>(QStringLiteral("insertText"))->setPlainText(QStringLiteral("Nota nueva"));
                dialog->findChild<QPushButton*>(QStringLiteral("insertContentApply"))->click();
            }
        });
        textAction->trigger();
        QTRY_VERIFY(window.isWindowModified() && textAction->isEnabled());
        undo->trigger();
        QVERIFY(!window.isWindowModified());
        window.findChild<QAction*>(QStringLiteral("redoAction"))->trigger();
        save->trigger();
        QTRY_VERIFY(!window.isWindowModified() && textAction->isEnabled());
        QCOMPARE(pdfeditor::PdfDocument::open(path).document->findText(QStringLiteral("Nota nueva")).matches.size(), 1);
        const bool originalNativeSetting = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        const auto restoreNativeSetting = qScopeGuard([originalNativeSetting] { QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, originalNativeSetting); });
        QTimer driver;
        driver.setInterval(30);
        bool pickerConfigured = false;
        bool insertionSeen = false;
        QString timedOutDialog;
        connect(&driver, &QTimer::timeout, &window, [&] {
            if (auto* picker = qobject_cast<QFileDialog*>(QApplication::activeModalWidget()); picker && picker->isVisible()) {
                if (!pickerConfigured) {
                    pickerConfigured = true;
                    picker->setDirectory(directory.path());
                    picker->selectFile(QStringLiteral("image.png"));
                    QTimer::singleShot(300, picker, [picker, imagePath] {
                        if (auto* filename = picker->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"))) { filename->setText(imagePath); }
                        if (auto* buttons = picker->findChild<QDialogButtonBox*>()) {
                            if (auto* open = buttons->button(QDialogButtonBox::Open)) { open->click(); }
                        }
                    });
                }
            } else if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("insertContentDialog"))) {
                insertionSeen = true;
                driver.stop();
                dialog->findChild<QDoubleSpinBox*>(QStringLiteral("insertLeft"))->setValue(55);
                dialog->findChild<QDoubleSpinBox*>(QStringLiteral("insertWidth"))->setValue(35);
                dialog->findChild<QDoubleSpinBox*>(QStringLiteral("insertHeight"))->setValue(25);
                dialog->findChild<QPushButton*>(QStringLiteral("insertContentApply"))->click();
            }
        });
        driver.start();
        QTimer::singleShot(10000, &window, [&] {
            if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                timedOutDialog = dialog->windowTitle();
                if (auto* picker = qobject_cast<QFileDialog*>(dialog)) {
                    timedOutDialog += QStringLiteral(" selected=%1 folder=%2 widgets=%3 flag=%4")
                        .arg(picker->selectedFiles().join(u'|'), picker->directory().absolutePath())
                        .arg(picker->findChildren<QLineEdit*>().size()).arg(QApplication::testAttribute(Qt::AA_DontUseNativeDialogs));
                }
                dialog->reject();
            }
        });
        imageAction->trigger();
        driver.stop();
        QTRY_VERIFY2(window.isWindowModified() && imageAction->isEnabled(),
            qPrintable(QStringLiteral("Picker=%1 insertion=%2 timeout=%3 error=%4").arg(pickerConfigured).arg(insertionSeen)
                .arg(timedOutDialog, window.findChild<QLabel*>(QStringLiteral("documentError"))->text())));
        save->trigger();
        QTRY_VERIFY(!window.isWindowModified() && imageAction->isEnabled());
        QVERIFY(!pdfeditor::PdfDocument::open(path).document->renderPage(0).image.isNull());
        const auto artifacts = qEnvironmentVariable("PDF_EDITOR_TEST_ARTIFACT_DIR");
        if (!artifacts.isEmpty()) {
            auto* view = window.findChild<pdfeditor::PageView*>();
            QVERIFY(view);
            view->setZoom(1.3);
            QTRY_VERIFY(view->hasPageImage() && !view->isRendering());
            QVERIFY(QDir().mkpath(artifacts));
            QVERIFY(window.grab().save(QDir(artifacts).filePath("inserted-content-window.png")));
        }
        window.closeDocument();
    }

    void cropsAndRestoresRangesWithPreviewHistoryAndSave()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("crop-range.pdf");
        QVERIFY(testdata::writeReorderPdf(path));
        const auto original = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(original);
        pdfeditor::MainWindow window;
        window.show();
        auto* crop = window.findChild<QAction*>(QStringLiteral("cropPagesAction"));
        auto* restore = window.findChild<QAction*>(QStringLiteral("resetCropAction"));
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        auto* redo = window.findChild<QAction*>(QStringLiteral("redoAction"));
        auto* save = window.findChild<QAction*>(QStringLiteral("saveAction"));
        auto* view = window.findChild<pdfeditor::PageView*>();
        QVERIFY(crop && restore && undo && redo && save && view);
        QVERIFY(!crop->isEnabled() && !restore->isEnabled());
        window.openDocument(path);
        QTRY_VERIFY(crop->isEnabled());
        view->setPage(1);
        bool configured = false;
        bool previewReady = false;
        bool rejectsEmptyArea = false;
        QTimer edit;
        edit.setInterval(30);
        connect(&edit, &QTimer::timeout, &window, [&] {
            auto* dialog = window.findChild<QDialog*>(QStringLiteral("cropDialog"));
            if (!dialog) { return; }
            auto* apply = dialog->findChild<QPushButton*>(QStringLiteral("cropApplyButton"));
            if (!configured) {
                configured = true;
                dialog->findChild<QSpinBox*>(QStringLiteral("cropFirstPage"))->setValue(1);
                dialog->findChild<QSpinBox*>(QStringLiteral("cropLastPage"))->setValue(2);
                dialog->findChild<QDoubleSpinBox*>(QStringLiteral("cropLeft"))->setValue(12.3);
                dialog->findChild<QDoubleSpinBox*>(QStringLiteral("cropTop"))->setValue(10);
                dialog->findChild<QDoubleSpinBox*>(QStringLiteral("cropRight"))->setValue(7.7);
                dialog->findChild<QDoubleSpinBox*>(QStringLiteral("cropBottom"))->setValue(5);
            }
            if (!apply->isEnabled()) { return; }
            previewReady = dialog->findChild<QWidget*>(QStringLiteral("cropPreview")) != nullptr;
            auto* left = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("cropLeft"));
            left->setValue(99);
            rejectsEmptyArea = !apply->isEnabled();
            left->setValue(12.3);
            const auto artifacts = qEnvironmentVariable("PDF_EDITOR_TEST_ARTIFACT_DIR");
            if (!artifacts.isEmpty() && QDir().mkpath(artifacts)) {
                dialog->grab().save(QDir(artifacts).filePath("crop-dialog.png"));
            }
            edit.stop();
            apply->click();
        });
        QTimer::singleShot(10000, &window, [&window] {
            if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("cropDialog"))) { dialog->reject(); }
        });
        edit.start();
        crop->trigger();
        edit.stop();
        QVERIFY(configured && previewReady && rejectsEmptyArea);
        QTRY_VERIFY(window.isWindowModified() && crop->isEnabled());
        QCOMPARE(view->currentPage(), 1);
        QCOMPARE(window.pageCount(), 3);
        undo->trigger();
        QVERIFY(!window.isWindowModified());
        redo->trigger();
        QVERIFY(window.isWindowModified());
        save->trigger();
        QTRY_VERIFY(!window.isWindowModified() && crop->isEnabled());
        const auto cropped = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(cropped);
        for (qsizetype page : {0, 1}) {
            QVERIFY(qAbs(cropped->pageSize(page).width() - original->pageSize(page).width() * 0.8) < 0.00001);
        }
        QCOMPARE(cropped->pageSize(2), original->pageSize(2));
        QTimer::singleShot(0, &window, [&window] {
            if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("cropDialog"))) { dialog->reject(); }
        });
        crop->trigger();
        QVERIFY(!window.isWindowModified());
        QTimer::singleShot(0, &window, [&window] {
            if (auto* dialog = window.findChild<QDialog*>(QStringLiteral("cropDialog"))) {
                dialog->findChild<QSpinBox*>(QStringLiteral("cropFirstPage"))->setValue(1);
                dialog->findChild<QSpinBox*>(QStringLiteral("cropLastPage"))->setValue(3);
                dialog->findChild<QPushButton*>(QStringLiteral("cropApplyButton"))->click();
            }
        });
        restore->trigger();
        QTRY_VERIFY(window.isWindowModified() && restore->isEnabled());
        save->trigger();
        QTRY_VERIFY(!window.isWindowModified() && restore->isEnabled());
        const auto restored = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(restored);
        QVERIFY(restored->pageSize(0).width() > cropped->pageSize(0).width());
        QCOMPARE(restored->bookmarks()[0].page, 1);
        window.closeDocument();
        QVERIFY(!crop->isEnabled() && !restore->isEnabled());
    }

    void editsBookmarksWithHistoryAndSave()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("bookmark-editing.pdf");
        QVERIFY(testdata::writeReorderPdf(path));
        pdfeditor::MainWindow window;
        window.show();
        auto* add = window.findChild<QAction*>(QStringLiteral("addBookmarkAction"));
        auto* rename = window.findChild<QAction*>(QStringLiteral("renameBookmarkAction"));
        auto* retarget = window.findChild<QAction*>(QStringLiteral("retargetBookmarkAction"));
        auto* remove = window.findChild<QAction*>(QStringLiteral("deleteBookmarkAction"));
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        auto* redo = window.findChild<QAction*>(QStringLiteral("redoAction"));
        auto* save = window.findChild<QAction*>(QStringLiteral("saveAction"));
        auto* tree = window.findChild<QTreeWidget*>(QStringLiteral("bookmarkTree"));
        auto* view = window.findChild<pdfeditor::PageView*>();
        QVERIFY(add && rename && retarget && remove && undo && redo && save && tree && view);
        QVERIFY(!add->isEnabled() && !rename->isEnabled());
        window.openDocument(path);
        QTRY_VERIFY(add->isEnabled());
        view->setPage(2);
        const auto enterTitle = [&](const QString& text) {
            QTimer::singleShot(0, &window, [&window, text] {
                if (auto* dialog = window.findChild<QInputDialog*>()) { dialog->setTextValue(text); dialog->accept(); }
            });
        };
        enterTitle(QStringLiteral("Conclusiones"));
        add->trigger();
        QTRY_COMPARE(tree->topLevelItemCount(), 2);
        QVERIFY(window.isWindowModified());
        tree->setCurrentItem(tree->topLevelItem(1));
        QVERIFY(rename->isEnabled() && retarget->isEnabled() && remove->isEnabled());
        enterTitle(QStringLiteral("Resultado final"));
        rename->trigger();
        QTRY_COMPARE(tree->topLevelItem(1)->text(0), QStringLiteral("Resultado final"));
        QTimer::singleShot(0, &window, [&window] {
            if (auto* dialog = window.findChild<QInputDialog*>()) { dialog->setIntValue(1); dialog->accept(); }
        });
        retarget->trigger();
        QTRY_COMPARE(tree->topLevelItem(1)->data(0, Qt::UserRole).toLongLong(), 0);
        tree->setFocus();
        QTest::keyClick(tree, Qt::Key_Return);
        QCOMPARE(view->currentPage(), 0);
        const auto artifacts = qEnvironmentVariable("PDF_EDITOR_TEST_ARTIFACT_DIR");
        if (!artifacts.isEmpty()) {
            QTRY_VERIFY(view->hasPageImage() && !view->isRendering());
            QVERIFY(QDir().mkpath(artifacts));
            QVERIFY(window.grab().save(QDir(artifacts).filePath("bookmark-editing-window.png")));
        }
        QTimer::singleShot(0, &window, [&window] {
            if (auto* dialog = window.findChild<QMessageBox*>()) { dialog->button(QMessageBox::No)->click(); }
        });
        remove->trigger();
        QCOMPARE(tree->topLevelItemCount(), 2);
        QTimer::singleShot(0, &window, [&window] {
            if (auto* dialog = window.findChild<QMessageBox*>()) { dialog->button(QMessageBox::Yes)->click(); }
        });
        remove->trigger();
        QTRY_COMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(window.pageCount(), 3);
        undo->trigger();
        QCOMPARE(tree->topLevelItemCount(), 2);
        redo->trigger();
        QCOMPARE(tree->topLevelItemCount(), 1);
        undo->trigger();
        QCOMPARE(tree->topLevelItemCount(), 2);
        save->trigger();
        QTRY_VERIFY(!window.isWindowModified() && add->isEnabled());
        const auto reopened = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(reopened);
        QCOMPARE(reopened->bookmarks()[1].title, QStringLiteral("Resultado final"));
        QCOMPARE(reopened->bookmarks()[1].page, 0);
        window.closeDocument();
        QVERIFY(!add->isEnabled() && !remove->isEnabled());
    }

    void copiesAndMarksSelectedTextAndNavigatesBookmarks()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("select.pdf");
        QVERIFY(testdata::writeReorderPdf(path));
        pdfeditor::MainWindow window;
        window.show();
        window.openDocument(path);
        auto* view = window.findChild<pdfeditor::PageView*>();
        auto* tree = window.findChild<QTreeWidget*>(QStringLiteral("bookmarkTree"));
        auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("navigationTabs"));
        auto* copy = window.findChild<QAction*>(QStringLiteral("copyTextAction"));
        auto* mark = window.findChild<QAction*>(QStringLiteral("markSelection0Action"));
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        QVERIFY(view && tree && tabs && copy && mark && undo);
        QTRY_COMPARE(window.pageCount(), 3);
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Page B"));
        tabs->setCurrentWidget(tree);
        tree->setCurrentItem(tree->topLevelItem(0));
        QCOMPARE(view->currentPage(), 1);
        QTRY_VERIFY(view->hasPageImage() && !view->isRendering());
        const auto document = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(document);
        const auto matches = document->findText(QStringLiteral("B")).matches;
        QVERIFY(!matches.isEmpty());
        const auto bounds = matches.front().highlight.boundingRect();
        auto* image = view->findChild<QWidget*>(QStringLiteral("pageImage"));
        QVERIFY(image);
        const auto pixel = [image](QPointF point) {
            return QPoint(qRound(point.x() * image->width()), qRound(point.y() * image->height()));
        };
        QTest::mousePress(image, Qt::LeftButton, Qt::NoModifier, pixel(bounds.topLeft()));
        QTest::mouseRelease(image, Qt::LeftButton, Qt::NoModifier, pixel(bounds.bottomRight()));
        QTRY_VERIFY(copy->isEnabled() && mark->isEnabled());
        const auto artifacts = qEnvironmentVariable("PDF_EDITOR_TEST_ARTIFACT_DIR");
        if (!artifacts.isEmpty()) {
            QVERIFY(QDir().mkpath(artifacts));
            QVERIFY(window.grab().save(QDir(artifacts).filePath("text-selection-window.png")));
        }
        QTest::keyClick(view, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("B"));
        mark->trigger();
        QTRY_VERIFY(window.isWindowModified() && undo->isEnabled());
        QVERIFY(!copy->isEnabled());
        undo->trigger();
        QVERIFY(!window.isWindowModified());
        view->setPage(2);
        tree->setFocus();
        QTest::keyClick(tree, Qt::Key_Return);
        QCOMPARE(view->currentPage(), 1);
        window.closeDocument();
        QVERIFY(!copy->isEnabled() && !mark->isEnabled());
        QVERIFY(view->textSelection().text.isEmpty());
    }

    void marksSearchTextAndDeletesTheMarkFromTheMenu_data()
    {
        QTest::addColumn<QString>("actionName");
        QTest::addColumn<int>("kind");
        QTest::newRow("underline") << QStringLiteral("underlineMatchAction") << static_cast<int>(pdfeditor::PdfTextMarkup::Underline);
        QTest::newRow("strikeout") << QStringLiteral("strikeoutMatchAction") << static_cast<int>(pdfeditor::PdfTextMarkup::StrikeOut);
        QTest::newRow("squiggly") << QStringLiteral("squigglyMatchAction") << static_cast<int>(pdfeditor::PdfTextMarkup::Squiggly);
    }

    void marksSearchTextAndDeletesTheMarkFromTheMenu()
    {
        QFETCH(QString, actionName);
        QFETCH(int, kind);
        QTemporaryDir directory;
        const auto path = directory.filePath("markup.pdf");
        QVERIFY(testdata::writeVisualPdf(path, 90));
        const auto original = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(original);
        pdfeditor::MainWindow window;
        window.show();
        auto* find = window.findChild<QAction*>(QStringLiteral("searchAction"));
        auto* mark = window.findChild<QAction*>(actionName);
        auto* manage = window.findChild<QAction*>(QStringLiteral("pageTextMarksAction"));
        auto* query = window.findChild<QLineEdit*>(QStringLiteral("searchQuery"));
        auto* save = window.findChild<QAction*>(QStringLiteral("saveAction"));
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        auto* redo = window.findChild<QAction*>(QStringLiteral("redoAction"));
        QVERIFY(find && mark && manage && query && save && undo && redo);
        QVERIFY(!mark->isEnabled() && !manage->isEnabled());
        window.openDocument(path);
        QTRY_VERIFY(find->isEnabled());
        find->trigger();
        query->setText(QStringLiteral("Editor"));
        QTest::keyClick(query, Qt::Key_Return);
        QTRY_VERIFY(mark->isEnabled());
        mark->trigger();
        QTRY_VERIFY(save->isEnabled());
        QVERIFY(!mark->isEnabled());
        undo->trigger();
        QVERIFY(!window.isWindowModified());
        redo->trigger();
        save->trigger();
        QTRY_VERIFY(!window.isWindowModified());
        const auto marked = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(marked);
        QCOMPARE(marked->pageTextMarks(0).size(), 1);
        QCOMPARE(marked->pageTextMarks(0)[0].type, static_cast<pdfeditor::PdfTextMarkup>(kind));
        QTimer::singleShot(0, &window, [&window] {
            auto* dialog = window.findChild<QDialog*>(QStringLiteral("pageTextMarksDialog"));
            if (!dialog) { return; }
            auto* list = dialog->findChild<QListWidget*>(QStringLiteral("pageTextMarkList"));
            auto* remove = dialog->findChild<QPushButton*>(QStringLiteral("deleteTextMarkButton"));
            if (!list || !remove || list->count() != 1) { dialog->reject(); return; }
            list->setCurrentRow(0);
            QTimer::singleShot(0, &window, [] {
                if (auto* confirmation = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                    confirmation->button(QMessageBox::Yes)->click();
                }
            });
            remove->click();
        });
        manage->trigger();
        QTRY_VERIFY(save->isEnabled());
        undo->trigger();
        QVERIFY(!window.isWindowModified());
        redo->trigger();
        QVERIFY(window.isWindowModified());
        save->trigger();
        QTRY_VERIFY(!window.isWindowModified());
        const auto reopened = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(reopened && reopened->pageTextMarks(0).isEmpty());
        QCOMPARE(reopened->renderPage(0).image, original->renderPage(0).image);
    }

    void insertsAPdfIntervalFromTheMenu_data()
    {
        QTest::addColumn<bool>("accept");
        QTest::newRow("insert") << true;
        QTest::newRow("cancel") << false;
    }

    void insertsAPdfIntervalFromTheMenu()
    {
        QFETCH(bool, accept);
        QTemporaryDir directory;
        const auto path = directory.filePath("target.pdf");
        const auto sourcePath = directory.filePath("source.pdf");
        QVERIFY(testdata::writeOutputPdf(path));
        QVERIFY(testdata::writeReorderPdf(sourcePath));
        const auto digest = testdata::digest(path);
        const auto sourceDigest = testdata::digest(sourcePath);
        const auto source = pdfeditor::PdfDocument::open(sourcePath).document;
        QVERIFY(source);
        pdfeditor::MainWindow window;
        window.show();
        auto* insert = window.findChild<QAction*>(QStringLiteral("insertPdfAction"));
        auto* rotate = window.findChild<QAction*>(QStringLiteral("rotatePageAction"));
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        auto* redo = window.findChild<QAction*>(QStringLiteral("redoAction"));
        auto* save = window.findChild<QAction*>(QStringLiteral("saveAction"));
        auto* view = window.findChild<pdfeditor::PageView*>();
        QVERIFY(insert && rotate && undo && redo && save && view);
        QVERIFY(!insert->isEnabled());
        window.openDocument(path);
        QTRY_VERIFY(insert->isEnabled());
        rotate->trigger();
        QTRY_VERIFY(save->isEnabled());

        const bool nonNative = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        const auto restoreDialogs = qScopeGuard([nonNative] {
            QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nonNative);
        });
        QTimer configure;
        bool visited = false;
        connect(&configure, &QTimer::timeout, &window, [&] {
            auto* dialog = window.findChild<QDialog*>(QStringLiteral("pageImportDialog"));
            if (!dialog || !dialog->isVisible()) { return; }
            configure.stop();
            visited = true;
            if (!accept) { dialog->reject(); return; }
            auto* first = dialog->findChild<QSpinBox*>(QStringLiteral("importFirstPage"));
            auto* last = dialog->findChild<QSpinBox*>(QStringLiteral("importLastPage"));
            auto* position = dialog->findChild<QComboBox*>(QStringLiteral("importPosition"));
            if (!first || !last || !position) { dialog->reject(); return; }
            first->setValue(2);
            last->setValue(3);
            position->setCurrentIndex(2);
            dialog->accept();
        });
        configure.start(10);
        QTimer::singleShot(0, &window, [&] {
            if (auto* dialog = window.findChild<QFileDialog*>()) {
                dialog->selectFile(sourcePath);
                static_cast<QDialog*>(dialog)->accept();
            }
        });
        insert->trigger();
        QTRY_VERIFY(visited && insert->isEnabled());
        QCOMPARE(window.currentFilePath(), path);
        QCOMPARE(testdata::digest(path), digest);
        QCOMPARE(testdata::digest(sourcePath), sourceDigest);
        QVERIFY(window.isWindowModified());
        if (!accept) {
            QCOMPARE(window.pageCount(), qsizetype{3});
            undo->trigger();
            QVERIFY(!window.isWindowModified());
            return;
        }
        QCOMPARE(window.pageCount(), qsizetype{5});
        QCOMPARE(view->currentPage(), qsizetype{3});
        undo->trigger();
        QCOMPARE(window.pageCount(), qsizetype{3});
        QVERIFY(window.isWindowModified());
        redo->trigger();
        QCOMPARE(window.pageCount(), qsizetype{5});
        save->trigger();
        QTRY_VERIFY(!window.isWindowModified());
        const auto reopened = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(reopened);
        QCOMPARE(reopened->renderPage(3).image, source->renderPage(1).image);
        QCOMPARE(reopened->renderPage(4).image, source->renderPage(2).image);
    }

    void mergesFilesInTheChosenOrderAndKeepsEditsOnFailure_data()
    {
        QTest::addColumn<bool>("invalid");
        QTest::newRow("ordered-merge") << false;
        QTest::newRow("failed-source") << true;
    }

    void mergesFilesInTheChosenOrderAndKeepsEditsOnFailure()
    {
        QFETCH(bool, invalid);
        QTemporaryDir directory;
        const auto path = directory.filePath("target.pdf");
        const auto firstPath = directory.filePath("first.pdf");
        const auto secondPath = directory.filePath("second.pdf");
        QVERIFY(testdata::writeOutputPdf(path));
        QVERIFY(testdata::writeVisualPdf(firstPath, 180));
        QVERIFY(testdata::writeOutputPdf(secondPath));
        const auto first = pdfeditor::PdfDocument::open(firstPath).document;
        const auto second = pdfeditor::PdfDocument::open(secondPath).document;
        QVERIFY(first && second);
        if (invalid) {
            QFile file(secondPath);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QVERIFY(file.write("invalid PDF") > 0);
        }
        const auto digest = testdata::digest(path);
        pdfeditor::MainWindow window;
        window.show();
        auto* merge = window.findChild<QAction*>(QStringLiteral("mergePdfsAction"));
        auto* rotate = window.findChild<QAction*>(QStringLiteral("rotatePageAction"));
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        auto* redo = window.findChild<QAction*>(QStringLiteral("redoAction"));
        auto* save = window.findChild<QAction*>(QStringLiteral("saveAction"));
        auto* error = window.findChild<QLabel*>(QStringLiteral("documentError"));
        auto* view = window.findChild<pdfeditor::PageView*>();
        QVERIFY(merge && rotate && undo && redo && save && error && view);
        window.openDocument(path);
        QTRY_VERIFY(merge->isEnabled());
        rotate->trigger();
        QTRY_VERIFY(save->isEnabled());
        const bool nonNative = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        const auto restoreDialogs = qScopeGuard([nonNative] {
            QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nonNative);
        });
        QTimer order;
        bool visited = false;
        connect(&order, &QTimer::timeout, &window, [&] {
            auto* dialog = window.findChild<QDialog*>(QStringLiteral("mergePdfsDialog"));
            if (!dialog || !dialog->isVisible()) { return; }
            order.stop();
            auto* files = dialog->findChild<QListWidget*>(QStringLiteral("mergeFileList"));
            auto* up = dialog->findChild<QPushButton*>(QStringLiteral("mergeFileUp"));
            if (!files || !up || files->count() != 2) { dialog->reject(); return; }
            for (int index = 0; index < files->count(); ++index) {
                if (files->item(index)->data(Qt::UserRole).toString() == secondPath) {
                    files->setCurrentRow(index);
                    if (index > 0) { up->click(); }
                    break;
                }
            }
            visited = files->item(0)->data(Qt::UserRole).toString() == secondPath;
            dialog->accept();
        });
        order.start(10);
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = window.findChild<QFileDialog*>();
            if (!dialog) { return; }
            if (auto* edit = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"))) {
                edit->setText(QStringLiteral("\"%1\" \"%2\"").arg(firstPath, secondPath));
                static_cast<QDialog*>(dialog)->accept();
            } else { dialog->reject(); }
        });
        merge->trigger();
        QTRY_VERIFY(visited && merge->isEnabled());
        QCOMPARE(window.currentFilePath(), path);
        QCOMPARE(testdata::digest(path), digest);
        QVERIFY(window.isWindowModified());
        if (invalid) {
            QCOMPARE(window.pageCount(), qsizetype{3});
            QVERIFY(error->isVisible() && error->text().contains(QStringLiteral("second.pdf")));
            undo->trigger();
            QVERIFY(!window.isWindowModified());
            return;
        }
        QCOMPARE(window.pageCount(), qsizetype{7});
        QCOMPARE(view->currentPage(), qsizetype{3});
        undo->trigger();
        QCOMPARE(window.pageCount(), qsizetype{3});
        QVERIFY(window.isWindowModified());
        redo->trigger();
        QCOMPARE(window.pageCount(), qsizetype{7});
        save->trigger();
        QTRY_VERIFY(!window.isWindowModified());
        const auto reopened = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(reopened);
        for (qsizetype index = 0; index < 3; ++index) {
            QCOMPARE(reopened->renderPage(index + 3).image, second->renderPage(index).image);
        }
        QCOMPARE(reopened->renderPage(6).image, first->renderPage(0).image);
    }

    void organizesPagesWithUndoAndKeepsTheSelection()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("pages.pdf");
        const auto single = directory.filePath("single.pdf");
        QVERIFY(testdata::writeOutputPdf(path));
        QVERIFY(testdata::writeVisualPdf(single));
        const auto digest = testdata::digest(path);
        const auto original = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(original);
        pdfeditor::MainWindow window;
        window.show();
        auto* duplicate = window.findChild<QAction*>(QStringLiteral("duplicatePageAction"));
        auto* insert = window.findChild<QAction*>(QStringLiteral("insertBlankPageAction"));
        auto* remove = window.findChild<QAction*>(QStringLiteral("deletePageAction"));
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        auto* redo = window.findChild<QAction*>(QStringLiteral("redoAction"));
        auto* save = window.findChild<QAction*>(QStringLiteral("saveAction"));
        auto* view = window.findChild<pdfeditor::PageView*>();
        auto* thumbs = window.findChild<pdfeditor::ThumbnailView*>();
        auto* number = window.findChild<QSpinBox*>(QStringLiteral("pageNumber"));
        QVERIFY(duplicate && insert && remove && undo && redo && save && view && thumbs && number);
        QVERIFY(!duplicate->isEnabled() && !insert->isEnabled() && !remove->isEnabled());
        window.openDocument(path);
        QTRY_VERIFY(duplicate->isEnabled());
        duplicate->trigger();
        QTRY_COMPARE(window.pageCount(), qsizetype{4});
        QCOMPARE(view->currentPage(), qsizetype{1});
        QCOMPARE(thumbs->currentIndex().row(), 1);
        QCOMPARE(number->value(), 2);
        insert->trigger();
        QTRY_COMPARE(window.pageCount(), qsizetype{5});
        QCOMPARE(view->currentPage(), qsizetype{2});
        QCOMPARE(thumbs->model()->rowCount(), 5);
        QTRY_VERIFY(view->hasPageImage() && !view->isRendering());
        undo->trigger();
        QCOMPARE(window.pageCount(), qsizetype{4});
        QCOMPARE(view->currentPage(), qsizetype{1});
        const auto confirmDeletion = [&window](QMessageBox::StandardButton answer) {
            QTimer::singleShot(0, &window, [answer] {
                if (auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                    dialog->button(answer)->click();
                }
            });
        };
        confirmDeletion(QMessageBox::No);
        remove->trigger();
        QCOMPARE(window.pageCount(), qsizetype{4});
        confirmDeletion(QMessageBox::Yes);
        remove->trigger();
        QTRY_COMPARE(window.pageCount(), qsizetype{3});
        QCOMPARE(view->currentPage(), qsizetype{1});
        QCOMPARE(testdata::digest(path), digest);
        undo->trigger();
        QCOMPARE(window.pageCount(), qsizetype{4});
        redo->trigger();
        QCOMPARE(window.pageCount(), qsizetype{3});
        save->trigger();
        QTRY_VERIFY(!window.isWindowModified());
        const auto reopened = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(reopened);
        for (qsizetype index = 0; index < original->pageCount(); ++index) {
            QCOMPARE(reopened->renderPage(index).image, original->renderPage(index).image);
        }
        window.openDocument(single);
        QTRY_COMPARE(window.pageCount(), qsizetype{1});
        QVERIFY(!remove->isEnabled() && insert->isEnabled() && duplicate->isEnabled());
    }

    void highlightsASearchMatchAndKeepsItAfterSaving()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("highlight.pdf");
        QVERIFY(testdata::writeVisualPdf(path, 90));
        const auto original = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(original);
        pdfeditor::MainWindow window;
        window.show();
        auto* find = window.findChild<QAction*>(QStringLiteral("searchAction"));
        auto* highlight = window.findChild<QAction*>(QStringLiteral("highlightMatchAction"));
        auto* query = window.findChild<QLineEdit*>(QStringLiteral("searchQuery"));
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        auto* redo = window.findChild<QAction*>(QStringLiteral("redoAction"));
        auto* save = window.findChild<QAction*>(QStringLiteral("saveAction"));
        QVERIFY(find && highlight && query && undo && redo && save);
        window.openDocument(path);
        QTRY_VERIFY(find->isEnabled());
        find->trigger();
        query->setText(QStringLiteral("Editor"));
        QTest::keyClick(query, Qt::Key_Return);
        QTRY_VERIFY(highlight->isEnabled());
        highlight->trigger();
        QTRY_VERIFY(save->isEnabled());
        QVERIFY(!highlight->isEnabled());
        undo->trigger();
        QVERIFY(!window.isWindowModified());
        redo->trigger();
        QVERIFY(window.isWindowModified());
        save->trigger();
        QTRY_VERIFY(!window.isWindowModified());
        const auto reopened = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(reopened);
        QVERIFY(reopened->renderPage(0).image != original->renderPage(0).image);
    }

    void fillsTextAndCheckboxFieldsFromTheMenu()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("forms.pdf");
        QVERIFY(testdata::writeVisualPdf(path, 0, 1, testdata::VisualPdfExtras::AnnotationsAndForm));
        const auto digest = testdata::digest(path);
        pdfeditor::MainWindow window;
        window.show();
        auto* fill = window.findChild<QAction*>(QStringLiteral("fillFormAction"));
        auto* save = window.findChild<QAction*>(QStringLiteral("saveAction"));
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        auto* redo = window.findChild<QAction*>(QStringLiteral("redoAction"));
        QVERIFY(fill && save && undo && redo);
        QVERIFY(!fill->isEnabled());
        window.openDocument(path);
        QTRY_VERIFY(fill->isEnabled());
        const auto enterValue = [&window](int index, QVariant value) {
            QTimer::singleShot(0, &window, [&window, index, value] {
                auto* chooser = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
                if (!chooser) { return; }
                if (auto* combo = chooser->findChild<QComboBox*>()) { combo->setCurrentIndex(index); }
                QTimer::singleShot(0, &window, [value] {
                    auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                    if (!dialog) { return; }
                    if (auto* text = dialog->findChild<QLineEdit*>(QStringLiteral("formText"))) {
                        text->setText(value.toString());
                    }
                    if (auto* checkbox = dialog->findChild<QCheckBox*>(QStringLiteral("formCheckbox"))) {
                        checkbox->setChecked(value.toBool());
                    }
                    dialog->accept();
                });
                chooser->accept();
            });
        };
        enterValue(0, QStringLiteral("María"));
        fill->trigger();
        QTRY_VERIFY(save->isEnabled());
        undo->trigger();
        QVERIFY(!window.isWindowModified());
        redo->trigger();
        QVERIFY(window.isWindowModified());
        enterValue(1, false);
        fill->trigger();
        QTRY_VERIFY(fill->isEnabled() && save->isEnabled());
        QCOMPARE(testdata::digest(path), digest);
        save->trigger();
        QTRY_VERIFY(!window.isWindowModified());
        const auto reopened = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(reopened);
        const auto fields = reopened->pageFormFields(0);
        QCOMPARE(fields.size(), 2);
        QCOMPARE(fields[0].value.toString(), QStringLiteral("María"));
        QVERIFY(!fields[1].value.toBool());
    }

    void deletesANoteAndRestoresItWithUndo()
    {
        QTemporaryDir directory;
        const auto original = directory.filePath("original.pdf");
        const auto path = directory.filePath("note.pdf");
        QVERIFY(testdata::writeVisualPdf(original));
        const auto source = pdfeditor::PdfDocument::open(original).document;
        QVERIFY(source);
        const auto added = source->addNote(0, {0.5, 0.5}, QStringLiteral("Eliminar"));
        QVERIFY(added.document);
        QVERIFY(added.document->save(pdfeditor::PdfSaveMode::NewCopy, path).document);
        const auto digest = testdata::digest(path);
        pdfeditor::MainWindow window;
        window.show();
        auto* notes = window.findChild<QAction*>(QStringLiteral("pageNotesAction"));
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        auto* redo = window.findChild<QAction*>(QStringLiteral("redoAction"));
        auto* save = window.findChild<QAction*>(QStringLiteral("saveAction"));
        QVERIFY(notes && undo && redo && save);
        window.openDocument(path);
        QTRY_VERIFY(notes->isEnabled());
        QTimer::singleShot(0, &window, [&window] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) { return; }
            auto* remove = dialog->findChild<QPushButton*>(QStringLiteral("deleteNoteButton"));
            if (!remove) { dialog->reject(); return; }
            QTimer::singleShot(0, &window, [] {
                if (auto* confirmation = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                    confirmation->button(QMessageBox::Yes)->click();
                }
            });
            remove->click();
        });
        notes->trigger();
        QTRY_VERIFY(save->isEnabled());
        QCOMPARE(testdata::digest(path), digest);
        undo->trigger();
        QVERIFY(!window.isWindowModified());
        redo->trigger();
        QVERIFY(window.isWindowModified());
        save->trigger();
        QTRY_VERIFY(!window.isWindowModified());
        const auto reopened = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(reopened && reopened->pageNotes(0).isEmpty());
        QCOMPARE(reopened->renderPage(0).image, source->renderPage(0).image);
    }

    void searchesAndNavigatesMatchesInTheViewer()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("search.pdf");
        QVERIFY(testdata::writeOutputPdf(path));
        pdfeditor::MainWindow window;
        window.show();
        auto* find = window.findChild<QAction*>(QStringLiteral("searchAction"));
        auto* query = window.findChild<QLineEdit*>(QStringLiteral("searchQuery"));
        auto* status = window.findChild<QLabel*>(QStringLiteral("searchStatus"));
        auto* bar = window.findChild<QToolBar*>(QStringLiteral("searchBar"));
        auto* view = window.findChild<pdfeditor::PageView*>();
        QVERIFY(find && query && status && bar && view);
        QVERIFY(!find->isEnabled() && bar->isHidden());
        window.openDocument(path);
        QTRY_VERIFY(find->isEnabled());

        find->trigger();
        QVERIFY(bar->isVisible());
        query->setText(QStringLiteral("informe"));
        QTest::keyClick(query, Qt::Key_Return);
        QTRY_COMPARE(status->text(), QStringLiteral("1 de 2"));
        QCOMPARE(view->currentPage(), qsizetype{0});
        QTest::keyClick(query, Qt::Key_F3);
        QTRY_COMPARE(view->currentPage(), qsizetype{2});
        QCOMPARE(status->text(), QStringLiteral("2 de 2"));
        QTest::keyClick(query, Qt::Key_F3, Qt::ShiftModifier);
        QTRY_COMPARE(view->currentPage(), qsizetype{0});

        query->setText(QStringLiteral("informe"));
        QTest::keyClick(query, Qt::Key_Return);
        query->setText(QStringLiteral("resumen"));
        QTest::keyClick(query, Qt::Key_Return);
        QTRY_COMPARE(status->text(), QStringLiteral("1 de 1"));
        QCOMPARE(view->currentPage(), qsizetype{1});
        QTest::keyClick(query, Qt::Key_Escape);
        QVERIFY(bar->isHidden());
        QVERIFY(!window.isWindowModified());
    }

    void undoesRedoesAndExportsPendingPageChanges()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("source.pdf");
        const auto output = directory.filePath("page.png");
        QVERIFY(testdata::writeOutputPdf(path));
        const auto digest = testdata::digest(path);
        pdfeditor::MainWindow window;
        window.show();
        auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
        auto* redo = window.findChild<QAction*>(QStringLiteral("redoAction"));
        auto* move = window.findChild<QAction*>(QStringLiteral("movePageDownAction"));
        auto* rotate = window.findChild<QAction*>(QStringLiteral("rotatePageAction"));
        auto* exportPage = window.findChild<QAction*>(QStringLiteral("exportPageAction"));
        auto* save = window.findChild<QAction*>(QStringLiteral("saveAction"));
        auto* view = window.findChild<pdfeditor::PageView*>();
        QVERIFY(undo && redo && move && rotate && exportPage && save && view);
        window.openDocument(path);
        QTRY_VERIFY(move->isEnabled());
        QVERIFY(!undo->isEnabled() && !redo->isEnabled());

        move->trigger();
        QTRY_COMPARE(view->currentPage(), qsizetype{1});
        rotate->trigger();
        QTRY_VERIFY(undo->isEnabled());
        undo->trigger();
        QVERIFY(window.isWindowModified() && redo->isEnabled());
        undo->trigger();
        QCOMPARE(view->currentPage(), qsizetype{0});
        QVERIFY(!window.isWindowModified() && !save->isEnabled());
        redo->trigger();
        QCOMPARE(view->currentPage(), qsizetype{1});
        redo->trigger();
        QVERIFY(window.isWindowModified() && !redo->isEnabled());

        const bool nonNative = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        const auto restoreDialogs = qScopeGuard([nonNative] {
            QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nonNative);
        });
        QTimer::singleShot(0, &window, [&window, output] {
            if (auto* dialog = window.findChild<QFileDialog*>()) {
                dialog->selectFile(output);
                static_cast<QDialog*>(dialog)->accept();
                if (dialog->isVisible()) { dialog->reject(); }
            }
        });
        exportPage->trigger();
        QTRY_VERIFY(exportPage->isEnabled() && QFileInfo::exists(output));
        const QImage image(output);
        QCOMPARE(image.size(), QSize(450, 300));
        QCOMPARE(image.pixelColor(225, 150), QColor(Qt::red));
        QCOMPARE(testdata::digest(path), digest);
        QVERIFY(window.isWindowModified() && undo->isEnabled());

        undo->trigger();
        QVERIFY(redo->isEnabled());
        rotate->trigger();
        QTRY_VERIFY(save->isEnabled());
        QVERIFY(!redo->isEnabled());
        save->trigger();
        QTRY_VERIFY(!window.isWindowModified());
        QVERIFY(!undo->isEnabled() && !redo->isEnabled());
    }

    void printsTheRequestedPagesToAPdfDevice()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("source.pdf");
        const auto output = directory.filePath("printed.pdf");
        QVERIFY(testdata::writeOutputPdf(path));
        const auto digest = testdata::digest(path);
        const auto document = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(document);
        {
            QPdfWriter printer(output);
            printer.setResolution(120);
            const auto result = pdfeditor::paintPrintPages(*document, printer, {1, 2, 120, true});
            QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        }
        const auto printed = pdfeditor::PdfDocument::open(output).document;
        QVERIFY(printed);
        QCOMPARE(printed->pageCount(), qsizetype{2});
        const auto first = printed->renderPage(0, 0.25).image;
        const auto second = printed->renderPage(1, 0.25).image;
        QVERIFY(!first.isNull() && !second.isNull());
        const auto blue = first.pixelColor(first.rect().center());
        const auto green = second.pixelColor(second.rect().center());
        QVERIFY(blue.blue() > 230 && blue.red() < 20 && blue.green() < 20);
        QVERIFY(green.green() > 230 && green.red() < 20 && green.blue() < 20);
        QCOMPARE(testdata::digest(path), digest);
    }

    void addsAndEditsANoteThroughTheMenu_data()
    {
        QTest::addColumn<bool>("keyboard");
        QTest::newRow("mouse") << false;
        QTest::newRow("keyboard") << true;
    }

    void addsAndEditsANoteThroughTheMenu()
    {
        QFETCH(bool, keyboard);
        QTemporaryDir directory;
        const auto path = directory.filePath("notes.pdf");
        QVERIFY(testdata::writeVisualPdf(path, 270));
        const auto digest = testdata::digest(path);
        pdfeditor::MainWindow window;
        window.show();
        auto* add = window.findChild<QAction*>(QStringLiteral("addNoteAction"));
        auto* notes = window.findChild<QAction*>(QStringLiteral("pageNotesAction"));
        auto* save = window.findChild<QAction*>(QStringLiteral("saveAction"));
        auto* view = window.findChild<pdfeditor::PageView*>();
        auto* page = window.findChild<QLabel*>(QStringLiteral("pageImage"));
        QVERIFY(add && notes && save && view && page);
        QVERIFY(!add->isEnabled() && !notes->isEnabled());
        window.openDocument(path);
        QTRY_VERIFY(view->hasPageImage() && !view->isRendering());
        QVERIFY(add->isEnabled() && notes->isEnabled());

        add->trigger();
        QVERIFY(add->isChecked());
        QTest::keyClick(view, Qt::Key_Escape);
        QVERIFY(!add->isChecked() && !window.isWindowModified());

        const auto enterNote = [&window](const QString& text) {
            QTimer::singleShot(0, &window, [&window, text] {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if (!dialog) { return; }
                auto* editor = dialog->findChild<QPlainTextEdit*>(QStringLiteral("noteText"));
                auto* buttons = dialog->findChild<QDialogButtonBox*>();
                if (!editor || !buttons) { dialog->reject(); return; }
                editor->setPlainText(text);
                buttons->button(QDialogButtonBox::Save)->click();
            });
        };
        enterNote(QStringLiteral("Primera versión"));
        add->trigger();
        if (keyboard) {
            QTest::keyClick(view, Qt::Key_Return);
        } else {
            const auto origin = page->mapTo(view->viewport(), QPoint());
            const auto visible = view->viewport()->rect().intersected(QRect(origin, page->size()));
            QVERIFY(!visible.isEmpty());
            QTest::mouseClick(page, Qt::LeftButton, Qt::NoModifier, visible.center() - origin);
        }
        QTRY_VERIFY(save->isEnabled() && window.isWindowModified());
        QVERIFY(!add->isChecked());
        QCOMPARE(testdata::digest(path), digest);

        const auto finalText = QStringLiteral("Nota revisada\nSegunda línea");
        enterNote(finalText);
        notes->trigger();
        QTRY_VERIFY(save->isEnabled() && window.isWindowModified());
        save->trigger();
        QTRY_VERIFY(!window.isWindowModified());
        QCOMPARE(window.currentFilePath(), path);
        QCOMPARE(view->currentPage(), qsizetype{0});
        const auto reopened = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(reopened);
        QCOMPARE(reopened->pageNotes(0).size(), 1);
        QCOMPARE(reopened->pageNotes(0)[0].text, finalText);
    }

    void extractsPagesWithoutSwitchingDocumentsOrSavingPendingEdits()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("original.pdf");
        const auto destination = directory.filePath("extracted.pdf");
        QVERIFY(testdata::writeReorderPdf(path));
        const auto original = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(original);
        const auto digest = testdata::digest(path);

        pdfeditor::MainWindow window;
        window.show();
        auto* extract = window.findChild<QAction*>(QStringLiteral("extractPagesAction"));
        auto* rotate = window.findChild<QAction*>(QStringLiteral("rotatePageAction"));
        auto* view = window.findChild<pdfeditor::PageView*>();
        QVERIFY(extract && rotate && view);
        QVERIFY(!extract->isEnabled());
        window.openDocument(path);
        QTRY_VERIFY(extract->isEnabled());
        view->setPage(1);
        rotate->trigger();
        QTRY_VERIFY(extract->isEnabled() && window.isWindowModified());
        QTRY_VERIFY(view->hasPageImage() && !view->isRendering());

        const bool nonNative = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        const auto restoreDialogs = qScopeGuard([nonNative] {
            QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nonNative);
        });
        int initialFirst = 0;
        int initialLast = 0;
        QTimer::singleShot(0, &window, [&] {
            auto* first = window.findChild<QSpinBox*>(QStringLiteral("extractFirstPage"));
            auto* last = window.findChild<QSpinBox*>(QStringLiteral("extractLastPage"));
            if (!first || !last) { return; }
            initialFirst = first->value();
            initialLast = last->value();
            last->setValue(3);
            QTimer::singleShot(0, &window, [&] {
                if (auto* dialog = window.findChild<QFileDialog*>()) {
                    dialog->selectFile(destination);
                    static_cast<QDialog*>(dialog)->accept();
                    if (dialog->isVisible()) { dialog->reject(); }
                }
            });
            qobject_cast<QDialog*>(first->window())->accept();
        });
        extract->trigger();
        QCOMPARE(initialFirst, 2);
        QCOMPARE(initialLast, 2);
        QTRY_VERIFY(extract->isEnabled() && QFileInfo::exists(destination));
        const auto result = pdfeditor::PdfDocument::open(destination).document;
        QVERIFY(result);
        QCOMPARE(result->pageCount(), qsizetype{2});
        QCOMPARE(result->pageSize(0), original->pageSize(1).transposed());
        QCOMPARE(result->renderPage(1).image, original->renderPage(2).image);
        QCOMPARE(window.currentFilePath(), path);
        QCOMPARE(window.pageCount(), qsizetype{3});
        QCOMPARE(view->currentPage(), qsizetype{1});
        QVERIFY(window.isWindowModified());
        QCOMPARE(testdata::digest(path), digest);
    }

    void movesTheSelectedPageAndSavesTogetherWithARotation()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("pages.pdf");
        QVERIFY(testdata::writeReorderPdf(path));
        const auto original = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(original);
        const auto digest = testdata::digest(path);
        pdfeditor::MainWindow window;
        window.show();
        auto* up = window.findChild<QAction*>(QStringLiteral("movePageUpAction"));
        auto* down = window.findChild<QAction*>(QStringLiteral("movePageDownAction"));
        auto* rotate = window.findChild<QAction*>(QStringLiteral("rotatePageAction"));
        auto* save = window.findChild<QAction*>(QStringLiteral("saveAction"));
        auto* view = window.findChild<pdfeditor::PageView*>();
        auto* thumbs = window.findChild<pdfeditor::ThumbnailView*>();
        auto* pageNumber = window.findChild<QSpinBox*>(QStringLiteral("pageNumber"));
        QVERIFY(up && down && rotate && save && view && thumbs && pageNumber);
        QVERIFY(!up->isEnabled() && !down->isEnabled());
        window.openDocument(path);
        QTRY_VERIFY(down->isEnabled());
        QVERIFY(!up->isEnabled());
        view->setZoom(1.0);
        QTRY_VERIFY(view->hasPageImage() && !view->isRendering());
        const auto firstSize = view->imageSize();

        down->trigger();
        QTRY_COMPARE(view->currentPage(), qsizetype{1});
        QCOMPARE(thumbs->currentIndex().row(), 1);
        QCOMPARE(pageNumber->value(), 2);
        QVERIFY(up->isEnabled() && down->isEnabled() && save->isEnabled());
        QTRY_VERIFY(view->hasPageImage() && !view->isRendering());
        QCOMPARE(view->imageSize(), firstSize);
        const auto thumbnailSize = [thumbs](int row) {
            return thumbs->model()->index(row, 0).data(Qt::DecorationRole).value<QIcon>().actualSize(thumbs->iconSize());
        };
        QTRY_VERIFY(thumbnailSize(0).width() > thumbnailSize(0).height());
        QTRY_VERIFY(thumbnailSize(1).width() < thumbnailSize(1).height());

        down->trigger();
        QTRY_COMPARE(view->currentPage(), qsizetype{2});
        QVERIFY(up->isEnabled() && !down->isEnabled());
        up->trigger();
        QTRY_COMPARE(view->currentPage(), qsizetype{1});
        QTRY_VERIFY(view->hasPageImage() && !view->isRendering());
        QCOMPARE(thumbs->currentIndex().row(), 1);
        QCOMPARE(view->zoom(), 1.0);

        const auto artifacts = qEnvironmentVariable("PDF_EDITOR_TEST_ARTIFACT_DIR");
        if (!artifacts.isEmpty()) {
            QVERIFY(QDir().mkpath(artifacts));
            QVERIFY(window.grab().save(QDir(artifacts).filePath("reorder-window.png")));
        }

        rotate->trigger();
        QTRY_VERIFY(save->isEnabled());
        QCOMPARE(testdata::digest(path), digest);
        save->trigger();
        QTRY_VERIFY(!window.isWindowModified());
        QCOMPARE(view->currentPage(), qsizetype{1});
        const auto saved = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(saved);
        QCOMPARE(saved->pageCount(), qsizetype{3});
        QCOMPARE(saved->renderPage(0).image, original->renderPage(1).image);
        QCOMPARE(saved->pageSize(1), original->pageSize(0).transposed());
        QCOMPARE(saved->renderPage(2).image, original->renderPage(2).image);
    }

    void appliesTwoRotationsBeforeSavingOneCopy()
    {
        QTemporaryDir directory;
        const auto original = directory.filePath("original.pdf");
        const auto destination = directory.filePath("edited.pdf");
        QVERIFY(testdata::writePdf(original, 2, true));
        const auto digest = testdata::digest(original);
        pdfeditor::MainWindow window;
        window.show();
        auto* rotate = window.findChild<QAction*>(QStringLiteral("rotatePageAction"));
        auto* save = window.findChild<QAction*>(QStringLiteral("saveAction"));
        auto* saveAs = window.findChild<QAction*>(QStringLiteral("saveAsAction"));
        auto* view = window.findChild<pdfeditor::PageView*>();
        auto* thumbnails = window.findChild<pdfeditor::ThumbnailView*>();
        auto* pending = window.findChild<QLabel*>(QStringLiteral("pendingEdits"));
        QVERIFY(rotate && save && saveAs && view && thumbnails && pending);
        QVERIFY(!rotate->isEnabled() && !save->isEnabled() && !saveAs->isEnabled());
        window.openDocument(original);
        QTRY_VERIFY(rotate->isEnabled());
        view->setPage(1);
        QTRY_VERIFY(view->hasPageImage() && !view->isRendering());
        QVERIFY(view->imageSize().width() > view->imageSize().height());

        const auto thumbnailSize = [thumbnails] {
            return thumbnails->model()->index(1, 0).data(Qt::DecorationRole).value<QIcon>().actualSize(thumbnails->iconSize());
        };
        rotate->trigger();
        QTRY_VERIFY(rotate->isEnabled() && view->hasPageImage() && !view->isRendering());
        QCOMPARE(view->currentPage(), qsizetype{1});
        QVERIFY(view->imageSize().width() < view->imageSize().height());
        QTRY_VERIFY(thumbnailSize().width() < thumbnailSize().height());
        QVERIFY(window.isWindowModified() && pending->isVisible());
        QVERIFY(save->isEnabled() && saveAs->isEnabled());
        QCOMPARE(testdata::digest(original), digest);

        rotate->trigger();
        QTRY_VERIFY(rotate->isEnabled() && view->hasPageImage() && !view->isRendering());
        QVERIFY(view->imageSize().width() > view->imageSize().height());
        QTRY_VERIFY(thumbnailSize().width() > thumbnailSize().height());
        QCOMPARE(window.currentFilePath(), original);
        QCOMPARE(testdata::digest(original), digest);
        QVERIFY(!QFileInfo::exists(destination));

        const auto artifacts = qEnvironmentVariable("PDF_EDITOR_TEST_ARTIFACT_DIR");
        if (!artifacts.isEmpty()) {
            QVERIFY(QDir().mkpath(artifacts));
            QVERIFY(window.grab().save(QDir(artifacts).filePath("pending-edits-window.png")));
        }

        const bool nonNative = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        const auto restoreDialogs = qScopeGuard([nonNative] {
            QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nonNative);
        });
        QTimer::singleShot(0, &window, [&window, destination] {
            if (auto* dialog = window.findChild<QFileDialog*>()) {
                dialog->selectFile(destination);
                static_cast<QDialog*>(dialog)->accept();
                if (dialog->isVisible()) { dialog->reject(); }
            }
        });
        saveAs->trigger();
        QTRY_COMPARE(window.currentFilePath(), destination);
        QCOMPARE(view->currentPage(), qsizetype{1});
        QTRY_VERIFY(view->hasPageImage() && !view->isRendering());
        QVERIFY(view->imageSize().width() > view->imageSize().height());
        QVERIFY(!window.isWindowModified() && !pending->isVisible());
        QCOMPARE(testdata::digest(original), digest);
        const auto copyDigest = testdata::digest(destination);
        QVERIFY(!copyDigest.isEmpty());

        rotate->trigger();
        QTRY_VERIFY(save->isEnabled());
        save->trigger();
        QTRY_VERIFY(!window.isWindowModified());
        QCOMPARE(window.currentFilePath(), destination);
        QCOMPARE(view->currentPage(), qsizetype{1});
        QVERIFY(testdata::digest(destination) != copyDigest);
        QCOMPARE(testdata::digest(original), digest);
    }

    void resolvesPendingEditsBeforeOpeningOrClosing()
    {
        QTemporaryDir directory;
        const auto original = directory.filePath("first.pdf");
        const auto other = directory.filePath("second.pdf");
        QVERIFY(testdata::writePdf(original, 1));
        QVERIFY(testdata::writePdf(other, 1));
        const auto originalDigest = testdata::digest(original);
        pdfeditor::MainWindow window;
        window.show();
        auto* rotate = window.findChild<QAction*>(QStringLiteral("rotatePageAction"));
        auto* save = window.findChild<QAction*>(QStringLiteral("saveAction"));
        auto* error = window.findChild<QLabel*>(QStringLiteral("documentError"));
        QVERIFY(rotate && save && error);
        window.openDocument(original);
        QTRY_VERIFY(rotate->isEnabled());
        rotate->trigger();
        QTRY_VERIFY(save->isEnabled());

        choosePendingAction(window, QMessageBox::RejectRole);
        window.closeDocument();
        QVERIFY(window.isWindowModified());
        QCOMPARE(window.currentFilePath(), original);
        QCOMPARE(testdata::digest(original), originalDigest);

        QSignalSpy failed(&window, &pdfeditor::MainWindow::documentOpenFailed);
        choosePendingAction(window, QMessageBox::DestructiveRole);
        window.openDocument(directory.filePath("missing.pdf"));
        QTRY_COMPARE(failed.size(), 1);
        QVERIFY(window.isWindowModified());
        QCOMPARE(window.currentFilePath(), original);

        choosePendingAction(window, QMessageBox::AcceptRole);
        window.openDocument(other);
        QTRY_COMPARE(window.currentFilePath(), other);
        QVERIFY(!window.isWindowModified());
        QVERIFY(testdata::digest(original) != originalDigest);

        rotate->trigger();
        QTRY_VERIFY(save->isEnabled());
        QVERIFY(testdata::writePdf(other, 2));
        const auto externalDigest = testdata::digest(other);
        choosePendingAction(window, QMessageBox::AcceptRole);
        window.closeDocument();
        QTRY_VERIFY(save->isEnabled() && error->isVisible());
        QVERIFY(window.isWindowModified());
        QCOMPARE(window.currentFilePath(), other);

        choosePendingAction(window, QMessageBox::DestructiveRole);
        window.close();
        QTRY_VERIFY(!window.isVisible());
        QCOMPARE(testdata::digest(other), externalDigest);
    }

    void showsAnnotationsInThePageAndThumbnail()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("annotations.pdf");
        using Extras = testdata::VisualPdfExtras;
        QVERIFY(testdata::writeVisualPdf(path, 0, 1, Extras::AnnotationsAndForm));
        pdfeditor::MainWindow window;
        window.show();
        auto* view = window.findChild<pdfeditor::PageView*>();
        auto* thumbnails = window.findChild<pdfeditor::ThumbnailView*>();
        auto* page = window.findChild<QLabel*>(QStringLiteral("pageImage"));
        auto* message = window.findChild<QLabel*>(QStringLiteral("documentError"));
        QVERIFY(view && thumbnails && page && message);
        window.openDocument(path);
        QTRY_COMPARE(window.currentFilePath(), path);
        view->setZoom(1.75);
        QTRY_VERIFY(view->hasPageImage() && !view->isRendering());
        QVERIFY(message->isHidden());

        const auto thumbnail = [thumbnails] {
            return thumbnails->model()->index(0, 0).data(Qt::DecorationRole).value<QIcon>();
        };
        QTRY_VERIFY(!thumbnail().isNull());
        const auto hasHighlight = [](const QImage& image) {
            if (image.isNull()) { return false; }
            const auto color = image.pixelColor(qRound((image.width() - 1) * 90.0 / 160.0),
                qRound((image.height() - 1) * 30.0 / 220.0));
            return color.red() > 220 && color.green() > 220 && color.blue() < 180;
        };
        QVERIFY(hasHighlight(page->pixmap().toImage()));
        QVERIFY(hasHighlight(thumbnail().pixmap(thumbnails->iconSize()).toImage()));

        const auto artifacts = qEnvironmentVariable("PDF_EDITOR_TEST_ARTIFACT_DIR");
        if (!artifacts.isEmpty()) {
            QVERIFY(QDir().mkpath(artifacts));
            QVERIFY(window.grab().save(QDir(artifacts).filePath("annotations-window.png")));
        }

        const auto incomplete = directory.filePath("missing-appearance.pdf");
        QVERIFY(testdata::writeVisualPdf(incomplete, 0, 1, Extras::MissingAppearance));
        window.openDocument(incomplete);
        QTRY_COMPARE(window.currentFilePath(), incomplete);
        QTRY_VERIFY(view->hasPageImage() && !view->isRendering());
        QVERIFY(message->isVisible());
    }

    void navigatesWithControlsAndLoadsVisibleThumbnails()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("pages.pdf");
        QVERIFY(testdata::writePdf(path, 12, true));
        const auto replacement = directory.filePath("single.pdf");
        QVERIFY(testdata::writePdf(replacement, 1));
        pdfeditor::MainWindow window;
        window.show();
        auto* view = window.findChild<pdfeditor::PageView*>();
        auto* thumbnails = window.findChild<pdfeditor::ThumbnailView*>();
        auto* number = window.findChild<QSpinBox*>(QStringLiteral("pageNumber"));
        auto* previous = window.findChild<QAction*>(QStringLiteral("previousPageAction"));
        auto* next = window.findChild<QAction*>(QStringLiteral("nextPageAction"));
        QVERIFY(view && thumbnails && number && previous && next);
        auto* model = thumbnails->model();
        const auto hasPreview = [model](int row) {
            return !model->index(row, 0).data(Qt::DecorationRole).value<QIcon>().isNull();
        };
        QVERIFY(!number->isEnabled() && !previous->isEnabled() && !next->isEnabled());
        window.openDocument(path);
        QTRY_COMPARE(window.pageCount(), qsizetype{12});
        QCOMPARE(model->rowCount(), 12);
        QCOMPARE(number->value(), 1);
        QVERIFY(!previous->isEnabled() && next->isEnabled());
        QTRY_VERIFY(view->hasPageImage() && hasPreview(0) && hasPreview(1));
        QVERIFY(!hasPreview(11));

        next->trigger();
        QCOMPARE(number->value(), 2);
        QCOMPARE(thumbnails->currentIndex().row(), 1);
        QTRY_VERIFY(view->hasPageImage() && !view->isRendering());
        QVERIFY(view->imageSize().width() > view->imageSize().height());
        QVERIFY(window.statusBar()->currentMessage().startsWith(QStringLiteral("Página 2 de 12")));

        number->setFocus();
        number->selectAll();
        QTest::keyClicks(number, "12");
        QTest::keyClick(number, Qt::Key_Return);
        QCOMPARE(view->currentPage(), qsizetype{11});
        QCOMPARE(thumbnails->currentIndex().row(), 11);
        QVERIFY(previous->isEnabled() && !next->isEnabled());
        QTRY_VERIFY(hasPreview(11));

        const auto penultimate = thumbnails->visualRect(model->index(10, 0)).center();
        QVERIFY(thumbnails->viewport()->rect().contains(penultimate));
        QTest::mouseClick(thumbnails->viewport(), Qt::LeftButton, Qt::NoModifier, penultimate);
        QCOMPARE(view->currentPage(), qsizetype{10});
        QCOMPARE(number->value(), 11);
        previous->trigger();
        QCOMPARE(number->value(), 10);
        QCOMPARE(thumbnails->currentIndex().row(), 9);
        QTRY_VERIFY(view->hasPageImage() && !view->isRendering());

        const auto artifacts = qEnvironmentVariable("PDF_EDITOR_TEST_ARTIFACT_DIR");
        if (!artifacts.isEmpty()) {
            QVERIFY(QDir().mkpath(artifacts));
            QVERIFY(window.grab().save(QDir(artifacts).filePath("navigation-window.png")));
        }

        QSignalSpy failed(&window, &pdfeditor::MainWindow::documentOpenFailed);
        window.openDocument(directory.filePath("missing.pdf"));
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(number->value(), 10);
        QCOMPARE(thumbnails->currentIndex().row(), 9);
        window.openDocument(replacement);
        QTRY_COMPARE(model->rowCount(), 1);
        QCOMPARE(view->currentPage(), qsizetype{0});
        QCOMPARE(number->value(), 1);
        QVERIFY(!number->isEnabled() && !previous->isEnabled() && !next->isEnabled());

        window.closeDocument();
        QCOMPARE(model->rowCount(), 0);
        QCOMPARE(view->currentPage(), qsizetype{-1});
        QVERIFY(!thumbnails->isVisible() && !view->hasPageImage());
    }

    void opensClosesAndPreservesTheDocumentOnFailure()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("documento de prueba.pdf"));
        QVERIFY(testdata::writePdf(path));
        const auto before = testdata::digest(path);
        pdfeditor::MainWindow window;
        QSignalSpy opened(&window, &pdfeditor::MainWindow::documentOpened);
        QSignalSpy failed(&window, &pdfeditor::MainWindow::documentOpenFailed);
        auto* openAction = window.findChild<QAction*>(QStringLiteral("openAction"));
        auto* closeAction = window.findChild<QAction*>(QStringLiteral("closeDocumentAction"));
        QVERIFY(openAction && closeAction);
        QVERIFY(!closeAction->isEnabled());

        window.openDocument(path);
        QVERIFY(!openAction->isEnabled());
        window.openDocument(directory.filePath("ignored-while-loading.pdf"));
        QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 10000);
        QCOMPARE(failed.size(), 0);
        QCOMPARE(window.currentFilePath(), path);
        QCOMPARE(window.pageCount(), qsizetype{2});
        QVERIFY(window.windowTitle().contains(QStringLiteral("documento de prueba.pdf")));
        QVERIFY(openAction->isEnabled());
        QVERIFY(closeAction->isEnabled());

        window.openDocument({}); // Cancelling the file picker leaves the document unchanged.
        QCOMPARE(window.currentFilePath(), path);
        window.openDocument(directory.filePath("missing.pdf"));
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 10000);
        QCOMPARE(window.currentFilePath(), path);
        QCOMPARE(window.pageCount(), qsizetype{2});
        auto* error = window.findChild<QLabel*>(QStringLiteral("documentError"));
        QVERIFY(error && !error->isHidden());
        QVERIFY(openAction->isEnabled());

        const auto secondPath = directory.filePath("second.pdf");
        QVERIFY(testdata::writePdf(secondPath, 1));
        window.openDocument(secondPath);
        QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 2, 10000);
        QCOMPARE(window.currentFilePath(), secondPath);
        QCOMPARE(window.pageCount(), qsizetype{1});
        QVERIFY(error->isHidden());

        closeAction->trigger();
        QVERIFY(window.currentFilePath().isEmpty());
        QCOMPARE(window.pageCount(), qsizetype{0});
        QVERIFY(!closeAction->isEnabled());
        QVERIFY(error->isHidden());
        QCOMPARE(testdata::digest(path), before);
    }

    void windowCanStartAndCloseFromItsMenu()
    {
        pdfeditor::MainWindow window;
        window.show();
        QTRY_VERIFY(window.isVisible());
        QVERIFY(window.centralWidget() != nullptr);

        auto* quitAction = window.findChild<QAction*>(QStringLiteral("quitAction"));
        QVERIFY(quitAction != nullptr);
        QVERIFY(quitAction->isEnabled());

        quitAction->trigger();
        QTRY_VERIFY(!window.isVisible());
    }

    void rendersTheFirstPageAndConnectsZoomControls()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("document.pdf");
        QVERIFY(testdata::writeVisualPdf(path));
        pdfeditor::MainWindow window;
        window.show();
        auto* view = window.findChild<pdfeditor::PageView*>();
        QVERIFY(view);
        auto* actualSize = window.findChild<QAction*>(QStringLiteral("actualSizeAction"));
        auto* zoomIn = window.findChild<QAction*>(QStringLiteral("zoomInAction"));
        auto* fitWidth = window.findChild<QAction*>(QStringLiteral("fitWidthAction"));
        QVERIFY(actualSize && zoomIn && fitWidth);
        QSignalSpy opened(&window, &pdfeditor::MainWindow::documentOpened);
        QSignalSpy failed(view, &pdfeditor::PageView::renderFailed);
        window.openDocument(path);
        QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(view->hasPageImage() && !view->isRendering(), 10000);
        QVERIFY(view->fitsWidth());
        actualSize->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!view->isRendering(), 10000);
        QCOMPARE(view->zoom(), 1.0);
        QVERIFY(!view->fitsWidth());
        const auto normal = view->imageSize();
        zoomIn->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!view->isRendering() && view->imageSize().width() > normal.width(), 10000);
        fitWidth->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!view->isRendering(), 10000);
        QVERIFY(view->fitsWidth());
        QCOMPARE(failed.size(), 0);
        const auto artifacts = qEnvironmentVariable("PDF_EDITOR_TEST_ARTIFACT_DIR");
        if (!artifacts.isEmpty()) {
            QVERIFY(QDir().mkpath(artifacts));
            QVERIFY(window.grab().save(QDir(artifacts).filePath("viewer-window.png")));
        }
        window.closeDocument();
        QVERIFY(!view->hasPageImage());
    }
};

QTEST_MAIN(MainWindowTest)
#include "MainWindowTest.moc"
