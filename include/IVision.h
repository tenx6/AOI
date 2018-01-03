#ifndef _VISION_DETECT_INTERFACE_H_
#define _VISION_DETECT_INTERFACE_H_

#include "../lib/VisionLibrary/include/opencv/cv.h"


class QDetectObj;
class QProfileObj;
class IVision
{
public:
	virtual bool loadCmdData(int nStation) = 0;
	virtual bool loadImage(cv::Mat& matImg) = 0;

	virtual bool setHeightData(cv::Mat& matHeight) = 0;
	virtual bool setProfGrayImg(cv::Mat& imgGray) = 0;
	virtual bool setProf3DImg(QImage& img3D) = 0;
	virtual bool setProfData(QVector<cv::Point2d>& profDatas) = 0;
	virtual bool setProfRange(double x1, double y1, double x2, double y2) = 0;
	virtual void prepareNewProf() = 0;

	virtual bool calculate3DHeight(int nStation, QVector<cv::Mat>& imageMats, cv::Mat& heightMat, cv::Mat& matHeightResultImg) = 0;
	virtual bool generateGrayImage(QVector<cv::Mat>& imageMats, cv::Mat& grayMat) = 0;
	virtual bool matchPosition(cv::Mat& matDisplay, QVector<QDetectObj*>& objTests) = 0;
	virtual bool calculateDetectHeight(cv::Mat& matHeight, QVector<QDetectObj*>& objTests) = 0;
	virtual bool merge3DHeight(QVector<cv::Mat>& matHeights, cv::Mat& matHeight) = 0;

	virtual bool matchAlignment(cv::Mat& matDisplay, QVector<QProfileObj*>& objProfTests) = 0;
	virtual bool calculateDetectProfile(cv::Mat& matHeight, QVector<QProfileObj*>& objProfTests) = 0;
};

#endif