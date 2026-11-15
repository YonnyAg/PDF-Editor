#include "SessionStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSet>
#include <QUuid>

#include <cmath>
#include <exception>

namespace pdfeditor {
namespace {
bool validId(const QString& id)
{
    return !QUuid(id).isNull() && QUuid(id).toString(QUuid::WithoutBraces) == id;
}

bool validSnapshotName(const QString& name)
{
    return name.endsWith(QStringLiteral(".pdf")) && validId(name.chopped(4));
}

QByteArray fileHash(const QString& path)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (QFileInfo(path).isSymLink() || !file.open(QIODevice::ReadOnly) || !hash.addData(&file)) { return {}; }
    return hash.result();
}

QJsonObject toJson(const SessionEntry& entry)
{
    return {{QStringLiteral("id"), entry.id}, {QStringLiteral("path"), entry.path},
        {QStringLiteral("sourceHash"), QString::fromLatin1(entry.sourceHash.toHex())},
        {QStringLiteral("snapshot"), entry.snapshot}, {QStringLiteral("snapshotHash"), QString::fromLatin1(entry.snapshotHash.toHex())},
        {QStringLiteral("page"), qint64(entry.page)}, {QStringLiteral("zoom"), entry.zoom}, {QStringLiteral("fitWidth"), entry.fitWidth}};
}
} // namespace

SessionStore::SessionStore(QString directory)
    : directory_(QFileInfo(directory).absoluteFilePath())
{
    if (directory.trimmed().isEmpty() || QFileInfo(directory_).isSymLink() || !QDir().mkpath(directory_)) {
        error_ = QStringLiteral("No se puede crear la carpeta de recuperación. Guarda manualmente tus cambios.");
        return;
    }
    lock_ = std::make_unique<QLockFile>(QDir(directory_).filePath(QStringLiteral("session.lock")));
    lock_->setStaleLockTime(0);
    if (!lock_->tryLock(0)) {
        error_ = QStringLiteral("Otra instancia utiliza esta sesión. La recuperación automática está desactivada en esta ventana.");
    }
}

SessionIndex SessionStore::read() const
{
    SessionIndex result;
    if (!error_.isEmpty()) { result.error = error_; return result; }
    QFile file(QDir(directory_).filePath(QStringLiteral("session.json")));
    if (!file.exists()) { return result; }
    const auto invalid = QStringLiteral("El registro de recuperación no es válido. Se conserva sin sobrescribir; guarda manualmente tus cambios.");
    if (QFileInfo(file).isSymLink() || file.size() > 1024 * 1024 || !file.open(QIODevice::ReadOnly)) { result.error = invalid; return result; }
    QJsonParseError parseError;
    const auto json = QJsonDocument::fromJson(file.readAll(), &parseError);
    const auto root = json.object();
    if (parseError.error != QJsonParseError::NoError || !json.isObject() || root.value(QStringLiteral("version")).toInt() != 1 ||
        !root.value(QStringLiteral("documents")).isArray() || !root.value(QStringLiteral("recentFiles")).isArray() ||
        !root.value(QStringLiteral("cleanExit")).isBool()) { result.error = invalid; return result; }
    const auto documents = root.value(QStringLiteral("documents")).toArray();
    if (documents.size() > 20) { result.error = invalid; return result; }
    QSet<QString> ids;
    for (const auto& value : documents) {
        const auto object = value.toObject();
        SessionEntry entry;
        entry.id = object.value(QStringLiteral("id")).toString();
        entry.path = object.value(QStringLiteral("path")).toString();
        entry.sourceHash = QByteArray::fromHex(object.value(QStringLiteral("sourceHash")).toString().toLatin1());
        entry.snapshot = object.value(QStringLiteral("snapshot")).toString();
        entry.snapshotHash = QByteArray::fromHex(object.value(QStringLiteral("snapshotHash")).toString().toLatin1());
        const auto page = object.value(QStringLiteral("page")).toDouble(-1);
        entry.zoom = object.value(QStringLiteral("zoom")).toDouble(-1);
        entry.fitWidth = object.value(QStringLiteral("fitWidth")).toBool();
        if (!validId(entry.id) || ids.contains(entry.id) || !QFileInfo(entry.path).isAbsolute() || entry.path.size() > 32768 ||
            entry.sourceHash.size() != 32 || page < 0 || page > 1000000 || std::floor(page) != page ||
            !std::isfinite(entry.zoom) || entry.zoom < 0.1 || entry.zoom > 4 || !object.value(QStringLiteral("fitWidth")).isBool() ||
            (!entry.snapshot.isEmpty() && (!validSnapshotName(entry.snapshot) || entry.snapshotHash.size() != 32)) ||
            (entry.snapshot.isEmpty() && !entry.snapshotHash.isEmpty())) {
            result.error = invalid; result.entries.clear(); return result;
        }
        entry.page = static_cast<qsizetype>(page);
        ids.insert(entry.id);
        result.entries.append(std::move(entry));
    }
    for (const auto& value : root.value(QStringLiteral("recentFiles")).toArray()) {
        const auto path = value.toString();
        if (result.recentFiles.size() >= 20) { break; }
        if (QFileInfo(path).isAbsolute() && path.size() <= 32768 && !result.recentFiles.contains(path)) { result.recentFiles.append(path); }
    }
    result.cleanExit = root.value(QStringLiteral("cleanExit")).toBool();
    result.activeId = root.value(QStringLiteral("activeId")).toString();
    if (!result.activeId.isEmpty() && !ids.contains(result.activeId)) { result.activeId.clear(); }
    return result;
}

QString SessionStore::save(const QList<SessionDocument>& documents, const QList<SessionEntry>& retained,
    const QStringList& recentFiles, bool cleanExit, const QString& activeId)
{
    if (!error_.isEmpty()) { return error_; }
    if (documents.size() + retained.size() > 20) { return QStringLiteral("La sesión admite hasta 20 documentos."); }
    // Never replace an invalid or foreign index, even after an earlier successful read.
    const auto existing = read();
    if (!existing.error.isEmpty()) { return existing.error; }
    QStringList created;
    bool committed = false;
    const auto rollback = qScopeGuard([&] {
        if (!committed) { for (const auto& name : created) { QFile::remove(QDir(directory_).filePath(name)); } }
    });
    try {
        QJsonArray entries;
        QSet<QString> ids;
        QSet<QString> snapshots;
        QHash<QString, CachedSnapshot> nextCache;
        for (const auto& retainedEntry : retained) {
            if (!validId(retainedEntry.id) || ids.contains(retainedEntry.id) ||
                (!retainedEntry.snapshot.isEmpty() && !validSnapshotName(retainedEntry.snapshot))) {
                return QStringLiteral("No se puede conservar una entrada de recuperación no válida.");
            }
            ids.insert(retainedEntry.id);
            if (!retainedEntry.snapshot.isEmpty()) { snapshots.insert(retainedEntry.snapshot); }
            entries.append(toJson(retainedEntry));
        }
        for (const auto& current : documents) {
            auto entry = current.entry;
            if (current.document && current.document->isEncrypted()) { continue; }
            if (!current.document || !validId(entry.id) || ids.contains(entry.id) || !QFileInfo(current.document->filePath()).isAbsolute()) {
                return QStringLiteral("No se pudo preparar el documento para recuperación.");
            }
            ids.insert(entry.id);
            entry.path = current.document->filePath();
            entry.sourceHash = current.document->sourceHash();
            entry.snapshot.clear();
            entry.snapshotHash.clear();
            if (current.modified) {
                const auto cached = cache_.constFind(entry.id);
                if (cached != cache_.constEnd() && cached->document == current.document && QFileInfo::exists(QDir(directory_).filePath(cached->entry.snapshot))) {
                    entry.snapshot = cached->entry.snapshot;
                    entry.snapshotHash = cached->entry.snapshotHash;
                } else {
                    entry.snapshot = QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".pdf");
                    const auto path = QDir(directory_).filePath(entry.snapshot);
                    const auto saved = current.document->save(PdfSaveMode::NewCopy, path);
                    if (!saved.document) { return QStringLiteral("No se pudo guardar la recuperación: %1").arg(saved.error); }
                    created.append(entry.snapshot);
                    entry.snapshotHash = fileHash(path);
                    if (entry.snapshotHash.size() != 32) { return QStringLiteral("No se pudo verificar la copia de recuperación."); }
                }
                nextCache.insert(entry.id, {current.document, entry});
                snapshots.insert(entry.snapshot);
            }
            entries.append(toJson(entry));
        }
        QJsonArray recent;
        for (const auto& path : recentFiles.mid(0, 20)) { recent.append(path); }
        const QJsonObject root{{QStringLiteral("version"), 1}, {QStringLiteral("documents"), entries},
            {QStringLiteral("recentFiles"), recent}, {QStringLiteral("cleanExit"), cleanExit},
            {QStringLiteral("activeId"), ids.contains(activeId) ? activeId : QString{}}};
        const auto bytes = QJsonDocument(root).toJson();
        if (bytes.size() > 1024 * 1024) { return QStringLiteral("El registro de recuperación supera el límite de 1 MB. Se conserva la sesión anterior."); }
        QSaveFile file(QDir(directory_).filePath(QStringLiteral("session.json")));
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
            return QStringLiteral("No se pudo actualizar el registro de recuperación. Se conserva la versión anterior.");
        }
        committed = true;
        cache_ = std::move(nextCache);
        // Remove only snapshots referenced by our previous valid index after commit.
        for (const auto& old : existing.entries) {
            if (!old.snapshot.isEmpty() && validSnapshotName(old.snapshot) && !snapshots.contains(old.snapshot)) {
                QFile::remove(QDir(directory_).filePath(old.snapshot));
            }
        }
        return {};
    } catch (const std::exception&) { return QStringLiteral("Ocurrió un error al guardar la recuperación. Guarda manualmente tus cambios."); }
}

QList<SessionRestoredDocument> SessionStore::restore(const QList<SessionEntry>& entries) const
{
    QList<SessionRestoredDocument> result;
    for (const auto& entry : entries) {
        SessionRestoredDocument restored;
        restored.entry = entry;
        if (!error_.isEmpty()) { restored.error = error_; }
        else if (entry.snapshot.isEmpty()) {
            auto opened = PdfDocument::open(entry.path);
            restored.document = opened.document;
            restored.savedDocument = opened.document;
            restored.error = opened.error;
        } else if (!validSnapshotName(entry.snapshot) || fileHash(QDir(directory_).filePath(entry.snapshot)) != entry.snapshotHash) {
            restored.error = QStringLiteral("La copia de recuperación no coincide con su verificación; se conserva sin abrir.");
        } else {
            auto recovered = PdfDocument::recover(QDir(directory_).filePath(entry.snapshot), entry.path, entry.sourceHash);
            restored.document = recovered.document;
            restored.error = recovered.error;
            if (restored.document) { restored.savedDocument = PdfDocument::open(entry.path).document; }
        }
        result.append(std::move(restored));
    }
    return result;
}
} // namespace pdfeditor
