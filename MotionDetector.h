#ifndef MOTIONDETECTOR_H_
#define MOTIONDETECTOR_H_

//#include <boost/circular_buffer.hpp>
#include <opencv2/opencv.hpp>


class MotionDetector
{
    cv::Mat frameCurrent;
    cv::Mat framePrevious;
    int threshold;

public:
    MotionDetector(int defaultThreshold);

    void setThreshold(int _threshold)
    {
        threshold = _threshold;
    }
    cv::Mat& getFrame()
    {
        return frameCurrent;
    }

    bool update(cv::Mat & frame);
};

#endif /* MOTIONDETECTOR_H_ */

