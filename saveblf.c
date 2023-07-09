#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include "zcan.h"
#include "binlog.h"

#define msleep(ms)  usleep((ms)*1000)
#define min(a,b)  (((a) < (b)) ? (a) : (b))

#define CANFD_TEST  1

#define MAX_CHANNELS  4
#define CHECK_POINT  200
#define RX_WAIT_TIME  100
#define RX_BUFF_SIZE  1000

unsigned gDevType = 42;  // 33
unsigned gDevIdx = 0;
unsigned gChMask = 0;
unsigned gTxType = 0;

unsigned s2n(const char *s)
{
    unsigned l = strlen(s);
    unsigned v = 0;
    unsigned h = (l > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'));
    unsigned char c;
    unsigned char t;
    if (!h) return atoi(s);
    if (l > 10) return 0;
    for (s += 2; c = *s; s++) {
        if (c >= 'A' && c <= 'F') c += 32;
        if (c >= '0' && c <= '9') t = c - '0';
        else if (c >= 'a' && c <= 'f') t = c - 'a' + 10;
        else return 0;
        v = (v << 4) | t;
    }
    return v;
}

U8 len_to_dlc(U8 len)
{
    if (len < 8) return len;
    U8 idx = ((len - 8) >> 2) & 0x0f;
    static const U8 tbl[16] = {
        8, 9, 10, 11, 12, 0,
        13, 0, 0, 0,
        14, 0, 0, 0,
        15, 0
    };
    return tbl[idx];
}

U8 dlc_to_len(U8 dlc)
{
    static const U8 tbl[16] = {
        0, 1, 2, 3, 4, 5, 6, 7, 
        8, 12, 16, 20, 24, 32, 48, 64
    };
    return tbl[dlc & 0x0f];
}

#if CANFD_TEST
void generate_frame(U8 chn, ZCAN_FD_MSG *can)
{
    U8 i, dlc = rand() % 16; // random data length��0~64��
    memset(can, 0, sizeof(ZCAN_FD_MSG));
    can->hdr.inf.fmt = 1; // canfd
    can->hdr.inf.brs = 1; // 1M+4M
#else
void generate_frame(U8 chn, ZCAN_20_MSG *can)
{
    U8 i, dlc = rand() % 9; // random data length��0~8��
    memset(can, 0, sizeof(ZCAN_20_MSG));
    can->hdr.inf.fmt = 0; // can2.0
    can->hdr.inf.brs = 0; // 1M+1M
#endif
    can->hdr.inf.txm = gTxType;
    can->hdr.inf.sdf = 0; // data frame
    can->hdr.inf.sef = rand() % 2; // random std/ext frame
    can->hdr.chn = chn;
    can->hdr.len = dlc_to_len(dlc);
    for (i = 0; i < can->hdr.len; i++) {
        can->dat[i] = rand() & 0x7f; // random data
        can->hdr.id ^= can->dat[i]; // id: bit0~6, checksum of data0~N
    }
    can->hdr.id |= (U32)dlc << 7; // id: bit7~bit10 = encoded_dat_len
    if (!can->hdr.inf.sef)
        return;
    can->hdr.id |= can->hdr.id << 11; // id: bit11~bit21 == bit0~bit10
    can->hdr.id |= can->hdr.id << 11; // id: bit22~bit28 == bit0~bit7
}

#if CANFD_TEST
int verify_frame(ZCAN_FD_MSG *can)
#else
int verify_frame(ZCAN_20_MSG *can)
#endif
{
    unsigned i;
    unsigned bcc = 0;
    if (can->hdr.len > 64) return -1; // error: data length
    for (i = 0; i < can->hdr.len; i++)
        bcc ^= can->dat[i];
    if ((can->hdr.id & 0x7f) != (bcc & 0x7f)) return -2; // error: data checksum
    if (((can->hdr.id >> 7) & 0x0f) != len_to_dlc(can->hdr.len)) return -3; // error: data length
    if (!can->hdr.inf.sef) return 1; // std-frame ok
    if (((can->hdr.id >> 11) & 0x7ff) != (can->hdr.id & 0x7ff)) return -4; // error: frame id
    if (((can->hdr.id >> 22) & 0x7f) != (can->hdr.id & 0x7f)) return -5; // error: frame id
    return 1; // ext-frame ok
}

typedef struct {
    unsigned channel; // channel index, 0~3
    unsigned stop; // stop RX-thread
    unsigned total; // total received
    unsigned error; // error(s) detected
} THREAD_CTX;

SYSTEMTIME ToUtcTime(struct  timespec *time){
    struct tm t;
    gmtime_r(&time->tv_sec, &t);
    SYSTEMTIME st;
    st.wYear = (uint16_t)(t.tm_year + 1900);
    st.wMonth = (uint16_t)(t.tm_mon + 1);
    st.wDayOfWeek = (uint16_t)t.tm_wday;
    st.wDay = (uint16_t)t.tm_mday;
    st.wHour = (uint16_t)t.tm_hour;
    st.wMinute = (uint16_t)t.tm_min;
    st.wSecond = (uint16_t)t.tm_sec;
    st.wMilliseconds = (uint16_t)(time->tv_nsec / 1000000);
    return st;
}

SYSTEMTIME GetUtcTime(){
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ToUtcTime(&ts);
}

void* rx_thread_can(void *data)
{
    const char *pFileName = "can.blf";
    BLHANDLE hFile;
    SYSTEMTIME systemtime;

    VBLAppTrigger  appTrigger;
    VBLCANMessage message;

    uint64_t time;

    // open file
    hFile = BLCreateFile(pFileName, GENERIC_WRITE);
    if (BLINVALID_HANDLE_VALUE == hFile) {
        printf("open can blf file fail");
        return NULL;
    }
    // 指定写入文件的应用程序
    BLSetApplication(hFile, BL_APPID_CANCASEXLLOG,1,0,1);

    systemtime = GetUtcTime();
    // 设定blf文件的写入时间
    BLSetMeasurementStartTime(hFile, &systemtime);
    // 设置压缩级别
    BLSetWriteOptions(hFile, 6, 0);

    THREAD_CTX *ctx = (THREAD_CTX *)data;
    ctx->total = 0; // reset counter
    ZCAN_20_MSG buff[RX_BUFF_SIZE]; // buffer
    int cnt; // current received
    int i;

    unsigned check_point = 0;

    while (!ctx->stop && !ctx->error)
    {
        cnt = VCI_Receive(gDevType, gDevIdx, ctx->channel, buff, RX_BUFF_SIZE, RX_WAIT_TIME);
        if (!cnt)
            continue;

        for (i = 0; i < cnt; i++) {
            // initialize all numbers to zero
            memset(&appTrigger, 0, sizeof(VBLAppTrigger));
            memset(&message, 0, sizeof(VBLCANMessage));

            // setup object headers
            appTrigger.mHeader.mBase.mSignature = BL_OBJ_SIGNATURE;
            appTrigger.mHeader.mBase.mHeaderSize = sizeof(appTrigger.mHeader);
            appTrigger.mHeader.mBase.mHeaderVersion = 1;
            appTrigger.mHeader.mBase.mObjectSize = sizeof(VBLAppTrigger);
            appTrigger.mHeader.mBase.mObjectType = BL_OBJ_TYPE_APP_TRIGGER;
            appTrigger.mHeader.mObjectFlags = BL_OBJ_FLAG_TIME_ONE_NANS;

            message.mHeader.mBase.mSignature = BL_OBJ_SIGNATURE;
            message.mHeader.mBase.mHeaderSize = sizeof(message.mHeader);;
            message.mHeader.mBase.mHeaderVersion = 1;
            message.mHeader.mBase.mObjectSize = sizeof(VBLCANMessage);;
            message.mHeader.mBase.mObjectType = BL_OBJ_TYPE_CAN_MESSAGE;
            message.mHeader.mObjectFlags = BL_OBJ_FLAG_TIME_ONE_NANS;

            time = buff[i].hdr.ts;
            // usbcanfd-100u时间戳为百微妙级别，但是blf文件接收纳秒，所以*100000
            // 此处必须先转化为uint64位，再进行乘法，否则会溢出，表现为写入文件的数据前后时间错乱
            time *= 100000;

            //设置头文件
            appTrigger.mHeader.mObjectTimeStamp = time;
            BLWriteObject(hFile, &appTrigger.mHeader.mBase);
            message.mHeader.mObjectTimeStamp = time;

            // 设置CAN数据
            message.mChannel = 1;  // zlg从can1开始，此处只连接了can1
            message.mFlags = CAN_MSG_FLAGS(0, buff[i].hdr.inf.sdf);
            message.mDLC = buff[i].hdr.len;
            message.mID = buff[i].hdr.id;
            memcpy(message.mData, buff[i].dat, message.mDLC);

            BLWriteObject(hFile, &message.mHeader.mBase);

            printf("id=%x, timestamp=%u", buff[i].hdr.id, buff[i].hdr.ts);
            ctx->error = 1;
            break;
        }
        if (ctx->error) break;

        ctx->total += cnt;
        if (ctx->total / CHECK_POINT >= check_point) {
            printf("CAN%d: RX: %d frames received & verified\n", ctx->channel, ctx->total);
            check_point++;
        }
    }

    printf("CAN%d: RX: rx-thread terminated, %d frames received & verified: %s\n",
        ctx->channel, ctx->total, ctx->error ? "error(s) detected" : "no error");

    // 关闭blf文件
    if(!BLCloseHandle(hFile)){
        printf("close file fail");
        return NULL;
    }

    pthread_exit(0);
    return NULL;
}

void* rx_thread_canfd(void *data)
{
    const char *pFileName = "canfd.blf";
    BLHANDLE hFile;
    SYSTEMTIME systemtime;

    VBLAppTrigger  appTrigger;
    VBLCANFDMessage64 fdmessage;

    uint64_t time;

    // open file
    hFile = BLCreateFile(pFileName, GENERIC_WRITE);
    if (BLINVALID_HANDLE_VALUE == hFile) {
        printf("open can blf file fail");
        return NULL;
    }
    // 指定写入文件的应用程序
    BLSetApplication(hFile, BL_APPID_CANCASEXLLOG,1,0,1);

    systemtime = GetUtcTime();
    // 设定blf文件的写入时间
    BLSetMeasurementStartTime(hFile, &systemtime);

    // 设置压缩级别
    BLSetWriteOptions(hFile, 6, 0);

    THREAD_CTX *ctx = (THREAD_CTX *)data;
    ctx->total = 0; // reset counter
    ZCAN_FD_MSG buff[RX_BUFF_SIZE]; // buffer
    int cnt; // current received
    int i;

    unsigned check_point = 0;

    while (!ctx->stop && !ctx->error)
    {
        cnt = VCI_Receive(gDevType, gDevIdx, ctx->channel, buff, RX_BUFF_SIZE, RX_WAIT_TIME);
        if (!cnt)
            continue;

        for (i = 0; i < cnt; i++) {
            // initialize all numbers to zero
            memset(&appTrigger, 0, sizeof(VBLAppTrigger));
            memset(&fdmessage, 0, sizeof(VBLCANFDMessage64));

            // setup object headers
            appTrigger.mHeader.mBase.mSignature = BL_OBJ_SIGNATURE;
            appTrigger.mHeader.mBase.mHeaderSize = sizeof(appTrigger.mHeader);
            appTrigger.mHeader.mBase.mHeaderVersion = 1;
            appTrigger.mHeader.mBase.mObjectSize = sizeof(VBLAppTrigger);
            appTrigger.mHeader.mBase.mObjectType = BL_OBJ_TYPE_APP_TRIGGER;
            appTrigger.mHeader.mObjectFlags = BL_OBJ_FLAG_TIME_ONE_NANS;

            fdmessage.mHeader.mBase.mSignature = BL_OBJ_SIGNATURE;
            fdmessage.mHeader.mBase.mHeaderSize = sizeof(fdmessage.mHeader);;
            fdmessage.mHeader.mBase.mHeaderVersion = 1;
            fdmessage.mHeader.mBase.mObjectSize = sizeof(VBLCANFDMessage64);;
            fdmessage.mHeader.mBase.mObjectType = BL_OBJ_TYPE_CAN_FD_MESSAGE_64;
            fdmessage.mHeader.mObjectFlags = BL_OBJ_FLAG_TIME_ONE_NANS;

            time = buff[i].hdr.ts;
            // usbcanfd-100u时间戳为百微妙级别，但是blf文件接收纳秒，所以*100000
            // 此处必须先转化为uint64位，再进行乘法，否则会溢出，表现为写入文件的数据前后时间错乱
            time *= 100000;

            //设置头文件
            appTrigger.mHeader.mObjectTimeStamp = time;
            BLWriteObject(hFile, &appTrigger.mHeader.mBase);
            fdmessage.mHeader.mObjectTimeStamp = time;

            // 设置CAN数据
            fdmessage.mChannel = 1;  // zlg从can1开始，此处只连接了can1
            fdmessage.mFlags = CAN_FD_MSG_FLAGS(buff[i].hdr.inf.fmt, buff[i].hdr.inf.brs,buff[i].hdr.inf.est);
            fdmessage.mDLC = len_to_dlc(buff[i].hdr.len);
            fdmessage.mID = buff[i].hdr.id;
            fdmessage.mDir = buff[i].hdr.inf.txm;
            fdmessage.mValidDataBytes = buff[i].hdr.len;
            fdmessage.mExtDataOffset = 0;
            memcpy(fdmessage.mData, buff[i].dat, fdmessage.mDLC);

            BLWriteObject(hFile, &fdmessage.mHeader.mBase);

            printf("id=%x, timestamp=%u", buff[i].hdr.id, buff[i].hdr.ts);
            ctx->error = 1;
            break;
        }
        if (ctx->error) break;

        ctx->total += cnt;
        if (ctx->total / CHECK_POINT >= check_point) {
            printf("CAN%d: RX: %d frames received & verified\n", ctx->channel, ctx->total);
            check_point++;
        }
    }

    printf("CAN%d: RX: rx-thread terminated, %d frames received & verified: %s\n",
           ctx->channel, ctx->total, ctx->error ? "error(s) detected" : "no error");

    // 关闭blf文件
    if(!BLCloseHandle(hFile)){
        printf("close file fail");
        return NULL;
    }

    pthread_exit(0);
    return NULL;
}

void* tx_thread(void *data)
{
    THREAD_CTX *ctx = (THREAD_CTX *)data;
    int port = ctx->channel;
    time_t tm1, tm2;
    unsigned tx;
    int j;

    ctx->total = 0; // reset counter

    if ((gChMask & (1 << port)) == 0) {
        pthread_exit(0);
        return NULL;
    }

#if CANFD_TEST
    int msgsz = sizeof(ZCAN_FD_MSG);
    ZCAN_FD_MSG *buff = (ZCAN_FD_MSG *)malloc(msgsz * gTxFrames);
#else
    int msgsz = sizeof(ZCAN_20_MSG);
    ZCAN_20_MSG *buff = (ZCAN_20_MSG *)malloc(msgsz * gTxFrames);
#endif
    if (buff) {
        memset(buff, 0, msgsz * gTxFrames);
        time(&tm1);
        for (tx = 0; !ctx->error && tx < gTxCount; tx++) {
            for (j = 0; j < gTxFrames; j++)
                generate_frame(port, &buff[j]);
#if CANFD_TEST
            if (gTxFrames != VCI_TransmitFD(gDevType, gDevIdx, port, buff, gTxFrames))
#else
            if (gTxFrames != VCI_Transmit(gDevType, gDevIdx, port, buff, gTxFrames))
#endif
            {
                printf("CAN%d TX failed: ID=%08x\n", port, buff->hdr.id);
                ctx->error = 1;
                break;
            }
            ctx->total += gTxFrames;
            if (gTxSleep) msleep(gTxSleep);
        }
        time(&tm2);
        free(buff);
    }
    else ctx->error = -1;

    if (!ctx->error) {
        printf("CAN%d: TX: %d frames sent, %ld seconds elapsed\n",
            port, gTxFrames * gTxCount, tm2 - tm1);
        if (tm2 - tm1)
            printf("CAN%d: TX: %ld frames/second\n", port, gTxFrames * gTxCount / (tm2 - tm1));
    }

    pthread_exit(0);
    return NULL;
}


int main(int argc, char* argv[])
{
    if(!VCI_OpenDevice(gDevType, gDevIdx, 0)){
        printf("VCI_OpenDevice failed\n");
        return 0;
    }
    printf("VCI_OpenDevice succeeded\n");

    // ----- device info --------------------------------------------------

    ZCAN_DEV_INF info;
    memset(&info, 0, sizeof(info));
    VCI_ReadBoardInfo(gDevType, gDevIdx, &info);
    char sn[21];
    char id[41];
    memcpy(sn, info.sn, 20);
    memcpy(id, info.id, 40);
    sn[20] = '\0';
    id[40] = '\0';
    printf("HWV=0x%04x, FWV=0x%04x, DRV=0x%04x, API=0x%04x, IRQ=0x%04x, CHN=0x%02x, SN=%s, ID=%s\n",
           info.hwv, info.fwv, info.drv, info.api, info.irq, info.chn, sn, id);

    // ----- init & start -------------------------------------------------

    ZCAN_INIT init;
    init.clk = 60000000; // clock: 60M
    init.mode = 0;
    init.aset.tseg1 = 46; // 1M
    init.aset.tseg2 = 11;
    init.aset.sjw = 3;
    init.aset.smp = 0;
    init.aset.brp = 1;
    init.dset.tseg1 = 10; // 4M
    init.dset.tseg2 = 2;
    init.dset.sjw = 2;
    init.dset.smp = 0;
    init.dset.brp = 1;


    // 只读取can0的信号，所以之初始化can0
    if (!VCI_InitCAN(gDevType, gDevIdx, 0, &init)) {
        printf("VCI_InitCAN failed\n");
        return 0;
    }
    printf("VCI_InitCAN succeeded\n");

    if (!VCI_StartCAN(gDevType, gDevIdx, 0)) {
        printf("VCI_StartCAN failed\n");
        return 0;
    }
    printf("VCI_StartCAN succeeded\n");


    ZCAN_FD_MSG can;
    time_t tm1, tm2;
    for (i = 0; i < 3; i++) {
        time(&tm1);
        VCI_ReceiveFD(gDevType, gDevIdx, 0, &can, 1, (i + 1) * 1000/*ms*/);
        time(&tm2);
        printf("VCI_Receive returned: time ~= %ld seconds\n", tm2 - tm1);
    }

    // ----- create RX-threads --------------------------------------------

    THREAD_CTX rx_ctx[2];  // can和canfd两个线程接受信号
    pthread_t rx_threads[2];
    rx_ctx[0].channel = 0;
    rx_ctx[0].stop = 0;
    rx_ctx[0].total = 0;
    rx_ctx[0].error = 0;
    pthread_create(&rx_threads[0], NULL, rx_thread_can, &rx_ctx[0]);
    rx_ctx[1].channel = 0;
    rx_ctx[1].stop = 0;
    rx_ctx[1].total = 0;
    rx_ctx[1].error = 0;
    pthread_create(&rx_threads[1], NULL, rx_thread_canfd, &rx_ctx[1]);


    // ----- stop RX -------------------------------------------------
    // 设置终止条件
    for (int i = 0; i < 1000; ++i) {
        sleep(1);
    }
    // 关闭线程
    for (int i = 0; i < 2; ++i) {
        printf("kill receive thread\n");
        rx_ctx[i].stop = 1;
        pthread_join(rx_threads[i], NULL);
    }

    VCI_CloseDevice(gDevType, gDevIdx);
    printf("VCI_CloseDevice\n");
    return 0;
}

