#ifndef PIMGR_H_
#define PIMGR_H_

#include <algorithm>
#include <limits>
#include <numeric>
#include <opencv2/opencv.hpp>
#include <string>
#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#define STATUS_SUPPRESS_DELAY 10


enum eBDErrorCode
{
    EC_NONE,
    EC_LISTENFAIL,
    EC_ACCEPTFAIL,
    EC_CAPTUREOPENFAIL,
    EC_CAPTUREGRABFAIL,
    EC_SENDFAIL,
    EC_RELEASEFAIL,
    EC_INTERRUPT,
    EC_BADAUTH
};

enum eBDImageProcMode
{
    IPM_NONE,
    IPM_GRAY,
    IPM_BLUR,
    IPM_DEBUG,
    IPM_MAX
};

enum eBDImageProcStage
{
    IPS_GRAY,
    IPS_BLUR,
    IPS_SENT,
    IPS_TOTAL,
    IPS_MAX
};

enum eBDParamPage
{
    PP_BLUR,
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

    Config(unsigned char _kernelSize) :
        kernelSize(_kernelSize)
    {}
};

struct FDRecord
{
    cv::Mat frame;
};


class SocketMgr;


class PiMgr
{
    static const char * const c_imageProcModeNames[];
    static const char * const c_imageProcStageNames[];
    static constexpr int c_maxFDRecords = 150;
    static constexpr int c_frameSkip = 1;
    static constexpr int c_frameBacklogMin = -5;
    static constexpr int c_defKernelSize = 5;
    static constexpr int c_numTxSegments = 4;

    eBDErrorCode m_errorCode = EC_NONE;
    SocketMgr * m_pSocketMgr;
    boost::thread m_thread;
    volatile bool m_running = false;
    bool m_interrupted = false;
    eBDImageProcMode m_ipm = IPM_BLUR;
    Status m_status;
    Config m_config;
    eBDParamPage m_paramPage = PP_BLUR;
    bool m_debugTrigger = false;
    boost::posix_time::ptime m_startTime;
    boost::posix_time::time_duration m_diff;
    cv::Mat m_frameResize;
    cv::Mat m_frameGray;
    cv::Mat m_frameROI;
    cv::Mat m_frameFilter;
    FDRecord m_FDRecords[c_maxFDRecords];
    int m_currFDRIndex = 0;
    bool m_FDRFull = false;
    int m_selectedFDRIndex = 0;
    bool m_updateFDR = true;
    bool m_debugMode = false;

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
    void DebugCommand();
    void ToggleDebugMode() { m_debugMode = !m_debugMode; }

    void PrevFDR()
    {
        int maxIndex = (m_FDRFull ? c_maxFDRecords - 1 : m_currFDRIndex - 1);

        if (--m_selectedFDRIndex < 0)
            m_selectedFDRIndex = maxIndex;

        m_updateFDR = true;
    }
    void NextFDR()
    {
        int maxIndex = (m_FDRFull ? c_maxFDRecords - 1 : m_currFDRIndex - 1);

        if (++m_selectedFDRIndex > maxIndex)
            m_selectedFDRIndex = 0;

        m_updateFDR = true;
    }

private:
    void WorkerFunc();
    bool ProcessFrame(cv::Mat & frame);
    cv::Mat * ProcessDebugFrame();
    void DisplayCurrentParamPage();

};

#endif /* PIMGR_H_ */

