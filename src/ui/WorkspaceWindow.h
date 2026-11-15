#pragma once

#include "session/SessionStore.h"

#include <QMainWindow>
#include <QTimer>

class QLabel;
class QTabWidget;
class QDragEnterEvent;
class QDropEvent;
template<typename T> class QFutureWatcher;

namespace pdfeditor {
class MainWindow;

class WorkspaceWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit WorkspaceWindow(const QString& sessionDirectory = {}, QWidget* parent = nullptr);
    ~WorkspaceWindow() override;
    MainWindow* currentDocument() const;
    int documentCount() const;
    QStringList recentFiles() const { return recentFiles_; }
    void openDocument(const QString& path);
    void closeTab(int index);
    void flushSession();
    void restoreSession();
    bool offerSessionRestore();

signals:
    void sessionSaved(const QString& error);
    void sessionRestored(int count, int failures);

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    MainWindow* createPane(const QString& id = {});
    void updatePane(MainWindow* pane);
    void refreshMenus();
    void requestSessionWrite();
    void startSessionWrite();
    void confirmNextClose(int index);
    bool hasBusyDocument() const;
    void showMessage(const QString& message);

    QTabWidget* tabs_;
    QLabel* message_;
    QLabel* sessionStatus_;
    std::shared_ptr<SessionStore> store_;
    QFutureWatcher<QString>* saveWatcher_;
    QFutureWatcher<QList<SessionRestoredDocument>>* restoreWatcher_;
    QTimer saveTimer_;
    QStringList recentFiles_;
    QStringList queuedPaths_;
    QString pendingActiveId_;
    QList<SessionEntry> pendingEntries_;
    bool pendingChoice_ = false;
    bool writeRequested_ = false;
    bool storeAvailable_ = true;
    bool restoring_ = false;
    bool closing_ = false;
    bool finalizing_ = false;
    bool currentSaveFinal_ = false;
    bool allowExit_ = false;
    bool saving_ = false;
};
} // namespace pdfeditor
