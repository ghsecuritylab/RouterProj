#include <rtthread.h>
#include <rtdevice.h>
#include "strategy.h"
#include "chargepile.h"
#include "698.h"
#include "meter.h"
#include "storage.h"
#include <board.h>

#ifndef FALSE
#define FALSE         0
#endif
#ifndef TRUE
#define TRUE          1
#endif
#ifndef SUCCESSFUL
#define SUCCESSFUL    0
#endif
#ifndef FAILED
#define FAILED        1
#endif
#ifndef ORTHERS
#define ORTHERS       255
#endif

#define THREAD_STRATEGY_PRIORITY     8
#define THREAD_STRATEGY_STACK_SIZE   2048
#define THREAD_STRATEGY_TIMESLICE    5

static rt_uint8_t strategy_stack[THREAD_STRATEGY_STACK_SIZE];//线程堆栈
static struct rt_thread strategy;

#define RELAYA_PIN    GET_PIN(F, 2)
#define RELAYB_PIN    GET_PIN(F, 3)

#define KC1_PIN     GET_PIN(F, 4)
#define KC2_PIN     GET_PIN(F, 5)
#define KC3_PIN     GET_PIN(F, 6)

extern rt_uint32_t Strategy_get_BLE_event(void);
extern rt_uint8_t BLE_CtrlUnit_RecResp(COMM_CMD_C cmd,void *STR_SetPara,int count);
////////////////////////////// 变量定义区 ////////////////////////////////
//故障原因
static char *err_strfault[] = 
{
           "                       ",            /* ERR_OK          0  */
           "终端主板内存故障！   ",               /* ERR             1  */
           "时钟故障！         ",                 /* ERR             2  */
           "主板通信故障！	       ",            /* ERR             3  */
           "485抄表故障！         ",              /* ERR             4  */
           "显示板故障！         ",               /* ERR             5  */
           "载波通道异常！       ",               /* ERR             6  */
           "NandFLASH初始化错误！       ",        /* ERR             7  */
           "ESAM错误！         ",                 /* ERR             8  */
           "蓝牙模块故障！         ",             /* ERR             9  */
           "电源模块故障！         ",             /* ERR             10 */
           "充电桩通信故障！        ",            /* ERR             11 */
           "充电桩设备故障！         ",           /* ERR             12 */
	       "本地订单记录满！       ",             /* ERR             13 */
		   "RTC通信故障！       ",             	 /* ERR             14 */
};

CCMRAM ChargPilePara_TypeDef ChargePilePara_Set;
CCMRAM ChargPilePara_TypeDef ChargePilePara_Get;

CCMRAM static CHARGE_EXE_STATE_ASK ExeState_CtrlAsk;//控制器
CCMRAM static CHARGE_EXE_STATE_ASK ExeState_BleAsk;//蓝牙

CCMRAM CHARGE_STRATEGY Chg_Strategy;
CCMRAM static CHARGE_STRATEGY_RSP Chg_StrategyRsp;
CCMRAM CHARGE_STRATEGY Adj_Chg_Strategy;
CCMRAM static CHARGE_STRATEGY_RSP Adj_Chg_StrategyRsp;

CCMRAM static CHARGE_APPLY Chg_Apply;
CCMRAM static CHARGE_APPLY_RSP Chg_Apply_Rsp;

//上送事件
CCMRAM static PLAN_OFFER_EVENT Plan_Offer_Event;
CCMRAM static CHARGE_APPLY_EVENT Chg_Apply_Event;
CCMRAM static CHARGE_EXE_EVENT BLE_ChgExe_Event;
//表计在线状态变化事件
CCMRAM static ONLINE_STATE OnlineState_Event;	

//超时处理
CCMRAM static rt_timer_t StartChgResp;
CCMRAM static rt_timer_t StopChgResp;
CCMRAM static rt_timer_t PowerAdjResp;
CCMRAM static rt_timer_t ChgReqReportRsp;
//定时轮训
CCMRAM static rt_timer_t ChgPileStateGet;
//等待上报事件确认
CCMRAM static rt_timer_t ChgPlanOfferAck;
CCMRAM static rt_timer_t ChgPlanExeStateAck;
CCMRAM static rt_timer_t ChgRecordAck;


//指令标志
CCMRAM static rt_bool_t startchg_flag;
CCMRAM static rt_bool_t stopchg_flag;
CCMRAM static rt_bool_t adjpower_flag;

CCMRAM static CTL_CHARGE Ctrl_Start;//包括返回参数
CCMRAM static CTL_CHARGE Ctrl_Stop;
CCMRAM static CTL_CHARGE BLE_Stop;
CCMRAM static CTL_CHARGE Ctrl_PowerAdj;

//充电执行状态
CCMRAM CHARGE_EXE_STATE Chg_ExeState;

CCMRAM CTL_CHARGE_EVENT CtrlCharge_Event;
CCMRAM static CHARGE_EXE_EVENT Ctrl_ChgExe_Event;
CCMRAM static CHG_ORDER_EVENT ChgOrder_Event;//订单共用一个变量

CCMRAM static unsigned char PlanSlotCount;
CCMRAM static unsigned char SetPowerFinishFlag[50];
CCMRAM static char cRequestNO_Old[17];
CCMRAM static char cRequestNO_New[17];

/**************************************************************
 * 函数名称: RELAY_ON 
 * 参    数: 
 * 返 回 值: 
 * 描    述: 吸合继电器
 **************************************************************/
 void RELAY_ON(void)
{
	rt_pin_write(RELAYB_PIN, PIN_LOW);
	rt_thread_mdelay(200);
	rt_pin_write(RELAYA_PIN, PIN_HIGH);
}
/**************************************************************
 * 函数名称: RELAY_OFF 
 * 参    数: 
 * 返 回 值: 
 * 描    述: 断开继电器
 **************************************************************/
void RELAY_OFF(void)
{
	rt_pin_write(RELAYA_PIN, PIN_LOW);
	rt_thread_mdelay(200);
	rt_pin_write(RELAYB_PIN, PIN_HIGH);
}
/**************************************************************
 * 函数名称: StartChgResp_Timeout 
 * 参    数: 
 * 返 回 值: 
 * 描    述: 启动充电超时函数
 **************************************************************/
static void StartChgResp_Timeout(void *parameter)
{
	rt_int8_t p_rst;
	rt_lprintf("[strategy] : StartChgResp event is timeout!\n");
	p_rst = ChargepileDataGetSet(Cmd_ChargeStartResp,0);
	
//	if(p_rst != SUCCESSFUL)
	
}
/**************************************************************
 * 函数名称: StopChgResp_Timeout 
 * 参    数: 
 * 返 回 值: 
 * 描    述: 停止充电超时函数
 **************************************************************/
static void StopChgResp_Timeout(void *parameter)
{
    rt_uint8_t p_rst;
	rt_lprintf("[strategy] : StopChgResp event is timeout!\n");
	p_rst = ChargepileDataGetSet(Cmd_ChargeStopResp,0);
	
	if(p_rst == SUCCESSFUL)
		PileIfo.WorkState = ChgSt_Finished;
}
/**************************************************************
 * 函数名称: PowAdjResp_Timeout 
 * 参    数: 
 * 返 回 值: 
 * 描    述: 调整功率超时函数
 **************************************************************/
static void PowAdjResp_Timeout(void *parameter)
{
    rt_uint8_t p_rst;
	rt_lprintf("[strategy] : PowerAdjResp event is timeout!\n");
	p_rst = ChargepileDataGetSet(Cmd_SetPowerResp,0);
}
/**************************************************************
 * 函数名称: ChgReqReportResp_Timeout 
 * 参    数: 
 * 返 回 值: 
 * 描    述: 上送充电申请超时函数
 **************************************************************/
static void ChgReqReportResp_Timeout(void *parameter)
{
    rt_lprintf("[strategy] : ChgReqReportResp event is timeout!\n");
	
}
/**************************************************************
 * 函数名称: ChgReqReportResp_Timeout 
 * 参    数: 
 * 返 回 值: 
 * 描    述: 轮训充电桩状态函数
 **************************************************************/
static void ChgPileStateGet_Timeout(void *parameter)
{
    rt_lprintf("[strategy] : ChgPileStateGet event is timeout!\n");
	//查询充电桩状态
	ChargepileDataGetSet(Cmd_GetPilePara,&ChargePilePara_Get);
}
/**************************************************************
 * 函数名称: ChgPlanOfferAck_Timeout 
 * 参    数: 
 * 返 回 值: 
 * 描    述: 等待充电计划确认函数
 **************************************************************/
CCMRAM static rt_uint8_t ChgPlanOfferAck_count;
static void ChgPlanOfferAck_Timeout(void *parameter)
{
	rt_int8_t c_rst;
	rt_lprintf("[strategy] : ChgPlanOfferAck event is timeout!\n");
	
	c_rst = CtrlUnit_RecResp(Cmd_ChgPlanOffer,&Plan_Offer_Event,0);//上报充电计划事件
	
	//等待确认
	if(c_rst == SUCCESSFUL)
	{	
		ChgPlanOfferAck_count++;
	}
		
	if(ChgPlanOfferAck_count>=3)
		rt_timer_stop(ChgPlanOfferAck);
}

/**************************************************************
 * 函数名称: ChgPlanExeStateAck_Timeout 
 * 参    数: 
 * 返 回 值: 
 * 描    述: 等待充电计划执行状态确认函数
 **************************************************************/
CCMRAM static rt_uint8_t ChgPlanExeStateAck_count;
static void ChgPlanExeStateAck_Timeout(void *parameter)
{
	rt_int8_t c_rst;
	rt_lprintf("[strategy] : ChgPlanExeStateAck event is timeout!\n");
	
	c_rst = CtrlUnit_RecResp(Cmd_ChgPlanExeState,&Ctrl_ChgExe_Event,0);//上报充电计划执行事件
	
	//等待确认
	if(c_rst == SUCCESSFUL)
	{	
		ChgPlanExeStateAck_count++;
	}
		
	if(ChgPlanExeStateAck_count>=3)
		rt_timer_stop(ChgPlanExeStateAck);
}
/**************************************************************
 * 函数名称: ChgRecordAck_Timeout 
 * 参    数: 
 * 返 回 值: 
 * 描    述: 等待充电订单确认函数
 **************************************************************/
CCMRAM static rt_uint8_t ChgRecordAck_count;
static void ChgRecordAck_Timeout(void *parameter)
{
	rt_int8_t c_rst;
	rt_lprintf("[strategy] : ChgRecordAck event is timeout!\n");
	
	c_rst = CtrlUnit_RecResp(Cmd_ChgRecord,&ChgOrder_Event,0);//上报充电订单
	
	//等待确认
	if(c_rst == SUCCESSFUL)
	{	
		ChgRecordAck_count++;
	}
		
	if(ChgRecordAck_count>=3)
		rt_timer_stop(ChgRecordAck);
}

/**************************************************************
 * 函数名称: timer_create_init 
 * 参    数: 
 * 返 回 值: 
 * 描    述: 定时器
 **************************************************************/
static void timer_create_init()
{
     /* 创建启机回复定时器 */
	 StartChgResp = rt_timer_create("StartChgResp",  /* 定时器名字是 StartChgResp */
									StartChgResp_Timeout, /* 超时时回调的处理函数 */
									RT_NULL, /* 超时函数的入口参数 */
									5000, /* 定时长度，以OS Tick为单位，即5000个OS Tick */
									RT_TIMER_FLAG_ONE_SHOT|RT_TIMER_FLAG_SOFT_TIMER); /* 一次性定时器 */
	/* 创建停机回复定时器 */
	 StopChgResp = rt_timer_create("StopChgResp",  /* 定时器名字是 StopChgResp */
									StopChgResp_Timeout, /* 超时时回调的处理函数 */
									RT_NULL, /* 超时函数的入口参数 */
									5000, /* 定时长度，以OS Tick为单位，即5000个OS Tick */
									RT_TIMER_FLAG_ONE_SHOT|RT_TIMER_FLAG_SOFT_TIMER); /* 一次性定时器 */
	/* 创建调整功率回复定时器 */
	 PowerAdjResp = rt_timer_create("PowerAdjResp",  /* 定时器名字是 PowerAdjResp */
									PowAdjResp_Timeout, /* 超时时回调的处理函数 */
									RT_NULL, /* 超时函数的入口参数 */
									5000, /* 定时长度，以OS Tick为单位，即5000个OS Tick */
									RT_TIMER_FLAG_ONE_SHOT|RT_TIMER_FLAG_SOFT_TIMER); /* 一次性定时器 */
	
	/* 创建 充电申请上送回复 定时器 */
	 ChgReqReportRsp = rt_timer_create("ChgReqReportRsp",  /* 定时器名字是 ChgReqReportRsp */
									ChgReqReportResp_Timeout, /* 超时时回调的处理函数 */
									RT_NULL, /* 超时函数的入口参数 */
									5000, /* 定时长度，以OS Tick为单位，即5000个OS Tick */
									RT_TIMER_FLAG_ONE_SHOT|RT_TIMER_FLAG_SOFT_TIMER); /* 一次性定时器 */
	/* 创建 充电桩状态查询 定时器 */
	ChgPileStateGet = rt_timer_create("ChgPileStateGet",  /* 定时器名字是 ChgPileStateGet */
									ChgPileStateGet_Timeout, /* 超时时回调的处理函数 */
									RT_NULL, /* 超时函数的入口参数 */
									1000, /* 定时长度，以OS Tick为单位，即1000个OS Tick */
									RT_TIMER_FLAG_PERIODIC|RT_TIMER_FLAG_SOFT_TIMER); /* 周期性定时器 */
	/* 创建 充电计划确认 定时器 */
	ChgPlanOfferAck = rt_timer_create("ChgPlanOfferAck",  /* 定时器名字是 ChgPlanOfferAck */
									ChgPlanOfferAck_Timeout, /* 超时时回调的处理函数 */
									RT_NULL, /* 超时函数的入口参数 */
									5000, /* 定时长度，以OS Tick为单位，即3000个OS Tick */
									RT_TIMER_FLAG_PERIODIC|RT_TIMER_FLAG_SOFT_TIMER); /* 周期性定时器 */
	/* 创建 充电计划执行状态确认 定时器 */
	ChgPlanExeStateAck = rt_timer_create("ChgPlanExeStateAck",  /* 定时器名字是 ChgPlanExeStateAck */
									ChgPlanExeStateAck_Timeout, /* 超时时回调的处理函数 */
									RT_NULL, /* 超时函数的入口参数 */
									5000, /* 定时长度，以OS Tick为单位，即3000个OS Tick */
									RT_TIMER_FLAG_PERIODIC|RT_TIMER_FLAG_SOFT_TIMER); /* 周期性定时器 */
	/* 创建 充电订单确认 定时器 */
	ChgRecordAck = rt_timer_create("ChgRecordAck",  /* 定时器名字是 ChgRecordAck */
									ChgRecordAck_Timeout, /* 超时时回调的处理函数 */
									RT_NULL, /* 超时函数的入口参数 */
									5000, /* 定时长度，以OS Tick为单位，即3000个OS Tick */
									RT_TIMER_FLAG_PERIODIC|RT_TIMER_FLAG_SOFT_TIMER); /* 周期性定时器 */
	/* 启动定时器 */
	if (ChgPileStateGet != RT_NULL)
		rt_timer_start(ChgPileStateGet);
}
/********************************************************************  
*	函 数 名: ExeState_Update()
*	功能说明: 路由器充电执行状态更新
*	形    参: 无
*	返 回 值: 无
********************************************************************/ 
static void ExeState_Update(void)
{	
	memcpy(Chg_ExeState.cRequestNO,Chg_Apply_Event.RequestNO,sizeof(Chg_ExeState.cRequestNO));
	memcpy(Chg_ExeState.cAssetNO,RouterIfo.AssetNum,sizeof(Chg_ExeState.cAssetNO));
	Chg_ExeState.GunNum = ExeState_BleAsk.GunNum;
	
	if(Chg_ExeState.exeState != EXE_ING)//非执行过程中，按计划中额定功率传
		Chg_ExeState.ucPlanPower = Chg_Strategy.ulChargeRatePow;
	
	ScmMeter_HisData stgMeter_HisData;
	cmMeter_get_data(EMMETER_HISDATA,&stgMeter_HisData);//获取电表计量数据
	memcpy(&Chg_ExeState.ulEleBottomValue[0],&stgMeter_HisData.ulMeter_Day,5*sizeof(long));
	memcpy(&Chg_ExeState.ulEleActualValue[0],&stgMeter_HisData.ulMeter_Day,5*sizeof(long));
	
	ScmMeter_Analog stgMeter_Analog;
	cmMeter_get_data(EMMETER_ANALOG,&stgMeter_Analog);
	Chg_ExeState.ucActualPower = stgMeter_Analog.ulAcPwr/100;
	Chg_ExeState.ucVoltage.A = stgMeter_Analog.ulVol;
	Chg_ExeState.ucCurrent.A = stgMeter_Analog.ulCur;
	
	ChargepileDataGetSet(Cmd_GetPilePara,&ChargePilePara_Get);
	Chg_ExeState.ChgPileState = ChargePilePara_Get.ChgPileState;//直接取用充电桩的三态
	memcpy(&Chg_ExeState.cUserID,&Chg_Apply_Event.UserAccount,sizeof(Chg_ExeState.cUserID));	
}
/********************************************************************  
*	函 数 名: CtrlData_RecProcess()
*	功能说明: 控制命令接收处理函数
*	形    参: 无
*	返 回 值: 无
********************************************************************/ 
static void CtrlData_RecProcess(void)
{
	rt_uint8_t i;
	rt_bool_t PlanAccepted;
	rt_int8_t c_rst,b_rst,p_rst,s_rst;
	rt_uint32_t chgplanIssue,chgplanIssueAdj,startchg,stopchg;
	rt_uint32_t Ctrl_EventCmd=Cmd_Null,BLE_EventCmd=Cmd_Null;
	char Cmd[32];
	
	/***************************** 接收控制器命令 *******************************/
	Ctrl_EventCmd = strategy_event_get();
	memset(Cmd,0,sizeof(Cmd));
	itoa(Ctrl_EventCmd,Cmd,2);		  //转换成字符串，进制基数2 
	
	for(i = 0; i <sizeof(rt_uint32_t)*8;i++)
	{
		if(Ctrl_EventCmd&(0x00000001<<i))
		{
			if(i != 0)
			{
				Ctrl_EventCmd = i;
				rt_kprintf("[strategy]:  (%s)  recv event is [%s]  \n",__func__,Cmd);
				break;
			}
		}
	}
	
	switch(Ctrl_EventCmd)
	{
		case Cmd_ChgRequestReportAck://充电申请事件应答
		{
			c_rst = CtrlUnit_RecResp(Cmd_ChgRequestReportAck,0,0);//取值
			break;
		}
		//收到充电计划
		case Cmd_ChgPlanIssue:
		{
			PlanAccepted = TRUE;
			c_rst = CtrlUnit_RecResp(Cmd_ChgPlanIssue,&Chg_Strategy,0);//取值
			
			if(Fault.Total != TRUE)
			{
				if(memcmp(&RouterIfo.AssetNum,&Chg_Strategy.cAssetNO,sizeof(RouterIfo.AssetNum)) == 0)//校验资产一致性
				{
					if((Chg_Strategy.ucChargeMode == ORDER)&&(Chg_Strategy.ucDecType == PLAN_CREATE))
					{
						memcpy(cRequestNO_New,Chg_Strategy.cRequestNO,sizeof(cRequestNO_New));//接收到最新充电计划
						memset(&SetPowerFinishFlag,0,50);//清空标志位
						PlanSlotCount = 0;
					}
					
					memcpy(&Chg_StrategyRsp.cRequestNO,&Chg_Strategy.cRequestNO,sizeof(Chg_StrategyRsp.cRequestNO));
					memcpy(&Chg_StrategyRsp.cAssetNO,&Chg_Strategy.cAssetNO,sizeof(Chg_StrategyRsp.cAssetNO));
					Chg_StrategyRsp.cSucIdle = SUCCESSFUL;
					
					rt_kprintf("[strategy]:  (%s)  recv charge plan sucess \n",__func__);
				}
				else
				{
					rt_kprintf("[strategy]:  (%s)  recv charge plan assetnum error \n",__func__);
					Chg_StrategyRsp.cSucIdle = ORTHERS;
				}
			}
			else
			{
				rt_kprintf("[strategy]:  (%s)  device fault \n",__func__);
				Chg_StrategyRsp.cSucIdle = FAILED;
			}		
			
			c_rst = CtrlUnit_RecResp(Cmd_ChgPlanIssueAck,&Chg_StrategyRsp,0);//回复	
		
			//回复成功上送事件
			if(c_rst == SUCCESSFUL)
			{
				memcpy(&Plan_Offer_Event.StartTimestamp,&System_Time_STR,sizeof(STR_SYSTEM_TIME));//记录上报时间
				SetStorageData(Cmd_PlanOfferWr,&Plan_Offer_Event,sizeof(PLAN_OFFER_EVENT));//存储事件
				
				Plan_Offer_Event.OrderNum++;		
				Plan_Offer_Event.ChannelState = 0;//通道状态
				memcpy(&Plan_Offer_Event.Chg_Strategy,&Chg_Strategy,sizeof(CHARGE_STRATEGY));
				memcpy(&Plan_Offer_Event.FinishTimestamp,&System_Time_STR,sizeof(STR_SYSTEM_TIME));
				c_rst = CtrlUnit_RecResp(Cmd_ChgPlanOffer,&Plan_Offer_Event,0);//上报充电计划事件
				
				//等待确认
				if(c_rst == SUCCESSFUL)
				{				
					/* 开始启动回复计时 */
					if (ChgPlanOfferAck != RT_NULL)
					{
						rt_timer_start(ChgPlanOfferAck);
						ChgPlanOfferAck_count = 0;
						rt_lprintf("[strategy]  (%s)  Start ChgPlanOfferAck TimeCounting!\n",__func__);	
					}
					else
						rt_lprintf("ChgPlanOfferAck timer create error\n");					
									
				}
				else
				{
					rt_kprintf("[strategy]  (%s)  充电计划上送失败!\n",__func__);					
				}
			}
			else
			{
				rt_kprintf("[strategy]  (%s)  回复充电计划失败!\n",__func__);					
			}
			break;
		}
		//收到充电计划事件确认
		case Cmd_ChgPlanOfferAck://wyg191113
		{
			rt_kprintf("[strategy]  (%s) ChgPlanOfferAck Received!\n",__func__);
			rt_timer_stop(ChgPlanOfferAck);
			c_rst = CtrlUnit_RecResp(Cmd_ChgPlanOfferAck,&Plan_Offer_Event,0);//回复
			
			Chg_Apply.ChargeMode = DISORDER;//  测试用 需要删除  wyg191113
			Chg_Apply.GunNum = 0;
			
				if(Chg_Apply.ChargeMode == DISORDER)//无序充电
				{
					memcpy(&BLE_ChgExe_Event.StartTimestamp,&System_Time_STR,sizeof(STR_SYSTEM_TIME));//事件发生时间
					if(PileIfo.WorkState == ChgSt_Standby)
					{
						CtrlCharge_Event.CtrlType = CTRL_START;
						CtrlCharge_Event.StartSource = BLE_UNIT;
						p_rst = ChargepileDataGetSet(Cmd_ChargeStart,0);
						
						if(p_rst == SUCCESSFUL)
						{
							PileIfo.WorkState = ChgSt_InCharging;//此处优先置位，下发启动成功视为充电桩已启动
							Chg_ExeState.exeState = EXE_ING;
							rt_kprintf("[strategy]  (%s) Charge Started, Successful!\n",__func__);				
						}
						else
						{
							Chg_ExeState.exeState = EXE_FAILED;
							rt_kprintf("[strategy]  (%s) BLE申请后立即启动充电，失败！\n",__func__);
						}
					}
					else
					{
						Chg_ExeState.exeState = EXE_FAILED;
						rt_kprintf("[strategy]  (%s) Charge Started, fail! PileIfo.WorkState = %d\n",__func__,PileIfo.WorkState);
					}
					
					//上送充电执行事件
					BLE_ChgExe_Event.OrderNum++;
					BLE_ChgExe_Event.OccurSource = 0;
					ExeState_Update();
					memcpy(&BLE_ChgExe_Event.Chg_ExeState,&Chg_ExeState,sizeof(CHARGE_EXE_STATE));
					BLE_ChgExe_Event.Chg_ExeState.GunNum = Chg_Apply.GunNum;//注：这儿取值不同
				
					memcpy(&BLE_ChgExe_Event.FinishTimestamp,&System_Time_STR,sizeof(STR_SYSTEM_TIME));//事件结束时间
					//存储充电执行事件
					s_rst = SetStorageData(Cmd_ChgExecuteWr,&BLE_ChgExe_Event,sizeof(CHARGE_EXE_EVENT));
					if(s_rst == SUCCESSFUL)
					{
						rt_kprintf("[strategy]  (%s) Storage BLE_ChgExe_Event, Successful!\n",__func__);
					}
					else
					{
						rt_kprintf("[strategy]  (%s) 保存充电执行事件，失败！\n",__func__);
						SetStorageData(Cmd_ChgExecuteWr,&BLE_ChgExe_Event,sizeof(CHARGE_EXE_EVENT));//再存一次
					}

					b_rst = BLE_CtrlUnit_RecResp(Cmd_ChgPlanExeState,&BLE_ChgExe_Event,0);		
					if(b_rst == SUCCESSFUL)
					{
						rt_kprintf("[strategy]  (%s) BLE_ChgExe_Event Apply to BLE, Successful!\n",__func__);				
					}
					else
					{
						rt_kprintf("[strategy]  (%s) 回复BLE充电执行事件，失败！\n",__func__);
					}
					
					c_rst = CtrlUnit_RecResp(Cmd_ChgPlanExeState,&BLE_ChgExe_Event,0);//回复	wyg191105
					if(c_rst == SUCCESSFUL)
					{
						rt_kprintf("[strategy]  (%s) BLE_ChgExe_Event Apply to Controller, Successful!\n",__func__);				
					}
					else
					{
						rt_kprintf("[strategy]  (%s) 回复控制器关于BLE充电执行事件，失败！\n",__func__);
					}
				}
				
				break;
	
		}
		//收到充电计划执行事件确认
		case Cmd_ChgPlanExeStateAck:
		{
			rt_kprintf("[strategy]  (%s) ChgPlanExeStateAck Received!\n",__func__);
			rt_timer_stop(ChgPlanExeStateAck);
			CtrlUnit_RecResp(Cmd_ChgPlanExeStateAck,0,0);//取值	
			break;
		}
		//收到充电计划调整
		case Cmd_ChgPlanAdjust:
		{
			c_rst = CtrlUnit_RecResp(Cmd_ChgPlanAdjust,&Adj_Chg_Strategy,0);//取值	
			if(Fault.Total != TRUE)
			{
				if(memcmp(&RouterIfo.AssetNum,&Adj_Chg_Strategy.cAssetNO,sizeof(RouterIfo.AssetNum)) == 0)//校验资产一致性
				{
					if((Adj_Chg_Strategy.ucChargeMode == ORDER)&&(Adj_Chg_Strategy.ucDecType == PLAN_ADJ))
					{
						memcpy(cRequestNO_New,Adj_Chg_Strategy.cRequestNO,sizeof(cRequestNO_New));//接收到最新充电计划
					}
					
					memcpy(&Adj_Chg_StrategyRsp.cRequestNO,&Adj_Chg_Strategy.cRequestNO,sizeof(Adj_Chg_Strategy.cRequestNO));
					memcpy(&Adj_Chg_StrategyRsp.cAssetNO,&Adj_Chg_Strategy.cAssetNO,sizeof(Adj_Chg_Strategy.cAssetNO));
					Adj_Chg_StrategyRsp.cSucIdle = SUCCESSFUL;
				}
				else
				{
					Adj_Chg_StrategyRsp.cSucIdle = ORTHERS;
				}
			}
			else
			{
				Adj_Chg_StrategyRsp.cSucIdle = FAILED;
			}
			
			c_rst = CtrlUnit_RecResp(Cmd_ChgPlanAdjustAck,&Adj_Chg_StrategyRsp,0);//回复	
			
			//事件记录与上报，待定
			break;
		}
		//收到启动充电命令
		case Cmd_StartChg:
		{
			startchg_flag = TRUE;
			CtrlCharge_Event.StartSource = CONTRL_UNIT;
			
			c_rst = CtrlUnit_RecResp(Cmd_StartChg,&Ctrl_Start,0);//取值
			rt_lprintf("[strategy]  (%s)  收到@控制器@启动充电命令  \n",__func__);
			memcpy(&CtrlCharge_Event,&Ctrl_Start,41);
			CtrlCharge_Event.CtrlType = CTRL_START;
			
			if(Fault.Total != TRUE)
			{
				if(memcmp(&RouterIfo.AssetNum,&Ctrl_Start.cAssetNO,sizeof(RouterIfo.AssetNum)) == 0)//校验资产一致性
				{
					p_rst = ChargepileDataGetSet(Cmd_ChargeStart,0);	
				
					/* 开始启动回复计时 */
					if (StartChgResp != RT_NULL)
						rt_timer_start(StartChgResp);
					else
						rt_lprintf("StartChgResp timer create error\n");							
				}
				else
				{
					Ctrl_Start.cSucIdle = ORTHERS;
					c_rst = CtrlUnit_RecResp(Cmd_StartChgAck,&Ctrl_Start,0);//回复
				}			
			}
			else
			{
				Ctrl_Start.cSucIdle = FAILED;	
				c_rst = CtrlUnit_RecResp(Cmd_StartChgAck,&Ctrl_Start,0);//回复			
			}		
			CtrlCharge_Event.cSucIdle = Ctrl_Start.cSucIdle;			
			break;
		}
		//收到停止充电命令
		case Cmd_StopChg:
		{
			stopchg_flag = TRUE;	
			CtrlCharge_Event.StopSource = CONTRL_UNIT;
			
			c_rst = CtrlUnit_RecResp(Cmd_StopChg,&Ctrl_Stop,0);//取值			
			rt_lprintf("[strategy]  (%s)  收到@控制器@停止充电命令  \n",__func__); 
			memcpy(&CtrlCharge_Event,&Ctrl_Stop,41);			
			CtrlCharge_Event.CtrlType = CTRL_STOP;
			
			if(Fault.Total != TRUE)
			{
				if((memcmp(&RouterIfo.AssetNum,&Ctrl_Stop.cAssetNO,sizeof(RouterIfo.AssetNum)) == 0)//校验资产一致性
					||(memcmp(&Ctrl_Start.OrderSn,&Ctrl_Stop.OrderSn,sizeof(Ctrl_Start.OrderSn)) == 0))//校验启停单号一致性		
				{
					p_rst = ChargepileDataGetSet(Cmd_ChargeStop,0);	
				
					/* 开始停机回复计时 */
					if (StopChgResp != RT_NULL)
						rt_timer_start(StopChgResp);
					else
						rt_lprintf("StopChgResp timer create error\n");
				}
				else
				{
					Ctrl_Stop.cSucIdle = ORTHERS;
					c_rst = CtrlUnit_RecResp(Cmd_StopChgAck,&Ctrl_Stop,0);//回复
				}
			}
			else
			{
				Ctrl_Stop.cSucIdle = FAILED;
				c_rst = CtrlUnit_RecResp(Cmd_StopChgAck,&Ctrl_Stop,0);//回复
			}
			CtrlCharge_Event.cSucIdle = Ctrl_Stop.cSucIdle;
			break;
		}
		//收到调整功率命令
		case Cmd_PowerAdj:
		{
			adjpower_flag = TRUE;
			c_rst = CtrlUnit_RecResp(Cmd_PowerAdj,&Ctrl_PowerAdj,0);//取值	
			rt_lprintf("[strategy]  (%s)  收到@控制器@调整功率命令  \n",__func__);  
			memcpy(&CtrlCharge_Event,&Ctrl_PowerAdj,41);
			CtrlCharge_Event.CtrlType = CTRL_ADJPOW;
			
			if(Fault.Total != TRUE)
			{
				if(memcmp(&RouterIfo.AssetNum,&Ctrl_PowerAdj.cAssetNO,sizeof(RouterIfo.AssetNum)) == 0)//校验资产一致性
				{
					ChargePilePara_Set.PWM_Duty = Ctrl_PowerAdj.SetPower*10/132;//功率换算: D(含一位小数)=I/60*1000=P/(60*220)*1000
					p_rst = ChargepileDataGetSet(Cmd_SetPower,&ChargePilePara_Set);	
					
					/* 开始功率调整回复计时 */
					if (PowerAdjResp != RT_NULL)
						rt_timer_start(PowerAdjResp);
					else
						rt_lprintf("[strategy] : PowerAdjResp timer create error\n");
				}
				else
				{
					Ctrl_PowerAdj.cSucIdle = ORTHERS;
					c_rst = CtrlUnit_RecResp(Cmd_PowerAdjAck,&Ctrl_PowerAdj,0);//回复
				}
			}
			else
			{
				Ctrl_PowerAdj.cSucIdle = FAILED;
				c_rst = CtrlUnit_RecResp(Cmd_PowerAdjAck,&Ctrl_PowerAdj,0);//回复
			}
			Ctrl_Stop.cSucIdle = Ctrl_PowerAdj.cSucIdle;
			break;
		}
		//收到控制器抄读路由器工作状态
		case Cmd_RouterExeState:
		{
			rt_lprintf("[strategy]  (%s)  收到@控制器@查询工作状态的命令  \n",__func__);  
			
			c_rst = CtrlUnit_RecResp(Cmd_RouterExeState,&Chg_ExeState,0);//取值	
			
			if(memcmp(ExeState_CtrlAsk.cAssetNO,RouterIfo.AssetNum,sizeof(ExeState_CtrlAsk.cAssetNO)) == 0)
			{
				ExeState_Update();	
			}
			else
			{
				Chg_ExeState.exeState = EXE_FAILED;//资产号不匹配，视为“执行失败"
			}		
			c_rst = CtrlUnit_RecResp(Cmd_RouterExeStateAck,&Chg_ExeState,1);//回复	 wyg191105
			
			break;
		}
		//收到充电订单事件确认
		case Cmd_ChgRecordAck:
		{
			rt_lprintf("[strategy]  (%s) ChgRecordAck Received!\n",__func__);
			rt_timer_stop(ChgRecordAck);
			break;
		}
		//收到充电申请确认
		case ChgReqReportConfirm_EVENT:
		{	
				
			break;
		}	
		//收到STA监测自身及路由器在线状态
		case Cmd_STAOnlineState:
		{			
			rt_lprintf("[strategy]  (%s)  收到@STA@监测自身及路由器在线状态的命令  \n",__func__); 
			c_rst = CtrlUnit_RecResp(Cmd_STAOnlineState,&OnlineState_Event,0);//取值
			if(c_rst == SUCCESSFUL)
			{
				rt_kprintf("[strategy]  (%s) OnlineState_Event,Cmd_STAOnlineState,Successful!\n",__func__);
				CtrlUnit_RecResp(Cmd_STAOnlineStateAck,&OnlineState_Event,0);//回复确认
			}
			else
				rt_kprintf("[strategy]  (%s) 获取表计在线状态，失败！\n",__func__);
		
			//存储在线状态变化事件
			s_rst = SetStorageData(Cmd_OnlineStateWr,&OnlineState_Event,sizeof(ONLINE_STATE));
			if(s_rst == SUCCESSFUL)
			{
				rt_lprintf("[strategy]  (%s) Storage BLE_ChgExe_Event, Successful!\n",__func__);
			}
			else
			{
				rt_kprintf("[strategy]  (%s) 保存在线状态，失败！\n",__func__);
				SetStorageData(Cmd_OnlineStateWr,&OnlineState_Event,sizeof(ONLINE_STATE));//再存一次
			}
					
			b_rst = BLE_CtrlUnit_RecResp(Cmd_STAOnlineStateAPP,&OnlineState_Event,0);//传送至蓝牙
			if(b_rst == SUCCESSFUL)
				rt_lprintf("[strategy]  (%s) OnlineState_Event Apply to BLE, Successful!\n",__func__);				
			else
				rt_kprintf("[strategy]  (%s) 回复BLE 表计在线状态事件，失败！\n",__func__);		
			break;
		}			
		default:
			break;
	}
	Ctrl_EventCmd = 0;//清位
	
	
	/***************************** 接收APP命令 *******************************/
	BLE_EventCmd = Strategy_get_BLE_event();
	memset(Cmd,0,sizeof(Cmd));
	itoa(BLE_EventCmd,Cmd,2);		  //转换成字符串，进制基数2 
	rt_lprintf("[strategy]  (%s)  收到@APP@命令,事件号编码为：[%s]  \n",__func__,Cmd);
	for(i = 0; i <sizeof(rt_uint32_t)*8;i++)
	{
		if(i != 0)
		{
			if(BLE_EventCmd&(0x00000001<<i))
			{
				BLE_EventCmd = i;
				break;
			}
		}
	}
	switch(BLE_EventCmd)
	{
		//充电申请    然后直接启动充电，启动后立即上送执行事件
		case Cmd_ChgRequest:
		{	
			rt_lprintf("[strategy]  (%s) <ChgRequest_EVENT> 收到@蓝牙@ 充电申请 的命令\n",__func__); 
			memset(&Chg_Apply_Event,0,sizeof(CHARGE_APPLY_EVENT));
			memset(&Chg_Apply_Rsp,0,sizeof(CHARGE_APPLY_RSP));
			memset(&BLE_ChgExe_Event,0,sizeof(CHARGE_EXE_EVENT));
			
			memcpy(&Chg_Apply_Event.RequestTimeStamp,&System_Time_STR,sizeof(STR_SYSTEM_TIME));//充电申请时间
			memcpy(&Chg_Apply_Event.StartTimestamp,&System_Time_STR,sizeof(STR_SYSTEM_TIME));//事件发生时间
			b_rst = BLE_CtrlUnit_RecResp(Cmd_ChgRequest,&Chg_Apply,0);//取值
			
			if(b_rst == SUCCESSFUL)
			{
				rt_lprintf("[strategy]  (%s) Receive the data of Chg_Apply from BLE, Successful!\n",__func__);
				for(i=0;i<sizeof(Chg_Apply.cAssetNO);i++)
					rt_kprintf("[strategy]  (%s) Chg_Apply.cAssetNO = %0x\n",__func__,Chg_Apply.cAssetNO[i]);
				
				if(memcmp(&RouterIfo.AssetNum,&Chg_Apply.cAssetNO,sizeof(RouterIfo.AssetNum)) == 0)//校验资产一致性
				{
					Chg_Apply_Event.OrderNum++;
					rt_lprintf("[strategy]  (%s) Chg_Apply_Event.OrderNum = %d\n",__func__,Chg_Apply_Event.OrderNum);	

					memcpy(&Chg_Apply_Event.RequestNO,&Chg_Apply.cRequestNO,sizeof(Chg_Apply_Event.RequestNO));
					memcpy(&Chg_Apply_Event.AssetNO,&Chg_Apply.cAssetNO,sizeof(Chg_Apply_Event.AssetNO));
					Chg_Apply_Event.GunNum = Chg_Apply.GunNum;
					
					Chg_Apply_Event.ChargeReqEle = Chg_Apply.ulChargeReqEle;			
					memcpy(&Chg_Apply_Event.PlanUnChg_TimeStamp,&Chg_Apply.PlanUnChg_TimeStamp,sizeof(STR_SYSTEM_TIME));
					Chg_Apply_Event.ChargeMode = Chg_Apply.ChargeMode;
					
					memcpy(&Chg_Apply_Event.UserAccount,&Chg_Apply.cUserID,sizeof(Chg_Apply_Event.UserAccount));							
					memcpy(&Chg_Apply_Event.Token,&Chg_Apply.Token,sizeof(Chg_Apply.Token));		//按32有效数取值

					/* 充电申请上送回复计时 */
					if (ChgReqReportRsp != RT_NULL)
						rt_timer_start(ChgReqReportRsp);
					else
						rt_lprintf("ChgReqReportResp timer create error\n");

					Chg_Apply_Rsp.cSucIdle = SUCCESSFUL;
				}
				else
				{				
					Chg_Apply_Rsp.cSucIdle = FAILED;
					rt_kprintf("[strategy]  (%s) 校验资产一致性，失败！\n",__func__);
				}
				memcpy(&Chg_Apply_Rsp.cRequestNO,&Chg_Apply.cRequestNO,sizeof(Chg_Apply.cRequestNO));
				memcpy(&Chg_Apply_Rsp.cAssetNO,&Chg_Apply.cAssetNO,sizeof(Chg_Apply.cAssetNO));
				Chg_Apply_Rsp.GunNum = Chg_Apply.GunNum;
			}
			else
			{
				rt_kprintf("[strategy]  (%s) 获取BLE传输的数据，失败！\n",__func__);
			}
			
			//充电申请确认响应
			b_rst = BLE_CtrlUnit_RecResp(Cmd_ChgRequestAck,&Chg_Apply_Rsp,0);	
			if(b_rst == SUCCESSFUL)
			{
				rt_lprintf("[strategy]  (%s) Chg_Apply Response, Successful!\n",__func__);				
			}
			else
			{
				rt_kprintf("[strategy]  (%s) 回复BLE充电申请确认响应，失败！\n",__func__);
			}
			

			//申请事件处理
			s_rst = SetStorageData(Cmd_ChgRequestWr,&Chg_Apply_Event,sizeof(CHARGE_APPLY_EVENT));
			if(s_rst == SUCCESSFUL)
			{
				rt_lprintf("[strategy]  (%s) Sto+rage Chg_Apply_Event, Successful!\n",__func__);
			}
			else
			{
				rt_kprintf("[strategy]  (%s) 保存 BLE申请事件，失败！\n",__func__);
				SetStorageData(Cmd_ChgRequestWr,&Chg_Apply_Event,sizeof(CHARGE_APPLY_EVENT));//再存一次
			}
			memcpy(&Chg_Apply_Event.FinishTimestamp,&System_Time_STR,sizeof(STR_SYSTEM_TIME));//事件结束时间
			c_rst = CtrlUnit_RecResp(Cmd_ChgRequestReport,&Chg_Apply_Event,0);//通知控制器
			b_rst = BLE_CtrlUnit_RecResp(Cmd_ChgRequestReportAPP,&Chg_Apply_Event,0);//同时将事件回传APP
			
			
			//暂定立即启动充电
			if(b_rst == SUCCESSFUL)
			{
//				rt_lprintf("[strategy]  (%s) Charge_Apply Response to BLE, Successful!\n",__func__);
//				if(Chg_Apply.ChargeMode == DISORDER)//无序充电
//				{
//					memcpy(&BLE_ChgExe_Event.StartTimestamp,&System_Time_STR,sizeof(STR_SYSTEM_TIME));//事件发生时间
//					if(PileIfo.WorkState == ChgSt_Standby)
//					{
//						CtrlCharge_Event.CtrlType = CTRL_START;
//						CtrlCharge_Event.StartSource = BLE_UNIT;
//						p_rst = ChargepileDataGetSet(Cmd_ChargeStart,0);
//						
//						if(p_rst == SUCCESSFUL)
//						{
//							PileIfo.WorkState = ChgSt_InCharging;//此处优先置位，下发启动成功视为充电桩已启动
//							Chg_ExeState.exeState = EXE_ING;
//							rt_lprintf("[strategy]  (%s) Charge Started, Successful!\n",__func__);				
//						}
//						else
//						{
//							Chg_ExeState.exeState = EXE_FAILED;
//							rt_kprintf("[strategy]  (%s) BLE申请后立即启动充电，失败！\n",__func__);
//						}
//					}
//					else
//					{
//						Chg_ExeState.exeState = EXE_FAILED;
//					}
//					
//					//上送充电执行事件
//					BLE_ChgExe_Event.OrderNum++;
//					BLE_ChgExe_Event.OccurSource = 0;
//					ExeState_Update();
//					memcpy(&BLE_ChgExe_Event.Chg_ExeState,&Chg_ExeState,sizeof(CHARGE_EXE_STATE));
//					BLE_ChgExe_Event.Chg_ExeState.GunNum = Chg_Apply.GunNum;//注：这儿取值不同
//				
//					memcpy(&BLE_ChgExe_Event.FinishTimestamp,&System_Time_STR,sizeof(STR_SYSTEM_TIME));//事件结束时间
//					//存储充电执行事件
//					s_rst = SetStorageData(Cmd_ChgExecuteWr,&BLE_ChgExe_Event,sizeof(CHARGE_EXE_EVENT));
//					if(s_rst == SUCCESSFUL)
//					{
//						rt_lprintf("[strategy]  (%s) Storage BLE_ChgExe_Event, Successful!\n",__func__);
//					}
//					else
//					{
//						rt_kprintf("[strategy]  (%s) 保存充电执行事件，失败！\n",__func__);
//						SetStorageData(Cmd_ChgExecuteWr,&BLE_ChgExe_Event,sizeof(CHARGE_EXE_EVENT));//再存一次
//					}

//					b_rst = BLE_CtrlUnit_RecResp(Cmd_ChgPlanExeState,&BLE_ChgExe_Event,0);		
//					if(b_rst == SUCCESSFUL)
//					{
//						rt_lprintf("[strategy]  (%s) BLE_ChgExe_Event Apply to BLE, Successful!\n",__func__);				
//					}
//					else
//					{
//						rt_kprintf("[strategy]  (%s) 回复BLE充电执行事件，失败！\n",__func__);
//					}
//					
//					c_rst = CtrlUnit_RecResp(Cmd_ChgPlanExeState,&BLE_ChgExe_Event,0);//回复	wyg191105
//					if(c_rst == SUCCESSFUL)
//					{
//						rt_lprintf("[strategy]  (%s) BLE_ChgExe_Event Apply to Controller, Successful!\n",__func__);				
//					}
//					else
//					{
//						rt_kprintf("[strategy]  (%s) 回复控制器关于BLE充电执行事件，失败！\n",__func__);
//					}
//				}
			}	
			else
			{
				rt_kprintf("[strategy]  (%s) 回复BLE充电申请事件，失败！\n",__func__);
			}	
			break;
		}
		//收到蓝牙抄读路由器工作状态
		case Cmd_RouterExeState:
		{
			rt_lprintf("[strategy]  (%s)  <AskState_EVENT> 收到 @蓝牙@ 查询工作状态 的命令  \n",__func__,BLE_EventCmd);  				
			b_rst = BLE_CtrlUnit_RecResp(Cmd_RouterExeState,0,0);//取值
			
			ExeState_Update();				
			
			b_rst = BLE_CtrlUnit_RecResp(Cmd_RouterExeStateAck,&Chg_ExeState,0);//回复

			if(b_rst == SUCCESSFUL)
			{
				rt_lprintf("[strategy]  (%s) Chg_ExeState Response to BLE, Successful!\n",__func__);
			}
			else
			{
				rt_kprintf("[strategy]  (%s) 上送给BLE路由器工作状态，失败！\n",__func__);
			}
			break;
		}
		//收到蓝牙停机命令
		case Cmd_StopChg:
		{
			CtrlCharge_Event.StopSource = BLE_UNIT;
				
			c_rst = BLE_CtrlUnit_RecResp(Cmd_StopChg,&BLE_Stop,0);//取值			
			rt_lprintf("[strategy]  (%s)  收到@蓝牙@停止充电命令  \n",__func__); 
			memcpy(&CtrlCharge_Event.OrderSn,&BLE_Stop.OrderSn,sizeof(CtrlCharge_Event.OrderSn));	
			memcpy(&CtrlCharge_Event.cAssetNO,&BLE_Stop.cAssetNO,sizeof(CtrlCharge_Event.cAssetNO));	
			CtrlCharge_Event.GunNum = BLE_Stop.GunNum;	
			CtrlCharge_Event.CtrlType = CTRL_STOP;
			
			if(Fault.Total != TRUE)
			{
				if(Ctrl_Stop.GunNum == GUN_SINGLE)		
				{
					p_rst = ChargepileDataGetSet(Cmd_ChargeStop,0);

					if(p_rst == SUCCESSFUL)
					{
						PileIfo.WorkState = ChgSt_Finished;	
						Chg_ExeState.exeState = EXE_END;
						rt_lprintf("[strategy] : APP停机后，收到Chargepile返回\n");
					}					
				}
//				else
//				{
//					Ctrl_Stop.cSucIdle = ORTHERS;
//					c_rst = BLE_CtrlUnit_RecResp(Cmd_StopChgAck,&Ctrl_Stop,0);//回复
//				}
			}
//			else
//			{
//				Ctrl_Stop.cSucIdle = FAILED;
//				c_rst = BLE_CtrlUnit_RecResp(Cmd_StopChgAck,&Ctrl_Stop,0);//回复
//			}
		}
		default:
			break;
	}
	BLE_EventCmd = 0;//清位
}

/********************************************************************  
*	函 数 名: PileData_RecProcess()
*	功能说明: 电桩数据接收处理函数
*	形    参: 无
*	返 回 值: 无
********************************************************************/ 
static void PileData_RecProcess(void)
{
	rt_int8_t c_rst,p_rst;
	rt_uint32_t start_result,stop_result,adjpow_result;
	
	if(startchg_flag == TRUE)
	{
		//启动成功
		if(rt_event_recv(&ChargePileEvent, ChargeStartOK_EVENT,RT_EVENT_FLAG_OR|RT_EVENT_FLAG_CLEAR,100, &start_result) == RT_EOK)	
		{
			rt_timer_stop(StartChgResp);
			
			Ctrl_Start.cSucIdle = SUCCESSFUL;
			c_rst = CtrlUnit_RecResp(Cmd_StartChgAck,&Ctrl_Start,0);
			
			//记录表底值			
			ScmMeter_HisData engMeter_HisData;
			cmMeter_get_data(EMMETER_HISDATA,&engMeter_HisData);
			memcpy(&ChgOrder_Event.StartMeterValue[0],&engMeter_HisData.ulMeter_Day,5*sizeof(long));
			//记录时间
			memcpy(&ChgOrder_Event.ChgStartTime,&System_Time_STR,sizeof(STR_SYSTEM_TIME));
			
			if(c_rst != SUCCESSFUL)
			{
				
			}
			else
			{
				startchg_flag = FALSE;//清位
				rt_lprintf("[strategy] : start charge successful!\n");
			}
		}
		//启动失败
		else if(rt_event_recv(&ChargePileEvent, ChargeStartER_EVENT,RT_EVENT_FLAG_OR|RT_EVENT_FLAG_CLEAR,100, &start_result) == RT_EOK)	
		{
			rt_timer_stop(StartChgResp);
			
			Ctrl_Start.cSucIdle = FAILED;
			p_rst = ChargepileDataGetSet(Cmd_ChargeStartResp,&ChargePilePara_Get);//获取失败原因
			
			if(p_rst != SUCCESSFUL)
			{
				
			}
			else
			{
				startchg_flag = FALSE;//清位
				rt_lprintf("[strategy] : start charge failed,reason:%d!\n",ChargePilePara_Get.StartReson);
			}
			
			c_rst = CtrlUnit_RecResp(Cmd_StartChgAck,&Ctrl_Start,0);		
		}
		rt_lprintf("[strategy] : ChargePileEvent 0x%02X\n", start_result);
	}
	
	if(stopchg_flag == TRUE)
	{
		//停机成功
		if(rt_event_recv(&ChargePileEvent, ChargeStopOK_EVENT,RT_EVENT_FLAG_OR|RT_EVENT_FLAG_CLEAR,100, &stop_result) == RT_EOK)		
		{
			rt_timer_stop(StopChgResp);
			
			Ctrl_Stop.cSucIdle = SUCCESSFUL;
			PileIfo.WorkState = ChgSt_Finished;//状态变更
			c_rst = CtrlUnit_RecResp(Cmd_StopChgAck,&Ctrl_Stop,0);
			
			//记录表底值			
			ScmMeter_HisData engMeter_HisData;
			cmMeter_get_data(EMMETER_HISDATA,&engMeter_HisData);
			memcpy(&ChgOrder_Event.StopMeterValue[0],&engMeter_HisData.ulMeter_Day,5*sizeof(long));
			//记录时间
			memcpy(&ChgOrder_Event.ChgStopTime,&System_Time_STR,sizeof(STR_SYSTEM_TIME));
						
			if(c_rst != SUCCESSFUL)
			{
				
			}
			else
			{
				stopchg_flag = FALSE;//清位
				rt_lprintf("[strategy] : stop charge successful!\n");
			}
		}
		//停机失败
		else if(rt_event_recv(&ChargePileEvent, ChargeStopER_EVENT,RT_EVENT_FLAG_OR|RT_EVENT_FLAG_CLEAR,100, &stop_result) == RT_EOK)		
		{
			rt_timer_stop(StopChgResp);
			
			Ctrl_Start.cSucIdle = FAILED;
			p_rst = ChargepileDataGetSet(Cmd_ChargeStartResp,&ChargePilePara_Get);//获取失败原因
			
			if(p_rst != SUCCESSFUL)
			{
				
			}
			else
			{
				stopchg_flag = FALSE;//清位
				rt_lprintf("[strategy] : stop charge failed,reason:%d!\n",ChargePilePara_Get.StopReson);
			}
			
			c_rst = CtrlUnit_RecResp(Cmd_StopChgAck,&Ctrl_Stop,0);			
		}
		rt_lprintf("[strategy] : ChargePileEvent 0x%02X\n", stop_result);
	}
	
	if(adjpower_flag == TRUE)
	{
		//调整功率成功
		if(rt_event_recv(&ChargePileEvent, SetPowerOK_EVENT,RT_EVENT_FLAG_OR|RT_EVENT_FLAG_CLEAR,100, &adjpow_result) == RT_EOK)	
		{
			rt_timer_stop(PowerAdjResp);
			
			Ctrl_PowerAdj.cSucIdle = SUCCESSFUL;
			c_rst = CtrlUnit_RecResp(Cmd_PowerAdjAck,&Ctrl_PowerAdj,0);
			
			if(c_rst != SUCCESSFUL)
			{
				
			}
			else
			{
				adjpower_flag = FALSE;//清位
				rt_lprintf("[strategy] : start charge successful!\n");
			}
		}
		//调整功率失败
		else if(rt_event_recv(&ChargePileEvent, SetPowerER_EVENT,RT_EVENT_FLAG_OR|RT_EVENT_FLAG_CLEAR,100, &adjpow_result) == RT_EOK)	
		{
			rt_timer_stop(PowerAdjResp);
			
			Ctrl_PowerAdj.cSucIdle = FAILED;
			p_rst = ChargepileDataGetSet(Cmd_SetPowerResp,&ChargePilePara_Get);//获取失败原因
			
			if(p_rst != SUCCESSFUL)
			{
				
			}
			else
			{
				adjpower_flag = FALSE;//清位
				rt_lprintf("[strategy] : adjust power failed,reason:%d!\n",ChargePilePara_Get.AdjPowerReson);
			}
			
			c_rst = CtrlUnit_RecResp(Cmd_PowerAdjAck,&Ctrl_PowerAdj,0);		
		}
		rt_lprintf("[strategy] : ChargePileEvent 0x%02X\n", adjpow_result);
	}
}
/********************************************************************  
*	函 数 名: TimeCalculation
*	功能说明: 时间计算
*	形    参: 无
*	返 回 值: 无
********************************************************************/ 
static time_t TimeCalculation(STR_SYSTEM_TIME TimeStamp)
{
	time_t time_s = 0;
	struct tm timep;
	
	BCD_toInt((unsigned char*)&timep.tm_sec,&TimeStamp.Second,1);
	BCD_toInt((unsigned char*)&timep.tm_min,&TimeStamp.Minute,1);
	BCD_toInt((unsigned char*)&timep.tm_hour,&TimeStamp.Hour,1);
	BCD_toInt((unsigned char*)&timep.tm_mday,&TimeStamp.Day,1);	
	BCD_toInt((unsigned char*)&timep.tm_wday,&TimeStamp.Week,1);
	BCD_toInt((unsigned char*)&timep.tm_mon,&TimeStamp.Month,1);
	BCD_toInt((unsigned char*)&timep.tm_year,&TimeStamp.Year,1);
	time_s = mktime(&timep);
	
	return time_s;
}

/********************************************************************  
*	函 数 名: TimeSolt_PilePowerCtrl()
*	功能说明: 分时段进行电桩功率控制
*	形    参: 无
*	返 回 值: 无
********************************************************************/ 
static void TimeSolt_PilePowerCtrl(void)
{
	rt_uint8_t i;
	rt_int8_t c_rst=0,p_rst=0,s_rst=0,b_rst=0;
	time_t NowTime_s;
	unsigned char PlanStartTime_s[50];
	
	NowTime_s = TimeCalculation(System_Time_STR);//获取当前时间秒数
	//定位当前所属计划单起始时间段
	for(i=PlanSlotCount;i<Chg_Strategy.ucTimeSlotNum;i++)
	{
		if(Chg_Strategy.strChargeTimeSolts[i].strDecStartTime.Day> 0)//校验时间的合法性
		{
			PlanStartTime_s[i] = TimeCalculation(Chg_Strategy.strChargeTimeSolts[i].strDecStartTime);
			
			if(NowTime_s >= PlanStartTime_s[i])//检测到执行到当前时间段	
			{
				Chg_ExeState.ucPlanPower = Chg_Strategy.strChargeTimeSolts[i].ulChargePow;		
				ChargePilePara_Set.PWM_Duty = Chg_ExeState.ucPlanPower*10/132;//功率换算
				
				if(SetPowerFinishFlag[i] == FALSE)//限制发送一次
				{
					if(PlanSlotCount == 0)
						memcpy(&Ctrl_ChgExe_Event.StartTimestamp,&System_Time_STR,sizeof(STR_SYSTEM_TIME));//事件发生时间
					
					p_rst = ChargepileDataGetSet(Cmd_SetPower,&ChargePilePara_Set);//下发充电桩设定功率
					if(p_rst == SUCCESSFUL)
						rt_kprintf("[strategy]  (%s) ChargePilePara_Set,Cmd_SetPower,Successful!\n",__func__);
					else
						rt_kprintf("[strategy]  (%s) 执行充电计划时功率设定，失败！\n",__func__);
					
					Ctrl_ChgExe_Event.OrderNum++;
					memcpy(&Ctrl_ChgExe_Event.Chg_ExeState,&Chg_ExeState,sizeof(CHARGE_EXE_STATE));
					
					c_rst = CtrlUnit_RecResp(Cmd_ChgPlanExeState,&Ctrl_ChgExe_Event,0);//上报充电计划执行事件

					//等待确认
					if(c_rst == SUCCESSFUL)
					{				
						/* 开始启动回复计时 */
						if (ChgPlanExeStateAck != RT_NULL)
						{
							rt_timer_start(ChgPlanExeStateAck);
							ChgPlanExeStateAck_count = 0;
							rt_lprintf("[strategy]  (%s) Ctrl_ChgExe_Event,Cmd_ChgPlanExeState,Successful!\n",__func__);
						}
						else
							rt_lprintf("ChgPlanExeStateAck timer create error\n");					
										
					}
					else
					{
						rt_kprintf("[strategy]  (%s) 充电计划执行事件上报，失败！\n",__func__);					
					}			
					
					
					Chg_ExeState.exeState = EXE_ING;
					SetPowerFinishFlag[i] = TRUE;
					PlanSlotCount = i;
					//count++;
				}
				break;
			}
		}
		else
		{
			rt_kprintf("[strategy]  (%s) 执行充电计划时时间内容异常，失败！\n",__func__);		
		}
	}
	
	if(c_rst !=0 )
		CtrlUnit_RecResp(Cmd_ChgPlanExeState,&Ctrl_ChgExe_Event,0);//若失败再上报一次
		
	if(PlanSlotCount == Chg_Strategy.ucTimeSlotNum)//检测到执行完计划
	{
		memcpy(cRequestNO_Old,cRequestNO_New,sizeof(cRequestNO_Old));//不再给桩发送功率设定帧
		memcpy(&Ctrl_ChgExe_Event.FinishTimestamp,&System_Time_STR,sizeof(STR_SYSTEM_TIME));//执行事件结束时间
		
		c_rst = CtrlUnit_RecResp(Cmd_ChgPlanExeState,&Ctrl_ChgExe_Event,0);//上报充电计划执行事件
		if(c_rst == SUCCESSFUL)
			rt_kprintf("[strategy]  (%s) Ctrl_ChgExe_Event,Cmd_ChgPlanExeState,The last one,Successful!\n",__func__);
		else
			rt_kprintf("[strategy]  (%s) 最后一次上报执行充电计划，失败！\n",__func__);
	}
	
}
/********************************************************************  
*	函 数 名: ChgOrder_Apply()
*	功能说明: 形成充电订单上报
*	形    参: 无
*	返 回 值: 无
********************************************************************/ 
static void ChgOrder_Apply(void)
{
	rt_int8_t c_rst=0,b_rst=0;
	rt_uint8_t i;
	
	if(PileIfo.WorkState == ChgSt_Finished)//检测到充电完成
	{
		memcpy(&ChgOrder_Event.StartTimestamp,&System_Time_STR,sizeof(STR_SYSTEM_TIME));//事件发生时间
		
		ChgOrder_Event.OrderNum++;	
		
		if(CtrlCharge_Event.StartSource == CONTRL_UNIT)//控制器操控
		{
			memcpy(&ChgOrder_Event.cUserID,&Plan_Offer_Event.Chg_Strategy.cUserID,sizeof(Chg_Strategy.cUserID));
			memcpy(&ChgOrder_Event.RequestNO,&Plan_Offer_Event.Chg_Strategy.cRequestNO,sizeof(ChgOrder_Event.RequestNO));
			memcpy(&ChgOrder_Event.AssetNO,&Plan_Offer_Event.Chg_Strategy.cAssetNO,sizeof(ChgOrder_Event.AssetNO));
			ChgOrder_Event.GunNum = Plan_Offer_Event.Chg_Strategy.GunNum;
			
			ChgOrder_Event.ChargeReqEle = Plan_Offer_Event.Chg_Strategy.ulChargeReqEle;
//			memcpy(&ChgOrder_Event.RequestTimeStamp,&Chg_Apply_Event.RequestTimeStamp,sizeof(STR_SYSTEM_TIME));
			ChgOrder_Event.ChargeMode = Plan_Offer_Event.Chg_Strategy.ucChargeMode;	
		}
		else//蓝牙操控
		{
			memcpy(&ChgOrder_Event.cUserID,&Chg_Apply_Event.UserAccount,sizeof(Chg_Strategy.cUserID));
			memcpy(&ChgOrder_Event.RequestNO,&Chg_Apply_Event.RequestNO,sizeof(ChgOrder_Event.RequestNO));
			memcpy(&ChgOrder_Event.AssetNO,&Chg_Apply_Event.AssetNO,sizeof(ChgOrder_Event.AssetNO));
			ChgOrder_Event.GunNum = Chg_Apply_Event.GunNum;
			
			ChgOrder_Event.ChargeReqEle = Chg_Apply_Event.ChargeReqEle;
			memcpy(&ChgOrder_Event.RequestTimeStamp,&Chg_Apply_Event.RequestTimeStamp,sizeof(STR_SYSTEM_TIME));		
			ChgOrder_Event.ChargeMode = Chg_Apply_Event.ChargeMode;
		}
		
		//计算已充电量
		for(i=0;i<5;i++)
			ChgOrder_Event.ucChargeEle[i] = ChgOrder_Event.StopMeterValue[i] - ChgOrder_Event.StartMeterValue[i]; 
		//计算已充时间
		time_t start_time = 0;
		rt_kprintf("[strategy]  (%s) calculate start_time!\n",__func__);
		start_time = TimeCalculation(ChgOrder_Event.ChgStartTime);
		rt_kprintf("[strategy]  (%s) get start_time!\n",__func__);
		time_t stop_time = 0;
		stop_time = TimeCalculation(ChgOrder_Event.ChgStopTime);
		ChgOrder_Event.ucChargeTime = stop_time - start_time;
		
		memcpy(&ChgOrder_Event.FinishTimestamp,&System_Time_STR,sizeof(STR_SYSTEM_TIME));//事件结束时间
//		s_rst = SetStorageData(Cmd_HistoryRecordWr,&ChgOrder_Event,sizeof(CHG_ORDER_EVENT));
//		if(s_rst == SUCCESSFUL)
//		{
//			rt_kprintf("[strategy]  (%s) Storage ChgOrder_Event, Successful!\n",__func__);
//		}
//		else
//		{
//			rt_kprintf("[strategy]  (%s) 保存充电订单事件，失败！\n",__func__);
//			SetStorageData(Cmd_HistoryRecordWr,&ChgOrder_Event,sizeof(CHG_ORDER_EVENT));//再存一次
//		}

		c_rst = CtrlUnit_RecResp(Cmd_ChgRecord,&ChgOrder_Event,0);//上报充电订单

		//等待确认
		if(c_rst == SUCCESSFUL)
		{				
			/* 开始启动回复计时 */
			if (ChgRecordAck != RT_NULL)
			{
				rt_timer_start(ChgRecordAck);
				ChgRecordAck_count = 0;	
				rt_lprintf("[strategy]  (%s) ChgOrder_Event Apply to Contrllor, Successful!\n",__func__);
			}
			else
				rt_lprintf("ChgRecordAck timer create error\n");					
							
		}
		else
		{
			rt_kprintf("[strategy]  (%s) 充电订单事件上送至Contrllor，失败！\n",__func__);					
		}
		
		b_rst = BLE_CtrlUnit_RecResp(Cmd_ChgRecord,&ChgOrder_Event,0);//同时将事件回传APP
		if(b_rst == SUCCESSFUL)
			rt_kprintf("[strategy]  (%s) ChgOrder_Event Apply to BLE, Successful!\n",__func__);
		else
			rt_kprintf("[strategy]  (%s) 充电订单事件上送至BLE，失败！\n",__func__);
		
		PileIfo.WorkState = ChgSt_Standby;//状态变更
	}
}
/********************************************************************  
*	函 数 名: RtState_Judge()
*	功能说明: 路由器和充电桩状态判断
*	形    参: 无
*	返 回 值: 故障代码
********************************************************************/ 
static void RtState_Judge(void)
{
	if(PileIfo.WorkState != ChgSt_Finished)
	{
		if(ChargePilePara_Get.ChgPileState == PILE_STANDBY)
			PileIfo.WorkState = ChgSt_Standby;
		else if(ChargePilePara_Get.ChgPileState == PILE_WORKING)
			PileIfo.WorkState = ChgSt_InCharging;
		else if(ChargePilePara_Get.ChgPileState == PILE_FAU)
			PileIfo.WorkState = ChgSt_Fault;
	}
	
	switch(PileIfo.WorkState)
	{
		case ChgSt_Fault:
		{
			Fault.Bit.ChgPile_Fau = TRUE;
			break;
		}
		case ChgSt_InCharging:
		case ChgSt_DisCharging:
		{
			RouterIfo.WorkState = RtSt_CtrlPower;
			break;
		}
		case ChgSt_Finished:
		{
			RouterIfo.WorkState = RtSt_StandbyOK;//充电结束回待机
			break;
		}
		default:
			break;
	}

	
	if(Fault.Total == TRUE)
		RouterIfo.WorkState = RtSt_Fault;	
}

static void strategy_thread_entry(void *parameter)
{
	rt_err_t res,p_rst;

	RouterIfo.WorkState = RtSt_StandbyOK;//待机正常
	
	
	rt_thread_mdelay(3000);
	
//	GetStorageData(Cmd_MeterNumRd,&RouterIfo,sizeof(RouterIfo));
//	rt_thread_mdelay(100);
	
	rt_pin_mode(RELAYA_PIN, PIN_MODE_OUTPUT);
	rt_pin_mode(RELAYB_PIN, PIN_MODE_OUTPUT);
	RELAY_ON();//上电吸合继电器
	while (1)
	{
		RtState_Judge();
		ChgOrder_Apply();
		CtrlData_RecProcess();
		PileData_RecProcess();	
		
		if(memcmp(cRequestNO_Old,cRequestNO_New,sizeof(cRequestNO_Old)) != 0)
		{
			TimeSolt_PilePowerCtrl(); 
		}
		
		if(rt_pin_read(KC3_PIN) == PIN_LOW)
			p_rst = ChargepileDataGetSet(Cmd_ChargeStart,0);
//			strategy_event_send(StartChg_EVENT);
		
		if(rt_pin_read(KC2_PIN) == PIN_LOW)
			p_rst = ChargepileDataGetSet(Cmd_ChargeStop,0);
						
		rt_thread_mdelay(1000);
	}
}

int strategy_thread_init(void)
{
	rt_err_t res;
	
	/*初始化变量*/
	RouterIfo.WorkState = RtSt_Starting;//开机中
	Chg_ExeState.exeState = EXE_NULL;
	memset(&Plan_Offer_Event,0,sizeof(PLAN_OFFER_EVENT));
	memset(&Chg_Apply_Event,0,sizeof(CHARGE_APPLY_EVENT));
	CtrlCharge_Event.CtrlType = CTRL_NULL;
	
	/* 初始化定时器 */
    timer_create_init();
	
	res=rt_thread_init(&strategy,
									"strategy",
									strategy_thread_entry,
									RT_NULL,
									strategy_stack,
									THREAD_STRATEGY_STACK_SIZE,
									THREAD_STRATEGY_PRIORITY,
									THREAD_STRATEGY_TIMESLICE);
	if (res == RT_EOK) 
	{	
		rt_kprintf("[strategy] : Strategy Initialized!\n");
		rt_thread_startup(&strategy);
	}
	return res;
}


#if defined (RT_STRATEGY_AUTORUN) && defined(RT_USING_COMPONENTS_INIT)
	INIT_APP_EXPORT(strategy_thread_init);
#endif
MSH_CMD_EXPORT(strategy_thread_init, strategy thread run);

void REALY_ON(int argc, char**argv)
{
	RELAY_ON();
}
MSH_CMD_EXPORT(REALY_ON, AC out CMD);

void REALY_OFF(int argc, char**argv)
{
	RELAY_OFF();
}
MSH_CMD_EXPORT(REALY_OFF, AC out  CMD);

