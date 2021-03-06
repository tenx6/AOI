﻿#include <QDir>
#include <QThread>
#include <QThreadPool>
#include <QDateTime>
#include <QMessageBox>
#include <time.h>

#include "opencv2/highgui.hpp"
#include "opencv2/video.hpp"

#include "AutoRunThread.h"
#include "../Common/eos.h"
#include "../Common/ModuleMgr.h"
#include "../Common/SystemData.h"
#include "../include/IMotion.h"
#include "../include/IData.h"
#include "../include/IDlp.h"
#include "../include/ICamera.h"
#include "../include/IVision.h"
#include "../include/IVisionUI.h"
#include "../include/IdDefine.h"
#include "hsgworkflowctrl_global.h"
#include "../include/constants.h"
#include "TimeLog.h"
#include "../DataModule/DataUtils.h"
#include "AlignmentRunnable.h"
#include "Calc3DHeightRunnable.h"
#include "Insp3DHeightRunnable.h"
#include "Insp2DRunnable.h"

AutoRunThread::AutoRunThread(const Engine::AlignmentVector         &vecAlignments,
                             const DeviceInspWindowVector          &vecDeviceWindows,
                             const Vision::VectorOfVectorOfPoint2f &vecVecFrameCtr,
                             const AutoRunParams                   &stAutoRunParams,
                             MapBoardInspResult                    *pMapBoardInspResult)
    :m_vecAlignments        (vecAlignments),
     m_vecDeviceInspWindow  (vecDeviceWindows),
     m_vecVecFrameCtr       (vecVecFrameCtr),
     m_stAutoRunParams      (stAutoRunParams),
     m_pMapBoardInspResult  (pMapBoardInspResult),
     m_exit                 (false)
{
    m_threadPoolCalc3D.setMaxThreadCount(4);  // (4 DLP height calculation thead)
    m_threadPoolInsp2D.setMaxThreadCount(2); 
    QEos::Attach(EVENT_THREAD_STATE, this, SLOT(onThreadState(const QVariantList &)));

    int ROWS = m_vecVecFrameCtr.size();
    int COLS = m_vecVecFrameCtr[0].size();
    int TOTAL = ROWS * COLS;
    m_vecVecFrameImages = Vision::VectorOfVectorOfMat(PROCESSED_IMAGE_SEQUENCE::TOTAL_COUNT, Vision::VectorOfMat(TOTAL, cv::Mat()));
    m_vecFrame3DHeight = Vision::VectorOfMat(TOTAL, cv::Mat());
    m_vecMatBigImage = Vision::VectorOfMat(PROCESSED_IMAGE_SEQUENCE::TOTAL_COUNT, cv::Mat());
}

AutoRunThread::~AutoRunThread() {
}

void AutoRunThread::onThreadState(const QVariantList &data)
{    
    if (data.size() <= 0) return;

    int iEvent = data[0].toInt();

    switch (iEvent)
    {
    case SHUTDOWN_MAIN_THREAD:
        quit();
        break;  
    default:
        break;
    }
}

void AutoRunThread::quit()
{
    resetResoultLight();
    m_exit = true;
}

bool AutoRunThread::preRunning()
{
    m_dResolutionX = System->getSysParam("CAM_RESOLUTION_X").toDouble();
    m_dResolutionY = System->getSysParam("CAM_RESOLUTION_Y").toDouble();
    m_nDLPCount = System->getParam("motion_trigger_dlp_num_index").toInt() == 0 ? 2 : 4;
    m_nTotalImageCount = m_nDLPCount * DLP_IMG_COUNT + CAPTURE_2D_IMAGE_SEQUENCE::TOTAL_COUNT;
    m_fFovWidthUm  = m_stAutoRunParams.nImgWidthPixel  * m_dResolutionX;
    m_fFovHeightUm = m_stAutoRunParams.nImgHeightPixel * m_dResolutionY;
    return true;
}

void AutoRunThread::run()
{
    if (! preRunning()) return;

    System->setTrackInfo(QString(QStringLiteral("主流程启动成功!")));
    m_boardName.clear();

    double dtime_start = 0, dtime_movePos = 0;
    BoardInspResultPtr ptrBoardInspResult = nullptr;

    while (! isExit())
    {
        //if (! waitStartBtn()) continue;
        if (isExit()) break;

        _feedBoard();
        
        QThreadPool::globalInstance()->waitForDone();

        if(!m_boardName.isEmpty())
            System->setTrackInfo(QStringLiteral("电路板: ") + m_boardName + QStringLiteral(" 检测完毕."));

        if (ptrBoardInspResult != nullptr) {
            _generateResultBigImage(m_vecMatBigImage[PROCESSED_IMAGE_SEQUENCE::SOLDER_LIGHT], ptrBoardInspResult);

            if (ptrBoardInspResult->isWithFatalError()) {
                m_strErrorMsg = ptrBoardInspResult->getErrorMsg();
                _sendErrorAndWaitForResponse();
            }

            if (isExit()) break;
        }

        _readBarcode();
        ptrBoardInspResult = std::make_shared<BoardInspResult>(m_boardName);
        m_pMapBoardInspResult->insert(m_boardName, ptrBoardInspResult);

        if (!_doAlignment()) {
            if (isExit()) break;
            else continue;
        }

        if (isExit()) break;
        TimeLogInstance->addTimeLog("Finished do alignment.");

        if (m_vecVecFrameCtr.empty())
            break;

        if(! _doInspection(ptrBoardInspResult)) {
            if (isExit()) break;
            else continue;
        }
        if (isExit()) break;
    }

    if (isExit())
    {
        TimeLogInstance->dumpTimeLog("./log/timelog.log");
        IData * pData = getModule<IData>(DATA_MODEL);
        if (pData)
        {
            pData->decrementCycleTests();
        }
    }

    QThreadPool::globalInstance()->waitForDone();
    postRunning();

    System->setTrackInfo(QString(QStringLiteral("主流程已停止")));
}

void AutoRunThread::postRunning()
{  
    QEos::Notify(EVENT_THREAD_STATE, THREAD_CLOSED);
}

bool AutoRunThread::waitStartBtn()
{
    IData * pData = getModule<IData>(DATA_MODEL);
    if (pData) {
        int iState = 0;

        QEos::Notify(EVENT_CHECK_STATE, 0, STATION_STATE_WAIT_START, 0);

        while (1) {
            if (pData->getCycleTests() > 0) {
                return true;
            }

            if (isExit())return false;
            QThread::msleep(10);
        }

        return true;
    }

    return false;
}

bool AutoRunThread::moveToCapturePos(float fPosX, float fPosY)
{
    IMotion* pMotion = getModule<IMotion>(MOTION_MODEL);
    if (!pMotion) return false;

    fPosX *= UM_TO_MM;
    fPosY *= UM_TO_MM;

    if (! pMotion->moveToGroup(std::vector<QString>({AXIS_MOTOR_X, AXIS_MOTOR_Y}), std::vector<double>({fPosX, fPosY}), std::vector<int>({0, 0}), true))
    {
        System->setTrackInfo(QString("move to position error"));
        return false;
    }
    return true;
}

bool AutoRunThread::captureAllImages(QVector<cv::Mat>& imageMats, int col)
{
    if (System->isRunOffline()) {
        QVector<cv::Mat> vecLocalImages;

        std::string strImagePath = System->getOfflinePath().toStdString();
        if (strImagePath.back() != '/')
            strImagePath.push_back('/');
        char strfileName[100];
        for (int i = 1; i <= 54; ++i) {
            _snprintf(strfileName, sizeof(strfileName), "%d/%02d.bmp", col, i);
            cv::Mat matImage = cv::imread(strImagePath + strfileName, cv::IMREAD_GRAYSCALE);
            if (matImage.empty())
                return false;
            vecLocalImages.push_back(matImage);
        }
        imageMats = vecLocalImages;
        return true;
    }

    ICamera* pCam = getModule<ICamera>(CAMERA_MODEL);
    if (!pCam) return false;

    bool bResult = pCam->captureAllImages(imageMats);
    if (! bResult)
        return bResult;

    if (imageMats.size() != m_nTotalImageCount)
        return false;

    return true;
}

bool AutoRunThread::captureLightImages(QVector<cv::Mat>& imageMats, int index)
{
    if (System->isRunOffline()) {
        QVector<cv::Mat> vecLocalImages;
        std::string strImagePath = System->getOfflinePath().toStdString();
        char strfileName[100];
        for (int i = 1; i <= 6; ++i) {
            _snprintf(strfileName, sizeof(strfileName), "alignment_%d/%02d.bmp", index, i);
            cv::Mat matImage = cv::imread(strImagePath + strfileName, cv::IMREAD_GRAYSCALE);
            if (matImage.empty())
                return false;
            vecLocalImages.push_back(matImage);
        }
        imageMats = vecLocalImages;
        return true;
    }

    ICamera* pCam = getModule<ICamera>(CAMERA_MODEL);
    if (!pCam) return false;

    bool bResult = pCam->captureLightImages(imageMats);
    if (!bResult)
        return bResult;

    if (imageMats.size() != CAPTURE_2D_IMAGE_SEQUENCE::TOTAL_COUNT)
        return false;

    return true;
}

bool AutoRunThread::isExit()
{
    return m_exit;
}

void AutoRunThread::setResoultLight(bool isOk)
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

void AutoRunThread::resetResoultLight()
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

bool AutoRunThread::getLightIO(int &okLight, int &ngLight)
{
    return false;
}

QString AutoRunThread::generateImagePath()
{
    QString capturePath = System->getParam("camera_cap_image_path").toString();

    QDateTime dtm = QDateTime::currentDateTime();
    QString fileDir = capturePath + "/image/" + dtm.toString("MMddhhmmss") + "/";
    QDir dir; dir.mkdir(fileDir);

    return fileDir;
}

void AutoRunThread::saveImages(const QString& szImagePath, int nRowIndex, int nColIndex, int nCountOfImgPerRow, const QVector<cv::Mat>& imageMats)
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

void AutoRunThread::saveCombineImages(const QString& szImagePath, const QVector<cv::Mat>& imageMats)
{
    for (int i = 0; i < imageMats.size(); i++)
    {
        QString strSave = szImagePath + QString("CombineResult_") + QString::number(i + 1, 'g', 2) + QString(".bmp");

        IplImage frameImg = IplImage(imageMats[i]);
        cvSaveImage(strSave.toStdString().c_str(), &frameImg);
    }
}

bool AutoRunThread::_feedBoard()
{
    // Track motor on to drag the PCB board into the machine and put under camera.
    // Need to implement later.
    return true;
}

bool AutoRunThread::_readBarcode()
{
    // Read the bard code.
    // Need to implement later.
    m_boardName = "Board_" + QDateTime::currentDateTime().toString();
    for (auto &chValue : m_boardName)
        if (':' == chValue )
            chValue = '_';
    return true;
}

bool AutoRunThread::_doAlignment() {
    if (! System->isRunOffline()) {
        auto pCam = getModule<ICamera>(CAMERA_MODEL);
        pCam->selectCaptureMode(ICamera::TRIGGER_LIGHT, true);
    }

    bool bResult = true;
    std::vector<AlignmentRunnablePtr> vecAlignmentRunnable;
    Vision::VectorOfPoint2f vecCadPoint, vecSrchPoint;

    int index = 0;
    for (const auto &alignment : m_vecAlignments) {
        vecCadPoint.push_back(cv::Point2f(alignment.tmplPosX, alignment.tmplPosY));

        if (!moveToCapturePos(alignment.tmplPosX, alignment.tmplPosY)) {
            bResult = false;
            break;
        }
        TimeLogInstance->addTimeLog("Move to alignment capture position.");
        
        QVector<cv::Mat> vecMatImages;
        if (!captureLightImages(vecMatImages, index)) {
            System->setTrackInfo(QString(QStringLiteral("Capture 2D light image fail.")));
            bResult = false;
            break;
        }
        TimeLogInstance->addTimeLog("Capture alignment light images.");

        cv::Mat matAlignmentImg = vecMatImages[0];
        cv::Point2f ptFrameCtr(alignment.tmplPosX, alignment.tmplPosY), ptAlignment(alignment.tmplPosX, alignment.tmplPosY);

        if (System->isRunOffline()) {
            ptFrameCtr.y = m_stAutoRunParams.nImgHeightPixel * m_dResolutionY / 2.f;
        }else {
            auto pMotion = getModule<IMotion>(MOTION_MODEL);
            double dPosX = 0., dPosY = 0.;
            pMotion->getCurrentPos(AXIS_MOTOR_X, &dPosX);
            pMotion->getCurrentPos(AXIS_MOTOR_Y, &dPosY);
            ptFrameCtr = cv::Point2f(dPosX * MM_TO_UM, dPosY * MM_TO_UM);

            // Only has frame in X direction. It should only happen when there is only X axis can move.
            if (fabs(ptFrameCtr.y) <= 0.01f)
                ptFrameCtr.y = m_stAutoRunParams.nImgHeightPixel * m_dResolutionY / 2.f;
        }

        cv::Rect rectSrchWindow = DataUtils::convertWindowToFrameRect(ptAlignment, alignment.srchWinWidth, alignment.srchWinHeight, ptFrameCtr, m_stAutoRunParams.nImgWidthPixel, m_stAutoRunParams.nImgHeightPixel, m_dResolutionX, m_dResolutionY);
        auto pAlignmentRunnable = std::make_unique<AlignmentRunnable>(matAlignmentImg, alignment, rectSrchWindow);
        pAlignmentRunnable->setAutoDelete(false);
        pAlignmentRunnable->setResolution(m_dResolutionX, m_dResolutionY);
        QThreadPool::globalInstance()->start(pAlignmentRunnable.get());
        vecAlignmentRunnable.push_back(std::move(pAlignmentRunnable));
        ++ index;
    }

    QThreadPool::globalInstance()->waitForDone();
    TimeLogInstance->addTimeLog("Search for alignment point done.");

    if (!bResult) {
        m_strErrorMsg = "Alignment fail";
        _sendErrorAndWaitForResponse();
        return false;
    }
    
    for (size_t i = 0; i < m_vecAlignments.size(); ++ i) {
        if (Vision::VisionStatus::OK != vecAlignmentRunnable[i]->getStatus()) {
            m_strErrorMsg = "Alignment fail";
            _sendErrorAndWaitForResponse();
            return false;
        }            

        if (System->isRunOffline())
            vecSrchPoint.push_back(cv::Point(m_vecAlignments[i].tmplPosX, m_vecAlignments[i].tmplPosY));
        else {
            auto ptOffset = vecAlignmentRunnable[i]->getResultCtrOffset();
            cv::Point2f ptResult(m_vecAlignments[i].tmplPosX + ptOffset.x, m_vecAlignments[i].tmplPosY + ptOffset.y);
            vecSrchPoint.push_back(ptResult);
        }
    }

    float fRotationInRadian, Tx, Ty, fScale;
    if (vecCadPoint.size() >= 3) {
        if (vecCadPoint.size() == 3)
            m_matTransform = cv::getAffineTransform(vecCadPoint, vecSrchPoint);
        else
            m_matTransform = cv::estimateRigidTransform(vecCadPoint, vecSrchPoint, true);

        m_matTransform.convertTo(m_matTransform, CV_32FC1);
    }else {
        DataUtils::alignWithTwoPoints(vecCadPoint[0],
                                      vecCadPoint[1],
                                      vecSrchPoint[0],
                                      vecSrchPoint[1],
                                      fRotationInRadian, Tx, Ty, fScale);

        cv::Point2f ptCtr(vecCadPoint[0].x / 2.f + vecCadPoint[1].x / 2.f,  vecCadPoint[0].y / 2.f + vecCadPoint[1].y / 2.f);
        double fDegree = fRotationInRadian * 180. / CV_PI;
        m_matTransform = cv::getRotationMatrix2D(ptCtr, fDegree, fScale);
        m_matTransform.at<double>(0, 2) += Tx;
        m_matTransform.at<double>(1, 2) += Ty;
        m_matTransform.convertTo(m_matTransform, CV_32FC1);
    }

    _alignWindows();
    return bResult;
}

void AutoRunThread::_transformPosition(float &x, float &y) {
    std::vector<float> vecSrcPos;
    vecSrcPos.push_back(x);
    vecSrcPos.push_back(y);
    vecSrcPos.push_back(1);
    cv::Mat matSrcPos(vecSrcPos);
    cv::Mat matDestPos = m_matTransform * matSrcPos;
    x = matDestPos.at<float>(0);
    y = matDestPos.at<float>(1);
}

bool AutoRunThread::_alignWindows()
{
    m_vecAlignedDeviceInspWindow = m_vecDeviceInspWindow;
    for (auto &deviceWindow : m_vecAlignedDeviceInspWindow) {
        _transformPosition(deviceWindow.device.x, deviceWindow.device.y);

        for (auto &window : deviceWindow.vecUngroupedWindows)
            _transformPosition(window.x, window.y);

        for (auto &windowGroup : deviceWindow.vecWindowGroup) {
            for ( auto &window : windowGroup.vecWindows)
                _transformPosition(window.x, window.y);
        }
    }
    return true;
}

bool AutoRunThread::_doInspection(BoardInspResultPtr ptrBoardInspResult) {
    auto pCam = getModule<ICamera>(CAMERA_MODEL);
    pCam->selectCaptureMode(ICamera::TRIGGER_ALL, true);

    bool bGood = true;
    int ROWS = m_vecVecFrameCtr.size();
    int COLS = m_vecVecFrameCtr[0].size();
    for (int row = 0; row < ROWS; ++ row) {
        for (int col = 0; col < COLS; ++ col) {
            auto ptFrameCtr = m_vecVecFrameCtr[row][col];
            if (! moveToCapturePos(ptFrameCtr.x, ptFrameCtr.y)) {
                bGood = false;
                break;
            }

            if (!System->isRunOffline()) {
                auto pMotion = getModule<IMotion>(MOTION_MODEL);
                double dPosX = 0., dPosY = 0.;
                pMotion->getCurrentPos(AXIS_MOTOR_X, &dPosX);
                pMotion->getCurrentPos(AXIS_MOTOR_Y, &dPosY);
                ptFrameCtr = cv::Point2f(dPosX * MM_TO_UM, dPosY * MM_TO_UM);
            }

            // Only has frame in X direction. It should only happen when there is only X axis can move.
            if (fabs(ptFrameCtr.y) <= 0.01f)
                ptFrameCtr.y = m_stAutoRunParams.nImgHeightPixel * m_dResolutionY / 2.f;

            TimeLogInstance->addTimeLog("Move to inspect capture position.");
        
            QVector<cv::Mat> vecMatImages;
            if (! captureAllImages(vecMatImages, col)) {
                m_strErrorMsg = "Capture image failed";
                _sendErrorAndWaitForResponse();
                bGood = false;
                break;
            }
            TimeLogInstance->addTimeLog("Capture all inspect images.");

            QThreadPool::globalInstance()->waitForDone();
            if (ptrBoardInspResult->isWithFatalError()) {
                m_strErrorMsg = ptrBoardInspResult->getErrorMsg();
                _sendErrorAndWaitForResponse();
                bGood = false;
            }

            std::vector<Calc3DHeightRunnablePtr> vecCalc3dHeightRunnable;
            for (int nDlpId = 0; nDlpId < m_nDLPCount; ++ nDlpId) {
                QVector<cv::Mat> vecDlpImage = vecMatImages.mid(nDlpId * DLP_IMG_COUNT, DLP_IMG_COUNT);
                auto pCalc3DHeightRunnable = std::make_shared<Calc3DHeightRunnable>(nDlpId + 1, vecDlpImage);
                pCalc3DHeightRunnable->setAutoDelete(false);
                m_threadPoolCalc3D.start(pCalc3DHeightRunnable.get());
                vecCalc3dHeightRunnable.push_back(std::move(pCalc3DHeightRunnable));
            }

            Vision::VectorOfMat vec2DCaptureImages(vecMatImages.begin() + m_nDLPCount * DLP_IMG_COUNT, vecMatImages.end());
            if (vec2DCaptureImages.size() != CAPTURE_2D_IMAGE_SEQUENCE::TOTAL_COUNT) {
                System->setTrackInfo(QString(QStringLiteral("2D image count %0 not correct.")).arg(vec2DCaptureImages.size()));
                bGood = false;
                break;
            }

            auto vecDeviceWindows = _getDeviceWindowInFrame(ptFrameCtr);

            auto pInsp2DRunnable = std::make_shared<Insp2DRunnable>(
                vec2DCaptureImages,
               &m_vecVecFrameImages,
               &m_threadPoolCalc3D,
                vecCalc3dHeightRunnable,
               &m_vecFrame3DHeight,
                vecDeviceWindows,
                row, col, ROWS, COLS,
                ptFrameCtr,
                ptrBoardInspResult);
            pInsp2DRunnable->setAutoDelete(false);
            pInsp2DRunnable->setResolution(m_dResolutionX, m_dResolutionY);
            pInsp2DRunnable->setImageSize(m_stAutoRunParams.nImgWidthPixel, m_stAutoRunParams.nImgHeightPixel);
            m_threadPoolInsp2D.start(pInsp2DRunnable.get());

            {
                auto pInsp3DHeightRunnalbe = new Insp3DHeightRunnable(&m_threadPoolInsp2D, pInsp2DRunnable, ptFrameCtr, row, col, ROWS, COLS, m_stAutoRunParams, ptrBoardInspResult);
                pInsp3DHeightRunnalbe->setResolution(m_dResolutionX, m_dResolutionY);
                pInsp3DHeightRunnalbe->setImageSize(m_stAutoRunParams.nImgWidthPixel, m_stAutoRunParams.nImgHeightPixel);
                QThreadPool::globalInstance()->start(pInsp3DHeightRunnalbe);
            }

            if (isExit())
                return bGood;
        }

        if (! bGood)
            break;
    }

    QThreadPool::globalInstance()->waitForDone();

    for (int i = 0; i < 4; ++ i) {
        if (! _combineBigImage(m_vecVecFrameImages[i], m_vecMatBigImage[i]))
            return false;
    }
    QEos::Notify(EVENT_THREAD_STATE, REFRESH_BIG_IMAGE);

    if (!_combineBigImage(m_vecFrame3DHeight, m_matWhole3DHeight))
        return false;

    auto vecNotInspectedDeviceWindow = _getNotInspectedDeviceWindow();
    if (! vecNotInspectedDeviceWindow.empty()) {
        cv::Point2f ptBigImageCenter((m_stAutoRunParams.fBoardLeftPos + m_stAutoRunParams.fBoardRightPos) / 2.f, (m_stAutoRunParams.fBoardTopPos, m_stAutoRunParams.fBoardBtmPos) / 2.f);
        // Only has frame in X direction. It should only happen when there is only X axis can move.
        if (fabs(ptBigImageCenter.y) <= 0.01f)
            ptBigImageCenter.y = m_vecMatBigImage[0].rows * m_dResolutionY / 2.f;

        auto pInsp2DRunnable = std::make_shared<Insp2DRunnable>(m_matWhole3DHeight, m_vecMatBigImage, vecNotInspectedDeviceWindow, ptBigImageCenter, ptrBoardInspResult);
        pInsp2DRunnable->setAutoDelete(false);
        pInsp2DRunnable->setResolution(m_dResolutionX, m_dResolutionY);
        pInsp2DRunnable->setImageSize(m_vecMatBigImage[0].cols, m_vecMatBigImage[0].rows);
        m_threadPoolInsp2D.start(pInsp2DRunnable.get());

        {
            auto pInsp3DHeightRunnalbe = new Insp3DHeightRunnable(&m_threadPoolInsp2D, pInsp2DRunnable, ptBigImageCenter, 0, 0, ROWS, COLS, m_stAutoRunParams, ptrBoardInspResult);
            pInsp3DHeightRunnalbe->set3DHeight(m_matWhole3DHeight);
            pInsp3DHeightRunnalbe->setResolution(m_dResolutionX, m_dResolutionY);
            pInsp3DHeightRunnalbe->setImageSize(m_vecMatBigImage[0].cols, m_vecMatBigImage[0].rows);
            QThreadPool::globalInstance()->start(pInsp3DHeightRunnalbe);
        }
    }
    
    return bGood;
}

DeviceInspWindowVector AutoRunThread::_getDeviceWindowInFrame(const cv::Point2f &ptFrameCtr)
{
    DeviceInspWindowVector vecResult;
    for (auto &deviceInspWindow : m_vecAlignedDeviceInspWindow) {
        bool bInFrame = true;
        for (const auto &window : deviceInspWindow.vecUngroupedWindows) {
            if (! DataUtils::isWindowInFrame(cv::Point2f(window.x, window.y), window.width, window.height, ptFrameCtr, m_fFovWidthUm, m_fFovHeightUm)) {
                bInFrame = false;
                break;
            }
        }

        if (!bInFrame)
            continue;

        for (const auto &windowGroup : deviceInspWindow.vecWindowGroup) {
            for (const auto &window : windowGroup.vecWindows) {
                if (! DataUtils::isWindowInFrame(cv::Point2f(window.x, window.y), window.width, window.height, ptFrameCtr, m_fFovWidthUm, m_fFovHeightUm)) {
                    bInFrame = false;
                    break;
                }
            }
        }

        if (bInFrame) {
            deviceInspWindow.bInspected = true;
            vecResult.push_back(deviceInspWindow);
        }
    }
    return vecResult;
}

DeviceInspWindowVector AutoRunThread::_getNotInspectedDeviceWindow() const
{
    DeviceInspWindowVector vecResult;
    for (const auto &deviceInspWindow : m_vecAlignedDeviceInspWindow) {
        if (! deviceInspWindow.bInspected)
            vecResult.push_back(deviceInspWindow);
    }
    return vecResult;
}

bool AutoRunThread::_combineBigImage(const Vision::VectorOfMat &vecMatImages, cv::Mat &matBigImage) {
    Vision::PR_COMBINE_IMG_CMD stCmd;
    Vision::PR_COMBINE_IMG_RPY stRpy;
    stCmd.nCountOfImgPerFrame = 1;
    stCmd.nCountOfFrameX = m_vecVecFrameCtr[0].size();
    stCmd.nCountOfFrameY = m_vecVecFrameCtr.size();
    stCmd.nOverlapX = m_stAutoRunParams.fOverlapUmX / m_dResolutionX;
    stCmd.nOverlapY = m_stAutoRunParams.fOverlapUmY / m_dResolutionY;
    stCmd.nCountOfImgPerRow = m_vecVecFrameCtr[0].size();
    stCmd.enScanDir = m_stAutoRunParams.enScanDir;

    stCmd.vecInputImages = vecMatImages;
    if (Vision::VisionStatus::OK == Vision::PR_CombineImg(&stCmd, &stRpy)) {
        matBigImage = stRpy.vecResultImages[0];
        return true;
    }else {
        System->setTrackInfo(QString(QStringLiteral("合并大图失败.")));
        matBigImage = cv::Mat();
        return false;
    }
}

void AutoRunThread::_generateResultBigImage(cv::Mat matBigImage, BoardInspResultPtr ptrBoardInspResult) {
    cv::Point2f ptBigImageCenter((m_stAutoRunParams.fBoardLeftPos + m_stAutoRunParams.fBoardRightPos) / 2.f, (m_stAutoRunParams.fBoardTopPos, m_stAutoRunParams.fBoardBtmPos) / 2.f);
    // Only has frame in X direction. It should only happen when there is only X axis can move.
    if (fabs(ptBigImageCenter.y) <= 0.01f)
        ptBigImageCenter.y = matBigImage.rows * m_dResolutionY / 2.f;

    cv::Scalar scalarGreen(0, 255, 0), scalarRed(0, 0, 255), scalarYellow(0, 255, 255);

    auto drawText = [](cv::Mat &matImage, const cv::Rect &rectROI, const std::string &strText) {
        int baseline = 0;
        int fontFace = cv::FONT_HERSHEY_SIMPLEX;
        double fontScale = 1;
        int thickness = 2;
        cv::Size textSize = cv::getTextSize(strText, fontFace, fontScale, thickness, &baseline);
        //The height use '+' because text origin start from left-bottom.
        cv::Point ptTextOrg(rectROI.x + (rectROI.width - textSize.width) / 2, rectROI.y + (rectROI.height + textSize.height) / 2);
        cv::putText(matImage, strText, ptTextOrg, fontFace, fontScale, cv::Scalar(255, 0, 0), thickness);
    };

    auto vecDeviceInspWindow = ptrBoardInspResult->getDeviceInspWindow();
    for (const auto &deviceInspWindow : vecDeviceInspWindow) {
        for (const auto &window : deviceInspWindow.vecUngroupedWindows) {
            auto rectROI = DataUtils::convertWindowToFrameRect(cv::Point2f(window.x, window.y),
                window.width,
                window.height,
                ptBigImageCenter,
                matBigImage.cols,
                matBigImage.rows,
                m_dResolutionX,
                m_dResolutionY);
            cv::Scalar scalar;
            auto status = ptrBoardInspResult->getWindowStatus(window.Id);
            if (status < 0)
                scalar = scalarYellow;
            else if(0 == status)
                scalar = scalarGreen;
            else
                scalar = scalarRed;
            cv::rectangle(matBigImage, rectROI, scalar, 2);
            //drawText(matBigImage, rectROI, window.name);
        }

        for (const auto &windowGroup : deviceInspWindow.vecWindowGroup) {
            for (const auto &window : windowGroup.vecWindows) {
                auto rectROI = DataUtils::convertWindowToFrameRect(cv::Point2f(window.x, window.y),
                    window.width,
                    window.height,
                    ptBigImageCenter,
                    matBigImage.cols,
                    matBigImage.rows,
                    m_dResolutionX,
                    m_dResolutionY);
                cv::Scalar scalar;
                auto status = ptrBoardInspResult->getWindowStatus(window.Id);
                if (status < 0)
                    scalar = scalarYellow;
                else if (0 == status)
                    scalar = scalarGreen;
                else
                    scalar = scalarRed;
                cv::rectangle(matBigImage, rectROI, scalar, 2);
                //drawText(matBigImage, rectROI, window.name);
            }
        }
    }

    std::string strCapture = "./capture/";
    strCapture += ptrBoardInspResult->getBoardName().toStdString() + "_result.png";
    cv::imwrite(strCapture, matBigImage);
}

void AutoRunThread::_sendErrorAndWaitForResponse() {
    QEos::Notify(EVENT_THREAD_STATE, AUTO_RUN_WITH_ERROR);
    m_conditionVariable.wait(std::unique_lock<std::mutex>(m_mutex));
}

void AutoRunThread::nofityResponse(bool bExit) {
    //m_exit = bExit;
    if (bExit) System->userStop();
    m_conditionVariable.notify_one();
}