// Render the vector source at every icon size, preserving transparent corners.
#include <QGuiApplication>
#include <QImage>
#include <QBuffer>
#include <QDataStream>
#include <QSaveFile>
#include <QDir>
#include <QPainter>
#include <QSvgRenderer>
#include <iostream>

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 3) return 1;
    QSvgRenderer renderer(args[1]);
    if (!renderer.isValid()) {
        std::cerr << "Expected a valid SVG icon source\n";
        return 2;
    }
    if (!QDir().mkpath(args[2])) return 3;
    const auto render = [&renderer](int size) {
        QImage image(size * 4, size * 4, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        renderer.render(&painter);
        painter.end();
        return image.scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    };
    if (!render(512).save(args[2] + "/codexsync.png")) return 4;
    if (!render(120).save(args[2] + "/codexsync-google-120.png")) return 4;
    QList<QByteArray> frames;
    const QList<int> sizes = {16, 32, 48, 64, 128, 256};
    for (const int size : sizes) {
        const auto image = render(size);
        if (image.pixelColor(0, 0).alpha() != 0) return 5;
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG")) return 6;
        frames.push_back(png);
        if (!image.save(args[2] + QStringLiteral("/codexsync-%1.png").arg(size))) return 7;
    }
    QByteArray ico;
    QDataStream stream(&ico, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint16(0) << quint16(1) << quint16(sizes.size());
    quint32 offset = 6 + 16 * static_cast<quint32>(sizes.size());
    for (int i = 0; i < sizes.size(); ++i) {
        const int size = sizes[i];
        stream << quint8(size == 256 ? 0 : size) << quint8(size == 256 ? 0 : size)
               << quint8(0) << quint8(0) << quint16(1) << quint16(32)
               << quint32(frames[i].size()) << offset;
        offset += static_cast<quint32>(frames[i].size());
    }
    for (const auto& frame : frames) ico += frame;
    QSaveFile file(args[2] + "/codexsync.ico");
    if (!file.open(QIODevice::WriteOnly) || file.write(ico) != ico.size() || !file.commit()) return 8;
    std::cout << "ICON_PACKAGING_PASS: flat SVG, transparent PNG, 6 ICO resolutions\n";
    return 0;
}
