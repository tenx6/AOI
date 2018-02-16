#include "InspWindowSelectWidget.h"
#include "InspWindowWidget.h"

InspWindowSelectWidget::InspWindowSelectWidget(InspWindowWidget *parent)
    : QWidget(parent)
{
    ui.setupUi(this);
    m_pParent = parent;
}

InspWindowSelectWidget::~InspWindowSelectWidget()
{
}

void InspWindowSelectWidget::on_btnFindLineWindow_clicked()
{
    m_pParent->setCurrentIndex(FIND_LINE);
}

void InspWindowSelectWidget::on_btnCaliperCircleWindow_clicked()
{
}

void InspWindowSelectWidget::on_btnVoidInspWindow_clicked()
{
    m_pParent->setCurrentIndex ( INSP_VOID );
}