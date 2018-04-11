#include "LightCalibrationView.h"

#include "../Common/SystemData.h"
#include "../Common/ModuleMgr.h"
#include "../include/IdDefine.h"
#include "../include/ICamera.h"
#include "../include/IMotion.h"
#include "../include/IVisionUI.h"
#include "../include/ILight.h"

#include <QMessageBox>
#include <qthread.h>

#include "../lib/VisionLibrary/include/VisionAPI.h"
#define ToInt(value)                (static_cast<int>(value))
#define ToFloat(param)      (static_cast<float>(param))

#include "opencv2/opencv.hpp"
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>

using namespace AOI;

LightCalibrationView::LightCalibrationView(VisionCtrl* pCtrl, QWidget *parent)
	: m_pCtrl(pCtrl), QWidget(parent)
{
	ui.setupUi(this);

	m_bGuideCali = false;
	ui.pushButton_stopCali->setEnabled(false);

	initUI();

	ui.comboBox_selectLight->setCurrentIndex(0);
}

LightCalibrationView::~LightCalibrationView()
{
}

void LightCalibrationView::initUI()
{
	double dLightCaliReadyPositionX = System->getParam("light_cali_ready_pos_x").toDouble();
	double dLightCaliReadyPositionY = System->getParam("light_cali_ready_pos_y").toDouble();
	double dLightCaliReadyPositionZ = System->getParam("light_cali_ready_pos_z").toDouble();
	ui.lineEdit_readyPosX->setText(QString("%1").arg(dLightCaliReadyPositionX));
	ui.lineEdit_readyPosY->setText(QString("%1").arg(dLightCaliReadyPositionY));
	ui.lineEdit_readyPosZ->setText(QString("%1").arg(dLightCaliReadyPositionZ));

	connect(ui.pushButton_Joystick, SIGNAL(clicked()), SLOT(onReadyPosJoystick()));
	connect(ui.pushButton_MoveToReady, SIGNAL(clicked()), SLOT(onMoveToReady()));

	QStringList ls;
	ls << QStringLiteral("��һȦ��") << QStringLiteral("��һȦ��") << QStringLiteral("�ڶ�Ȧ��") << QStringLiteral("����Ȧ��") << QStringLiteral("����Ȧ��") << QStringLiteral("����Ȧ��");
	ui.comboBox_selectLight->addItems(ls);	

	connect(ui.pushButton_startCali, SIGNAL(clicked()), SLOT(onStart()));
	connect(ui.pushButton_stopCali, SIGNAL(clicked()), SLOT(onEnd()));
	connect(ui.pushButton_captureLight, SIGNAL(clicked()), SLOT(onCaptureLight()));

	connect(ui.comboBox_selectLight, SIGNAL(currentIndexChanged(int)), SLOT(onSelectLightIndexChanged(int)));
	connect(ui.horizontalSlider, SIGNAL(valueChanged(int)), SLOT(onSliderChanged(int)));

	int nDetectGrayScaleRow = System->getParam("detect_gray_scale_row").toInt();
	int nDetectGrayScaleCol = System->getParam("detect_gray_scale_col").toInt();
	ui.lineEdit_DetectGraySacleRow->setText(QString("%1").arg(nDetectGrayScaleRow));
	ui.lineEdit_DetectGrayScaleCol->setText(QString("%1").arg(nDetectGrayScaleCol));
	connect(ui.pushButton_DetectGrayScale, SIGNAL(clicked()), SLOT(onDetectGrayScale()));

	connect(ui.pushButton_SaveCali, SIGNAL(clicked()), SLOT(onSaveCali()));
}

void LightCalibrationView::onReadyPosJoystick()
{
	IMotion* pMotion = getModule<IMotion>(MOTION_MODEL);
	if (pMotion)
	{
		pMotion->setJoystickXMotor(AXIS_MOTOR_X, 1.2, ui.lineEdit_readyPosX);
		pMotion->setJoystickYMotor(AXIS_MOTOR_Y, 1.2, ui.lineEdit_readyPosY);

		pMotion->startJoystick();
	}
}

void LightCalibrationView::onMoveToReady()
{
	double dLightCaliReadyPositionX = ui.lineEdit_readyPosX->text().toDouble();
	double dLightCaliReadyPositionY = ui.lineEdit_readyPosY->text().toDouble();
	double dLightCaliReadyPositionZ = ui.lineEdit_readyPosZ->text().toDouble();

	IMotion* pMotion = getModule<IMotion>(MOTION_MODEL);
	if (pMotion)
	{
		std::vector<int> axis;
		std::vector<double> pos;
		std::vector<int> profs;

		axis.push_back(AXIS_MOTOR_X); axis.push_back(AXIS_MOTOR_Y); axis.push_back(AXIS_MOTOR_Z);
		pos.push_back(dLightCaliReadyPositionX); pos.push_back(dLightCaliReadyPositionY); pos.push_back(dLightCaliReadyPositionZ);
		profs.push_back(1); axis.push_back(1); axis.push_back(1);

		pMotion->moveToGroup(axis, pos, profs, true);
	}
}

void LightCalibrationView::onSaveCali()
{
	double dLightCaliReadyPositionX = ui.lineEdit_readyPosX->text().toDouble();
	double dLightCaliReadyPositionY = ui.lineEdit_readyPosY->text().toDouble();
	double dLightCaliReadyPositionZ = ui.lineEdit_readyPosZ->text().toDouble();

	System->setParam("light_cali_ready_pos_x", dLightCaliReadyPositionX);
	System->setParam("light_cali_ready_pos_y", dLightCaliReadyPositionY);
	System->setParam("light_cali_ready_pos_z", dLightCaliReadyPositionZ);	

	int nIndex = ui.comboBox_selectLight->currentIndex();
	ILight* pLight = getModule<ILight>(LIGHT_MODEL);
	if (pLight)
	{
		int device = 0;
		int chn = 0;
		if (nIndex >= 0 && nIndex < 4)
		{
			device = 0;
			chn = nIndex;
		}
		else if (nIndex >= 4 && nIndex < 8)
		{
			device = 1;
			chn = nIndex - 4;
		}

		pLight->saveLuminance(device, chn);		
	}
}

void LightCalibrationView::onStart()
{
	ICamera* pCam = getModule<ICamera>(CAMERA_MODEL);
	if (!pCam) return;

	IVisionUI* pUI = getModule<IVisionUI>(UI_MODEL);
	if (!pUI) return;

	if (m_bGuideCali)
	{
		QMessageBox::warning(NULL, QStringLiteral("����"), QStringLiteral("Ŀǰ�Ѿ������У�"));
		return;
	}

	if (pCam->getCameraNum() > 0)
	{
		if (!pCam->startUpCapture() || !pUI->startUpCapture())
		{
			QSystem::closeMessage();
			QMessageBox::warning(NULL, QStringLiteral("����"), QStringLiteral("�����ʼ�����⡣"));
			return;
		}
	}
	else
	{
		QSystem::closeMessage();
		QMessageBox::warning(NULL, QStringLiteral("����"), QStringLiteral("��������Ƿ����ӡ�"));
		return;
	}

	m_bGuideCali = true;
	ui.pushButton_startCali->setEnabled(false);
	ui.pushButton_stopCali->setEnabled(true);
}

void LightCalibrationView::onEnd()
{
	ICamera* pCam = getModule<ICamera>(CAMERA_MODEL);
	if (!pCam) return;

	IVisionUI* pUI = getModule<IVisionUI>(UI_MODEL);
	if (!pUI) return;

	if (!m_bGuideCali) return;	

	if (pCam->getCameraNum() > 0)
	{
		pCam->endUpCapture();
	}
	pUI->endUpCapture();

	QSystem::closeMessage();

	m_bGuideCali = false;
	ui.pushButton_startCali->setEnabled(true);
	ui.pushButton_stopCali->setEnabled(false);
}

void LightCalibrationView::onCaptureLight()
{
	IVisionUI* pUI = getModule<IVisionUI>(UI_MODEL);
	if (!pUI) return;

	cv::Mat matImg;
	if (guideReadImage(matImg, ui.comboBox_selectLight->currentIndex()))
	{
		pUI->setImage(matImg, true);
		m_matImage = matImg;
	}
}

bool LightCalibrationView::guideReadImage(cv::Mat& matImg, int nSelectLight)
{
	ICamera* pCam = getModule<ICamera>(CAMERA_MODEL);
	if (!pCam) return false;

	IMotion* pMotion = getModule<IMotion>(MOTION_MODEL);
	if (!pMotion) return false;

	if (!pCam->selectCaptureMode(ICamera::TRIGGER_ONE))// 1 image
	{
		System->setTrackInfo(QString("startCapturing error"));
		return false;
	}

	if (!pCam->startCapturing())
	{
		System->setTrackInfo(QString("startCapturing error"));
		return false;
	}

	QVector<int> nPorts;
	switch (nSelectLight)
	{	
	case 0:
		nPorts.push_back(DO_LIGHT1_CH1);
		break;
	case 1:
		nPorts.push_back(DO_LIGHT1_CH2);
		break;
	case 2:
		nPorts.push_back(DO_LIGHT1_CH3);
		break;
	case 3:
		nPorts.push_back(DO_LIGHT1_CH4);
		break;
	case 4:
		nPorts.push_back(DO_LIGHT2_CH1);
		break;
	case 5:
		nPorts.push_back(DO_LIGHT2_CH2);
		break;
	default:
		break;
	}
	nPorts.push_back(DO_CAMERA_TRIGGER2);

	pMotion->setDOs(nPorts, 1);
	QThread::msleep(10);
	pMotion->setDOs(nPorts, 0);

	int nWaitTime = 10 * 100;
	while (!pCam->isCaptureImageBufferDone() && nWaitTime-- > 0)
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
		matImg = matImage;
	}

	System->setTrackInfo(QString("System captureImages Image Num: %1").arg(nCaptureNum));

	return true;
}

int LightCalibrationView::getLightLum(int nIndex)
{
	ILight* pLight = getModule<ILight>(LIGHT_MODEL);
	if (pLight)
	{
		int device = 0;
		int chn = 0;
		if (nIndex >= 0 && nIndex < 4)
		{
			device = 0;
			chn = nIndex;
		}
		else if (nIndex >= 4 && nIndex < 8)
		{
			device = 1;
			chn = nIndex - 4;
		}

		int nLum = pLight->getChLuminace(device, chn);
		return nLum;
	}

	return 0;
}

void LightCalibrationView::setLightLum(int nIndex, int lum)
{
	ILight* pLight = getModule<ILight>(LIGHT_MODEL);
	if (pLight)
	{
		int device = 0;
		int chn = 0;
		if (nIndex >= 0 && nIndex < 4)
		{
			device = 0;
			chn = nIndex;
		}
		else if (nIndex >= 4 && nIndex < 8)
		{
			device = 1;
			chn = nIndex - 4;
		}

		pLight->setLuminance(device, chn, lum);		
	}
}

void LightCalibrationView::onSelectLightIndexChanged(int iIndex)
{
	int nLightIndex = ui.comboBox_selectLight->currentIndex();

	ui.horizontalSlider->setValue(getLightLum(nLightIndex));
}

void LightCalibrationView::onSliderChanged(int lum)
{
	QString str = QString::number(lum);
	ui.lineEdit_6->setText(str);

	int nLightIndex = ui.comboBox_selectLight->currentIndex();
	if (lum != getLightLum(nLightIndex))
	{
		setLightLum(nLightIndex, lum);
	}
}

void LightCalibrationView::onDetectGrayScale()
{
	IVisionUI* pUI = getModule<IVisionUI>(UI_MODEL);
	if (!pUI) return;

	if (m_matImage.empty())
	{
		QMessageBox::warning(this, QStringLiteral("��ʾ"), QStringLiteral("����ѡ��ͼƬ"));
		return;
	}

	int nDetectGrayScaleRow = ui.lineEdit_DetectGraySacleRow->text().toInt();
	int nDetectGrayScaleCol = ui.lineEdit_DetectGrayScaleCol->text().toInt();

	Vision::PR_GRID_AVG_GRAY_SCALE_CMD  stCmd;
	stCmd.nGridRow = nDetectGrayScaleRow;
	stCmd.nGridCol = nDetectGrayScaleCol;
	
	stCmd.vecInputImgs.push_back(m_matImage);

	Vision::PR_GRID_AVG_GRAY_SCALE_RPY stRpy;
	Vision::VisionStatus retStatus = Vision::PR_GridAvgGrayScale(&stCmd, &stRpy);
	if (retStatus == Vision::VisionStatus::OK)
	{
		pUI->displayImage(stRpy.matResultImg);
	}
	else
	{
		pUI->addImageText(QString("Error at Cal Gray Sacle, error code = %1").arg((int)retStatus));
	}
}