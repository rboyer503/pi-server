#ifndef VIDEOCAPTUREMGR_H_
#define VIDEOCAPTUREMGR_H_

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <libv4l2.h>
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <opencv2/opencv.hpp>
#include <queue>


struct RawBuffer
{
    void * start;
    size_t length;
};


class PiMgr;

class VideoCaptureMgr
{
    static constexpr int c_numBuffers = 10;
    static constexpr int c_minQueueHeadspace = 3; // User-owned buffer, Just captured buffer, and filling buffer

    PiMgr * m_owner;
    int m_fd = -1;
    fd_set m_fds;
    RawBuffer m_buffers[c_numBuffers];
    boost::thread m_thread;
    volatile bool m_capturing = false;
    std::queue<int> m_readyQueue;
    std::queue<int> m_freeQueue;
    boost::mutex m_readyQueueMutex;
    boost::mutex m_freeQueueMutex;
    boost::condition_variable m_condition;
    cv::Mat * m_pCurrImage = nullptr;
    int m_currIndex = -1;

public:
    VideoCaptureMgr(PiMgr * owner);
    ~VideoCaptureMgr();

    bool Initialize();
    bool IsCapturing() const { return m_capturing; }
    int GetLatest(cv::Mat *& image);

private:
    bool SetImageFormat();
    bool SetFrameRate(int fps);
    bool AllocateVideoMemory();
    void DeAllocateVideoMemory();
    void DoCapture();
    bool ReEnqueue(int index);

    int xioctl(int fd, int request, void * arg);
};

#endif /* VIDEOCAPTUREMGR_H_ */

