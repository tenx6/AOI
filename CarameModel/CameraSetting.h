#pragma once

#include <QWidget>
#include "ui_CameraSetting.h"
#include "CameraCtrl.h"
#include "viewctrl.h"

#include "opencv/cv.h"

class IGraphicEditor;
class CameraSetting : public QWidget
{
	Q_OBJECT

public:
	CameraSetting(CameraCtrl* pCameraCtrl, QWidget *parent = Q_NULLPTR);
	~CameraSetting();

public:
	void endUpCapture();
	void load3DViewData(int nSizeX, int nSizeY, QVector<double>& xValues, QVector<double>& yValues, QVector<double>& zValues);
	void show3DView();

protected slots:
	void onStateChangeHWTrig(int iState);
	void onStateChangeCapture(int iState);
	void onStateChangeHWTrigCon(int iState);
	void onStateChangeCaptureAsMatlab(int iState);
	void onStateChangeCrossEnable(int iState);

	void onLogAllCase(int iState);
	void onClickLogFailCase(bool s);
	void onClickLogAllCase(bool s);
	void onInitPRSystem();
	void onUninitPRSystem();
	void onClearAllRecords();
	void onAutoClearRecord(int iState);

	void onCaptureModeIndexChanged(int iIndex);
	void onCaptureNumModeIndexChanged(int iIndex);
	void onSelCapturePath();
	void onSaveCapturePath();
	void onSelImgDLP1Path();
	void onSaveImgDLP1Path();
	void onSelImgDLP2Path();
	void onSaveImgDLP2Path();
	void onCalibration();
	void onView3D();

private:
	double convertToPixel(double umValue);
	double convertToUm(double pixel);


private:
	QIntValidator *inputIntRangePos;
	QIntValidator *inputIntRangeAll;
	QDoubleValidator *inputDoubleRangePos;
	QDoubleValidator *inputDoubleRangeAll;

private:
	Ui::CameraSetting ui;
	CameraCtrl* m_pCameraCtrl;
	ViewCtrl* m_mainView;
	IGraphicEditor *m_pGraphicsEditor;	
};
