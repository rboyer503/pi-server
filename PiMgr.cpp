#include <cstdlib>
#include <fstream>
#include <vector>

#include "PiMgr.h"
#include "Profiling.h"
#include "SocketMgr.h"
#include "VideoCaptureMgr.h"

#define ENABLE_SOCKET_MGR 1

#define FRAME_SKIP 1
#define FRAME_BACKLOG_MIN -5

#define ROI_TOP 72
#define ROI_WIDTH 160
#define ROI_HEIGHT 30

#define DEF_KERNEL_SIZE 5


using namespace std;
using namespace cv;


const char * const PiMgr::c_imageProcModeNames[] = {"None", "Gray", "Blur", "FDR"};
const char * const PiMgr::c_imageProcStageNames[] = {"Gray", "Blur", "Send", "Total"};


PiMgr::PiMgr() :
        m_config(Config(DEF_KERNEL_SIZE))
{
    m_pSocketMgr = new SocketMgr(this);
}

PiMgr::~PiMgr()
{
    delete m_pSocketMgr;

    if (m_thread.joinable())
        m_thread.join();
}

bool PiMgr::Initialize()
{
    // Initialize monitor and command sockets.
#ifdef ENABLE_SOCKET_MGR
    if (!m_pSocketMgr->Initialize())
    {
        m_errorCode = EC_LISTENFAIL;
        return false;
    }
#endif

    m_running = true;
    m_thread = boost::thread(&PiMgr::WorkerFunc, this);
    return true;
}

void PiMgr::Terminate()
{
    m_thread.interrupt();
}

void PiMgr::UpdateIPM()
{
    m_ipm = (eBDImageProcMode)(((int)m_ipm + 1) % IPM_MAX);
    if (m_ipm == IPM_NONE)
        m_ipm = IPM_GRAY;
    cout << "Image processing mode: " << c_imageProcModeNames[m_ipm] << endl;
}

void PiMgr::OutputStatus()
{
    cout << endl << "Statistics" << endl;

    cout << "  Total Frames=" << m_status.numFrames;
    cout << "\tDelayed Frames=" << m_status.numDroppedFrames;

    if (m_status.numFrames == 0)
    {
        cout << endl;
        return;
    }

    cout << "\tAverage FPS=" << (m_status.numFrames / m_diff.total_seconds()) << endl;

    cout << "  Processing times:" << endl;
    for (int i = IPS_GRAY; i < IPS_MAX; ++i)
    {
        cout << "    " << c_imageProcStageNames[i] <<
                ": curr=" << m_status.currProcessUs[i] <<
                ", avg=" << (m_status.totalProcessUs[i] / m_status.numFrames) <<
                ", max=" << m_status.maxProcessUs[i] << endl;
    }
}

void PiMgr::OutputConfig()
{
    cout << endl << "Configuration" << endl;
    cout << "  Image Processing Mode=" << c_imageProcModeNames[m_ipm] << endl;
    cout << "  Current Parameter Page=" << m_paramPage << endl;
    cout << "  Kernel Size=" << (int)m_config.kernelSize << endl;
}

void PiMgr::UpdatePage()
{
    m_paramPage = (eBDParamPage)((m_paramPage + 1) % PP_MAX);
    DisplayCurrentParamPage();
}

void PiMgr::UpdateParam(int param, bool up)
{
    switch (m_paramPage)
    {
    case PP_BLUR:
        if ( up && (m_config.kernelSize < 15) )
            m_config.kernelSize += 2;
        else if (!up && (m_config.kernelSize > 1))
            m_config.kernelSize -= 2;
        break;
    }
}

void PiMgr::DebugCommand()
{
    m_debugTrigger = true;
}

void PiMgr::WorkerFunc()
{
    // Main loop - each iteration handles a client connection.
    // Exit only in response to an error condition or interruption (user cancellation request).
    while (1)
    {
        // Accept an incoming connection.
#ifdef ENABLE_SOCKET_MGR
        if (!m_pSocketMgr->WaitForConnection())
        {
            if (m_interrupted)
                m_errorCode = EC_INTERRUPT;
            else
                m_errorCode = EC_ACCEPTFAIL;
            break;
        }

        // Start accepting commands from client and create monitor worker thread.
        m_pSocketMgr->StartReadingCommands();
        m_pSocketMgr->StartMonitorThread();
#endif

        // Initialize video.
        VideoCaptureMgr vcMgr(this);
        if (!vcMgr.Initialize())
        {
            cerr << "Error: Failed to open video capture." << endl;
            m_errorCode = EC_CAPTUREOPENFAIL;
            break;
        }

        //system("v4l2-ctl --set-ctrl=contrast=100");
        //system("v4l2-ctl --set-ctrl=brightness=90");

        m_status = Status();

        DisplayCurrentParamPage();

        // Continually transmit frames to client.
        Mat * frame;
        int nextFrame = FRAME_SKIP;
        while (1)
        {
            //PROFILE_START;

            // Retrieve frame.
            int skippedFrames;
            do
            {
                if ( (skippedFrames = vcMgr.GetLatest(frame)) < 0 )
                {
                    if (m_interrupted)
                    {
                        cout << "Interrupted while waiting for frame - shutting down server..." << endl;
                        m_errorCode = EC_INTERRUPT;
                    }
                    else
                    {
                        cerr << "Error: Failed to read a frame." << endl;
                        m_errorCode = EC_CAPTUREGRABFAIL;
                    }
                    break;
                }
                nextFrame -= skippedFrames + 1;
                //PROFILE_LOG(read);
            } while (nextFrame > 0);
            if (m_errorCode)
                break;
            nextFrame += FRAME_SKIP;
            if (nextFrame < FRAME_BACKLOG_MIN)
                nextFrame = FRAME_BACKLOG_MIN;

            if (m_status.SuppressionProcessing())
                m_startTime = boost::posix_time::microsec_clock::local_time();

            //PROFILE_LOG(read);

            if (!m_status.IsSuppressed())
            {
                if (nextFrame != FRAME_SKIP)
                {
                    m_status.numDroppedFrames++;

                    // Not possible to "catch up" on backlog when running full speed - just move on.
                    if (FRAME_SKIP == 1)
                        nextFrame = 1;
                }
                m_status.numFrames++;
            }

            // Process frame.
            if (!ProcessFrame(*frame))
            {
                // Suppress error for send failure - just wait for another connection.
                if (m_errorCode == EC_SENDFAIL)
                    m_errorCode = EC_NONE;
                break;
            }

            m_diff = boost::posix_time::microsec_clock::local_time() - m_startTime;

            try
            {
                boost::this_thread::interruption_point();
            }
            catch (boost::thread_interrupted&)
            {
                cout << "Interrupted after processing frame - shutting down server..." << endl;
                m_errorCode = EC_INTERRUPT;
                break;
            }
        } // end video streaming loop

        // Clean up connection.
#ifdef ENABLE_SOCKET_MGR
        if (!m_pSocketMgr->ReleaseConnection())
        {
            cerr << "Error: Connection release failed." << endl;
            m_errorCode = EC_RELEASEFAIL;
        }
#endif

        // Exit program if any error was reported.
        if (m_errorCode)
            break;
    } // end connection handling loop

    m_running = false;
}

bool PiMgr::ProcessFrame(Mat & frame)
{
    PROFILE_START;

    Mat * pFrameDisplay = NULL;
    eBDImageProcMode ipm = m_ipm;
    int processUs[IPS_MAX];
    memset(processUs, 0, sizeof(processUs));

    resize(frame, frame, Size(), 0.5, 0.5, INTER_AREA); //INTER_NEAREST);

    if (ipm == IPM_NONE)
    {
        pFrameDisplay = &frame;
    }
    else if (ipm == IPM_DEBUG)
    {
        pFrameDisplay = ProcessDebugFrame();
    }
    else
    {
        // Convert to downsampled, grayscale image with correct orientation and focus to ROI.
        //resize(frame, m_frameResize, Size(), 0.5, 0.5, INTER_NEAREST);
        //cvtColor(m_frameResize, m_frameGray, COLOR_BGR2GRAY);
        cvtColor(frame, m_frameGray, COLOR_BGR2GRAY);
        //flip(m_frameGray, m_frameGray, -1);
        //m_frameROI = m_frameGray(Rect(0, ROI_TOP, ROI_WIDTH, ROI_HEIGHT));
        m_frameROI = m_frameGray;

        processUs[IPS_GRAY] = PROFILE_DIFF;
        PROFILE_START;

        if (ipm == IPM_GRAY)
        {
            pFrameDisplay = &m_frameROI;
        }
        else
        {
            // Apply gaussian blur.
            GaussianBlur(m_frameROI, m_frameFilter, Size(m_config.kernelSize, m_config.kernelSize), 0, 0);

            processUs[IPS_BLUR] = PROFILE_DIFF;
            PROFILE_START;

            if (ipm == IPM_BLUR)
            {
                if (m_debugMode)
                    pFrameDisplay = &m_frameFilter;
                else
                {
                    //flip(frame, frame, -1);
                    pFrameDisplay = &frame;
                }
            }
        }
    }

    // Encode for wifi transmission.
    unique_ptr<vector<uchar> > pBuf;

    // TODO: Revisit configurable tx ratio.
    // Currently just sending all frames to client.
    //static int sendToClient = 0;
    //if (++sendToClient % 2)
    {
        pBuf = std::make_unique<vector<uchar> >();
        imencode(".bmp", *pFrameDisplay, *pBuf);

        // Transmit to client.
#ifdef ENABLE_SOCKET_MGR
        if (!m_pSocketMgr->SendFrame(std::move(pBuf)))
        {
            // Client probably disconnected - exit streaming loop and wait for a new connection.
            m_errorCode = EC_SENDFAIL;
            return false;
        }
#endif

        processUs[IPS_SENT] = PROFILE_DIFF;
    }

    // Update status.
    if (!m_status.IsSuppressed())
    {
        for (int i = IPS_GRAY; i < IPS_MAX; ++i)
        {
            if (processUs[i])
            {
                if (i < IPS_TOTAL)
                    processUs[IPS_TOTAL] += processUs[i];
                m_status.currProcessUs[i] = processUs[i];
                m_status.totalProcessUs[i] += processUs[i];
                if (processUs[i] > m_status.maxProcessUs[i])
                    m_status.maxProcessUs[i] = processUs[i];
            }
            else
            {
                m_status.currProcessUs[i] = 0;
            }
        }
    }

    return true;
}

Mat * PiMgr::ProcessDebugFrame()
{
    FDRecord & currFDR = m_FDRecords[m_selectedFDRIndex];
    if (m_updateFDR)
    {
        m_updateFDR = false;

        cout << "FDR #" << m_selectedFDRIndex << endl;
    }

    return &(currFDR.frame);
}

void PiMgr::DisplayCurrentParamPage()
{
    cout << "Current parameter page: " << m_paramPage << endl;
    switch (m_paramPage)
    {
    case PP_BLUR:
        cout << "  1) Kernel Size" << endl;
        break;
    }
}
