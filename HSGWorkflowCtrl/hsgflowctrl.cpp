﻿#include <time.h>
#include <qthread.h>
#include <qthreadpool.h>
#include <qmessagebox.h>
#include <QApplication>
#include <qdebug.h>
#include <qthreadpool.h>
#include <QFileDialog>
#include < QThread>
#include <QTime>

#include "hsgflowctrl.h"
#include "../Common/ModuleMgr.h"
#include "../Common/SystemData.h"
#include "../include/IMotion.h"
#include "../include/IData.h"
#include "../include/IDlp.h"
#include "../include/ICamera.h"
#include "../include/IVision.h"
#include "../include/IVisionUI.h"
#include "../include/IdDefine.h"
#include "../Common/eos.h"
#include "../Common/CVSFile.h"

#include "../include/IFlowCtrl.h"
#include "../Common/ThreadPrioc.h"
#include "CryptLib.h"
#include <opencv2/highgui/highgui.hpp>

#define POS_ZHOME   "zHome%0"
#define PROFILE_X   "xMoveProfile%0"
const QString TimeFormatString ="MMdd";

QCheckerRunable::QCheckerRunable(QCheckerParamMap * paramMap, Q3DStructData* dataParam)
	:m_paramMap(paramMap), m_dataParam(dataParam), m_exit(false), m_normal("evAiValue.xml")
{	
	//SetThreadAffinityMask(GetCurrentThread(), 0x01);	
}

QCheckerRunable::~QCheckerRunable()
{

}

void QCheckerRunable::quit()
{
	m_exit = true;	
}

void QCheckerRunable::imgStop()
{	
}

bool QCheckerRunable::startUpSetup()
{
	IDlp* pDlp = getModule<IDlp>(DLP_MODEL);
	if (!pDlp) return false;

	int iStation = getStationID();
	return pDlp->isConnected(iStation - 1);
}

bool QCheckerRunable::endUpSetup()
{
	IDlp* pDlp = getModule<IDlp>(DLP_MODEL);
	if (!pDlp) return false;

	int iStation = getStationID();

	return true;
}

void QCheckerRunable::run()
{
	int iStation = getStationID();	

	if (!startUpSetup())
	{
		System->setTrackInfo(QString(QStringLiteral("工位%0启动失败！")).arg(iStation));
		return;
	}
	System->setTrackInfo(QString(QStringLiteral("工位%0启动成功")).arg(iStation));

	int nDlpMode = System->getParam("sys_run_dlp_mode").toInt();
	bool bMotionCardTrigger = (1 == nDlpMode);

	double dtime_start = 0, dtime_movePos = 0;
	while(!isExit())
	{	
		if (!waitStartBtn())continue;
		if(isExit())break;

		if (!bMotionCardTrigger)
		{
			dtime_start = double(clock());
			if (!captureImages())continue;
			if (isExit())break;
			dtime_movePos = double(clock());
			System->setTrackInfo(QStringLiteral("Station%1:采集图像数据: %2 ms").arg(iStation).arg(dtime_movePos - dtime_start), true);
		}	
		
		dtime_start = double(clock());
		if (!generateGrayImage(bMotionCardTrigger))continue;
		if (isExit())break;
		dtime_movePos = double(clock());
		System->setTrackInfo(QStringLiteral("Station%1:计算3D灰度图: %2 ms").arg(iStation).arg(dtime_movePos - dtime_start), true);

		dtime_start = double(clock());
		if (!calculate3DHeight(bMotionCardTrigger))continue;
		if (isExit())break;
		dtime_movePos = double(clock());
		System->setTrackInfo(QStringLiteral("Station%1:计算3D高度数据: %2 ms").arg(iStation).arg(dtime_movePos - dtime_start), true);

		if (!waitCheckDone())continue;
		if (isExit())break;
	}

	endUpSetup();

	System->setTrackInfo(QString(QStringLiteral("工位%0已停止")).arg(getStationID()));
}

bool QCheckerRunable::waitStartBtn()
{
	IData * pData = getModule<IData>(DATA_MODEL);
	if (pData)
	{
		while (1)
		{
			if (m_dataParam->_bStartCapturing)
			{
				return true;
			}
			
			if (isExit())return false;
			QThread::msleep(10);
		}

		return true;
	}

	return false;
}

bool QCheckerRunable::captureImages()
{
	ICamera* pCam = getModule<ICamera>(CAMERA_MODEL);
	if (!pCam) return false;

	IDlp* pDlp = getModule<IDlp>(DLP_MODEL);
	if (!pDlp) return false;

	IVision* pVision = getModule<IVision>(VISION_MODEL);
	if (!pVision) return false;	

	m_imageMats.clear();

	int nWaitTime = 5 * 100;
	while (!pCam->isCameraCaptureAvaiable() && nWaitTime-- > 0 && !isExit())
	{
		QThread::msleep(10);
	}
	if (nWaitTime <= 0) return false;

	int iStation = getStationID();
	
	if (!pCam->lockCameraCapture())
	{
		return false;
	}
	
	//QEos::Notify(EVENT_CHECK_STATE, iStation, STATION_STATE_CAPTURING, 0);

	if (!pCam->startCapturing())
	{
		System->setTrackInfo(QString("startCapturing error,station %1").arg(iStation));
		pCam->unlockCameraCapture();
		return false;
	}

	System->setTrackInfo(QString("Trigger Station:%1").arg(iStation));

	if (!pDlp->isConnected(iStation - 1) || !pDlp->trigger(iStation - 1))
	{	
		System->setTrackInfo(QString("DLP not connection error,station %1").arg(iStation));
		pCam->unlockCameraCapture();
		return false;
	}

	nWaitTime = 5 * 100;
	while (!pCam->isCaptureImageBufferDone() && nWaitTime-- > 0 && !isExit())
	{
		QThread::msleep(10);
	}

	if (nWaitTime <= 0)
	{
		System->setTrackInfo(QString("CaptureImageBufferDone error,station %1").arg(iStation));
		pCam->unlockCameraCapture();
		return false;
	}

	int nCaptureNum = pCam->getImageBufferCaptureNum();
	for (int i = 0; i < nCaptureNum; i++)
	{
		cv::Mat matImage = pCam->getImageItemBuffer(i);
		m_imageMats.push_back(matImage.clone());
	}

	if (nCaptureNum < pCam->getImageBufferNum())
	{
		pCam->unlockCameraCapture();
		System->setTrackInfo(QString("System captureImages error, Image Num: %1").arg(nCaptureNum));

		bool bCaptureImage = System->getParam("camera_cap_image_enable").toBool();
		if (!bCaptureImage)
		{
			QString capturePath = System->getParam("camera_cap_image_path").toString();

			QDateTime dtm = QDateTime::currentDateTime();
			QString fileDir = capturePath + "/" + dtm.toString("MMddhhmmss") + "/";
			QDir dir; dir.mkdir(fileDir);

			for (int i = 0; i < m_imageMats.size(); i++)
			{
				IplImage frameImg = IplImage(m_imageMats[i]);
				QString name = QString("%1").arg(i + 1, 2, 10, QChar('0')) + QStringLiteral(".bmp");
				cvSaveImage((fileDir + name).toStdString().c_str(), &frameImg);

				QString nameEncrypt = fileDir + QString("%1").arg(i + 1, 2, 10, QChar('0')) + ".ent";
				AOI::Crypt::EncryptFileNfg((fileDir + name).toStdString(), nameEncrypt.toStdString());
				QFile::remove(fileDir + name);
			}
		}

		return false;
	}
	System->setTrackInfo(QString("System captureImages Image Num: %1").arg(nCaptureNum));
	
	pCam->unlockCameraCapture();	

	return true;
}

bool QCheckerRunable::calculate3DHeight(bool bMotionCardTrigger)
{
	ICamera* pCam = getModule<ICamera>(CAMERA_MODEL);
	if (!pCam) return false;

	IVision* pVision = getModule<IVision>(VISION_MODEL);
	if (!pVision) return false;

	int iStation = getStationID();
	//QEos::Notify(EVENT_CHECK_STATE, iStation, STATION_STATE_CALCULATE_3D, 0);

	if (pVision)
	{
		if (pVision->calculate3DHeight(iStation, bMotionCardTrigger ? m_dataParam->_srcImageMats : m_imageMats, m_3DMatHeight, m_matHeightResultImg));
		{			
			//pCam->setImage(m_matHeightResultImg);
			//pCam->setHeightData(m_3DMatHeight);
			m_dataParam->_3DMatHeight = m_3DMatHeight;
			return true;
		}
	}

	return false;
}

bool QCheckerRunable::generateGrayImage(bool bMotionCardTrigger)
{
	ICamera* pCam = getModule<ICamera>(CAMERA_MODEL);
	if (!pCam) return false;

	IVision* pVision = getModule<IVision>(VISION_MODEL);
	if (!pVision) return false;

	int iStation = getStationID();
	//QEos::Notify(EVENT_CHECK_STATE, iStation, STATION_STATE_GENERATE_GRAY, 0);

	if (pVision)
	{
		if (pVision->generateGrayImage(bMotionCardTrigger ? m_dataParam->_srcImageMats : m_imageMats, m_matImage))
		{	
			m_dataParam->_matImage = m_matImage;
			//pCam->setImage(m_matImage, false);
			//QEos::Notify(EVENT_RESULT_DISPLAY, 0, STATION_RESULT_IMAGE_DISPLAY);
			return true;
		}
	}

	return false;
}

bool QCheckerRunable::waitCheckDone()
{
	//int iStation = getStationID();

	//IData * pData = getModule<IData>(DATA_MODEL);
	//if (pData)
	//{
	//}

	m_dataParam->_bStartCapturing = false;
	m_dataParam->_bCapturedDone = true;

	return true;
}

bool QCheckerRunable::isExit()
{
	return m_exit;
}

void QCheckerRunable::addImageText(cv::Mat image, cv::Point ptPos, QString szText)
{
	double dScaleFactor = 2.0;


	cv::String text = szText.toStdString();

	cv::Point ptPos1, ptPos2;
	ptPos1.x = ptPos.x - 10 * dScaleFactor;
	ptPos1.y = ptPos.y + 10 * dScaleFactor;
	ptPos2.x = ptPos.x + 10 * dScaleFactor * text.length();
	ptPos2.y = ptPos.y - 20 * dScaleFactor;
	cv::rectangle(image, ptPos1, ptPos2, cv::Scalar(125, 125, 125), -1);
	cv::rectangle(image, ptPos1, ptPos2, cv::Scalar(200, 200, 200), 1);

	double fontScale = dScaleFactor*0.5f;
	cv::putText(image, text, ptPos, CV_FONT_HERSHEY_COMPLEX, fontScale, cv::Scalar(0, 255, 255), 1);
}

int QCheckerRunable::getStationID()
{
	return m_paramMap->value(PARAM_STATION_ID).toInt();
}

//*****************************************************************************
//
//
//******************************************************************************
#define MTR_READY_POS			0
#define MTR_MOVETO_POS1			1
#define MTR_MOVETO_POS2			2
#define MTR_MOVETO_POS3			5
#define MTR_MOVETO_POS4			6

QMainRunable::QMainRunable(QCheckerParamMap * paramMap, QCheckerParamDataList* paramData)
	:m_paramMap(paramMap), m_paramData(paramData), m_exit(false), m_normal("evAiValue.xml")
{

	m_positionMap.insert(0, MTR_MOVETO_POS1);
	m_positionMap.insert(1, MTR_MOVETO_POS2);
	m_positionMap.insert(2, MTR_MOVETO_POS3);
	m_positionMap.insert(3, MTR_MOVETO_POS4);

	m_nImageIndex = 0;
}

QMainRunable::~QMainRunable()
{

}

int QMainRunable::getPositionNum()
{
	return m_positionMap.size();
}

int QMainRunable::getPositionID(int nIndex)
{
	return m_positionMap.value(nIndex);
}

void QMainRunable::quit()
{
	resetResoultLight();
	m_exit = true;
}

void QMainRunable::imgStop()
{
}

void QMainRunable::setImageIndex(int nIndex)
{
	if (nIndex < 0 || nIndex >= g_nImageStructDataNum) return;

	m_nImageIndex = nIndex;
}

bool QMainRunable::preRunning()
{
	if (!moveToReadyPos())
	{
		System->setTrackInfo(QString(QStringLiteral("系统运动Ready位置错误！")));
		return false;
	}

	m_3DMatHeights.clear();
	m_matImages.clear();	

	for (int i = 0; i < getPositionNum(); i++)
	{
		m_3DMatHeights.push_back(cv::Mat());
		m_matImages.push_back(QImageStruct());		
	}

	return true;
}

void QMainRunable::run()
{
	if (!preRunning()) return;

	System->setTrackInfo(QString(QStringLiteral("主流程启动成功!")));	

	double dtime_start = 0, dtime_movePos = 0;
	while (!isExit())
	{
		if (!waitStartBtn())continue;
		if (isExit())break;
		
		QString szImagePath = generateImagePath();
		bool bCaptureError = false;
		for (int i = 0; i < getPositionNum(); i++)
		{
			if (!moveToCapturePos(i))
			{
				bCaptureError = true;
				break;
			}
			if (isExit())break;

			dtime_start = double(clock());
			if (!captureImages(i, szImagePath)) 
			{
				bCaptureError = true;
				break;
			}
			if (isExit())break;
			dtime_movePos = double(clock());
			System->setTrackInfo(QStringLiteral("Board%1:采集图像并生成3D数据: %2 ms").arg(0).arg(dtime_movePos - dtime_start), true);
		}
		if (bCaptureError) continue;
		if (isExit())break;

		dtime_start = double(clock());
		if (!mergeImages(szImagePath))continue;
		if (isExit())break;
		dtime_movePos = double(clock());
		System->setTrackInfo(QStringLiteral("Board%1:合成数据: %2 ms").arg(0).arg(dtime_movePos - dtime_start), true);

		dtime_start = double(clock());
		if (!matchPosition())continue;
		if (isExit())break;
		dtime_movePos = double(clock());
		System->setTrackInfo(QStringLiteral("Board%1:模板匹配元件: %2 ms").arg(0).arg(dtime_movePos - dtime_start), true);

        dtime_start = double(clock());
		if (!doAlignment())continue;
		if (isExit())break;
		dtime_movePos = double(clock());
		System->setTrackInfo(QStringLiteral("Do fiducial mark alignment: %1 ms").arg(dtime_movePos - dtime_start), true);

		dtime_start = double(clock());
		if (!calculateDetectHeight())continue;
		if (isExit())break;
		dtime_movePos = double(clock());
		System->setTrackInfo(QStringLiteral("Board%1:计算3D测量数据: %2 ms").arg(0).arg(dtime_movePos - dtime_start), true);

		if (!waitCheckDone())continue;
		if (isExit())break;
	}

	if (isExit())
	{
		IData * pData = getModule<IData>(DATA_MODEL);
		if (pData)
		{
			pData->decrementCycleTests();
		}
	}

	System->setTrackInfo(QString(QStringLiteral("主流程已停止")));
}

bool QMainRunable::waitStartBtn()
{
	IData * pData = getModule<IData>(DATA_MODEL);
	if (pData)
	{
		int iState = 0;

		QEos::Notify(EVENT_CHECK_STATE, 0, STATION_STATE_WAIT_START, 0);

		while (1)
		{
			if (pData->getCycleTests() > 0)
			{
				return true;
			}

			if (isExit())return false;
			QThread::msleep(10);
		}

		return true;
	}

	return false;
}

bool QMainRunable::moveToReadyPos()
{
	IMotion* pMotion = getModule<IMotion>(MOTION_MODEL);
	if (!pMotion) return false;

	if (!pMotion->moveToPosGroup(MTR_READY_POS, true))
	{
		System->setTrackInfo(QString("move to position error"));
		return false;
	}
	return true;
}

bool QMainRunable::moveToCapturePos(int nIndex)
{
	IMotion* pMotion = getModule<IMotion>(MOTION_MODEL);
	if (!pMotion) return false;

	if (!pMotion->moveToPosGroup(getPositionID(nIndex), true))
	{
		System->setTrackInfo(QString("move to position error"));
		return false;
	}
	return true;
}

bool QMainRunable::captureAllImages(QVector<cv::Mat>& imageMats)
{
	ICamera* pCam = getModule<ICamera>(CAMERA_MODEL);
	if (!pCam) return false;

	IDlp* pDlp = getModule<IDlp>(DLP_MODEL);
	if (!pDlp) return false;

	IVision* pVision = getModule<IVision>(VISION_MODEL);
	if (!pVision) return false;

	IMotion* pMotion = getModule<IMotion>(MOTION_MODEL);
	if (!pMotion) return false;

	imageMats.clear();	

	if (!pCam->startCapturing())
	{
		System->setTrackInfo(QString("startCapturing error"));	
		return false;
	}
	
	if (!pMotion->triggerCapturing(IMotion::TRIGGER_ALL, true))
	{
		System->setTrackInfo(QString("triggerCapturing error"));	
		return false;
	}	

	int nWaitTime = 10 * 100;
	while (!pCam->isCaptureImageBufferDone() && nWaitTime-- > 0 && !isExit())
	{
		QThread::msleep(10);
	}

	if (nWaitTime <= 0)
	{
		System->setTrackInfo(QString("CaptureImageBufferDone error"));		
		return false;
	}

	int nCaptureNum = pCam->getImageBufferCaptureNum();
	for (int i = 0; i < nCaptureNum; i++)
	{
		cv::Mat matImage = pCam->getImageItemBuffer(i);
		imageMats.push_back(matImage);
	}

	if (nCaptureNum != pCam->getImageBufferNum())
	{	
		System->setTrackInfo(QString("System captureImages error, Image Num: %1").arg(nCaptureNum));

		bool bCaptureImage = System->getParam("camera_cap_image_enable").toBool();
		if (!bCaptureImage)
		{
			QString capturePath = System->getParam("camera_cap_image_path").toString();

			QDateTime dtm = QDateTime::currentDateTime();
			QString fileDir = capturePath + "/" + dtm.toString("MMddhhmmss") + "/";
			QDir dir; dir.mkdir(fileDir);

			for (int i = 0; i < imageMats.size(); i++)
			{
				IplImage frameImg = IplImage(imageMats[i]);
				QString name = QString("%1").arg(i + 1, 2, 10, QChar('0')) + QStringLiteral(".bmp");
				cvSaveImage((fileDir + name).toStdString().c_str(), &frameImg);
			}
		}

		return false;
	}
	System->setTrackInfo(QString("System captureImages Image Num: %1").arg(nCaptureNum));	

	return true;
}

bool QMainRunable::captureImages(int nIndex, QString& szImagePath)
{
	ICamera* pCam = getModule<ICamera>(CAMERA_MODEL);
	if (!pCam) return false;

	IDlp* pDlp = getModule<IDlp>(DLP_MODEL);
	if (!pDlp) return false;

	IVision* pVision = getModule<IVision>(VISION_MODEL);
	if (!pVision) return false;

	IVisionUI* pUI = getModule<IVisionUI>(UI_MODEL);
	if (!pUI) return false;

	QEos::Notify(EVENT_CHECK_STATE, 0, STATION_STATE_CAPTURING, 0);

	int nDlpMode = System->getParam("sys_run_dlp_mode").toInt();
	bool bMotionCardTrigger = (1 == nDlpMode);

	int nStationNum = System->getParam("motion_trigger_dlp_num_index").toInt() == 0 ? 2 : 4;
	for (int i = 0; i < nStationNum; i++)
	{
		if (!pDlp->isConnected(i))
		{
			System->setTrackInfo(QString("System DLP not connect error!"));
			return false;
		}
	}

	QVector<cv::Mat> imageMats;
	int nPatternNum = System->getParam("motion_trigger_pattern_num").toInt();
	if (bMotionCardTrigger)
	{
		double dtime_start = double(clock());		
		if (!captureAllImages(imageMats)) return false;
		double dtime_movePos = double(clock());
		System->setTrackInfo(QStringLiteral("Board%1:采集图像数据: %2 ms").arg(0).arg(dtime_movePos - dtime_start), true);
		
		for (int i = 0; i < nStationNum; i++)
		{
			(*m_paramData)[i]._srcImageMats.clear();
			for (int j = 0; j < nPatternNum; j++)
			{
				(*m_paramData)[i]._srcImageMats.push_back(imageMats[i*nPatternNum + j]);
            }
		}
	}

	for (int i = 0; i < nStationNum; i++)
	{
		(*m_paramData)[i]._bCapturedDone = false;
		(*m_paramData)[i]._bStartCapturing = true;
	}

	//bool bCaptureLightImage = System->getParam("camera_cap_image_light").toBool();
	//if (bCaptureLightImage)
	//{
	//	saveImages(szImagePath, nIndex, imageMats.mid(nStationNum*nPatternNum));
	//}

	int nWaitTime = 30 * 100;
	while (nWaitTime-- > 0 && !isExit())
	{
		bool bCapturedDone = true;
		for (int i = 0; i < nStationNum; i++)
		{
			if (!(*m_paramData)[i]._bCapturedDone)
			{
				bCapturedDone = false;
				break;
			}
		}

		if (bCapturedDone) break;
		QThread::msleep(10);
	}
	if (nWaitTime <= 0)
	{
		System->setTrackInfo(QStringLiteral("等待DLP计算TimeOut. Try again!"));
		return false;
	}

	QEos::Notify(EVENT_CHECK_STATE, 0, STATION_STATE_GENERATE_GRAY, 0);
	
	cv::Mat matGray; int nCapturedNum = 0;
	for (int i = 0; i < nStationNum; i++)
	{
		if (matGray.empty())
		{
			matGray = (*m_paramData)[i]._matImage.clone();
			matGray.setTo(0);
		}
		matGray += (*m_paramData)[i]._matImage;
		nCapturedNum += 1;
	}
	matGray /= nCapturedNum;
	cv::cvtColor(matGray, matGray, CV_BayerGR2BGR);
	m_matImages[nIndex]._img[4] = matGray;

	//pUI->setImage(matGray, true);
	//QEos::Notify(EVENT_RESULT_DISPLAY, 0, STATION_RESULT_IMAGE_DISPLAY);

	QVector<cv::Mat> lightImages = imageMats.mid(nStationNum*nPatternNum);
	cv::Mat outputPseudocolor(matGray.size(), CV_8UC3);
	unsigned char grayValueB, grayValueG, grayValueR;
	for (int y = 0; y < matGray.rows; y++)
	{
		for (int x = 0; x < matGray.cols; x++)
		{
			grayValueB = lightImages[4].at<uchar>(y, x);
			grayValueG = lightImages[3].at<uchar>(y, x);
			grayValueR = lightImages[2].at<uchar>(y, x);

			cv::Vec3b& pixel = outputPseudocolor.at<cv::Vec3b>(y, x);
			pixel[0] = abs(grayValueB);
			pixel[1] = abs(grayValueG);
			pixel[2] = abs(grayValueR);
		}
	}
	cv::cvtColor(lightImages[0], lightImages[0], CV_BayerGR2BGR);
	m_matImages[nIndex]._img[0] = lightImages[0];
	cv::cvtColor(lightImages[1], lightImages[1], CV_BayerGR2BGR);
	m_matImages[nIndex]._img[1] = lightImages[1];
	m_matImages[nIndex]._img[2] = outputPseudocolor;
	cv::cvtColor(lightImages[5], lightImages[5], CV_BayerGR2BGR);
	m_matImages[nIndex]._img[3] = lightImages[5];

	bool bCaptureLightImage = System->getParam("camera_cap_image_light").toBool();
	if (bCaptureLightImage)
	{
		QVector<cv::Mat> savingImages;
		for (int i = 0; i < g_nImageStructDataNum; i++)
			savingImages.push_back(m_matImages[nIndex]._img[i]);
		saveImages(szImagePath, 0, nIndex, g_nImageStructDataNum*getPositionNum(), savingImages);
	}

	pUI->setImage(m_matImages[nIndex]._img[m_nImageIndex], true);

	QEos::Notify(EVENT_CHECK_STATE, 0, STATION_STATE_CALCULATE_3D, 0);

	QVector<cv::Mat> matHeights;
	for (int i = 0; i < nStationNum; i++)
	{
		matHeights.push_back((*m_paramData)[i]._3DMatHeight);
	}

	if (!pVision->merge3DHeight(matHeights, m_3DMatHeights[nIndex]))
	{		
		return false;
	}	
	
	return true;
}

bool QMainRunable::mergeImages(QString& szImagePath)
{
	IVisionUI* pUI = getModule<IVisionUI>(UI_MODEL);
	if (!pUI) return false;

	IVision* pVision = getModule<IVision>(VISION_MODEL);
	if (!pVision) return false;

	QVector<cv::Mat> matInputImages;
	for each (QImageStruct var in m_matImages)
	{
		for (int i = 0; i < g_nImageStructDataNum; i++)
		{
			matInputImages.push_back(var._img[i]);
		}
	}
	
	QVector<cv::Mat> matOutputImages;
	pVision->mergeImage(matInputImages, matOutputImages);
	if (matOutputImages.size() == g_nImageStructDataNum)
	{
		for (int i = 0; i < matOutputImages.size(); i++)
		{
			m_stCombinedImage._img[i] = matOutputImages.at(i);
		}
	}

	pUI->setImage(m_stCombinedImage._img[m_nImageIndex], true);
	bool bCaptureLightImage = System->getParam("camera_cap_image_light").toBool();
	if (bCaptureLightImage)
	{		
		saveCombineImages(szImagePath, matOutputImages);
	}

	for (int i = 0; i < getPositionNum(); i++)
	{

	}
	pUI->setHeightData(m_3DMatHeight);

	return true;
}

bool QMainRunable::matchPosition()
{
	//ICamera* pCam = getModule<ICamera>(CAMERA_MODEL);
	//if (!pCam) return false;

	//IVision* pVision = getModule<IVision>(VISION_MODEL);
	//if (!pVision) return false;

	//QEos::Notify(EVENT_CHECK_STATE, 0, STATION_STATE_MATCH_POSITION, 0);

	//clearTestObjs();

	//cv::Mat matDisplay = m_matImage.clone();

	//if (!m_matImage.empty())
	//{
	//	if (!pVision->matchPosition(matDisplay, m_objTests)) return false;
	//}

	////pCam->setImage(matDisplay);

	return true;
}

bool QMainRunable::calculateDetectHeight()
{
	//ICamera* pCam = getModule<ICamera>(CAMERA_MODEL);
	//if (!pCam) return false;

	//IVision* pVision = getModule<IVision>(VISION_MODEL);
	//if (!pVision) return false;

	//QEos::Notify(EVENT_CHECK_STATE, 0, STATION_STATE_CALCULATE_HEIGHT, 0);

	//if (pVision->calculateDetectHeight(m_3DMatHeight, m_objTests))
	//{
	//	return true;
	//}

	return true;
}

bool QMainRunable::waitCheckDone()
{
	QEos::Notify(EVENT_CHECK_STATE, 0, STATION_STATE_RESOULT, 1);

	IData * pData = getModule<IData>(DATA_MODEL);
	if (pData)
	{
		pData->decrementCycleTests();
	}

	return true;
}

bool QMainRunable::doAlignment()
{
    IData * pData = getModule<IData>(DATA_MODEL);
    Vision::VectorOfMat vecFrameImage;
    for ( const auto &stFrameImages : m_matImages ) {
        vecFrameImage.push_back ( stFrameImages._img[m_nImageIndex] );
    }
    return pData->doAlignment ( vecFrameImage );
}

bool QMainRunable::isExit()
{
	return m_exit;
}

void QMainRunable::addImageText(cv::Mat image, cv::Point ptPos, QString szText)
{
	double dScaleFactor = 2.0;

	cv::String text = szText.toStdString();

	cv::Point ptPos1, ptPos2;
	ptPos1.x = ptPos.x - 10 * dScaleFactor;
	ptPos1.y = ptPos.y + 10 * dScaleFactor;
	ptPos2.x = ptPos.x + 10 * dScaleFactor * text.length();
	ptPos2.y = ptPos.y - 20 * dScaleFactor;
	cv::rectangle(image, ptPos1, ptPos2, cv::Scalar(125, 125, 125), -1);
	cv::rectangle(image, ptPos1, ptPos2, cv::Scalar(200, 200, 200), 1);

	double fontScale = dScaleFactor*0.5f;
	cv::putText(image, text, ptPos, CV_FONT_HERSHEY_COMPLEX, fontScale, cv::Scalar(0, 255, 255), 1);
}

void QMainRunable::setResoultLight(bool isOk)
{
	IMotion * p = getModule<IMotion>(MOTION_MODEL);
	if (p)
	{
		int okLight = 0;
		int ngLight = 0;
		if (getLightIO(okLight, ngLight))
		{
			if (isOk)
			{
				//p->setExtDO(okLight, 1);
				//p->setExtDO(ngLight, 0);
			}
			else
			{
				//p->setExtDO(okLight, 0);
				//p->setExtDO(ngLight, 1);
			}
		}
	}
}

void QMainRunable::resetResoultLight()
{
	IMotion * p = getModule<IMotion>(MOTION_MODEL);
	if (p)
	{
		int okLight = 0;
		int ngLight = 0;
		if (getLightIO(okLight, ngLight))
		{
			//p->setExtDO(okLight, 0);
			//p->setExtDO(ngLight, 0);
		}
	}
}

bool QMainRunable::getLightIO(int &okLight, int &ngLight)
{
	if (m_paramMap->contains(PARAM_STATION_OK_LIGHT)
		&& m_paramMap->contains(PARAM_STATION_NG_LIGHT))
	{
		okLight = m_paramMap->value(PARAM_STATION_OK_LIGHT).toInt();
		ngLight = m_paramMap->value(PARAM_STATION_NG_LIGHT).toInt();

		return true;
	}

	return false;
}

QString QMainRunable::generateImagePath()
{
	QString capturePath = System->getParam("camera_cap_image_path").toString();

	QDateTime dtm = QDateTime::currentDateTime();
	QString fileDir = capturePath + "/image/" + dtm.toString("MMddhhmmss") + "/";
	QDir dir; dir.mkdir(fileDir);

	return fileDir;
}

void QMainRunable::saveImages(const QString& szImagePath, int nRowIndex, int nColIndex, int nCountOfImgPerRow, const QVector<cv::Mat>& imageMats)
{
	int nCountOfImgPerFrame = imageMats.size();
	for (int i = 0; i < nCountOfImgPerFrame; i++)
	{
		int nImageIndex = nRowIndex * nCountOfImgPerRow + nColIndex*nCountOfImgPerFrame + i + 1;

		QString strSave = szImagePath + QString("F") + QString::number(nRowIndex + 1, 'g', 2) + QString("-") + QString::number(nImageIndex, 'g', 2) + QString("-") +
			QString("1") + QString(".bmp");

		IplImage frameImg = IplImage(imageMats[i]);
		cvSaveImage(strSave.toStdString().c_str(), &frameImg);
	}	
}

void QMainRunable::saveCombineImages(const QString& szImagePath, const QVector<cv::Mat>& imageMats)
{
	for (int i = 0; i < imageMats.size(); i++)
	{
		QString strSave = szImagePath + QString("CombineResult_") + QString::number(i + 1, 'g', 2) + QString(".bmp");

		IplImage frameImg = IplImage(imageMats[i]);
		cvSaveImage(strSave.toStdString().c_str(), &frameImg);
	}
}

//*****************************************************************************
//
//
//******************************************************************************
QFlowCtrl::QFlowCtrl(QObject *parent)
	: QObject(parent),m_isHome(false),m_isStart(false),m_timerId(-1),m_normal("evAiValue.xml")
	,m_homeIng(false)
{	
	m_errorCode = -1;
	m_timerId = startTimer(50);

	m_pMainParamMap = NULL;
	m_pMainStation = NULL;

	this->initStationParam();

	QSystem * p = QSystem::instance();
	bool s = connect(p,SIGNAL(goHome()),this,SLOT(home()));
	s = connect(p, SIGNAL(start()), this, SLOT(startAutoRun()));
	s = connect(p, SIGNAL(stop()), this, SLOT(stopAutoRun()));

	m_dateTime = QDateTime::currentDateTime();

	QEos::Attach(EVENT_IMAGE_STATE, this, SLOT(onImageEvent(const QVariantList &)));

	//IMotion * pMotion = getModule<IMotion>(MOTION_MODEL);
	//if(pMotion)
	//{
	//	pMotion->setExtDO(DO_YELLOW_LIGHT,1);
	//	pMotion->setExtDO(DO_GREEN_LIGHT,0);
	//	pMotion->setExtDO(DO_RED_LIGHT,0);
	//	pMotion->setExtDO(DO_BUZZER,0);
	//}
}

QFlowCtrl::~QFlowCtrl()
{
	if(-1 != m_timerId)killTimer(m_timerId);
	QCheckerParamMapList::iterator it = m_stationParams.begin();
	for(; it != m_stationParams.end(); ++it)delete *it;

	m_stationParams.clear();
	m_stationDatas.clear();

	if (m_pMainParamMap)
	{
		delete m_pMainParamMap;
		m_pMainParamMap = NULL;
	}
}

bool QFlowCtrl::isRuning()
{
	return m_isStart;
}

void QFlowCtrl::onImageEvent(const QVariantList &data)
{
	if (data.size() < 3) return;

	int iBoard = data[0].toInt();
	int iEvent = data[1].toInt();
	if (iEvent != IMAGE_STATE_CHANGE) return;
	int nIndex = data[2].toInt();

	if (m_pMainStation) m_pMainStation->setImageIndex(nIndex);
}

void QFlowCtrl::home()
{	
	if(m_isStart)
	{
		QSystem::showMessage(QStringLiteral("提示"),QStringLiteral("设备正在运行中，请先停止在回零"));
		QApplication::processEvents();

		//this->stop();
		return ;
	}

	if (!m_isHome)
	{
		m_homeIng = true;
		QEos::Notify(EVENT_GOHOMEING_STATE, GOHOMEING_STATE_OK);

		//急停判断
		IMotion * p = getModule<IMotion>(MOTION_MODEL);
		if (p)
		{
			int iState = 1;
			//p->getExtDI(DI_IM_STOP, iState);
			if (0 == iState)
			{
				QSystem::showMessage(QStringLiteral("提示"), QStringLiteral("设备处于急停状态，请先排除急停。"));
				return;
			}
		}
		else
		{
			System->setTrackInfo(QStringLiteral("系统暂无控制系统，无法回零。"));			
		}

		QSystem::showMessage(QStringLiteral("提示"), QStringLiteral("设备正在回零中..."), 0);
		QApplication::processEvents();

		if (p->homeAll(true))
		{
			m_isHome = true;
			QEos::Notify(EVENT_GOHOME_STATE, GOHOME_STATE_OK);
			QSystem::closeMessage();			
		}
		else
		{
			m_isHome = false;
			QEos::Notify(EVENT_GOHOME_STATE, GOHOME_STATE_NG);
			System->setErrorCode(ERROR_HOME_MOTION_ALM);
			QSystem::closeMessage();
		}
	}

	//if (m_isHome)
	//{
		//this->start();
	//}	
}

void QFlowCtrl::startAutoRun()
{
	start();
}

void QFlowCtrl::stopAutoRun()
{
	if (m_isStart) stop();
}

void QFlowCtrl::timerEvent(QTimerEvent * event)
{
	static int count = 0;

	if(count > 50)
	{
		checkPowError();
		checkImStop();
		checkReset();
		checkStart();
		checkStop();
		readbarCode();
		checkMotionState();
		checkError();
		checkAuthError();
	}
	else count++;
}


void QFlowCtrl::checkImStop()
{
	static int s_iState = 1;

	IMotion * p = getModule<IMotion>(MOTION_MODEL);
	if(p)
	{
		int iState = 1;
		//p->getExtDI(DI_IM_STOP,iState);
		if(0 == iState && 1 == s_iState)
		{
			this->imStop();
			s_iState = iState;
		}
		else if(1 == iState)
		{
			s_iState = 1;
		}
	}
}

void QFlowCtrl::checkReset()
{
	static int s_iState = 0;

	IMotion * p = getModule<IMotion>(MOTION_MODEL);
	if(p)
	{
		int iState = 0;
		//p->getExtDI(DI_RESET,iState);
		if(1 == iState && 0 == s_iState)
		{
			this->reset();
			s_iState = iState;
		}
		else if(0 == iState)
		{
			s_iState = 0;
		}
	}
}

void QFlowCtrl::checkStart()
{
	static int s_iState = 0;
	static clock_t s_clock = 0;

	IMotion * p = getModule<IMotion>(MOTION_MODEL);
	if(p)
	{
		int iState = 0;
		if(System->isEnableOutline())
		{
			iState = System->data(OUT_LINE_RUN).toInt();
		}
		else
		{
			//p->getExtDI(DI_START,iState);
		}

		if(1 == iState && 0 == s_iState)
		{
			//启动按钮持续按下200ms以上在响应
			if(0 == s_clock)
			{
				s_clock = clock();
			}
			else
			{
				clock_t tm = clock();
				if((tm - s_clock) > 50)
				{
					this->start();
					s_iState = iState;
					s_clock = 0;
				}
			}
		}
		else if(0 == iState)
		{
			s_iState = 0;
			s_clock = 0;
		}
	}
}

void QFlowCtrl::checkStop()
{
	static int s_iState = 0;

	IMotion * p = getModule<IMotion>(MOTION_MODEL);
	if(p)
	{
		int iState = 0;
		if(System->isEnableOutline())
		{
			iState = System->data(OUT_LINE_STOP).toInt();
		}
		else
		{
			//p->getExtDI(DI_STOP,iState);
		}
		
		if(1 == iState && 0 == s_iState)
		{			
			this->stop();
			s_iState = iState;
		}
		else if(0 == iState)
		{
			s_iState = 0;
		}
	}
}

void QFlowCtrl::readbarCode()
{
	
}


void QFlowCtrl::imStop()
{
	m_isHome = false;
	QCheckerRunableList::iterator it = m_checkStantion.begin();
	for(; it != m_checkStantion.end(); ++it)
	{
		QCheckerRunable * p = *it;
		if(p)p->imgStop();
	}
	this->stop();
	System->userImStop();

	IMotion * p = getModule<IMotion>(MOTION_MODEL);
	if(p)
	{
		//p->setExtDO(DO_YELLOW_LIGHT,0);
		//p->setExtDO(DO_GREEN_LIGHT,0);
		//p->setExtDO(DO_RED_LIGHT,1);
		//p->setExtDO(DO_BUZZER,1);
	}

	QSystem::showMessage(QStringLiteral("告警"),QStringLiteral("设备处于急停状态。"));
}
	
void QFlowCtrl::reset()
{
	if(m_isStart)
	{
		QMessageBox::warning(NULL,QStringLiteral("警告"),QStringLiteral("复位前请停止设备。"));
		return;
	}
	m_errorCode =-1;
	m_isHome = false;
	m_isStart = false;
	System->setErrorCode(ERROR_NO_ERROR);
	IMotion * p = getModule<IMotion>(MOTION_MODEL);
	if(p)
	{
		//p->clearError();
		QThread::msleep(100);
		//p->setExtDO(DO_YELLOW_LIGHT,1);
		//p->setExtDO(DO_GREEN_LIGHT,0);
		//p->setExtDO(DO_RED_LIGHT,0);
		//p->setExtDO(DO_BUZZER,0);
	}	
	
	if(QSystem::isMessageShowed())
	{
		QSystem::closeMessage();
	}	
}
	
void QFlowCtrl::start()
{
	ICamera* pCam = getModule<ICamera>(CAMERA_MODEL);
	if (!pCam) return;

	IDlp* pDlp = getModule<IDlp>(DLP_MODEL);
	if (!pDlp) return;

	IVisionUI* pUI = getModule<IVisionUI>(UI_MODEL);
	if (!pUI) return;

	if(!m_isHome)
	{		
		QSystem::showMessage(QStringLiteral("提示"),QStringLiteral("请先将设备回零"));
		return;
	}

	if(m_isStart)
	{		
		QSystem::showMessage(QStringLiteral("提示"),QStringLiteral("设备正在运行，请先按停止按钮"));
		return;
	}

	QSystem::showMessage(QStringLiteral("提示"), QStringLiteral("设备正在启动中..."), 0);
	QApplication::processEvents();

	System->setParam("camera_capture_num_mode", 0);// all capturing images

	if (pCam->getCameraNum() > 0)
	{
		if (!pCam->startUpCapture() || !pUI->startUpCapture())
		{
			QSystem::closeMessage();
			QMessageBox::warning(NULL, QStringLiteral("警告"), QStringLiteral("相机初始化问题。"));
			return;
		}
	}
	else
	{
		QSystem::closeMessage();
		QMessageBox::warning(NULL, QStringLiteral("警告"), QStringLiteral("请检查相机是否连接。"));
		return;
	}	

	int nStationNum = System->getParam("motion_trigger_dlp_num_index").toInt() == 0 ? 2 : 4;

	m_checkStantion.clear();
	for (int i = 0; i < nStationNum; i++)
	{
		if (pDlp->isConnected(i))
		{
			if (!pDlp->startUpCapture(i)) continue;		

			m_stationDatas[i].clear();
			QCheckerRunable * p = new QCheckerRunable(m_stationParams[i], &(m_stationDatas[i]));
			m_checkStantion.append(p);
			QThreadPool::globalInstance()->start(p, QThread::TimeCriticalPriority);
		}	
		else
		{
			System->setTrackInfo(QString(QStringLiteral("工位%0启动失败, 请检查DLP硬件！")).arg(i + 1));
		}
	}
	
	m_pMainStation = new QMainRunable(m_pMainParamMap, &m_stationDatas);
	QThreadPool::globalInstance()->start(m_pMainStation);

	IMotion * p = getModule<IMotion>(MOTION_MODEL);
	if(p)
	{
		//p->setExtDO(DO_YELLOW_LIGHT,0);
		//p->setExtDO(DO_GREEN_LIGHT,1);
		//p->setExtDO(DO_RED_LIGHT,0);
		//p->setExtDO(DO_BUZZER,0);
	}

	m_isStart = true;

	QEos::Notify(EVENT_RUN_STATE,RUN_STATE_RUNING);

	System->setParam("camera_show_image_toScreen_enable", false);

	QSystem::closeMessage();
}
	
void QFlowCtrl::stop()
{	
	ICamera* pCam = getModule<ICamera>(CAMERA_MODEL);
	if (!pCam) return;

	IDlp* pDlp = getModule<IDlp>(DLP_MODEL);
	if (!pDlp) return;	

	IVisionUI* pUI = getModule<IVisionUI>(UI_MODEL);
	if (!pUI) return;
	
	//m_isHome = false;

	QSystem::showMessage(QStringLiteral("提示"), QStringLiteral("设备正在停止中..."), 0);
	QApplication::processEvents();

	int nStationNum = System->getParam("motion_trigger_dlp_num_index").toInt() == 0 ? 2 : 4;
	for (int i = 0; i <nStationNum; i++)
	{
		imStopStation(i);
	}

	QCheckerRunableList::iterator it = m_checkStantion.begin();
	for(; it != m_checkStantion.end(); ++it)
	{
		QCheckerRunable * p = *it;
		if (p) p->quit();
	}
	if (m_pMainStation) m_pMainStation->quit();

	//if (QThreadPool::globalInstance()->activeThreadCount())
	QThreadPool::globalInstance()->waitForDone();

	//it = m_checkStantion.begin();
	//for (; it != m_checkStantion.end(); ++it)
	//{
	//	delete *it;
	//}
	m_checkStantion.clear();
	//if (m_pMainParamMap)
	//{
	//	delete m_pMainParamMap;
	//	m_pMainParamMap = NULL;
	//}

	if (pCam->getCameraNum() > 0)
	{
		pCam->endUpCapture();
	}
	pUI->endUpCapture();
	
	for (int i = 0; i < nStationNum; i++)
	{
		if (pDlp->isConnected(i))
		{
			if (!pDlp->endUpCapture(i)) continue;
		}
	}

	IMotion * p = getModule<IMotion>(MOTION_MODEL);
	if(p)
	{
		//p->setExtDO(DO_YELLOW_LIGHT,1);
		//p->setExtDO(DO_GREEN_LIGHT,0);
		//p->setExtDO(DO_RED_LIGHT,0);
		//p->setExtDO(DO_BUZZER,0);

		//p->releaseInputLock();
		//p->releaseAllStationStart();
	}

	System->setParam("camera_show_image_toScreen_enable", true);

	m_isStart = false;
	QEos::Notify(EVENT_RUN_STATE,RUN_STATE_STOP);
	QSystem::closeMessage();
}

void QFlowCtrl::initStationParam()
{
	//第一个检测器
	QCheckerParamMap *paramMap = new QCheckerParamMap;
	paramMap->insert(PARAM_STATION_ID, 1);
	/*paramMap->insert(PARAM_STATION_START_IO, DI_START_STATION1);
	paramMap->insert(PARAM_STATION_IN_AXIS,AXIS_CHECK1_IN);
	paramMap->insert(PARAM_STATION_TOP_AXIS, AXIS_CHECK1_TOP);
	paramMap->insert(PARAM_STATION_CHECK_POS, 1);
	paramMap->insert(PARAM_STATION_BACK_POS, 0);
	paramMap->insert(PARAM_STATION_CHECK_TYPE, CHECK_TYPE_ATEQ);
	paramMap->insert(PARAM_STATION_OK_LIGHT, DO_CHECK_OK1);
	paramMap->insert(PARAM_STATION_NG_LIGHT, DO_CHECK_NG1);
	paramMap->insert(PARAM_STATION_AI, CHECK_AI_1);*/
	m_stationParams.append(paramMap);
	m_stationDatas.append(Q3DStructData());

	paramMap = new QCheckerParamMap;
	paramMap->insert(PARAM_STATION_ID, 2);	
	m_stationParams.append(paramMap);
	m_stationDatas.append(Q3DStructData());

	paramMap = new QCheckerParamMap;
	paramMap->insert(PARAM_STATION_ID, 3);
	m_stationParams.append(paramMap);
	m_stationDatas.append(Q3DStructData());

	paramMap = new QCheckerParamMap;
	paramMap->insert(PARAM_STATION_ID, 4);
	m_stationParams.append(paramMap);
	m_stationDatas.append(Q3DStructData());

	m_pMainParamMap = new QCheckerParamMap;
}

void QFlowCtrl::checkAuthError()
{
	if (m_dateTime.secsTo(QDateTime::currentDateTime()) >= 60*10)
	//if (m_dateTime.daysTo(QDateTime::currentDateTime()) >= 1)
	{
		if (!System->checkRuntimeAuthRight())
		{
			System->setErrorCode(ERROR_ATHU_NORIGHT_WARRING);
			return;
		}
		m_dateTime = QDateTime::currentDateTime();
	}
	
}

//检测电机驱动器状态，报警，急停，使能等。
void QFlowCtrl::checkMotionState()
{
	static bool alm_state = false;

	IMotion * p = getModule<IMotion>(MOTION_MODEL);
	if(p)
	{
		int nAxis = 0;
		int axis[12];
		IMotion::STATE state[12];
		memset(axis,0,sizeof(int));
		memset(state,0,sizeof(IMotion::STATE));
		//p->getState(nAxis,axis,state);

		for(int i=0; i<nAxis; i++)
		{
			if(state[i] == IMotion::STATE_ALM)
			{
				if(!alm_state)
				{
					alm_state = true;
					System->setErrorCode(ERROR_MOTOR_ALM);
					return;
				}
			}
		}

		alm_state = false;
	}
}

void QFlowCtrl::checkError()
{
	unsigned int code = System->getErrorCode();
	if( m_errorCode == code) return;
	if (code < 0x30000000)
	{
		if(!System->isImStop())
		{
			this->imStop();
			m_errorCode = code;
		}
	}
}

void QFlowCtrl::checkPowError()
{
	IMotion * p = getModule<IMotion>(MOTION_MODEL);
	if(p)
	{
		//if(p->IsPowerError())
		//{
		//	QCheckerRunableList::iterator it = m_checkStantion.begin();
		//	for(; it != m_checkStantion.end(); ++it)
		//	{
		//		QCheckerRunable * p = *it;
		//		if(p)p->quit();
		//	}

		//	QThreadPool::globalInstance()->waitForDone();
		//	m_checkStantion.clear();

		//	QMessageBox::warning(NULL,QStringLiteral("警告"),QStringLiteral("系统错误或驱动掉电，请排查供电情况。软件即将退出。"));
		//	killTimer(m_timerId);
		//	m_timerId = -1;
		//	//QApplication::quit();		
		//}
	}
}

void QFlowCtrl::initErrorCode()
{
	System->addErrorMap(ERROR_ATHU_NORIGHT_WARRING, MSG_ATH_NORIGHT_WARRING);
	//System->addErrorMap(ERROR_Z_POS_WARRING,MSG_Z_POS_WARRING);
	//System->addErrorMap(ERROR_Z_POS_WARRING,MSG_Z_POS_WARRING);
	System->addErrorMap(ERROR_MOTION_POS_WARRING,MSG_MOTION_POS_WARRING);
	System->addErrorMap(ERROR_STATION_SAFE_GRATING_ALRM,MSG_STTATION_SAFE_GRATING_ALRM);
}

void QFlowCtrl::imStopStation(int nStation)
{	
	IMotion * p = getModule<IMotion>(MOTION_MODEL);
	if(p)
	{
		/*int limtCount[6] = {2,2,2,2,2,2};
		int axisTop[6] = {-1,-1,-1,-1,-1,-1};
		int count = getAllTopAxis(axisTop);
		p->stopMove(axisTop[nStation]);*/	
	}	
}