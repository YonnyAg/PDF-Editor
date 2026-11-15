#include "session/SessionStore.h"
#include "VisualPdfData.h"
#include "PdfTestData.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

class SessionStoreTest final : public QObject
{
    Q_OBJECT
private:
    static pdfeditor::SessionDocument entry(const std::shared_ptr<const pdfeditor::PdfDocument>& document, bool modified)
    {
        pdfeditor::SessionDocument result;
        result.document = document;
        result.modified = modified;
        result.entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        result.entry.zoom = 1.7;
        result.entry.fitWidth = false;
        return result;
    }

private slots:
    void snapshotsPendingChangesWithoutWritingTheOriginal()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("original.pdf");
        QVERIFY(testdata::writeVisualPdf(path));
        const auto originalHash = testdata::digest(path);
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const auto rotated = source->rotatePage(0).document;
        QVERIFY(rotated);
        const auto sessionPath = directory.filePath("session");
        pdfeditor::SessionStore store(sessionPath);
        QVERIFY2(store.error().isEmpty(), qPrintable(store.error()));
        const auto document = entry(rotated, true);
        const auto error = store.save({document}, {}, {path}, false, document.entry.id);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const auto index = store.read();
        QVERIFY2(index.error.isEmpty(), qPrintable(index.error));
        QCOMPARE(index.entries.size(), 1);
        QCOMPARE(index.activeId, document.entry.id);
        QCOMPARE(index.recentFiles, QStringList{path});
        QVERIFY(!index.cleanExit);
        QVERIFY(!index.entries[0].snapshot.isEmpty());
        const auto restored = store.restore(index.entries);
        QCOMPARE(restored.size(), 1);
        QVERIFY2(restored[0].document, qPrintable(restored[0].error));
        QCOMPARE(restored[0].document->filePath(), path);
        QCOMPARE(restored[0].document->sourceHash(), originalHash);
        QCOMPARE(restored[0].document->renderPage(0).image, rotated->renderPage(0).image);
        QVERIFY(restored[0].savedDocument);
        QCOMPARE(restored[0].savedDocument->renderPage(0).image, source->renderPage(0).image);
        QCOMPARE(restored[0].entry.zoom, 1.7);
        QVERIFY(!restored[0].entry.fitWidth);
        const auto saved = restored[0].document->save(pdfeditor::PdfSaveMode::NewCopy, directory.filePath("recovered.pdf"));
        QVERIFY2(saved.document, qPrintable(saved.error));
        QCOMPARE(testdata::digest(path), originalHash);
    }

    void cachesSnapshotsAndRollsBackAnUncommittedSession()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("source.pdf");
        QVERIFY(testdata::writeVisualPdf(path));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const auto rotated = source->rotatePage(0).document;
        QVERIFY(rotated);
        const auto sessionPath = directory.filePath("session");
        pdfeditor::SessionStore store(sessionPath);
        auto document = entry(rotated, true);
        QVERIFY(store.save({document}, {}, {path}).isEmpty());
        const auto firstSnapshot = store.read().entries[0].snapshot;
        document.entry.zoom = 2;
        QVERIFY(store.save({document}, {}, {path}).isEmpty());
        QCOMPARE(store.read().entries[0].snapshot, firstSnapshot);
        const auto committedHash = testdata::digest(QDir(sessionPath).filePath("session.json"));
        document.document = rotated->rotatePage(0).document;
        QVERIFY(document.document);
        auto invalid = document;
        invalid.entry.id = QStringLiteral("../invalid");
        QVERIFY(!store.save({document, invalid}, {}, {path}).isEmpty());
        QCOMPARE(testdata::digest(QDir(sessionPath).filePath("session.json")), committedHash);
        QCOMPARE(QDir(sessionPath).entryList({QStringLiteral("*.pdf")}, QDir::Files), QStringList{firstSnapshot});
        QVERIFY(store.save({document}, {}, {path}).isEmpty());
        const auto nextSnapshot = store.read().entries[0].snapshot;
        QVERIFY(nextSnapshot != firstSnapshot);
        QVERIFY(!QFileInfo::exists(QDir(sessionPath).filePath(firstSnapshot)));
        QFile unrelated(QDir(sessionPath).filePath("unrelated.pdf"));
        QVERIFY(unrelated.open(QIODevice::WriteOnly));
        unrelated.write("User file");
        unrelated.close();
        document.document = source;
        document.modified = false;
        QVERIFY(store.save({document}, {}, {path}, true).isEmpty());
        QVERIFY(store.read().cleanExit);
        QVERIFY(store.read().entries[0].snapshot.isEmpty());
        QVERIFY(!QFileInfo::exists(QDir(sessionPath).filePath(nextSnapshot)));
        QVERIFY(QFileInfo::exists(unrelated.fileName()));
    }

    void excludesConcurrentWriters()
    {
        QTemporaryDir directory;
        pdfeditor::SessionStore first(directory.path());
        QVERIFY(first.error().isEmpty());
        pdfeditor::SessionStore second(directory.path());
        QVERIFY(!second.error().isEmpty());
        QVERIFY(!second.read().error.isEmpty());
        QVERIFY(!second.save({}, {}, {}).isEmpty());
        QVERIFY(first.save({}, {}, {}).isEmpty());
    }

    void oversizedIndexKeepsTheLastCommittedSession()
    {
        QTemporaryDir directory;
        pdfeditor::SessionStore store(directory.path());
        QVERIFY(store.save({}, {}, {}).isEmpty());
        const auto path = directory.filePath("session.json");
        const auto digest = testdata::digest(path);
        QVERIFY(!store.save({}, {}, {directory.filePath(QString(1100000, 'x'))}).isEmpty());
        QCOMPARE(testdata::digest(path), digest);
        QVERIFY(store.read().error.isEmpty());
    }

    void refusesCorruptSnapshotsAndPreservesThemForReview()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("source.pdf");
        QVERIFY(testdata::writeVisualPdf(path));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const auto sessionPath = directory.filePath("session");
        pdfeditor::SessionStore store(sessionPath);
        QVERIFY(store.save({entry(source, true)}, {}, {path}).isEmpty());
        const auto index = store.read();
        QFile snapshot(QDir(sessionPath).filePath(index.entries[0].snapshot));
        QVERIFY(snapshot.open(QIODevice::Append));
        snapshot.write("corruption");
        snapshot.close();
        const auto restored = store.restore(index.entries);
        QVERIFY(!restored[0].document);
        QVERIFY(!restored[0].error.isEmpty());
        QVERIFY(store.save({}, index.entries, {path}).isEmpty());
        QCOMPARE(store.read().entries[0].snapshot, index.entries[0].snapshot);
        QVERIFY(QFileInfo::exists(snapshot.fileName()));
    }

    void rejectsTraversalAndNeverOverwritesAnInvalidIndex()
    {
        QTemporaryDir directory;
        const auto sessionPath = directory.filePath("session");
        pdfeditor::SessionStore store(sessionPath);
        QVERIFY(store.save({}, {}, {}).isEmpty());
        const auto indexPath = QDir(sessionPath).filePath("session.json");
        QFile file(indexPath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QJsonObject document{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"path", directory.filePath("original.pdf")},
            {"sourceHash", QString(64, 'a')}, {"snapshot", "../outside.pdf"}, {"snapshotHash", QString(64, 'b')}, {"page", 0}, {"zoom", 1}, {"fitWidth", true}};
        file.write(QJsonDocument(QJsonObject{{"version", 1}, {"documents", QJsonArray{document}}, {"recentFiles", QJsonArray{}}, {"cleanExit", false}}).toJson());
        file.close();
        const auto digest = testdata::digest(indexPath);
        QVERIFY(!store.read().error.isEmpty());
        QVERIFY(!store.save({}, {}, {}).isEmpty());
        QCOMPARE(testdata::digest(indexPath), digest);
    }

    void changedOrMissingOriginalRequiresSavingACopy()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("original.pdf");
        QVERIFY(testdata::writeVisualPdf(path));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const auto changed = source->rotatePage(0).document;
        QVERIFY(changed);
        pdfeditor::SessionStore store(directory.filePath("session"));
        QVERIFY(store.save({entry(changed, true)}, {}, {path}).isEmpty());
        const auto index = store.read();
        QVERIFY(testdata::writeVisualPdf(path, 180));
        const auto diskHash = testdata::digest(path);
        auto restored = store.restore(index.entries);
        QVERIFY2(restored[0].document, qPrintable(restored[0].error));
        QVERIFY(!restored[0].document->save(pdfeditor::PdfSaveMode::Original).document);
        QCOMPARE(testdata::digest(path), diskHash);
        QVERIFY(QFile::remove(path));
        restored = store.restore(index.entries);
        QVERIFY(restored[0].document);
        QVERIFY(!restored[0].savedDocument);
        QVERIFY(!restored[0].document->save(pdfeditor::PdfSaveMode::Original).document);
        QVERIFY(restored[0].document->save(pdfeditor::PdfSaveMode::NewCopy, directory.filePath("safe-copy.pdf")).document);
        QVERIFY(!QFileInfo::exists(path));
    }
};

QTEST_MAIN(SessionStoreTest)
#include "SessionStoreTest.moc"
