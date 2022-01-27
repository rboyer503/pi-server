#include <cstdlib>
#include <fstream>
#include <vector>

#include "PiMgr.h"
#include "Profiling.h"
#include "SocketMgr.h"
#include "VideoCaptureMgr.h"


using namespace std;
using namespace cv;


const char * const PiMgr::c_imageProcModeNames[] = {"None", "Gray", "Blur", "FDR"};
const char * const PiMgr::c_imageProcStageNames[] = {"Gray", "Blur", "Send", "Total"};


PiMgr::PiMgr() :
    m_config(Config(c_defKernelSize))
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
    if (!m_pSocketMgr->Initialize())
    {
        m_errorCode = EC_LISTENFAIL;
        return false;
    }

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
    // Initialize video.
    VideoCaptureMgr vcMgr(this);
    if (!vcMgr.Initialize())
    {
        cerr << "Error: Failed to open video capture." << endl;
        m_errorCode = EC_CAPTUREOPENFAIL;
        m_running = false;
        return;
    }

    //system("v4l2-ctl --set-ctrl=contrast=100");
    //system("v4l2-ctl --set-ctrl=brightness=90");

    m_status = Status();

    DisplayCurrentParamPage();

    // Continually process frames.
    Mat * frame;
    int nextFrame = c_frameSkip;
    while (true)
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
        nextFrame += c_frameSkip;
        if (nextFrame < c_frameBacklogMin)
            nextFrame = c_frameBacklogMin;

        if (m_status.SuppressionProcessing())
            m_startTime = boost::posix_time::microsec_clock::local_time();

        //PROFILE_LOG(read);

        if (!m_status.IsSuppressed())
        {
            if (nextFrame != c_frameSkip)
            {
                m_status.numDroppedFrames++;

                // Not possible to "catch up" on backlog when running full speed - just move on.
                if (c_frameSkip == 1)
                    nextFrame = 1;
            }
            m_status.numFrames++;
        }

        ProcessFrame(*frame);

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
    }

    m_running = false;
}

void PiMgr::ProcessFrame(Mat & frame)
{
    PROFILE_START;

    Mat * pFrameDisplay = nullptr;
    eBDImageProcMode ipm = m_ipm;
    int processUs[IPS_MAX];
    memset(processUs, 0, sizeof(processUs));

    //resize(frame, frame, Size(), 0.5, 0.5, INTER_AREA); //INTER_NEAREST);

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

    // Encode as PNG with fast compression.
    vector<int> compression_params;
    compression_params.push_back(IMWRITE_PNG_COMPRESSION);
    compression_params.push_back(1);

    // Parallel processing for PNG encoding.
    // Break image into segments and independently encode them.
    vector<uchar> buffers[c_numTxSegments];
    int segmentHeight = pFrameDisplay->rows / c_numTxSegments;

    #pragma omp parallel for
    for (int i = 0; i < c_numTxSegments; ++i)
    {
        Mat mat = (*pFrameDisplay)(Rect(0, segmentHeight * i, pFrameDisplay->cols, segmentHeight));
        imencode(".png", mat, buffers[i], compression_params);
    }

    // Concatenate length-value pairs of buffers.
    size_t bufferSize = c_numTxSegments * sizeof(int32_t);
    for (int i = 0; i < c_numTxSegments; ++i)
    {
        bufferSize += buffers[i].size();
    }

    pBuf = make_unique<vector<uchar> >();
    pBuf->reserve(bufferSize);

    for (int i = 0; i < c_numTxSegments; ++i)
    {
        int32_t size = buffers[i].size();
        uchar * sizeData = reinterpret_cast<uchar *>(&size);
        pBuf->insert(pBuf->end(), sizeData, sizeData + sizeof(int32_t));
        pBuf->insert(pBuf->end(), buffers[i].begin(), buffers[i].end());
    }

    // Transmit to client.
    m_pSocketMgr->SendFrame(move(pBuf));

    processUs[IPS_SENT] = PROFILE_DIFF;

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
