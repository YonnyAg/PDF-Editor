#pragma once

#include "core/PdfDocument.h"

#include <QHash>
#include <QLockFile>

namespace pdfeditor {

struct SessionEntry
{
    QString id;
    QString path;
    QByteArray sourceHash;
    QString snapshot;
    QByteArray snapshotHash;
    qsizetype page = 0;
    qreal zoom = 1;
    bool fitWidth = true;
};

struct SessionDocument
{
    SessionEntry entry;
    std::shared_ptr<const PdfDocument> document;
    bool modified = false;
};

struct SessionIndex
{
    QList<SessionEntry> entries;
    QStringList recentFiles;
    QString error;
    bool cleanExit = false;
    QString activeId;
};

struct SessionRestoredDocument
{
    SessionEntry entry;
    std::shared_ptr<const PdfDocument> document;
    std::shared_ptr<const PdfDocument> savedDocument;
    QString error;
};

class SessionStore final
{
public:
    explicit SessionStore(QString directory);
    const QString& error() const { return error_; }
    const QString& directory() const { return directory_; }
    SessionIndex read() const;
    QString save(const QList<SessionDocument>& documents, const QList<SessionEntry>& retained,
        const QStringList& recentFiles, bool cleanExit = false, const QString& activeId = {});
    QList<SessionRestoredDocument> restore(const QList<SessionEntry>& entries) const;

private:
    struct CachedSnapshot
    {
        std::shared_ptr<const PdfDocument> document;
        SessionEntry entry;
    };
    QString directory_;
    QString error_;
    std::unique_ptr<QLockFile> lock_;
    QHash<QString, CachedSnapshot> cache_;
};

} // namespace pdfeditor
