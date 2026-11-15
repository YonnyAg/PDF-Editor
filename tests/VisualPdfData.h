#pragma once

#include <QFile>
#include <QList>

namespace testdata {

enum class VisualPdfExtras { None, AnnotationsAndForm, MissingAppearance, XfaForm, SignatureMarker };

inline bool writeSyntheticPdf(const QString& path, const QList<QByteArray>& objects)
{
    QByteArray bytes = "%PDF-1.7\n";
    QList<qsizetype> offsets{0};
    for (qsizetype index = 0; index < objects.size(); ++index) {
        offsets.append(bytes.size());
        bytes += QByteArray::number(index + 1) + " 0 obj\n" + objects[index] + "\nendobj\n";
    }
    const auto xref = bytes.size();
    bytes += "xref\n0 " + QByteArray::number(offsets.size()) + "\n0000000000 65535 f \n";
    for (qsizetype index = 1; index < offsets.size(); ++index) {
        bytes += QByteArray::number(offsets[index]).rightJustified(10, '0') + " 00000 n \n";
    }
    bytes += "trailer\n<< /Size " + QByteArray::number(offsets.size()) + " /Root 1 0 R >>\nstartxref\n" +
        QByteArray::number(xref) + "\n%%EOF\n";
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

// An independent, synthetic PDF: off-origin crop, two colored corners, text and a blue image.
inline bool writeVisualPdf(const QString& path, int rotation = 0, int userUnit = 1,
    VisualPdfExtras extras = VisualPdfExtras::None)
{
    QByteArray form;
    QByteArray annotations;
    switch (extras) {
    case VisualPdfExtras::AnnotationsAndForm:
        form = "/AcroForm << /Fields [9 0 R 13 0 R] /DA (/F1 10 Tf 0 g) /DR << /Font << /F1 5 0 R >> >> >> ";
        annotations = "/Annots [7 0 R 8 0 R 9 0 R 11 0 R 12 0 R 13 0 R] ";
        break;
    case VisualPdfExtras::MissingAppearance:
        form = "/AcroForm << /Fields [7 0 R] /NeedAppearances true /DA (/F1 10 Tf 0 g) /DR << /Font << /F1 5 0 R >> >> >> ";
        annotations = "/Annots [7 0 R] ";
        break;
    case VisualPdfExtras::XfaForm:
        form = "/AcroForm << /Fields [] /XFA 7 0 R >> ";
        break;
    case VisualPdfExtras::SignatureMarker:
        form = "/Perms << /DocMDP 7 0 R >> ";
        break;
    case VisualPdfExtras::None:
        break;
    }
    const QByteArray commands = "1 0 0 rg 20 210 50 50 re f\n"
        "0 1 0 rg 130 40 50 50 re f\n"
        "0 0 0 rg BT /F1 16 Tf 40 100 Td (PDF Editor) Tj ET\n"
        "q 40 0 0 40 80 140 cm /Im1 Do Q\n";
    QList<QByteArray> objects{
        "<< /Type /Catalog /Pages 2 0 R " + form + ">>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] /CropBox [20 40 180 260] /Rotate " +
            QByteArray::number(rotation) + " /UserUnit " + QByteArray::number(userUnit) +
            " /Resources << /Font << /F1 5 0 R >> /XObject << /Im1 6 0 R >> >> /Contents 4 0 R " +
            annotations + ">>",
        "<< /Length " + QByteArray::number(commands.size()) + " >>\nstream\n" + commands + "endstream",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        "<< /Type /XObject /Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /ASCIIHexDecode /Length 7 >>\nstream\n0000FF>\nendstream"
    };
    const auto stream = [](const QByteArray& dictionary, const QByteArray& contents) {
        return "<< " + dictionary + " /Length " + QByteArray::number(contents.size()) + " >>\nstream\n" +
            contents + "\nendstream";
    };
    if (extras == VisualPdfExtras::AnnotationsAndForm) {
        objects.append("<< /Type /Annot /Subtype /Text /Rect [36 153 56 173] /Name /Note /Contents (Synthetic test note) >>");
        objects.append("<< /Type /Annot /Subtype /Highlight /Rect [75 222 155 240] "
            "/QuadPoints [75 240 155 240 75 222 155 222] /C [1 1 0] /CA 0.6 >>");
        objects.append("<< /Type /Annot /Subtype /Widget /P 3 0 R /Rect [76 56 119 82] /FT /Tx /T (Name) "
            "/V (Ana) /AP << /N 10 0 R >> >>");
        objects.append(stream("/Type /XObject /Subtype /Form /BBox [0 0 43 26] /Resources << /Font << /F1 5 0 R >> >>",
            "0.7 0.9 1 rg 0 0 43 26 re f\n0 0 0 rg BT /F1 12 Tf 5 8 Td (Ana) Tj ET"));
        objects.append("<< /Type /Annot /Subtype /Square /Rect [143 184 172 208] /F 2 /IC [1 0 1] /C [1 0 1] >>");
        objects.append("<< /Type /Annot /Subtype /Square /Rect [143 153 172 176] /F 32 /IC [1 0 1] /C [1 0 1] >>");
        objects.append("<< /Type /Annot /Subtype /Widget /P 3 0 R /Rect [44 59 61 76] /FT /Btn /T (Agreed) "
            "/V /Yes /AS /Yes /AP << /N << /Yes 14 0 R /Off 15 0 R >> >> >>");
        objects.append(stream("/Type /XObject /Subtype /Form /BBox [0 0 17 17] /Resources << >>",
            "0 0.5 0 rg 0 0 17 17 re f\n1 1 1 RG 2 w 3 8 m 7 4 l 14 13 l S"));
        objects.append(stream("/Type /XObject /Subtype /Form /BBox [0 0 17 17] /Resources << >>",
            "0 0 0 RG 0.5 0.5 16 16 re S"));
    } else if (extras == VisualPdfExtras::MissingAppearance) {
        objects.append("<< /Type /Annot /Subtype /Widget /P 3 0 R /Rect [76 56 119 82] /FT /Tx /T (Name) /V (Ana) >>");
    } else if (extras == VisualPdfExtras::XfaForm) {
        objects.append(stream({}, "<xdp:xdp xmlns:xdp=\"http://ns.adobe.com/xdp/\"/>"));
    } else if (extras == VisualPdfExtras::SignatureMarker) {
        // The fixture marks a signature dictionary without using a certificate.
        objects.append("<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached "
            "/ByteRange [0 1 2 1] /Contents <0000> >>");
    }
    return writeSyntheticPdf(path, objects);
}

inline bool writeOutputPdf(const QString& path)
{
    const auto content = [](const QByteArray& color, const QByteArray& text) {
        const QByteArray commands = color + " rg 0 0 144 216 re f\n0 0 0 rg BT /F1 14 Tf 20 160 Td (" + text + ") Tj ET\n";
        return "<< /Length " + QByteArray::number(commands.size()) + " >>\nstream\n" + commands + "endstream";
    };
    return writeSyntheticPdf(path, {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R] /Count 3 /MediaBox [0 0 144 216] /Resources << /Font << /F1 9 0 R >> >> >>",
        "<< /Type /Page /Parent 2 0 R /Contents 6 0 R >>",
        "<< /Type /Page /Parent 2 0 R /Contents 7 0 R /Rotate 90 >>",
        "<< /Type /Page /Parent 2 0 R /Contents 8 0 R >>",
        content("1 0 0", "Informe"),
        content("0 1 0", "Resumen"),
        content("0 0 1", "Informe"),
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"
    });
}

inline bool writeReorderPdf(const QString& path, bool nested = true)
{
    const QByteArray resources = " /Resources << /Font << /F1 12 0 R >> >> ";
    const QByteArray firstAttrs = " /MediaBox [0 0 200 300] /CropBox [20 30 180 270] /Rotate 0" + resources;
    const QByteArray secondAttrs = " /CropBox [10 15 130 205]" + resources;
    const QByteArray firstParent = nested ? QByteArray("6 0 R ") : "2 0 R " + firstAttrs;
    const QByteArray secondParent = nested ? QByteArray("7 0 R ") : "2 0 R " + secondAttrs;
    const QByteArray kids = nested ? "[6 0 R 7 0 R]" : "[3 0 R 4 0 R 5 0 R]";
    const auto content = [](const QByteArray& color, const QByteArray& label) {
        const QByteArray commands = color + " rg 25 50 90 110 re f\n0 0 0 rg BT /F1 20 Tf 36 90 Td (" + label + ") Tj ET\n";
        return "<< /Length " + QByteArray::number(commands.size()) + " >>\nstream\n" + commands + "endstream";
    };
    return writeSyntheticPdf(path, {
        "<< /Type /Catalog /Pages 2 0 R /Outlines 13 0 R >>",
        "<< /Type /Pages /Kids " + kids + " /Count 3 /MediaBox [0 0 140 220] /Rotate 90 >>",
        "<< /Type /Page /Parent " + firstParent + " /Contents 8 0 R >>",
        "<< /Type /Page /Parent " + secondParent + " /Contents 9 0 R /Annots [11 0 R] >>",
        "<< /Type /Page /Parent " + secondParent + " /Contents 10 0 R /UserUnit 2 >>",
        "<< /Type /Pages /Parent 2 0 R /Kids [3 0 R] /Count 1 " + firstAttrs + ">>",
        "<< /Type /Pages /Parent 2 0 R /Kids [4 0 R 5 0 R] /Count 2 " + secondAttrs + ">>",
        content("1 0.55 0.45", "A"),
        content("0.4 0.85 0.55", "B"),
        content("0.4 0.7 1", "C"),
        "<< /Type /Annot /Subtype /Text /P 4 0 R /Rect [25 164 45 184] /Contents (Note for B) >>",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        "<< /Type /Outlines /First 14 0 R /Last 14 0 R /Count 1 >>",
        "<< /Title (Page B) /Parent 13 0 R /Dest [4 0 R /Fit] >>"
    });
}

} // namespace testdata
