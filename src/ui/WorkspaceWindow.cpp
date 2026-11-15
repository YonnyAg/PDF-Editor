#include "WorkspaceWindow.h"
#include "MainWindow.h"

#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDir>
#include <QDesktopServices>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QPointer>
#include <QPushButton>
#include <QShortcut>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTabBar>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <utility>

namespace pdfeditor {
namespace {
QString pathKey(const QString& path)
{
    const auto canonical = QFileInfo(path).canonicalFilePath();
    auto key = canonical.isEmpty() ? QFileInfo(path).absoluteFilePath() : canonical;
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}
} // namespace

WorkspaceWindow::WorkspaceWindow(const QString& sessionDirectory, QWidget* parent)
    : QMainWindow(parent), tabs_(new QTabWidget(this)), message_(new QLabel(this)), sessionStatus_(new QLabel(this)),
      store_(std::make_shared<SessionStore>(sessionDirectory.isEmpty()
          ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("sessions")) : sessionDirectory)),
      saveWatcher_(new QFutureWatcher<QString>(this)), restoreWatcher_(new QFutureWatcher<QList<SessionRestoredDocument>>(this))
{
    setObjectName(QStringLiteral("workspaceWindow"));
    setWindowTitle(tr("PDF Editor"));
    resize(1150, 820);
    setMinimumSize(680, 540);
    setAcceptDrops(true);
    auto* body = new QWidget(this);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(0, 0, 0, 0);
    message_->setObjectName(QStringLiteral("sessionMessage"));
    message_->setWordWrap(true);
    message_->setMargin(8);
    message_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    message_->hide();
    layout->addWidget(message_);
    tabs_->setObjectName(QStringLiteral("documentTabs"));
    tabs_->setDocumentMode(true);
    tabs_->setTabsClosable(true);
    tabs_->setMovable(true);
    tabs_->tabBar()->setAccessibleName(tr("Documentos abiertos"));
    sessionStatus_->setObjectName(QStringLiteral("sessionStatus"));
    sessionStatus_->setMargin(5);
    sessionStatus_->setText(tr("Recuperación local"));
    tabs_->setCornerWidget(sessionStatus_);
    layout->addWidget(tabs_);
    setCentralWidget(body);
    const auto previous = store_->read();
    recentFiles_ = previous.recentFiles;
    pendingEntries_ = previous.entries;
    pendingActiveId_ = previous.activeId;
    pendingChoice_ = !pendingEntries_.isEmpty();
    storeAvailable_ = previous.error.isEmpty();
    if (!storeAvailable_) { showMessage(previous.error); sessionStatus_->setText(tr("Recuperación no disponible")); }
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(1200);
    connect(&saveTimer_, &QTimer::timeout, this, &WorkspaceWindow::startSessionWrite);
    connect(tabs_, &QTabWidget::tabCloseRequested, this, &WorkspaceWindow::closeTab);
    connect(tabs_->tabBar(), &QTabBar::tabMoved, this, [this] { requestSessionWrite(); });
    connect(tabs_, &QTabWidget::currentChanged, this, [this] {
        if (auto* pane = currentDocument()) { updatePane(pane); pane->setFocus(); requestSessionWrite(); }
    });
    for (const auto& sequence : {QKeySequence(Qt::CTRL | Qt::Key_Tab), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab)}) {
        auto* shortcut = new QShortcut(sequence, this);
        const bool previousTab = sequence == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab);
        connect(shortcut, &QShortcut::activated, this, [this, previousTab] {
            if (tabs_->isEnabled() && tabs_->count() > 1) { tabs_->setCurrentIndex((tabs_->currentIndex() + (previousTab ? tabs_->count() - 1 : 1)) % tabs_->count()); }
        });
    }
    connect(saveWatcher_, &QFutureWatcher<QString>::finished, this, [this] {
        saving_ = false;
        const auto error = saveWatcher_->result();
        const bool finalSave = currentSaveFinal_;
        currentSaveFinal_ = false;
        if (!error.isEmpty()) { showMessage(error); sessionStatus_->setText(tr("Recuperación no actualizada")); }
        else { sessionStatus_->setText(writeRequested_ && !finalSave ? tr("Recuperación pendiente") : tr("Recuperación actualizada")); }
        if (!error.isEmpty() || !writeRequested_ || finalSave) { emit sessionSaved(error); }
        if (finalSave) {
            if (error.isEmpty()) { allowExit_ = true; close(); }
            else { closing_ = false; finalizing_ = false; tabs_->setEnabled(true); }
        } else if (writeRequested_ || finalizing_) { saveTimer_.start(0); }
    });
    connect(restoreWatcher_, &QFutureWatcher<QList<SessionRestoredDocument>>::finished, this, [this] {
        const auto restored = restoreWatcher_->result();
        pendingEntries_.clear();
        int count = 0;
        QStringList errors;
        for (const auto& entry : restored) {
            if (!entry.document) {
                pendingEntries_.append(entry.entry);
                errors.append(tr("%1: %2").arg(QFileInfo(entry.entry.path).fileName(), entry.error));
                continue;
            }
            auto* pane = tabs_->count() == 1 && currentDocument() && currentDocument()->currentFilePath().isEmpty()
                ? currentDocument() : createPane(entry.entry.id);
            pane->setProperty("sessionId", entry.entry.id);
            pane->adoptSessionDocument(entry);
            ++count;
        }
        restoring_ = false;
        tabs_->setEnabled(true);
        pendingChoice_ = false;
        for (int i = 0; i < tabs_->count(); ++i) {
            if (tabs_->widget(i)->property("sessionId").toString() == pendingActiveId_) { tabs_->setCurrentIndex(i); break; }
        }
        if (!errors.isEmpty()) { showMessage(tr("Se conservan %1 entradas que no se pudieron restaurar:\n%2").arg(errors.size()).arg(errors.join('\n'))); }
        else { message_->hide(); }
        refreshMenus();
        requestSessionWrite();
        emit sessionRestored(count, errors.size());
        const auto queuedPaths = std::exchange(queuedPaths_, QStringList{});
        for (const auto& path : queuedPaths) { openDocument(path); }
    });
    createPane();
}

WorkspaceWindow::~WorkspaceWindow()
{
    saveTimer_.stop();
    saveWatcher_->waitForFinished();
    restoreWatcher_->waitForFinished();
}

MainWindow* WorkspaceWindow::currentDocument() const { return qobject_cast<MainWindow*>(tabs_->currentWidget()); }
int WorkspaceWindow::documentCount() const
{
    int count = 0;
    for (int i = 0; i < tabs_->count(); ++i) { if (!qobject_cast<MainWindow*>(tabs_->widget(i))->currentFilePath().isEmpty()) { ++count; } }
    return count;
}

MainWindow* WorkspaceWindow::createPane(const QString& id)
{
    auto* pane = new MainWindow(tabs_);
    pane->setProperty("sessionId", id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id);
    connect(pane, &MainWindow::workspaceStateChanged, this, [this, pane] { updatePane(pane); requestSessionWrite(); });
    connect(pane, &MainWindow::documentOpened, this, [this, pane](const QString& path) {
        for (int i = recentFiles_.size() - 1; i >= 0; --i) { if (pathKey(recentFiles_[i]) == pathKey(path)) { recentFiles_.removeAt(i); } }
        recentFiles_.prepend(path);
        if (recentFiles_.size() > 20) { recentFiles_.removeLast(); }
        updatePane(pane);
        refreshMenus();
        requestSessionWrite();
    });
    tabs_->addTab(pane, tr("Inicio"));
    tabs_->setCurrentWidget(pane);
    refreshMenus();
    return pane;
}

void WorkspaceWindow::refreshMenus()
{
    for (int i = 0; i < tabs_->count(); ++i) {
        auto* pane = qobject_cast<MainWindow*>(tabs_->widget(i));
        pane->configureWorkspace([this](const QString& path) { openDocument(path); },
            [this, pane] { closeTab(tabs_->indexOf(pane)); }, [this] { offerSessionRestore(); }, recentFiles_, !pendingEntries_.isEmpty() && storeAvailable_,
            [this] { recentFiles_.clear(); refreshMenus(); requestSessionWrite(); },
            [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(store_->directory())); });
    }
}

void WorkspaceWindow::updatePane(MainWindow* pane)
{
    const int index = tabs_->indexOf(pane);
    if (index < 0) { return; }
    const auto name = pane->currentFilePath().isEmpty() ? tr("Inicio") : QFileInfo(pane->currentFilePath()).fileName();
    tabs_->setTabText(index, name + (pane->isWindowModified() ? QStringLiteral(" *") : QString{}));
    tabs_->setTabToolTip(index, QDir::toNativeSeparators(pane->currentFilePath()));
    bool modified = false;
    for (int i = 0; i < tabs_->count(); ++i) { modified = modified || qobject_cast<MainWindow*>(tabs_->widget(i))->isWindowModified(); }
    setWindowModified(modified);
    if (pane == currentDocument()) { setWindowTitle(tr("%1[*] — PDF Editor").arg(name)); }
}

void WorkspaceWindow::showMessage(const QString& message) { message_->setText(message); message_->setVisible(!message.isEmpty()); }

void WorkspaceWindow::openDocument(const QString& path)
{
    if (path.isEmpty() || closing_) { return; }
    if (restoring_) { if (queuedPaths_.size() < 20) { queuedPaths_.append(path); } return; }
    if (pendingChoice_ && !offerSessionRestore()) { return; }
    if (restoring_) { if (queuedPaths_.size() < 20) { queuedPaths_.append(path); } return; }
    for (int i = 0; i < tabs_->count(); ++i) {
        auto* pane = qobject_cast<MainWindow*>(tabs_->widget(i));
        if ((!pane->currentFilePath().isEmpty() && pathKey(pane->currentFilePath()) == pathKey(path)) ||
            (pane->isBusy() && pathKey(pane->property("requestedPath").toString()) == pathKey(path))) {
            tabs_->setCurrentWidget(pane);
            return;
        }
    }
    auto* pane = currentDocument();
    if (!pane || pane->isBusy() || !pane->currentFilePath().isEmpty()) {
        if (tabs_->count() + pendingEntries_.size() >= 20) { showMessage(tr("Puedes mantener hasta 20 documentos en esta sesión.")); return; }
        pane = createPane();
    }
    pane->setProperty("requestedPath", path);
    pane->openDocument(path);
}

bool WorkspaceWindow::hasBusyDocument() const
{
    for (int i = 0; i < tabs_->count(); ++i) { if (qobject_cast<MainWindow*>(tabs_->widget(i))->isBusy()) { return true; } }
    return false;
}

void WorkspaceWindow::closeTab(int index)
{
    if (index < 0 || index >= tabs_->count() || closing_ || restoring_) { return; }
    auto* pane = qobject_cast<MainWindow*>(tabs_->widget(index));
    if (pane->isBusy()) { showMessage(tr("Espera a que termine la operación antes de cerrar esta pestaña.")); return; }
    pane->prepareClose([this, pane] {
        tabs_->removeTab(tabs_->indexOf(pane));
        pane->deleteLater();
        if (tabs_->count() == 0) { createPane(); }
        flushSession();
    }, [] {});
}

void WorkspaceWindow::requestSessionWrite()
{
    writeRequested_ = true;
    if (!closing_ && !restoring_ && !pendingChoice_ && storeAvailable_) {
        sessionStatus_->setText(tr("Recuperación pendiente"));
        saveTimer_.start();
    }
}

void WorkspaceWindow::flushSession()
{
    writeRequested_ = true;
    saveTimer_.stop();
    startSessionWrite();
}

void WorkspaceWindow::startSessionWrite()
{
    if (!storeAvailable_ || pendingChoice_ || restoring_ || saving_) { return; }
    if (hasBusyDocument()) { saveTimer_.start(); return; }
    if (closing_ && !finalizing_) { return; }
    QList<SessionDocument> documents;
    for (int i = 0; i < tabs_->count(); ++i) {
        auto* pane = qobject_cast<MainWindow*>(tabs_->widget(i));
        auto document = pane->sessionDocument(pane->property("sessionId").toString(), finalizing_);
        if (document.document && !document.document->isEncrypted()) { documents.append(std::move(document)); }
    }
    writeRequested_ = false;
    currentSaveFinal_ = finalizing_;
    saving_ = true;
    sessionStatus_->setText(tr("Guardando recuperación…"));
    const auto activeId = currentDocument() ? currentDocument()->property("sessionId").toString() : QString{};
    saveWatcher_->setFuture(QtConcurrent::run([store = store_, documents, retained = pendingEntries_, recent = recentFiles_, finalSave = finalizing_, activeId] {
        return store->save(documents, retained, recent, finalSave, activeId);
    }));
}

bool WorkspaceWindow::offerSessionRestore()
{
    if (!storeAvailable_) { return true; }
    if (restoring_ || closing_) { return false; }
    if (saving_ && !pendingEntries_.isEmpty()) { showMessage(tr("Espera a que termine el guardado de recuperación antes de restaurar.")); return false; }
    if (pendingEntries_.isEmpty()) { return true; }
    QMessageBox dialog(QMessageBox::Question, tr("Restaurar sesión anterior"),
        tr("Hay %1 documentos de una sesión anterior. Puedes recuperar las páginas abiertas y los cambios pendientes que se alcanzaron a guardar localmente.").arg(pendingEntries_.size()),
        QMessageBox::NoButton, this);
    dialog.setObjectName(QStringLiteral("restoreSessionDialog"));
    auto* restore = dialog.addButton(tr("Restaurar"), QMessageBox::AcceptRole);
    auto* discard = dialog.addButton(tr("Empezar sin restaurar"), QMessageBox::DestructiveRole);
    auto* cancel = dialog.addButton(tr("Cancelar"), QMessageBox::RejectRole);
    dialog.setDefaultButton(restore);
    dialog.setEscapeButton(cancel);
    dialog.exec();
    if (dialog.clickedButton() == restore) { restoreSession(); return true; }
    if (dialog.clickedButton() == discard) { pendingEntries_.clear(); pendingChoice_ = false; message_->hide(); refreshMenus(); requestSessionWrite(); return true; }
    return false;
}

void WorkspaceWindow::restoreSession()
{
    if (!storeAvailable_ || restoring_ || closing_ || pendingEntries_.isEmpty() || saving_ || hasBusyDocument()) { return; }
    for (const auto& entry : pendingEntries_) {
        for (int i = 0; i < tabs_->count(); ++i) {
            if (pathKey(qobject_cast<MainWindow*>(tabs_->widget(i))->currentFilePath()) == pathKey(entry.path)) {
                showMessage(tr("Cierra el documento «%1» antes de recuperar otra versión de esa sesión.").arg(QFileInfo(entry.path).fileName()));
                return;
            }
        }
    }
    if (documentCount() + pendingEntries_.size() > 20) { showMessage(tr("Cierra documentos antes de restaurar el resto de la sesión.")); return; }
    restoring_ = true;
    tabs_->setEnabled(false);
    saveTimer_.stop();
    restoreWatcher_->setFuture(QtConcurrent::run([store = store_, entries = pendingEntries_] { return store->restore(entries); }));
}

void WorkspaceWindow::confirmNextClose(int index)
{
    if (index >= tabs_->count()) {
        if (!storeAvailable_ || pendingChoice_) {
            allowExit_ = true;
            QMetaObject::invokeMethod(this, [this] { close(); }, Qt::QueuedConnection);
            return;
        }
        finalizing_ = true;
        writeRequested_ = true;
        startSessionWrite();
        return;
    }
    auto* pane = qobject_cast<MainWindow*>(tabs_->widget(index));
    pane->prepareClose([this, index] { confirmNextClose(index + 1); }, [this] {
        closing_ = false;
        tabs_->setEnabled(true);
        requestSessionWrite();
    });
}

void WorkspaceWindow::closeEvent(QCloseEvent* event)
{
    if (allowExit_) { QMainWindow::closeEvent(event); return; }
    event->ignore();
    if (closing_) { return; }
    if (restoring_ || hasBusyDocument()) { showMessage(tr("Espera a que terminen las operaciones antes de salir.")); return; }
    closing_ = true;
    tabs_->setEnabled(false);
    saveTimer_.stop();
    confirmNextClose(0);
}

void WorkspaceWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (closing_ || restoring_) { return; }
    for (const auto& url : event->mimeData()->urls()) {
        if (url.isLocalFile() && QFileInfo(url.toLocalFile()).suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0) { event->acceptProposedAction(); return; }
    }
}

void WorkspaceWindow::dropEvent(QDropEvent* event)
{
    if (closing_ || restoring_) { return; }
    int accepted = 0;
    for (const auto& url : event->mimeData()->urls()) {
        if (url.isLocalFile() && QFileInfo(url.toLocalFile()).suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0) {
            openDocument(url.toLocalFile());
            if (++accepted >= 20) { break; }
        }
    }
    if (accepted > 0) { event->acceptProposedAction(); }
}
} // namespace pdfeditor
