#pragma once

#include <QObject>
#include <QRunnable>

class VisionDetectSetting;
class QCameraRunnable : public QRunnable
{
public:
	QCameraRunnable(VisionDetectSetting* pSetting);
	~QCameraRunnable();

	void quit();	
	bool isRunning();

	void startCapture();
	void stopCapture();	
	bool isCapturing();

protected:
	void run();

	bool waitStartBtn();
	bool captureImages();
	bool waitCheckDone();

	bool isExit();

private:
	bool m_exit;
	bool m_bRunning;
	bool m_bCapturEnable;
	bool m_bCapturing;
	//QMutex m_mutex;

	VisionDetectSetting* m_pSetting;
};