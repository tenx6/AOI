#pragma once

#include <QWidget>
#include "ui_Inspect3DProfileWidget.h"

#include <memory>

#include "opencv/cv.h"

using InspectProfDataVector = QVector < QVector<cv::Point2d> > ;

class QCustomPlot;
class DisplayScene;
class Inspect3DProfileWidget : public QWidget
{
    Q_OBJECT

public:
    Inspect3DProfileWidget(QWidget *parent = Q_NULLPTR);
    ~Inspect3DProfileWidget();

    bool set3DHeight(QVector<cv::Mat>& matHeights);
    void inspect();

    void setInspectPos(double dX, double dY);

private:
    void initUI();

private slots:
    void onInspect();

private:
    cv::Mat drawHeightGray(const cv::Mat &matHeight);

    void generateProfData(bool bRow, int nIndex, QVector<cv::Mat>& matHeights, InspectProfDataVector& profData);

    void setupPlotData(std::shared_ptr<QCustomPlot> customPlot, InspectProfDataVector& profData);

private:
    Ui::Inspect3DProfileWidget ui;
    QVector<cv::Mat> m_matHeights;

    std::shared_ptr<DisplayScene> m_dlpImgScene1;
    std::shared_ptr<DisplayScene> m_dlpImgScene2;
    std::shared_ptr<DisplayScene> m_dlpImgScene3;
    std::shared_ptr<DisplayScene> m_dlpImgScene4;

    InspectProfDataVector m_profRowDatas;
    InspectProfDataVector m_profColDatas;

    std::shared_ptr<QCustomPlot> m_pInspectRowPlot; 
    std::shared_ptr<QCustomPlot> m_pInspectColPlot;
};
