#pragma once

#include <QThread>
#include <sstream>

#include "Offsets.h"
#include "GlobalVariables.h"

class WorkerThread : public QThread
{
    Q_OBJECT

public:
    WorkerThread(QObject *parent = NULL);
    ~WorkerThread();

    void run() override;
private:
    void chatLoop();
    void doInput(Pointer messageCavePtr, ChatMessagePointer chatMessagePtr, bool isFullscreen);
    void writeChatMessage(Pointer messageCavePtr, ChatMessagePointer chatMessagePtr, std::string content, int length);
    bool suspendAndWrite(Pointer messageCavePtr, ChatMessagePointer chatMessagePtr, std::string content, int length);
    void resumePointer(ChatMessagePointer chatMessagePtr, uintptr_t oldAddress);
};

class Log : public std::ostringstream {
public:
    Log() {
    };

    ~Log() {
        mainWindow->pushLog(str());
    }
};
