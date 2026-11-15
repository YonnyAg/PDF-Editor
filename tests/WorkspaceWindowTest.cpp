#include "ui/WorkspaceWindow.h"
#include "ui/MainWindow.h"
#include "ui/PageView.h"
#include "PdfTestData.h"
#include "VisualPdfData.h"

#include <QAction>
#include <QApplication>
#include <QAbstractButton>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUuid>

class WorkspaceWindowTest final : public QObject
{
    Q_OBJECT
public:
    static void initMain() { QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs); }

private:
    static QAction* action(pdfeditor::MainWindow* pane, const char* name)
    {
        return pane->findChild<QAction*>(QString::fromLatin1(name));
    }

    static void answerNext(pdfeditor::WorkspaceWindow& window, QMessageBox::ButtonRole role)
    {
        QTimer::singleShot(0, &window, [role] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                for (auto* button : box->buttons()) { if (box->buttonRole(button) == role) { button->click(); return; } }
                box->reject();
            }
        });
    }

private slots:
    void keepsIndependentDocumentsHistoryAndView()
    {
        QTemporaryDir directory;
        const auto first = directory.filePath("first.pdf");
        const auto second = directory.filePath("second.pdf");
        QVERIFY(testdata::writeReorderPdf(first));
        QVERIFY(testdata::writeVisualPdf(second));
        const auto firstHash = testdata::digest(first);
        pdfeditor::WorkspaceWindow window(directory.filePath("session"));
        window.show();
        window.openDocument(first);
        QTRY_COMPARE(window.documentCount(), 1);
        auto* firstPane = window.currentDocument();
        QTRY_VERIFY(!firstPane->isBusy());
        auto* firstView = firstPane->findChild<pdfeditor::PageView*>();
        QVERIFY(firstView);
        firstView->setPage(1);
        firstView->setZoom(1.5);
        action(firstPane, "rotatePageAction")->trigger();
        QTRY_VERIFY(!firstPane->isBusy() && firstPane->isWindowModified());
        window.openDocument(second);
        QTRY_COMPARE(window.documentCount(), 2);
        auto* secondPane = window.currentDocument();
        QVERIFY(secondPane != firstPane);
        QTRY_VERIFY(!secondPane->isBusy());
        QVERIFY(!secondPane->isWindowModified());
        QVERIFY(!action(secondPane, "undoAction")->isEnabled());
        auto* secondView = secondPane->findChild<pdfeditor::PageView*>();
        secondView->setZoom(0.8);
        auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
        QVERIFY(tabs);
        tabs->setCurrentWidget(firstPane);
        QCOMPARE(firstView->currentPage(), 1);
        QCOMPARE(firstView->zoom(), 1.5);
        QVERIFY(firstPane->isWindowModified());
        QVERIFY(tabs->tabText(tabs->indexOf(firstPane)).endsWith(" *"));
        QTest::keyClick(firstPane, Qt::Key_Tab, Qt::ControlModifier);
        QTRY_COMPARE(window.currentDocument(), secondPane);
        QTest::keyClick(secondPane, Qt::Key_Tab, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_COMPARE(window.currentDocument(), firstPane);
        action(firstPane, "undoAction")->trigger();
        QVERIFY(!firstPane->isWindowModified());
        QCOMPARE(testdata::digest(first), firstHash);
        action(firstPane, "redoAction")->trigger();
        QVERIFY(firstPane->isWindowModified());
        window.openDocument(second);
        QCOMPARE(window.documentCount(), 2);
        QCOMPARE(window.currentDocument(), secondPane);
        QCOMPARE(secondView->zoom(), 0.8);
        const auto artifacts = qEnvironmentVariable("PDF_EDITOR_TEST_ARTIFACT_DIR");
        QTRY_VERIFY(secondView->hasPageImage() && !secondView->isRendering());
        if (!artifacts.isEmpty() && QDir().mkpath(artifacts)) { window.grab().save(QDir(artifacts).filePath("workspace-tabs.png")); }
    }

    void keyboardSaveAffectsOnlyTheActiveTabAndRecentFilesDeduplicate()
    {
        QTemporaryDir directory;
        const auto first = directory.filePath("first.pdf");
        const auto second = directory.filePath("second.pdf");
        QVERIFY(testdata::writeVisualPdf(first));
        QVERIFY(testdata::writeVisualPdf(second));
        const auto firstHash = testdata::digest(first);
        const auto secondHash = testdata::digest(second);
        pdfeditor::WorkspaceWindow window(directory.filePath("session"));
        window.show();
        window.openDocument(first);
        QTRY_COMPARE(window.documentCount(), 1);
        auto* firstPane = window.currentDocument();
        QTRY_VERIFY(!firstPane->isBusy());
        action(firstPane, "rotatePageAction")->trigger();
        QTRY_VERIFY(firstPane->isWindowModified() && !firstPane->isBusy());
        window.openDocument(second);
        QTRY_COMPARE(window.documentCount(), 2);
        auto* secondPane = window.currentDocument();
        QTRY_VERIFY(!secondPane->isBusy());
        action(secondPane, "rotatePageAction")->trigger();
        QTRY_VERIFY(secondPane->isWindowModified() && !secondPane->isBusy());
        secondPane->setFocus();
        QTest::keyClick(secondPane, Qt::Key_S, Qt::ControlModifier);
        QTRY_VERIFY(!secondPane->isWindowModified() && !secondPane->isBusy());
        QVERIFY(firstPane->isWindowModified());
        QCOMPARE(testdata::digest(first), firstHash);
        QVERIFY(testdata::digest(second) != secondHash);
        QCOMPARE(window.recentFiles().size(), 2);
        auto* menu = secondPane->findChild<QMenu*>(QStringLiteral("recentFilesMenu"));
        QVERIFY(menu);
        int fileActions = 0;
        for (auto* item : menu->actions()) { if (item->data().isValid()) { ++fileActions; } }
        QCOMPARE(fileActions, 2);
        for (auto* item : menu->actions()) { if (item->data().toString() == first) { item->trigger(); break; } }
        QCOMPARE(window.currentDocument(), firstPane);
        QCOMPARE(window.documentCount(), 2);
        auto* clear = firstPane->findChild<QAction*>(QStringLiteral("clearRecentFilesAction"));
        QVERIFY(clear);
        clear->trigger();
        QTRY_VERIFY(window.recentFiles().isEmpty());
        QCOMPARE(window.documentCount(), 2);
    }

    void closesATabWithCancelSaveAndDiscard()
    {
        QTemporaryDir directory;
        const auto first = directory.filePath("first.pdf");
        const auto second = directory.filePath("second.pdf");
        QVERIFY(testdata::writeVisualPdf(first));
        QVERIFY(testdata::writeVisualPdf(second));
        const auto firstHash = testdata::digest(first);
        pdfeditor::WorkspaceWindow window(directory.filePath("session"));
        window.show();
        window.openDocument(first);
        QTRY_COMPARE(window.documentCount(), 1);
        auto* pane = window.currentDocument();
        QTRY_VERIFY(!pane->isBusy());
        action(pane, "rotatePageAction")->trigger();
        QTRY_VERIFY(pane->isWindowModified() && !pane->isBusy());
        answerNext(window, QMessageBox::RejectRole);
        window.closeTab(0);
        QCOMPARE(window.documentCount(), 1);
        QVERIFY(pane->isWindowModified());
        answerNext(window, QMessageBox::AcceptRole);
        window.closeTab(0);
        QTRY_COMPARE(window.documentCount(), 0);
        QVERIFY(testdata::digest(first) != firstHash);
        window.openDocument(second);
        QTRY_COMPARE(window.documentCount(), 1);
        pane = window.currentDocument();
        QTRY_VERIFY(!pane->isBusy());
        const auto secondHash = testdata::digest(second);
        action(pane, "rotatePageAction")->trigger();
        QTRY_VERIFY(pane->isWindowModified() && !pane->isBusy());
        answerNext(window, QMessageBox::DestructiveRole);
        action(pane, "closeDocumentAction")->trigger();
        QCOMPARE(window.documentCount(), 0);
        QCOMPARE(testdata::digest(second), secondHash);
    }

    void restoresPendingChangesAndViewAfterAnInterruptedSession()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("original.pdf");
        const auto sessionPath = directory.filePath("session");
        QVERIFY(testdata::writeReorderPdf(path));
        const auto originalHash = testdata::digest(path);
        {
            auto first = std::make_unique<pdfeditor::WorkspaceWindow>(sessionPath);
            first->show();
            first->openDocument(path);
            QTRY_COMPARE(first->documentCount(), 1);
            auto* pane = first->currentDocument();
            QTRY_VERIFY(!pane->isBusy());
            auto* view = pane->findChild<pdfeditor::PageView*>();
            view->setPage(1);
            view->setZoom(1.6);
            action(pane, "rotatePageAction")->trigger();
            QTRY_VERIFY(pane->isWindowModified() && !pane->isBusy());
            QSignalSpy saved(first.get(), &pdfeditor::WorkspaceWindow::sessionSaved);
            first->flushSession();
            QTRY_VERIFY(!saved.isEmpty());
            QVERIFY2(saved.back()[0].toString().isEmpty(), qPrintable(saved.back()[0].toString()));
            first.reset(); // Destroy without a close event to simulate interruption.
        }
        QCOMPARE(testdata::digest(path), originalHash);
        pdfeditor::WorkspaceWindow window(sessionPath);
        window.show();
        QSignalSpy restored(&window, &pdfeditor::WorkspaceWindow::sessionRestored);
        window.restoreSession();
        QTRY_COMPARE(restored.size(), 1);
        QCOMPARE(restored[0][0].toInt(), 1);
        QCOMPARE(restored[0][1].toInt(), 0);
        auto* pane = window.currentDocument();
        QVERIFY(pane->isWindowModified());
        QCOMPARE(pane->currentFilePath(), path);
        auto* view = pane->findChild<pdfeditor::PageView*>();
        QCOMPARE(view->currentPage(), 1);
        QCOMPARE(view->zoom(), 1.6);
        QVERIFY(!view->fitsWidth());
        const auto recovered = pane->sessionDocument(QStringLiteral("test"));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        QCOMPARE(recovered.document->renderPage(1).image, source->rotatePage(1).document->renderPage(1).image);
        QTRY_VERIFY(view->hasPageImage() && !view->isRendering());
        const auto artifacts = qEnvironmentVariable("PDF_EDITOR_TEST_ARTIFACT_DIR");
        if (!artifacts.isEmpty() && QDir().mkpath(artifacts)) { window.grab().save(QDir(artifacts).filePath("workspace-recovered.png")); }
        action(pane, "saveAction")->trigger();
        QTRY_VERIFY(!pane->isWindowModified() && !pane->isBusy());
        QVERIFY(testdata::digest(path) != originalHash);
    }

    void cancelsClosingAllWithoutDiscardingEarlierTabs()
    {
        QTemporaryDir directory;
        const auto first = directory.filePath("first.pdf");
        const auto second = directory.filePath("second.pdf");
        QVERIFY(testdata::writeVisualPdf(first));
        QVERIFY(testdata::writeVisualPdf(second));
        pdfeditor::WorkspaceWindow window(directory.filePath("session"));
        window.show();
        window.openDocument(first);
        QTRY_COMPARE(window.documentCount(), 1);
        auto* firstPane = window.currentDocument();
        QTRY_VERIFY(!firstPane->isBusy());
        action(firstPane, "rotatePageAction")->trigger();
        QTRY_VERIFY(!firstPane->isBusy() && firstPane->isWindowModified());
        window.openDocument(second);
        QTRY_COMPARE(window.documentCount(), 2);
        auto* secondPane = window.currentDocument();
        QTRY_VERIFY(!secondPane->isBusy());
        action(secondPane, "rotatePageAction")->trigger();
        QTRY_VERIFY(!secondPane->isBusy() && secondPane->isWindowModified());
        int dialogs = 0;
        QTimer driver;
        driver.setInterval(20);
        connect(&driver, &QTimer::timeout, &window, [&] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                const auto role = dialogs++ == 0 ? QMessageBox::DestructiveRole : QMessageBox::RejectRole;
                for (auto* button : box->buttons()) { if (box->buttonRole(button) == role) { button->click(); return; } }
            }
        });
        driver.start();
        window.close();
        driver.stop();
        QCOMPARE(dialogs, 2);
        QVERIFY(window.isVisible());
        QCOMPARE(window.documentCount(), 2);
        QVERIFY(firstPane->isWindowModified() && secondPane->isWindowModified());
        QVERIFY(window.findChild<QTabWidget*>(QStringLiteral("documentTabs"))->isEnabled());
    }

    void cleanExitRemembersFilesWithoutRestoringDiscardedChanges()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("original.pdf");
        const auto sessionPath = directory.filePath("session");
        QVERIFY(testdata::writeVisualPdf(path));
        const auto digest = testdata::digest(path);
        {
            pdfeditor::WorkspaceWindow window(sessionPath);
            window.show();
            window.openDocument(path);
            QTRY_COMPARE(window.documentCount(), 1);
            auto* pane = window.currentDocument();
            QTRY_VERIFY(!pane->isBusy());
            action(pane, "rotatePageAction")->trigger();
            QTRY_VERIFY(!pane->isBusy() && pane->isWindowModified());
            answerNext(window, QMessageBox::DestructiveRole);
            window.close();
            QTRY_VERIFY(!window.isVisible());
        }
        QCOMPARE(testdata::digest(path), digest);
        pdfeditor::WorkspaceWindow next(sessionPath);
        QSignalSpy restored(&next, &pdfeditor::WorkspaceWindow::sessionRestored);
        next.restoreSession();
        QTRY_COMPARE(restored.size(), 1);
        QCOMPARE(next.documentCount(), 1);
        QVERIFY(!next.currentDocument()->isWindowModified());
        QCOMPARE(next.recentFiles().size(), 1);
        const auto recovered = next.currentDocument()->sessionDocument(QStringLiteral("test"));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        QCOMPARE(recovered.document->renderPage(0).image, source->renderPage(0).image);
    }

    void acceptsOnlyLocalPdfDropsAndKeepsExistingTabsOnOpenFailure()
    {
        QTemporaryDir directory;
        const auto first = directory.filePath("first.pdf");
        const auto second = directory.filePath("second.PDF");
        QVERIFY(testdata::writeVisualPdf(first));
        QVERIFY(testdata::writeVisualPdf(second));
        pdfeditor::WorkspaceWindow window(directory.filePath("session"));
        window.show();
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(first), QUrl(QStringLiteral("https://example.com/remote.pdf")), QUrl::fromLocalFile(second)});
        QDragEnterEvent enter(QPoint(20, 20), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(20, 20), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &drop);
        QVERIFY(drop.isAccepted());
        QTRY_COMPARE(window.documentCount(), 2);
        auto* validPane = window.currentDocument();
        QTRY_VERIFY(!validPane->isBusy());
        window.openDocument(directory.filePath("missing.pdf"));
        auto* missingPane = window.currentDocument();
        QSignalSpy failed(missingPane, &pdfeditor::MainWindow::documentOpenFailed);
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(window.documentCount(), 2);
        QVERIFY(!validPane->currentFilePath().isEmpty());
    }

    void cancelledRestorePreservesThePreviousSession()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("original.pdf");
        const auto sessionPath = directory.filePath("session");
        QVERIFY(testdata::writeVisualPdf(path));
        {
            pdfeditor::SessionStore store(sessionPath);
            pdfeditor::SessionDocument document;
            document.entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            document.document = pdfeditor::PdfDocument::open(path).document;
            QVERIFY(document.document);
            QVERIFY(store.save({document}, {}, {path}).isEmpty());
        }
        const auto indexPath = QDir(sessionPath).filePath("session.json");
        const auto digest = testdata::digest(indexPath);
        pdfeditor::WorkspaceWindow window(sessionPath);
        window.show();
        answerNext(window, QMessageBox::RejectRole);
        QVERIFY(!window.offerSessionRestore());
        window.flushSession();
        QTest::qWait(50);
        QCOMPARE(window.documentCount(), 0);
        QCOMPARE(testdata::digest(indexPath), digest);
        window.close();
        QTRY_VERIFY(!window.isVisible());
        QCOMPARE(testdata::digest(indexPath), digest);
    }

    void keepsFailedSnapshotsDuringPartialRecovery()
    {
        QTemporaryDir directory;
        const auto first = directory.filePath("first.pdf");
        const auto second = directory.filePath("second.pdf");
        const auto sessionPath = directory.filePath("session");
        QVERIFY(testdata::writeVisualPdf(first));
        QVERIFY(testdata::writeVisualPdf(second));
        QString corruptName;
        {
            pdfeditor::SessionStore store(sessionPath);
            QList<pdfeditor::SessionDocument> documents;
            for (const auto& path : {first, second}) {
                pdfeditor::SessionDocument document;
                document.entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                const auto source = pdfeditor::PdfDocument::open(path).document;
                QVERIFY(source);
                document.document = source->rotatePage(0).document;
                document.modified = true;
                documents.append(document);
            }
            QVERIFY(store.save(documents, {}, {first, second}).isEmpty());
            corruptName = store.read().entries[1].snapshot;
            QFile corrupt(QDir(sessionPath).filePath(corruptName));
            QVERIFY(corrupt.open(QIODevice::Append));
            corrupt.write("broken");
        }
        {
            pdfeditor::WorkspaceWindow window(sessionPath);
            window.show();
            QSignalSpy restored(&window, &pdfeditor::WorkspaceWindow::sessionRestored);
            window.restoreSession();
            QTRY_COMPARE(restored.size(), 1);
            QCOMPARE(restored[0][0].toInt(), 1);
            QCOMPARE(restored[0][1].toInt(), 1);
            QVERIFY(window.currentDocument()->isWindowModified());
            auto* message = window.findChild<QLabel*>(QStringLiteral("sessionMessage"));
            QVERIFY(message && message->isVisible());
            QSignalSpy saved(&window, &pdfeditor::WorkspaceWindow::sessionSaved);
            window.flushSession();
            QTRY_VERIFY(!saved.isEmpty());
            QVERIFY2(saved.back()[0].toString().isEmpty(), qPrintable(saved.back()[0].toString()));
        }
        pdfeditor::SessionStore retained(sessionPath);
        const auto index = retained.read();
        QCOMPARE(index.entries.size(), 2);
        bool found = false;
        for (const auto& entry : index.entries) { found = found || entry.snapshot == corruptName; }
        QVERIFY(found);
        QVERIFY(QFileInfo::exists(QDir(sessionPath).filePath(corruptName)));
    }

    void failedSaveDuringExitLeavesTheWindowEditable()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("original.pdf");
        QVERIFY(testdata::writeVisualPdf(path));
        pdfeditor::WorkspaceWindow window(directory.filePath("session"));
        window.show();
        window.openDocument(path);
        QTRY_COMPARE(window.documentCount(), 1);
        auto* pane = window.currentDocument();
        QTRY_VERIFY(!pane->isBusy());
        action(pane, "rotatePageAction")->trigger();
        QTRY_VERIFY(!pane->isBusy() && pane->isWindowModified());
        QVERIFY(testdata::writeVisualPdf(path, 180));
        const auto externalHash = testdata::digest(path);
        answerNext(window, QMessageBox::AcceptRole);
        window.close();
        QTRY_VERIFY(!pane->isBusy());
        QVERIFY(window.isVisible() && pane->isWindowModified());
        auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
        QTRY_VERIFY(tabs->isEnabled());
        QCOMPARE(testdata::digest(path), externalHash);
    }

    void restoresActiveTabOrderAndQueuesRequestedFiles()
    {
        QTemporaryDir directory;
        const auto first = directory.filePath("first.pdf");
        const auto second = directory.filePath("second.pdf");
        const auto third = directory.filePath("third.pdf");
        const auto sessionPath = directory.filePath("session");
        QVERIFY(testdata::writeReorderPdf(first));
        QVERIFY(testdata::writeVisualPdf(second));
        QVERIFY(testdata::writeVisualPdf(third));
        QString firstId;
        QString secondId;
        QString thirdId;
        {
            pdfeditor::SessionStore store(sessionPath);
            QList<pdfeditor::SessionDocument> documents;
            for (const auto& path : {first, second}) {
                pdfeditor::SessionDocument document;
                document.entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                document.document = pdfeditor::PdfDocument::open(path).document;
                QVERIFY(document.document);
                documents.append(document);
            }
            documents[0].entry.page = 1;
            documents[0].entry.zoom = 1.4;
            documents[0].entry.fitWidth = false;
            firstId = documents[0].entry.id;
            secondId = documents[1].entry.id;
            QVERIFY(store.save(documents, {}, {first, second}, true, firstId).isEmpty());
        }
        {
            pdfeditor::WorkspaceWindow window(sessionPath);
            window.show();
            QSignalSpy restored(&window, &pdfeditor::WorkspaceWindow::sessionRestored);
            window.restoreSession();
            QTRY_COMPARE(restored.size(), 1);
            QCOMPARE(window.currentDocument()->currentFilePath(), first);
            const auto view = window.currentDocument()->sessionDocument(firstId).entry;
            QCOMPARE(view.page, 1);
            QCOMPARE(view.zoom, 1.4);
            QCOMPARE(window.currentDocument()->property("sessionId").toString(), firstId);
            auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
            tabs->tabBar()->moveTab(0, 1);
            window.openDocument(third);
            QTRY_COMPARE(window.documentCount(), 3);
            QTRY_VERIFY(!window.currentDocument()->isBusy());
            thirdId = window.currentDocument()->property("sessionId").toString();
            QSignalSpy saved(&window, &pdfeditor::WorkspaceWindow::sessionSaved);
            window.flushSession();
            QTRY_VERIFY(!saved.isEmpty());
            QVERIFY2(saved.back()[0].toString().isEmpty(), qPrintable(saved.back()[0].toString()));
        }
        {
            pdfeditor::SessionStore store(sessionPath);
            const auto index = store.read();
            QCOMPARE(index.entries.size(), 3);
            QCOMPARE(index.entries[0].id, secondId);
            QCOMPARE(index.entries[1].id, firstId);
            QCOMPARE(index.activeId, thirdId);
        }
        pdfeditor::WorkspaceWindow window(sessionPath);
        QSignalSpy restored(&window, &pdfeditor::WorkspaceWindow::sessionRestored);
        window.restoreSession();
        window.openDocument(first); // Queue while restoration is running, then select its existing tab.
        QTRY_COMPARE(restored.size(), 1);
        QTRY_COMPARE(window.currentDocument()->currentFilePath(), first);
        QCOMPARE(window.documentCount(), 3);
    }

    void aSecondInstanceCanEditWithoutOwningRecovery()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("original.pdf");
        const auto sessionPath = directory.filePath("session");
        QVERIFY(testdata::writeVisualPdf(path));
        pdfeditor::WorkspaceWindow first(sessionPath);
        pdfeditor::WorkspaceWindow second(sessionPath);
        second.show();
        QVERIFY(second.offerSessionRestore());
        second.openDocument(path);
        QTRY_COMPARE(second.documentCount(), 1);
        auto* pane = second.currentDocument();
        QTRY_VERIFY(!pane->isBusy());
        action(pane, "rotatePageAction")->trigger();
        QTRY_VERIFY(!pane->isBusy() && pane->isWindowModified());
        action(pane, "saveAction")->trigger();
        QTRY_VERIFY(!pane->isBusy() && !pane->isWindowModified());
        auto* message = second.findChild<QLabel*>(QStringLiteral("sessionMessage"));
        QVERIFY(message && message->isVisible());
    }

    void coalescesEditsThatArriveDuringRecoveryWrites()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("original.pdf");
        const auto sessionPath = directory.filePath("session");
        QVERIFY(testdata::writeVisualPdf(path));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        {
            pdfeditor::WorkspaceWindow window(sessionPath);
            window.show();
            window.openDocument(path);
            QTRY_COMPARE(window.documentCount(), 1);
            auto* pane = window.currentDocument();
            QTRY_VERIFY(!pane->isBusy());
            action(pane, "rotatePageAction")->trigger();
            QTRY_VERIFY(!pane->isBusy() && pane->isWindowModified());
            window.flushSession();
            action(pane, "rotatePageAction")->trigger();
            QTRY_VERIFY(!pane->isBusy());
            QSignalSpy saved(&window, &pdfeditor::WorkspaceWindow::sessionSaved);
            window.flushSession();
            QTRY_VERIFY(!saved.isEmpty());
            QVERIFY2(saved.back()[0].toString().isEmpty(), qPrintable(saved.back()[0].toString()));
        }
        pdfeditor::SessionStore store(sessionPath);
        const auto restored = store.restore(store.read().entries);
        QCOMPARE(restored.size(), 1);
        QVERIFY(restored[0].document);
        const auto twice = source->rotatePage(0).document->rotatePage(0).document;
        QVERIFY(twice);
        QCOMPARE(restored[0].document->renderPage(0).image, twice->renderPage(0).image);
    }
};

QTEST_MAIN(WorkspaceWindowTest)
#include "WorkspaceWindowTest.moc"
