#pragma once

#include <QMainWindow>
#include "core/PdfDocument.h"
#include "session/SessionStore.h"

#include <functional>

class QAction;
class QCloseEvent;
class QLabel;
class QMenu;
class QSpinBox;
class QStackedWidget;
class QToolBar;
class QTreeWidget;
template<typename T> class QFutureWatcher;

namespace pdfeditor {

class PageView;
class SearchBar;
class ThumbnailView;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    QString currentFilePath() const;
    qsizetype pageCount() const;
    void openDocument(const QString& filePath);
    void closeDocument();
    bool isBusy() const { return operation_ != Operation::Idle; }
    SessionDocument sessionDocument(const QString& id, bool discardChanges = false) const;
    void adoptSessionDocument(const SessionRestoredDocument& restored);
    void configureWorkspace(std::function<void(const QString&)> open, std::function<void()> close,
        std::function<void()> restore, const QStringList& recentFiles, bool restoreAvailable,
        std::function<void()> clearRecent, std::function<void()> openRecoveryFolder);
    void prepareClose(std::function<void()> proceed, std::function<void()> cancelled);

signals:
    void documentOpened(const QString& filePath, qsizetype pageCount);
    void documentOpenFailed(const QString& message);
    void workspaceStateChanged();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    enum class Operation { Idle, Open, LoadImport, Rotate, Move, Pages, Save, Extract, Annotate, Output };

    struct HistoryEntry
    {
        std::shared_ptr<const PdfDocument> document;
        qsizetype page;
    };

    void startOperation(Operation operation, const QString& status, std::function<PdfOpenResult()> work);
    void startOutput(const QString& status, const QString& completed, std::function<PdfOutputResult()> work);
    void finishOperation();
    void rotateCurrentPage();
    void moveCurrentPage(qsizetype offset);
    void changeCurrentPage(PdfPageOperation operation);
    void insertFromPdf();
    void configurePageImport(std::shared_ptr<const PdfDocument> source);
    void mergePdfs();
    void extractPages();
    void splitDocument();
    void insertContent(bool image);
    void decoratePages(bool numbering);
    void editProperties();
    void addPageLink();
    void showPageLinks();
    void cropPages(bool reset);
    void addNote(QPointF position);
    void showPageNotes();
    void showPageTextMarks();
    void fillPageForm();
    void createPageFormField();
    void addPageShape();
    void drawInk();
    void showPageShapes();
    void manageAttachments();
    void exportOptimizedCopy();
    void exportProtectedCopy();
    void recognizeText();
    void exportRedactedCopy(bool localized = false);
    void editPageContent();
    void signDocumentCopy();
    void showSignatures();
    void exportPage();
    void printPdf();
    void restoreHistory(QList<HistoryEntry>& from, QList<HistoryEntry>& to);
    void saveAs();
    void saveDocument(PdfSaveMode mode, const QString& copyPath = {}, std::function<void()> afterSave = {}, std::function<void()> failed = {});
    void confirmPendingChanges(std::function<void()> proceed, std::function<void()> cancelled = {});
    void clearDocument();
    void updateDocumentActions();
    void updateSelectionActions();
    void updateBookmarks();
    void updateBookmarkActions();
    void changeSelectedBookmark(PdfBookmarkChange change, bool asChild = false);
    void updateMoveActions();
    void updateDocumentView(bool keepView = false);
    void updateNavigation();

    std::shared_ptr<const PdfDocument> document_;
    std::shared_ptr<const PdfDocument> savedDocument_;
    QList<HistoryEntry> undoHistory_;
    QList<HistoryEntry> redoHistory_;
    Operation operation_ = Operation::Idle;
    QString outputMessage_;
    qsizetype pageDestination_ = -1;
    std::function<void()> afterSave_;
    std::function<void()> afterSaveFailed_;
    std::function<void(const QString&)> workspaceOpen_;
    std::function<void()> workspaceClose_;
    QMenu* recentMenu_ = nullptr;
    QAction* restoreSessionAction_ = nullptr;
    QAction* recoveryFolderAction_ = nullptr;
    bool renderMessage_ = false;
    QString pendingOpenPath_;
    int passwordAttempts_ = 0;
    QFutureWatcher<PdfOpenResult>* watcher_ = nullptr;
    QAction* openAction_ = nullptr;
    QAction* closeAction_ = nullptr;
    QAction* exportAction_ = nullptr;
    QAction* printAction_ = nullptr;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QAction* searchAction_ = nullptr;
    QAction* selectTextAction_ = nullptr;
    QAction* copyTextAction_ = nullptr;
    QList<QAction*> selectionMarkActions_;
    QTreeWidget* bookmarks_ = nullptr;
    QAction* addBookmarkAction_ = nullptr;
    QAction* renameBookmarkAction_ = nullptr;
    QAction* retargetBookmarkAction_ = nullptr;
    QAction* deleteBookmarkAction_ = nullptr;
    QAction* addChildBookmarkAction_ = nullptr;
    QAction* bookmarkUpAction_ = nullptr;
    QAction* bookmarkDownAction_ = nullptr;
    QAction* indentBookmarkAction_ = nullptr;
    QAction* outdentBookmarkAction_ = nullptr;
    QVariantList requestedBookmarkPath_;
    QAction* saveAction_ = nullptr;
    QAction* saveAsAction_ = nullptr;
    QAction* rotateAction_ = nullptr;
    QAction* movePageUp_ = nullptr;
    QAction* movePageDown_ = nullptr;
    QAction* extractPagesAction_ = nullptr;
    QAction* splitAction_ = nullptr;
    QAction* insertTextAction_ = nullptr;
    QAction* insertImageAction_ = nullptr;
    QAction* numberPagesAction_ = nullptr;
    QAction* watermarkAction_ = nullptr;
    QAction* propertiesAction_ = nullptr;
    QAction* addLinkAction_ = nullptr;
    QAction* pageLinksAction_ = nullptr;
    QAction* cropPagesAction_ = nullptr;
    QAction* resetCropAction_ = nullptr;
    QAction* duplicatePageAction_ = nullptr;
    QAction* deletePageAction_ = nullptr;
    QAction* insertBlankPageAction_ = nullptr;
    QAction* insertPdfAction_ = nullptr;
    QAction* mergePdfsAction_ = nullptr;
    QAction* addNoteAction_ = nullptr;
    QAction* pageNotesAction_ = nullptr;
    QAction* pageTextMarksAction_ = nullptr;
    QAction* fillFormAction_ = nullptr;
    QAction* createFormAction_ = nullptr;
    QAction* addShapeAction_ = nullptr;
    QAction* drawInkAction_ = nullptr;
    QAction* pageShapesAction_ = nullptr;
    QAction* attachmentsAction_ = nullptr;
    QAction* optimizeAction_ = nullptr;
    QAction* protectAction_ = nullptr;
    QAction* ocrAction_ = nullptr;
    QAction* redactAction_ = nullptr;
    QAction* localizedRedactAction_ = nullptr;
    QAction* accessibleReaderAction_ = nullptr;
    QAction* contentEditAction_ = nullptr;
    QAction* signAction_ = nullptr;
    QAction* verifySignaturesAction_ = nullptr;
    QAction* previousPage_ = nullptr;
    QAction* nextPage_ = nullptr;
    QSpinBox* pageNumber_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* description_ = nullptr;
    QLabel* error_ = nullptr;
    QLabel* pendingLabel_ = nullptr;
    PageView* pageView_ = nullptr;
    ThumbnailView* thumbnails_ = nullptr;
    QWidget* documentPane_ = nullptr;
    QStackedWidget* content_ = nullptr;
    QToolBar* viewTools_ = nullptr;
    SearchBar* searchBar_ = nullptr;
};

} // namespace pdfeditor
