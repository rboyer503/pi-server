#ifndef PIMGR_H_
#define PIMGR_H_

#include <algorithm>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <string>
#include <vector>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <opencv2/opencv.hpp>

#define STATUS_SUPPRESS_DELAY 10


enum eBDErrorCode
{
    EC_NONE,
    EC_LISTENFAIL,
    EC_ACCEPTFAIL,
    EC_CAPTUREOPENFAIL,
    EC_CAPTUREGRABFAIL,
    EC_RELEASEFAIL,
    EC_INTERRUPT
};

enum eBDImageProcMode
{
    IPM_NONE,
    IPM_MOTIONDETECT,
    IPM_GRAY,
    IPM_BLUR,
    IPM_MAX
};

enum eBDImageProcStage
{
    IPS_MOTIONDETECT,
    IPS_GRAY,
    IPS_BLUR,
    IPS_SENT,
    IPS_TOTAL,
    IPS_MAX
};

enum eBDParamPage
{
    PP_BLUR,
    PP_THRESHOLD,
    PP_MAX
};

struct Status
{
    unsigned char suppressDelay;
    int numFrames;
    int numDroppedFrames;
    int currProcessUs[IPS_MAX];
    int totalProcessUs[IPS_MAX];
    int maxProcessUs[IPS_MAX];

public:
    Status() : suppressDelay(STATUS_SUPPRESS_DELAY), numFrames(0), numDroppedFrames(0)
    {
        for (int i = 0; i < IPS_MAX; ++i)
        {
            currProcessUs[i] = totalProcessUs[i] = maxProcessUs[i] = 0;
        }
    }

    bool IsSuppressed() const { return suppressDelay; }

    bool SuppressionProcessing()
    {
        // Return true only first time supression delay expires.
        if (suppressDelay)
        {
            --suppressDelay;
            return (!suppressDelay);
        }
        return false;
    }

};

struct Config
{
    unsigned char kernelSize; // Gaussian kernel size used for preliminary blur op.
    unsigned char threshold;

    Config(unsigned char _kernelSize, unsigned char _threshold) :
        kernelSize(_kernelSize),
        threshold(_threshold)
    {}
};


class SocketMgr;
class MotionDetector;
class NotificationMgr;


class PiMgr
{
    static const char * const c_imageProcModeNames[];
    static const char * const c_imageProcStageNames[];
    static constexpr int c_frameSkip = 2;
    static constexpr int c_frameBacklogMin = -5;
    static constexpr int c_defKernelSize = 5;
    static constexpr int c_defThreshold = 40;
    static constexpr int c_numTxSegments = 4;

    using CompressFramePtr = std::unique_ptr<std::vector<uchar>>;

    eBDErrorCode m_errorCode = EC_NONE;
    SocketMgr * m_pSocketMgr;
    std::unique_ptr<MotionDetector> m_motionDetector;
    std::unique_ptr<NotificationMgr> m_notificationMgr;
    boost::thread m_thread;
    volatile bool m_running = false;
    bool m_interrupted = false;
    eBDImageProcMode m_ipm = IPM_BLUR;
    Status m_status;
    Config m_config;
    eBDParamPage m_paramPage = PP_BLUR;
    boost::posix_time::ptime m_startTime;
    boost::posix_time::time_duration m_diff;
    cv::Mat m_frameGray;
    cv::Mat m_frameFilter;
    bool m_debugMode = false;
    std::queue<CompressFramePtr> m_frameQueue;
    mutable std::mutex m_frameQueueMutex;

public:
    PiMgr();
    ~PiMgr();

    eBDErrorCode GetErrorCode() const { return m_errorCode; }
    bool IsRunning() const { return m_running; }
    bool IsInterrupted() const { return m_interrupted; }
    void SetInterrupted() { m_interrupted = true; }

    bool Initialize();
    void Terminate();

    void UpdateIPM();
    void OutputStatus();
    void OutputConfig();
    void UpdatePage();
    void UpdateParam(int param, bool up);
    void ToggleDebugMode() { m_debugMode = !m_debugMode; }

private:
    void WorkerFunc();
    void ProcessFrame(cv::Mat & frame);
    CompressFramePtr CompressFrame(cv::Mat * pFrame) const;
    void DisplayCurrentParamPage();

};

#endif /* PIMGR_H_ */

