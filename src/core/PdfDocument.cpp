#include "PdfDocument.h"

#include <pdfannotation.h>
#include <pdfdocumentbuilder.h>
#include <pdfdocumentreader.h>
#include <pdfdocumentwriter.h>
#include <pdfcms.h>
#include <pdfpagecontenteditorprocessor.h>
#include <pdfpagecontenteditorcontentstreambuilder.h>
#include <pdfpainterutils.h>
#include <pdfcertificatemanager.h>
#include <pdfsignaturehandler.h>
#include <pdfconstants.h>
#include <pdffont.h>
#include <pdfform.h>
#include <pdfoptionalcontent.h>
#include <pdfoptimizer.h>
#include <pdfobjectutils.h>
#include <pdfoutline.h>
#include <pdfrenderer.h>
#include <pdftextlayout.h>
#include <pdftextlayoutgenerator.h>

#include <QCoreApplication>
#include <QBuffer>
#include <QDateTime>
#include <QUuid>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterStateGuard>
#include <QPdfWriter>
#include <QPageSize>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QUrl>
#include <QDomDocument>
#include <pdffile.h>

#include <algorithm>
#include <exception>
#include <map>
#include <limits>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>
#include <cmath>

namespace pdfeditor {

namespace {
QString message(const char* text)
{
    return QCoreApplication::translate("PdfDocument", text);
}

pdf::PDFObject dictionaryOf(std::initializer_list<std::pair<QByteArray, pdf::PDFObject>> entries)
{
    auto dictionary = std::make_shared<pdf::PDFDictionary>();
    for (const auto& [key, value] : entries) { dictionary->setEntry(pdf::PDFInplaceOrMemoryString(key), pdf::PDFObject(value)); }
    return pdf::PDFObject::createDictionary(std::move(dictionary));
}

template<typename T> pdf::PDFObject objectOf(const T& value)
{
    pdf::PDFObjectFactory factory;
    factory << value;
    return factory.takeObject();
}

pdf::PDFObjectReference addStream(pdf::PDFDocumentBuilder& builder, QByteArray bytes, const pdf::PDFObject& attributes = {})
{
    auto dictionary = attributes.isDictionary() ? *attributes.getDictionary() : pdf::PDFDictionary();
    dictionary.setEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(bytes.size()));
    return builder.addObject(pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(std::move(dictionary), std::move(bytes))));
}

QTransform pageToCanvas(const pdf::PDFPage& page, QSizeF canvasSize);
QRectF visibleAreaToPage(const pdf::PDFPage& page, const QRectF& area)
{
    const auto size = page.getRotatedCropBox().size();
    bool okay = false;
    const auto inverse = pageToCanvas(page, size).inverted(&okay);
    if (!okay) { throw std::runtime_error("Invalid page transform"); }
    return inverse.mapRect(QRectF(area.x() * size.width(), area.y() * size.height(), area.width() * size.width(), area.height() * size.height()));
}

bool validArea(const QRectF& area)
{
    return area.isValid() && std::isfinite(area.x()) && std::isfinite(area.y()) && std::isfinite(area.width()) &&
        std::isfinite(area.height()) && QRectF(0, 0, 1, 1).contains(area);
}

using EmbeddedEntries = std::map<QByteArray, pdf::PDFObject>;
EmbeddedEntries embeddedEntries(const pdf::PDFDocument& document)
{
    EmbeddedEntries entries;
    const auto* catalog = document.getDictionaryFromObject(document.getStorage().getTrailerDictionary().getDictionary()->get("Root"));
    const auto* names = catalog ? document.getDictionaryFromObject(catalog->get("Names")) : nullptr;
    if (!names || names->get("EmbeddedFiles").isNull()) { return entries; }
    std::set<pdf::PDFObjectReference> visited;
    const auto read = [&](const auto& self, const pdf::PDFObject& object, int depth) -> void {
        if (depth > 32 || (object.isReference() && !visited.insert(object.getReference()).second)) { throw std::runtime_error("Invalid embedded name tree"); }
        const auto* dictionary = document.getDictionaryFromObject(object);
        if (!dictionary) { throw std::runtime_error("Invalid embedded names"); }
        const auto& kids = document.getObject(dictionary->get("Kids"));
        const auto& namesArray = document.getObject(dictionary->get("Names"));
        if (kids.isArray() && namesArray.isNull()) {
            for (const auto& child : *kids.getArray()) { self(self, child, depth + 1); }
        } else if (namesArray.isArray() && kids.isNull() && namesArray.getArray()->getCount() % 2 == 0) {
            for (size_t i = 0; i < namesArray.getArray()->getCount(); i += 2) {
                const auto& key = namesArray.getArray()->getItem(i);
                const auto& value = namesArray.getArray()->getItem(i + 1);
                if (!key.isString() || !document.getDictionaryFromObject(value) || !entries.emplace(key.getString(), value).second || entries.size() > 1000) {
                    throw std::runtime_error("Invalid embedded entry");
                }
            }
        } else { throw std::runtime_error("Invalid embedded tree node"); }
    };
    read(read, names->get("EmbeddedFiles"), 0);
    return entries;
}

void writeEmbeddedEntries(pdf::PDFDocumentBuilder& builder, const pdf::PDFDocument& document, const EmbeddedEntries& entries)
{
    const auto* catalog = document.getDictionaryFromObject(document.getStorage().getTrailerDictionary().getDictionary()->get("Root"));
    const auto& old = document.getObject(catalog->get("Names"));
    if (!old.isNull() && !old.isDictionary()) { throw std::runtime_error("Invalid catalog names"); }
    auto names = old.isDictionary() ? std::make_shared<pdf::PDFDictionary>(*old.getDictionary()) : std::make_shared<pdf::PDFDictionary>();
    pdf::PDFObjectFactory factory;
    factory.beginArray();
    for (const auto& [key, value] : entries) { factory << pdf::PDFObject::createString(key) << value; }
    factory.endArray();
    names->setEntry(pdf::PDFInplaceOrMemoryString("EmbeddedFiles"), dictionaryOf({{"Names", factory.takeObject()}}));
    builder.setCatalogNames(builder.addObject(pdf::PDFObject::createDictionary(std::move(names))));
}

bool hasMissingWidgetAppearance(const pdf::PDFDocument& document, const pdf::PDFAnnotationManager& manager,
    pdf::PDFInteger pageIndex)
{
    for (const auto& entry : manager.getPageAnnotations(pageIndex).annotations) {
        const auto& annotation = *entry.annotation;
        if (annotation.getType() != pdf::AnnotationType::Widget || annotation.isReplyTo()) { continue; }
        const auto flags = annotation.getEffectiveFlags();
        if (flags.testFlag(pdf::PDFAnnotation::Hidden) || flags.testFlag(pdf::PDFAnnotation::NoView)) { continue; }
        const auto appearance = document.getObject(manager.getAppearanceStream(entry));
        if (!appearance.isStream()) { return true; }
    }
    return false;
}

QString editingRestriction(const pdf::PDFDocument& document)
{
    const auto& storage = document.getStorage();
    if (storage.getSecurityHandler()->getMode() != pdf::EncryptionMode::None) {
        return message("La edición está disponible para PDFs sin cifrado.");
    }
    const auto* form = document.getDictionaryFromObject(document.getCatalog()->getFormObject());
    if (form && !form->get("XFA").isNull()) {
        return message("La edición de documentos con formularios XFA aún no está disponible.");
    }
    const pdf::PDFDocumentDataLoaderDecorator loader(&document);
    for (const auto& entry : storage.getObjects()) {
        if (!entry.object.isDictionary()) { continue; }
        const auto* dictionary = entry.object.getDictionary();
        const auto type = loader.readNameFromDictionary(dictionary, "Type");
        if (type == "Sig" || type == "DocTimeStamp" ||
            (!dictionary->get("ByteRange").isNull() && !dictionary->get("Contents").isNull())) {
            return message("Editar documentos firmados puede invalidar sus firmas. Elige un PDF sin firma digital.");
        }
    }
    return {};
}

const pdf::PDFDictionary* catalogDictionary(const pdf::PDFDocument& document)
{
    const auto& trailer = document.getStorage().getTrailerDictionary();
    return trailer.isDictionary() ? document.getDictionaryFromObject(trailer.getDictionary()->get("Root")) : nullptr;
}

QString pageCountRestriction(const pdf::PDFDictionary* dictionary)
{
    for (const auto* key : {"PageLabels", "StructTreeRoot", "Threads", "DPartRoot"}) {
        if (dictionary && !dictionary->get(key).isNull()) {
            return message("Cambiar el número de páginas en PDFs con numeración personalizada, estructura etiquetada o artículos aún no está disponible.");
        }
    }
    return {};
}

pdf::PDFObject directImportDestination(const pdf::PDFDocument& source, const pdf::PDFObject& object)
{
    auto destination = pdf::PDFDestination::parse(&source.getStorage(), object);
    std::set<QByteArray> visited;
    while (destination.isNamedDestination()) {
        if (!visited.insert(destination.getName()).second) { throw std::runtime_error("Cyclic destination"); }
        const auto* named = source.getCatalog()->getNamedDestination(destination.getName());
        if (!named) { throw std::runtime_error("Missing destination"); }
        destination = *named;
    }
    if (!destination.isValid()) { throw std::runtime_error("Invalid destination"); }
    if (!destination.getPageReference().isValid()) {
        const auto index = destination.getPageIndex();
        if (index < 0 || static_cast<size_t>(index) >= source.getCatalog()->getPageCount()) {
            throw std::runtime_error("Invalid destination page");
        }
        destination.setPageReference(source.getCatalog()->getPage(static_cast<size_t>(index))->getPageReference());
    }
    pdf::PDFObjectFactory factory;
    factory << destination;
    return factory.takeObject();
}

pdf::PDFObject prepareImportActions(const pdf::PDFObject& object, const pdf::PDFDocument& source, int depth = 0)
{
    if (depth > 64) { throw std::runtime_error("Import nesting limit"); }
    if (object.isArray()) {
        pdf::PDFObjectFactory factory;
        factory.beginArray();
        for (size_t index = 0; index < object.getArray()->getCount(); ++index) {
            factory << prepareImportActions(object.getArray()->getItem(index), source, depth + 1);
        }
        factory.endArray();
        return factory.takeObject();
    }
    if (!object.isDictionary()) { return object; }
    const auto* dictionary = object.getDictionary();
    const pdf::PDFDocumentDataLoaderDecorator loader(&source);
    const auto action = loader.readNameFromDictionary(dictionary, "S");
    if (action == "JavaScript") { throw std::runtime_error("Scripted import action"); }
    pdf::PDFObjectFactory factory;
    factory.beginDictionary();
    for (size_t index = 0; index < dictionary->getCount(); ++index) {
        const auto key = dictionary->getKey(index).getString();
        factory.beginDictionaryItem(key);
        if ((action == "GoTo" && key == "D") || key == "Dest") {
            factory << directImportDestination(source, dictionary->getValue(index));
        } else {
            factory << prepareImportActions(dictionary->getValue(index), source, depth + 1);
        }
        factory.endDictionaryItem();
    }
    factory.endDictionary();
    return factory.takeObject();
}

bool sourceStillMatches(const QString& path, const QByteArray& expectedHash)
{
    const QFileInfo info(path);
    if (expectedHash.isEmpty() || !info.isFile() || info.isSymLink()) { return false; }
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return file.open(QIODevice::ReadOnly) && hash.addData(&file) && hash.result() == expectedHash;
}

bool samePageGeometry(const pdf::PDFPage& before, const pdf::PDFPage& after)
{
    return after.getPageRotation() == before.getPageRotation() &&
        after.getMediaBox() == before.getMediaBox() && after.getCropBox() == before.getCropBox() &&
        after.getUserUnit() == before.getUserUnit() && after.getResources() == before.getResources();
}

bool samePageLayout(const pdf::PDFPage& before, const pdf::PDFPage& after)
{
    return after.getPageReference() == before.getPageReference() && samePageGeometry(before, after);
}

void removeOutlineDestination(pdf::PDFOutlineItem& parent, pdf::PDFObjectReference page)
{
    for (size_t index = 0; index < parent.getChildCount();) {
        auto child = parent.getChildPtr(index);
        removeOutlineDestination(*child, page);
        const auto* action = dynamic_cast<const pdf::PDFActionGoTo*>(child->getAction());
        if (action && action->getDestination().getPageReference() == page) {
            if (child->getChildCount() == 0) {
                parent.removeChild(index);
                continue;
            }
            child->setAction({});
        }
        ++index;
    }
}

pdf::PDFObjectReference duplicatePageObjects(pdf::PDFDocumentBuilder& builder, const pdf::PDFPage& page)
{
    const auto emptyDictionary = [] {
        return pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>());
    };
    const auto clone = builder.addObject(emptyDictionary());
    std::map<pdf::PDFObjectReference, pdf::PDFObjectReference> mapping{{page.getPageReference(), clone}};
    std::vector<pdf::PDFObjectReference> annotations = page.getAnnotations();
    const pdf::PDFDocumentDataLoaderDecorator loader(builder.getStorage());
    // Some readers keep a popup outside Annots. Clone it together with its note.
    for (size_t index = 0; index < annotations.size(); ++index) {
        const auto* dictionary = builder.getDictionaryFromObject(builder.getObjectByReference(annotations[index]));
        if (!dictionary) { throw std::runtime_error("Invalid annotation"); }
        const auto popup = loader.readReferenceFromDictionary(dictionary, "Popup");
        if (popup.isValid() && std::ranges::find(annotations, popup) == annotations.end()) {
            const auto* popupDictionary = builder.getDictionaryFromObject(builder.getObjectByReference(popup));
            if (!popupDictionary || loader.readNameFromDictionary(popupDictionary, "Subtype") != "Popup" ||
                loader.readReferenceFromDictionary(popupDictionary, "Parent") != annotations[index]) {
                throw std::runtime_error("Invalid popup");
            }
            annotations.push_back(popup);
        }
    }
    for (const auto reference : annotations) {
        if (!mapping.contains(reference)) { mapping.emplace(reference, builder.addObject(emptyDictionary())); }
    }
    for (const auto& [before, after] : mapping) {
        auto object = builder.getObjectByReference(before);
        // Resolve an indirect Annots array before replacing its local references.
        if (before == page.getPageReference()) {
            const auto* dictionary = object.getDictionary();
            if (!dictionary) { throw std::runtime_error("Invalid page"); }
            auto copy = *dictionary;
            auto annots = builder.getObject(copy.get("Annots"));
            if (!annots.isNull() && !annots.isArray()) { throw std::runtime_error("Invalid annotation array"); }
            copy.setEntry(pdf::PDFInplaceOrMemoryString("Annots"), std::move(annots));
            object = pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(copy)));
        }
        builder.setObject(after, pdf::PDFObjectUtils::replaceReferences(object, mapping));
    }
    return clone;
}

QTransform pageToCanvas(const pdf::PDFPage& page, QSizeF canvasSize)
{
    const auto crop = page.getRotatedCropBox().size();
    const auto media = page.getRotatedMediaBox().size();
    auto matrix = pdf::PDFRenderer::createPagePointToDevicePointMatrix(&page,
        QRectF(0, 0, media.width() * canvasSize.width() / crop.width(),
            media.height() * canvasSize.height() / crop.height()));
    const auto mappedCrop = matrix.mapRect(page.getCropBox());
    return matrix * QTransform::fromTranslate(-mappedCrop.left(), -mappedCrop.top());
}

bool prepareAnnotationPage(pdf::PDFDocumentBuilder& builder, const pdf::PDFDocument& document, const pdf::PDFPage& page)
{
    const auto reference = page.getPageReference();
    const auto* dictionary = builder.getDictionaryFromObject(builder.getObjectByReference(reference));
    if (!dictionary) { return false; }
    const auto annotations = document.getObject(dictionary->get("Annots"));
    if (!annotations.isNull() && !annotations.isArray()) { return false; }
    // PDF4QT appends to a direct Annots array and uses a local MediaBox for appearances.
    pdf::PDFObjectFactory update;
    update.beginDictionary();
    update.beginDictionaryItem("MediaBox");
    update << page.getMediaBox();
    update.endDictionaryItem();
    if (dictionary->get("Annots").isReference()) {
        update.beginDictionaryItem("Annots");
        update << annotations;
        update.endDictionaryItem();
    }
    update.endDictionary();
    builder.mergeTo(reference, update.takeObject());
    return true;
}

void completeHighlightStreamLength(pdf::PDFDocumentBuilder& builder, pdf::PDFObjectReference annotation)
{
    const auto* dictionary = builder.getDictionaryFromObject(builder.getObjectByReference(annotation));
    const auto* appearance = dictionary ? builder.getDictionaryFromObject(dictionary->get("AP")) : nullptr;
    if (!appearance || !appearance->get("N").isReference()) { return; }
    const auto reference = appearance->get("N").getReference();
    const auto& object = builder.getObjectByReference(reference);
    if (!object.isStream()) { return; }
    const auto* stream = object.getStream();
    // This PDF4QT highlight generator omits the required Length entry.
    if (stream->getDictionary()->get("Length").isNull()) {
        auto streamDictionary = *stream->getDictionary();
        QByteArray content = *stream->getContent();
        streamDictionary.setEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(content.size()));
        builder.setObject(reference, pdf::PDFObject::createStream(
            std::make_shared<pdf::PDFStream>(std::move(streamDictionary), std::move(content))));
    }
}

pdf::PDFObject inheritedFieldEntry(const pdf::PDFDocument& document, const pdf::PDFFormField* field, const char* key)
{
    for (auto* current = field; current; current = current->getParentField()) {
        const auto* dictionary = document.getDictionaryFromObject(pdf::PDFObject::createReference(current->getSelfReference()));
        if (dictionary && dictionary->hasKey(key)) { return dictionary->get(key); }
    }
    return {};
}

class FormAppearanceManager final : public pdf::PDFFormManager
{
public:
    FormAppearanceManager() : PDFFormManager(nullptr) {}

    void drawFormField(const pdf::PDFFormField* field, pdf::AnnotationDrawParameters& parameters, bool) const override
    {
        const auto* textField = dynamic_cast<const pdf::PDFFormFieldText*>(field);
        const auto* choice = dynamic_cast<const pdf::PDFFormFieldChoice*>(field);
        const auto* widget = dynamic_cast<const pdf::PDFWidgetAnnotation*>(parameters.annotation);
        if ((!textField && !choice) || !widget) { return; }
        const pdf::PDFDocumentDataLoaderDecorator loader(getDocument());
        auto appearanceBytes = loader.readString(inheritedFieldEntry(*getDocument(), field, "DA"));
        if (appearanceBytes.isEmpty()) { appearanceBytes = getForm()->getDefaultAppearance().value_or(QByteArray{}); }
        const auto appearance = pdf::PDFAnnotationDefaultAppearance::parse(appearanceBytes);
        const auto rect = widget->getRectangle();
        if (!rect.isValid()) { return; }
        auto& painter = *parameters.painter;
        const QPainterStateGuard state(&painter);
        const auto color = [](const std::vector<pdf::PDFReal>& components) {
            if (components.size() == 1) { return QColor::fromRgbF(components[0], components[0], components[0]); }
            if (components.size() == 3) { return QColor::fromRgbF(components[0], components[1], components[2]); }
            if (components.size() == 4) { return QColor::fromCmykF(components[0], components[1], components[2], components[3]); }
            return QColor{};
        };
        const auto& characteristics = widget->getAppearanceCharacteristics();
        const auto background = color(characteristics.getBackgroundColor());
        const auto border = color(characteristics.getBorderColor());
        painter.setClipRect(rect);
        if (background.isValid()) { painter.fillRect(rect, background); }
        if (border.isValid()) {
            painter.setPen(QPen(border, 1));
            painter.drawRect(rect.adjusted(0.5, 0.5, -0.5, -0.5));
        }
        auto value = loader.readTextString(field->getValue(), {});
        if (choice) {
            for (const auto& option : choice->getOptions()) { if (option.exportString == value) { value = option.userString; break; } }
        }
        painter.translate(rect.left(), rect.bottom());
        painter.scale(1, -1);
        const QRectF textRect(2, 1, std::max(1.0, rect.width() - 4), std::max(1.0, rect.height() - 2));
        QFont font(QStringLiteral("Arial"));
        qreal size = appearance.getFontSize();
        const bool multiline = field->getFlags().testFlag(pdf::PDFFormField::Multiline);
        if (size <= 0) {
            size = std::min(12.0, textRect.height() * 0.75);
            if (!multiline) {
                font.setPixelSize(std::max(1, qRound(size)));
                const auto width = QFontMetricsF(font).horizontalAdvance(value);
                if (width > textRect.width()) { size *= textRect.width() / width; }
            }
        }
        font.setPixelSize(std::max(1, qRound(size)));
        painter.setFont(font);
        painter.setPen(appearance.getFontColor());
        if (choice && choice->isListBox()) {
            QStringList selected;
            const auto& stored = getDocument()->getObject(field->getValue());
            if (stored.isArray()) { for (const auto& item : *stored.getArray()) { selected.append(loader.readTextString(item, {})); } }
            else if (stored.isString()) { selected.append(loader.readTextString(stored, {})); }
            const qreal lineHeight = QFontMetricsF(font).height() + 2;
            qreal y = textRect.top();
            const auto top = std::clamp<pdf::PDFInteger>(choice->getTopIndex(), 0, static_cast<pdf::PDFInteger>(choice->getOptions().size()));
            for (size_t optionIndex = static_cast<size_t>(top); optionIndex < choice->getOptions().size(); ++optionIndex) {
                const auto& option = choice->getOptions()[optionIndex];
                if (y + lineHeight > textRect.bottom()) { break; }
                const QRectF line(textRect.left(), y, textRect.width(), lineHeight);
                if (selected.contains(option.exportString)) { painter.fillRect(line, QColor(180, 210, 255)); }
                painter.drawText(line, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, option.userString); y += lineHeight;
            }
            parameters.boundingRectangle = rect; return;
        }
        const auto quadding = loader.readInteger(inheritedFieldEntry(*getDocument(), field, "Q"),
            getForm()->getQuadding().value_or(0));
        const auto alignment = quadding == 1 ? Qt::AlignHCenter : quadding == 2 ? Qt::AlignRight : Qt::AlignLeft;
        const int flags = alignment | (multiline ? Qt::AlignTop | Qt::TextWordWrap : Qt::AlignVCenter | Qt::TextSingleLine);
        painter.drawText(textRect, flags, value);
        parameters.boundingRectangle = rect;
    }
};

const pdf::PDFFormField* radioRoot(const pdf::PDFFormField* field)
{
    if (!field || !field->getFlags().testFlag(pdf::PDFFormField::Radio)) { return field; }
    while (field->getParentField() && field->getParentField()->getFlags().testFlag(pdf::PDFFormField::Radio) &&
        field->getParentField()->getName(pdf::PDFFormField::FullyQualified) == field->getName(pdf::PDFFormField::FullyQualified)) { field = field->getParentField(); }
    return field;
}
std::optional<PdfFormField> describeFormField(const pdf::PDFDocument& document, const pdf::PDFFormField* field,
    pdf::PDFObjectReference widget, qsizetype index)
{
    if (!field) { return std::nullopt; }
    const bool childReadOnly = field->getFlags().testFlag(pdf::PDFFormField::ReadOnly);
    field = radioRoot(field);
    using Field = pdf::PDFFormField;
    const auto flags = field->getFlags();
    if (flags.testFlag(Field::Password)) { return std::nullopt; }
    const auto* dictionary = document.getDictionaryFromObject(pdf::PDFObject::createReference(widget));
    if (!dictionary) { return std::nullopt; }
    const pdf::PDFDocumentDataLoaderDecorator loader(&document);
    const auto annotationFlags = loader.readIntegerFromDictionary(dictionary, "F", 0);
    if (annotationFlags & (pdf::PDFAnnotation::Hidden | pdf::PDFAnnotation::NoView)) { return std::nullopt; }
    const bool editable = !childReadOnly && !flags.testFlag(Field::ReadOnly) &&
        !(annotationFlags & (pdf::PDFAnnotation::ReadOnly | pdf::PDFAnnotation::Locked | pdf::PDFAnnotation::LockedContents));
    const auto* characteristics = document.getDictionaryFromObject(dictionary->get("MK"));
    const auto widgetRotation = characteristics ? loader.readIntegerFromDictionary(characteristics, "R", 0) : 0;
    auto name = field->getName(Field::UserCaption);
    if (name.isEmpty()) { name = field->getName(Field::FullyQualified); }
    if (name.isEmpty()) { name = message("Campo %1").arg(index + 1); }
    if (const auto* text = dynamic_cast<const pdf::PDFFormFieldText*>(field)) {
        const bool plain = !flags.testAnyFlags({Field::RichText, Field::FileSelect, Field::Comb}) &&
            text->getRichTextValue().isEmpty() && inheritedFieldEntry(document, field, "RV").isNull() &&
            widgetRotation == 0;
        const auto maximum = loader.readInteger(inheritedFieldEntry(document, field, "MaxLen"), 0);
        return PdfFormField{index, name, PdfFormFieldType::Text, loader.readTextString(field->getValue(), {}),
            editable && plain, flags.testFlag(Field::Multiline), static_cast<qsizetype>(maximum)};
    }
    if (const auto* button = dynamic_cast<const pdf::PDFFormFieldButton*>(field);
        button && button->getButtonType() == pdf::PDFFormFieldButton::ButtonType::CheckBox) {
        const auto* appearance = document.getDictionaryFromObject(dictionary->get("AP"));
        const auto* states = appearance ? document.getDictionaryFromObject(appearance->get("N")) : nullptr;
        bool supported = states && document.getObject(states->get("Off")).isStream();
        int onStates = 0;
        if (states) {
            for (size_t state = 0; state < states->getCount(); ++state) {
                if (states->getKey(state).getString() != "Off") {
                    ++onStates;
                    supported = supported && document.getObject(states->getValue(state)).isStream();
                }
            }
        }
        const auto value = document.getObject(field->getValue());
        return PdfFormField{index, name, PdfFormFieldType::CheckBox, value.isName() && value.getString() != "Off",
            editable && supported && onStates == 1};
    }
    if (const auto* choice = dynamic_cast<const pdf::PDFFormFieldChoice*>(field)) {
        const bool multiple = choice->isListBox() && flags.testFlag(Field::MultiSelect);
        if (!choice->isComboBox() && !multiple) { return std::nullopt; }
        QStringList selected;
        const auto& stored = document.getObject(field->getValue());
        if (stored.isArray()) { for (const auto& value : *stored.getArray()) { selected.append(loader.readTextString(value, {})); } }
        else if (stored.isString()) { selected.append(loader.readTextString(stored, {})); }
        PdfFormField result{index, name, multiple ? PdfFormFieldType::MultiSelectList : PdfFormFieldType::ComboBox,
            multiple ? QVariant(selected) : QVariant(loader.readTextString(field->getValue(), {})),
            editable && !choice->isEditableComboBox() && (multiple || !flags.testFlag(Field::MultiSelect)) && widgetRotation == 0};
        for (const auto& option : choice->getOptions()) {
            result.options.append(option.userString);
            result.exportValues.append(option.exportString);
        }
        result.editable = result.editable && !result.options.isEmpty() && result.options.size() <= 500;
        return result;
    }
    if (const auto* button = dynamic_cast<const pdf::PDFFormFieldButton*>(field); button && button->getButtonType() == pdf::PDFFormFieldButton::ButtonType::RadioButton) {
        PdfFormField result{index, name, PdfFormFieldType::RadioGroup, QString::fromLatin1(loader.readName(field->getValue())), editable && !flags.testFlag(Field::RadiosInUnison)};
        pdf::PDFFormWidgets widgets;
        field->apply([&](const auto* candidate) {
            if (candidate->getFlags().testFlag(Field::ReadOnly)) { result.editable = false; }
            for (const auto& item : candidate->getWidgets()) { widgets.push_back(item); }
        });
        QSet<QString> unique;
        for (size_t i = 0; i < widgets.size(); ++i) {
            const auto* widgetDictionary = document.getDictionaryFromObject(pdf::PDFObject::createReference(widgets[i].getWidget()));
            const auto widgetFlags = widgetDictionary ? loader.readIntegerFromDictionary(widgetDictionary, "F", 0) : 0;
            if (widgetFlags & (pdf::PDFAnnotation::ReadOnly | pdf::PDFAnnotation::Locked | pdf::PDFAnnotation::LockedContents | pdf::PDFAnnotation::Hidden | pdf::PDFAnnotation::NoView)) { result.editable = false; }
            const auto* appearance = widgetDictionary ? document.getDictionaryFromObject(widgetDictionary->get("AP")) : nullptr;
            const auto* states = appearance ? document.getDictionaryFromObject(appearance->get("N")) : nullptr;
            if (!states || !document.getObject(states->get("Off")).isStream()) { result.editable = false; continue; }
            QString on;
            for (size_t j = 0; j < states->getCount(); ++j) {
                if (states->getKey(j).getString() != "Off") {
                    if (!on.isEmpty() || !document.getObject(states->getValue(j)).isStream()) { result.editable = false; }
                    on = QString::fromLatin1(states->getKey(j).getString());
                }
            }
            if (on.isEmpty() || unique.contains(on)) { result.editable = false; }
            unique.insert(on); result.exportValues.append(on);
            result.options.append(i < static_cast<size_t>(button->getOptions().size()) ? button->getOptions().at(static_cast<qsizetype>(i)) : on);
        }
        result.editable = result.editable && result.options.size() >= 2 && result.options.size() <= 20;
        return result;
    }
    return std::nullopt;
}

QPainterPath matchGeometry(const pdf::PDFTextLayout& layout, const pdf::PDFFindResult& match, QPolygonF* quads)
{
    QPainterPath highlight;
    highlight.setFillRule(Qt::WindingFill);
    for (const auto& [first, last] : match.textSelectionItems) {
        const auto& lines = layout.getTextBlocks().at(first.blockIndex).getLines();
        for (size_t line = first.lineIndex; line <= last.lineIndex; ++line) {
            const auto& characters = lines.at(line).getCharacters();
            if (characters.empty()) { continue; }
            const auto start = line == first.lineIndex ? first.characterIndex : 0;
            const auto end = line == last.lineIndex ? last.characterIndex : characters.size() - 1;
            QTransform unrotate;
            unrotate.rotate(characters.front().angle);
            QRectF bounds;
            for (size_t index = start; index <= end; ++index) {
                bounds = bounds.united(unrotate.map(characters.at(index).boundingBox).controlPointRect());
            }
            if (bounds.isValid()) {
                bounds.adjust(-0.5, -0.5, 0.5, 0.5);
                QPainterPath lineArea;
                lineArea.addRect(bounds);
                const auto restore = unrotate.inverted();
                highlight.addPath(restore.map(lineArea));
                quads->append(restore.map(QPolygonF{bounds.topLeft(), bounds.topRight(), bounds.bottomLeft(), bounds.bottomRight()}));
            }
        }
    }
    return highlight;
}

std::optional<PdfNote> readNote(const pdf::PDFDocument& document, pdf::PDFObjectReference reference, qsizetype index)
{
    const auto* dictionary = document.getDictionaryFromObject(pdf::PDFObject::createReference(reference));
    const pdf::PDFDocumentDataLoaderDecorator loader(&document);
    if (!dictionary || loader.readNameFromDictionary(dictionary, "Subtype") != "Text" ||
        !dictionary->get("IRT").isNull()) {
        return std::nullopt;
    }
    using Annotation = pdf::PDFAnnotation;
    const auto flags = Annotation::Flags::fromInt(static_cast<uint>(loader.readIntegerFromDictionary(dictionary, "F", 0)));
    if (flags.testAnyFlags({Annotation::Hidden, Annotation::NoView})) { return std::nullopt; }
    const bool editable = !flags.testAnyFlags({Annotation::ReadOnly, Annotation::Locked, Annotation::LockedContents}) &&
        dictionary->get("RC").isNull();
    return PdfNote{index, loader.readTextStringFromDictionary(dictionary, "Contents", {}), editable};
}

std::optional<PdfTextMark> readTextMark(const pdf::PDFDocument& document, pdf::PDFObjectReference reference, qsizetype index)
{
    const auto* dictionary = document.getDictionaryFromObject(pdf::PDFObject::createReference(reference));
    if (!dictionary || !dictionary->get("IRT").isNull()) { return std::nullopt; }
    const pdf::PDFDocumentDataLoaderDecorator loader(&document);
    const auto subtype = loader.readNameFromDictionary(dictionary, "Subtype");
    constexpr std::pair<const char*, PdfTextMarkup> types[] = {
        {"Highlight", PdfTextMarkup::Highlight}, {"Underline", PdfTextMarkup::Underline},
        {"StrikeOut", PdfTextMarkup::StrikeOut}, {"Squiggly", PdfTextMarkup::Squiggly}
    };
    const auto type = std::ranges::find_if(types, [&subtype](const auto& entry) { return subtype == entry.first; });
    if (type == std::end(types)) { return std::nullopt; }
    using Annotation = pdf::PDFAnnotation;
    const auto flags = Annotation::Flags::fromInt(static_cast<uint>(loader.readIntegerFromDictionary(dictionary, "F", 0)));
    if (flags.testAnyFlags({Annotation::Hidden, Annotation::NoView})) { return std::nullopt; }
    const bool editable = !flags.testAnyFlags({Annotation::ReadOnly, Annotation::Locked, Annotation::LockedContents});
    const auto rectangle = loader.readRectangle(dictionary->get("Rect"), {});
    auto area = pdf::PDFAnnotation::parseQuadrilaterals(&document.getStorage(), dictionary->get("QuadPoints"), rectangle).getPath();
    return PdfTextMark{index, type->second, loader.readTextStringFromDictionary(dictionary, "Contents", {}), editable, std::move(area)};
}

QString removeAnnotationAndPopup(pdf::PDFDocumentBuilder& builder, const pdf::PDFDocument& document,
    pdf::PDFObjectReference page, pdf::PDFObjectReference reference)
{
    const pdf::PDFDocumentDataLoaderDecorator loader(&document);
    for (const auto& entry : document.getStorage().getObjects()) {
        if (entry.object.isDictionary() &&
            loader.readReferenceFromDictionary(entry.object.getDictionary(), "IRT") == reference) {
            return message("La anotación tiene respuestas. Su eliminación aún no está disponible.");
        }
    }
    const auto* dictionary = builder.getDictionaryFromObject(builder.getObjectByReference(reference));
    const auto popup = loader.readReferenceFromDictionary(dictionary, "Popup");
    if (popup.isValid()) {
        const auto* popupDictionary = builder.getDictionaryFromObject(builder.getObjectByReference(popup));
        if (!popupDictionary || loader.readNameFromDictionary(popupDictionary, "Subtype") != "Popup" ||
            loader.readReferenceFromDictionary(popupDictionary, "Parent") != reference) {
            return message("No se pudo eliminar la ventana emergente de la anotación.");
        }
        builder.removeAnnotation(page, popup);
    }
    builder.removeAnnotation(page, reference);
    return {};
}
} // namespace

PdfDocument::PdfDocument(QString filePath, std::shared_ptr<pdf::PDFDocument> document, QByteArray sourceHash)
    : filePath_(std::move(filePath)), document_(std::move(document)), sourceHash_(std::move(sourceHash))
{
}

qsizetype PdfDocument::pageCount() const
{
    return static_cast<qsizetype>(document_->getCatalog()->getPageCount());
}

QSizeF PdfDocument::pageSize(qsizetype pageIndex) const
{
    if (pageIndex < 0 || pageIndex >= pageCount()) { return {}; }
    const auto* page = document_->getCatalog()->getPage(static_cast<size_t>(pageIndex));
    const auto validBox = [](const QRectF& box) {
        return box.isValid() && std::isfinite(box.x()) && std::isfinite(box.y()) &&
            std::isfinite(box.width()) && std::isfinite(box.height());
    };
    if (!validBox(page->getCropBox()) || !validBox(page->getMediaBox())) { return {}; }
    const auto size = page->getRotatedCropBox().size() * page->getUserUnit() * (96.0 / 72.0);
    if (!std::isfinite(size.width()) || !std::isfinite(size.height()) || size.width() <= 0 || size.height() <= 0) {
        return {};
    }
    return size;
}

PdfRenderResult PdfDocument::renderPage(qsizetype pageIndex, qreal zoom, qreal devicePixelRatio,
    PdfRenderTarget target) const
{
    const auto logicalSize = pageSize(pageIndex);
    if (logicalSize.isEmpty() || !std::isfinite(zoom) || zoom <= 0 || zoom > 4.0 ||
        !std::isfinite(devicePixelRatio) || devicePixelRatio <= 0 || devicePixelRatio > 4.0) {
        return {{}, message("La página o la escala solicitada no es válida."), {}};
    }
    const qreal width = std::ceil(logicalSize.width() * zoom * devicePixelRatio);
    const qreal height = std::ceil(logicalSize.height() * zoom * devicePixelRatio);
    // Bound allocation before converting floating point dimensions to integers.
    if (!std::isfinite(width) || !std::isfinite(height) || width > 8192 || height > 8192 || width * height > 16000000) {
        return {{}, message("La imagen es demasiado grande. Reduce el zoom para mostrar esta página."), {}};
    }

    try {
        // The page view and the thumbnail worker can request the same document.
        const std::lock_guard lock(renderMutex_);
        QImage image(QSize(static_cast<int>(width), static_cast<int>(height)), QImage::Format_ARGB32_Premultiplied);
        if (image.isNull()) { return {{}, message("No hay memoria suficiente para mostrar esta página."), {}}; }
        image.fill(Qt::white);
        const auto* page = document_->getCatalog()->getPage(static_cast<size_t>(pageIndex));
        const auto matrix = pageToCanvas(*page, QSizeF(width, height));
        if (!matrix.isInvertible() || !std::isfinite(matrix.determinant()) ||
            !std::isfinite(matrix.dx()) || !std::isfinite(matrix.dy())) {
            return {{}, message("La geometría de esta página no se puede mostrar."), {}};
        }

        const bool printing = target == PdfRenderTarget::Print;
        pdf::PDFOptionalContentActivity optionalContent(document_.get(), printing ? pdf::OCUsage::Print : pdf::OCUsage::View, nullptr);
        pdf::PDFCMSManager cmsManager(nullptr);
        cmsManager.setDocument(document_.get());
        const auto cms = cmsManager.getCurrentCMS();
        pdf::PDFFontCache fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
        // This upstream wrapper takes a mutable pointer; the font cache only reads it.
        const pdf::PDFModifiedDocument renderDocument(document_.get(), &optionalContent);
        fontCache.setDocument(renderDocument);
        fontCache.setCacheShrinkEnabled(nullptr, false);
        const auto features = pdf::PDFRenderer::Features(pdf::PDFRenderer::Antialiasing |
            pdf::PDFRenderer::TextAntialiasing | pdf::PDFRenderer::SmoothImages | pdf::PDFRenderer::ClipToCropBox |
            pdf::PDFRenderer::DisplayAnnotations);
        pdf::PDFRenderer renderer(document_.get(), &fontCache, cms.data(), &optionalContent, features, {});
        QPainter painter(&image);
        const auto* pageDictionary = document_->getDictionaryFromObject(
            pdf::PDFObject::createReference(page->getPageReference()));
        // A page may omit Contents and still display annotations.
        const auto errors = pageDictionary && pageDictionary->get("Contents").isNull()
            ? QList<pdf::PDFRenderError>{} : renderer.render(&painter, matrix, static_cast<size_t>(pageIndex));
        QStringList warnings;
        for (const auto& error : errors) {
            if (error.type == pdf::RenderErrorType::Error) {
                return {{}, message("No se pudo representar el contenido de esta página."), {error.message}};
            }
            if (error.type != pdf::RenderErrorType::Information) { warnings.append(error.message); }
        }

        if (!page->getAnnotations().empty()) {
            try {
                const QPainterStateGuard painterState(&painter);
                pdf::PDFAnnotationManager annotations(&fontCache, &cmsManager, &optionalContent, {}, features,
                    printing ? pdf::PDFAnnotationManager::Target::Print : pdf::PDFAnnotationManager::Target::View, nullptr);
                annotations.setDocument(renderDocument);
                pdf::PDFTextLayoutGetter layout(nullptr, pageIndex);
                QList<pdf::PDFRenderError> annotationErrors;
                annotations.drawPage(&painter, pageIndex, nullptr, layout, matrix, cms->getColorConvertor(), annotationErrors);
                for (const auto& error : annotationErrors) {
                    if (error.type != pdf::RenderErrorType::Information) { warnings.append(error.message); }
                }
                // Static appearances need no form manager, widget editors or form actions.
                if (hasMissingWidgetAppearance(*document_, annotations, pageIndex)) {
                    warnings.append(message("Algunos campos de formulario no tienen una apariencia guardada."));
                }
            } catch (const std::exception&) {
                warnings.append(message("No se pudieron mostrar todas las anotaciones de esta página."));
            }
        }
        const auto* form = document_->getDictionaryFromObject(document_->getCatalog()->getFormObject());
        if (form && !form->get("XFA").isNull()) {
            warnings.append(message("El contenido de formularios XFA requiere un visor compatible."));
        }

        painter.end();
        image.setDevicePixelRatio(devicePixelRatio);
        return {std::move(image), {}, std::move(warnings)};
    } catch (const std::exception&) {
        return {{}, message("Ocurrió un error al mostrar la página."), {}};
    }
}

PdfSearchResult PdfDocument::findText(const QString& text, const std::function<bool()>& cancelled) const
{
    PdfSearchResult result;
    const auto query = text.trimmed();
    if (query.isEmpty()) { return result; }
    constexpr qsizetype maxMatches = 5000;
    pdf::PDFOptionalContentActivity optionalContent(document_.get(), pdf::OCUsage::View, nullptr);
    pdf::PDFCMSManager cmsManager(nullptr);
    cmsManager.setDocument(document_.get());
    const auto cms = cmsManager.getCurrentCMS();
    pdf::PDFFontCache fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    fontCache.setDocument(pdf::PDFModifiedDocument(document_.get(), &optionalContent));
    const auto* catalog = document_->getCatalog();
    const QPainterPath crop = [] {
        QPainterPath path;
        path.addRect(QRectF(0, 0, 1, 1));
        return path;
    }();
    for (qsizetype index = 0; index < pageCount(); ++index) {
        if (cancelled && cancelled()) { break; }
        try {
            const std::lock_guard lock(renderMutex_);
            const auto* page = catalog->getPage(static_cast<size_t>(index));
            if (page->getContents().isNull()) { continue; }
            if (pageSize(index).isEmpty()) { ++result.skippedPages; continue; }
            pdf::PDFTextLayoutGenerator generator(pdf::PDFRenderer::Features{}, page, document_.get(), &fontCache,
                cms.data(), &optionalContent, QTransform(), {});
            const auto errors = generator.processContents();
            if (std::ranges::any_of(errors, [](const auto& error) { return error.type == pdf::RenderErrorType::Error; })) {
                ++result.skippedPages;
                continue;
            }
            const auto layout = generator.createTextLayout();
            const auto size = page->getRotatedCropBox().size();
            const auto matrix = pageToCanvas(*page, size) * QTransform::fromScale(1 / size.width(), 1 / size.height());
            for (const auto& flow : pdf::PDFTextFlow::createTextFlows(layout, pdf::PDFTextFlow::RemoveSoftHyphen, index)) {
                for (const auto& found : flow.find(query, Qt::CaseInsensitive)) {
                    QPolygonF quads;
                    auto highlight = matrix.map(matchGeometry(layout, found, &quads)).intersected(crop);
                    if (highlight.isEmpty()) { continue; }
                    if (result.matches.size() == maxMatches) {
                        result.limited = true;
                        return result;
                    }
                    result.matches.append({index, std::move(highlight), found.matched, std::move(quads)});
                }
            }
        } catch (const std::exception&) {
            ++result.skippedPages;
        }
    }
    return result;
}

bool PdfDocument::canCopyText() const
{
    return document_->getStorage().getSecurityHandler()->isAllowed(pdf::PDFSecurityHandler::Permission::CopyContent);
}

PdfTextSelectionResult PdfDocument::selectText(qsizetype pageIndex, QPointF start, QPointF end) const
{
    const auto validPoint = [](QPointF point) {
        return std::isfinite(point.x()) && std::isfinite(point.y()) && QRectF(0, 0, 1, 1).contains(point);
    };
    if (pageSize(pageIndex).isEmpty() || !validPoint(start) || !validPoint(end)) {
        return {{-1, {}, {}, {}}, message("La selección de texto no es válida.")};
    }
    if (!canCopyText()) { return {{-1, {}, {}, {}}, message("Este PDF no permite copiar texto.")}; }
    try {
        const std::lock_guard lock(renderMutex_);
        const auto* page = document_->getCatalog()->getPage(static_cast<size_t>(pageIndex));
        if (page->getContents().isNull()) { return {}; }
        const auto size = page->getRotatedCropBox().size();
        const auto matrix = pageToCanvas(*page, size) * QTransform::fromScale(1 / size.width(), 1 / size.height());
        if (!matrix.isInvertible()) { return {{-1, {}, {}, {}}, message("No se pudo calcular la selección.")}; }
        pdf::PDFOptionalContentActivity optionalContent(document_.get(), pdf::OCUsage::View, nullptr);
        pdf::PDFCMSManager cmsManager(nullptr);
        cmsManager.setDocument(document_.get());
        const auto cms = cmsManager.getCurrentCMS();
        pdf::PDFFontCache fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
        fontCache.setDocument(pdf::PDFModifiedDocument(document_.get(), &optionalContent));
        pdf::PDFTextLayoutGenerator generator(pdf::PDFRenderer::Features{}, page, document_.get(), &fontCache,
            cms.data(), &optionalContent, QTransform(), {});
        const auto errors = generator.processContents();
        if (std::ranges::any_of(errors, [](const auto& error) { return error.type == pdf::RenderErrorType::Error; })) {
            return {{-1, {}, {}, {}}, message("No se pudo leer el texto de esta página.")};
        }
        const auto layout = generator.createTextLayout();
        auto rectangle = QRectF(start, end).normalized();
        // Keep straight drags selectable without changing the region at normal sizes.
        if (rectangle.width() < 0.01 / size.width()) { rectangle.adjust(-0.005 / size.width(), 0, 0.005 / size.width(), 0); }
        if (rectangle.height() < 0.01 / size.height()) { rectangle.adjust(0, -0.005 / size.height(), 0, 0.005 / size.height()); }
        pdf::PDFTextSelection selection;
        const auto& blocks = layout.getTextBlocks();
        for (size_t block = 0; block < blocks.size(); ++block) {
            const auto& lines = blocks[block].getLines();
            for (size_t line = 0; line < lines.size(); ++line) {
                const auto& characters = lines[line].getCharacters();
                size_t first = characters.size();
                size_t last = 0;
                for (size_t index = 0; index < characters.size(); ++index) {
                    if (matrix.map(characters[index].boundingBox).intersects(rectangle)) {
                        first = std::min(first, index);
                        last = index;
                    }
                }
                if (first < characters.size()) {
                    selection.addItems({{{pageIndex, block, line, first}, {pageIndex, block, line, last}}}, Qt::yellow);
                }
            }
        }
        selection.build();
        if (selection.isEmpty()) { return {}; }
        pdf::PDFFindResult found;
        const auto flows = pdf::PDFTextFlow::createTextFlows(layout,
            pdf::PDFTextFlow::RemoveSoftHyphen | pdf::PDFTextFlow::AddLineBreaks, pageIndex);
        QStringList fragments;
        if (!flows.empty()) {
            for (const auto& item : selection) { fragments.append(flows.front().getText(item.start, item.end)); }
        }
        found.matched = fragments.join(u'\n').trimmed();
        if (found.matched.isEmpty()) { return {}; }
        for (const auto& item : selection) { found.textSelectionItems.emplace_back(item.start, item.end); }
        QPolygonF quads;
        QPainterPath crop;
        crop.addRect(QRectF(0, 0, 1, 1));
        auto highlight = matrix.map(matchGeometry(layout, found, &quads)).intersected(crop);
        if (highlight.isEmpty()) { return {}; }
        return {{pageIndex, std::move(highlight), std::move(found.matched), std::move(quads)}, {}};
    } catch (const std::exception&) {
        return {{-1, {}, {}, {}}, message("No se pudo seleccionar el texto de esta página.")};
    }
}

namespace {
struct BookmarkTree
{
    pdf::PDFObjectReference reference;
    pdf::PDFObject data;
    QList<BookmarkTree> children;
    bool open = true;
};

BookmarkTree readBookmarkTree(const pdf::PDFDocument& document)
{
    BookmarkTree root;
    const auto* catalog = catalogDictionary(document);
    if (!catalog) { throw std::runtime_error("Missing catalog"); }
    const auto& outlines = catalog->get("Outlines");
    if (outlines.isNull()) {
        root.data = pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>());
        return root;
    }
    if (!outlines.isReference()) { throw std::runtime_error("Unsupported outline root"); }
    root.reference = outlines.getReference();
    root.data = document.getObject(outlines);
    const pdf::PDFDocumentDataLoaderDecorator loader(&document);
    std::set<pdf::PDFObjectReference> visited{root.reference};
    const auto read = [&](const auto& self, BookmarkTree& parent, int depth) -> void {
        if (!parent.data.isDictionary() || depth > 64) { throw std::runtime_error("Invalid outline tree"); }
        const auto* dictionary = parent.data.getDictionary();
        auto current = dictionary->get("First");
        pdf::PDFObjectReference previous;
        while (!current.isNull()) {
            if (!current.isReference() || visited.size() >= 10001 || !visited.insert(current.getReference()).second) {
                throw std::runtime_error("Invalid outline links");
            }
            BookmarkTree node;
            node.reference = current.getReference();
            node.data = document.getObject(current);
            if (!node.data.isDictionary()) { throw std::runtime_error("Invalid outline item"); }
            const auto* item = node.data.getDictionary();
            if (loader.readReferenceFromDictionary(item, "Parent") != parent.reference ||
                loader.readReferenceFromDictionary(item, "Prev") != previous) {
                throw std::runtime_error("Inconsistent outline links");
            }
            node.open = loader.readIntegerFromDictionary(item, "Count", 0) >= 0;
            self(self, node, depth + 1);
            previous = node.reference;
            current = item->get("Next");
            parent.children.append(std::move(node));
        }
        if (loader.readReferenceFromDictionary(dictionary, "Last") != previous) {
            throw std::runtime_error("Invalid last outline item");
        }
    };
    read(read, root, 0);
    return root;
}

void writeBookmarkTree(pdf::PDFDocumentBuilder& builder, BookmarkTree& root)
{
    if (!root.reference.isValid()) { root.reference = builder.addObject(root.data); }
    const auto write = [&](const auto& self, BookmarkTree& node, bool isRoot) -> pdf::PDFInteger {
        auto dictionary = std::make_shared<pdf::PDFDictionary>(*node.data.getDictionary());
        for (auto& child : node.children) {
            if (!child.reference.isValid()) { child.reference = builder.addObject(child.data); }
        }
        pdf::PDFInteger visible = 0;
        for (qsizetype i = 0; i < node.children.size(); ++i) {
            auto& child = node.children[i];
            auto childDictionary = std::make_shared<pdf::PDFDictionary>(*child.data.getDictionary());
            childDictionary->setEntry(pdf::PDFInplaceOrMemoryString("Parent"), pdf::PDFObject::createReference(node.reference));
            for (const auto* key : {"Prev", "Next"}) { childDictionary->removeEntry(key); }
            if (i > 0) { childDictionary->setEntry(pdf::PDFInplaceOrMemoryString("Prev"), pdf::PDFObject::createReference(node.children[i - 1].reference)); }
            if (i + 1 < node.children.size()) { childDictionary->setEntry(pdf::PDFInplaceOrMemoryString("Next"), pdf::PDFObject::createReference(node.children[i + 1].reference)); }
            child.data = pdf::PDFObject::createDictionary(std::move(childDictionary));
            visible += self(self, child, false);
        }
        for (const auto* key : {"First", "Last", "Count"}) { dictionary->removeEntry(key); }
        if (!node.children.isEmpty()) {
            dictionary->setEntry(pdf::PDFInplaceOrMemoryString("First"), pdf::PDFObject::createReference(node.children.front().reference));
            dictionary->setEntry(pdf::PDFInplaceOrMemoryString("Last"), pdf::PDFObject::createReference(node.children.back().reference));
            dictionary->setEntry(pdf::PDFInplaceOrMemoryString("Count"), pdf::PDFObject::createInteger(isRoot || node.open ? visible : -visible));
        }
        builder.setObject(node.reference, pdf::PDFObject::createDictionary(std::move(dictionary)));
        return 1 + (node.open ? visible : 0);
    };
    write(write, root, true);
    pdf::PDFObjectFactory update;
    update.beginDictionary();
    update.beginDictionaryItem("Outlines");
    update << root.reference;
    update.endDictionaryItem();
    update.endDictionary();
    builder.mergeTo(builder.getCatalogReference(), update.takeObject());
}
} // namespace

PdfOpenResult PdfDocument::changeBookmark(PdfBookmarkChange change, const QList<qsizetype>& path,
    const QString& title, qsizetype pageIndex) const
{
    const auto failure = message("No se pudo modificar el marcador. El árbol puede tener enlaces incompatibles o dañados.");
    if (change != PdfBookmarkChange::Add && change != PdfBookmarkChange::Rename &&
        change != PdfBookmarkChange::Retarget && change != PdfBookmarkChange::Delete &&
        change != PdfBookmarkChange::MoveUp && change != PdfBookmarkChange::MoveDown &&
        change != PdfBookmarkChange::Indent && change != PdfBookmarkChange::Outdent) { return {nullptr, failure, {}}; }
    const auto text = title.trimmed();
    if ((change == PdfBookmarkChange::Add || change == PdfBookmarkChange::Rename) && (text.isEmpty() || text.size() > 4096)) {
        return {nullptr, message("Escribe un nombre de marcador de hasta 4096 caracteres."), {}};
    }
    if ((change == PdfBookmarkChange::Add || change == PdfBookmarkChange::Retarget) && (pageIndex < 0 || pageIndex >= pageCount())) {
        return {nullptr, message("Selecciona una página de destino válida."), {}};
    }
    try {
        const std::lock_guard lock(renderMutex_);
        const auto restriction = editingRestriction(*document_);
        if (!restriction.isEmpty()) { return {nullptr, restriction, {}}; }
        auto root = readBookmarkTree(*document_);
        auto* item = &root;
        auto* parent = &root;
        for (const auto index : path) {
            if (index < 0 || index >= item->children.size()) { return {nullptr, failure, {}}; }
            parent = item;
            item = &item->children[index];
        }
        if (change != PdfBookmarkChange::Add && path.isEmpty()) { return {nullptr, failure, {}}; }
        pdf::PDFDocumentBuilder builder(document_.get());
        if (change == PdfBookmarkChange::Delete) {
            parent->children.removeAt(path.back());
        } else if (change == PdfBookmarkChange::MoveUp || change == PdfBookmarkChange::MoveDown) {
            const auto destination = path.back() + (change == PdfBookmarkChange::MoveUp ? -1 : 1);
            if (destination < 0 || destination >= parent->children.size()) { return {nullptr, failure, {}}; }
            parent->children.swapItemsAt(path.back(), destination);
        } else if (change == PdfBookmarkChange::Indent) {
            const auto index = path.back();
            if (index == 0) { return {nullptr, failure, {}}; }
            auto moved = parent->children.takeAt(index);
            parent->children[index - 1].children.append(std::move(moved));
            parent->children[index - 1].open = true;
        } else if (change == PdfBookmarkChange::Outdent) {
            if (path.size() < 2) { return {nullptr, failure, {}}; }
            auto* grandparent = &root;
            for (qsizetype i = 0; i + 2 < path.size(); ++i) { grandparent = &grandparent->children[path[i]]; }
            auto moved = parent->children.takeAt(path.back());
            grandparent->children.insert(path[path.size() - 2] + 1, std::move(moved));
        } else {
            if (change == PdfBookmarkChange::Add) {
                BookmarkTree added;
                added.data = pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>());
                item->children.append(std::move(added));
                item = &item->children.back();
            }
            auto dictionary = std::make_shared<pdf::PDFDictionary>(*item->data.getDictionary());
            if (change == PdfBookmarkChange::Add || change == PdfBookmarkChange::Rename) {
                pdf::PDFObjectFactory value;
                value << text;
                dictionary->setEntry(pdf::PDFInplaceOrMemoryString("Title"), value.takeObject());
            }
            if (change == PdfBookmarkChange::Add || change == PdfBookmarkChange::Retarget) {
                pdf::PDFDestination destination;
                destination.setDestinationType(pdf::DestinationType::Fit);
                destination.setPageReference(document_->getCatalog()->getPage(static_cast<size_t>(pageIndex))->getPageReference());
                pdf::PDFObjectFactory value;
                value << destination;
                dictionary->removeEntry("A");
                dictionary->setEntry(pdf::PDFInplaceOrMemoryString("Dest"), value.takeObject());
            }
            item->data = pdf::PDFObject::createDictionary(std::move(dictionary));
        }
        qsizetype count = 0;
        const auto validateSize = [&](const auto& self, const BookmarkTree& node, int depth) -> void {
            if (depth > 64 || ++count > 10001) { throw std::runtime_error("Outline limit exceeded"); }
            for (const auto& child : node.children) { self(self, child, depth + 1); }
        };
        validateSize(validateSize, root, 0);
        writeBookmarkTree(builder, root);
        auto edited = std::make_shared<pdf::PDFDocument>(builder.build());
        return {std::shared_ptr<const PdfDocument>(new PdfDocument(filePath_, std::move(edited), sourceHash_)), {}, {}};
    } catch (const std::exception&) { return {nullptr, failure, {}}; }
}

QList<PdfBookmark> PdfDocument::bookmarks() const
{
    const std::lock_guard lock(renderMutex_);
    QList<PdfBookmark> result;
    const auto* catalog = document_->getCatalog();
    const auto root = catalog->getOutlineRootPtr();
    if (!root) { return result; }
    const auto append = [&](const auto& self, const pdf::PDFOutlineItem& parent, int depth, const QList<qsizetype>& path) -> void {
        if (depth > 64) { return; }
        for (size_t i = 0; i < parent.getChildCount() && result.size() < 10000; ++i) {
            const auto* item = parent.getChild(i);
            qsizetype pageIndex = -1;
            if (const auto* action = dynamic_cast<const pdf::PDFActionGoTo*>(item->getAction())) {
                auto destination = action->getDestination();
                std::set<QByteArray> visited;
                while (destination.isNamedDestination()) {
                    if (!visited.insert(destination.getName()).second) { break; }
                    const auto* named = catalog->getNamedDestination(destination.getName());
                    if (!named) { break; }
                    destination = *named;
                }
                if (destination.isValid() && !destination.isNamedDestination()) {
                    if (destination.getPageReference().isValid()) {
                        const auto index = catalog->getPageIndexFromPageReference(destination.getPageReference());
                        if (index < static_cast<size_t>(pageCount())) { pageIndex = static_cast<qsizetype>(index); }
                    } else if (destination.getPageIndex() >= 0 && destination.getPageIndex() < pageCount()) {
                        pageIndex = destination.getPageIndex();
                    }
                }
            }
            auto itemPath = path;
            itemPath.append(static_cast<qsizetype>(i));
            result.append({item->getTitle(), pageIndex, depth, itemPath});
            self(self, *item, depth + 1, itemPath);
        }
    };
    append(append, *root, 0, {});
    return result;
}

int PdfDocument::printResolutionLimit() const
{
    using Permission = pdf::PDFSecurityHandler::Permission;
    const auto* handler = document_->getStorage().getSecurityHandler();
    if (!handler->isAllowed(Permission::PrintLowResolution)) { return 0; }
    return handler->isAllowed(Permission::PrintHighResolution) ? 300 : 150;
}

PdfRenderResult PdfDocument::renderForOutput(qsizetype pageIndex, int dpi, PdfRenderTarget target) const
{
    const auto size = pageSize(pageIndex);
    if (size.isEmpty() || dpi <= 0) {
        return {{}, message("La página o la resolución no es válida."), {}};
    }
    const auto zoom = std::min({dpi / 96.0, 4.0, 8191.0 / size.width(), 8191.0 / size.height(),
        std::sqrt(15980000.0 / size.width() / size.height())});
    auto result = renderPage(pageIndex, zoom, 1.0, target);
    if (!result.image.isNull()) {
        const auto dotsPerMeter = qRound(zoom * 96 / 0.0254);
        result.image.setDotsPerMeterX(dotsPerMeter);
        result.image.setDotsPerMeterY(dotsPerMeter);
    }
    return result;
}

PdfOutputResult PdfDocument::exportPng(qsizetype pageIndex, const QString& path) const
{
    using Permission = pdf::PDFSecurityHandler::Permission;
    if (!document_->getStorage().getSecurityHandler()->isAllowed(Permission::CopyContent)) {
        return {message("El PDF restringe la copia de su contenido."), {}};
    }
    const QFileInfo destination(path);
    if (path.isEmpty() || !destination.isNativePath() || destination.fileName().isEmpty()
#ifdef Q_OS_WIN
        || destination.fileName().contains(u':')
#endif
    ) {
        return {message("Elige un nombre de archivo válido para el PNG."), {}};
    }
    if (destination.exists() || destination.isSymLink()) {
        return {message("Ya existe un archivo o carpeta con ese nombre. Elige un nombre nuevo para el PNG."), {}};
    }
    const auto rendered = renderForOutput(pageIndex, 150, PdfRenderTarget::View);
    if (rendered.image.isNull()) { return {rendered.error, rendered.warnings}; }
    QTemporaryFile temporary(destination.dir().absoluteFilePath(QStringLiteral(".pdf-editor-XXXXXX.tmp")));
    if (!temporary.open() || !rendered.image.save(&temporary, "PNG") || !temporary.flush() ||
        !temporary.rename(destination.absoluteFilePath())) {
        return {message("No se pudo guardar el PNG. Comprueba el destino, los permisos y el espacio disponible."), {}};
    }
    temporary.setAutoRemove(false);
    return {{}, rendered.warnings};
}

QList<PdfNote> PdfDocument::pageNotes(qsizetype pageIndex) const
{
    QList<PdfNote> notes;
    if (pageIndex < 0 || pageIndex >= pageCount()) { return notes; }
    const auto& annotations = document_->getCatalog()->getPage(static_cast<size_t>(pageIndex))->getAnnotations();
    for (size_t index = 0; index < annotations.size(); ++index) {
        if (auto note = readNote(*document_, annotations[index], static_cast<qsizetype>(index))) {
            notes.append(std::move(*note));
        }
    }
    return notes;
}

PdfOpenResult PdfDocument::addNote(qsizetype pageIndex, QPointF position, const QString& text) const
{
    if (text.trimmed().isEmpty()) { return {nullptr, message("Escribe el texto de la nota."), {}}; }
    if (pageSize(pageIndex).isEmpty() || !std::isfinite(position.x()) || !std::isfinite(position.y()) ||
        !QRectF(0, 0, 1, 1).contains(position)) {
        return {nullptr, message("Selecciona una posición dentro de una página válida."), {}};
    }
    const auto failure = message("No se pudo añadir la nota a la página.");
    try {
        const std::lock_guard lock(renderMutex_);
        const auto restriction = editingRestriction(*document_);
        if (!restriction.isEmpty()) { return {nullptr, restriction, {}}; }
        const auto* page = document_->getCatalog()->getPage(static_cast<size_t>(pageIndex));
        const auto size = page->getRotatedCropBox().size();
        bool invertible = false;
        const auto canvasToPage = pageToCanvas(*page, size).inverted(&invertible);
        constexpr qreal iconSize = 32;
        const auto anchor = canvasToPage.map(QPointF(
            std::min(position.x() * size.width(), std::max(0.0, size.width() - iconSize)),
            std::min(position.y() * size.height(), std::max(0.0, size.height() - iconSize))));
        if (!invertible || !std::isfinite(anchor.x()) || !std::isfinite(anchor.y())) { return {nullptr, failure, {}}; }

        const auto pageReference = page->getPageReference();
        pdf::PDFDocumentBuilder builder(document_.get());
        if (!prepareAnnotationPage(builder, *document_, *page)) { return {nullptr, failure, {}}; }
        const QRectF rectangle(anchor - QPointF(0, iconSize), QSizeF(iconSize, iconSize));
        const auto note = builder.createAnnotationText(pageReference, rectangle, pdf::TextAnnotationIcon::Note,
            {}, {}, text, false);
        if (!note.isValid()) { return {nullptr, failure, {}}; }
        auto edited = std::make_shared<pdf::PDFDocument>(builder.build());
        return {std::shared_ptr<const PdfDocument>(new PdfDocument(filePath_, std::move(edited), sourceHash_)), {}, {}};
    } catch (const std::exception&) {
        return {nullptr, failure, {}};
    }
}

PdfOpenResult PdfDocument::editNote(qsizetype pageIndex, qsizetype noteIndex, const QString& text) const
{
    if (text.trimmed().isEmpty()) { return {nullptr, message("Escribe el texto de la nota."), {}}; }
    const auto invalid = message("Selecciona una nota válida en esta página.");
    if (pageIndex < 0 || pageIndex >= pageCount() || noteIndex < 0) { return {nullptr, invalid, {}}; }
    try {
        const std::lock_guard lock(renderMutex_);
        const auto restriction = editingRestriction(*document_);
        if (!restriction.isEmpty()) { return {nullptr, restriction, {}}; }
        const auto& annotations = document_->getCatalog()->getPage(static_cast<size_t>(pageIndex))->getAnnotations();
        const auto index = static_cast<size_t>(noteIndex);
        if (index >= annotations.size()) { return {nullptr, invalid, {}}; }
        const auto reference = annotations[index];
        const auto note = readNote(*document_, reference, noteIndex);
        if (!note) { return {nullptr, invalid, {}}; }
        if (!note->editable) { return {nullptr, message("Esta nota es de solo lectura."), {}}; }

        pdf::PDFDocumentBuilder builder(document_.get());
        builder.setAnnotationContents(reference, text);
        pdf::PDFObjectFactory modification;
        modification.beginDictionary();
        modification.beginDictionaryItem("M");
        modification << pdf::WrapCurrentDateTime();
        modification.endDictionaryItem();
        modification.endDictionary();
        builder.mergeTo(reference, modification.takeObject());
        auto edited = std::make_shared<pdf::PDFDocument>(builder.build());
        return {std::shared_ptr<const PdfDocument>(new PdfDocument(filePath_, std::move(edited), sourceHash_)), {}, {}};
    } catch (const std::exception&) {
        return {nullptr, message("No se pudo editar la nota."), {}};
    }
}

PdfOpenResult PdfDocument::deleteNote(qsizetype pageIndex, qsizetype noteIndex) const
{
    const auto failure = message("No se pudo eliminar la nota.");
    if (pageIndex < 0 || pageIndex >= pageCount() || noteIndex < 0) { return {nullptr, failure, {}}; }
    try {
        const std::lock_guard lock(renderMutex_);
        const auto restriction = editingRestriction(*document_);
        if (!restriction.isEmpty()) { return {nullptr, restriction, {}}; }
        const auto* page = document_->getCatalog()->getPage(static_cast<size_t>(pageIndex));
        const auto& annotations = page->getAnnotations();
        if (static_cast<size_t>(noteIndex) >= annotations.size()) { return {nullptr, failure, {}}; }
        const auto reference = annotations[static_cast<size_t>(noteIndex)];
        const auto note = readNote(*document_, reference, noteIndex);
        if (!note || !note->editable) { return {nullptr, message("Esta nota es de solo lectura."), {}}; }
        pdf::PDFDocumentBuilder builder(document_.get());
        const auto error = removeAnnotationAndPopup(builder, *document_, page->getPageReference(), reference);
        if (!error.isEmpty()) { return {nullptr, error, {}}; }
        auto edited = std::make_shared<pdf::PDFDocument>(builder.build());
        return {std::shared_ptr<const PdfDocument>(new PdfDocument(filePath_, std::move(edited), sourceHash_)), {}, {}};
    } catch (const std::exception&) {
        return {nullptr, failure, {}};
    }
}

PdfOpenResult PdfDocument::highlightMatch(const PdfSearchMatch& match) const
{
    return markMatch(match, PdfTextMarkup::Highlight);
}

PdfOpenResult PdfDocument::markMatch(const PdfSearchMatch& match, PdfTextMarkup type) const
{
    const auto failure = message("No se pudo aplicar la marca de texto.");
    if (pageSize(match.page).isEmpty() || match.highlight.isEmpty()) { return {nullptr, failure, {}}; }
    try {
        const std::lock_guard lock(renderMutex_);
        const auto restriction = editingRestriction(*document_);
        if (!restriction.isEmpty()) { return {nullptr, restriction, {}}; }
        const auto* page = document_->getCatalog()->getPage(static_cast<size_t>(match.page));
        const auto size = page->getRotatedCropBox().size();
        bool invertible = false;
        const auto inverse = pageToCanvas(*page, size).inverted(&invertible);
        if (!invertible) { return {nullptr, failure, {}}; }
        const auto toPage = QTransform::fromScale(size.width(), size.height()) * inverse;
        auto quads = match.quadrilaterals;
        if (quads.isEmpty()) {
            for (const auto& polygon : match.highlight.toSubpathPolygons()) {
                const auto rect = polygon.boundingRect().intersected(QRectF(0, 0, 1, 1));
                if (!rect.isValid()) { continue; }
                const auto pageRect = toPage.map(QPolygonF{rect.topLeft(), rect.topRight(), rect.bottomLeft(), rect.bottomRight()}).boundingRect();
                quads.append({pageRect.topLeft(), pageRect.topRight(), pageRect.bottomLeft(), pageRect.bottomRight()});
            }
        }
        if (quads.isEmpty() || quads.size() % 4 != 0) { return {nullptr, failure, {}}; }
        for (const auto& point : quads) {
            if (!std::isfinite(point.x()) || !std::isfinite(point.y())) { return {nullptr, failure, {}}; }
        }
        pdf::PDFDocumentBuilder builder(document_.get());
        if (!prepareAnnotationPage(builder, *document_, *page)) { return {nullptr, failure, {}}; }
        pdf::PDFObjectReference reference;
        switch (type) {
        case PdfTextMarkup::Highlight:
            reference = builder.createAnnotationHighlight(page->getPageReference(), quads, Qt::yellow);
            completeHighlightStreamLength(builder, reference);
            builder.setAnnotationFillOpacity(reference, 0.35);
            break;
        case PdfTextMarkup::Underline:
            reference = builder.createAnnotationUnderline(page->getPageReference(), quads, QColor(30, 90, 190));
            break;
        case PdfTextMarkup::StrikeOut:
            reference = builder.createAnnotationStrikeout(page->getPageReference(), quads, QColor(190, 45, 45));
            break;
        case PdfTextMarkup::Squiggly:
            reference = builder.createAnnotationSquiggly(page->getPageReference(), quads, QColor(190, 45, 45));
            break;
        }
        if (!reference.isValid()) { return {nullptr, failure, {}}; }
        builder.setAnnotationContents(reference, match.text);
        pdf::PDFObjectFactory flags;
        flags.beginDictionary();
        flags.beginDictionaryItem("F");
        flags << pdf::PDFInteger(pdf::PDFAnnotation::Print);
        flags.endDictionaryItem();
        flags.endDictionary();
        builder.mergeTo(reference, flags.takeObject());
        builder.updateAnnotationAppearanceStreams(reference);
        completeHighlightStreamLength(builder, reference);
        auto edited = std::make_shared<pdf::PDFDocument>(builder.build());
        return {std::shared_ptr<const PdfDocument>(new PdfDocument(filePath_, std::move(edited), sourceHash_)), {}, {}};
    } catch (const std::exception&) {
        return {nullptr, failure, {}};
    }
}

QList<PdfTextMark> PdfDocument::pageTextMarks(qsizetype pageIndex) const
{
    QList<PdfTextMark> marks;
    if (pageSize(pageIndex).isEmpty()) { return marks; }
    const auto* page = document_->getCatalog()->getPage(static_cast<size_t>(pageIndex));
    const auto size = page->getRotatedCropBox().size();
    const auto normalized = pageToCanvas(*page, size) * QTransform::fromScale(1 / size.width(), 1 / size.height());
    const auto& annotations = page->getAnnotations();
    for (size_t index = 0; index < annotations.size(); ++index) {
        if (auto mark = readTextMark(*document_, annotations[index], static_cast<qsizetype>(index))) {
            mark->area = normalized.map(mark->area);
            marks.append(std::move(*mark));
        }
    }
    return marks;
}

PdfOpenResult PdfDocument::deleteTextMark(qsizetype pageIndex, qsizetype markIndex) const
{
    const auto failure = message("No se pudo eliminar la marca de texto.");
    if (pageIndex < 0 || pageIndex >= pageCount() || markIndex < 0) { return {nullptr, failure, {}}; }
    try {
        const std::lock_guard lock(renderMutex_);
        const auto restriction = editingRestriction(*document_);
        if (!restriction.isEmpty()) { return {nullptr, restriction, {}}; }
        const auto* page = document_->getCatalog()->getPage(static_cast<size_t>(pageIndex));
        const auto& annotations = page->getAnnotations();
        if (static_cast<size_t>(markIndex) >= annotations.size()) { return {nullptr, failure, {}}; }
        const auto reference = annotations[static_cast<size_t>(markIndex)];
        const auto mark = readTextMark(*document_, reference, markIndex);
        if (!mark || !mark->editable) { return {nullptr, message("Esta marca es de solo lectura."), {}}; }
        pdf::PDFDocumentBuilder builder(document_.get());
        const auto error = removeAnnotationAndPopup(builder, *document_, page->getPageReference(), reference);
        if (!error.isEmpty()) { return {nullptr, error, {}}; }
        auto edited = std::make_shared<pdf::PDFDocument>(builder.build());
        return {std::shared_ptr<const PdfDocument>(new PdfDocument(filePath_, std::move(edited), sourceHash_)), {}, {}};
    } catch (const std::exception&) {
        return {nullptr, failure, {}};
    }
}

QList<PdfFormField> PdfDocument::pageFormFields(qsizetype pageIndex) const
{
    QList<PdfFormField> fields;
    if (pageIndex < 0 || pageIndex >= pageCount()) { return fields; }
    const auto* dictionary = document_->getDictionaryFromObject(document_->getCatalog()->getFormObject());
    if (!dictionary || !dictionary->get("XFA").isNull()) { return fields; }
    const auto form = pdf::PDFForm::parse(document_.get(), document_->getCatalog()->getFormObject());
    const auto& annotations = document_->getCatalog()->getPage(static_cast<size_t>(pageIndex))->getAnnotations();
    QSet<const pdf::PDFFormField*> radioGroups;
    for (size_t index = 0; index < annotations.size(); ++index) {
        const auto* native = form.getFormFieldForWidget(annotations[index]);
        if (auto field = describeFormField(*document_, native,
            annotations[index], static_cast<qsizetype>(index))) {
            if (field->type == PdfFormFieldType::RadioGroup && radioGroups.contains(radioRoot(native))) { continue; }
            if (field->type == PdfFormFieldType::RadioGroup) { radioGroups.insert(radioRoot(native)); }
            fields.append(std::move(*field));
        }
    }
    return fields;
}

PdfOpenResult PdfDocument::setFormField(qsizetype pageIndex, qsizetype fieldIndex, const QVariant& value) const
{
    const auto failure = message("No se pudo rellenar el campo de formulario.");
    if (pageIndex < 0 || pageIndex >= pageCount() || fieldIndex < 0) { return {nullptr, failure, {}}; }
    try {
        const std::lock_guard lock(renderMutex_);
        const auto restriction = editingRestriction(*document_);
        if (!restriction.isEmpty()) { return {nullptr, restriction, {}}; }
        FormAppearanceManager manager;
        manager.setDocument(pdf::PDFModifiedDocument(document_.get(), nullptr));
        const auto* page = document_->getCatalog()->getPage(static_cast<size_t>(pageIndex));
        const auto& annotations = page->getAnnotations();
        if (static_cast<size_t>(fieldIndex) >= annotations.size()) { return {nullptr, failure, {}}; }
        const auto widgetReference = annotations[static_cast<size_t>(fieldIndex)];
        auto* field = manager.getFormFieldForWidget(widgetReference);
        const auto description = describeFormField(*document_, field, widgetReference, fieldIndex);
        if (!description || !description->editable) { return {nullptr, message("Este campo es de solo lectura o no es compatible."), {}}; }
        if (description->type == PdfFormFieldType::RadioGroup) {
            if (value.metaType().id() != QMetaType::QString || !description->exportValues.contains(value.toString())) { return {nullptr, failure, {}}; }
            const auto* root = radioRoot(field); pdf::PDFDocumentBuilder builder(document_.get());
            bool linked = false;
            manager.modify([&](const auto* candidate) { if (candidate->getName(pdf::PDFFormField::FullyQualified) == root->getName(pdf::PDFFormField::FullyQualified) && radioRoot(candidate) != root) { linked = true; } });
            if (linked) { return {nullptr, message("Los grupos de opción vinculados con el mismo nombre aún no son compatibles."), {}}; }
            bool valid = true; int selectedCount = 0;
            root->apply([&](const auto* candidate) {
                builder.mergeTo(candidate->getSelfReference(), dictionaryOf({{"V", pdf::PDFObject::createName(value.toString().toLatin1())}}));
                for (const auto& widget : candidate->getWidgets()) {
                    const auto state = pdf::PDFFormFieldButton::getOnAppearanceState(&manager, &widget); const bool selected = state == value.toString().toLatin1();
                    selectedCount += selected ? 1 : 0;
                    const auto info = describeFormField(*document_, candidate, widget.getWidget(), fieldIndex); valid = valid && info && info->editable;
                    builder.mergeTo(widget.getWidget(), dictionaryOf({{"AS", pdf::PDFObject::createName(selected ? state : QByteArray("Off"))}}));
                }
            });
            if (!valid || selectedCount != 1) { return {nullptr, failure, {}}; }
            return {std::shared_ptr<const PdfDocument>(new PdfDocument(filePath_, std::make_shared<pdf::PDFDocument>(builder.build()), sourceHash_)), {}, {}};
        }
        pdf::PDFObject fieldValue;
        if (description->type == PdfFormFieldType::Text) {
            if (value.metaType().id() != QMetaType::QString) { return {nullptr, failure, {}}; }
            const auto text = value.toString();
            if ((!description->multiline && (text.contains(u'\n') || text.contains(u'\r'))) ||
                (description->maxLength > 0 && text.toUcs4().size() > description->maxLength)) {
                return {nullptr, message("El texto supera los límites del campo."), {}};
            }
            pdf::PDFObjectFactory factory;
            factory << text;
            fieldValue = factory.takeObject();
        } else if (description->type == PdfFormFieldType::RadioGroup) {
            if (value.metaType().id() != QMetaType::QString || !description->exportValues.contains(value.toString())) { return {nullptr, failure, {}}; }
            fieldValue = pdf::PDFObject::createName(value.toString().toLatin1());
        } else if (description->type == PdfFormFieldType::MultiSelectList) {
            if (value.metaType().id() != QMetaType::QStringList) { return {nullptr, failure, {}}; }
            QSet<QString> seen;
            for (const auto& selected : value.toStringList()) {
                if (!description->exportValues.contains(selected) || seen.contains(selected)) { return {nullptr, message("La selección contiene valores no válidos o duplicados."), {}}; }
                seen.insert(selected);
            }
            QStringList ordered;
            for (const auto& option : description->exportValues) { if (seen.contains(option)) { ordered.append(option); } }
            fieldValue = objectOf(ordered);
        } else if (description->type == PdfFormFieldType::ComboBox) {
            if (value.metaType().id() != QMetaType::QString || !description->exportValues.contains(value.toString())) {
                return {nullptr, message("Elige una opción válida del desplegable."), {}};
            }
            pdf::PDFObjectFactory factory;
            factory << value.toString();
            fieldValue = factory.takeObject();
        } else {
            if (value.metaType().id() != QMetaType::Bool) { return {nullptr, failure, {}}; }
            const auto& widgets = field->getWidgets();
            const auto invoking = std::find_if(widgets.begin(), widgets.end(),
                [widgetReference](const auto& widget) { return widget.getWidget() == widgetReference; });
            if (invoking == widgets.end()) { return {nullptr, failure, {}}; }
            const auto state = value.toBool() ? pdf::PDFFormFieldButton::getOnAppearanceState(&manager, &*invoking) : QByteArray("Off");
            if (state.isEmpty()) { return {nullptr, failure, {}}; }
            fieldValue = pdf::PDFObject::createName(state);
        }
        pdf::PDFDocumentModifier modifier(document_.get());
        auto* builder = modifier.getBuilder();
        builder->setFormManager(&manager);
        bool valid = true;
        QList<pdf::PDFFormField*> related;
        const auto name = field->getName(pdf::PDFFormField::FullyQualified);
        manager.modify([&](pdf::PDFFormField* candidate) {
            if (candidate == field || (!name.isEmpty() && candidate->getName(pdf::PDFFormField::FullyQualified) == name)) {
                related.append(candidate);
            }
        });
        for (auto* candidate : related) {
            if (candidate->getFieldType() != field->getFieldType() ||
                candidate->getFlags().testFlag(pdf::PDFFormField::ReadOnly)) {
                return {nullptr, message("Hay campos vinculados que impiden aplicar el cambio."), {}};
            }
            for (const auto& widget : candidate->getWidgets()) {
                const auto info = describeFormField(*document_, candidate, widget.getWidget(), fieldIndex);
                if (!info || !info->editable || info->type != description->type) { return {nullptr, failure, {}}; }
                if (info->type == PdfFormFieldType::Text &&
                    ((!info->multiline && (value.toString().contains(u'\n') || value.toString().contains(u'\r'))) ||
                     (info->maxLength > 0 && value.toString().toUcs4().size() > info->maxLength))) {
                    return {nullptr, message("El texto supera los límites de un campo vinculado."), {}};
                }
                if (info->type == PdfFormFieldType::ComboBox && !info->exportValues.contains(value.toString())) {
                    return {nullptr, message("Los desplegables vinculados no comparten esta opción."), {}};
                }
                if (info->type == PdfFormFieldType::MultiSelectList) {
                    if (info->exportValues != description->exportValues) { return {nullptr, failure, {}}; }
                    for (const auto& selected : value.toStringList()) { if (!info->exportValues.contains(selected)) { return {nullptr, failure, {}}; } }
                }
                if (info->type == PdfFormFieldType::RadioGroup && !info->exportValues.contains(value.toString())) { return {nullptr, failure, {}}; }
                if (info->type == PdfFormFieldType::CheckBox && value.toBool() &&
                    pdf::PDFFormFieldButton::getOnAppearanceState(&manager, &widget) != fieldValue.getString()) {
                    return {nullptr, message("Las casillas vinculadas tienen estados incompatibles."), {}};
                }
                bool found = false;
                for (size_t index = 0; index < document_->getCatalog()->getPageCount(); ++index) {
                    const auto* widgetPage = document_->getCatalog()->getPage(index);
                    if (std::ranges::find(widgetPage->getAnnotations(), widget.getWidget()) != widgetPage->getAnnotations().end()) {
                        if (found) { return {nullptr, failure, {}}; }
                        valid = valid && prepareAnnotationPage(*builder, *document_, *widgetPage);
                        pdf::PDFObjectFactory widgetUpdate;
                        widgetUpdate.beginDictionary();
                        widgetUpdate.beginDictionaryItem("P");
                        widgetUpdate << widgetPage->getPageReference();
                        widgetUpdate.endDictionaryItem();
                        widgetUpdate.endDictionary();
                        builder->mergeTo(widget.getWidget(), widgetUpdate.takeObject());
                        found = true;
                    }
                }
                if (!found) { return {nullptr, failure, {}}; }
            }
        }
        if (!valid) { return {nullptr, failure, {}}; }
        for (auto* candidate : related) {
            pdf::PDFFormField::SetValueParameters parameters;
            parameters.value = fieldValue;
            parameters.invokingWidget = widgetReference;
            if (description->type == PdfFormFieldType::RadioGroup) {
                bool found = false;
                for (const auto& widget : candidate->getWidgets()) {
                    if (pdf::PDFFormFieldButton::getOnAppearanceState(&manager, &widget) == fieldValue.getString()) { parameters.invokingWidget = widget.getWidget(); found = true; break; }
                }
                if (!found) { return {nullptr, failure, {}}; }
            }
            if (description->type == PdfFormFieldType::MultiSelectList) {
                const auto* choice = dynamic_cast<const pdf::PDFFormFieldChoice*>(candidate);
                if (!choice) { return {nullptr, failure, {}}; }
                for (size_t i = 0; i < choice->getOptions().size(); ++i) { if (value.toStringList().contains(choice->getOptions()[i].exportString)) { parameters.listboxChoices.push_back(static_cast<pdf::PDFInteger>(i)); } }
                if (!parameters.listboxChoices.empty()) { parameters.listboxTopIndex = parameters.listboxChoices.front(); }
            }
            parameters.invokingFormField = candidate;
            parameters.modifier = &modifier;
            parameters.formManager = &manager;
            if (!candidate->setValue(parameters)) { return {nullptr, failure, {}}; }
        }
        if (!modifier.finalize()) {
            return {std::shared_ptr<const PdfDocument>(new PdfDocument(filePath_, document_, sourceHash_)), {}, {}};
        }
        auto edited = std::make_shared<pdf::PDFDocument>(*modifier.getDocument());
        for (auto* candidate : related) {
            for (const auto& widget : candidate->getWidgets()) {
                const auto* widgetDictionary = edited->getDictionaryFromObject(pdf::PDFObject::createReference(widget.getWidget()));
                const auto* appearance = widgetDictionary ? edited->getDictionaryFromObject(widgetDictionary->get("AP")) : nullptr;
                if (!appearance || ((description->type != PdfFormFieldType::CheckBox && description->type != PdfFormFieldType::RadioGroup) && !edited->getObject(appearance->get("N")).isStream())) {
                    return {nullptr, message("No se pudo generar la apariencia del campo."), {}};
                }
            }
        }
        return {std::shared_ptr<const PdfDocument>(new PdfDocument(filePath_, std::move(edited), sourceHash_)), {}, {}};
    } catch (const std::exception&) {
        return {nullptr, failure, {}};
    }
}

PdfOpenResult PdfDocument::createFormField(qsizetype pageIndex, const QRectF& area, const QString& name,
    PdfFormFieldType type, const QStringList& options, bool multiline) const
{
    if (pageSize(pageIndex).isEmpty() || !validArea(area) || name.trimmed().isEmpty() || name.size() > 128 || name.contains('.') ||
        name.contains(QChar::Null) || (type != PdfFormFieldType::Text && type != PdfFormFieldType::CheckBox && type != PdfFormFieldType::ComboBox && type != PdfFormFieldType::RadioGroup && type != PdfFormFieldType::MultiSelectList) ||
        ((type == PdfFormFieldType::ComboBox || type == PdfFormFieldType::MultiSelectList) && (options.isEmpty() || options.size() > 500)) ||
        (type == PdfFormFieldType::RadioGroup && (options.size() < 2 || options.size() > 20))) {
        return {nullptr, message("Revisa el nombre del campo, su tipo, sus opciones y el área."), {}};
    }
    QSet<QString> unique;
    for (const auto& option : options) {
        if (option.trimmed().isEmpty() || option.size() > 256 || option.contains(QChar::Null) || unique.contains(option)) {
            return {nullptr, message("Las opciones deben ser distintas y tener hasta 256 caracteres."), {}};
        }
        unique.insert(option);
    }
    try {
        const std::lock_guard lock(renderMutex_);
        const auto restriction = editingRestriction(*document_);
        if (!restriction.isEmpty()) { return {nullptr, restriction, {}}; }
        const auto form = pdf::PDFForm::parse(document_.get(), document_->getCatalog()->getFormObject());
        bool duplicate = false;
        int count = 0;
        form.apply([&](const pdf::PDFFormField* field) { ++count; duplicate = duplicate || field->getName(pdf::PDFFormField::FullyQualified) == name; });
        if (duplicate || count >= 1000) { return {nullptr, message("El nombre ya existe o se alcanzó el límite de 1000 campos."), {}}; }
        const auto* page = document_->getCatalog()->getPage(static_cast<size_t>(pageIndex));
        const auto rectangle = visibleAreaToPage(*page, area);
        if (rectangle.width() * page->getUserUnit() < 8 || rectangle.height() * page->getUserUnit() < 8) {
            return {nullptr, message("El campo debe medir al menos 8 puntos por lado."), {}};
        }
        pdf::PDFDocumentBuilder builder(document_.get());
        if (!prepareAnnotationPage(builder, *document_, *page)) { throw std::runtime_error("Invalid annotations"); }
        const auto& oldForm = document_->getObject(document_->getCatalog()->getFormObject());
        auto formDictionary = oldForm.isDictionary() ? std::make_shared<pdf::PDFDictionary>(*oldForm.getDictionary()) : std::make_shared<pdf::PDFDictionary>();
        const auto& oldResources = document_->getObject(formDictionary->get("DR"));
        if (!oldResources.isNull() && !oldResources.isDictionary()) { throw std::runtime_error("Invalid form resources"); }
        auto resources = oldResources.isDictionary() ? std::make_shared<pdf::PDFDictionary>(*oldResources.getDictionary()) : std::make_shared<pdf::PDFDictionary>();
        const auto& oldFonts = document_->getObject(resources->get("Font"));
        if (!oldFonts.isNull() && !oldFonts.isDictionary()) { throw std::runtime_error("Invalid form fonts"); }
        auto fonts = oldFonts.isDictionary() ? std::make_shared<pdf::PDFDictionary>(*oldFonts.getDictionary()) : std::make_shared<pdf::PDFDictionary>();
        QByteArray fontName("PdfEditorFormFont");
        for (int i = 0; fonts->hasKey(fontName); ++i) { fontName = "PdfEditorFormFont" + QByteArray::number(i); }
        const auto font = builder.addObject(dictionaryOf({{"Type", pdf::PDFObject::createName("Font")}, {"Subtype", pdf::PDFObject::createName("Type1")},
            {"BaseFont", pdf::PDFObject::createName("Helvetica")}, {"Encoding", pdf::PDFObject::createName("WinAnsiEncoding")}}));
        fonts->setEntry(pdf::PDFInplaceOrMemoryString(fontName), pdf::PDFObject::createReference(font));
        resources->setEntry(pdf::PDFInplaceOrMemoryString("Font"), pdf::PDFObject::createDictionary(std::move(fonts)));
        formDictionary->setEntry(pdf::PDFInplaceOrMemoryString("DR"), pdf::PDFObject::createDictionary(std::move(resources)));
        if (type == PdfFormFieldType::RadioGroup) {
            if (area.height() * pageSize(pageIndex).height() * .75 / options.size() < 12) { return {nullptr, message("Amplía el alto: cada opción necesita al menos 12 puntos."), {}}; }
            const auto group = builder.addObject(dictionaryOf({{"FT", pdf::PDFObject::createName("Btn")}, {"T", objectOf(name)},
                {"Ff", pdf::PDFObject::createInteger((1 << 15) | (1 << 14))}, {"V", pdf::PDFObject::createName("0")}, {"Opt", objectOf(options)}}));
            pdf::PDFObjectReferenceVector kids; auto annotations = page->getAnnotations();
            for (qsizetype i = 0; i < options.size(); ++i) {
                const auto row = visibleAreaToPage(*page, QRectF(area.x(), area.y() + i * area.height() / options.size(), area.width(), area.height() / options.size()));
                const auto appearance = [&](bool checked) {
                    pdf::PDFContentStreamBuilder content(row.size(), pdf::PDFContentStreamBuilder::CoordinateSystem::Qt);
                    auto* painter = content.begin(); if (!painter) { throw std::runtime_error("Cannot draw radio field"); }
                    painter->fillRect(QRectF(QPointF(), row.size()), Qt::white); painter->setPen(QPen(Qt::black, .75));
                    const qreal radius = std::min(5.0, row.height() / 3); const QPointF center(radius + 2, row.height() / 2);
                    painter->drawEllipse(center, radius, radius);
                    if (checked) { painter->setBrush(Qt::black); painter->drawEllipse(center, radius * .5, radius * .5); }
                    QFont label(QStringLiteral("Arial")); label.setPointSizeF(std::min(10.0, row.height() * .6)); painter->setFont(label);
                    painter->drawText(QRectF(radius * 2 + 6, 0, std::max(1.0, row.width() - radius * 2 - 8), row.height()), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, options[i]);
                    const auto generated = content.end(painter); QByteArray commands;
                    const auto decode = [&](const pdf::PDFObject& object) { const auto& stream = generated.document.getObject(object); if (!stream.isStream()) { throw std::runtime_error("Invalid appearance"); } commands += generated.document.getDecodedStream(stream.getStream()); commands += '\n'; };
                    const auto& contents = generated.document.getObject(generated.contents);
                    if (contents.isArray()) { for (const auto& object : *contents.getArray()) { decode(object); } } else { decode(generated.contents); }
                    const auto copied = builder.copyFrom({generated.resources}, generated.document.getStorage(), true).front();
                    return pdf::PDFObject::createReference(addStream(builder, commands, dictionaryOf({{"Type", pdf::PDFObject::createName("XObject")}, {"Subtype", pdf::PDFObject::createName("Form")}, {"BBox", objectOf(QRectF(QPointF(), row.size()))}, {"Resources", copied}})));
                };
                const auto on = QByteArray::number(i);
                const auto widget = builder.addObject(dictionaryOf({{"Type", pdf::PDFObject::createName("Annot")}, {"Subtype", pdf::PDFObject::createName("Widget")},
                    {"Parent", pdf::PDFObject::createReference(group)}, {"P", pdf::PDFObject::createReference(page->getPageReference())}, {"Rect", objectOf(row)}, {"F", pdf::PDFObject::createInteger(4)},
                    {"AS", pdf::PDFObject::createName(i == 0 ? on : QByteArray("Off"))}, {"AP", dictionaryOf({{"N", dictionaryOf({{"Off", appearance(false)}, {on, appearance(true)}})}})}}));
                kids.push_back(widget); annotations.push_back(widget);
            }
            builder.mergeTo(group, dictionaryOf({{"Kids", objectOf(kids)}})); builder.mergeTo(page->getPageReference(), dictionaryOf({{"Annots", objectOf(annotations)}}));
            const auto& oldFields = document_->getObject(formDictionary->get("Fields"));
            pdf::PDFArray fields; if (oldFields.isArray()) { fields = *oldFields.getArray(); } else if (!oldFields.isNull()) { throw std::runtime_error("Invalid form fields"); }
            fields.appendItem(pdf::PDFObject::createReference(group)); formDictionary->setEntry(pdf::PDFInplaceOrMemoryString("Fields"), pdf::PDFObject::createArray(std::make_shared<pdf::PDFArray>(std::move(fields))));
            builder.setCatalogAcroForm(builder.addObject(pdf::PDFObject::createDictionary(std::move(formDictionary))));
            return {std::shared_ptr<const PdfDocument>(new PdfDocument(filePath_, std::make_shared<pdf::PDFDocument>(builder.build()), sourceHash_)), {}, {}};
        }
        const auto width = QByteArray::number(rectangle.width(), 'g', 12);
        const auto height = QByteArray::number(rectangle.height(), 'g', 12);
        const auto appearanceAttributes = dictionaryOf({{"Type", pdf::PDFObject::createName("XObject")}, {"Subtype", pdf::PDFObject::createName("Form")},
            {"BBox", objectOf(QRectF(QPointF(), rectangle.size()))}, {"Resources", dictionaryOf({})}});
        const auto off = addStream(builder, "q 1 1 1 rg 0 0 " + width + ' ' + height + " re f 0 0 0 RG 1 w 0.5 0.5 " +
            QByteArray::number(rectangle.width() - 1) + ' ' + QByteArray::number(rectangle.height() - 1) + " re S Q\n", appearanceAttributes);
        pdf::PDFObject appearance = pdf::PDFObject::createReference(off);
        if (type == PdfFormFieldType::CheckBox) {
            const auto on = addStream(builder, "q 0 0 0 RG 1 w 0.5 0.5 " + QByteArray::number(rectangle.width() - 1) + ' ' +
                QByteArray::number(rectangle.height() - 1) + " re S 2 2 m " + QByteArray::number(rectangle.width() - 2) + ' ' +
                QByteArray::number(rectangle.height() - 2) + " l S 2 " + QByteArray::number(rectangle.height() - 2) + " m " +
                QByteArray::number(rectangle.width() - 2) + " 2 l S Q\n", appearanceAttributes);
            appearance = dictionaryOf({{"Off", pdf::PDFObject::createReference(off)}, {"Yes", pdf::PDFObject::createReference(on)}});
        }
        auto widget = dictionaryOf({{"Type", pdf::PDFObject::createName("Annot")}, {"Subtype", pdf::PDFObject::createName("Widget")},
            {"P", pdf::PDFObject::createReference(page->getPageReference())}, {"Rect", objectOf(rectangle)}, {"F", pdf::PDFObject::createInteger(4)},
            {"T", objectOf(name)}, {"FT", pdf::PDFObject::createName(type == PdfFormFieldType::Text ? "Tx" : type == PdfFormFieldType::CheckBox ? "Btn" : "Ch")},
            {"Ff", pdf::PDFObject::createInteger(type == PdfFormFieldType::ComboBox ? 1 << 17 : type == PdfFormFieldType::MultiSelectList ? 1 << 21 : multiline && type == PdfFormFieldType::Text ? 1 << 12 : 0)},
            {"V", type == PdfFormFieldType::CheckBox ? pdf::PDFObject::createName("Off") : type == PdfFormFieldType::MultiSelectList ? objectOf(QStringList{}) : objectOf(QString{})},
            {"AS", pdf::PDFObject::createName("Off")}, {"AP", dictionaryOf({{"N", appearance}})},
            {"DA", pdf::PDFObject::createString('/' + fontName + " 10 Tf 0 g")},
            {"MK", dictionaryOf({{"BC", objectOf(std::vector<pdf::PDFInteger>{0, 0, 0})}, {"BG", objectOf(std::vector<pdf::PDFInteger>{1, 1, 1})}})}});
        if (type == PdfFormFieldType::ComboBox || type == PdfFormFieldType::MultiSelectList) {
            auto dictionary = std::make_shared<pdf::PDFDictionary>(*widget.getDictionary());
            dictionary->setEntry(pdf::PDFInplaceOrMemoryString("Opt"), objectOf(options));
            widget = pdf::PDFObject::createDictionary(std::move(dictionary));
        }
        const auto reference = builder.addObject(widget);
        auto annotations = page->getAnnotations();
        annotations.push_back(reference);
        builder.mergeTo(page->getPageReference(), dictionaryOf({{"Annots", objectOf(annotations)}}));
        auto fields = document_->getObject(formDictionary->get("Fields"));
        pdf::PDFObjectFactory fieldArray;
        fieldArray.beginArray();
        if (fields.isArray()) { for (const auto& field : *fields.getArray()) { fieldArray << field; } }
        else if (!fields.isNull()) { throw std::runtime_error("Invalid fields"); }
        fieldArray << reference;
        fieldArray.endArray();
        formDictionary->setEntry(pdf::PDFInplaceOrMemoryString("Fields"), fieldArray.takeObject());
        builder.setCatalogAcroForm(builder.addObject(pdf::PDFObject::createDictionary(std::move(formDictionary))));
        auto edited = std::make_shared<pdf::PDFDocument>(builder.build());
        auto result = std::shared_ptr<const PdfDocument>(new PdfDocument(filePath_, std::move(edited), sourceHash_));
        if (type == PdfFormFieldType::ComboBox) { return result->setFormField(pageIndex, static_cast<qsizetype>(annotations.size() - 1), options.front()); }
        if (type == PdfFormFieldType::MultiSelectList) { return result->setFormField(pageIndex, static_cast<qsizetype>(annotations.size() - 1), QStringList{}); }
        return {std::move(result), {}, {}};
    } catch (const std::exception&) { return {nullptr, message("No se pudo crear el campo de formulario."), {}}; }
}

QList<PdfShape> PdfDocument::pageShapes(qsizetype pageIndex) const
{
    QList<PdfShape> result;
    if (pageSize(pageIndex).isEmpty()) { return result; }
    const auto* page = document_->getCatalog()->getPage(static_cast<size_t>(pageIndex));
    const auto size = page->getRotatedCropBox().size();
    const auto matrix = pageToCanvas(*page, size) * QTransform::fromScale(1 / size.width(), 1 / size.height());
    const pdf::PDFDocumentDataLoaderDecorator loader(document_.get());
    const auto& annotations = page->getAnnotations();
    for (size_t i = 0; i < annotations.size(); ++i) {
        const auto* dictionary = document_->getDictionaryFromObject(pdf::PDFObject::createReference(annotations[i]));
        if (!dictionary) { continue; }
        const auto subtype = loader.readNameFromDictionary(dictionary, "Subtype");
        if (subtype != "Square" && subtype != "Circle" && subtype != "Line" && subtype != "Ink") { continue; }
        const auto flags = loader.readIntegerFromDictionary(dictionary, "F", 0);
        if (flags & (pdf::PDFAnnotation::Hidden | pdf::PDFAnnotation::NoView)) { continue; }
        const auto annotation = pdf::PDFAnnotation::parse(&document_->getStorage(), annotations[i]);
        if (!annotation) { continue; }
        auto type = subtype == "Square" ? PdfShapeType::Rectangle : subtype == "Circle" ? PdfShapeType::Ellipse : PdfShapeType::Line;
        if (subtype == "Ink") { type = PdfShapeType::Freehand; }
        const auto& endings = document_->getObject(dictionary->get("LE"));
        if (subtype == "Line" && endings.isArray() && endings.getArray()->getCount() == 2 && endings.getArray()->getItem(1).isName() &&
            endings.getArray()->getItem(1).getString() == "ClosedArrow") { type = PdfShapeType::Arrow; }
        result.append({static_cast<qsizetype>(i), type, matrix.mapRect(annotation->getRectangle()), loader.readTextStringFromDictionary(dictionary, "Contents", {}),
            !(flags & (pdf::PDFAnnotation::ReadOnly | pdf::PDFAnnotation::Locked | pdf::PDFAnnotation::LockedContents))});
    }
    return result;
}

PdfOpenResult PdfDocument::addShape(qsizetype pageIndex, const QRectF& area, PdfShapeType type,
    const QColor& stroke, const QColor& fill, qreal width, const QString& text) const
{
    if (pageSize(pageIndex).isEmpty() || !validArea(area) || !stroke.isValid() || stroke.alpha() != 255 ||
        (fill.isValid() && fill.alpha() != 255) || !std::isfinite(width) || width < 0.5 || width > 20 || text.size() > 4096 ||
        (type != PdfShapeType::Rectangle && type != PdfShapeType::Ellipse && type != PdfShapeType::Line && type != PdfShapeType::Arrow)) {
        return {nullptr, message("Revisa el área, el color y el grosor de 0,5 a 20 puntos."), {}};
    }
    try {
        const std::lock_guard lock(renderMutex_);
        const auto restriction = editingRestriction(*document_);
        if (!restriction.isEmpty()) { return {nullptr, restriction, {}}; }
        const auto* page = document_->getCatalog()->getPage(static_cast<size_t>(pageIndex));
        const auto rectangle = visibleAreaToPage(*page, area);
        pdf::PDFDocumentBuilder builder(document_.get());
        if (!prepareAnnotationPage(builder, *document_, *page)) { throw std::runtime_error("Invalid annotations"); }
        const auto points = width / page->getUserUnit();
        pdf::PDFObjectReference reference;
        if (type == PdfShapeType::Rectangle) { reference = builder.createAnnotationSquare(page->getPageReference(), rectangle, points, fill, stroke, {}, {}, text); }
        else if (type == PdfShapeType::Ellipse) { reference = builder.createAnnotationCircle(page->getPageReference(), rectangle, points, fill, stroke, {}, {}, text); }
        else {
            const auto size = page->getRotatedCropBox().size();
            const auto inverse = pageToCanvas(*page, size).inverted();
            const auto first = inverse.map(QPointF(area.left() * size.width(), area.top() * size.height()));
            const auto last = inverse.map(QPointF(area.right() * size.width(), area.bottom() * size.height()));
            reference = builder.createAnnotationLine(page->getPageReference(), rectangle, first, last, points, stroke, stroke, {}, {}, text,
                pdf::AnnotationLineEnding::None, type == PdfShapeType::Arrow ? pdf::AnnotationLineEnding::ClosedArrow : pdf::AnnotationLineEnding::None);
        }
        if (!reference.isValid()) { throw std::runtime_error("Invalid shape"); }
        builder.mergeTo(reference, dictionaryOf({{"F", pdf::PDFObject::createInteger(4)}}));
        completeHighlightStreamLength(builder, reference);
        auto edited = std::make_shared<pdf::PDFDocument>(builder.build());
        return {std::shared_ptr<const PdfDocument>(new PdfDocument(filePath_, std::move(edited), sourceHash_)), {}, {}};
    } catch (const std::exception&) { return {nullptr, message("No se pudo añadir la forma."), {}}; }
}

PdfOpenResult PdfDocument::addInk(qsizetype pageIndex, const QList<QPolygonF>& strokes, const QColor& color, qreal width) const
{
    qsizetype count = 0;
    if (pageSize(pageIndex).isEmpty() || strokes.isEmpty() || strokes.size() > 32 || !color.isValid() || color.alpha() != 255 ||
        !std::isfinite(width) || width < 0.5 || width > 20) { return {nullptr, message("Revisa la página, los trazos, el color y el grosor."), {}}; }
    for (const auto& stroke : strokes) {
        count += stroke.size();
        if (stroke.size() < 2 || count > 10000) { return {nullptr, message("El dibujo admite hasta 32 trazos y 10000 puntos."), {}}; }
        bool distinct = false;
        for (const auto& point : stroke) {
            if (!std::isfinite(point.x()) || !std::isfinite(point.y()) || !QRectF(0, 0, 1, 1).contains(point)) {
                return {nullptr, message("Los trazos deben quedar dentro de la página visible."), {}};
            }
            distinct = distinct || point != stroke.front();
        }
        if (!distinct) { return {nullptr, message("Dibuja un trazo con al menos dos puntos distintos."), {}}; }
    }
    try {
        const std::lock_guard lock(renderMutex_);
        const auto restriction = editingRestriction(*document_);
        if (!restriction.isEmpty()) { return {nullptr, restriction, {}}; }
        const auto* page = document_->getCatalog()->getPage(static_cast<size_t>(pageIndex));
        const auto size = page->getRotatedCropBox().size();
        bool invertible = false;
        const auto inverse = pageToCanvas(*page, size).inverted(&invertible);
        if (!invertible) { throw std::runtime_error("Invalid ink geometry"); }
        pdf::Polygons converted;
        for (const auto& stroke : strokes) {
            QPolygonF polygon;
            for (const auto& point : stroke) { polygon.append(inverse.map(QPointF(point.x() * size.width(), point.y() * size.height()))); }
            converted.push_back(std::move(polygon));
        }
        pdf::PDFDocumentBuilder builder(document_.get());
        if (!prepareAnnotationPage(builder, *document_, *page)) { throw std::runtime_error("Invalid annotations"); }
        const auto annotation = builder.createAnnotationInk(page->getPageReference(), std::move(converted), width / page->getUserUnit(), color, {}, {}, {});
        if (!annotation.isValid()) { throw std::runtime_error("Invalid ink annotation"); }
        builder.mergeTo(annotation, dictionaryOf({{"F", pdf::PDFObject::createInteger(4)}}));
        completeHighlightStreamLength(builder, annotation);
        auto edited = std::make_shared<pdf::PDFDocument>(builder.build());
        return {std::shared_ptr<const PdfDocument>(new PdfDocument(filePath_, std::move(edited), sourceHash_)), {}, {}};
    } catch (const std::exception&) { return {nullptr, message("No se pudo añadir el dibujo a mano alzada."), {}}; }
}

PdfOpenResult PdfDocument::restyleShape(qsizetype pageIndex, qsizetype index, const QColor& stroke, const QColor& fill, qreal width) const
{
    if (!stroke.isValid() || stroke.alpha() != 255 || (fill.isValid() && fill.alpha() != 255) || !std::isfinite(width) || width < .5 || width > 20) {
        return {nullptr, message("Revisa los colores y el grosor de 0,5 a 20 puntos."), {}};
    }
    try {
        const std::lock_guard lock(renderMutex_);
        const auto restriction = editingRestriction(*document_); if (!restriction.isEmpty()) { return {nullptr, restriction, {}}; }
        const auto shapes = pageShapes(pageIndex);
        const auto selected = std::find_if(shapes.begin(), shapes.end(), [index](const auto& shape) { return shape.index == index && shape.editable; });
        if (selected == shapes.end()) { return {nullptr, message("Selecciona una forma editable de esta página."), {}}; }
        const auto* page = document_->getCatalog()->getPage(static_cast<size_t>(pageIndex));
        const auto reference = page->getAnnotations().at(static_cast<size_t>(index));
        pdf::PDFDocumentBuilder builder(document_.get());
        if (!prepareAnnotationPage(builder, *document_, *page)) { throw std::runtime_error("Invalid annotation page"); }
        builder.mergeTo(reference, dictionaryOf({{"P", pdf::PDFObject::createReference(page->getPageReference())}}));
        builder.setAnnotationColor(reference, stroke);
        builder.setAnnotationBorderStyle(reference, pdf::AnnotationBorderStyle::Solid, width / page->getUserUnit());
        if (selected->type == PdfShapeType::Rectangle || selected->type == PdfShapeType::Ellipse) {
            builder.mergeTo(reference, dictionaryOf({{"IC", fill.isValid() ? objectOf(std::vector<pdf::PDFReal>{fill.redF(), fill.greenF(), fill.blueF()}) : pdf::PDFObject()}}));
        }
        builder.updateAnnotationAppearanceStreams(reference); completeHighlightStreamLength(builder, reference);
        auto result = std::make_shared<pdf::PDFDocument>(builder.build());
        return {std::shared_ptr<const PdfDocument>(new PdfDocument(filePath_, std::move(result), sourceHash_)), {}, {}};
    } catch (const std::exception&) { return {nullptr, message("No se pudo cambiar el estilo de la forma."), {}}; }
}

PdfOpenResult PdfDocument::deleteShape(qsizetype pageIndex, qsizetype index) const
{
    try {
        const std::lock_guard lock(renderMutex_);
        const auto restriction = editingRestriction(*document_);
        if (!restriction.isEmpty()) { return {nullptr, restriction, {}}; }
        const auto shapes = pageShapes(pageIndex);
        if (std::none_of(shapes.begin(), shapes.end(), [index](const auto& shape) { return shape.index == index && shape.editable; })) {
            return {nullptr, message("Selecciona una forma editable de esta página."), {}};
        }
        const auto* page = document_->getCatalog()->getPage(static_cast<size_t>(pageIndex));
        pdf::PDFDocumentBuilder builder(document_.get());
        const auto error = removeAnnotationAndPopup(builder, *document_, page->getPageReference(), page->getAnnotations()[static_cast<size_t>(index)]);
        if (!error.isEmpty()) { return {nullptr, error, {}}; }
        auto edited = std::make_shared<pdf::PDFDocument>(builder.build());
        return {std::shared_ptr<const PdfDocument>(new PdfDocument(filePath_, std::move(edited), sourceHash_)), {}, {}};
    } catch (const std::exception&) { return {nullptr, message("No se pudo eliminar la forma."), {}}; }
}

QList<PdfAttachment> PdfDocument::attachments() const
{
    QList<PdfAttachment> result;
    try {
        const pdf::PDFDocumentDataLoaderDecorator loader(document_.get());
        for (const auto& [key, value] : embeddedEntries(*document_)) {
            const auto* dictionary = document_->getDictionaryFromObject(value);
            auto name = loader.readTextStringFromDictionary(dictionary, "UF", {});
            if (name.isEmpty()) { name = loader.readTextStringFromDictionary(dictionary, "F", {}); }
            const auto specification = pdf::PDFFileSpecification::parse(&document_->getStorage(), value);
            const auto* file = specification.getPlatformFile();
            result.append({key, name, specification.getDescription(), file ? file->getSize() : -1});
        }
    } catch (const std::exception&) { /* A malformed name tree cannot be edited or exported. */ }
    return result;
}

PdfOpenResult PdfDocument::addAttachment(const QString& name, const QByteArray& bytes, const QString& description) const
{
    if (name.trimmed().isEmpty() || name.size() > 256 || name.contains('/') || name.contains('\\') || name.contains(QChar::Null) ||
        bytes.size() > 64 * 1024 * 1024 || description.size() > 4096) { return {nullptr, message("Elige un nombre de archivo válido y un adjunto de hasta 64 MB."), {}}; }
    try {
        const std::lock_guard lock(renderMutex_);
        const auto restriction = editingRestriction(*document_);
        if (!restriction.isEmpty()) { return {nullptr, restriction, {}}; }
        auto entries = embeddedEntries(*document_);
        const auto key = objectOf(name).getString();
        const pdf::PDFDocumentDataLoaderDecorator loader(document_.get());
        for (const auto& [existingKey, value] : entries) {
            const auto* dictionary = document_->getDictionaryFromObject(value);
            if (loader.readTextStringFromDictionary(dictionary, "UF", loader.readTextStringFromDictionary(dictionary, "F", {})) == name || existingKey == key) {
                return {nullptr, message("Ya existe un adjunto con ese nombre."), {}};
            }
        }
        if (entries.size() >= 1000) { return {nullptr, message("Se alcanzó el límite de 1000 adjuntos."), {}}; }
        pdf::PDFDocumentBuilder builder(document_.get());
        const auto stream = addStream(builder, bytes, dictionaryOf({{"Type", pdf::PDFObject::createName("EmbeddedFile")},
            {"Params", dictionaryOf({{"Size", pdf::PDFObject::createInteger(bytes.size())}})}}));
        const auto file = builder.addObject(dictionaryOf({{"Type", pdf::PDFObject::createName("Filespec")}, {"F", objectOf(name)}, {"UF", objectOf(name)},
            {"Desc", objectOf(description)}, {"EF", dictionaryOf({{"F", pdf::PDFObject::createReference(stream)}, {"UF", pdf::PDFObject::createReference(stream)}})}}));
        entries.emplace(key, pdf::PDFObject::createReference(file));
        writeEmbeddedEntries(builder, *document_, entries);
        auto edited = std::make_shared<pdf::PDFDocument>(builder.build());
        return {std::shared_ptr<const PdfDocument>(new PdfDocument(filePath_, std::move(edited), sourceHash_)), {}, {}};
    } catch (const std::exception&) { return {nullptr, message("No se pudo añadir el adjunto. Comprueba la estructura de nombres del PDF."), {}}; }
}

PdfOpenResult PdfDocument::deleteAttachment(const QByteArray& key) const
{
    try {
        const std::lock_guard lock(renderMutex_);
        const auto restriction = editingRestriction(*document_);
        if (!restriction.isEmpty()) { return {nullptr, restriction, {}}; }
        auto entries = embeddedEntries(*document_);
        const auto found = entries.find(key);
        if (found == entries.end()) { return {nullptr, message("Selecciona un adjunto válido."), {}}; }
        const auto* catalog = catalogDictionary(*document_);
        if (catalog && !catalog->get("AF").isNull()) { return {nullptr, message("La retirada de archivos asociados AF aún no está disponible."), {}}; }
        for (const auto& item : document_->getStorage().getObjects()) {
            if (item.object.isDictionary() && !item.object.getDictionary()->get("FS").isNull() && item.object.getDictionary()->get("FS") == found->second) {
                return {nullptr, message("El adjunto se utiliza en una anotación y no se puede retirar desde esta lista."), {}};
            }
        }
        entries.erase(found);
        pdf::PDFDocumentBuilder builder(document_.get());
        writeEmbeddedEntries(builder, *document_, entries);
        auto edited = std::make_shared<pdf::PDFDocument>(builder.build());
        return {std::shared_ptr<const PdfDocument>(new PdfDocument(filePath_, std::move(edited), sourceHash_)), {}, {}};
    } catch (const std::exception&) { return {nullptr, message("No se pudo retirar el adjunto."), {}}; }
}

PdfOutputResult PdfDocument::exportAttachment(const QByteArray& key, const QString& destination) const
{
    try {
        const std::lock_guard lock(renderMutex_);
        if (!canCopyText()) { return {message("El documento no permite copiar su contenido."), {}}; }
        const auto entries = embeddedEntries(*document_);
        const auto found = entries.find(key);
        if (found == entries.end()) { return {message("Selecciona un adjunto válido."), {}}; }
        const auto specification = pdf::PDFFileSpecification::parse(&document_->getStorage(), found->second);
        const auto* file = specification.getPlatformFile();
        if (!file || !file->isValid() || file->getSize() > 64 * 1024 * 1024) { return {message("El adjunto no está integrado o supera 64 MB."), {}}; }
        const auto bytes = document_->getDecodedStream(file->getStream());
        if (bytes.size() > 64 * 1024 * 1024) { return {message("El adjunto supera 64 MB."), {}}; }
        const QFileInfo target(destination);
        if (destination.trimmed().isEmpty() || !target.isNativePath() || target.exists() || !target.dir().exists()) { return {message("Elige un archivo de destino nuevo."), {}}; }
        QTemporaryFile output(target.dir().filePath(QStringLiteral(".pdf-editor-attachment-XXXXXX")));
        if (!output.open() || output.write(bytes) != bytes.size() || !output.flush() || !output.rename(target.absoluteFilePath())) {
            return {message("No se pudo exportar el adjunto."), {}};
        }
        output.setAutoRemove(false);
        return {};
    } catch (const std::exception&) { return {message("No se pudo exportar el adjunto."), {}}; }
}

PdfOutputResult PdfDocument::redactedCopy(const QString& destination, const QList<PdfRedactionArea>& areas,
    int dpi, const std::function<bool()>& cancelled) const
{
    if (areas.isEmpty() || areas.size() > 100 || pageCount() > 50 || (dpi != 150 && dpi != 200 && dpi != 300)) {
        return {message("Selecciona de 1 a 100 zonas en un documento de hasta 50 páginas y una resolución de 150, 200 o 300 ppp."), {}};
    }
    for (const auto& region : areas) {
        if (region.page < 0 || region.page >= pageCount() || !validArea(region.area)) { return {message("Las zonas deben quedar dentro de la página visible."), {}}; }
    }
    const QFileInfo target(destination);
    if (destination.trimmed().isEmpty() || !target.isNativePath() || target.exists() || !target.dir().exists()) {
        return {message("Elige un archivo de destino nuevo para la copia con contenido eliminado."), {}};
    }
    try {
        {
            const std::lock_guard lock(renderMutex_);
            const auto restriction = editingRestriction(*document_);
            if (!restriction.isEmpty()) { return {restriction, {}}; }
        }
        qreal totalPixels = 0;
        for (qsizetype page = 0; page < pageCount(); ++page) {
            const auto size = pageSize(page);
            const auto width = std::ceil(size.width() * dpi / 96.0);
            const auto height = std::ceil(size.height() * dpi / 96.0);
            totalPixels += width * height;
            if (!std::isfinite(totalPixels) || width <= 0 || height <= 0 || width > 8192 || height > 8192 || width * height > 16000000 || totalPixels > 100000000) {
                return {message("El documento supera el límite de 16 millones de píxeles por página o 100 millones en total. Reduce la resolución o extrae primero un intervalo."), {}};
            }
        }
        QTemporaryFile output(target.dir().filePath(QStringLiteral(".pdf-editor-redacted-XXXXXX")));
        if (!output.open()) { return {message("No se pudo crear la copia temporal."), {}}; }
        {
            QPdfWriter writer(&output);
            writer.setResolution(dpi);
            writer.setCreator(QStringLiteral("PDF Editor"));
            writer.setPageMargins(QMarginsF());
            QPainter painter;
            for (qsizetype page = 0; page < pageCount(); ++page) {
                if (cancelled && cancelled()) { return {message("Exportación cancelada. No se publicó ninguna copia."), {}}; }
                const auto rendered = renderPage(page, dpi / 96.0);
                if (rendered.image.isNull() || !rendered.warnings.isEmpty()) {
                    return {message("No se pudo representar íntegramente la página %1. No se creó la copia.").arg(page + 1), rendered.warnings};
                }
                // Only the rendered pixels enter the new file. No source objects,
                // text, annotations, attachments or metadata are copied.
                auto pixels = rendered.image.convertToFormat(QImage::Format_RGB32);
                if (pixels.isNull()) { return {message("No hay memoria para preparar la página."), {}}; }
                {
                    QPainter erase(&pixels);
                    for (const auto& region : areas) {
                        if (region.page != page) { continue; }
                        const int left = static_cast<int>(std::floor(region.area.left() * pixels.width())) - 2;
                        const int top = static_cast<int>(std::floor(region.area.top() * pixels.height())) - 2;
                        const int right = static_cast<int>(std::ceil(region.area.right() * pixels.width())) + 2;
                        const int bottom = static_cast<int>(std::ceil(region.area.bottom() * pixels.height())) + 2;
                        erase.fillRect(QRect(left, top, right - left, bottom - top).intersected(pixels.rect()), Qt::black);
                    }
                }
                const auto points = pageSize(page) * (72.0 / 96.0);
                writer.setPageSize(QPageSize(points, QPageSize::Point, {}, QPageSize::ExactMatch));
                if (page == 0 ? !painter.begin(&writer) : !writer.newPage()) { return {message("No se pudo escribir una página de la copia."), {}}; }
                painter.setRenderHint(QPainter::LosslessImageRendering);
                painter.drawImage(writer.pageLayout().paintRectPixels(dpi), pixels);
            }
            if (!painter.end()) { return {message("No se pudo terminar la copia."), {}}; }
        }
        if (!output.flush()) { return {message("No se pudo completar la copia temporal."), {}}; }
        const auto checked = PdfDocument::open(output.fileName());
        if (!checked.document || checked.document->pageCount() != pageCount() || !checked.document->attachments().isEmpty() || !checked.document->bookmarks().isEmpty()) {
            return {message("La copia no superó la verificación de estructura."), {}};
        }
        for (qsizetype page = 0; page < pageCount(); ++page) {
            if (cancelled && cancelled()) { return {message("Exportación cancelada. No se publicó ninguna copia."), {}}; }
            const auto text = checked.document->selectText(page, {0, 0}, {1, 1});
            const auto difference = checked.document->pageSize(page) - pageSize(page);
            if (!text.error.isEmpty() || !text.match.text.isEmpty() || !checked.document->pageFormFields(page).isEmpty() ||
                qAbs(difference.width()) > 1 || qAbs(difference.height()) > 1) {
                return {message("La copia no superó la verificación de texto, campos o tamaño de página."), {}};
            }
        }
        if (!output.rename(target.absoluteFilePath())) { return {message("No se pudo publicar la copia. Elige un destino libre."), {}}; }
        output.setAutoRemove(false);
        return {};
    } catch (const std::exception&) { return {message("No se pudo crear la copia con contenido eliminado. El original se conserva."), {}}; }
}

PdfOutputResult PdfDocument::protectedCopy(const QString& destination, const QString& userPassword, const QString& ownerPassword,
    bool allowCopy, bool allowPrint) const
{
    const auto validPassword = [](const QString& password) {
        return password.size() >= 8 && password.size() <= 64 && password.toUtf8().size() <= 127 &&
            !std::any_of(password.begin(), password.end(), [](QChar c) { return c.isNull() || c.unicode() < 32; });
    };
    if (!validPassword(userPassword) || !validPassword(ownerPassword) || userPassword == ownerPassword) {
        return {message("Usa dos contraseñas distintas, de 8 a 64 caracteres y hasta 127 bytes UTF-8."), {}};
    }
    const QFileInfo target(destination);
    if (destination.trimmed().isEmpty() || !target.isNativePath() || target.exists() || !target.dir().exists()) {
        return {message("Elige un archivo de destino nuevo para la copia protegida."), {}};
    }
    try {
        const std::lock_guard lock(renderMutex_);
        const auto restriction = editingRestriction(*document_);
        if (!restriction.isEmpty()) { return {restriction, {}}; }
        pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
        settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
        settings.encryptContents = pdf::PDFSecurityHandlerFactory::All;
        settings.userPassword = userPassword;
        settings.ownerPassword = ownerPassword;
        settings.id = document_->getIdPart(0);
        using Permission = pdf::PDFSecurityHandler::Permission;
        settings.permissions = static_cast<uint32_t>(Permission::Accessibility) | static_cast<uint32_t>(Permission::Modify) |
            static_cast<uint32_t>(Permission::ModifyInteractiveItems) | static_cast<uint32_t>(Permission::ModifyFormFields) | static_cast<uint32_t>(Permission::Assemble);
        if (allowCopy) { settings.permissions |= static_cast<uint32_t>(Permission::CopyContent); }
        if (allowPrint) { settings.permissions |= static_cast<uint32_t>(Permission::PrintLowResolution) | static_cast<uint32_t>(Permission::PrintHighResolution); }
        auto handler = pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings);
        if (!handler) { return {message("No se pudo preparar el cifrado con esas contraseñas."), {}}; }
        pdf::PDFDocumentBuilder builder(document_.get());
        builder.setSecurityHandler(handler);
        const auto encrypted = builder.build();
        QTemporaryFile temporary(target.dir().filePath(QStringLiteral(".pdf-editor-protected-XXXXXX")));
        if (!temporary.open()) { return {message("No se pudo crear la copia temporal protegida."), {}}; }
        pdf::PDFDocumentWriter writer(nullptr);
        if (!writer.write(&temporary, &encrypted) || !temporary.flush() || !temporary.seek(0)) { return {message("No se pudo escribir la copia protegida."), {}}; }
        const auto bytes = temporary.readAll();
        if (temporary.error() != QFileDevice::NoError || bytes.size() != temporary.size()) { return {message("No se pudo leer la copia para verificarla."), {}}; }
        const auto verify = [&](const QString& password, bool ownerOnly) {
            bool attempted = false;
            const auto callback = [&](bool* okay) { *okay = !attempted && !password.isEmpty(); attempted = true; return password; };
            pdf::PDFDocumentReader reader(nullptr, callback, false, ownerOnly);
            const auto checked = reader.readFromBuffer(bytes);
            if (password.isEmpty()) { return reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK; }
            if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK || checked.getCatalog()->getPageCount() != document_->getCatalog()->getPageCount()) { return false; }
            const auto* security = checked.getStorage().getSecurityHandler();
            const auto expected = ownerOnly ? pdf::PDFSecurityHandler::AuthorizationResult::OwnerAuthorized : pdf::PDFSecurityHandler::AuthorizationResult::UserAuthorized;
            if (security->getMode() != pdf::EncryptionMode::Standard || security->getAuthorizationResult() != expected) { return false; }
            if (!ownerOnly && (security->isAllowed(Permission::CopyContent) != allowCopy || security->isAllowed(Permission::PrintLowResolution) != allowPrint)) { return false; }
            for (size_t i = 0; i < checked.getCatalog()->getPageCount(); ++i) {
                if (!samePageLayout(*document_->getCatalog()->getPage(i), *checked.getCatalog()->getPage(i))) { return false; }
            }
            return true;
        };
        if (!verify({}, false) || !verify(userPassword, false) || !verify(ownerPassword, true)) {
            return {message("La copia protegida no superó la verificación de contraseñas, permisos o páginas."), {}};
        }
        if (!temporary.rename(target.absoluteFilePath())) { return {message("No se pudo publicar la copia protegida. Elige otro destino libre."), {}}; }
        temporary.setAutoRemove(false);
        return {};
    } catch (const std::exception&) { return {message("No se pudo crear la copia protegida. El original no se ha modificado."), {}}; }
}

PdfOpenResult PdfDocument::optimizedCopy(const QString& destination) const
{
    std::shared_ptr<pdf::PDFDocument> optimized;
    try {
        const std::lock_guard lock(renderMutex_);
        const auto restriction = editingRestriction(*document_);
        if (!restriction.isEmpty()) { return {nullptr, restriction, {}}; }
        pdf::PDFOptimizer optimizer(pdf::PDFOptimizer::RemoveUnusedObjects | pdf::PDFOptimizer::RecompressFlateStreams, nullptr);
        optimizer.setDocument(document_.get());
        optimizer.optimize();
        optimized = std::make_shared<pdf::PDFDocument>(optimizer.takeStorage(), document_->getInfo()->version, QByteArray{});
        if (optimized->getCatalog()->getPageCount() != document_->getCatalog()->getPageCount()) { throw std::runtime_error("Page count changed"); }
        for (size_t i = 0; i < optimized->getCatalog()->getPageCount(); ++i) {
            if (!samePageLayout(*document_->getCatalog()->getPage(i), *optimized->getCatalog()->getPage(i))) { throw std::runtime_error("Page layout changed"); }
        }
    } catch (const std::exception&) { return {nullptr, message("No se pudo optimizar este PDF sin alterar su estructura."), {}}; }
    return PdfDocument({}, std::move(optimized), {}).save(PdfSaveMode::NewCopy, destination);
}

PdfOpenResult PdfDocument::rotatePage(qsizetype pageIndex) const
{
    if (pageIndex < 0 || pageIndex >= pageCount()) {
        return {nullptr, message("Selecciona una página válida para girarla."), {}};
    }
    try {
        const std::lock_guard lock(renderMutex_);
        const auto restriction = editingRestriction(*document_);
        if (!restriction.isEmpty()) { return {nullptr, restriction, {}}; }
        const auto* page = document_->getCatalog()->getPage(static_cast<size_t>(pageIndex));
        pdf::PDFDocumentBuilder builder(document_.get());
        builder.setPageRotation(page->getPageReference(), pdf::getPageRotationRotatedRight(page->getPageRotation()));
        auto edited = std::make_shared<pdf::PDFDocument>(builder.build());
        auto result = std::shared_ptr<const PdfDocument>(new PdfDocument(filePath_, std::move(edited), sourceHash_));
        return {std::move(result), {}, {}};
    } catch (const std::exception&) {
        return {nullptr, message("Ocurrió un error al girar la página."), {}};
    }
}

PdfOpenResult PdfDocument::movePage(qsizetype from, qsizetype to) const
{
    if (from < 0 || to < 0 || from >= pageCount() || to >= pageCount() || from == to) {
        return {nullptr, message("Selecciona una página y una posición distinta dentro del documento."), {}};
    }
    const auto failure = message("No se pudo reordenar el PDF conservando sus páginas.");
    try {
        const std::lock_guard lock(renderMutex_);
        const auto restriction = editingRestriction(*document_);
        if (!restriction.isEmpty()) { return {nullptr, restriction, {}}; }
        const auto* catalog = document_->getCatalog();
        const auto count = catalog->getPageCount();
        std::vector<pdf::PDFObjectReference> references;
        references.reserve(count);
        for (size_t index = 0; index < count; ++index) {
            references.push_back(catalog->getPage(index)->getPageReference());
        }

        pdf::PDFDocumentBuilder builder(document_.get());
        if (builder.getPages() != references) {
            builder.flattenPageTree();
            if (builder.getPages() != references) { return {nullptr, failure, {}}; }
            // An inherited zero must override any rotation inherited from the new parent.
            for (size_t index = 0; index < count; ++index) {
                if (catalog->getPage(index)->getPageRotation() == pdf::PageRotation::None) {
                    builder.setPageRotation(references[index], pdf::PageRotation::None);
                }
            }
        }

        std::vector<size_t> order(count);
        std::iota(order.begin(), order.end(), size_t{0});
        order.erase(order.begin() + from);
        order.insert(order.begin() + to, static_cast<size_t>(from));
        std::vector<pdf::PDFObjectReference> reordered;
        reordered.reserve(count);
        for (const auto index : order) { reordered.push_back(references[index]); }
        builder.setPages(reordered);

        auto edited = std::make_shared<pdf::PDFDocument>(builder.build());
        const auto* editedCatalog = edited->getCatalog();
        if (editedCatalog->getPageCount() != count) { return {nullptr, failure, {}}; }
        for (size_t index = 0; index < count; ++index) {
            const auto* before = catalog->getPage(order[index]);
            const auto* after = editedCatalog->getPage(index);
            if (!samePageLayout(*before, *after)) { return {nullptr, failure, {}}; }
        }
        auto result = std::shared_ptr<const PdfDocument>(new PdfDocument(filePath_, std::move(edited), sourceHash_));
        return {std::move(result), {}, {}};
    } catch (const std::exception&) {
        return {nullptr, failure, {}};
    }
}

PdfOpenResult PdfDocument::changePage(qsizetype pageIndex, PdfPageOperation operation) const
{
    const auto failure = message("No se pudo modificar la organización de las páginas.");
    if (pageSize(pageIndex).isEmpty()) { return {nullptr, message("Selecciona una página válida."), {}}; }
    if (operation == PdfPageOperation::Delete && pageCount() == 1) {
        return {nullptr, message("El documento debe conservar al menos una página."), {}};
    }
    try {
        const std::lock_guard lock(renderMutex_);
        const auto restriction = editingRestriction(*document_);
        if (!restriction.isEmpty()) { return {nullptr, restriction, {}}; }
        const auto* catalog = document_->getCatalog();
        const auto* sourcePage = catalog->getPage(static_cast<size_t>(pageIndex));
        const pdf::PDFDocumentDataLoaderDecorator loader(document_.get());
        pdf::PDFDocumentBuilder builder(document_.get());
        const auto countRestriction = pageCountRestriction(catalogDictionary(*document_));
        if (!countRestriction.isEmpty()) { return {nullptr, countRestriction, {}}; }
        if (operation != PdfPageOperation::InsertBlankAfter) {
            for (const auto reference : sourcePage->getAnnotations()) {
                const auto* dictionary = builder.getDictionaryFromObject(builder.getObjectByReference(reference));
                if (dictionary && loader.readNameFromDictionary(dictionary, "Subtype") == "Widget") {
                    return {nullptr, message("Duplicar o eliminar páginas con campos de formulario aún no está disponible."), {}};
                }
            }
        }
        builder.flattenPageTree();
        std::vector<pdf::PDFObjectReference> references;
        for (size_t index = 0; index < catalog->getPageCount(); ++index) {
            const auto* page = catalog->getPage(index);
            references.push_back(page->getPageReference());
            builder.setPageRotation(page->getPageReference(), page->getPageRotation());
        }
        if (builder.getPages() != references) { return {nullptr, failure, {}}; }

        if (operation == PdfPageOperation::Duplicate) {
            const auto flattened = builder.build();
            const auto clone = duplicatePageObjects(builder, *flattened.getCatalog()->getPage(static_cast<size_t>(pageIndex)));
            references.insert(references.begin() + pageIndex + 1, clone);
        } else if (operation == PdfPageOperation::InsertBlankAfter) {
            const auto size = sourcePage->getRotatedCropBox().size() * sourcePage->getUserUnit();
            const auto blank = builder.appendPage(QRectF(QPointF(), size));
            builder.setPageRotation(blank, pdf::PageRotation::None);
            references.insert(references.begin() + pageIndex + 1, blank);
        } else {
            references.erase(references.begin() + pageIndex);
            if (auto outline = catalog->getOutlineRootPtr()) {
                outline = outline->clone();
                removeOutlineDestination(*outline, sourcePage->getPageReference());
                builder.setOutline(outline.data());
            }
        }
        builder.setPages(references);
        auto edited = std::make_shared<pdf::PDFDocument>(builder.build());
        if (operation == PdfPageOperation::Delete) {
            pdf::PDFOptimizer optimizer(pdf::PDFOptimizer::RemoveUnusedObjects, nullptr);
            optimizer.setDocument(edited.get());
            optimizer.optimize();
            edited = std::make_shared<pdf::PDFDocument>(optimizer.takeOptimizedDocument());
            if (!edited->getStorage().getObject(sourcePage->getPageReference()).isNull()) {
                return {nullptr, message("La página tiene referencias desde otras partes del PDF. Su eliminación requiere actualizar esos vínculos."), {}};
            }
        }
        const auto* result = edited->getCatalog();
        if (result->getPageCount() != references.size()) { return {nullptr, failure, {}}; }
        for (size_t index = 0; index < result->getPageCount(); ++index) {
            const auto* after = result->getPage(index);
            if (after->getPageReference() != references[index]) { return {nullptr, failure, {}}; }
            if (operation != PdfPageOperation::Delete && index == static_cast<size_t>(pageIndex + 1)) {
                if (operation == PdfPageOperation::Duplicate && !samePageGeometry(*sourcePage, *after)) {
                    return {nullptr, failure, {}};
                }
                continue;
            }
            const auto beforeIndex = operation == PdfPageOperation::Delete
                ? index + (index >= static_cast<size_t>(pageIndex) ? 1 : 0)
                : index - (index > static_cast<size_t>(pageIndex) ? 1 : 0);
            const auto* before = catalog->getPage(beforeIndex);
            if (!samePageLayout(*before, *after) || before->getAnnotations() != after->getAnnotations()) {
                return {nullptr, failure, {}};
            }
        }
        return {std::shared_ptr<const PdfDocument>(new PdfDocument(filePath_, std::move(edited), sourceHash_)), {}, {}};
    } catch (const std::exception&) {
        return {nullptr, failure, {}};
    }
}

PdfOpenResult PdfDocument::insertPages(qsizetype position, const QList<PdfPageImport>& sources) const
{
    const auto failure = message("No se pudieron incorporar las páginas de los archivos elegidos.");
    if (position < 0 || position > pageCount() || sources.isEmpty()) { return {nullptr, failure, {}}; }
    struct PreparedSource
    {
        std::shared_ptr<const PdfDocument> source;
        std::unique_ptr<pdf::PDFDocumentBuilder> builder;
        std::vector<pdf::PDFObjectReference> pages;
        qsizetype first;
    };
    try {
        std::vector<PreparedSource> prepared;
        qsizetype importedCount = 0;
        // Prepare sources individually, so concurrent imports never hold two document locks.
        for (const auto& selection : sources) {
            const auto& source = selection.document;
            if (!source) { return {nullptr, failure, {}}; }
            const auto last = selection.last == -1 ? source->pageCount() - 1 : selection.last;
            if (selection.first < 0 || last < selection.first || last >= source->pageCount() ||
                last - selection.first + 1 > std::numeric_limits<int>::max() - pageCount() - importedCount) {
                return {nullptr, message("El intervalo o el número de páginas importadas no es válido."), {}};
            }
            const std::lock_guard sourceLock(source->renderMutex_);
            const auto restriction = editingRestriction(*source->document_);
            if (!restriction.isEmpty()) { return {nullptr, restriction, {}}; }
            const auto* sourceCatalog = catalogDictionary(*source->document_);
            for (const auto* key : {"StructTreeRoot", "Threads", "DPartRoot", "OCProperties", "OutputIntents"}) {
                if (sourceCatalog && !sourceCatalog->get(key).isNull()) {
                    return {nullptr, message("La importación de PDFs con etiquetas, artículos, capas o perfiles de salida aún no está disponible."), {}};
                }
            }
            auto sourceBuilder = std::make_unique<pdf::PDFDocumentBuilder>(source->document_.get());
            sourceBuilder->flattenPageTree();
            std::vector<pdf::PDFObjectReference> selected;
            for (qsizetype index = selection.first; index <= last; ++index) {
                if (source->pageSize(index).isEmpty()) { return {nullptr, failure, {}}; }
                const auto* page = source->document_->getCatalog()->getPage(static_cast<size_t>(index));
                const auto reference = page->getPageReference();
                selected.push_back(reference);
                sourceBuilder->setPageRotation(reference, page->getPageRotation());
                pdf::PDFObjectFactory detach;
                detach.beginDictionary();
                detach.beginDictionaryItem("Parent");
                detach << pdf::PDFObject();
                detach.endDictionaryItem();
                detach.endDictionary();
                sourceBuilder->mergeTo(reference, detach.takeObject());
            }
            const auto objects = pdf::PDFDocumentBuilder::createObjectsFromReferences(selected);
            auto reachable = pdf::PDFObjectUtils::getReferences(objects, *sourceBuilder->getStorage());
            for (const auto reference : reachable) {
                const auto object = sourceBuilder->getObjectByReference(reference);
                sourceBuilder->setObject(reference, prepareImportActions(object, *source->document_));
            }
            reachable = pdf::PDFObjectUtils::getReferences(objects, *sourceBuilder->getStorage());
            const std::set<pdf::PDFObjectReference> selectedSet(selected.begin(), selected.end());
            const pdf::PDFDocumentDataLoaderDecorator loader(sourceBuilder->getStorage());
            for (const auto reference : reachable) {
                const auto* dictionary = sourceBuilder->getDictionaryFromObject(sourceBuilder->getObjectByReference(reference));
                if (!dictionary) { continue; }
                const auto type = loader.readNameFromDictionary(dictionary, "Type");
                if (type == "Catalog" || (type == "Page" && !selectedSet.contains(reference))) {
                    return {nullptr, message("El intervalo conserva referencias a páginas excluidas o al catálogo del origen. Amplía el intervalo o elige otro PDF."), {}};
                }
                if (loader.readNameFromDictionary(dictionary, "Subtype") == "Widget") {
                    return {nullptr, message("Importar páginas con campos de formulario aún no está disponible."), {}};
                }
                if (type == "OCG" || type == "OCMD" || type == "StructElem") {
                    return {nullptr, message("Las páginas dependen de capas o etiquetas que requieren soporte adicional."), {}};
                }
            }
            importedCount += static_cast<qsizetype>(selected.size());
            prepared.push_back({source, std::move(sourceBuilder), std::move(selected), selection.first});
        }
        const std::lock_guard lock(renderMutex_);
        const auto restriction = editingRestriction(*document_);
        if (!restriction.isEmpty()) { return {nullptr, restriction, {}}; }
        const auto countRestriction = pageCountRestriction(catalogDictionary(*document_));
        if (!countRestriction.isEmpty()) { return {nullptr, countRestriction, {}}; }
        const auto* targetCatalog = catalogDictionary(*document_);
        if (targetCatalog && !targetCatalog->get("OutputIntents").isNull()) {
            return {nullptr, message("Importar páginas en un PDF con perfil de salida requiere soporte adicional."), {}};
        }
        pdf::PDFDocumentBuilder builder(document_.get());
        builder.flattenPageTree();
        std::vector<pdf::PDFObjectReference> pages;
        for (size_t index = 0; index < document_->getCatalog()->getPageCount(); ++index) {
            const auto* page = document_->getCatalog()->getPage(index);
            pages.push_back(page->getPageReference());
            builder.setPageRotation(page->getPageReference(), page->getPageRotation());
        }
        if (builder.getPages() != pages) { return {nullptr, failure, {}}; }
        std::vector<pdf::PDFObjectReference> imported;
        for (const auto& source : prepared) {
            const auto copied = builder.copyFrom(pdf::PDFDocumentBuilder::createObjectsFromReferences(source.pages),
                *source.builder->getStorage(), true);
            const auto references = pdf::PDFDocumentBuilder::createReferencesFromObjects(copied);
            imported.insert(imported.end(), references.begin(), references.end());
        }
        if (static_cast<qsizetype>(imported.size()) != importedCount) { return {nullptr, failure, {}}; }
        pages.insert(pages.begin() + position, imported.begin(), imported.end());
        builder.setPages(pages);
        builder.flattenPageTree();
        auto edited = std::make_shared<pdf::PDFDocument>(builder.build());
        const auto* result = edited->getCatalog();
        if (result->getPageCount() != pages.size()) { return {nullptr, failure, {}}; }
        for (qsizetype index = 0; index < static_cast<qsizetype>(pages.size()); ++index) {
            const auto* after = result->getPage(static_cast<size_t>(index));
            if (after->getPageReference() != pages[static_cast<size_t>(index)]) { return {nullptr, failure, {}}; }
            if (index >= position && index < position + importedCount) { continue; }
            const auto beforeIndex = index < position ? index : index - importedCount;
            const auto* before = document_->getCatalog()->getPage(static_cast<size_t>(beforeIndex));
            if (!samePageLayout(*before, *after) || before->getAnnotations() != after->getAnnotations()) {
                return {nullptr, failure, {}};
            }
        }
        qsizetype offset = position;
        for (const auto& source : prepared) {
            for (size_t index = 0; index < source.pages.size(); ++index) {
                const auto* before = source.source->document_->getCatalog()->getPage(static_cast<size_t>(source.first) + index);
                const auto* after = result->getPage(static_cast<size_t>(offset++));
                if (before->getPageRotation() != after->getPageRotation() || before->getCropBox() != after->getCropBox() ||
                    before->getMediaBox() != after->getMediaBox() || before->getUserUnit() != after->getUserUnit() ||
                    before->getAnnotations().size() != after->getAnnotations().size()) {
                    return {nullptr, failure, {}};
                }
            }
        }
        return {std::shared_ptr<const PdfDocument>(new PdfDocument(filePath_, std::move(edited), sourceHash_)), {}, {}};
    } catch (const std::exception&) {
        return {nullptr, failure, {}};
    }
}

PdfOpenResult PdfDocument::cropPages(qsizetype first, qsizetype last, const QMarginsF& margins) const
{
    return editPageCrop(first, last, &margins);
}

PdfOpenResult PdfDocument::resetPageCrop(qsizetype first, qsizetype last) const
{
    return editPageCrop(first, last, nullptr);
}

PdfOpenResult PdfDocument::editPageCrop(qsizetype first, qsizetype last, const QMarginsF* margins) const
{
    const auto failure = message("No se pudo cambiar el área visible de las páginas.");
    if (first < 0 || last < first || last >= pageCount()) {
        return {nullptr, message("Selecciona un intervalo de páginas válido."), {}};
    }
    if (margins) {
        for (const qreal value : {margins->left(), margins->top(), margins->right(), margins->bottom()}) {
            if (!std::isfinite(value) || value < 0 || value >= 1) { return {nullptr, message("Los márgenes no son válidos."), {}}; }
        }
        if (margins->isNull() || margins->left() + margins->right() >= 1 || margins->top() + margins->bottom() >= 1) {
            return {nullptr, message("Indica márgenes que conserven una parte de la página."), {}};
        }
    }
    try {
        const std::lock_guard lock(renderMutex_);
        const auto restriction = editingRestriction(*document_);
        if (!restriction.isEmpty()) { return {nullptr, restriction, {}}; }
        pdf::PDFDocumentBuilder builder(document_.get());
        for (qsizetype index = first; index <= last; ++index) {
            const auto* page = document_->getCatalog()->getPage(static_cast<size_t>(index));
            if (pageSize(index).isEmpty()) { return {nullptr, failure, {}}; }
            auto rectangle = page->getMediaBox();
            if (margins) {
                const auto size = page->getRotatedCropBox().size();
                const auto matrix = pageToCanvas(*page, size) * QTransform::fromScale(1 / size.width(), 1 / size.height());
                bool invertible = false;
                const auto inverse = matrix.inverted(&invertible);
                if (!invertible) { return {nullptr, failure, {}}; }
                const QRectF kept(margins->left(), margins->top(), 1 - margins->left() - margins->right(),
                    1 - margins->top() - margins->bottom());
                rectangle = inverse.mapRect(kept).intersected(page->getCropBox()).intersected(page->getMediaBox());
            }
            if (!rectangle.isValid() || !std::isfinite(rectangle.x()) || !std::isfinite(rectangle.y()) ||
                !std::isfinite(rectangle.width()) || !std::isfinite(rectangle.height()) ||
                rectangle.width() < 0.01 || rectangle.height() < 0.01) { return {nullptr, failure, {}}; }
            pdf::PDFObjectFactory update;
            update.beginDictionary();
            update.beginDictionaryItem("CropBox");
            update << rectangle;
            update.endDictionaryItem();
            update.endDictionary();
            builder.mergeTo(page->getPageReference(), update.takeObject());
        }
        auto edited = std::make_shared<pdf::PDFDocument>(builder.build());
        if (edited->getCatalog()->getPageCount() != static_cast<size_t>(pageCount())) { return {nullptr, failure, {}}; }
        return {std::shared_ptr<const PdfDocument>(new PdfDocument(filePath_, std::move(edited), sourceHash_)), {}, {}};
    } catch (const std::exception&) { return {nullptr, failure, {}}; }
}

PdfOpenResult PdfDocument::extractPages(qsizetype first, qsizetype last, const QString& copyPath) const
{
    if (first < 0 || last < first || last >= pageCount()) {
        return {nullptr, message("Selecciona un intervalo de páginas válido."), {}};
    }
    const auto failure = message("No se pudo extraer el intervalo conservando sus páginas.");
    try {
        std::shared_ptr<pdf::PDFDocument> extracted;
        {
            const std::lock_guard lock(renderMutex_);
            const auto restriction = editingRestriction(*document_);
            if (!restriction.isEmpty()) { return {nullptr, restriction, {}}; }
            const auto* catalog = document_->getCatalog();
            pdf::PDFDocumentBuilder builder(document_.get());
            builder.flattenPageTree();
            std::vector<pdf::PDFObjectReference> selected;
            for (auto index = first; index <= last; ++index) {
                const auto* page = catalog->getPage(static_cast<size_t>(index));
                selected.push_back(page->getPageReference());
                // Materialize zero rotation when the root defines a nonzero default.
                builder.setPageRotation(page->getPageReference(), page->getPageRotation());
            }
            builder.setPages(selected);
            builder.removeOutline();
            builder.removeThreads();
            builder.removeDocumentActions();
            builder.removeStructureTree();
            pdf::PDFObjectFactory catalogUpdate;
            catalogUpdate.beginDictionary();
            catalogUpdate.beginDictionaryItem("PageLabels");
            catalogUpdate << pdf::PDFObject();
            catalogUpdate.endDictionaryItem();
            catalogUpdate.endDictionary();
            builder.mergeTo(builder.getCatalogReference(), catalogUpdate.takeObject());

            const auto subset = builder.build();
            pdf::PDFOptimizer optimizer(pdf::PDFOptimizer::RemoveUnusedObjects, nullptr);
            optimizer.setDocument(&subset);
            optimizer.optimize();
            extracted = std::make_shared<pdf::PDFDocument>(optimizer.takeOptimizedDocument());
            const auto* result = extracted->getCatalog();
            if (result->getPageCount() != selected.size()) { return {nullptr, failure, {}}; }
            for (qsizetype index = 0; index < pageCount(); ++index) {
                const auto* page = catalog->getPage(static_cast<size_t>(index));
                if (index >= first && index <= last) {
                    const auto* included = result->getPage(static_cast<size_t>(index - first));
                    if (!samePageLayout(*page, *included)) { return {nullptr, failure, {}}; }
                } else if (!extracted->getStorage().getObject(page->getPageReference()).isNull()) {
                    return {nullptr, message("El intervalo conserva referencias a otras páginas. Elige un intervalo que también las incluya."), {}};
                }
            }
        }
        return PdfDocument({}, std::move(extracted), {}).save(PdfSaveMode::NewCopy, copyPath);
    } catch (const std::exception&) {
        return {nullptr, failure, {}};
    }
}

PdfMetadata PdfDocument::metadata() const
{
    const auto* info = document_->getInfo();
    return {info->title, info->author, info->subject, info->keywords};
}

PdfOpenResult PdfDocument::setMetadata(const PdfMetadata& values) const
{
    for (const auto& value : {values.title, values.author, values.subject, values.keywords}) {
        if (value.size() > 4096 || value.contains(QChar::Null) || std::any_of(value.begin(), value.end(), [](QChar c) {
                return c.unicode() < 32 && c != '\n' && c != '\r' && c != '\t';
            })) {
            return {nullptr, message("Cada propiedad admite hasta 4096 caracteres de texto."), {}};
        }
    }
    try {
        const std::lock_guard lock(renderMutex_);
        const auto restriction = editingRestriction(*document_);
        if (!restriction.isEmpty()) { return {nullptr, restriction, {}}; }
        QDomDocument xml;
        const QString rdfNs = QStringLiteral("http://www.w3.org/1999/02/22-rdf-syntax-ns#");
        const QString dcNs = QStringLiteral("http://purl.org/dc/elements/1.1/");
        const QString pdfNs = QStringLiteral("http://ns.adobe.com/pdf/1.3/");
        const auto* catalog = catalogDictionary(*document_);
        const auto metadataObject = catalog ? document_->getObject(catalog->get("Metadata")) : pdf::PDFObject();
        if (!metadataObject.isNull()) {
            if (!metadataObject.isStream()) { return {nullptr, message("Los metadatos XMP no son válidos."), {}}; }
            const auto bytes = document_->getDecodedStream(metadataObject.getStream());
            if (bytes.size() > 4 * 1024 * 1024 || !xml.setContent(bytes, QDomDocument::ParseOption::UseNamespaceProcessing)) {
                return {nullptr, message("No se puede actualizar el XMP existente sin perder información. El documento no se ha modificado."), {}};
            }
        } else {
            xml.appendChild(xml.createElementNS(QStringLiteral("adobe:ns:meta/"), QStringLiteral("x:xmpmeta")));
        }
        auto rdfNodes = xml.elementsByTagNameNS(rdfNs, QStringLiteral("RDF"));
        if (rdfNodes.count() > 1) { return {nullptr, message("El XMP contiene varios árboles RDF y no se puede editar."), {}}; }
        QDomElement rdf;
        if (rdfNodes.isEmpty()) {
            if (!metadataObject.isNull()) { return {nullptr, message("El XMP no contiene un árbol RDF válido."), {}}; }
            rdf = xml.createElementNS(rdfNs, QStringLiteral("rdf:RDF"));
            xml.documentElement().appendChild(rdf);
        } else { rdf = rdfNodes.at(0).toElement(); }
        // Remove only the four properties being edited, including attribute forms.
        const auto isEdited = [&](const QDomNode& node) {
            return (node.namespaceURI() == dcNs && (node.localName() == QStringLiteral("title") ||
                node.localName() == QStringLiteral("creator") || node.localName() == QStringLiteral("description"))) ||
                (node.namespaceURI() == pdfNs && node.localName() == QStringLiteral("Keywords"));
        };
        for (auto description = rdf.firstChild(); !description.isNull(); description = description.nextSibling()) {
            if (description.namespaceURI() != rdfNs || description.localName() != QStringLiteral("Description")) { continue; }
            auto element = description.toElement();
            for (auto child = element.firstChild(); !child.isNull();) {
                const auto next = child.nextSibling();
                if (isEdited(child)) { element.removeChild(child); }
                child = next;
            }
            const auto attributes = element.attributes();
            for (int i = attributes.count() - 1; i >= 0; --i) {
                const auto attribute = attributes.item(i);
                if (isEdited(attribute)) { element.removeAttributeNode(attribute.toAttr()); }
            }
        }
        auto description = xml.createElementNS(rdfNs, QStringLiteral("rdf:Description"));
        description.setAttributeNS(rdfNs, QStringLiteral("rdf:about"), QString{});
        rdf.appendChild(description);
        const auto add = [&](const QString& ns, const QString& name, const QString& value, const QString& container) {
            auto property = xml.createElementNS(ns, name);
            description.appendChild(property);
            if (container.isEmpty()) { property.appendChild(xml.createTextNode(value)); return; }
            auto array = xml.createElementNS(rdfNs, QStringLiteral("rdf:") + container);
            auto item = xml.createElementNS(rdfNs, QStringLiteral("rdf:li"));
            if (container == QStringLiteral("Alt")) {
                item.setAttributeNS(QStringLiteral("http://www.w3.org/XML/1998/namespace"), QStringLiteral("xml:lang"), QStringLiteral("x-default"));
            }
            item.appendChild(xml.createTextNode(value));
            array.appendChild(item);
            property.appendChild(array);
        };
        add(dcNs, QStringLiteral("dc:title"), values.title, QStringLiteral("Alt"));
        add(dcNs, QStringLiteral("dc:creator"), values.author, QStringLiteral("Seq"));
        add(dcNs, QStringLiteral("dc:description"), values.subject, QStringLiteral("Alt"));
        add(pdfNs, QStringLiteral("pdf:Keywords"), values.keywords, {});
        pdf::PDFDocumentBuilder builder(document_.get());
        builder.setDocumentTitle(values.title);
        builder.setDocumentAuthor(values.author);
        builder.setDocumentSubject(values.subject);
        builder.setDocumentKeywords(values.keywords);
        builder.setCatalogMetadata(xml.toByteArray());
        auto edited = std::make_shared<pdf::PDFDocument>(builder.build());
        return {std::shared_ptr<const PdfDocument>(new PdfDocument(filePath_, std::move(edited), sourceHash_)), {}, {}};
    } catch (const std::exception&) { return {nullptr, message("No se pudieron actualizar las propiedades."), {}}; }
}

QList<PdfLink> PdfDocument::pageLinks(qsizetype pageIndex) const
{
    QList<PdfLink> links;
    if (pageSize(pageIndex).isEmpty()) { return links; }
    const auto* page = document_->getCatalog()->getPage(static_cast<size_t>(pageIndex));
    const auto size = page->getRotatedCropBox().size();
    const auto normalized = pageToCanvas(*page, size) * QTransform::fromScale(1 / size.width(), 1 / size.height());
    const pdf::PDFDocumentDataLoaderDecorator loader(document_.get());
    const auto& annotations = page->getAnnotations();
    for (size_t i = 0; i < annotations.size(); ++i) {
        try {
            const auto* dictionary = document_->getDictionaryFromObject(pdf::PDFObject::createReference(annotations[i]));
            if (!dictionary || loader.readNameFromDictionary(dictionary, "Subtype") != "Link") { continue; }
            using Annotation = pdf::PDFAnnotation;
            const auto flags = Annotation::Flags::fromInt(static_cast<uint>(loader.readIntegerFromDictionary(dictionary, "F", 0)));
            if (flags.testAnyFlags({Annotation::Hidden, Annotation::NoView})) { continue; }
            PdfLink link;
            link.index = static_cast<qsizetype>(i);
            link.editable = !flags.testAnyFlags({Annotation::ReadOnly, Annotation::Locked, Annotation::LockedContents});
            const auto annotation = pdf::PDFAnnotation::parse(&document_->getStorage(), annotations[i]);
            if (!annotation) { continue; }
            link.area = normalized.mapRect(annotation->getRectangle());
            const auto* action = document_->getDictionaryFromObject(dictionary->get("A"));
            auto destinationObject = dictionary->get("Dest");
            // Additional action chains are never launched by the editor.
            if (action && action->get("Next").isNull()) {
                const auto type = loader.readNameFromDictionary(action, "S");
                if (type == "URI") {
                    const auto value = QString::fromUtf8(loader.readString(action->get("URI")));
                    const QUrl url(value, QUrl::StrictMode);
                    if (url.isValid() && !url.host().isEmpty() && (url.scheme() == QStringLiteral("https") || url.scheme() == QStringLiteral("http")) &&
                        url.userInfo().isEmpty() && value.size() <= 2048 && !value.contains(QChar::Null)) { link.url = url.toString(QUrl::FullyEncoded); }
                } else if (type == "GoTo") { destinationObject = action->get("D"); }
            }
            if (!destinationObject.isNull()) {
                const auto direct = directImportDestination(*document_, destinationObject);
                const auto destination = pdf::PDFDestination::parse(&document_->getStorage(), direct);
                const auto index = document_->getCatalog()->getPageIndexFromPageReference(destination.getPageReference());
                if (index < static_cast<size_t>(pageCount())) { link.targetPage = static_cast<qsizetype>(index); }
            }
            links.append(std::move(link));
        } catch (const std::exception&) { /* Ignore malformed annotations, preserving them in the PDF. */ }
    }
    return links;
}

PdfOpenResult PdfDocument::addLink(qsizetype pageIndex, const QRectF& area, qsizetype targetPage, const QString& address) const
{
    const QUrl url(address, QUrl::StrictMode);
    const bool internal = address.isEmpty() && targetPage >= 0 && targetPage < pageCount();
    const bool web = targetPage == -1 && address.size() <= 2048 && !address.contains(QChar::Null) && url.isValid() &&
        !url.host().isEmpty() && url.userInfo().isEmpty() && (url.scheme() == QStringLiteral("https") || url.scheme() == QStringLiteral("http"));
    if ((!internal && !web) || pageSize(pageIndex).isEmpty() || !area.isValid() ||
        !std::isfinite(area.x()) || !std::isfinite(area.y()) || !std::isfinite(area.width()) || !std::isfinite(area.height()) ||
        !QRectF(0, 0, 1, 1).contains(area)) {
        return {nullptr, message("Elige un área válida y una página de destino o una dirección http/https sin credenciales."), {}};
    }
    try {
        const std::lock_guard lock(renderMutex_);
        const auto restriction = editingRestriction(*document_);
        if (!restriction.isEmpty()) { return {nullptr, restriction, {}}; }
        const auto* page = document_->getCatalog()->getPage(static_cast<size_t>(pageIndex));
        const auto size = page->getRotatedCropBox().size();
        bool invertible = false;
        const auto inverse = pageToCanvas(*page, size).inverted(&invertible);
        if (!invertible) { return {nullptr, message("La geometría de la página no es válida."), {}}; }
        const auto rectangle = inverse.mapRect(QRectF(area.x() * size.width(), area.y() * size.height(), area.width() * size.width(), area.height() * size.height()));
        pdf::PDFDocumentBuilder builder(document_.get());
        if (!prepareAnnotationPage(builder, *document_, *page)) { return {nullptr, message("No se pudieron preparar las anotaciones."), {}}; }
        const auto action = internal ? builder.createActionGoTo(pdf::PDFDestination::createFit(document_->getCatalog()->getPage(static_cast<size_t>(targetPage))->getPageReference()))
            : builder.createActionURI(url.toString(QUrl::FullyEncoded));
        const auto annotation = builder.createAnnotationLink(page->getPageReference(), rectangle, action, pdf::LinkHighlightMode::Invert);
        if (!annotation.isValid()) { return {nullptr, message("No se pudo añadir el enlace."), {}}; }
        builder.setAnnotationBorder(annotation, 0, 0, 1 / page->getUserUnit());
        builder.setAnnotationColor(annotation, QColor(30, 90, 200));
        auto edited = std::make_shared<pdf::PDFDocument>(builder.build());
        return {std::shared_ptr<const PdfDocument>(new PdfDocument(filePath_, std::move(edited), sourceHash_)), {}, {}};
    } catch (const std::exception&) { return {nullptr, message("No se pudo añadir el enlace."), {}}; }
}

PdfOpenResult PdfDocument::deleteLink(qsizetype pageIndex, qsizetype linkIndex) const
{
    try {
        const std::lock_guard lock(renderMutex_);
        const auto restriction = editingRestriction(*document_);
        if (!restriction.isEmpty()) { return {nullptr, restriction, {}}; }
        const auto links = pageLinks(pageIndex);
        const auto found = std::find_if(links.begin(), links.end(), [linkIndex](const auto& link) { return link.index == linkIndex && link.editable; });
        if (found == links.end()) { return {nullptr, message("Elige un enlace editable de esta página."), {}}; }
        const auto* page = document_->getCatalog()->getPage(static_cast<size_t>(pageIndex));
        pdf::PDFDocumentBuilder builder(document_.get());
        const auto error = removeAnnotationAndPopup(builder, *document_, page->getPageReference(), page->getAnnotations()[static_cast<size_t>(linkIndex)]);
        if (!error.isEmpty()) { return {nullptr, error, {}}; }
        auto edited = std::make_shared<pdf::PDFDocument>(builder.build());
        return {std::shared_ptr<const PdfDocument>(new PdfDocument(filePath_, std::move(edited), sourceHash_)), {}, {}};
    } catch (const std::exception&) { return {nullptr, message("No se pudo eliminar el enlace."), {}}; }
}

PdfOpenResult PdfDocument::insertText(qsizetype pageIndex, const QRectF& area, const QString& text, qreal pointSize) const
{
    PdfTextStyle style;
    style.pointSize = pointSize;
    return insertStyledText(pageIndex, area, text, style);
}

PdfOpenResult PdfDocument::insertStyledText(qsizetype pageIndex, const QRectF& area, const QString& text, const PdfTextStyle& style) const
{
    PdfPageDecoration decoration;
    decoration.text = text;
    decoration.area = area;
    decoration.style = style;
    return appendPageOverlays(pageIndex, pageIndex, decoration);
}

PdfOpenResult PdfDocument::insertImage(qsizetype pageIndex, const QRectF& area, const QImage& image) const
{
    if (image.isNull() || image.width() > 8192 || image.height() > 8192 || qint64(image.width()) * image.height() > 16000000) {
        return {nullptr, message("La imagen debe tener hasta 8192 píxeles por lado y 16 millones de píxeles."), {}};
    }
    PdfPageDecoration decoration;
    decoration.area = area;
    return appendPageOverlays(pageIndex, pageIndex, decoration, image);
}

PdfOpenResult PdfDocument::decoratePages(qsizetype first, qsizetype last, const PdfPageDecoration& decoration) const
{
    return appendPageOverlays(first, last, decoration);
}

PdfOpenResult PdfDocument::addOcrLayer(const QList<PdfOcrPage>& pages) const
{
    if (pages.isEmpty() || pages.size() > 50 || isEncrypted()) { return {nullptr, message("Elige resultados OCR de hasta 50 páginas sin cifrado."), {}}; }
    qsizetype first = pageCount();
    qsizetype last = -1;
    qsizetype words = 0;
    QSet<qsizetype> seen;
    for (const auto& page : pages) {
        if (page.page < 0 || page.page >= pageCount() || seen.contains(page.page) || page.words.isEmpty() || page.words.size() > 10000 ||
            !std::isfinite(page.angle) || qAbs(page.angle) > 180) { return {nullptr, message("Los resultados OCR no son válidos."), {}}; }
        seen.insert(page.page);
        first = std::min(first, page.page); last = std::max(last, page.page);
        const auto selection = selectText(page.page, {0, 0}, {1, 1});
        if (!selection.error.isEmpty() || !selection.match.text.trimmed().isEmpty() || !pageFormFields(page.page).isEmpty()) {
            return {nullptr, message("El OCR solo se añade a páginas sin capa de texto ni campos interactivos."), {}};
        }
        for (const auto& word : page.words) {
            if (++words > 50000 || word.text.trimmed().isEmpty() || word.text.size() > 512 || !validArea(word.area) || word.text.contains(QChar::Null)) {
                return {nullptr, message("El texto o las posiciones OCR no son válidos."), {}};
            }
        }
    }
    if (last - first >= 50) { return {nullptr, message("El intervalo OCR está limitado a 50 páginas."), {}}; }
    PdfPageDecoration decoration;
    decoration.area = QRectF(0, 0, 1, 1);
    return appendPageOverlays(first, last, decoration, {}, pages);
}

PdfOpenResult PdfDocument::appendPageOverlays(qsizetype first, qsizetype last, const PdfPageDecoration& decoration,
    const QImage& image, const QList<PdfOcrPage>& ocrPages) const
{
    const auto failure = message("No se pudo insertar el contenido en esta página.");
    const auto& area = decoration.area;
    const auto& style = decoration.style;
    const auto alignment = style.alignment;
    if (first < 0 || last < first || last >= pageCount() || last - first >= 500) {
        return {nullptr, message("Selecciona un intervalo válido de hasta 500 páginas."), {}};
    }
    if (ocrPages.isEmpty() && image.isNull() && (decoration.text.trimmed().isEmpty() || decoration.text.size() > 10000 ||
        !std::isfinite(style.pointSize) || style.pointSize < 4 || style.pointSize > 144 ||
        style.family.trimmed().isEmpty() || style.family.size() > 128 || !style.color.isValid() || style.color.alpha() != 255 ||
        (alignment != Qt::AlignLeft && alignment != Qt::AlignHCenter && alignment != Qt::AlignRight))) {
        return {nullptr, message("Revisa el texto, la fuente, el color y el tamaño de 4 a 144 puntos."), {}};
    }
    if (!std::isfinite(decoration.opacity) || decoration.opacity <= 0 || decoration.opacity > 1 ||
        !std::isfinite(decoration.rotation) || decoration.rotation < -180 || decoration.rotation > 180 ||
        decoration.firstNumber < 1 || decoration.firstNumber > 1000000000) {
        return {nullptr, message("Revisa la opacidad, el giro y el número inicial."), {}};
    }
    if (!area.isValid() || !std::isfinite(area.x()) || !std::isfinite(area.y()) ||
        !std::isfinite(area.width()) || !std::isfinite(area.height()) || !QRectF(0, 0, 1, 1).contains(area)) {
        return {nullptr, message("El área de inserción debe quedar dentro de la página visible."), {}};
    }
    try {
        const std::lock_guard lock(renderMutex_);
        const auto restriction = editingRestriction(*document_);
        if (!restriction.isEmpty()) { return {nullptr, restriction, {}}; }
        pdf::PDFDocumentBuilder builder(document_.get());
        for (qsizetype pageIndex = first; pageIndex <= last; ++pageIndex) {
            const auto ocr = std::find_if(ocrPages.begin(), ocrPages.end(), [pageIndex](const auto& page) { return page.page == pageIndex; });
            if (!ocrPages.isEmpty() && ocr == ocrPages.end()) { continue; }
            if (pageSize(pageIndex).isEmpty()) { return {nullptr, failure, {}}; }
            const auto* page = document_->getCatalog()->getPage(static_cast<size_t>(pageIndex));
            const auto size = page->getRotatedCropBox().size();
            bool invertible = false;
            const auto toPage = pageToCanvas(*page, size).inverted(&invertible);
            if (!invertible) { return {nullptr, failure, {}}; }
            const QRectF box(area.x() * size.width(), area.y() * size.height(), area.width() * size.width(), area.height() * size.height());
            pdf::PDFContentStreamBuilder content(size, pdf::PDFContentStreamBuilder::CoordinateSystem::Qt);
            auto* painter = content.begin();
            if (!painter) { return {nullptr, failure, {}}; }
            painter->setOpacity(decoration.opacity);
            bool fits = true;
            if (ocr != ocrPages.end()) {
                painter->setPen(Qt::black);
                painter->translate(size.width() / 2, size.height() / 2);
                painter->rotate(ocr->angle);
                painter->translate(-size.width() / 2, -size.height() / 2);
                for (const auto& word : ocr->words) {
                    const QRectF wordBox(word.area.x() * size.width(), word.area.y() * size.height(), word.area.width() * size.width(), word.area.height() * size.height());
                    QFont font(QStringLiteral("Arial"));
                    font.setPointSizeF(std::max(0.01, wordBox.height() * 0.8));
                    const auto textWidth = QFontMetricsF(font, painter->device()).horizontalAdvance(word.text);
                    if (textWidth <= 0) { fits = false; break; }
                    // Scale the text transform rather than stretching the font: the
                    // native Windows font backend otherwise leaves gaps between
                    // glyphs in the PDF text extraction geometry.
                    painter->setFont(font);
                    painter->save();
                    painter->translate(wordBox.topLeft());
                    painter->scale(wordBox.width() / textWidth, 1);
                    painter->drawText(QRectF(0, 0, textWidth, wordBox.height()), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine | Qt::TextDontClip, word.text);
                    painter->restore();
                }
            } else if (image.isNull()) {
                auto text = decoration.text;
                if (decoration.numbering) {
                    text.replace(QStringLiteral("{n}"), QString::number(qint64(decoration.firstNumber) + pageIndex - first));
                    text.replace(QStringLiteral("{total}"), QString::number(last - first + 1));
                }
                QFont font(style.family);
                font.setPointSizeF(style.pointSize / page->getUserUnit());
                font.setBold(style.bold);
                font.setItalic(style.italic);
                font.setUnderline(style.underline);
                painter->setFont(font);
                painter->setPen(style.color);
                const int flags = alignment | (decoration.rotation == 0 ? Qt::AlignTop : Qt::AlignVCenter) | Qt::TextWordWrap;
                const QRectF localBox(-box.width() / 2, -box.height() / 2, box.width(), box.height());
                const auto bounds = QFontMetricsF(font, painter->device()).boundingRect(localBox, flags, text);
                QTransform rotation;
                rotation.rotate(decoration.rotation);
                const auto rotatedBounds = rotation.mapRect(bounds).translated(box.center());
                fits = bounds.width() <= box.width() + 0.01 && bounds.height() <= box.height() + 0.01 &&
                    QRectF(QPointF(), size).adjusted(-0.01, -0.01, 0.01, 0.01).contains(rotatedBounds);
                if (fits) {
                    painter->translate(box.center());
                    painter->rotate(decoration.rotation);
                    painter->drawText(localBox, flags, text);
                }
            } else {
                auto imageSize = QSizeF(image.size());
                imageSize.scale(box.size(), Qt::KeepAspectRatio);
                painter->drawImage(QRectF(box.center() - QPointF(imageSize.width() / 2, imageSize.height() / 2), imageSize), image);
            }
            const auto generated = content.end(painter);
            if (!fits) { return {nullptr, message("El texto no cabe en la página %1. Amplía el área o reduce el tamaño de letra.").arg(pageIndex + 1), {}}; }
            if (!generated.contents.isReference() && !generated.contents.isStream() && !generated.contents.isArray()) { return {nullptr, failure, {}}; }
            QByteArray commands;
            const auto decode = [&](const pdf::PDFObject& object) {
                const auto& resolved = generated.document.getObject(object);
                if (!resolved.isStream()) { throw std::runtime_error("Invalid overlay stream"); }
                commands += generated.document.getDecodedStream(resolved.getStream());
                commands += '\n';
            };
            const auto& generatedContents = generated.document.getObject(generated.contents);
            if (generatedContents.isArray()) { for (const auto& object : *generatedContents.getArray()) { decode(object); } }
            else { decode(generated.contents); }
            if (ocr != ocrPages.end()) {
                if (!commands.contains("BT\n")) { return {nullptr, message("No se pudo generar una capa de texto OCR buscable."), {}}; }
                commands.replace("BT\n", "BT\n3 Tr\n");
            }
            const auto resources = builder.copyFrom({generated.resources}, generated.document.getStorage(), true).front();
            // Qt omits this optional value on 1-bit masks. Materialize the PDF default
            // because PDF4QT reads explicit image masks through its grayscale path.
            const auto* importedResources = builder.getDictionaryFromObject(resources);
            const auto* importedImages = importedResources ? builder.getDictionaryFromObject(importedResources->get("XObject")) : nullptr;
            if (importedImages) {
                const pdf::PDFDocumentDataLoaderDecorator loader(builder.getStorage());
                for (size_t i = 0; i < importedImages->getCount(); ++i) {
                    const auto& importedImage = builder.getObject(importedImages->getValue(i));
                    if (!importedImage.isStream()) { continue; }
                    const auto mask = importedImage.getStream()->getDictionary()->get("Mask");
                    if (!mask.isReference()) { continue; }
                    const auto maskObject = builder.getObject(mask);
                    if (!maskObject.isStream()) { continue; }
                    const auto* dictionary = maskObject.getStream()->getDictionary();
                    if (!loader.readBooleanFromDictionary(dictionary, "ImageMask", false) || !dictionary->get("BitsPerComponent").isNull()) { continue; }
                    auto updated = *dictionary;
                    updated.setEntry(pdf::PDFInplaceOrMemoryString("BitsPerComponent"), pdf::PDFObject::createInteger(1));
                    builder.setObject(mask.getReference(), pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(
                        std::move(updated), QByteArray(*maskObject.getStream()->getContent()))));
                }
            }
            pdf::PDFObjectFactory form;
            form.beginDictionary();
            form.beginDictionaryItem("Type"); form << pdf::WrapName("XObject"); form.endDictionaryItem();
            form.beginDictionaryItem("Subtype"); form << pdf::WrapName("Form"); form.endDictionaryItem();
            form.beginDictionaryItem("BBox"); form << QRectF(QPointF(), size); form.endDictionaryItem();
            form.beginDictionaryItem("Resources"); form << resources; form.endDictionaryItem();
            form.beginDictionaryItem("Length"); form << pdf::PDFInteger(commands.size()); form.endDictionaryItem();
            form.endDictionary();
            auto formObject = form.takeObject();
            const auto formReference = builder.addObject(pdf::PDFObject::createStream(
                std::make_shared<pdf::PDFStream>(pdf::PDFDictionary(*formObject.getDictionary()), std::move(commands))));
            const auto stream = [&builder](QByteArray bytes) {
                pdf::PDFDictionary dictionary;
                dictionary.setEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(bytes.size()));
                return builder.addObject(pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(std::move(dictionary), std::move(bytes))));
            };
            const auto& oldResources = document_->getObject(page->getResources());
            if (!oldResources.isNull() && !oldResources.isDictionary()) { return {nullptr, failure, {}}; }
            auto pageResources = oldResources.isDictionary() ? std::make_shared<pdf::PDFDictionary>(*oldResources.getDictionary()) : std::make_shared<pdf::PDFDictionary>();
            const auto& oldXObjects = document_->getObject(pageResources->get("XObject"));
            if (!oldXObjects.isNull() && !oldXObjects.isDictionary()) { return {nullptr, failure, {}}; }
            auto xObjects = oldXObjects.isDictionary() ? std::make_shared<pdf::PDFDictionary>(*oldXObjects.getDictionary()) : std::make_shared<pdf::PDFDictionary>();
            QByteArray name("PdfEditorOverlay");
            for (int i = 0; xObjects->hasKey(name); ++i) { name = "PdfEditorOverlay" + QByteArray::number(i); }
            xObjects->setEntry(pdf::PDFInplaceOrMemoryString(name), pdf::PDFObject::createReference(formReference));
            pageResources->setEntry(pdf::PDFInplaceOrMemoryString("XObject"), pdf::PDFObject::createDictionary(std::move(xObjects)));
            const auto matrix = QTransform(1, 0, 0, -1, 0, size.height()) * toPage;
            const auto number = [](qreal value) { return QByteArray::number(value, 'g', 16); };
            QByteArray suffix("\nQ\nq\n");
            for (const qreal value : {matrix.m11(), matrix.m12(), matrix.m21(), matrix.m22(), matrix.dx(), matrix.dy()}) {
                if (!std::isfinite(value)) { return {nullptr, failure, {}}; }
                suffix += number(value) + ' ';
            }
            suffix += "cm\n/" + name + " Do\nQ\n";
            std::vector<pdf::PDFObjectReference> contents{stream("q\n")};
            const auto& previous = document_->getObject(page->getContents());
            if (previous.isArray()) {
                for (const auto& object : *previous.getArray()) {
                    if (!object.isReference() || !document_->getObject(object).isStream()) { return {nullptr, failure, {}}; }
                    contents.push_back(object.getReference());
                }
            } else if (previous.isStream()) {
                contents.push_back(page->getContents().isReference() ? page->getContents().getReference() : builder.addObject(previous));
            } else if (!previous.isNull()) { return {nullptr, failure, {}}; }
            const auto overlayStream = stream(std::move(suffix));
            if (decoration.behind) {
                std::vector<pdf::PDFObjectReference> ordered{stream("q\n"), overlayStream, stream("q\n")};
                ordered.insert(ordered.end(), contents.begin() + 1, contents.end());
                ordered.push_back(stream("\nQ\n"));
                contents = std::move(ordered);
            } else { contents.push_back(overlayStream); }
            pdf::PDFObjectFactory update;
            update.beginDictionary();
            update.beginDictionaryItem("Resources"); update << pdf::PDFObject::createDictionary(std::move(pageResources)); update.endDictionaryItem();
            update.beginDictionaryItem("Contents"); update << contents; update.endDictionaryItem();
            update.endDictionary();
            builder.mergeTo(page->getPageReference(), update.takeObject());
        }
        auto edited = std::make_shared<pdf::PDFDocument>(builder.build());
        return {std::shared_ptr<const PdfDocument>(new PdfDocument(filePath_, std::move(edited), sourceHash_)), {}, {}};
    } catch (const std::exception&) { return {nullptr, failure, {}}; }
}

PdfOutputResult PdfDocument::splitPages(qsizetype pagesPerPart, const QString& outputDirectory) const
{
    if (pagesPerPart < 1 || pagesPerPart > pageCount()) { return {message("Indica un número válido de páginas por archivo."), {}}; }
    const auto parts = (pageCount() - 1) / pagesPerPart + 1;
    if (parts > 1000) { return {message("La división está limitada a 1000 archivos. Aumenta las páginas por archivo."), {}}; }
    const QFileInfo target(outputDirectory);
    if (outputDirectory.trimmed().isEmpty() || !target.isNativePath() || target.fileName().isEmpty()
#ifdef Q_OS_WIN
        || target.fileName().contains(u':')
#endif
    ) { return {message("Elige una carpeta nueva para los archivos divididos."), {}}; }
    if (target.exists() || target.isSymLink() || !target.dir().exists()) {
        return {message("La carpeta de salida debe ser nueva y su carpeta contenedora debe existir."), {}};
    }
    QTemporaryDir staged(target.dir().absoluteFilePath(QStringLiteral(".pdf-editor-split-XXXXXX")));
    if (!staged.isValid()) { return {message("No se pudo preparar la carpeta temporal de división."), {}}; }
    for (qsizetype part = 0; part < parts; ++part) {
        const auto first = part * pagesPerPart;
        const auto last = std::min(pageCount() - 1, first + pagesPerPart - 1);
        const auto name = QStringLiteral("parte-%1-paginas-%2-%3.pdf").arg(part + 1, 4, 10, QLatin1Char('0')).arg(first + 1).arg(last + 1);
        const auto result = extractPages(first, last, QDir(staged.path()).filePath(name));
        if (!result.document) { return {message("No se pudo preparar la parte %1. %2").arg(part + 1).arg(result.error), result.warnings}; }
    }
    if (!QDir().rename(staged.path(), target.absoluteFilePath())) {
        return {message("No se pudo publicar la carpeta dividida. Comprueba el destino y el espacio disponible."), {}};
    }
    staged.setAutoRemove(false);
    return {};
}

PdfOpenResult PdfDocument::save(PdfSaveMode mode, const QString& copyPath) const
{
    const bool original = mode == PdfSaveMode::Original;
    const auto destination = original ? filePath_ : copyPath;
    const QFileInfo target(destination);
    if (destination.isEmpty() || !target.isNativePath() || target.fileName().isEmpty()
#ifdef Q_OS_WIN
        || target.fileName().contains(u':')
#endif
    ) {
        return {nullptr, message("Elige un nombre de archivo válido para guardar el PDF."), {}};
    }
    if (!original && (target.exists() || target.isSymLink())) {
        return {nullptr, message("Ya existe un archivo o carpeta con ese nombre. Elige un nombre nuevo para la copia."), {}};
    }

    try {
        const auto path = target.absoluteFilePath();
        const auto conflict = message("El archivo abierto cambió o no se puede reemplazar. Usa Guardar como para conservar los cambios.");
        if (original && !sourceStillMatches(path, sourceHash_)) { return {nullptr, conflict, {}}; }
        QTemporaryFile temporary(target.dir().absoluteFilePath(QStringLiteral(".pdf-editor-XXXXXX.tmp")));
        if (!temporary.open()) {
            return {nullptr, message("No se pudo crear el archivo temporal. Comprueba los permisos de la carpeta."), {}};
        }
        {
            const std::lock_guard lock(renderMutex_);
            const auto restriction = editingRestriction(*document_);
            if (!restriction.isEmpty()) { return {nullptr, restriction, {}}; }
            pdf::PDFDocumentWriter writer(nullptr);
            if (!writer.write(&temporary, document_.get()) || temporary.error() != QFileDevice::NoError ||
                !temporary.flush() || !temporary.seek(0)) {
                return {nullptr, message("No se pudo escribir el PDF. Comprueba los permisos y el espacio disponible."), {}};
            }
        }

        const auto bytes = temporary.readAll();
        const auto invalid = message("No se pudo verificar el PDF guardado.");
        if (temporary.error() != QFileDevice::NoError || bytes.size() != temporary.size()) {
            return {nullptr, invalid, {}};
        }
        const auto cancelPassword = [](bool* accepted) { *accepted = false; return QString{}; };
        pdf::PDFDocumentReader reader(nullptr, cancelPassword, false, false);
        auto verified = std::make_shared<pdf::PDFDocument>(reader.readFromBuffer(bytes));
        const auto* catalog = document_->getCatalog();
        const auto* savedCatalog = verified->getCatalog();
        if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK ||
            savedCatalog->getPageCount() != catalog->getPageCount()) {
            return {nullptr, invalid + u' ' + reader.getErrorMessage(), {}};
        }
        for (size_t index = 0; index < catalog->getPageCount(); ++index) {
            const auto* savedPage = savedCatalog->getPage(index);
            const auto* page = catalog->getPage(index);
            if (savedPage->getPageReference() != page->getPageReference() ||
                savedPage->getPageRotation() != page->getPageRotation()) {
                return {nullptr, invalid, {}};
            }
        }

        const auto verifiedHash = verified->getSourceDataHash();
        auto saved = std::shared_ptr<const PdfDocument>(new PdfDocument(path, std::move(verified), verifiedHash));
        if (original) {
            QSaveFile output(path);
            output.setDirectWriteFallback(false);
            const auto writeError = message("No se pudo guardar el PDF. Comprueba los permisos y el espacio disponible.");
            if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size()) {
                return {nullptr, writeError, {}};
            }
            if (!sourceStillMatches(path, sourceHash_)) { return {nullptr, conflict, {}}; }
            if (!output.commit()) { return {nullptr, writeError, {}}; }
        } else {
            if (!temporary.rename(path)) {
                return {nullptr, message("No se pudo guardar la copia con ese nombre. Elige un destino libre y con permiso de escritura."), {}};
            }
            temporary.setAutoRemove(false);
        }
        return {std::move(saved), {}, reader.getWarnings()};
    } catch (const std::exception&) {
        return {nullptr, message("Ocurrió un error al guardar el PDF."), {}};
    }
}

PdfOpenResult PdfDocument::recover(const QString& snapshotPath, const QString& originalPath, const QByteArray& originalHash)
{
    if (!QFileInfo(originalPath).isAbsolute() || originalHash.size() != 32) {
        return {nullptr, message("Los datos del documento recuperado no son válidos."), {}};
    }
    auto opened = open(snapshotPath);
    if (!opened.document) { return opened; }
    opened.document = std::shared_ptr<const PdfDocument>(new PdfDocument(QFileInfo(originalPath).absoluteFilePath(),
        opened.document->document_, originalHash));
    return opened;
}

bool PdfDocument::isEncrypted() const
{
    return document_->getStorage().getSecurityHandler()->getMode() != pdf::EncryptionMode::None;
}

namespace {
pdf::PDFEditedPageContent readEditableContent(pdf::PDFDocument& document, qsizetype index)
{
    const auto* catalog = document.getDictionaryFromObject(document.getStorage().getTrailerDictionary().getDictionary()->get("Root"));
    if (catalog && (!catalog->get("OCProperties").isNull() || !catalog->get("StructTreeRoot").isNull())) { throw std::runtime_error("Layered or tagged documents are not supported by this editor"); }
    pdf::PDFOptionalContentActivity optional(&document, pdf::OCUsage::View, nullptr);
    pdf::PDFCMSManager cms(nullptr); cms.setDocument(&document);
    const auto colors = cms.getCurrentCMS();
    pdf::PDFFontCache fonts(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    fonts.setDocument(pdf::PDFModifiedDocument(&document, &optional));
    fonts.setCacheShrinkEnabled(nullptr, false);
    pdf::PDFPageContentEditorProcessor processor(document.getCatalog()->getPage(static_cast<size_t>(index)), &document, &fonts,
        colors.data(), &optional, QTransform(), {});
    const auto errors = processor.processContents();
    for (const auto& error : errors) { if (error.type != pdf::RenderErrorType::Information) { throw std::runtime_error("Unsupported page content"); } }
    auto result = processor.takeEditedPageContent();
    if (result.getElementCount() > 2000) { throw std::runtime_error("Too many content items"); }
    // Forms can introduce fonts that are absent from the page resource dictionary.
    // Keep their actual objects; ambiguous names are rejected instead of substituted.
    auto fontsDictionary = result.getFontDictionary();
    auto statesDictionary = result.getGraphicStateDictionary();
    std::set<const pdf::PDFDictionary*> visited;
    size_t resourcesCount = 0;
    const auto collect = [&](const auto& self, const pdf::PDFObject& resource, int depth) -> void {
        const auto* dictionary = document.getDictionaryFromObject(resource);
        if (!dictionary || visited.contains(dictionary)) { return; }
        if (depth > 32 || ++resourcesCount > 1000) { throw std::runtime_error("Resource tree exceeds limits"); }
        visited.insert(dictionary);
        const auto merge = [&](const char* name, pdf::PDFDictionary& output) {
            if (const auto* input = document.getDictionaryFromObject(dictionary->get(name))) {
                for (size_t i = 0; i < input->getCount(); ++i) {
                    const auto key = input->getKey(i); const auto& object = input->getValue(i);
                    if (output.hasKey(key.getString()) && document.getDictionaryFromObject(output.get(key)) != document.getDictionaryFromObject(object)) { throw std::runtime_error("Conflicting resource names"); }
                    output.setEntry(pdf::PDFInplaceOrMemoryString(key), pdf::PDFObject(object));
                }
            }
        };
        merge("Font", fontsDictionary); merge("ExtGState", statesDictionary);
        if (const auto* objects = document.getDictionaryFromObject(dictionary->get("XObject"))) {
            for (size_t i = 0; i < objects->getCount(); ++i) {
                const auto* object = document.getDictionaryFromObject(objects->getValue(i));
                if (object && !object->get("Resources").isNull()) { self(self, object->get("Resources"), depth + 1); }
            }
        }
    };
    collect(collect, document.getCatalog()->getPage(static_cast<size_t>(index))->getResources(), 0);
    result.setFontDictionary(fontsDictionary); result.setGraphicStateDictionary(statesDictionary);
    return result;
}
QDomDocument textCommands(const pdf::PDFEditedPageContentElementText& text)
{
    QDomDocument document;
    if (!document.setContent(QStringLiteral("<doc>") + text.getItemsAsText() + QStringLiteral("</doc>"))) { return {}; }
    return document;
}
bool canReplaceContentText(const pdf::PDFEditedPageContentElementText& text)
{
    const auto document = textCommands(text);
    bool singleLine = true; bool first = true; QTransform line;
    auto state = text.getState();
    for (const auto& item : text.getItems()) {
        if (item.isUpdateGraphicState) { state.setState(item.state); }
        if (item.isText) {
            const auto matrix = state.getTextMatrix();
            if (first) { line = matrix; first = false; }
            singleLine = singleLine && qAbs(matrix.dy() - line.dy()) < .001 && qAbs(matrix.m11() - line.m11()) < .00001 &&
                qAbs(matrix.m12()) < .00001 && qAbs(matrix.m21()) < .00001 && qAbs(matrix.m22() - line.m22()) < .00001;
        }
    }
    return singleLine && !first && !document.isNull() && !document.documentElement().text().isEmpty() &&
        document.elementsByTagName(QStringLiteral("character")).isEmpty() &&
        document.elementsByTagName(QStringLiteral("tf")).size() <= 1 &&
        static_cast<int>(text.getState().getTextRenderingMode()) == 0;
}
QString encodedReplacement(const pdf::PDFEditedPageContentElementText& text, const QString& value)
{
    auto state = text.getState();
    pdf::PDFFontPointer font = state.getTextFont();
    QMap<QChar, pdf::CID> observed;
    for (const auto& item : text.getItems()) {
        if (item.isUpdateGraphicState) { state.setState(item.state); if (state.getTextFont()) { font = state.getTextFont(); } }
        if (item.isText) { for (const auto& character : item.textSequence.items) { if (!character.character.isNull() && character.cid) { observed.insert(character.character, character.cid); } } }
    }
    if (!font) { throw std::runtime_error("Original font is unavailable"); }
    const auto* simple = dynamic_cast<const pdf::PDFSimpleFont*>(font.data());
    QString result;
    for (const auto& character : value) {
        pdf::CID cid = observed.value(character, 0);
        if (!cid && simple) {
            const auto* encoding = simple->getEncoding();
            for (size_t i = 1; i < encoding->size(); ++i) {
                // Standard fonts have a known repertoire even when no embedded
                // glyph-index table is present. Subsets need an actual glyph.
                if ((*encoding)[i] == character.unicode() && (simple->getStandardFontType() != pdf::StandardFontType::Invalid || (*simple->getGlyphIndices())[i] != pdf::GID())) { cid = static_cast<pdf::CID>(i); break; }
            }
        }
        if (!cid && font->getToUnicode() && font->getCMap()) {
            cid = font->getToUnicode()->getFromUnicode(character);
            if (font->getCMap()->encode(cid).isEmpty()) { cid = 0; }
        }
        if (!cid) { throw std::runtime_error("The original font does not contain a requested character"); }
        result += QStringLiteral("<character cid=\"%1\"/>").arg(cid);
    }
    return result;
}
std::shared_ptr<pdf::PDFDocument> rebuildContent(pdf::PDFDocument& source, qsizetype pageIndex,
    const pdf::PDFEditedPageContent& content, qsizetype omitted = -1, const QSet<qsizetype>& omittedItems = {}, bool safeResources = false)
{
    pdf::PDFPageContentEditorContentStreamBuilder writer(&source);
    if (safeResources) {
        if (!content.getGraphicStateDictionary().isEmpty()) { throw std::runtime_error("Extended graphic resources are unsupported"); }
        const QSet<QByteArray> standard{"Helvetica", "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique", "Times-Roman", "Times-Bold", "Times-Italic", "Times-BoldItalic", "Courier", "Courier-Bold", "Courier-Oblique", "Courier-BoldOblique", "Symbol", "ZapfDingbats"};
        pdf::PDFDictionary clean;
        const auto fonts = content.getFontDictionary();
        for (size_t i = 0; i < fonts.getCount(); ++i) {
            const auto* font = source.getDictionaryFromObject(fonts.getValue(i));
            if (!font || !font->get("Subtype").isName() || font->get("Subtype").getString() != "Type1" || !font->get("BaseFont").isName() ||
                !standard.contains(font->get("BaseFont").getString()) || !font->get("ToUnicode").isNull() || (!font->get("Encoding").isNull() && !font->get("Encoding").isName())) { throw std::runtime_error("Only standard-font pages are supported"); }
            clean.setEntry(fonts.getKey(i), dictionaryOf({{"Type", pdf::PDFObject::createName("Font")}, {"Subtype", pdf::PDFObject::createName("Type1")},
                {"BaseFont", font->get("BaseFont")}, {"Encoding", font->get("Encoding")}}));
        }
        writer.setFontDictionary(clean);
    } else {
        writer.setFontDictionary(content.getFontDictionary()); writer.setXObjectDictionary(content.getXObjectDictionary()); writer.setGraphicStateDictionary(content.getGraphicStateDictionary());
    }
    for (size_t i = 0; i < content.getElementCount(); ++i) {
        if (static_cast<qsizetype>(i) == omitted || omittedItems.contains(static_cast<qsizetype>(i))) { continue; }
        const auto* element = content.getElement(i);
        if (const auto* text = element->asText()) {
            std::unique_ptr<pdf::PDFEditedPageContentElementText> original(text->clone());
            if (text->getItemsAsText() == pdf::PDFEditedPageContentElementText::createItemsAsText(text->getState(), text->getItems())) {
                auto items = text->getItems();
                for (auto& item : items) { if (item.isText) { for (auto& character : item.textSequence.items) { character.character = QChar(); } } }
                original->setItemsAsText(pdf::PDFEditedPageContentElementText::createItemsAsText(text->getState(), items));
            }
            // Recover full matrices from the states. The upstream serializer
            // loses scale/reflection on translated matrices and emits relative Td.
            QList<QTransform> matrices;
            auto state = text->getState(); state.setStateFlags({});
            for (const auto& item : text->getItems()) {
                if (!item.isUpdateGraphicState) { continue; }
                auto next = state; next.setStateFlags({}); next.setState(item.state);
                if (next.getStateFlags().testFlag(pdf::PDFPageContentProcessorState::StateTextMatrix)) { matrices.append(next.getTextMatrix()); }
                state = next; state.setStateFlags({});
            }
            auto xml = textCommands(*original);
            if (text->getItemsAsText() != pdf::PDFEditedPageContentElementText::createItemsAsText(text->getState(), text->getItems()) && matrices.size() > 1) {
                auto actual = text->getState();
                for (const auto& item : text->getItems()) {
                    if (item.isUpdateGraphicState) { actual.setState(item.state); }
                    if (item.isText) { matrices = {actual.getTextMatrix()}; break; }
                }
            }
            qsizetype matrixIndex = 0;
            for (auto node = xml.documentElement().firstChild(); !node.isNull();) {
                const auto next = node.nextSibling(); const auto old = node.toElement();
                if (old.tagName() == QStringLiteral("tpos") || old.tagName() == QStringLiteral("tmatrix")) {
                    if (matrixIndex >= matrices.size()) { throw std::runtime_error("Inconsistent text positions"); }
                    const auto matrix = matrices.at(matrixIndex++); auto absolute = xml.createElement(QStringLiteral("tmatrix"));
                    const char* names[] = {"m11", "m12", "m21", "m22", "x", "y"};
                    const qreal values[] = {matrix.m11(), matrix.m12(), matrix.m21(), matrix.m22(), matrix.dx(), matrix.dy()};
                    for (int j = 0; j < 6; ++j) { absolute.setAttribute(QString::fromLatin1(names[j]), QString::number(values[j], 'g', 16)); }
                    node.parentNode().replaceChild(absolute, node);
                }
                node = next;
            }
            auto inner = xml.toString(-1); const auto start = inner.indexOf(u'>') + 1;
            original->setItemsAsText(inner.mid(start, inner.lastIndexOf(QStringLiteral("</doc>")) - start));
            writer.writeEditedElement(original.get());
        } else { writer.writeEditedElement(element); }
    }
    if (!writer.getErrors().isEmpty()) { throw std::runtime_error(writer.getErrors().join(QStringLiteral("; ")).toStdString()); }
    if (writer.getOutputContent().size() > 32 * 1024 * 1024) { throw std::runtime_error("Content exceeds limit"); }
    pdf::PDFDocumentBuilder builder(&source);
    auto fonts = writer.getFontDictionary(); auto images = writer.getXObjectDictionary(); auto state = writer.getGraphicStateDictionary();
    builder.replaceObjectsByReferences(fonts); builder.replaceObjectsByReferences(images); builder.replaceObjectsByReferences(state);
    const auto resources = dictionaryOf({{"Font", pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(fonts)))},
        {"XObject", pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(images)))},
        {"ExtGState", pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(state)))}});
    const auto page = source.getCatalog()->getPage(static_cast<size_t>(pageIndex))->getPageReference();
    builder.mergeTo(page, dictionaryOf({{"Resources", resources}, {"Contents", pdf::PDFObject::createReference(addStream(builder, writer.getOutputContent()))}}));
    return std::make_shared<pdf::PDFDocument>(builder.build());
}
}

PdfOutputResult PdfDocument::localizedRedactedCopy(const QString& destination, const QList<PdfRedactionArea>& areas) const
{
    if (areas.isEmpty() || areas.size() > 100 || pageCount() > 50) { return {message("Selecciona hasta 100 zonas en un documento de hasta 50 páginas."), {}}; }
    const QFileInfo target(destination);
    if (destination.trimmed().isEmpty() || !target.isNativePath() || target.exists() || !target.dir().exists()) { return {message("Elige un archivo de destino nuevo."), {}}; }
    for (const auto& region : areas) { if (region.page < 0 || region.page >= pageCount() || !validArea(region.area)) { return {message("La zona no es válida."), {}}; } }
    try {
        pdf::PDFDocumentBuilder output; output.createDocument();
        for (qsizetype pageIndex = 0; pageIndex < pageCount(); ++pageIndex) {
            std::shared_ptr<pdf::PDFDocument> baseline;
            std::shared_ptr<pdf::PDFDocument> redacted;
            {
                const std::lock_guard lock(renderMutex_);
                const auto restriction = editingRestriction(*document_); if (!restriction.isEmpty()) { return {restriction, {}}; }
                const auto* page = document_->getCatalog()->getPage(static_cast<size_t>(pageIndex));
                if (!page->getAnnotations().empty()) { return {message("La modalidad localizada requiere páginas sin anotaciones ni campos. Usa la copia como imágenes para este documento."), {}}; }
                auto source = *document_; auto content = readEditableContent(source, pageIndex);
                for (size_t i = 0; i < content.getElementCount(); ++i) {
                    if (auto* image = content.getElement(i)->asImage()) {
                        const auto decoded = image->getImage(); if (decoded.isNull() || decoded.sizeInBytes() > 64 * 1024 * 1024) { throw std::runtime_error("Unsupported image"); }
                        QImage pixels(decoded.size(), QImage::Format_RGB32); pixels.fill(Qt::white); QPainter painter(&pixels); painter.drawImage(0, 0, decoded); painter.end();
                        image->setImageObject({}); image->setImage(pixels);
                    }
                }
                baseline = rebuildContent(source, pageIndex, content, -1, {}, true);
                QSet<qsizetype> removed;
                const auto size = pageSize(pageIndex); const auto toView = pageToCanvas(*page, size);
                for (size_t i = 0; i < content.getElementCount(); ++i) {
                    const auto* element = content.getElement(i); auto box = toView.mapRect(element->getBoundingBox());
                    const auto matrix = element->getTransform();
                    const auto strokeScale = std::max(std::hypot(matrix.m11(), matrix.m12()), std::hypot(matrix.m21(), matrix.m22()));
                    const auto pad = element->asPath() ? std::max(1.0, element->getState().getLineWidth() * strokeScale * page->getUserUnit() * (96.0 / 72.0)) : 1.0;
                    box.adjust(-pad, -pad, pad, pad);
                    const QRectF normalized(box.x() / size.width(), box.y() / size.height(), box.width() / size.width(), box.height() / size.height());
                    for (const auto& region : areas) { if (region.page == pageIndex && normalized.intersects(region.area)) { removed.insert(static_cast<qsizetype>(i)); } }
                }
                for (const auto& region : areas) {
                    if (region.page != pageIndex) { continue; }
                    QPainterPath mask; mask.addRect(visibleAreaToPage(*page, region.area));
                    pdf::PDFPageContentProcessorState state; pdf::PDFPainterHelper::applyBrushToGraphicState(&state, QBrush(Qt::black));
                    pdf::PDFPainterHelper::applyPenToGraphicState(&state, QPen(Qt::NoPen)); content.addContentPath(state, mask, false, true);
                }
                redacted = rebuildContent(source, pageIndex, content, -1, removed, true);
                const auto newPage = output.appendPage(page->getMediaBox()); output.setPageCropBox(newPage, page->getCropBox()); output.setPageRotation(newPage, page->getPageRotation());
                output.mergeTo(newPage, dictionaryOf({{"UserUnit", objectOf(page->getUserUnit())}}));
                const auto* dictionary = redacted->getDictionaryFromObject(pdf::PDFObject::createReference(page->getPageReference()));
                const auto copied = output.copyFrom({dictionary->get("Contents"), dictionary->get("Resources")}, redacted->getStorage(), true);
                output.mergeTo(newPage, dictionaryOf({{"Contents", copied[0]}, {"Resources", copied[1]}}));
            }
            const auto size = pageSize(pageIndex); const auto zoom = std::min({1.0, 1200.0 / size.width(), 1200.0 / size.height()});
            const auto original = renderPage(pageIndex, zoom); const PdfDocument rebuilt({}, baseline, {}); const auto represented = rebuilt.renderPage(pageIndex, zoom);
            if (original.image.isNull() || !original.warnings.isEmpty() || represented.image.isNull() || !represented.warnings.isEmpty() || original.image != represented.image) {
                return {message("La página %1 no se reconstruye fielmente para supresión localizada. Usa la copia como imágenes.").arg(pageIndex + 1), {}};
            }
            const PdfDocument checked({}, redacted, {});
            for (const auto& region : areas) {
                if (region.page != pageIndex) { continue; }
                const auto selected = checked.selectText(pageIndex, region.area.topLeft(), region.area.bottomRight());
                if (!selected.error.isEmpty() || !selected.match.text.trimmed().isEmpty()) { return {message("No se pudo verificar la eliminación del texto seleccionado."), {}}; }
            }
        }
        auto document = std::make_shared<pdf::PDFDocument>(output.build());
        const PdfDocument clean({}, document, {});
        const auto result = clean.save(PdfSaveMode::NewCopy, destination);
        return {result.error, result.warnings};
    } catch (const std::exception&) { return {message("La modalidad localizada admite páginas simples con fuentes estándar y sin recursos ambiguos. Usa la copia como imágenes para este PDF."), {}}; }
}

PdfContentList PdfDocument::pageContent(qsizetype pageIndex) const
{
    if (pageIndex < 0 || pageIndex >= pageCount()) { return {{}, message("La página no es válida.")}; }
    try {
        const std::lock_guard lock(renderMutex_);
        const auto restriction = editingRestriction(*document_);
        if (!restriction.isEmpty()) { return {{}, restriction}; }
        auto source = *document_;
        auto content = readEditableContent(source, pageIndex);
        const auto* page = source.getCatalog()->getPage(static_cast<size_t>(pageIndex));
        const auto visibleSize = pageSize(pageIndex);
        const auto toView = pageToCanvas(*page, visibleSize);
        PdfContentList result;
        for (size_t i = 0; i < content.getElementCount(); ++i) {
            const auto* element = content.getElement(i);
            const auto mapped = toView.mapRect(element->getBoundingBox());
            const QRectF area(mapped.x() / visibleSize.width(), mapped.y() / visibleSize.height(), mapped.width() / visibleSize.width(), mapped.height() / visibleSize.height());
            if (!validArea(area)) { continue; }
            PdfContentItem item; item.index = static_cast<qsizetype>(i); item.area = area;
            if (const auto* text = element->asText()) {
                item.type = PdfContentType::Text; item.text = textCommands(*text).documentElement().text(); item.replaceable = canReplaceContentText(*text);
            } else { item.type = element->asImage() ? PdfContentType::Image : PdfContentType::Path; item.replaceable = element->asImage() != nullptr; }
            result.items.append(std::move(item));
        }
        return result;
    } catch (const std::exception&) { return {{}, message("No se pudo interpretar esta página para edición de contenido.")}; }
}

PdfOpenResult PdfDocument::editContent(qsizetype pageIndex, qsizetype index, const PdfContentUpdate& update) const
{
    if ((update.change != PdfContentChange::ReplaceText && update.change != PdfContentChange::ReplaceImage && update.change != PdfContentChange::Transform && update.change != PdfContentChange::Delete && update.change != PdfContentChange::PathStyle) ||
        pageIndex < 0 || pageIndex >= pageCount() || index < 0 || !std::isfinite(update.scale) || update.scale < .1 || update.scale > 4 ||
        !std::isfinite(update.rotation) || qAbs(update.rotation) > 180 || !std::isfinite(update.offset.x()) || !std::isfinite(update.offset.y()) ||
        qAbs(update.offset.x()) > 1 || qAbs(update.offset.y()) > 1) { return {nullptr, message("Revisa la página y la transformación del objeto."), {}}; }
    try {
        std::shared_ptr<pdf::PDFDocument> baseline;
        std::shared_ptr<pdf::PDFDocument> edited;
        {
            const std::lock_guard lock(renderMutex_);
            const auto restriction = editingRestriction(*document_);
            if (!restriction.isEmpty()) { return {nullptr, restriction, {}}; }
            auto source = *document_;
            auto content = readEditableContent(source, pageIndex);
            if (static_cast<size_t>(index) >= content.getElementCount()) { return {nullptr, message("El objeto ya no está disponible."), {}}; }
            baseline = rebuildContent(source, pageIndex, content);
            auto* element = content.getElement(static_cast<size_t>(index));
            const auto* page = source.getCatalog()->getPage(static_cast<size_t>(pageIndex));
            const auto viewSize = pageSize(pageIndex);
            const auto toView = pageToCanvas(*page, viewSize);
            const auto bounds = toView.mapRect(element->getBoundingBox());
            if (!bounds.isValid()) { return {nullptr, message("Este objeto no tiene una geometría editable."), {}}; }
            if (update.change == PdfContentChange::ReplaceText) {
                auto* text = element->asText();
                if (!text || !canReplaceContentText(*text) || update.text.trimmed().isEmpty() || update.text.size() > 1000 ||
                    std::any_of(update.text.begin(), update.text.end(), [](QChar c) { return c.unicode() < 32; })) {
                    return {nullptr, message("Solo se sustituyen bloques simples de una línea, con caracteres disponibles en su fuente original."), {}};
                }
                auto commands = textCommands(*text); auto root = commands.documentElement();
                bool firstTextSeen = false; QDomNode position;
                for (auto node = root.firstChild(); !node.isNull(); node = node.nextSibling()) {
                    if (node.isText() && !node.nodeValue().isEmpty()) { firstTextSeen = true; }
                    if (!firstTextSeen && (node.toElement().tagName() == QStringLiteral("tpos") || node.toElement().tagName() == QStringLiteral("tmatrix"))) { position = node; }
                }
                for (auto node = root.firstChild(); !node.isNull();) {
                    const auto next = node.nextSibling(); const auto tag = node.toElement().tagName();
                    if ((tag == QStringLiteral("tpos") || tag == QStringLiteral("tmatrix")) && node != position) { root.removeChild(node); }
                    node = next;
                }
                const auto encoded = encodedReplacement(*text, update.text);
                QDomDocument replacement; replacement.setContent(QStringLiteral("<doc>") + encoded + QStringLiteral("</doc>"));
                bool inserted = false;
                for (auto node = root.firstChild(); !node.isNull();) {
                    const auto next = node.nextSibling();
                    if (node.isText() || node.toElement().tagName() == QStringLiteral("space")) {
                        if (!inserted) {
                            for (auto child = replacement.documentElement().firstChild(); !child.isNull(); child = child.nextSibling()) { root.insertBefore(commands.importNode(child, true), node); }
                            inserted = true;
                        }
                        root.removeChild(node);
                    }
                    node = next;
                }
                // QDom indentation would become real text. Serialize without formatting.
                auto inner = commands.toString(-1);
                inner = inner.mid(inner.indexOf(u'>') + 1, inner.lastIndexOf(QStringLiteral("</doc>")) - inner.indexOf(u'>') - 1);
                text->setItemsAsText(inner);
            } else if (update.change == PdfContentChange::ReplaceImage) {
                auto* image = element->asImage();
                if (!image || update.image.isNull() || update.image.width() > 8192 || update.image.height() > 8192 || qint64(update.image.width()) * update.image.height() > 16000000) {
                    return {nullptr, message("Elige una imagen de hasta 16 millones de píxeles."), {}};
                }
                QImage opaque(update.image.size(), QImage::Format_RGB32); opaque.fill(Qt::white);
                QPainter paint(&opaque); paint.drawImage(0, 0, update.image); paint.end();
                image->setImageObject({}); image->setImage(opaque);
            } else if (update.change == PdfContentChange::Transform) {
                QTransform change;
                change.translate(bounds.center().x() + update.offset.x() * viewSize.width(), bounds.center().y() + update.offset.y() * viewSize.height());
                change.rotate(update.rotation); change.scale(update.scale, update.scale); change.translate(-bounds.center().x(), -bounds.center().y());
                if (!QRectF(QPointF(), viewSize).adjusted(-.01, -.01, .01, .01).contains(change.mapRect(bounds))) {
                    return {nullptr, message("El objeto transformado debe quedar dentro de la página visible."), {}};
                }
                element->setTransform(element->getTransform() * toView * change * toView.inverted());
            } else if (update.change == PdfContentChange::PathStyle) {
                auto* path = element->asPath();
                if (!path || !update.stroke.isValid() || update.stroke.alpha() != 255 || !update.fill.isValid() ||
                    (update.fill.alpha() != 0 && update.fill.alpha() != 255) || !std::isfinite(update.width) || update.width < .5 || update.width > 20) {
                    return {nullptr, message("Revisa el color y el grosor del trazado."), {}};
                }
                auto state = path->getState();
                pdf::PDFPainterHelper::applyPenToGraphicState(&state, QPen(update.stroke, update.width / page->getUserUnit()));
                pdf::PDFPainterHelper::applyBrushToGraphicState(&state, update.fill.alpha() ? QBrush(update.fill) : QBrush(Qt::NoBrush));
                path->setState(state); path->setStrokePath(true); path->setFillPath(update.fill.alpha() != 0);
            }
            edited = rebuildContent(source, pageIndex, content, update.change == PdfContentChange::Delete ? index : -1);
        }
        const PdfDocument roundtrip(filePath_, baseline, sourceHash_);
        const auto size = pageSize(pageIndex);
        const auto zoom = std::min({1.0, 1200.0 / size.width(), 1200.0 / size.height()});
        const auto before = renderPage(pageIndex, zoom);
        const auto after = roundtrip.renderPage(pageIndex, zoom);
        if (before.image.isNull() || after.image.isNull() || !before.warnings.isEmpty() || !after.warnings.isEmpty() || before.image != after.image) {
            return {nullptr, message("Esta página contiene elementos que no pueden reconstruirse fielmente. No se aplicó la edición."), {}};
        }
        auto result = std::shared_ptr<const PdfDocument>(new PdfDocument(filePath_, edited, sourceHash_));
        if (update.change == PdfContentChange::ReplaceText) {
            const auto content = result->pageContent(pageIndex);
            if (!content.error.isEmpty() || std::none_of(content.items.begin(), content.items.end(), [index](const auto& item) { return item.index == index && item.type == PdfContentType::Text; })) {
                return {nullptr, message("El texto nuevo debe quedar dentro de la página visible."), {}};
            }
        }
        const auto rendered = result->renderPage(pageIndex, zoom);
        if (rendered.image.isNull() || !rendered.warnings.isEmpty()) { return {nullptr, message("El resultado no superó la verificación de representación."), {}}; }
        return {std::move(result), {}, {}};
    } catch (const std::exception&) { return {nullptr, message("No se pudo editar el objeto con sus recursos originales. Revisa que su fuente contenga los caracteres nuevos y que la página sea compatible."), {}}; }
}

namespace {
PdfSignatureReport inspectSignatures(const pdf::PDFDocument& document, const QByteArray& bytes)
{
    // Bound form traversal and byte ranges before invoking the upstream verifier.
    std::set<const pdf::PDFDictionary*> visited;
    size_t fields = 0;
    const auto validateFields = [&](const auto& self, const pdf::PDFObject& value, int depth) -> bool {
        const auto& array = document.getObject(value);
        if (array.isNull()) { return true; }
        if (!array.isArray() || depth > 64) { return false; }
        for (const auto& entry : *array.getArray()) {
            const auto* field = document.getDictionaryFromObject(entry);
            if (!field || ++fields > 10000 || !visited.insert(field).second || !self(self, field->get("Kids"), depth + 1)) { return false; }
        }
        return true;
    };
    if (const auto* formDictionary = document.getDictionaryFromObject(document.getCatalog()->getFormObject())) {
        if (!validateFields(validateFields, formDictionary->get("Fields"), 0)) { return {{}, message("El árbol de campos de firma no es compatible o supera los límites.")}; }
    }
    size_t signatures = 0;
    for (const auto& object : document.getStorage().getObjects()) {
        if (!object.object.isDictionary()) { continue; }
        const auto* dictionary = object.object.getDictionary();
        if (dictionary->get("ByteRange").isNull()) { continue; }
        const auto& range = document.getObject(dictionary->get("ByteRange"));
        if (++signatures > 100 || !range.isArray() || range.getArray()->getCount() != 4) { return {{}, message("La estructura de rangos de firma no es compatible.")}; }
        pdf::PDFInteger numbers[4]{};
        for (size_t i = 0; i < 4; ++i) {
            const auto& number = document.getObject(range.getArray()->getItem(i));
            if (!number.isInt()) { return {{}, message("La firma contiene rangos no válidos.")}; }
            numbers[i] = number.getInteger();
        }
        if (numbers[0] != 0 || numbers[1] <= 0 || numbers[1] > bytes.size() || numbers[2] <= numbers[1] || numbers[2] > bytes.size() ||
            numbers[3] < 0 || numbers[3] > bytes.size() - numbers[2]) { return {{}, message("La firma contiene rangos fuera del archivo.")}; }
    }
    const auto form = pdf::PDFForm::parse(&document, document.getCatalog()->getFormObject());
    pdf::PDFCertificateStore store;
    pdf::PDFSignatureHandler::Parameters parameters; parameters.store = &store;
    PdfSignatureReport report;
    const auto results = pdf::PDFSignatureHandler::verifySignatures(form, bytes, parameters);
    if (results.size() > 100) { return {{}, message("El documento supera el límite de 100 firmas.")}; }
    for (const auto& result : results) {
        auto covered = result.getBytesCoveredBySignature();
        report.signatures.append({result.getSignatureFieldQualifiedName(), result.isSignatureValid(), result.isCertificateValid(),
            covered.isCovered(0, bytes.size() - 1), result.getErrors(), result.getWarnings(), result.getHashAlgorithms()});
    }
    return report;
}
}
PdfSignatureReport PdfDocument::verifySignatures() const
{
    try {
        QFile file(filePath_);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 100 * 1024 * 1024) { return {{}, message("Elige un PDF guardado de hasta 100 MB para verificar sus firmas.")}; }
        const auto bytes = file.readAll();
        if (file.error() != QFileDevice::NoError || QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) != sourceHash_) {
            return {{}, message("El archivo cambió en el disco. Vuelve a abrirlo antes de verificar sus firmas.")};
        }
        const std::lock_guard lock(renderMutex_);
        return inspectSignatures(*document_, bytes);
    } catch (const std::exception&) { return {{}, message("No se pudieron verificar las firmas del documento.")}; }
}

PdfOutputResult PdfDocument::signedCopy(const QString& destination, const QString& certificatePath, const QString& password, const QString& reason) const
{
    const QFileInfo target(destination);
    if (destination.trimmed().isEmpty() || !target.isNativePath() || target.exists() || !target.dir().exists() || reason.size() > 1000 || reason.contains(QChar::Null) ||
        password.size() > 1024 || password.contains(QChar::Null)) { return {message("Revisa el destino nuevo, el motivo y la contraseña del certificado."), {}}; }
    try {
        QFile certificateFile(certificatePath);
        if (!certificateFile.open(QIODevice::ReadOnly) || certificateFile.size() <= 0 || certificateFile.size() > 4 * 1024 * 1024) {
            return {message("Elige un certificado PFX/P12 de hasta 4 MB que contenga la clave privada."), {}};
        }
        pdf::PDFCertificateEntry certificate;
        certificate.pkcs12 = certificateFile.readAll();
        if (certificateFile.error() != QFileDevice::NoError) { return {message("No se pudo leer el certificado."), {}}; }
        QByteArray probe;
        if (!pdf::PDFSignatureFactory::sign(certificate, password, QByteArray("PDF Editor signing check"), probe)) {
            return {message("No se pudo utilizar el certificado. Revisa su contraseña y que contenga la clave privada."), {}};
        }
        QByteArray bytes;
        constexpr pdf::PDFInteger marker = 123456789123456;
        const QByteArray markerText = QByteArray::number(marker);
        // Parentheses force this writer to serialize the value in hexadecimal.
        const QByteArray placeholder(32768, '(');
        {
            const std::lock_guard lock(renderMutex_);
            const auto restriction = editingRestriction(*document_);
            if (!restriction.isEmpty()) { return {restriction, {}}; }
            pdf::PDFDocumentBuilder builder(document_.get());
            const auto signature = builder.createSignatureDictionary("Adobe.PPKLite", "adbe.pkcs7.detached", placeholder, QDateTime::currentDateTimeUtc(), marker);
            const auto field = builder.createFormFieldSignature(QStringLiteral("Signature_") + QUuid::createUuid().toString(QUuid::Id128), {}, signature);
            builder.createInvisibleFormFieldWidget(field, document_->getCatalog()->getPage(0)->getPageReference());
            if (!reason.isEmpty()) { builder.setSignatureReason(signature, reason); }
            auto form = pdf::PDFDictionary();
            if (const auto* original = builder.getDictionaryFromObject(document_->getCatalog()->getFormObject())) { form = *original; }
            pdf::PDFArray fields;
            const auto& existing = builder.getObject(form.get("Fields"));
            if (!existing.isNull() && !existing.isArray()) { return {message("La estructura del formulario no es compatible con la firma."), {}}; }
            if (existing.isArray()) { fields = *existing.getArray(); }
            fields.appendItem(pdf::PDFObject::createReference(field));
            form.setEntry(pdf::PDFInplaceOrMemoryString("Fields"), pdf::PDFObject::createArray(std::make_shared<pdf::PDFArray>(std::move(fields))));
            form.setEntry(pdf::PDFInplaceOrMemoryString("SigFlags"), pdf::PDFObject::createInteger(3));
            const auto catalog = document_->getStorage().getTrailerDictionary().getDictionary()->get("Root").getReference();
            builder.mergeTo(catalog, dictionaryOf({{"AcroForm", pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(form)))}}));
            const auto prepared = builder.build();
            QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
            pdf::PDFDocumentWriter writer(nullptr);
            if (!writer.write(&buffer, &prepared)) { return {message("No se pudo preparar el documento para firmarlo."), {}}; }
        }
        if (bytes.size() > 100 * 1024 * 1024) { return {message("La firma de esta versión admite documentos de hasta 100 MB."), {}}; }
        const auto hex = placeholder.toHex(); const auto position = bytes.indexOf(hex);
        if (position <= 0 || bytes.indexOf(hex, position + 1) != -1 || bytes[position - 1] != '<' || bytes[position + hex.size()] != '>') {
            return {message("No se pudo localizar de forma única el espacio de firma."), {}};
        }
        const QRegularExpression pattern(QStringLiteral("/ByteRange\\s*\\[\\s*(%1)\\s+(%1)\\s+(%1)\\s+(%1)\\s*\\]").arg(QString::fromLatin1(markerText)));
        const auto match = pattern.match(QString::fromLatin1(bytes));
        if (!match.hasMatch() || pattern.match(QString::fromLatin1(bytes), match.capturedEnd()).hasMatch()) { return {message("No se pudo preparar el rango firmado."), {}}; }
        const qsizetype left = position - 1; const qsizetype after = position + hex.size() + 1;
        const qsizetype ranges[] = {0, left, after, bytes.size() - after};
        for (int i = 0; i < 4; ++i) {
            const auto number = QByteArray::number(ranges[i]);
            if (number.size() > markerText.size()) { return {message("El documento supera el rango de firma admitido."), {}}; }
            bytes.replace(match.capturedStart(i + 1), markerText.size(), number.leftJustified(markerText.size(), ' '));
        }
        QByteArray signature;
        if (!pdf::PDFSignatureFactory::sign(certificate, password, bytes.left(left) + bytes.mid(after), signature) || signature.size() > placeholder.size()) {
            return {message("No se pudo crear la firma digital en el espacio disponible."), {}};
        }
        bytes.replace(position, hex.size(), signature.toHex().leftJustified(hex.size(), '0'));
        pdf::PDFDocumentReader reader(nullptr, [](bool* okay) { *okay = false; return QString(); }, false, false);
        const auto signedDocument = reader.readFromBuffer(bytes);
        if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK || signedDocument.getCatalog()->getPageCount() != static_cast<size_t>(pageCount())) {
            return {message("La copia firmada no superó la verificación de lectura."), {}};
        }
        const auto report = inspectSignatures(signedDocument, bytes);
        if (!report.error.isEmpty() || report.signatures.size() != 1 || !report.signatures[0].integrityValid || !report.signatures[0].coversWholeFile) {
            return {message("La copia no superó la verificación criptográfica. No se publicó."), {}};
        }
        const auto algorithms = report.signatures[0].algorithms.join(QStringLiteral(" ")).toLower();
        if (!algorithms.contains(QStringLiteral("sha256")) && !algorithms.contains(QStringLiteral("sha-256"))) { return {message("El certificado no produjo una firma SHA-256 compatible."), {}}; }
        QTemporaryFile output(target.dir().filePath(QStringLiteral(".pdf-editor-signed-XXXXXX")));
        if (!output.open() || output.write(bytes) != bytes.size() || !output.flush() || !output.rename(target.absoluteFilePath())) {
            return {message("No se pudo publicar la copia firmada. Elige un destino nuevo."), {}};
        }
        output.setAutoRemove(false);
        return {};
    } catch (const std::exception&) { return {message("No se pudo crear la copia firmada. El original se conserva."), {}}; }
}

PdfOpenResult PdfDocument::open(const QString& filePath, const QString& password)
{
    QFile file(filePath);
    if (filePath.isEmpty() || !QFileInfo(file).isFile() || !file.open(QIODevice::ReadOnly)) {
        return {nullptr, message("No se pudo leer el archivo. Comprueba que exista y tengas permiso para abrirlo."), {}};
    }

    try {
        bool passwordRequested = false;
        bool attempted = false;
        auto passwordCallback = [&passwordRequested, &attempted, &password](bool* accepted) {
            passwordRequested = true;
            *accepted = !attempted && !password.isEmpty();
            attempted = true;
            return password;
        };
        // Strict reading: do not silently repair a malformed document in this first increment.
        pdf::PDFDocumentReader reader(nullptr, passwordCallback, false, false);
        auto document = std::make_shared<pdf::PDFDocument>(reader.readFromDevice(&file));
        if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK && passwordRequested) {
            return {nullptr, password.isEmpty() ? message("Este PDF requiere una contraseña.") : message("La contraseña no permite abrir este PDF."), {}, true};
        }
        if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK) {
            return {nullptr, message("No se pudo interpretar el archivo como un PDF válido."), {}};
        }
        if (document->getCatalog()->getPageCount() == 0) {
            return {nullptr, message("El PDF no contiene páginas."), {}};
        }

        const auto sourceHash = document->getSourceDataHash();
        auto result = std::shared_ptr<const PdfDocument>(
            new PdfDocument(QFileInfo(file).absoluteFilePath(), std::move(document), sourceHash));
        return {std::move(result), {}, reader.getWarnings()};
    } catch (const std::exception&) {
        return {nullptr, message("Ocurrió un error al leer el PDF. El archivo original no se ha modificado."), {}};
    }
}

} // namespace pdfeditor


