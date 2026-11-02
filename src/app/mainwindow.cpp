#include "mainwindow.h"

#include "canvaswidget.h"
#include "headerbar.h"
#include "version.h"

#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

MainWindow::MainWindow(const Theme &theme, QWidget *parent)
    : QMainWindow(parent), m_theme(theme)
{
    setWindowTitle(QStringLiteral("DoNs Streaming Studio"));
    resize(1280, 800);
    createMenus();
    createCentralArea();
}

void MainWindow::createMenus()
{
    // Nur Datei → Beenden, Docks und Hilfe → Über tun etwas; die übrigen Menüs sind
    // noch leer und ausgegraut (Auftrag P05).
    QMenu *file = menuBar()->addMenu(QStringLiteral("&Datei"));
    QAction *quit = file->addAction(QStringLiteral("&Beenden"), this, &QWidget::close);
    quit->setShortcut(QKeySequence::Quit);
    quit->setMenuRole(QAction::QuitRole);

    for (const QString &title : {QStringLiteral("&Bearbeiten"), QStringLiteral("&Ansicht")})
        menuBar()->addMenu(title)->setEnabled(false);
    m_docksMenu = menuBar()->addMenu(QStringLiteral("D&ocks"));
    menuBar()->addMenu(QStringLiteral("&Werkzeuge"))->setEnabled(false);

    QMenu *help = menuBar()->addMenu(QStringLiteral("&Hilfe"));
    help->addAction(QStringLiteral("Ü&ber …"), this, &MainWindow::showAbout);
}

void MainWindow::createCentralArea()
{
    auto *central = new QWidget(this);
    auto *column = new QVBoxLayout(central);
    column->setContentsMargins(4, 4, 4, 0);
    column->setSpacing(6);

    auto *header = new HeaderBar(central);
    column->addWidget(header);

    m_modules = new QStackedWidget(central);
    m_modules->addWidget(new CanvasWidget(m_theme, m_modules));

    auto *chatBotPage = new QWidget(m_modules);
    chatBotPage->setObjectName(QStringLiteral("chatBotPage"));
    chatBotPage->setAttribute(Qt::WA_StyledBackground);
    auto *chatBotLayout = new QVBoxLayout(chatBotPage);
    chatBotLayout->addWidget(new QLabel(QStringLiteral("Noch nicht vorhanden"), chatBotPage), 0,
                             Qt::AlignCenter);
    m_modules->addWidget(chatBotPage);
    column->addWidget(m_modules, 1);

    connect(header, &HeaderBar::moduleSelected, m_modules, &QStackedWidget::setCurrentIndex);
    setCentralWidget(central);
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, QStringLiteral("Über DoNs Streaming Studio"),
                       QStringLiteral("<b>DoNs Streaming Studio</b><br>Version %1<br>Qt %2")
                           .arg(dss::version(), QString::fromLatin1(qVersion())));
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    QMainWindow::paintEvent(event);
    if (m_firstPaintReported)
        return;
    m_firstPaintReported = true;
    // Erst nach dem Zeichnen melden, wie in den Prototypen von P01.
    QTimer::singleShot(0, this, [this] {
        qInfo("Fenster gezeichnet");
        emit firstPainted();
    });
}
