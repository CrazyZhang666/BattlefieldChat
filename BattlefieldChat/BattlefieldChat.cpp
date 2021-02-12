#include "BattlefieldChat.h"
#include "GlobalVariables.h"
#include <QCloseEvent>

BattlefieldChat::BattlefieldChat(QWidget *parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);
    setWindowFlags(Qt::Window | Qt::MSWindowsFixedSizeDialogHint);
    mainWindow = this;
    inputWindow = new InputDialog();
}

void BattlefieldChat::showEvent(QShowEvent* ev) {
    QMainWindow::showEvent(ev);
    workerThread.start();
}

bool shutdownPending = false;
void BattlefieldChat::closeEvent(QCloseEvent* event) {
    shutdownPending = true;
    workerThread.requestInterruption();
    event->accept();
}

void BattlefieldChat::pushLog(std::string message) {
    if (!shutdownPending)
        ui.listLogs->addItem(QString::fromStdString(message));
}

