from ctypes import *
import threading
import time
lib = cdll.LoadLibrary("./libCANFDNET.so")
vbl= cdll.LoadLibrary("./libbinlog.so")
import platform
from datetime import datetime



ZCAN_DEVICE_TYPE  = c_uint32
ZCAN_DEVICE_INDEX = c_uint32
ZCAN_Reserved     = c_uint32
ZCAN_CHANNEL      = c_uint32
LEN               = c_uint32
ZCAN_CANFDNET_400U_TCP =ZCAN_DEVICE_TYPE(52)
ZCAN_CANFDNET_200U_TCP =ZCAN_DEVICE_TYPE(48)
ZCAN_CANFDDTU_CASCADE_TCP = ZCAN_DEVICE_TYPE(74)
USBCAN2       =   ZCAN_DEVICE_TYPE(4)
DEVICE_INDEX  =   ZCAN_DEVICE_INDEX(0)
Reserved      =   ZCAN_Reserved(0)
CHANNEL       =   ZCAN_CHANNEL(0)
GENERIC_WRITE =   0x40000000
GENERIC_READ  =   0x80000000
BL_OBJ_FLAG_TIME_TEN_MICS  =0x00000001 #/* 10 micro second timestamp */
BL_OBJ_FLAG_TIME_ONE_NANS  =0x00000002 #/* 1 nano second timestamp */
BL_OBJ_TYPE_CAN_FD_MESSAGE_64 =101
BL_OBJ_TYPE_CAN_MESSAGE       =1

def input_thread():
   input()

class ZCAN_CAN_BOARD_INFO(Structure):
    _fields_ = [("hw_Version",c_ushort),
                ("fw_Version",c_ushort),
                ("dr_Version",c_ushort),
                ("in_Version",c_ushort),
                ("irq_Num",c_ushort),
                ("can_Num",c_ubyte),
                ("str_Serial_Num",c_ubyte*20),
                ("str_hw_Type",c_ubyte*40),
                ("Reserved",c_ubyte*4)]

    def __str__(self):
        return "Hardware Version:%s\nFirmware Version:%s\nDriver Version:%s\nInterface:%s\nInterrupt Number:%s\nCAN_number:%d"%(\
                self.hw_Version,  self.fw_Version,  self.dr_Version,  self.in_Version,  self.irq_Num,  self.can_Num)

    def serial(self):
        serial=''
        for c in self.str_Serial_Num:
            if c>0:
                serial +=chr(c)
            else:
                break
        return serial   
        
    def hw_Type(self):
        hw_Type=''
        for c in self.str_hw_Type:
            if c>0:
                hw_Type +=chr(c)
            else:
                break
        return hw_Type   


#对于CANFDNET，此结构体不生效
class ZCAN_CAN_INIT_CONFIG(Structure):
    _fields_ = [("AccCode",c_int),
                ("AccMask",c_int),
                ("Reserved",c_int),
                ("Filter",c_ubyte),
                ("Timing0",c_ubyte),
                ("Timing1",c_ubyte),
                ("Mode",c_ubyte)]

class ZCAN_CAN_OBJ(Structure):
    _fields_ = [("ID",c_uint32),
                ("TimeStamp",c_uint32),
                ("TimeFlag",c_uint8),
                ("SendType",c_byte),
                ("RemoteFlag",c_byte),
                ("ExternFlag",c_byte),
                ("DataLen",c_byte),
                ("Data",c_ubyte*8),
                ("Reserved",c_ubyte*3)]



class ZCAN_CAN_FRAME(Structure):
    _fields_ = [("can_id",  c_uint, 29),
                ("err",     c_uint, 1),
                ("rtr",     c_uint, 1),
                ("eff",     c_uint, 1), 
                ("can_dlc", c_ubyte),
                ("__pad",   c_ubyte),
                ("__res0",  c_ubyte),
                ("__res1",  c_ubyte),
                ("data",    c_ubyte * 8)]

class ZCAN_CANFD_FRAME(Structure):
    _fields_ = [("can_id", c_uint, 29), 
                ("err",    c_uint, 1),
                ("rtr",    c_uint, 1),
                ("eff",    c_uint, 1), 
                ("len",    c_ubyte),
                ("brs",    c_ubyte, 1),
                ("esi",    c_ubyte, 1),
                ("__res",  c_ubyte, 6),
                ("__res0", c_ubyte),
                ("__res1", c_ubyte),
                ("data",   c_ubyte * 64)]


class ZCANdataFlag(Structure):
    _pack_  =  1
    _fields_= [("frameType",c_uint,2),
               ("txDelay",c_uint,2),
               ("transmitType",c_uint,4),
               ("txEchoRequest",c_uint,1),
               ("txEchoed",c_uint,1),
               ("reserved",c_uint,22),
               ]


#表示 CAN/CANFD 帧结构,目前仅作为 ZCANDataObj 结构的成员使用
class ZCANFDData(Structure):            
    _pack_  =  1
    _fields_= [("timestamp",c_uint64),
               ("flag",ZCANdataFlag),
               ("extraData",c_ubyte*4),
               ("frame",ZCAN_CANFD_FRAME),]



#合并发送/合并接收的帧结构体
class ZCANDataObj(Structure):
    _pack_  =  1   
    _fields_= [("dataType",c_ubyte),
               ("chnl",c_ubyte),
               ("flag",c_ushort),
               ("extraData",c_ubyte*4),
               ("zcanfddata",ZCANFDData),##88个字节
               ("reserved",c_ubyte*4),
               ]

class chn_handle(Structure):
    _pack_  =  1   
    _fields_= [("hchnl",c_uint64*100),
               ]

class Value_(Structure):
    _fields_=[("val",c_uint32)
              ]

class IP_(Structure):
    _fields_=[("ip",c_ubyte*20)]

############################# binlog结构体 #######################################
#####      Time       #####
class LPSYSTEMTIME(Structure):
    _pack_ =1
    _fields_=[("wYear",c_uint16),
              ("wMonth",c_uint16),
              ("wDayOfWeek",c_uint16),
              ("wDay",c_uint16),
              ("wHour",c_uint16),
              ("wMinute",c_uint16),
              ("wSecond",c_uint16),
              ("wMilliseconds",c_uint16),]



class VBLObjectHeaderBase(Structure):
    _pack_ =1
    _fields_=[("mSignature",c_uint32),      #/* signature (BL_OBJ_SIGNATURE) */
              ("mHeaderSize",c_uint16),     #/* sizeof object header */
              ("mHeaderVersion",c_uint16),  #/* header version (1) */
              ("mObjectSize",c_uint32),     #/* object size */
              ("mObjectType",c_uint32),     #/* object type (BL_OBJ_TYPE_XXX) */
              ]



class VBLObjectHeader(Structure):
    _pack_  =1
    _fields_=[("mBase",VBLObjectHeaderBase),    #/* base header object */
              ("mObjectFlags",c_uint32),        #/* object flags */        
              ("mObjectStaticSize",c_uint16),   #/* size of the static part of the object */
              ("mObjectVersion",c_uint16),      #/* object specific version */
              ("mObjectTimeStamp",c_uint64),    #/* object timestamp */
                ]

#####CANFD_Structure  #####

class VBLCANFDExtFrameData(Structure):
    _fields_=[("mBTRExtArb",c_uint32),
              ("mBTRExtData",c_uint32) ]



class VBLCANFDMessage64(Structure):
    _pack_  =1
    _fields_=[("mHeader",VBLObjectHeader),
              ("mChannel",c_ubyte),
              ("mDLC",c_ubyte),
              ("mValidDataBytes",c_ubyte),
              ("mTxCount",c_ubyte),
              ("mID",c_uint32),
              ("mFrameLength",c_uint32),
              ("mFlags",c_uint32),
              ("mBtrCfgArb",c_uint32),
              ("mBtrCfgData",c_uint32),
              ("mTimeOffsetBrsNs",c_uint32),
              ("mTimeOffsetCRCDelNs",c_uint32),
              ("mBitCount",c_uint16),
              ("mDir",c_byte),
              ("mExtDataOffset",c_byte),
              ("mCRC",c_uint32),
              ("mData",c_byte*64),
              ("mExtFrameData",VBLCANFDExtFrameData)]

#####CAN_Structure  #####


class VBLCANMessage(Structure):
    _pack_=1
    _fields_=[("mHeader",VBLObjectHeader),
              ("mChannel",c_int16),
              ("mFlags",c_ubyte),
              ("mDLC",c_ubyte),
              ("mID",c_uint32),
              ("mData",c_ubyte*8),]








##################################################################################

def GetDeviceInf(DeviceType,DeviceIndex):
    try:
        info = ZCAN_CAN_BOARD_INFO()
        ret  = lib.VCI_ReadBoardInfo(DeviceType,DeviceIndex,byref(info))
        return info if ret==1 else None
    except:
        print("Exception on readboardinfo")
        raise


#canfdnet作为服务器，电脑作为客户端
def tcp_start(DEVCIE_TYPE,Device_handle_,chn_num):
    Hchnl=chn_handle()
    init_config  = ZCAN_CAN_INIT_CONFIG() 
    
    memset(byref(init_config),0,sizeof(init_config))

    for i in range(chn_num):
        Hchnl.hchnl[i]=lib.ZCAN_InitCAN(Device_handle_,i,0)
        if Hchnl.hchnl[i] ==0:
            print("InitCAN fail!")
        else:
            print("InitCAN success!")

    #设置合并接收
    SETREF_SET_DATA_RECV_MERGE = 43
    value_v = Value_()
    Value="1"
    ret=lib.ZCAN_SetReference(DEVCIE_TYPE,0,0,SETREF_SET_DATA_RECV_MERGE,c_char_p(Value.encode("utf-8")))
    
    #设置电脑的TCP模式为客户端模式
    CMD_TCP_TYPE =4
    Value="0" 
    lib.ZCAN_SetReference(DEVCIE_TYPE,0,0,CMD_TCP_TYPE,c_char_p(Value.encode("utf-8")))
    
    #设置目标IP
    CMD_DESIP =0
    Value ="192.168.0.179"
    lib.ZCAN_SetReference(DEVCIE_TYPE,0,0,CMD_DESIP,c_char_p(Value.encode("utf-8")))
    
    #设置目标端口
    CMD_DESPORT =1
    value_v.val =8000
    lib.ZCAN_SetReference(DEVCIE_TYPE,0,0,CMD_DESPORT,byref(value_v))
    
    for i in range(chn_num):
        ret=lib.ZCAN_StartCAN(Hchnl.hchnl[i])
        if ret ==0:
            print("StartCAN fail!")
            ret=vbl.BLCloseHandle(bl_handle)
            if ret ==1:
                print("close blf file success")
            else:
                print("close blf file fail")
            exit(0)
        else:
            print("StartCAN success!")

    
    return Hchnl

####### write blf ########


def len2DLC(len_):
    if  len_<=8:
        return len_
    if  len_<=12:
        return 9
    if  len_<=16:
        return 10
    if  len_<=20:
        return 11
    if  len_<=24:
        return 12
    if  len_<=32:
        return 13
    if  len_<=48:
        return 14
    if  len_<=64:
        return 15

    
def setmeasurestarttime(bl_handle):
    now_=datetime.today() 
    weekday_= datetime.weekday(now_) 
    print("start time is   %d : %d , %d second,%d ms"%(now_.hour,now_.minute,now_.second,int(now_.microsecond/1000)))
    systime =LPSYSTEMTIME()
    systime.wYear            =now_.year
    systime.wMonth           =now_.month
    systime.wDayOfWeek       =weekday_
    systime.wDay             =now_.day
    systime.wHour            =now_.hour
    systime.wMinute          =now_.minute
    systime.wSecond          =now_.second
    systime.wMilliseconds    =int(now_.microsecond/1000)   #microsecond是us,需转成ms
    ret = vbl.BLSetMeasurementStartTime(bl_handle, byref(systime))
    return ret


def write_frametoblf(frame,bl_handle):
    canID =frame.zcanfddata.frame.can_id
    if frame.zcanfddata.flag.frameType:  #CANFD
        message =VBLCANFDMessage64()
        memset(byref(message),0,sizeof(message))
        message.mHeader.mBase.mSignature   =0x4A424F4C
        message.mHeader.mBase.mHeaderSize = sizeof(message.mHeader)
        message.mHeader.mBase.mHeaderVersion =1
        message.mHeader.mBase.mObjectSize   =sizeof(message)
        message.mHeader.mBase.mObjectType   =BL_OBJ_TYPE_CAN_FD_MESSAGE_64
        message.mHeader.mObjectFlags  =     BL_OBJ_FLAG_TIME_ONE_NANS
        message.mHeader.mObjectTimeStamp = frame.zcanfddata.timestamp*1000 #ns
        message.mChannel  = frame.chnl+1  #zlgCAN产品从0通道开始数
        message.mFlags = (0x0010 if frame.zcanfddata.frame.rtr  else 0)|(0x1000)|(0x2000 if frame.zcanfddata.frame.brs else   0)|(0x4000 if frame.zcanfddata.frame.esi  else 0)
        message.mID  = (0x80000000 | canID) if frame.zcanfddata.frame.eff else canID
        message.mValidDataBytes =frame.zcanfddata.frame.len
        message.mDir  =frame.zcanfddata.flag.txEchoed
        message.mExtDataOffset =0
        message.mDLC =len2DLC(frame.zcanfddata.frame.len)
        for i in  range(message.mValidDataBytes):
            message.mData[i] = frame.zcanfddata.frame.data[i]
        vbl.BLWriteObject(bl_handle , byref(message.mHeader.mBase))
    
    else:  #can
        message =VBLCANMessage()
        memset(byref(message),0,sizeof(message))
        message.mHeader.mBase.mSignature   =0x4A424F4C
        message.mHeader.mBase.mHeaderSize = sizeof(message.mHeader)
        message.mHeader.mBase.mHeaderVersion =1
        message.mHeader.mBase.mObjectSize   =sizeof(message)
        message.mHeader.mBase.mObjectType   =BL_OBJ_TYPE_CAN_MESSAGE
        message.mHeader.mObjectFlags  =     BL_OBJ_FLAG_TIME_ONE_NANS
        message.mHeader.mObjectTimeStamp = frame.zcanfddata.timestamp*1000 #ns
        message.mChannel  = frame.chnl+1
        message.mFlags    = (0x80 if frame.zcanfddata.frame.rtr else 0)|(frame.zcanfddata.flag.txEchoed)
        message.mDLC      = frame.zcanfddata.frame.len
        message.mID       =(0x80000000 | canID) if frame.zcanfddata.frame.eff else canID
        for i in range(message.mDLC):
            message.mData[i]= frame.zcanfddata.frame.data[i]
        ret =vbl.BLWriteObject(bl_handle , byref(message.mHeader.mBase))
        if ret:
            print("write CAN success")
        else:
            print("write can fail")

if __name__=="__main__":

    DEVCIE_TYPE =ZCAN_CANFDNET_400U_TCP
    Device_handle_=lib.ZCAN_OpenDevice(DEVCIE_TYPE,0,0)
    if Device_handle_ ==0:
        print("Opendevice fail!")
    else:
        print("Opendevice success!")  
    
   
    ###createfile
    filename = "001.blf"
    bl_handle= vbl.BLCreateFile(c_char_p(filename.encode("utf-8")),GENERIC_WRITE)
    print("blf cteatfile,handle is: %d, blf file name is %s"%(bl_handle,filename))  
    
    ###set measuretime
    ret=setmeasurestarttime(bl_handle)
    if ret:
        print("set time sucess")
    else:
        print("set time fail ")
    
    chn_num = 4 
    Hchnl = tcp_start(DEVCIE_TYPE,Device_handle_,chn_num)

    thread=threading.Thread(target=input_thread)
    thread.start()

    DataObj = (ZCANDataObj * 100)()
    memset(DataObj,0,sizeof(DataObj))
    for i in range(100):
        DataObj[i].dataType =1   #can/canfd frame
        DataObj[i].chnl=0        #can_channel
        DataObj[i].zcanfddata.flag.frameType=1  #0-can,1-canfd
        DataObj[i].zcanfddata.flag.txDelay=0    #不添加延迟
        DataObj[i].zcanfddata.flag.transmitType=0  #发送方式，0-正常发送，2-自发自收
        DataObj[i].zcanfddata.flag.txEchoRequest=1  #发送回显请求，0-不回显，1-回显
        DataObj[i].zcanfddata.frame.can_id= i
        DataObj[i].zcanfddata.frame.len =8
        DataObj[i].zcanfddata.frame.brs =0   #不加速
        for j in range(DataObj[i].zcanfddata.frame.len):
            DataObj[i].zcanfddata.frame.data[j]=j
    ret =lib.ZCAN_TransmitData(Device_handle_,DataObj,1)


    CAN_merge_type =2
    #Receive Messages
    rcv_msg=(ZCANDataObj*1000)()
    memset(rcv_msg,0,sizeof(rcv_msg))
    
    while True:
     
        rcv_num = lib.ZCAN_ReceiveData(Device_handle_,rcv_msg, 100,0)
        
        if rcv_num:
            for i in range(rcv_num):
                if rcv_msg[i].dataType ==1:
                    print("[%d]:timestamps:%d, chn: %d ,%s， type:%s, id:%s, dlc:%d, eff:%d, rtr:%d, data:%s \n" %(i, rcv_msg[i].zcanfddata.timestamp,
                        rcv_msg[i].chnl,("Tx" if  rcv_msg[i].zcanfddata.flag.txEchoed  else "Rx"),
                        ("canfd" if rcv_msg[i].zcanfddata.flag.frameType else "can"), 
                        hex(rcv_msg[i].zcanfddata.frame.can_id), rcv_msg[i].zcanfddata.frame.len, 
                        rcv_msg[i].zcanfddata.frame.eff, rcv_msg[i].zcanfddata.frame.rtr,
                        ''.join(hex(rcv_msg[i].zcanfddata.frame.data[j])+ ' 'for j in range(rcv_msg[i].zcanfddata.frame.len))))
                    write_frametoblf(rcv_msg[i],bl_handle)
                    
        if rcv_num ==0:
            time.sleep(1) 
        
        if thread.is_alive() == False:
           break

    ret=vbl.BLCloseHandle(bl_handle)
    if ret ==1:
        print("close blf file success")
    else:
        print("close blf file fail")
        
    ret=lib.ZCAN_CloseDevice(Device_handle_)
    if ret ==0:
        print("Closedevice Failed!")
    else:
        print("Closedevice success!")
    del lib
    print('done')
