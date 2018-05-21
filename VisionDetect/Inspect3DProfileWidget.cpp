﻿#include "Inspect3DProfileWidget.h"

#include "qcustomplot.h"

#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>

#include "opencv2/opencv.hpp"
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>

#define ToInt(value)                (static_cast<int>(value))
#define ToFloat(param)      (static_cast<float>(param))

const int IMG_DISPLAY_WIDTH = 350;
const int IMG_DISPLAY_HEIGHT = 250;

class Inspect3DProfileWidget;
class DisplayScene : public QGraphicsScene
{
public:
    explicit DisplayScene(QObject *parent = 0);
    void setInspectWidget(Inspect3DProfileWidget* pInspectWidget) { m_pInspectWidget = pInspectWidget; }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event);
private:
    Inspect3DProfileWidget* m_pInspectWidget;
};

DisplayScene::DisplayScene(QObject *parent)
    :QGraphicsScene(parent)
{
    clearFocus();
}

void DisplayScene::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    //qDebug() << event->scenePos().x() << " " << event->scenePos().y();
    if (m_pInspectWidget)
    {
        m_pInspectWidget->setInspectPos(event->scenePos().x(), event->scenePos().y());
    }
    QGraphicsScene::mousePressEvent(event);
}

Inspect3DProfileWidget::Inspect3DProfileWidget(QWidget *parent)
    : QWidget(parent)
{
    ui.setupUi(this);

    initUI();
}

Inspect3DProfileWidget::~Inspect3DProfileWidget()
{
}

void Inspect3DProfileWidget::initUI()
{
    // Image Display
    m_dlpImgScene1 = std::make_shared<DisplayScene>(this); 
    m_dlpImgScene1->setInspectWidget(this);
    ui.graphicsView_dlp1->setScene(m_dlpImgScene1.get());
    ui.graphicsView_dlp1->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui.graphicsView_dlp1->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui.graphicsView_dlp1->fitInView(QRectF(0, 0, IMG_DISPLAY_WIDTH, IMG_DISPLAY_HEIGHT), Qt::KeepAspectRatio);    //这样就没法缩放了 
    ui.graphicsView_dlp1->setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    ui.graphicsView_dlp1->setRenderHint(QPainter::Antialiasing);

    m_dlpImgScene2 = std::make_shared<DisplayScene>(this);
    m_dlpImgScene2->setInspectWidget(this);
    ui.graphicsView_dlp2->setScene(m_dlpImgScene2.get());
    ui.graphicsView_dlp2->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui.graphicsView_dlp2->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui.graphicsView_dlp2->fitInView(QRectF(0, 0, IMG_DISPLAY_WIDTH, IMG_DISPLAY_HEIGHT), Qt::KeepAspectRatio);    //这样就没法缩放了 
    ui.graphicsView_dlp2->setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    ui.graphicsView_dlp2->setRenderHint(QPainter::Antialiasing);

    m_dlpImgScene3 = std::make_shared<DisplayScene>(this);
    m_dlpImgScene3->setInspectWidget(this);
    ui.graphicsView_dlp3->setScene(m_dlpImgScene3.get());
    ui.graphicsView_dlp3->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui.graphicsView_dlp3->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui.graphicsView_dlp3->fitInView(QRectF(0, 0, IMG_DISPLAY_WIDTH, IMG_DISPLAY_HEIGHT), Qt::KeepAspectRatio);    //这样就没法缩放了 
    ui.graphicsView_dlp3->setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    ui.graphicsView_dlp3->setRenderHint(QPainter::Antialiasing);

    m_dlpImgScene4 = std::make_shared<DisplayScene>(this);
    m_dlpImgScene4->setInspectWidget(this);
    ui.graphicsView_dlp4->setScene(m_dlpImgScene4.get());
    ui.graphicsView_dlp4->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui.graphicsView_dlp4->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui.graphicsView_dlp4->fitInView(QRectF(0, 0, IMG_DISPLAY_WIDTH, IMG_DISPLAY_HEIGHT), Qt::KeepAspectRatio);    //这样就没法缩放了 
    ui.graphicsView_dlp4->setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    ui.graphicsView_dlp4->setRenderHint(QPainter::Antialiasing);

    ui.lineEdit_row->setText("50");
    ui.lineEdit_col->setText("50");
    connect(ui.pushButton_inspect, SIGNAL(clicked()), SLOT(onInspect()));

    m_pInspectRowPlot = std::make_shared<QCustomPlot>(this);
    setupPlotData(m_pInspectRowPlot, m_profRowDatas);
    ui.dockWidget_profChartRow->setWidget(m_pInspectRowPlot.get());

    m_pInspectColPlot = std::make_shared<QCustomPlot>(this);
    setupPlotData(m_pInspectColPlot, m_profColDatas);
    ui.dockWidget_profChartCol->setWidget(m_pInspectColPlot.get());
}

void Inspect3DProfileWidget::onInspect()
{
    inspect();
}

bool Inspect3DProfileWidget::set3DHeight(QVector<cv::Mat>& matHeights)
{
    m_matHeights = matHeights;
    return true;
}

void Inspect3DProfileWidget::inspect()
{
    int nImageNum = qMin(4, m_matHeights.size());
    std::shared_ptr<DisplayScene> scene[4] = { m_dlpImgScene1, m_dlpImgScene2, m_dlpImgScene3, m_dlpImgScene4 };
    for (int i = 0; i < nImageNum; i++)
    {
        cv::Mat matSourceImg = drawHeightGray(m_matHeights[i]);

        cv::cvtColor(matSourceImg, matSourceImg, CV_BGR2RGB);
        QImage img = QImage((uchar*)matSourceImg.data, matSourceImg.cols, matSourceImg.rows, ToInt(matSourceImg.step), QImage::Format_RGB888);
        scene[i]->clear();
        scene[i]->addPixmap(QPixmap::fromImage(img.scaled(QSize(IMG_DISPLAY_WIDTH, IMG_DISPLAY_HEIGHT))));
    }

    int nRow = ui.lineEdit_row->text().toInt();
    int nCol = ui.lineEdit_col->text().toInt();

    generateProfData(true, nRow, m_matHeights, m_profRowDatas);
    generateProfData(false, nCol, m_matHeights, m_profRowDatas);

    if (m_pInspectRowPlot)
    {
        m_pInspectRowPlot = std::make_shared<QCustomPlot>(this);
        setupPlotData(m_pInspectRowPlot, m_profRowDatas);
        ui.dockWidget_profChartRow->setWidget(m_pInspectRowPlot.get());
    }

    if (m_pInspectColPlot)
    {
        m_pInspectColPlot = std::make_shared<QCustomPlot>(this);
        setupPlotData(m_pInspectColPlot, m_profColDatas);
        ui.dockWidget_profChartCol->setWidget(m_pInspectColPlot.get());
    }
}

void Inspect3DProfileWidget::setInspectPos(double dX, double dY)
{
    if (m_matHeights.size() > 0)
    {
        cv::Mat matImg = m_matHeights[0];
        int nMatRow = matImg.rows;
        int nMatCol = matImg.cols;

        int nRow = ToInt(dY * nMatRow / IMG_DISPLAY_HEIGHT);
        int nCol = ToInt(dX * nMatCol / IMG_DISPLAY_WIDTH);

        ui.lineEdit_row->setText(QString("%1").arg(nRow));
        ui.lineEdit_col->setText(QString("%1").arg(nCol));
    }   
}
   

cv::Mat Inspect3DProfileWidget::drawHeightGray(const cv::Mat &matHeight)
{
    double dMinValue = 0, dMaxValue = 0;
    cv::Mat matMask = matHeight == matHeight;
    cv::minMaxIdx(matHeight, &dMinValue, &dMaxValue, 0, 0, matMask);

    cv::Mat matNewPhase = matHeight - dMinValue;

    float dRatio = 255.f / ToFloat(dMaxValue - dMinValue);
    matNewPhase = matNewPhase * dRatio;

    cv::Mat matResultImg;
    matNewPhase.convertTo(matResultImg, CV_8UC1);
    cv::cvtColor(matResultImg, matResultImg, CV_GRAY2BGR);

    return matResultImg;
}

void Inspect3DProfileWidget::generateProfData(bool bRow, int nIndex, QVector<cv::Mat>& matHeights, InspectProfDataVector& profData)
{
    profData.clear();

    int nSlopNum = matHeights.size();

    for (int i = 0; i < nSlopNum; i++)
    {
        QVector<cv::Point2d> points;

        cv::Mat matImg = matHeights[i];
        int nRow = matImg.rows;
        int nCol = matImg.cols;

        int nPtNum = bRow ? nCol : nRow;
        for (int j = 0; j < nPtNum; j++)
        {
            double dHeight = 0.0;
           
            if (bRow)
            {
                cv::Vec3f& data = matImg.at<cv::Vec3f>(nIndex, j);
                dHeight = data[2];
            }
            else
            {
                cv::Vec3f& data = matImg.at<cv::Vec3f>(j, nIndex);
                dHeight = data[2];
            } 

            cv::Point2d pt;
            pt.x = j;
            pt.y = dHeight;
            points.push_back(pt);
        }

        profData.push_back(points);
    }
}

void Inspect3DProfileWidget::setupPlotData(std::shared_ptr<QCustomPlot> customPlot, InspectProfDataVector& profData)
{
    customPlot->legend->setVisible(true);
    customPlot->legend->setFont(QFont("Helvetica", 9));

    QPen pen;
    int nSlopNum = profData.size();
    // add graphs with different line styles:
    for (int i = 0; i < nSlopNum; i++)
    {
        customPlot->addGraph();
        pen.setColor(QColor(qSin(i * 3 + 1.2) * 80 + 80, qSin(i * 1 + 0) * 80 + 80, qSin(i*0.3 + 1.5) * 80 + 80));
        customPlot->graph()->setPen(pen);
        customPlot->graph()->setName(QString("DLP%1").arg(i+1));
        customPlot->graph()->setLineStyle(QCPGraph::lsLine);
        customPlot->graph()->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, 1));
        // generate data:
        int nDataNum = profData[i].size();
        QVector<double> x(nDataNum), y(nDataNum);
        for (int j = 0; j < nDataNum; ++j)
        {
            x[j] = profData[i][j].x;
            y[j] = profData[i][j].y;
        }
        customPlot->graph()->setData(x, y);
        customPlot->graph()->rescaleAxes(true);
    }

    // zoom out a bit:
    customPlot->yAxis->scaleRange(0.15, customPlot->yAxis->range().center()*0.15);
    customPlot->xAxis->scaleRange(1.1, customPlot->xAxis->range().center());
    // set blank axis lines:
    customPlot->xAxis->setTicks(true);
    customPlot->yAxis->setTicks(true);
    customPlot->xAxis->setTickLabels(true);
    customPlot->yAxis->setTickLabels(true);
    // make top right axes clones of bottom left axes:
    customPlot->axisRect()->setupFullAxesBox();
}
