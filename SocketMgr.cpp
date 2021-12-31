#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdlib>

#include "SocketMgr.h"
#include "PiMgr.h"
#include "Profiling.h"


using namespace std;


SocketMgr::SocketMgr(PiMgr * owner) :
    m_owner(owner)
{
}

SocketMgr::~SocketMgr()
{
    Close();

    if (m_thread.joinable())
        m_thread.join();

    if (m_monitorThread.joinable())
        m_monitorThread.join();

    cout << "Socket manager destroyed." << endl;
}

bool SocketMgr::Initialize()
{
    m_pSocketMon = new Socket(this, c_monitorPort);
    if (!m_pSocketMon->EstablishListener())
    {
        delete m_pSocketMon;
        m_pSocketMon = nullptr;
        return false;
    }
    cout << "Monitor socket listening..." << endl;

    m_pSocketCmd = new Socket(this, c_commandPort);
    if (!m_pSocketCmd->EstablishListener())
    {
        delete m_pSocketMon;
        m_pSocketMon = nullptr;
        delete m_pSocketCmd;
        m_pSocketCmd = nullptr;
        return false;
    }
    cout << "Command socket listening..." << endl;

    cout << "Socket manager initialized successfully." << endl;
    return true;
}

bool SocketMgr::WaitForConnection()
{
    // Start worker threads to accept a connection for each socket.
    if (!m_pSocketMon->AcceptConnection())
    {
        cerr << "Error: Monitor socket already connected." << endl;
        return false;
    }
    if (!m_pSocketCmd->AcceptConnection())
    {
        m_pSocketMon->Close();
        cerr << "Error: Command socket already connected." << endl;
        return false;
    }

    // Block until both sockets have complete connection accept processing.
    {
        boost::mutex::scoped_lock lock(m_acceptMutex);
        try
        {
            while (m_pSocketMon->IsAccepting() || m_pSocketCmd->IsAccepting())
                m_condition.wait(lock);
        }
        catch (boost::thread_interrupted&)
        {
            cout << "Interrupted while waiting for connection - shutting down server..." << endl;
            m_owner->SetInterrupted();
        }
    }

    if (!m_pSocketMon->IsConnected() || !m_pSocketCmd->IsConnected() || m_owner->IsInterrupted())
    {
        // At least one socket accept failed.
        // Close both to get to a known state.
        m_pSocketMon->Close();
        m_pSocketCmd->Close();
        return false;
    }

    cout << "Socket manager accepted new connection." << endl;
    m_connected = true;
    m_droppedFrames = 0;
    return true;
}

void SocketMgr::StartReadingCommands()
{
    m_reading = true;
    m_thread = boost::thread(&SocketMgr::ReadCommandsWorker, this);
}

void SocketMgr::StartMonitorThread()
{
    m_monitoring = true;
    m_monitorThread = boost::thread(&SocketMgr::MonitorWorker, this);
}

bool SocketMgr::ReleaseConnection()
{
    bool ret = true;

    // Shut down sockets to give opportunity for command reading thread to exit.
    if (m_pSocketMon)
        ret = ret && m_pSocketMon->Shutdown();

    if (m_pSocketCmd)
        ret = ret && m_pSocketCmd->Shutdown();

    if (m_thread.joinable())
        m_thread.join();

    if (m_monitorThread.joinable())
        m_monitorThread.join();

    // Close actual sockets.
    if (m_pSocketMon)
        m_pSocketMon->Close();

    if (m_pSocketCmd)
        m_pSocketCmd->Close();

    cout << "Socket manager released connection." << endl;
    return ret;
}

void SocketMgr::Close()
{
    // Destroy sockets in preparation to program termination.
    delete m_pSocketMon;
    m_pSocketMon = nullptr;
    cout << "Monitor socket released..." << endl;

    delete m_pSocketCmd;
    m_pSocketCmd = nullptr;
    cout << "Command socket released..." << endl;
}

bool SocketMgr::SendFrame(unique_ptr<vector<unsigned char> > pBuf)
{
    if (!m_monitoring)
        return false;

    {
        boost::mutex::scoped_lock lock(m_monitorMutex);
        if (!m_pCurrBuffer)
            m_pCurrBuffer = move(pBuf);
        else
        {
            ++m_droppedFrames;

            cout << "DEBUG: Dropped frames=" << m_droppedFrames << endl;
        }
    }

    return true;
}

void SocketMgr::ReadCommandsWorker()
{
    // Wait for client commands on command socket and handle them.
    char * recvBuffer;
    while ( (recvBuffer = m_pSocketCmd->ReceiveCommand()) != nullptr )
    {
        if (strcmp(recvBuffer, "mode") == 0)
            m_owner->UpdateIPM();
        else if (strcmp(recvBuffer, "status") == 0)
            m_owner->OutputStatus();
        else if (strcmp(recvBuffer, "config") == 0)
            m_owner->OutputConfig();
        else if (strcmp(recvBuffer, "page") == 0)
            m_owner->UpdatePage();
        else if (strcmp(recvBuffer, "param1 up") == 0)
            m_owner->UpdateParam(1, true);
        else if (strcmp(recvBuffer, "param1 down") == 0)
            m_owner->UpdateParam(1, false);
        else if (strcmp(recvBuffer, "param2 up") == 0)
            m_owner->UpdateParam(2, true);
        else if (strcmp(recvBuffer, "param2 down") == 0)
            m_owner->UpdateParam(2, false);
        else if (strcmp(recvBuffer, "debug") == 0)
            m_owner->DebugCommand();
        else if (strcmp(recvBuffer, "debugmode") == 0)
        {
            m_owner->ToggleDebugMode();
        }
    }

    cout << "Socket manager command reader thread exited." << endl;
}

void SocketMgr::MonitorWorker()
{
    // Transmit frames to client for monitoring as they become available.
    while (true)
    {
        boost::this_thread::sleep(boost::posix_time::milliseconds(5));

        unique_ptr<vector<unsigned char> > pBuf;

        {
            boost::mutex::scoped_lock lock(m_monitorMutex);
            if (!m_pCurrBuffer)
                continue;
            else
                pBuf = move(m_pCurrBuffer);
        }

        //PROFILE_START;

        // Delegate to the monitor socket.
        if (!m_pSocketMon->TransmitSizedMessage(&(*pBuf)[0], pBuf->size()))
            break;

        //PROFILE_LOG(MSGOUT);
    }

    m_monitoring = false;
    cout << "Socket manager monitor thread exited." << endl;
}
