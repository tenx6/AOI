#pragma once

#include <QRunnable>
#include <QThreadPool>
#include "../HSGWorkflowCtrl/Calc3DHeightRunnable.h"

class Merge3DHeightRunnable : public QRunnable
{
    using VectorOfMat = std::vector<cv::Mat>;

public:
    Merge3DHeightRunnable(
        QThreadPool                                *pCalc3DHeightThreadPool,
        const std::vector<Calc3dHeightRunnablePtr> &vecCalc3DHeightRunnable,
        VectorOfMat                                *pVec3DFrameImages,
        int                                         nRow,
        int                                         nCol,
        int                                         nTotalRows,
        int                                         nTotalCols);
    ~Merge3DHeightRunnable();

protected:
    virtual void run() override;

private:
    QThreadPool                          *m_pCalc3dHeightThreadPool;
    std::vector<Calc3dHeightRunnablePtr>  m_vecCalc3DHeightRunnable;
    VectorOfMat                          *m_pVec3DFrameImages;
    int                                   m_nRow, m_nCol, m_nTotalRows, m_nTotalCols;
};