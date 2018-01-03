﻿#include "MotionControl.h"
#include "../common/SystemData.h"
#include "../include/IdDefine.h"
#include "gts.h"

#include <QApplication>
#include <QThread>

////////////////////////////////////////
#ifndef FALSE
#define FALSE               0
#endif

#ifndef TRUE
#define TRUE                1
#endif
/////////////////////////////////

// 定义home回零搜索距离
#define SEARCH_HOME 200000
// 定义到home捕获位置的偏移量
#define HOME_OFFSET 2000
// 定义index回零搜索距离
#define SEARCH_INDEX 15000
// 定义到index捕获位置的偏移量
#define INDEX_OFFSET 1000

MotionControl::MotionControl(QObject *parent)
	: QObject(parent)
{
	m_bSetupTriggerConfig = false;

	m_nMoveProfID = 0;
	m_nMovePointID = 0;

	for (int i = 0; i < AXIS_MOTOR_NUM; i++)
	{
		m_bHome[i] = false;
	}

	if (!init())
	{
		System->setTrackInfo(QStringLiteral("控制系统初始化出问题！"));
	}

	loadConfig();
}

MotionControl::~MotionControl()
{
}

void MotionControl::loadConfig()
{
	m_mapMtrID.clear();
	m_mapMtrID.insert(AxisEnum::MTR_AXIS_X, AXIS_MOTOR_X);
	m_mapMtrID.insert(AxisEnum::MTR_AXIS_Y, AXIS_MOTOR_Y);
	m_mapMtrID.insert(AxisEnum::MTR_AXIS_Z, AXIS_MOTOR_Z);	
}

bool MotionControl::init()
{
	short rtn = 0;

	rtn = GT_Open();
	commandhandler("GT_Open", rtn);
	if (rtn)
	{
		rtn = GT_Close();
		return false;
	}

	QString path = QApplication::applicationDirPath();
	path += "/config/motion/";
	std::string fileName = QString(path + "GTS800.cfg").toStdString();
	const char *expr = fileName.c_str();
	char *buf = new char[strlen(expr) + 1];
	strcpy(buf, expr);
	rtn = GT_Reset();
	commandhandler("GT_Reset", rtn);
	rtn = GT_LoadConfig(buf);
	commandhandler("GT_LoadConfig", rtn);
	delete[] buf;

	if (rtn)
	{
		System->setTrackInfo(QStringLiteral("控制系统Load配置文件出问题！"));
		return false;
	}

	//获取系统 Version
	char* version = new char[200];
	rtn = GT_GetVersion(&version);
	commandhandler("GT_GetVersion", rtn);
	System->setTrackInfo(QString("Motion Version:%1").arg(version));
	//delete[] version;

	if (rtn)
	{
		System->setTrackInfo(QStringLiteral("控制系统获取Version出问题！"));
		return false;
	}

	rtn = GT_ResetGpoMultiDOPulse();
	commandhandler("GT_ResetGpoMultiDOPulse", rtn);

	for (int j = 0; j < 16; j++)
	{
		rtn += GT_SetDoBit(MC_GPO, j, 0);
	}
	commandhandler("GT_SetDoBit", rtn);

	return true;
}

void MotionControl::unInit()
{
	for (int i = 0; i < getMotorAxisNum(); i++)
	{
		m_bHome[i] = false;
	}
	GT_Close();
}

bool MotionControl::reset()
{
	for (int i = 0; i < getMotorAxisNum(); i++)
	{
		m_bHome[i] = false;
	}

	short rtn = 0;

	rtn = GT_Reset();
	commandhandler("GT_Reset", rtn);

	return 0 == rtn;
}

void MotionControl::clearError(int AxisID)
{
	// 指令返回值
	short sRtn = 0;

	// 清除轴状态
	sRtn += GT_ClrSts(AxisID, 8);

	commandhandler("GT_ClrSts", sRtn);
}

void MotionControl::clearAllError()
{
	// 指令返回值
	short sRtn = 0;

	// 清除轴状态
	for (int i = 0; i < getMotorAxisNum(); i++)
	{
		sRtn += GT_ClrSts(m_mapMtrID.values().at(i), 8);
	}

	commandhandler("GT_ClrSts", sRtn);
}

bool MotionControl::IsPowerError()
{
	return true;
}

bool MotionControl::setDO(int nPort, int iState)
{
	// 指令返回值
	short sRtn = 0;

	// EXO6输出高电平，使指示灯灭
	//sRtn = GT_SetDo(MC_GPO, 1 << nPort);
	sRtn = GT_SetDoBit(MC_GPO, nPort, iState);
	commandhandler("GT_SetDo", sRtn);

	return true;
}

bool MotionControl::getDO(int nPort, int &iState)
{
	// 指令返回值
	short sRtn = 0;

	// 通用输入读取值
	long lGpiValue = 0;

	// 读取EXI3输入值
	sRtn = GT_GetDo(MC_GPO, &lGpiValue);

	//commandhandler("GT_GetDo", sRtn);

	iState = 0;
	if (lGpiValue & (1 << nPort))
	{
		iState = 1;
	}

	return true;
}

bool MotionControl::getDI(int nPort, int &iState)
{
	// 指令返回值
	short sRtn = 0;

	// 通用输入读取值
	long lGpiValue = 0;

	// 读取EXI3输入值
	sRtn = GT_GetDi(MC_GPI, &lGpiValue);

	//commandhandler("GT_GetDi", sRtn);

	iState = 0;
	if (lGpiValue & (1 << nPort))
	{
		iState = 1;
	}

	return true;
}

// 3D Functions:
bool MotionControl::triggerCapturing(IMotion::TRIGGER emTrig, bool bWaitDone)
{
	// 指令返回值
	short rtn = 0;

	short sMultPulseSts = 0;
	rtn = GT_GetGpoMultiDOPulseSts(DO_TRIGGER_DLP1, &sMultPulseSts);
	commandhandler("GT_GetGpoMultiDOPulseSts", rtn);

	if (sMultPulseSts)
	{
		System->setTrackInfo(QStringLiteral("motion trigger signal not ready yet"));
		return false;
	}

	if (!m_bSetupTriggerConfig)
	{
		m_bSetupTriggerConfig = true;

		rtn = GT_ResetGpoMultiDOPulse();
		commandhandler("GT_ResetGpoMultiDOPulse", rtn);

		switch (emTrig)
		{
		case IMotion::TRIGGER_ALL:
		{
			int nDlpNumIndex = System->getParam("motion_trigger_dlp_num_index").toInt();
			if (0 == nDlpNumIndex)
			{
				setupTrigger(IMotion::TRIGGER_DLP1);
				setupTrigger(IMotion::TRIGGER_DLP2);
			}
			else if (1 == nDlpNumIndex)
			{
				setupTrigger(IMotion::TRIGGER_DLP1);
				setupTrigger(IMotion::TRIGGER_DLP2);
				setupTrigger(IMotion::TRIGGER_DLP3);
				setupTrigger(IMotion::TRIGGER_DLP4);
			}
		}
		break;
		case IMotion::TRIGGER_DLP1:
			setupTrigger(IMotion::TRIGGER_DLP1);
			break;
		case IMotion::TRIGGER_DLP2:
			setupTrigger(IMotion::TRIGGER_DLP2);
			break;
		case IMotion::TRIGGER_DLP3:
			setupTrigger(IMotion::TRIGGER_DLP3);
			break;
		case IMotion::TRIGGER_DLP4:
			setupTrigger(IMotion::TRIGGER_DLP4);
			break;
		default:
			break;
		}
	}

	rtn = GT_TrigGpoMultiDOPulse();
	commandhandler("GT_TrigGpoMultiDOPulse", rtn);

	rtn = GT_GetGpoMultiDOPulseSts(1, &sMultPulseSts);
	commandhandler("GT_GetGpoMultiDOPulseSts", rtn);

	if (bWaitDone)
	{
		int nWaitTime = 30 * 100;// 10 seconds
		while (0 != sMultPulseSts && nWaitTime-- > 0)
		{
			GT_GetGpoMultiDOPulseSts(DO_TRIGGER_DLP1, &sMultPulseSts);
			QThread::msleep(10);
		}
		if (nWaitTime <= 0) return false;
	}	

	return true;
}

void MotionControl::setupTrigger(IMotion::TRIGGER emTrig)
{
	// 指令返回值
	short rtn = 0;

	TMultiDOPulseCfg strMDoPulseCfg;	

	int nTriggerIndex = 0;
	strMDoPulseCfg.doType = MC_GPO;
	strMDoPulseCfg.doIndex = 1;
	switch (emTrig)
	{
	case IMotion::TRIGGER_ALL:
		break;
	case IMotion::TRIGGER_DLP1:
		strMDoPulseCfg.doIndex = DO_TRIGGER_DLP1;
		nTriggerIndex = 0;
		break;
	case IMotion::TRIGGER_DLP2:
		strMDoPulseCfg.doIndex = DO_TRIGGER_DLP2;
		nTriggerIndex = 1;
		break;
	case IMotion::TRIGGER_DLP3:
		strMDoPulseCfg.doIndex = DO_TRIGGER_DLP3;
		nTriggerIndex = 2;
		break;
	case IMotion::TRIGGER_DLP4:
		strMDoPulseCfg.doIndex = DO_TRIGGER_DLP4;
		nTriggerIndex = 3;
		break;
	default:
		break;
	}
	int nDlpNum = System->getParam("motion_trigger_dlp_num_index").toInt() == 0 ? 2 : 4;
	strMDoPulseCfg.groupNo = nDlpNum;// 2, 4

	double dPatternExposure = System->getParam("motion_trigger_pattern_exposure").toDouble();
	double dPatternPeriod = System->getParam("motion_trigger_pattern_period").toDouble();
	int nPatternNum = System->getParam("motion_trigger_pattern_num").toInt();
	for (int i = 0; i < nDlpNum; i++)
	{
		strMDoPulseCfg.firstLevel[i] = 1;  //该group首先出现的电平状态,0:低电平;1:高电平
		strMDoPulseCfg.highLevelTime[i] = dPatternExposure;  //高电平持续时间[ms]
		strMDoPulseCfg.lowLevelTime[i] = (dPatternPeriod > dPatternExposure) ? (dPatternPeriod  - dPatternExposure): 0; //低电平持续时间[ms]
		strMDoPulseCfg.pulseNum[i] = nPatternNum; //该group输出脉冲的个数,取值范围:非负数,当为0时,表示无限输出脉冲,直到遇到关闭指令
		strMDoPulseCfg.pulseEnable[i] = (i == nTriggerIndex) ? 1 : 0;
		//strMDoPulseCfg.intervaltime[i]  = 0;  
	}

	rtn = GT_CfgGpoMultiDOPulse(&strMDoPulseCfg);
	commandhandler("GT_CfgGpoMultiDOPulse", rtn);
}

void MotionControl::commandhandler(char *command, short error)
{
	// 如果指令执行返回值为非0，说明指令执行错误，向屏幕输出错误结果
	if (error)
	{
		System->setTrackInfo(QStringLiteral("%1 error, error code = %2").arg(command).arg(error));
	}
}

double MotionControl::convertToUm(AxisEnum emAxis, long lPulse)
{
	double dRes = m_mtrParams[getMotorAxisIndex(changeToMtrID(emAxis))]._res;
	return (double)lPulse / dRes;
}

long MotionControl::convertToPulse(AxisEnum emAxis, double dDist)
{
	double dRes = m_mtrParams[getMotorAxisIndex(changeToMtrID(emAxis))]._res;
	return (long)(dDist * dRes);
}

double MotionControl::convertVelToUm(AxisEnum emAxis, double dVelPulse)
{
	double dRes = m_mtrParams[getMotorAxisIndex(changeToMtrID(emAxis))]._res;
	return dVelPulse / dRes * 1000.0; // 50 pulse/ms 
}

double MotionControl::convertVelToPulse(AxisEnum emAxis, double dVelDist)
{
	double dRes = m_mtrParams[getMotorAxisIndex(changeToMtrID(emAxis))]._res;
	return dVelDist * dRes / 1000.0; // 50 mm/s
}

double MotionControl::convertAccToUm(AxisEnum emAxis, double dAccPulse)
{
	double dRes = m_mtrParams[getMotorAxisIndex(changeToMtrID(emAxis))]._res;
	return dAccPulse / dRes * 1000.0 * 1000.0; // 50 pulse/ms2
}

double MotionControl::convertAccToPulse(AxisEnum emAxis, double dAccDist)
{
	double dRes = m_mtrParams[getMotorAxisIndex(changeToMtrID(emAxis))]._res;
	return dAccDist  * dRes / 1000.0 / 1000.0; // 50 mm/s2
}

MotionControl::AxisEnum MotionControl::changeToMtrEnum(int AxisID)
{
	return (AxisEnum)m_mapMtrID.key(AxisID);
}

int MotionControl::changeToMtrID(AxisEnum emAxis)
{
	return m_mapMtrID.value((int)emAxis);
}

int MotionControl::getMotorAxisNum()
{
	return m_mapMtrID.size();
}

int MotionControl::getMotorAxisID(int nIndex)
{
	if (nIndex < 0 || nIndex >= m_mapMtrID.size()) return 0;
	return m_mapMtrID.values().at(nIndex);
}

int MotionControl::getMotorAxisIndex(int AxisID)
{
	int nIndex = m_mapMtrID.values().indexOf(AxisID, 0);
	return nIndex >= 0 ? nIndex : -1;
}

bool MotionControl::enable(int AxisID)
{
	short sRtn = 0; // 指令返回值变量
	sRtn = GT_AxisOn(AxisID);
	commandhandler("GT_AxisOn", sRtn);

	return 0 == sRtn;
}

bool MotionControl::disable(int AxisID)
{
	short sRtn = 0; // 指令返回值变量
	sRtn = GT_AxisOff(AxisID);
	commandhandler("GT_AxisOff", sRtn);

	return 0 == sRtn;
}

bool MotionControl::enableAllAxis()
{
	short sRtn = 0; // 指令返回值变量
	for (int i = 0; i < getMotorAxisNum(); i++)
	{
		sRtn += GT_AxisOn(m_mapMtrID.values().at(i));		
		commandhandler("GT_AxisOn", sRtn);
	}

	return 0 == sRtn;
}

bool MotionControl::isEnabled(int AxisID)
{
	short sRtn = 0; // 指令返回值变量
	long lAxisStatus = 0; // 轴状态

	short bFlagServoOn = FALSE; // 伺服使能标志

	// 读取轴状态
	sRtn = GT_GetSts(AxisID, &lAxisStatus);
	commandhandler("GT_GetSts", sRtn);

	// 伺服使能标志
	if (lAxisStatus & 0x200)
	{
		bFlagServoOn = TRUE;		
	}
	else
	{
		bFlagServoOn = FALSE;
	}

	return bFlagServoOn;
}

bool MotionControl::IsError(int AxisID)
{
	short sRtn = 0; // 指令返回值变量
	long lAxisStatus = 0; // 轴状态

	short bFlagAlarm = FALSE; // 伺服报警标志
	short bFlagMError = FALSE; // 跟随误差越限标志

	// 读取轴状态
	sRtn = GT_GetSts(AxisID, &lAxisStatus);
	commandhandler("GT_GetSts", sRtn);

	// 伺服报警标志
	if (lAxisStatus & 0x2)
	{
		bFlagAlarm = TRUE;		
	}
	else
	{
		bFlagAlarm = FALSE;		
	}

	// 跟随误差越限标志
	if (lAxisStatus & 0x10)
	{
		bFlagMError = TRUE;		
	}
	else
	{
		bFlagMError = FALSE;	
	}

	return bFlagAlarm || bFlagMError;
}

bool MotionControl::IsEMStopError(int AxisID)
{
	short sRtn = 0; // 指令返回值变量
	long lAxisStatus = 0; // 轴状态

	short bFlagAbruptStop = FALSE; // 急停标志

	// 读取轴状态
	sRtn = GT_GetSts(AxisID, &lAxisStatus);
	commandhandler("GT_GetSts", sRtn);

	// 急停标志
	if (lAxisStatus & 0x100)
	{
		bFlagAbruptStop = TRUE;		
	}
	else
	{
		bFlagAbruptStop = FALSE;	
	}

	return bFlagAbruptStop;
}

bool MotionControl::IsLimit(int AxisID)
{
	short sRtn = 0; // 指令返回值变量
	long lAxisStatus = 0; // 轴状态

	short bFlagPosLimit = FALSE; // 正限位触发标志
	short bFlagNegLimit = FALSE; // 负限位触发标志

	// 读取轴状态
	sRtn = GT_GetSts(AxisID, &lAxisStatus);
	commandhandler("GT_GetSts", sRtn);

	// 正向限位
	if (lAxisStatus & 0x20)
	{
		bFlagPosLimit = TRUE;	
	}
	else
	{
		bFlagPosLimit = FALSE;
	}

	// 负向限位
	if (lAxisStatus & 0x40)
	{
		bFlagNegLimit = TRUE;		
	}
	else
	{
		bFlagNegLimit = FALSE;		
	}

	return bFlagPosLimit || bFlagNegLimit;
}

bool MotionControl::home(int AxisID, bool bSyn)
{
	short sRtn = 0; // 指令返回值变量

	int nHomeDir = m_mtrParams[getMotorAxisIndex(AxisID)]._homeProf._dir == 0 ? 1 : -1;

	// 清状态
	sRtn = GT_ClrSts(AxisID);
	commandhandler("GT_ClrSts", sRtn);

	if (!isEnabled(AxisID))
	{
		if (!enable(AxisID)) return false;		
	}

	// 开启轴的home捕获功能
	sRtn = GT_SetCaptureMode(AxisID, CAPTURE_HOME);
	commandhandler("GT_SetCaptureMode", sRtn);
	// 设置轴为点位运动模式
	sRtn = GT_PrfTrap(AxisID);
	commandhandler("GT_PrfTrap", sRtn);
	// 读取点位运动参数
	TTrapPrm trapPrm;
	sRtn = GT_GetTrapPrm(AxisID, &trapPrm);
	commandhandler("GT_GetTrapPrm", sRtn);
	trapPrm.acc = m_mtrParams[getMotorAxisIndex(AxisID)]._homeProf._velPf._acc;
	trapPrm.dec = m_mtrParams[getMotorAxisIndex(AxisID)]._homeProf._velPf._dec;
	// 设置点位运动参数
	sRtn = GT_SetTrapPrm(AxisID, &trapPrm);
	commandhandler("GT_SetTrapPrm", sRtn);
	// 设置目标速度
	sRtn = GT_SetVel(AxisID, m_mtrParams[getMotorAxisIndex(AxisID)]._homeProf._velPf._vel);
	commandhandler("GT_SetVel", sRtn);
	// 设置目标位置
	sRtn = GT_SetPos(AxisID, SEARCH_HOME * nHomeDir);
	commandhandler("GT_SetPos", sRtn);
	// 启动运动，等待home信号触发
	sRtn = GT_Update(1 << (AxisID - 1));
	commandhandler("GT_Update", sRtn);

	// 捕获状态
	short capture = 0;
	// 捕获位置
	long pos = 0;

	// 分别是规划位置，编码器位
	double prfPos = 0, encPos = 0;

	int nTimeOut = 30 * 100;// 30 seconds
	do
	{
		// 读取捕获状态
		sRtn = GT_GetCaptureStatus(AxisID, &capture, &pos);
		// 读取规划位置
		sRtn = GT_GetPrfPos(AxisID, &prfPos);
		// 读取编码器位置
		sRtn = GT_GetEncPos(AxisID, &encPos);
	
		// 电机已经停止，说明整个搜索过程中home信号一直没有触发
		if (isMoveDone(AxisID))
		{
			System->setTrackInfo(QStringLiteral("电机已经停止, Home失败！"));
			return false;
		}

		QThread::msleep(10);
		// 如果home信号已经触发，则退出循环，捕获位置已经在pos变量中保存
	} while (0 == capture && nTimeOut-- > 0);

	if (nTimeOut <= 0)
	{
		System->setTrackInfo(QStringLiteral("电机回零TimeOut！"));
		return false;
	}

	// 设定目标位置为捕获位置+偏移量
	sRtn = GT_SetPos(AxisID, pos + HOME_OFFSET*nHomeDir);
	commandhandler("GT_SetPos", sRtn);
	// 启动运动
	sRtn = GT_Update(1 << (AxisID - 1));
	commandhandler("GT_Update", sRtn);

	nTimeOut = 30 * 100;// 30 seconds
	do 
	{
		// 读取规划位置
		sRtn = GT_GetPrfPos(AxisID, &prfPos);

		QThread::msleep(10);
	} while (!isMoveDone(AxisID) && nTimeOut-- > 0);


	if (nTimeOut <= 0)
	{
		System->setTrackInfo(QStringLiteral("电机回零TimeOut！"));
		return false;
	}

	if (qAbs(prfPos - (pos + HOME_OFFSET)) > 10)
	{
		System->setTrackInfo(QStringLiteral("电机回零位置偏移问题！"));
		return false;
	}

	QThread::msleep(200);

	// 启动index捕获
	sRtn = GT_SetCaptureMode(AxisID, CAPTURE_INDEX);
	commandhandler("GT_SetCaptureMode", sRtn);

	// 设置当前位置+index搜索距离为目标位置
	sRtn = GT_SetPos(AxisID, (long)(prfPos + SEARCH_INDEX * (-nHomeDir)));
	commandhandler("GT_SetPos", sRtn);

	// 启动运动
	sRtn = GT_Update(1 << (AxisID - 1));
	commandhandler("GT_Update", sRtn);

	nTimeOut = 30 * 100;// 30 seconds
	do
	{
		// 读取捕获状态
		sRtn = GT_GetCaptureStatus(AxisID, &capture, &pos);
		// 读取规划位置
		sRtn = GT_GetPrfPos(AxisID, &prfPos);
		// 读取编码器位置
		sRtn = GT_GetEncPos(AxisID, &encPos);
	
		// 电机已经停止，说明整个搜索过程中index信号一直没有触发
		if (isMoveDone(AxisID))
		{
			System->setTrackInfo(QStringLiteral("电机已经停止, Index失败！"));
			return false;
		}

		QThread::msleep(10);

		// 如果index信号已经触发，则退出循环，捕获位置已经在pos变量中保存
	} while (0 == capture && nTimeOut-- > 0);

	if (nTimeOut <= 0)
	{
		System->setTrackInfo(QStringLiteral("电机回零TimeOut！"));
		return false;
	}

	// 设置捕获位置+index偏移量为目标位置
	sRtn = GT_SetPos(AxisID, pos + INDEX_OFFSET * (-nHomeDir));
	commandhandler("GT_SetPos", sRtn);
	// 启动运动
	sRtn = GT_Update(1 << (AxisID - 1));
	commandhandler("GT_Update", sRtn);

	nTimeOut = 30 * 100;// 30 seconds
	do
	{
		// 读取规划位置
		sRtn = GT_GetPrfPos(AxisID, &prfPos);
		// 读取编码器位置
		sRtn = GT_GetEncPos(AxisID, &encPos);

		QThread::msleep(10);
		
	} while (!isMoveDone(AxisID) && nTimeOut-- > 0);

	if (nTimeOut <= 0)
	{
		System->setTrackInfo(QStringLiteral("电机回零TimeOut！"));
		return false;
	}
	if (qAbs(prfPos - (pos + INDEX_OFFSET * (-nHomeDir))) > 10)
	{
		System->setTrackInfo(QStringLiteral("电机Index位置偏移问题！"));
		return false;
	}

	QThread::msleep(200);

	// 更新原点位置
	sRtn = GT_ZeroPos(AxisID);
	commandhandler("GT_ZeroPos", sRtn);

	// AXIS轴规划位置清零
	sRtn = GT_SetPrfPos(AxisID, 0);
	commandhandler("GT_SetPrfPos", sRtn);

	m_bHome[changeToMtrEnum(AxisID)] = true;

	return true;
}

bool MotionControl::move(int AxisID, int nProfile, double dDist, bool bSyn)
{
	QMtrMoveProfile mtrProf = getMotorProfile(nProfile);
	double dVec = mtrProf._velPf._vel;
	double acc = mtrProf._velPf._acc;
	double dec = mtrProf._velPf._dec;
	int smooth = mtrProf._smooth;

	double dCurPos = 0;
	if (!getCurrentPos(AxisID, &dCurPos))
	{
		return false;
	}

	return move(AxisID, dVec, acc, dec, smooth, dCurPos + dDist, bSyn);
}

bool MotionControl::moveTo(int AxisID, int nProfile, double dPos, bool bSyn)
{
	QMtrMoveProfile mtrProf = getMotorProfile(nProfile);
	double dVec = mtrProf._velPf._vel;
	double acc = mtrProf._velPf._acc;
	double dec = mtrProf._velPf._dec;
	int smooth = mtrProf._smooth;

	return move(AxisID, dVec, acc, dec, smooth, dPos, bSyn);
}

bool MotionControl::movePos(int nPointTable, bool bSyn)
{
	QMtrMovePoint mtrPt = getMotorPoint(nPointTable);
	QMtrMoveProfile mtrProf = getMotorProfile(mtrPt._ProfID);
	double dVec = mtrProf._velPf._vel;
	double acc = mtrProf._velPf._acc;
	double dec = mtrProf._velPf._dec;
	int smooth = mtrProf._smooth;

	double dCurPos = 0;
	if (!getCurrentPos(mtrPt._AxisID, &dCurPos))
	{
		return false;
	}

	return move(mtrPt._AxisID, dVec, acc, dec, smooth, dCurPos + mtrPt._posn, bSyn);
}

bool MotionControl::moveToPos(int nPointTable, bool bSyn)
{
	QMtrMovePoint mtrPt = getMotorPoint(nPointTable);
	QMtrMoveProfile mtrProf = getMotorProfile(mtrPt._ProfID);
	double dVec = mtrProf._velPf._vel;
	double acc = mtrProf._velPf._acc;
	double dec = mtrProf._velPf._dec;
	int smooth = mtrProf._smooth;

	return move(mtrPt._AxisID, dVec, acc, dec, smooth, mtrPt._posn, bSyn);
}

bool MotionControl::move(int AxisID, double dVec, double acc, double dec, int smooth, double dPos, bool bSyn)
{
	short sRtn = 0; // 指令返回值变量

	// 将AXIS轴设为点位模式
	sRtn = GT_PrfTrap(AxisID);
	commandhandler("GT_PrfTrap", sRtn);

	TTrapPrm trap;
	// 读取点位运动参数
	sRtn = GT_GetTrapPrm(AxisID, &trap);
	commandhandler("GT_GetTrapPrm", sRtn);

	trap.acc = convertAccToPulse(changeToMtrEnum(AxisID), acc);
	trap.dec = convertAccToPulse(changeToMtrEnum(AxisID), dec);
	trap.smoothTime = smooth;

	// 设置点位运动参数
	sRtn = GT_SetTrapPrm(AxisID, &trap);
	commandhandler("GT_SetTrapPrm", sRtn);

	// 设置AXIS轴的目标位置
	sRtn = GT_SetPos(AxisID, convertToPulse(changeToMtrEnum(AxisID), dPos));
	commandhandler("GT_SetPos", sRtn);

	// 设置AXIS轴的目标速度
	sRtn = GT_SetVel(AxisID, convertVelToPulse(changeToMtrEnum(AxisID), dVec));
	commandhandler("GT_SetVel", sRtn);

	// 启动AXIS轴的运动
	sRtn = GT_Update(1 << (AxisID - 1));
	commandhandler("GT_Update", sRtn);

	if (bSyn)
	{
		// 分别是规划位置，编码器位
		//double prfPos = 0, encPos = 0;
		int nTimeOut = 30 * 100;// 30 seconds
		do
		{
			// 读取规划位置
			//sRtn = GT_GetPrfPos(AxisID, &prfPos);
			// 读取编码器位置
			//sRtn = GT_GetEncPos(AxisID, &encPos);

			QThread::msleep(10);

		} while (!isMoveDone(AxisID) && nTimeOut-- > 0);

		if (nTimeOut <= 0)
		{
			System->setTrackInfo(QStringLiteral("电机运动TimeOut！"));
			return false;
		}
	}
	

	return true;
}

bool MotionControl::isHomed(int AxisID)
{
	return m_bHome[changeToMtrEnum(AxisID)];
}

bool MotionControl::isMoveDone(int AxisID)
{
	short sRtn = 0; // 指令返回值变量
	long lAxisStatus = 0; // 轴状态

	short bFlagMotion = FALSE; // 规划器运动标志

	// 读取轴状态
	sRtn = GT_GetSts(AxisID, &lAxisStatus);
	commandhandler("GT_GetSts", sRtn);

	// 规划器正在运动标志
	if (lAxisStatus & 0x400)
	{
		bFlagMotion = TRUE;		
	}
	else
	{
		bFlagMotion = FALSE;		
	}

	return !bFlagMotion;
}

bool MotionControl::stopMove(int AxisID)
{
	short sRtn = 0; // 指令返回值变量

	long mask = 0;

	mask = 1 << (AxisID - 1);
	sRtn = GT_Stop(mask, 0);
	commandhandler("GT_Stop", sRtn);

	return 0 == sRtn;
}

bool MotionControl::EmStop(int AxisID)
{
	short sRtn = 0; // 指令返回值变量

	long mask = 0;

	mask = 1 << (AxisID - 1);
	sRtn = GT_Stop(mask, mask);
	commandhandler("GT_Stop", sRtn);

	return 0 == sRtn;
}

bool MotionControl::getCurrentPos(int AxisID, double *pos)
{
	short sRtn = 0; // 指令返回值变量

	double dMtrPos = 0;

	// 读取axis编码器位置
	sRtn = GT_GetEncPos(AxisID, &dMtrPos);
	commandhandler("GT_GetAxisEncPos", sRtn);

	if (sRtn)
	{
		*pos = 0;
		return false;
	}
	else
	{
		*pos = convertToUm(changeToMtrEnum(AxisID), dMtrPos);
		return true;
	}
}

QString MotionControl::getCurrentStatus(int AxisID)
{
	QString szStatus = QStringLiteral("未初始化");
	if (isEnabled(AxisID))
	{		
		if (isHomed(AxisID))
		{
			szStatus = QStringLiteral("已回零");
		}
		else
		{
			szStatus = QStringLiteral("已使能");
		}
	}
	else if (IsError(AxisID))
	{
		szStatus = QStringLiteral("驱动错误");
	}
	else if (IsLimit(AxisID))
	{
		szStatus = QStringLiteral("限位报警");
	}
	else if (IsEMStopError(AxisID))
	{
		szStatus = QStringLiteral("急停报警");
	}

	return szStatus;
}

void MotionControl::clearMotorParams()
{
	m_mtrParams.clear();
}

void MotionControl::addMotorParam(QMotorParam& mtrParam)
{
	m_mtrParams.push_back(mtrParam);
}

void MotionControl::updateMotorParam(int nID, QMotorParam& mtrParam)
{
	for (int i = 0; i < m_mtrParams.size(); i++)
	{
		if (m_mtrParams[i]._ID == nID)
		{
			m_mtrParams[i] = mtrParam;
			m_mtrParams[i]._ID = nID;
			break;
		}
	}
}

int MotionControl::getMotorParamsNum()
{
	return m_mtrParams.size();
}

QMotorParam& MotionControl::getMotorParam(int nID)
{
	for (int i = 0; i < m_mtrParams.size(); i++)
	{
		if (m_mtrParams[i]._ID == nID)
		{
			return m_mtrParams[i];
		}
	}

	return QMotorParam();
}

QMotorParam& MotionControl::getMotorParamByIndex(int nIndex)
{
	if (nIndex < 0 || nIndex >= m_mtrParams.size()) return QMotorParam();
	return m_mtrParams[nIndex];
}

void MotionControl::clearMotorProfiles()
{
	m_mtrMoveProfs.clear();
}

void MotionControl::addMotorProfile(QMtrMoveProfile& mtrMoveProf)
{
	m_mtrMoveProfs.push_back(mtrMoveProf);
}

void MotionControl::updateMotorProfile(int nID, QMtrMoveProfile& mtrMoveProf)
{
	for (int i = 0; i < m_mtrMoveProfs.size(); i++)
	{
		if (m_mtrMoveProfs[i]._ID == nID)
		{
			m_mtrMoveProfs[i] = mtrMoveProf;
			m_mtrMoveProfs[i]._ID = nID;
			break;
		}
	}
}

int MotionControl::getMotorProfilesNum()
{
	return m_mtrMoveProfs.size();
}

QMtrMoveProfile& MotionControl::getMotorProfile(int nID)
{
	for (int i = 0; i < m_mtrMoveProfs.size(); i++)
	{
		if (m_mtrMoveProfs[i]._ID == nID)
		{
			return m_mtrMoveProfs[i];
		}
	}

	return QMtrMoveProfile();
}

QMtrMoveProfile& MotionControl::getMotorProfileByIndex(int nIndex)
{
	if (nIndex < 0 || nIndex >= m_mtrMoveProfs.size()) return QMtrMoveProfile();
	return m_mtrMoveProfs[nIndex];
}

void MotionControl::removeMotorProfile(int nID)
{
	for (int i = 0; i < m_mtrMoveProfs.size(); i++)
	{
		if (m_mtrMoveProfs[i]._ID == nID)
		{
			m_mtrMoveProfs.removeAt(i);
			break;
		}
	}
}

int MotionControl::incrementMotorProfileID()
{
	return m_nMoveProfID++;
}

void MotionControl::setMotorProfileID(int nID)
{
	m_nMoveProfID = nID;
}

void MotionControl::clearMotorPoints()
{
	m_mtrMovePoints.clear();
}

void MotionControl::addMotorPoint(QMtrMovePoint& mtrMovePoint)
{
	m_mtrMovePoints.push_back(mtrMovePoint);
}

void MotionControl::updateMotorPoint(int nID, QMtrMovePoint& mtrMovePoint)
{
	for (int i = 0; i < m_mtrMovePoints.size(); i++)
	{
		if (m_mtrMovePoints[i]._ID == nID)
		{
			m_mtrMovePoints[i] = mtrMovePoint;
			m_mtrMovePoints[i]._ID = nID;
			break;
		}
	}
}

int MotionControl::getMotorPointsNum()
{
	return m_mtrMovePoints.size();
}

QMtrMovePoint& MotionControl::getMotorPoint(int nID)
{
	for (int i = 0; i < m_mtrMovePoints.size(); i++)
	{
		if (m_mtrMovePoints[i]._ID == nID)
		{
			return m_mtrMovePoints[i];
		}
	}

	return QMtrMovePoint();
}

QMtrMovePoint& MotionControl::getMotorPointByIndex(int nIndex)
{
	if (nIndex < 0 || nIndex >= m_mtrMovePoints.size()) return QMtrMovePoint();
	return m_mtrMovePoints[nIndex];
}

void MotionControl::removeMotorPoint(int nID)
{
	for (int i = 0; i < m_mtrMovePoints.size(); i++)
	{
		if (m_mtrMovePoints[i]._ID == nID)
		{
			m_mtrMovePoints.removeAt(i);
			break;
		}
	}
}

int MotionControl::incrementMotorPointID()
{
	return m_nMovePointID++;
}

void MotionControl::setMotorPointID(int nID)
{
	m_nMovePointID = nID;
}