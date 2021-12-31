#ifndef SOCKETMGR_H_
#define SOCKETMGR_H_

#include <memory>
#include <vector>

#include "Socket.h"


class PiMgr;

class SocketMgr
{
    friend class Socket;

    static constexpr int c_monitorPort = 5000;
    static constexpr int c_commandPort = 5001;

    PiMgr * m_owner;

    Socket * m_pSocketMon = nullptr;
    Socket * m_pSocketCmd = nullptr;

    boost::mutex m_acceptMutex;
    boost::condition_variable m_condition;
    boost::mutex m_monitorMutex;

    bool m_connected = false;
    bool m_reading = false;
    bool m_monitoring = false;

    boost::thread m_thread;
    boost::thread m_monitorThread;

    std::unique_ptr<std::vector<unsigned char> > m_pCurrBuffer;
    int m_droppedFrames = 0;

public:
    SocketMgr(PiMgr * owner);
    ~SocketMgr();

    bool IsConnected() const { return m_connected; }
    int GetDroppedFrames() const { return m_droppedFrames; }

    bool Initialize();
    bool WaitForConnection();
    void StartReadingCommands();
    void StartMonitorThread();
    bool ReleaseConnection();
    void Close();

    bool SendFrame(std::unique_ptr<std::vector<unsigned char> > pBuf);

private:
    void ReadCommandsWorker();
    void MonitorWorker();

};

#endif /* SOCKETMGR_H_ */

