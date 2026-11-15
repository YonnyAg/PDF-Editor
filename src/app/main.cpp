#include "ui/WorkspaceWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>
#include <QIcon>
#include <QDir>
#include <QTranslator>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PDF Editor"));
    QApplication::setOrganizationName(QStringLiteral("PDFEditor"));
    QApplication::setApplicationVersion(QStringLiteral(PDF_EDITOR_VERSION));
    QIcon icon;
    for (const int size : {16, 20, 24, 32, 40, 48, 64, 96, 128, 256}) {
        icon.addFile(QStringLiteral(":/branding/pdf-editor-%1.png").arg(size));
    }
    QApplication::setWindowIcon(icon);
    QTranslator qtTranslator;
    if (qtTranslator.load(QStringLiteral("qtbase_es"), QDir(QApplication::applicationDirPath()).filePath(QStringLiteral("translations")))) {
        application.installTranslator(&qtTranslator);
    }
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Visor y editor PDF con documentos en pestañas."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption profileOption(QStringLiteral("profile"),
        QStringLiteral("Directorio independiente para la sesión y los recientes."), QStringLiteral("directorio"));
    parser.addOption(profileOption);
    parser.addPositionalArgument(QStringLiteral("archivos"), QStringLiteral("Documentos PDF para abrir."), QStringLiteral("[archivos…]"));
    parser.process(application);

    pdfeditor::WorkspaceWindow window(parser.value(profileOption));
    window.show();
    const auto paths = parser.positionalArguments();
    QTimer::singleShot(0, &window, [&window, paths] {
        if (!window.offerSessionRestore()) { return; }
        for (const auto& path : paths) { window.openDocument(path); }
    });

    return application.exec();
}
