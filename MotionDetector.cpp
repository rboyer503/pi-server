#include "MotionDetector.h"


using namespace std;
using namespace cv;


MotionDetector::MotionDetector(int defaultThreshold) :
    threshold(defaultThreshold)
{
}

bool MotionDetector::update(Mat & frame)
{
    // Trivial diff between current and previous image for now.
    // TODO: Improve algorithm.

    // Reduced, grayscale image for efficiency.
    resize(frame, frameCurrent, Size(), 0.5, 0.5);
    cvtColor(frameCurrent, frameCurrent, COLOR_BGR2GRAY);

    int voteCount = 0;
    if (!framePrevious.empty())
    {
        Mat frameDiff;
        absdiff(framePrevious, frameCurrent, frameDiff);
        cv::threshold(frameDiff, frameDiff, threshold, 255, THRESH_BINARY);

        voteCount = countNonZero(frameDiff);
        if (voteCount)
        {
            cout << "COUNT: " << voteCount << endl;
        }
    }
    framePrevious = frameCurrent;

    return (voteCount > 0);
}
