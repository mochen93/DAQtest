// daq_std_single.cpp : Defines the entry point for the console application.
//
/********************************************************************
	created:	2016/03/23
	created:	23:3:2016   16:36
	filename: 	D:\SVN\QtexV100\Trunk\Key\06.sw\code\VSP\daq_std_single\daq_std_single\daq_std_single.cpp
	file path:	D:\SVN\QtexV100\Trunk\Key\06.sw\code\VSP\daq_std_single\daq_std_single
	file base:	daq_std_single
	file ext:	cpp
	author:		BaiLi
	
	purpose:	Example for all QT11XX serial acquisition cards.
            Show DAQ using standard single mode(Single trigger event).
*********************************************************************/

//-----include all header files
#include "../../../Common/QTfmCommon.h"
#include "../../../Common/QTDevice.h"
#include "../../../Common/QTApi.h"
#include "../../../Common/QT_IMPORT.h"

/************************************************************************/
/* QT_StdSingle                                                         */
/************************************************************************/
int QT_StdSingle (ST_CARDINFO *pstCardInfo,unsigned int unBoardIndex)
{
  //////////////////////////////////////////////////////////////////////////
  //----Acquisition parameters
  QTFM_ABA_MODE ABAMode            = ABA_NORMAL_MODE;
  unsigned int ABADivider          = 48;//In step of 16
  unsigned int NoChnInUse          = 1;//Number of active channel in use
  unsigned int SegmentLen          = 8<<20;//number of samples per segment
  unsigned int DmaGranularityB     = 128<<10;//PCIe DMA granularity to get data. It shouldn't be greater than EACH_DMA_MAX_LEN
  unsigned int PreTriggerLen       = 0;//pre-trigger length in samples
  unsigned int SegmentCnt          = 1;//should be 0.
  unsigned int uncompressMode      = 0;//bit[0]=1:使能数据压缩模块,0:禁止数据压缩功能
  //bit[1]=1:使能数据解压缩功能，0：禁止数据解压缩功能
  QTFM_COMMON_TRIGGER_MODE TrigMode = QTFM_COMMON_TRIGGER_MODE_EDGE;//QTFM_COMMON_TRIGGER_MODE_LEVEL
  unsigned int TimeStampEn         = 0;//Enable/Disable Timestamp mode. The following list valid values:
  //       0x0: Disable timestamp mode. Timestamp counter keep no change and values is param2 and param3 are ignored.
  //       0x3: Enable GPS mode, internal clock is used to counting.Accumulative error could impact on precision of timestamp along the time.
  //       0x13: Enable GPS mode, external 1pps clock is required to counting. This is a more accurate way vs. 0x3 mode.
  unsigned int TimeStampSegmentLen = 0;
  unsigned int TimeStampSel        = 0x0;//时间戳数据选择。0：选择GPS时间戳数据；1：选择用户时间戳数据
  unsigned int GpsScond            = 0;
  int         SaveFile            = 1;
  QTFM_INPUT_RANGE   CurrInputRange = QTFM_INPUT_RANGE_1;//+full-scale in mV
  double              OffSet         = 0;//[-fullscale,+fullscale] in uV

  //////////////////////////////////////////////////////////////////////////
  //----Clock parameters
  unsigned int Fref                      = 100000000;//Reference clock frequency. 
  QTFM_COMMON_CLOCK_REFERENCE RefClkMode = QTFM_COMMON_CLOCK_REF_MODE_2;//Change to QTFM_COMMON_CLOCK_REF_MODE_1 if external reference clock is required.
  QTFM_COMMON_ADC_CLOCK ADCClkMode       = QTFM_COMMON_ADC_CLOCK_MODE_0;//Change to QTFM_COMMON_ADC_CLOCK_MODE_1 if external sampling clock is required.

  //////////////////////////////////////////////////////////////////////////
  //Internal variables
  int nRet = -1;//Default return value
  char szFilePathName[MAX_PATH]/* = {0}*/;
  unsigned int ClockDivider = 0;
  unsigned int tmp;
  unsigned int Memsize = 0;
  unsigned char *pszBuf = NULL;
  long long RecvDataLen=0;
  long long AvailableData=0;//No. of available sample data bytes in buffer
  int BufOverflow=-1;//buffer overflow flag
  unsigned int NoBytesOverwritten=0;//No. of overwritten sample data bytes
  FILE *fp = NULL;
  errno_t err;
  //////////////////////////////////////////////////////////////////////////
  //----Open card
  CHECK_RETURN_VALUE (QTOpenBoard (pstCardInfo,unBoardIndex));
  CHECK_RETURN_VALUE (QTResetBoard (pstCardInfo));
  //////////////////////////////////////////////////////////////////////////
  //Assign value to card structure member
  pstCardInfo->dib.MaxEachDmaLenB  = (DmaGranularityB>EACH_DMA_MAX_LEN)?EACH_DMA_MAX_LEN:DmaGranularityB;
  pstCardInfo->dib.MaxEachDmaLenB  = pstCardInfo->dib.MaxEachDmaLenB - pstCardInfo->dib.MaxEachDmaLenB%128;//Align with 128 bytes
  //Take max sample rate as default. Users feel free to change it by uncomment this line then assign new value.
  //pstCardInfo->ClockInfo.SRate = 250000000;//Sample rate in Hz
  //////////////////////////////////////////////////////////////////////////
  //----Setup clock
  CHECK_RETURN_VALUE (QTClockSet (pstCardInfo,Fref,1,(pstCardInfo->ClockInfo.SRate),QTFM_COMMON_CLOCK_VCO_MODE_0,RefClkMode,ADCClkMode,1));
  //----Setup AFE
  if (pstCardInfo->ProdInfo.product_number!=0x1125)
  {
    CHECK_RETURN_VALUE (QTAdcModeSet (pstCardInfo,0,(pstCardInfo->stAI.bForceIOdelay)?0:(1<<8), 0));
  }
  //Analog input enable. Run the command multiple times for other channels
  CHECK_RETURN_VALUE (QTInputChannelSet (pstCardInfo,CHANNEL_0,0,0,0,0,1));//Channel 0
  //Select data format between offset binary code and two's complement
  CHECK_RETURN_VALUE (QTDataFormatSet(pstCardInfo,1));//offset binary or two's complement
  //----Setup GPIO
  CHECK_RETURN_VALUE (QTGPIOSet(pstCardInfo,0,0,0));//input and disable digital data acquisition by default
  CHECK_RETURN_VALUE (QTGPIODelaySet(pstCardInfo,-1,4));
  CHECK_RETURN_VALUE (QTGPIORead(pstCardInfo,0,&tmp));
  CHECK_RETURN_VALUE (QTDigInLatency(pstCardInfo,35));//Align digital input with analog input.
  //----Setup Input range and offset
  if (pstCardInfo->ProdInfo.couple_type==0xDC)
  {
    //----Set analog input range first then offset
    CHECK_RETURN_VALUE (QTChannelRangeSet(pstCardInfo,-1,CurrInputRange));
    //----Set analog offset
    CHECK_RETURN_VALUE (QTChannelOffsetSet(pstCardInfo,-1,OffSet));
  }
  //----Setup work mode, acquisition parameters
  CHECK_RETURN_VALUE (QTABAModeSet(pstCardInfo,ABAMode,ABADivider,NoChnInUse));
  CHECK_RETURN_VALUE (QTWorkModeSet (pstCardInfo,SegmentLen,PreTriggerLen,SegmentCnt,QTFM_COMMON_BOARD_WORK_MODE_STD_SIGNLE|TrigMode,uncompressMode,0));
  CHECK_RETURN_VALUE (QTTimestampSet (pstCardInfo,TimeStampEn,TimeStampSegmentLen,GpsScond,(pstCardInfo->ClockInfo.SRate),TimeStampSel));
  //----Setup trigger source and types
  CHECK_RETURN_VALUE (QTSoftTriggerSet (pstCardInfo,QTFM_COMMON_TRIGGER_TYPE_RISING_EDGE,1));
  //----Calculate the amount of data and allocate buffer
  Memsize = SegmentLen*SegmentCnt*2;
  pszBuf = (unsigned char*) malloc(sizeof(char)*Memsize);
  if (!pszBuf)
  {
    printf_s("Buffer allocate error\n");
    return nRet;
  }
  //////////////////////////////////////////////////////////////////////////
  //Open file to save data
  sprintf_s(szFilePathName,sizeof(szFilePathName),"std_single_card%d_%u.bin",unBoardIndex,pstCardInfo->ClockInfo.SRate);
  err = fopen_s(&fp,szFilePathName,"wb");
  if (0 == err)
    printf("open file successfully\n");
  else
  {
    printf(" file open failed\n");
    return -1;
  }

  //----Get PCIe DMA ready waiting for trigger events
  QTStart (pstCardInfo,QTFM_COMMON_TRANSMIT_DIRECTION_BRD2PC,1,0xffffffff,0);
  //Dump register value
  //CHECK_RETURN_VALUE(QTDumpRegister(pstCardInfo,0x7ae00000,32));
  //----Receive sample data to buffer
  while(RecvDataLen<Memsize)
  {
    QTBufStatusGet(pstCardInfo,&AvailableData,&BufOverflow,&NoBytesOverwritten);
    if (BufOverflow||NoBytesOverwritten) break;
    RecvDataLen += NoBytesOverwritten;
    if (AvailableData<pstCardInfo->dib.MaxEachDmaLenB) continue;
    QTBufSDataGet(pstCardInfo,&pszBuf[RecvDataLen],(unsigned int)pstCardInfo->dib.MaxEachDmaLenB);//Get full size of data once data is ready
    RecvDataLen += pstCardInfo->dib.MaxEachDmaLenB;
    printf_s("RecvDataLen %d(KB)\n",RecvDataLen>>10);
  }
  printf("Acquisition done...\n");
  if (BufOverflow)
  {
    printf("Samples data were lost! Either speed up buffer read rate or reduce sample rate\n");
  }
  if (NoBytesOverwritten)
  {
    printf("Samples data could be corrupted by overwritten!Either speed up buffer read rate or reduce sample rate\n");
  }


  //----close the file
  if (SaveFile)
    fwrite(pszBuf,sizeof(UCHAR),Memsize,fp);
  free(pszBuf);
  pszBuf = NULL;

  //----Stop acquisition and close card handle
  QTStart (pstCardInfo,QTFM_COMMON_TRANSMIT_DIRECTION_BRD2PC,0,0xffffffff,0);
  CHECK_RETURN_VALUE (QTCloseBoard (pstCardInfo));
  nRet = RES_SUCCESS;
  fclose(fp);
  printf("close file successfully\n");
  return nRet;
}

/************************************************************************/
/* main                                                                */
/************************************************************************/
int main(void)
{
  unsigned int i=0;
  int nRet = -1;
  unsigned int NumOfBoard=1;//Specify the number of cards need to operate on.
  ST_CARDINFO stCardInfo[_QTFIRMWARE_NUM_OF_CARD];//card info for per each card on PC mainboard
  do
  {
    //Control multiple cards in sequence
    unsigned int loop = 0;
    while (1)
    {
      loop++;
      if (loop>100) break;
      printf_s("loop=%u\n",loop);
      for (i=0;i<NumOfBoard;i++)
      {
        CHECK_RETURN_VALUE(QT_StdSingle (&stCardInfo[i],i));
      }
    }
  }while(0);

	return nRet;
}

