#include "core/PdfDocument.h"
#include "PdfTestData.h"
#include "VisualPdfData.h"

#include <QDir>
#include <QFontDatabase>
#include <QTemporaryDir>
#include <QTest>
#include <limits>

class AdvancedToolsTest final : public QObject
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

    void createsAndFillsNewFields_data()
    {
        QTest::addColumn<int>("rotation");
        for (int angle : {0, 90, 180, 270}) { QTest::newRow(qPrintable(QString::number(angle))) << angle; }
    }

    void createsAndFillsNewFields()
    {
        QFETCH(int, rotation);
        QTemporaryDir directory;
        const auto path = directory.filePath("form.pdf");
        QVERIFY(testdata::writeVisualPdf(path, rotation, 2));
        const auto digest = testdata::digest(path);
        auto document = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(document);
        using Type = pdfeditor::PdfFormFieldType;
        for (const auto type : {Type::Text, Type::CheckBox, Type::ComboBox}) {
            const auto name = QStringLiteral("Field%1").arg(int(type));
            const auto created = document->createFormField(0, QRectF(0.1, 0.1 + int(type) * 0.25, 0.7, 0.15), name, type,
                type == Type::ComboBox ? QStringList{QStringLiteral("Uno"), QStringLiteral("Dos")} : QStringList{});
            QVERIFY2(created.document, qPrintable(created.error));
            document = created.document;
        }
        QCOMPARE(document->pageFormFields(0).size(), 3);
        for (const auto& field : document->pageFormFields(0)) {
            QVERIFY2(field.editable, qPrintable(field.name));
            const QVariant value = field.type == Type::Text ? QVariant(QStringLiteral("Texto nuevo")) : field.type == Type::CheckBox ? QVariant(true) : QVariant(QStringLiteral("Dos"));
            const auto filled = document->setFormField(0, field.index, value);
            QVERIFY2(filled.document, qPrintable(filled.error));
            document = filled.document;
        }
        const auto rendered = document->renderPage(0);
        QVERIFY2(!rendered.image.isNull(), qPrintable(rendered.error));
        QVERIFY2(rendered.warnings.isEmpty(), qPrintable(rendered.warnings.join('\n')));
        const auto saved = document->save(pdfeditor::PdfSaveMode::NewCopy, directory.filePath("filled.pdf"));
        QVERIFY2(saved.document, qPrintable(saved.error));
        auto reopened = pdfeditor::PdfDocument::open(saved.document->filePath()).document;
        QVERIFY(reopened);
        QCOMPARE(reopened->renderPage(0).image, rendered.image);
        const auto fields = reopened->pageFormFields(0);
        QCOMPARE(fields.size(), 3);
        QCOMPARE(fields[0].value.toString(), QStringLiteral("Texto nuevo"));
        QVERIFY(fields[1].value.toBool());
        QCOMPARE(fields[2].value.toString(), QStringLiteral("Dos"));
        QCOMPARE(fields[2].options, QStringList({QStringLiteral("Uno"), QStringLiteral("Dos")}));
        const auto changed = reopened->setFormField(0, fields[2].index, QStringLiteral("Uno"));
        QVERIFY2(changed.document, qPrintable(changed.error));
        QVERIFY(changed.document->renderPage(0).image != rendered.image);
        QVERIFY(!document->createFormField(0, QRectF(0.1, 0.1, 0.3, 0.1), QStringLiteral("Field0"), Type::Text).document);
        QVERIFY(!document->createFormField(0, QRectF(0.1, 0.1, 0.3, 0.1), QStringLiteral("Another"), Type::ComboBox, {QStringLiteral("A"), QStringLiteral("A")}).document);
        QVERIFY(!document->setFormField(0, fields[2].index, QStringLiteral("No existe")).document);
        QCOMPARE(testdata::digest(path), digest);
    }

    void drawsAndDeletesShapes_data()
    {
        QTest::addColumn<int>("rotation");
        QTest::addColumn<int>("type");
        for (int rotation : {0, 90, 180, 270}) {
            for (int type = 0; type < 4; ++type) { QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(rotation).arg(type))) << rotation << type; }
        }
    }

    void drawsAndDeletesShapes()
    {
        QFETCH(int, rotation);
        QFETCH(int, type);
        QTemporaryDir directory;
        const auto path = directory.filePath("shape.pdf");
        QVERIFY(testdata::writeVisualPdf(path, rotation, 2, testdata::VisualPdfExtras::AnnotationsAndForm));
        const auto digest = testdata::digest(path);
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const auto before = source->renderPage(0).image;
        const QRectF area(0.1, 0.25, 0.65, 0.5);
        const auto added = source->addShape(0, area, static_cast<pdfeditor::PdfShapeType>(type), QColor(220, 0, 0), type < 2 ? QColor(255, 220, 100) : QColor{}, 3, QStringLiteral("Comentario"));
        QVERIFY2(added.document, qPrintable(added.error));
        const auto shapes = added.document->pageShapes(0);
        QCOMPARE(shapes.size(), 1);
        QCOMPARE(int(shapes[0].type), type);
        QVERIFY(shapes[0].editable);
        QVERIFY(qAbs(shapes[0].area.center().x() - area.center().x()) < 0.05);
        QVERIFY(qAbs(shapes[0].area.center().y() - area.center().y()) < 0.05);
        const auto rendered = added.document->renderPage(0);
        QVERIFY2(rendered.error.isEmpty(), qPrintable(rendered.error));
        QVERIFY2(rendered.warnings.isEmpty(), qPrintable(rendered.warnings.join('\n')));
        QVERIFY(rendered.image != before);
        QCOMPARE(added.document->pageNotes(0).size(), source->pageNotes(0).size());
        QCOMPARE(added.document->pageFormFields(0).size(), source->pageFormFields(0).size());
        const auto saved = added.document->save(pdfeditor::PdfSaveMode::NewCopy, directory.filePath("saved.pdf"));
        QVERIFY2(saved.document, qPrintable(saved.error));
        const auto reopened = pdfeditor::PdfDocument::open(saved.document->filePath()).document;
        QVERIFY(reopened);
        QCOMPARE(reopened->renderPage(0).image, rendered.image);
        const auto deleted = reopened->deleteShape(0, reopened->pageShapes(0)[0].index);
        QVERIFY2(deleted.document, qPrintable(deleted.error));
        QCOMPARE(deleted.document->renderPage(0).image, before);
        QVERIFY(deleted.document->pageShapes(0).isEmpty());
        QCOMPARE(testdata::digest(path), digest);
    }

    void addsExportsAndRemovesBinaryAttachments()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("source.pdf");
        QVERIFY(testdata::writeReorderPdf(path));
        const auto digest = testdata::digest(path);
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const QByteArray bytes("binary\0data\xff\x01", 13);
        const auto result = source->addAttachment(QStringLiteral("informe-á.bin"), bytes, QStringLiteral("Descripción"));
        QVERIFY2(result.document, qPrintable(result.error));
        const auto list = result.document->attachments();
        QCOMPARE(list.size(), 1);
        QCOMPARE(list[0].name, QStringLiteral("informe-á.bin"));
        QCOMPARE(list[0].size, bytes.size());
        QCOMPARE(list[0].description, QStringLiteral("Descripción"));
        const auto exported = directory.filePath("exported.bin");
        QVERIFY2(result.document->exportAttachment(list[0].key, exported).error.isEmpty(), "Export failed");
        QFile file(exported);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), bytes);
        file.close();
        const auto exportedHash = testdata::digest(exported);
        QVERIFY(!result.document->exportAttachment(list[0].key, exported).error.isEmpty());
        QCOMPARE(testdata::digest(exported), exportedHash);
        const auto saved = result.document->save(pdfeditor::PdfSaveMode::NewCopy, directory.filePath("with-attachment.pdf"));
        QVERIFY2(saved.document, qPrintable(saved.error));
        const auto reopened = pdfeditor::PdfDocument::open(saved.document->filePath()).document;
        QVERIFY(reopened);
        QCOMPARE(reopened->attachments().size(), 1);
        const auto removed = reopened->deleteAttachment(reopened->attachments()[0].key);
        QVERIFY2(removed.document, qPrintable(removed.error));
        QVERIFY(removed.document->attachments().isEmpty());
        QCOMPARE(removed.document->renderPage(0).image, source->renderPage(0).image);
        QVERIFY(!result.document->addAttachment(list[0].name, bytes).document);
        QVERIFY(!source->addAttachment(QStringLiteral("../bad.bin"), bytes).document);
        QCOMPARE(testdata::digest(path), digest);
    }

    void preservesOtherNameTreesAndRejectsMalformedTrees()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("names.pdf");
        QVERIFY(testdata::writeSyntheticPdf(path, {
            "<< /Type /Catalog /Pages 2 0 R /Outlines 7 0 R /Names << /Dests << /Names [(page) [3 0 R /Fit]] >> /EmbeddedFiles << /Kids [4 0 R] >> >> >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>", "<< /Type /Page /Parent 2 0 R /MediaBox [100 200 300 500] >>",
            "<< /Names [(old.txt) 5 0 R] >>", "<< /Type /Filespec /F (old.txt) /EF << /F 6 0 R >> >>",
            "<< /Type /EmbeddedFile /Length 3 /Params << /Size 3 >> >>\nstream\nold\nendstream",
            "<< /Type /Outlines /First 8 0 R /Last 8 0 R /Count 1 >>", "<< /Title (Named) /Parent 7 0 R /Dest (page) >>"
        }));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const auto added = source->addAttachment(QStringLiteral("new.txt"), "new");
        QVERIFY2(added.document, qPrintable(added.error));
        QCOMPARE(added.document->attachments().size(), 2);
        const auto bookmark = added.document->changeBookmark(pdfeditor::PdfBookmarkChange::Add, {}, QStringLiteral("Page"), 0);
        QVERIFY(bookmark.document);
        const auto saved = bookmark.document->save(pdfeditor::PdfSaveMode::NewCopy, directory.filePath("names-saved.pdf"));
        QVERIFY2(saved.document, qPrintable(saved.error));
        QCOMPARE(saved.document->bookmarks().front().page, 0);
        const auto malformedPath = directory.filePath("malformed.pdf");
        QVERIFY(testdata::writeSyntheticPdf(malformedPath, {
            "<< /Type /Catalog /Pages 2 0 R /Names << /EmbeddedFiles 4 0 R >> >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>", "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] >>",
            "<< /Names [(duplicate) 5 0 R (duplicate) 5 0 R] >>", "<< /Type /Filespec /F (missing.txt) >>"
        }));
        const auto malformed = pdfeditor::PdfDocument::open(malformedPath);
        QVERIFY(!malformed.document);
        QVERIFY(!malformed.error.isEmpty());
    }

    void optimizesACopyAndPreservesPendingContent()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("bloated.pdf");
        const QByteArray unused(300000, 'x');
        QVERIFY(testdata::writeSyntheticPdf(path, {
            "<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] >>", "(" + unused + ")"
        }));
        const auto digest = testdata::digest(path);
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        auto changed = source->rotatePage(0).document;
        QVERIFY(changed);
        const auto attached = changed->addAttachment(QStringLiteral("data.bin"), QByteArray(500, 'a'));
        QVERIFY2(attached.document, qPrintable(attached.error));
        changed = attached.document;
        const auto output = directory.filePath("optimized.pdf");
        const auto result = changed->optimizedCopy(output);
        QVERIFY2(result.document, qPrintable(result.error));
        QVERIFY(QFileInfo(output).size() < QFileInfo(path).size());
        const auto reopened = pdfeditor::PdfDocument::open(output).document;
        QVERIFY(reopened);
        QCOMPARE(reopened->pageSize(0), changed->pageSize(0));
        QCOMPARE(reopened->renderPage(0).image, changed->renderPage(0).image);
        QCOMPARE(reopened->attachments().size(), 1);
        QVERIFY(!changed->optimizedCopy(output).document);
        QCOMPARE(testdata::digest(path), digest);
        QVERIFY(!source->addShape(0, QRectF(0, 0, 1, 1), pdfeditor::PdfShapeType::Rectangle, Qt::red, {}, std::numeric_limits<double>::quiet_NaN()).document);
    }

    void protectedDocumentsRejectNewEdits()
    {
        QTemporaryDir directory;
        for (const auto kind : {testdata::VisualPdfExtras::XfaForm, testdata::VisualPdfExtras::SignatureMarker}) {
            const auto path = directory.filePath(QString::number(int(kind)) + ".pdf");
            QVERIFY(testdata::writeVisualPdf(path, 0, 1, kind));
            const auto source = pdfeditor::PdfDocument::open(path).document;
            QVERIFY(source);
            QVERIFY(!source->addAttachment(QStringLiteral("data.txt"), "no").document);
            QVERIFY(!source->optimizedCopy(directory.filePath(QString::number(int(kind)) + "-out.pdf")).document);
            QVERIFY(!source->createFormField(0, QRectF(0.1, 0.1, 0.4, 0.2), QStringLiteral("New"), pdfeditor::PdfFormFieldType::Text).document);
            QVERIFY(!source->addShape(0, QRectF(0.1, 0.1, 0.4, 0.2), pdfeditor::PdfShapeType::Rectangle, Qt::red).document);
        }
    }

    void choiceFieldsPreserveExportValuesAndLabels()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("paired-options.pdf");
        QVERIFY(testdata::writeSyntheticPdf(path, {
            "<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [4 0 R] >> >>",
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>", "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] /Annots [4 0 R] >>",
            "<< /Type /Annot /Subtype /Widget /P 3 0 R /Rect [20 100 180 150] /FT /Ch /T (Choice) /Ff 131072 /V (A) /Opt [[(A) (Etiqueta A)] [(B) (Etiqueta B)]] /DA (/Helv 12 Tf 0 g) >>"
        }));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const auto fields = source->pageFormFields(0);
        QCOMPARE(fields.size(), 1);
        QCOMPARE(fields[0].options, QStringList({QStringLiteral("Etiqueta A"), QStringLiteral("Etiqueta B")}));
        QCOMPARE(fields[0].exportValues, QStringList({QStringLiteral("A"), QStringLiteral("B")}));
        const auto changed = source->setFormField(0, fields[0].index, QStringLiteral("B"));
        QVERIFY2(changed.document, qPrintable(changed.error));
        QCOMPARE(changed.document->pageFormFields(0)[0].value.toString(), QStringLiteral("B"));
        const auto saved = changed.document->save(pdfeditor::PdfSaveMode::NewCopy, directory.filePath("paired-filled.pdf"));
        QVERIFY2(saved.document, qPrintable(saved.error));
        QCOMPARE(saved.document->pageFormFields(0)[0].value.toString(), QStringLiteral("B"));
        QVERIFY(!source->setFormField(0, fields[0].index, QStringLiteral("Etiqueta B")).document);
    }

    void optimizationKeepsRichPageContentAndFormResources()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("rich.pdf");
        QVERIFY(testdata::writeVisualPdf(path, 90, 2, testdata::VisualPdfExtras::AnnotationsAndForm));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const auto created = source->createFormField(0, QRectF(0.1, 0.1, 0.5, 0.15), QStringLiteral("Extra"), pdfeditor::PdfFormFieldType::ComboBox,
            {QStringLiteral("España"), QStringLiteral("Chile")});
        QVERIFY2(created.document, qPrintable(created.error));
        const auto shaped = created.document->addShape(0, QRectF(0.3, 0.4, 0.4, 0.3), pdfeditor::PdfShapeType::Ellipse, Qt::red);
        QVERIFY2(shaped.document, qPrintable(shaped.error));
        const auto linked = shaped.document->addLink(0, QRectF(0.1, 0.8, 0.5, 0.1), 0);
        QVERIFY2(linked.document, qPrintable(linked.error));
        const auto result = linked.document->optimizedCopy(directory.filePath("rich-optimized.pdf"));
        QVERIFY2(result.document, qPrintable(result.error));
        QCOMPARE(result.document->renderPage(0).image, linked.document->renderPage(0).image);
        QCOMPARE(result.document->pageNotes(0).size(), source->pageNotes(0).size());
        QCOMPARE(result.document->pageFormFields(0).size(), 3);
        QCOMPARE(result.document->pageShapes(0).size(), 1);
        QCOMPARE(result.document->pageLinks(0)[0].targetPage, 0);
        QCOMPARE(result.document->findText(QStringLiteral("PDF Editor")).matches.size(), 1);
        const auto fields = result.document->pageFormFields(0);
        const auto filled = result.document->setFormField(0, fields.back().index, QStringLiteral("Chile"));
        QVERIFY2(filled.document, qPrintable(filled.error));
    }

    void freehandInkSurvivesSaveAndDeletion_data() { createsAndFillsNewFields_data(); }

    void freehandInkSurvivesSaveAndDeletion()
    {
        QFETCH(int, rotation);
        QTemporaryDir directory;
        const auto path = directory.filePath("ink.pdf");
        QVERIFY(testdata::writeVisualPdf(path, rotation, 2, testdata::VisualPdfExtras::AnnotationsAndForm));
        const auto source = pdfeditor::PdfDocument::open(path).document;
        QVERIFY(source);
        const auto before = source->renderPage(0).image;
        const QList<QPolygonF> strokes{
            QPolygonF{QPointF(0.1, 0.2), QPointF(0.3, 0.5), QPointF(0.55, 0.25), QPointF(0.7, 0.6)},
            QPolygonF{QPointF(0.1, 0.7), QPointF(0.7, 0.7)}
        };
        const auto added = source->addInk(0, strokes, QColor(200, 0, 150), 4);
        QVERIFY2(added.document, qPrintable(added.error));
        const auto shapes = added.document->pageShapes(0);
        QCOMPARE(shapes.size(), 1);
        QCOMPARE(shapes[0].type, pdfeditor::PdfShapeType::Freehand);
        const auto render = added.document->renderPage(0);
        QVERIFY2(render.warnings.isEmpty(), qPrintable(render.warnings.join('\n')));
        QVERIFY(render.image != before);
        QCOMPARE(added.document->pageFormFields(0).size(), source->pageFormFields(0).size());
        const auto saved = added.document->save(pdfeditor::PdfSaveMode::NewCopy, directory.filePath("drawn.pdf"));
        QVERIFY2(saved.document, qPrintable(saved.error));
        QCOMPARE(saved.document->renderPage(0).image, render.image);
        const auto removed = saved.document->deleteShape(0, saved.document->pageShapes(0)[0].index);
        QVERIFY2(removed.document, qPrintable(removed.error));
        QCOMPARE(removed.document->renderPage(0).image, before);
        QVERIFY(!source->addInk(0, {}, Qt::red).document);
        QVERIFY(!source->addInk(0, {QPolygonF{QPointF(0.1, 0.1), QPointF(0.1, 0.1)}}, Qt::red).document);
        QVERIFY(!source->addInk(0, {QPolygonF{QPointF(-0.1, 0.1), QPointF(0.2, 0.3)}}, Qt::red).document);
        auto excessive = strokes;
        while (excessive.size() <= 32) { excessive.append(strokes.front()); }
        QVERIFY(!source->addInk(0, excessive, Qt::red).document);
    }
};

QTEST_MAIN(AdvancedToolsTest)
#include "AdvancedToolsTest.moc"
