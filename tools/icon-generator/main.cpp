#include <QGuiApplication>
#include <QSvgRenderer>
#include <QPainter>
#include <QFile>
#include <QDir>
#include <QBuffer>
#include <QDataStream>
#include <QVector>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    const bool store = argc == 4 && QString::fromLocal8Bit(argv[3]) == QStringLiteral("--store");
    if (argc != 3 && !store) return 1;
    QSvgRenderer svg(QString::fromLocal8Bit(argv[1]));
    QDir output(QString::fromLocal8Bit(argv[2]));
    if (!svg.isValid() || !output.mkpath(QStringLiteral("."))) return 2;
    const QList<int> sizes = store ? QList<int>{44, 50, 150, 310, 620} : QList<int>{16, 20, 24, 32, 40, 48, 64, 96, 128, 256};
    QVector<QByteArray> images;
    for (const int size : sizes) {
        QImage image(size, size, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        svg.render(&painter);
        painter.end();
        if (!image.save(output.filePath(QStringLiteral("pdf-editor-%1.png").arg(size)))) return 3;
        QByteArray encoded;
        QBuffer buffer(&encoded);
        buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG")) return 4;
        images.append(encoded);
    }
    if (store) return 0;
    QFile icon(output.filePath(QStringLiteral("pdf-editor.ico")));
    if (!icon.open(QIODevice::WriteOnly)) return 5;
    QDataStream data(&icon);
    data.setByteOrder(QDataStream::LittleEndian);
    data << quint16(0) << quint16(1) << quint16(sizes.size());
    quint32 offset = 6 + 16 * quint32(sizes.size());
    for (qsizetype i = 0; i < sizes.size(); ++i) {
        const auto edge = quint8(sizes[i] == 256 ? 0 : sizes[i]);
        data << edge << edge << quint8(0) << quint8(0) << quint16(1) << quint16(32)
             << quint32(images[i].size()) << offset;
        offset += quint32(images[i].size());
    }
    for (const auto& bytes : images) data.writeRawData(bytes.constData(), int(bytes.size()));
    return data.status() == QDataStream::Ok ? 0 : 6;
}
