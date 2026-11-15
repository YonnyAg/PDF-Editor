#pragma once

#include "core/PdfDocument.h"

class QPrinter;
class QPagedPaintDevice;

namespace pdfeditor {

PdfOutputResult printDocument(const PdfDocument& document, qsizetype currentPage, QPrinter& printer);

struct PrintOptions
{
    qsizetype first;
    qsizetype last;
    int resolution;
    bool reverse = false;
    int copies = 1;
    bool collated = true;
};

PdfOutputResult paintPrintPages(const PdfDocument& document, QPagedPaintDevice& device, const PrintOptions& options);

} // namespace pdfeditor
