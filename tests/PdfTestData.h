#pragma once

#include <QCryptographicHash>
#include <QFile>
#include <QPainter>
#include <QPdfWriter>

namespace testdata {

// Generate synthetic documents with Qt, independently of the reader being tested.
inline bool writePdf(const QString& path, int pages = 2, bool alternateOrientation = false)
{
    QPdfWriter writer(path);
    writer.setTitle(QStringLiteral("Documento de prueba"));
    writer.setCreator(QStringLiteral("PDF Editor tests"));
    QPainter painter(&writer);
    if (!painter.isActive()) { return false; }
    for (int page = 0; page < pages; ++page) {
        if (page > 0) {
            if (alternateOrientation) {
                writer.setPageOrientation(page % 2 == 0 ? QPageLayout::Portrait : QPageLayout::Landscape);
            }
            if (!writer.newPage()) { return false; }
        }
        painter.drawText(100, 200, QStringLiteral("Página de prueba %1").arg(page + 1));
    }
    return painter.end();
}

inline QByteArray digest(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { return {}; }
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
}

} // namespace testdata
