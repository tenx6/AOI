#pragma once

#include <QWidget>
#include "ui_InspWindowWidget.h"
#include "InspWindowSelectWidget.h"
#include "InspVoidWidget.h"

enum INSP_WIDGET_INDEX {
    INSP_WINDOW_SELECT,
    INSP_VOID,
};

class InspWindowSelectWidget;

class InspWindowWidget : public QWidget
{
    Q_OBJECT

public:
    InspWindowWidget(QWidget *parent = Q_NULLPTR);
    ~InspWindowWidget();

    void setCurrentIndex(int index);

protected:
    virtual void showEvent(QShowEvent *event) override;

private slots:
    void on_btnReturn_clicked();

private:
    Ui::InspWindowWidget ui;

    InspWindowSelectWidget  *m_pInspWindowSelectWidget;
    InspVoidWidget          *m_pInspVoidWidget;
};
