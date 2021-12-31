#ifndef SOCKET_H_
#define SOCKET_H_

#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>


class SocketMgr;

class Socket
{
    static constexpr int c_recvBufferSize = 100;

    SocketMgr * m_pSocketMgr;
    int m_port;
    int m_listenfd = -1;
    int m_connfd = -1;
    volatile bool m_accepting = false;
    boost::thread m_thread;
    char m_recvBuffer[c_recvBufferSize];

public:
    Socket(SocketMgr * pSocketMgr, int port);
    ~Socket();

    bool IsAccepting() const { return m_accepting; }
    bool IsConnected() const { return (m_connfd >= 0); }

    bool EstablishListener();
    bool AcceptConnection();
    bool TransmitSizedMessage(unsigned char * pRawData, int size);
    char * ReceiveCommand();
    bool Shutdown();
    void Close();

private:
    void AcceptWorker();
};

#endif /* SOCKET_H_ */

