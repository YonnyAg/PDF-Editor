#include "core/PdfDocument.h"
#include "VisualPdfData.h"

#include <QCoreApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

class ReaderSafetyTest final : public QObject
{
    Q_OBJECT
public:
    static void initMain()
    {
#ifdef Q_OS_WIN
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    }

private:
    static void verifyProbe(const QString& path, bool expected)
    {
        QProcess process;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("PDF_EDITOR_READER_PROBE"), path);
        environment.insert(QStringLiteral("PDF_EDITOR_READER_EXPECTED"), expected ? QStringLiteral("valid") : QStringLiteral("invalid"));
        environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        process.setProcessEnvironment(environment);
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(QCoreApplication::applicationFilePath(), {QStringLiteral("probeFile"), QStringLiteral("-o"), QStringLiteral("-,txt")});
        QVERIFY(process.waitForStarted(5000));
        const bool finished = process.waitForFinished(10000);
        if (!finished) { process.kill(); process.waitForFinished(2000); }
        QVERIFY2(finished, "The PDF reader exceeded the process timeout");
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QVERIFY2(process.exitCode() == 0, process.readAll().constData());
    }

private slots:
    void probeFile()
    {
        const auto path = qEnvironmentVariable("PDF_EDITOR_READER_PROBE");
        if (path.isEmpty()) { return; }
        const auto opened = pdfeditor::PdfDocument::open(path);
        if (qEnvironmentVariable("PDF_EDITOR_READER_EXPECTED") == QStringLiteral("valid")) { QVERIFY2(opened.document, qPrintable(opened.error)); }
        else { QVERIFY(!opened.document); QVERIFY(!opened.error.isEmpty()); }
    }

    void rejectsCyclesAndRepeatedNodes_data()
    {
        QTest::addColumn<QString>("category");
        QTest::addColumn<int>("mode");
        for (const auto& category : {QStringLiteral("EmbeddedFiles"), QStringLiteral("Dests"), QStringLiteral("AP"), QStringLiteral("JavaScript"), QStringLiteral("Pages"),
                 QStringLiteral("Templates"), QStringLiteral("IDS"), QStringLiteral("URLS"), QStringLiteral("AlternatePresentations"), QStringLiteral("Renditions")}) {
            for (int mode : {0, 1, 2}) { QTest::newRow(qPrintable(category + QString::number(mode))) << category << mode; }
        }
    }

    void rejectsCyclesAndRepeatedNodes()
    {
        QFETCH(QString, category);
        QFETCH(int, mode);
        QTemporaryDir directory;
        const auto path = directory.filePath("cycle.pdf");
        const auto tree = mode == 0 ? QByteArray("<< /Kids [4 0 R] >>") : mode == 1 ? QByteArray("<< /Kids [5 0 R] >>") : QByteArray("<< /Kids [5 0 R 5 0 R] >>");
        const auto child = mode == 1 ? QByteArray("<< /Kids [4 0 R] >>") : QByteArray("<< /Names [] >>");
        QVERIFY(testdata::writeSyntheticPdf(path, {
            "<< /Type /Catalog /Pages 2 0 R /Names << /" + category.toLatin1() + " 4 0 R >> >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>", "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] >>", tree, child
        }));
        verifyProbe(path, false);
    }

    void respectsDepthAndEntryLimits_data()
    {
        QTest::addColumn<int>("depth");
        QTest::newRow("root-leaf") << 0;
        QTest::newRow("legal-depth-64") << 64;
        QTest::newRow("excess-depth-65") << 65;
        QTest::newRow("very-deep") << 1000;
    }

    void respectsDepthAndEntryLimits()
    {
        QFETCH(int, depth);
        QTemporaryDir directory;
        const auto path = directory.filePath("depth.pdf");
        QList<QByteArray> objects{
            "<< /Type /Catalog /Pages 2 0 R /Names << /EmbeddedFiles 4 0 R >> >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>", "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] >>"
        };
        for (int i = 0; i < depth; ++i) { objects.append("<< /Kids [" + QByteArray::number(i + 5) + " 0 R] >>"); }
        objects.append("<< /Names [(file) << /Type /Filespec /F (file.txt) >>] >>");
        QVERIFY(testdata::writeSyntheticPdf(path, objects));
        verifyProbe(path, depth <= 64);
    }

    void rejectsExcessiveWorkAndIncompletePairs()
    {
        QTemporaryDir directory;
        for (int mode : {0, 1, 2}) {
            const auto path = directory.filePath(QString::number(mode) + ".pdf");
            QByteArray tree;
            if (mode == 0) { tree = "<< /Kids ["; for (int i = 0; i < 100001; ++i) { tree += "null "; } tree += "] >>"; }
            else if (mode == 1) { tree = "<< /Names ["; for (int i = 0; i < 100001; ++i) { tree += "(" + QByteArray::number(i) + ") null "; } tree += "] >>"; }
            else { tree = "<< /Names [(missing-value)] >>"; }
            QVERIFY(testdata::writeSyntheticPdf(path, {
                "<< /Type /Catalog /Pages 2 0 R /Names << /EmbeddedFiles 4 0 R >> >>",
                "<< /Type /Pages /Kids [3 0 R] /Count 1 >>", "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] >>", tree
            }));
            verifyProbe(path, false);
        }
    }
};

QTEST_GUILESS_MAIN(ReaderSafetyTest)
#include "ReaderSafetyTest.moc"
