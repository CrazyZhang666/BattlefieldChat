﻿#include "WorkerThread.h"
#include "Utils.h"

WorkerThread::WorkerThread(QObject* parent)
    : QThread(parent) {
}

WorkerThread::~WorkerThread() {
}

void WorkerThread::run() {
    // FIXME: Migrating to Qt
    Log() << "Battlefield 1 中文输入工具";
    Log() << "Powered by.SakuraKooi (https://github.com/SakuraKoi/BattlefieldChat)";
    Log() << " ";
    Log() << "警告: 尽管Fairfight不检测聊天区域的内存数据, 但仍然可能存在一定的风险";
    Log() << "      USE AT YOUR OWN RISK, 作者不对工具造成的任何损失承担任何责任";
    Log() << " ";
    Log() << "注意: 游戏需要运行在无边框或窗口模式";
    Log() << " ";

    if (!loadNtDll()) {
        Log() << " [-] 警告: NtDll 加载失败, 可能会导致意料之外的游戏崩溃";
    }

    while (!isInterruptionRequested()) {
        // TODO move this block to another function
        Log() << " [*] 正在等待游戏启动...";
        while (!isInterruptionRequested()) {
            gameWindow = FindWindow(nullptr, L"Battlefield™ 1");
            if (gameWindow != 0) {
                GetWindowThreadProcessId(gameWindow, &pid);
                moduleBaseAddr = getModuleBaseAddress(pid, L"bf1.exe");
                if (pid != -1)
                    if (moduleBaseAddr != 0)
                        break;
            }
            Sleep(1000);
        }

        Log() << " [+] bf1.exe -> pid = " << pid << " 0x" << std::hex << moduleBaseAddr;

        hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        Log() << " [*] 正在初始化...";
        messageCaveAddr = (uintptr_t)VirtualAllocEx(hProcess, NULL, 256 * 3, MEM_COMMIT, PAGE_READWRITE);
        Log() << " [+] 预分配内存成功: 0x" << std::hex << messageCaveAddr;

        chatLoop();

        VirtualFreeEx(hProcess, (LPVOID)messageCaveAddr, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        Log() << " [*] 游戏已退出, Thanks for using!";
    }

    freeNtDll();
}

void WorkerThread::chatLoop() {
    Pointer messageCavePtr(hProcess, moduleBaseAddr);
    messageCavePtr.pointer = messageCaveAddr;
    ChatOpenPointer chatOpenPtr(hProcess, moduleBaseAddr);
    ChatMessagePointer chatMessagePtr(hProcess, moduleBaseAddr);

    Log() << " [+] Done! 在游戏中打开聊天即可自动呼出输入框";

    bool lastState = false;
    while (!isInterruptionRequested() && IsWindow(gameWindow)) {
        if (chatOpenPtr.refreshPointer()) {
            bool state = chatOpenPtr.readBoolean();
            bool isFullscreen = isFullscreenWindow(gameWindow);
            if (state && ( // chat is open
                (fullscreenSupport && isFullscreen && (GetAsyncKeyState(VK_HOME) & 0x8000) != 0) // fullscreen mode -> trigger by home key
                || (!lastState) // normal mode -> trigger by open chat
                )) {

                doInput(messageCavePtr, chatMessagePtr, isFullscreen);
            }
            lastState = state;
        }
        Sleep(200);
    }
}

void WorkerThread::doInput(Pointer messageCavePtr, ChatMessagePointer chatMessagePtr, bool isFullscreen) {
    Log() << " [+] 呼出聊天框, 等待输入...";
    QString str = inputWindow->showAndWaitForResult(gameWindow, isFullscreen ? DIALOG_FOR_FULLSCREEN : isBorderlessWindow(gameWindow) ? OVERLAY_FOR_BORDERLESS : OVERLAY_FOR_WINDOWED);

    SetForegroundWindow(gameWindow);
    if (str.length() == 0) {
        press(VK_ESCAPE, 20);
        Log() << " [-] 取消输入操作";
        return;
    }
    std::string converted = preprocessor.process(str);

    int length = converted.size();

    if (length > 90) {
        if (allowExceedLimit) {
            Log() << " [!] 消息长度超过90字节, 绕过这个限制并继续发送...";
        } else {
            press(VK_ESCAPE, 20);
            Log() << " [x] 消息长度超过90字节";
            MessageBox(NULL, L"聊天消息长度超过游戏限制 (90字节)\n\n您可以通过打开 [绕过游戏聊天长度限制] 的开关来禁用这个限制\n但这可能带来额外的FF风险", L"错误", 0);
            return;
        }
    }

    writeChatMessage(messageCavePtr, chatMessagePtr, converted, length);
}


void WorkerThread::writeChatMessage(Pointer messageCavePtr, ChatMessagePointer chatMessagePtr, std::string content, int length) {
    if (!chatMessagePtr.refreshPointer()) {
        Log() << " [-] 错误: 刷新指针失败 [ChatMessage]";
        return;
    }

    uintptr_t oldAddr = chatMessagePtr.readAddress(OFFSET_CHAT_MESSAGE_ADDRESS_START);
    if (oldAddr == 0) {
        Log() << " [-] 错误: 读取指针失败 [ChatMessage]";
        return;
    }

    if (suspendAndWrite(messageCavePtr, chatMessagePtr, content, length)) // true if pointer written
        resumePointer(chatMessagePtr, oldAddr);
}

bool WorkerThread::suspendAndWrite(Pointer messageCavePtr, ChatMessagePointer chatMessagePtr, std::string content, int length) {
    // Suspend the process to avoid desynchronized memory access
    if (NtSuspendProcess != NULL)
        NtSuspendProcess(hProcess);
    if (!messageCavePtr.writeString(content)) {
        Log() << " [-] 错误: 写入数据失败 [ChatMessage]";
        return false;
    }

    if (!chatMessagePtr.writeAddress(OFFSET_CHAT_MESSAGE_ADDRESS_START, messageCaveAddr)) {
        Log() << " [-] 错误: 写入指针失败 [ChatMessage]";
        return true;
    }

    if (!chatMessagePtr.writeAddress(OFFSET_CHAT_MESSAGE_ADDRESS_END, messageCaveAddr + length)) {
        Log() << " [-] 错误: 写入数据失败 [ChatLength]";
        return true;
    }
    // Resume the process to perform the send operation
    if (NtResumeProcess != NULL)
        NtResumeProcess(hProcess);

    Log() << " [+] 写入消息数据成功";
    press(VK_RETURN, 20);
    Log() << " [+] 模拟发送完成";

    // Loop to wait for the game to clear the string
    int count = 0;
    while (count++ <= 10) { // wait for 200ms max
        if (!messageCavePtr.readBoolean())
            break;
        Sleep(20);
    }

    // Then suspend the process again and restore the pointer
    if (NtSuspendProcess != NULL)
        NtSuspendProcess(hProcess);
}

void WorkerThread::resumePointer(ChatMessagePointer chatMessagePtr, uintptr_t oldAddress) {
    if (!chatMessagePtr.writeAddress(OFFSET_CHAT_MESSAGE_ADDRESS_START, oldAddress)) {
        Log() << " [-] 错误: 恢复指针失败 [ChatMessage]";
    }
    if (!chatMessagePtr.writeAddress(OFFSET_CHAT_MESSAGE_ADDRESS_END, oldAddress)) {
        Log() << " [-] 错误: 恢复指针失败 [ChatLength]";
    }
    if (NtResumeProcess != NULL)
        NtResumeProcess(hProcess);
    Log() << " [+] 恢复指针完成";
}