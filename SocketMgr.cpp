#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdio>
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

    m_clientConnThread.interrupt();

    if (m_commandThread.joinable())
        m_commandThread.join();

    if (m_clientConnThread.joinable())
        m_clientConnThread.join();

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

    m_clientConnThread = boost::thread(&SocketMgr::ClientConnectionWorker, this);

    cout << "Socket manager initialized successfully." << endl;
    return true;
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

void SocketMgr::SendFrame(unique_ptr<vector<unsigned char> > pBuf)
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

void SocketMgr::ClientConnectionWorker()
{
    // Handle client connections.
    // Exit only in response to an error condition or interruption (user cancellation request).
    bool done = false;

    while (!done)
    {
        // Accept an incoming connection.
        if (!WaitForConnection())
            break;

        // Start accepting commands from client and wait for authorization token.
        StartReadingCommands();

        while (!m_authorized)
        {
            if (m_badauth)
            {
                cerr << "Error: Bad authorization token." << endl;
                break;
            }

            try
            {
                boost::this_thread::interruption_point();
            }
            catch (boost::thread_interrupted&)
            {
                cout << "Interrupted waiting for authorization token..." << endl;
                m_owner->SetInterrupted();
                done = true;
                break;
            }
        }

        if (!m_authorized)
        {
            if (!ReleaseConnection())
            {
                cerr << "Error: Connection release failed." << endl;
                done = true;
            }

            continue;
        }

        // Transmit frames to client for monitoring as they become available.
        while (true)
        {
            try
            {
                boost::this_thread::sleep(boost::posix_time::milliseconds(5));
            }
            catch (boost::thread_interrupted&)
            {
                cout << "Client connection worker thread interrupted..." << endl;
                m_owner->SetInterrupted();
                done = true;
                break;
            }

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

        // Clean up connection.
        if (!ReleaseConnection())
        {
            cerr << "Error: Connection release failed." << endl;
            done = true;
        }
    }
}

bool SocketMgr::WaitForConnection()
{
    m_authorized = m_badauth = false;
    m_droppedFrames = 0;

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

    // Block until both sockets have completed connection accept processing.
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

    if (m_owner->IsInterrupted())
    {
        return false;
    }

    if (!m_pSocketMon->IsConnected() || !m_pSocketCmd->IsConnected())
    {
        // At least one socket accept failed.
        // Close both to get to a known state.
        m_pSocketMon->Close();
        m_pSocketCmd->Close();
        return false;
    }

    cout << "Socket manager accepted new connection." << endl;
    return true;
}

void SocketMgr::StartReadingCommands()
{
    m_commandThread = boost::thread(&SocketMgr::ReadCommandsWorker, this);
}

bool SocketMgr::ReleaseConnection()
{
    bool ret = true;

    // Shut down sockets to give opportunity for command reading thread to exit.
    if (m_pSocketMon)
        ret = ret && m_pSocketMon->Shutdown();

    if (m_pSocketCmd)
        ret = ret && m_pSocketCmd->Shutdown();

    if (m_commandThread.joinable())
        m_commandThread.join();

    // Close actual sockets.
    if (m_pSocketMon)
        m_pSocketMon->Close();

    if (m_pSocketCmd)
        m_pSocketCmd->Close();

    cout << "Socket manager released connection." << endl;
    return ret;
}

void SocketMgr::ReadCommandsWorker()
{
    // Wait for client commands on command socket and handle them.
    char * recvBuffer;
    while ( (recvBuffer = m_pSocketCmd->ReceiveCommand()) != nullptr )
    {
        if (!m_authorized)
        {
            string token = recvBuffer;
            if (token == GetToken())
                m_authorized = true;
            else
                m_badauth = true;
        }
        else
        {
            if (strcmp(recvBuffer, "status") == 0)
                m_owner->OutputStatus();
            else if (strcmp(recvBuffer, "config") == 0)
                m_owner->OutputConfig();
            else if (strcmp(recvBuffer, "mode") == 0)
                m_owner->UpdateIPM();
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
            else if (strcmp(recvBuffer, "debugmode") == 0)
                m_owner->ToggleDebugMode();
        }
    }

    cout << "Socket manager command reader thread exited." << endl;
}

std::string SocketMgr::GetToken() const
{
    // Read token from token file, then delete it.
    ifstream ifs(c_tokenFile);

    if (!ifs.is_open())
    {
        cerr << "Error: Cannot open token file." << endl;
        return {};
    }

    stringstream buffer;
    buffer << ifs.rdbuf();

    ifs.close();

    remove(c_tokenFile);

    return buffer.str();
}
