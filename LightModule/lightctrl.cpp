﻿#include "lightctrl.h"
#include "../Common/SystemData.h"
#include "LightDefine.h"
#include <qvariant.h>

#include "lightmodule_global.h"

QLightCtrl::QLightCtrl(QObject *parent)
	: QObject(parent)
{
	int n = System->getParam(LIGHT_COUNT).toInt();
	for(int i = 0; i<n; i++)
	{
		QString name = QString("light%0").arg(i+1);
        m_deviceList.append(new QLightDevice(name, _CHN_NUM, NULL));
	}
	
}
void QLightCtrl::init()
{
	int n = System->getParam(LIGHT_COUNT).toInt();
	for (int i = 0; i<n; i++)
	{
		QString key = QString("%0-%1").arg(m_deviceList[i]->getDeviceName()).arg(COMM_PORT);
		QString port = System->getParam(key).toString();
		key = QString("%0-%1").arg(m_deviceList[i]->getDeviceName()).arg(COMM_BOUND);
		int baud = System->getParam(key).toInt();
		m_deviceList[i]->openCommPort(port,baud);
        for (int j = 0; j<_MAX_CHDATA_NUM; j++)
		{
			m_deviceList[i]->openLight(j);
		}
		//m_deviceList[i]->openCommPort(->getDeviceName(),m_deviceList[i]->ge);
	}
}
QLightCtrl::~QLightCtrl()
{

}

void QLightCtrl::delDevice(int nIndex)
{
    QLightDevice * device = m_deviceList[nIndex];
	if(device)delete device;
    m_deviceList.removeAt(nIndex);
}

QLightDevice * QLightCtrl::getLightDevice(int nIndex)
{
    if (nIndex < m_deviceList.size())
	{
        return m_deviceList[nIndex];
	}
	return NULL;
}

void QLightCtrl::setLuminance(int nChannel, int nLum)
{
    QLightDevice * pDevice = getLightDevice(0);
    if (pDevice)
    {
        int nDevice = nChannel / pDevice->getChnNum();
        int nCh = nChannel % pDevice->getChnNum();

        QLightDevice *device = getLightDevice(nDevice);
        if (device)
        {
            device->setChLuminance(nCh, nLum);
        }
    }    
}

QString QLightCtrl::getChName(int nChannel)
{
    QLightDevice * pDevice = getLightDevice(0);
    if (pDevice)
    {
        int nDevice = nChannel / pDevice->getChnNum();
        int nCh = nChannel % pDevice->getChnNum();

        QString key;
        QVariant data;
        QLightDevice *device = getLightDevice(nDevice);
        if (device)
        {
            key = QString("%0-%1%2").arg(device->getDeviceName()).arg(NAMED_CH).arg(nCh);
            data = System->getParam(key);
            return data.toString();
        }
    }

    return "";
}

int QLightCtrl::getChLuminace(int nChannel)
{
    QLightDevice * pDevice = getLightDevice(0);
    if (pDevice)
    {
        int nDevice = nChannel / pDevice->getChnNum();
        int nCh = nChannel % pDevice->getChnNum();

        QString key;
        QVariant data;
        QLightDevice *device = getLightDevice(nDevice);
        if (device)
        {
            key = QString("%0-%1%2").arg(device->getDeviceName()).arg(LUM_CH).arg(nCh);
            data = System->getParam(key);
            return data.toInt();
        }

        //QLightDevice *device = getLightDevice(nDevice);
        //if (device)
        //{
        //    return device->getChLuminance(nCh);
        //}
    }

    return 0;
}

void QLightCtrl::saveLuminance(int nChannel)
{
    QLightDevice * pDevice = getLightDevice(0);
    if (pDevice)
    {
        int nDevice = nChannel / pDevice->getChnNum();
        int nCh = nChannel % pDevice->getChnNum();

        QLightDevice *device = getLightDevice(nDevice);
        if (device)
        {
            QString key = QString("%0-%1%2").arg(device->getDeviceName()).arg(LUM_CH).arg(nCh);
            QString data = QString("%1").arg(device->getChLuminance(nCh));
            System->setParam(key, data);
        }
    }
}