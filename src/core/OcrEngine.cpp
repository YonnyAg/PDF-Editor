#include "OcrEngine.h"
#include "PdfDocument.h"

#include <algorithm>
#include <chrono>
#include <future>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#endif

namespace pdfeditor {
#ifdef _WIN32
namespace {
class OcrRuntime
{
public:
    OcrRuntime() : worker_([this] {
        bool initialized = false;
        try { winrt::init_apartment(winrt::apartment_type::multi_threaded); initialized = true; }
        catch (const winrt::hresult_error&) { /* Individual requests return an availability error. */ }
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock lock(mutex_);
                ready_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
                if (stopping_ && jobs_.empty()) { break; }
                job = std::move(jobs_.front()); jobs_.pop_front();
            }
            job();
        }
        if (initialized) { winrt::clear_factory_cache(); winrt::uninit_apartment(); }
    }) {}
    ~OcrRuntime()
    {
        { const std::lock_guard lock(mutex_); stopping_ = true; }
        ready_.notify_one();
        worker_.join();
    }
    template<typename Work> auto submit(Work work)
    {
        using Result = decltype(work());
        auto task = std::make_shared<std::packaged_task<Result()>>(std::move(work));
        auto future = task->get_future();
        { const std::lock_guard lock(mutex_); jobs_.emplace_back([task] { (*task)(); }); }
        ready_.notify_one();
        return future;
    }
private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::function<void()>> jobs_;
    bool stopping_ = false;
    std::thread worker_;
};
OcrRuntime& runtime() { static OcrRuntime instance; return instance; }
QString qtString(const winrt::hstring& value) { return QString::fromWCharArray(value.c_str(), static_cast<int>(value.size())); }
} // namespace
#endif

QList<OcrLanguage> OcrEngine::languages()
{
#ifdef _WIN32
    return runtime().submit([] {
        QList<OcrLanguage> result;
        try {
            const auto available = winrt::Windows::Media::Ocr::OcrEngine::AvailableRecognizerLanguages();
            for (const auto& language : available) {
                result.append({qtString(language.LanguageTag()), qtString(language.DisplayName())});
            }
        } catch (const winrt::hresult_error&) { /* The UI reports that OCR is unavailable. */ }
        return result;
    }).get();
#else
    return {};
#endif
}

OcrResult OcrEngine::recognize(const QImage& source, const QString& tag)
{
    if (source.isNull() || source.width() > 8192 || source.height() > 8192 || qint64(source.width()) * source.height() > 16000000 || tag.isEmpty()) {
        return {{}, {}, QStringLiteral("Elige una imagen de hasta 16 millones de píxeles y un idioma instalado.")};
    }
#ifdef _WIN32
    return runtime().submit([source, tag] {
        OcrResult result;
        try {
            const winrt::Windows::Globalization::Language language(winrt::hstring(tag.toStdWString()));
            auto engine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(language);
            if (!engine) { result.error = QStringLiteral("El idioma OCR no está instalado en Windows."); return result; }
            const auto limit = static_cast<int>(winrt::Windows::Media::Ocr::OcrEngine::MaxImageDimension());
            auto image = source;
            if (image.width() > limit || image.height() > limit) { image = image.scaled(limit, limit, Qt::KeepAspectRatio, Qt::SmoothTransformation); }
            image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            winrt::Windows::Storage::Streams::DataWriter writer;
            const auto* bytes = reinterpret_cast<const uint8_t*>(image.constBits());
            writer.WriteBytes(winrt::array_view<const uint8_t>(bytes, bytes + image.sizeInBytes()));
            winrt::Windows::Graphics::Imaging::SoftwareBitmap bitmap(winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8,
                image.width(), image.height(), winrt::Windows::Graphics::Imaging::BitmapAlphaMode::Premultiplied);
            bitmap.CopyFromBuffer(writer.DetachBuffer());
            const auto operation = engine.RecognizeAsync(bitmap);
            if (operation.wait_for(std::chrono::seconds(30)) != winrt::Windows::Foundation::AsyncStatus::Completed) {
                operation.Cancel(); result.error = QStringLiteral("El reconocimiento excedió el tiempo disponible."); return result;
            }
            const auto recognized = operation.GetResults();
            result.text = qtString(recognized.Text());
            if (const auto angle = recognized.TextAngle()) { result.angle = angle.Value(); }
            for (const auto& line : recognized.Lines()) {
                for (const auto& word : line.Words()) {
                    const auto bounds = word.BoundingRect();
                    const auto text = qtString(word.Text());
                    const QRectF area(bounds.X / image.width(), bounds.Y / image.height(), bounds.Width / image.width(), bounds.Height / image.height());
                    if (text.size() > 512 || result.words.size() >= 10000 || !area.isValid() || !QRectF(0, 0, 1, 1).contains(area)) {
                        result.error = QStringLiteral("El resultado OCR supera los límites o contiene posiciones no válidas."); return result;
                    }
                    if (!text.trimmed().isEmpty()) { result.words.append({text, area}); }
                }
            }
        } catch (const winrt::hresult_error&) { result.error = QStringLiteral("Windows no pudo reconocer esta imagen. Revisa el idioma OCR y la imagen."); }
        return result;
    }).get();
#else
    return {{}, {}, QStringLiteral("El OCR local de esta versión requiere Windows.")};
#endif
}

OcrBatchResult OcrEngine::recognizePdf(const std::shared_ptr<const PdfDocument>& document, qsizetype first, qsizetype last,
    const QString& language, const std::function<bool()>& cancelled)
{
    OcrBatchResult result;
    if (!document || document->isEncrypted() || first < 0 || last < first || last >= document->pageCount() || last - first >= 50) {
        result.error = QStringLiteral("Selecciona hasta 50 páginas de un PDF sin cifrado."); return result;
    }
    qsizetype totalWords = 0;
    for (qsizetype page = first; page <= last; ++page) {
        if (cancelled && cancelled()) { result.cancelled = true; result.pages.clear(); result.text.clear(); return result; }
        const auto selection = document->selectText(page, {0, 0}, {1, 1});
        if (!selection.error.isEmpty()) { result.error = selection.error; return result; }
        if (!selection.match.text.trimmed().isEmpty() || !document->pageFormFields(page).isEmpty()) { ++result.skippedPages; continue; }
        const auto size = document->pageSize(page);
        const auto zoom = std::min({150.0 / 96.0, 2000 / std::max(1.0, size.width()), 2000 / std::max(1.0, size.height())});
        const auto rendered = document->renderPage(page, zoom);
        if (rendered.image.isNull()) { result.error = rendered.error; return result; }
        auto recognized = recognize(rendered.image, language);
        if (!recognized.error.isEmpty()) { result.error = recognized.error; return result; }
        if (recognized.words.isEmpty()) { ++result.skippedPages; continue; }
        totalWords += recognized.words.size();
        if (totalWords > 50000) { result.error = QStringLiteral("El intervalo supera el límite de 50000 palabras OCR."); return result; }
        result.text += QStringLiteral("Página %1\n%2\n\n").arg(page + 1).arg(recognized.text);
        result.pages.append({page, std::move(recognized.words), std::move(recognized.text), recognized.angle});
    }
    if (cancelled && cancelled()) { result.cancelled = true; result.pages.clear(); result.text.clear(); }
    return result;
}
} // namespace pdfeditor
