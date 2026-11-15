#include "DocumentPrint.h"

#include <QCoreApplication>
#include <QPainter>
#include <QPrinter>

#include <algorithm>

namespace pdfeditor {

namespace {
QString message(const char* text)
{
    return QCoreApplication::translate("DocumentPrint", text);
}
} // namespace

PdfOutputResult printDocument(const PdfDocument& document, qsizetype currentPage, QPrinter& printer)
{
    if (!printer.isValid()) { return {message("Selecciona una impresora válida."), {}}; }
    PrintOptions options{0, document.pageCount() - 1, printer.resolution()};
    if (printer.printRange() == QPrinter::CurrentPage) {
        options.first = options.last = currentPage;
    } else if (printer.printRange() == QPrinter::PageRange) {
        options.first = printer.fromPage() - 1;
        options.last = printer.toPage() - 1;
    } else if (printer.printRange() == QPrinter::Selection) {
        return {message("Selecciona todas las páginas, la página activa o un intervalo."), {}};
    }
    options.reverse = printer.pageOrder() == QPrinter::LastPageFirst;
    const bool nativeCopies = printer.supportsMultipleCopies();
    options.copies = nativeCopies ? 1 : std::max(1, printer.copyCount());
    options.collated = printer.collateCopies();
    if (!nativeCopies) { printer.setCopyCount(1); }
    printer.setFullPage(false);
    auto result = paintPrintPages(document, printer, options);
    if (result.error.isEmpty() && (printer.printerState() == QPrinter::Error || printer.printerState() == QPrinter::Aborted)) {
        result.error = message("La impresora no pudo completar el trabajo.");
    }
    return result;
}

PdfOutputResult paintPrintPages(const PdfDocument& document, QPagedPaintDevice& device, const PrintOptions& options)
{
    const int dpi = std::min(options.resolution, document.printResolutionLimit());
    if (dpi <= 0) { return {message("El PDF restringe la impresión o la resolución no es válida."), {}}; }
    if (options.first < 0 || options.last < options.first || options.last >= document.pageCount() || options.copies < 1) {
        return {message("Selecciona un intervalo de impresión válido."), {}};
    }
    QPainter painter;
    QStringList warnings;
    bool started = false;
    const auto printPage = [&](qsizetype pageIndex, int repeats) -> QString {
        const auto rendered = document.renderForOutput(pageIndex, dpi, PdfRenderTarget::Print);
        if (rendered.image.isNull()) {
            return message("No se pudo preparar la página %1 para imprimir. %2").arg(pageIndex + 1).arg(rendered.error);
        }
        if (!rendered.warnings.isEmpty() && warnings.isEmpty()) {
            warnings.append(message("La impresión puede omitir elementos de algunas páginas."));
        }
        for (int copy = 0; copy < repeats; ++copy) {
            if (!started) {
                if (!painter.begin(&device)) { return message("No se pudo iniciar la impresión."); }
                painter.setRenderHint(QPainter::SmoothPixmapTransform);
                started = true;
            } else if (!device.newPage()) {
                return message("La impresora rechazó una página del trabajo.");
            }
            const QSizeF paper = device.pageLayout().paintRectPixels(options.resolution).size();
            if (paper.isEmpty()) { return message("La impresora no tiene un área de impresión válida."); }
            const auto image = QSizeF(rendered.image.size()).scaled(paper, Qt::KeepAspectRatio);
            painter.drawImage(QRectF(QPointF((paper.width() - image.width()) / 2, (paper.height() - image.height()) / 2), image),
                rendered.image);
        }
        return {};
    };
    for (int copy = 0; copy < (options.collated ? options.copies : 1); ++copy) {
        for (qsizetype offset = 0; offset <= options.last - options.first; ++offset) {
            const auto page = options.reverse ? options.last - offset : options.first + offset;
            const auto error = printPage(page, options.collated ? 1 : options.copies);
            if (!error.isEmpty()) {
                if (auto* printer = dynamic_cast<QPrinter*>(&device); printer && started) { printer->abort(); }
                return {error, warnings};
            }
        }
    }
    if (!painter.end()) {
        return {message("La impresora no pudo completar el trabajo."), warnings};
    }
    return {{}, warnings};
}

} // namespace pdfeditor
