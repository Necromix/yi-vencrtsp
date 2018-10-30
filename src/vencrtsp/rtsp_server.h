#ifndef RTSP_SERVER_H
#define RTSP_SERVER_H 

#if !defined(WIN32)
	#define __PACKED__        __attribute__ ((__packed__))
#else
	#define __PACKED__ 
#endif

typedef enum
{
	RTSP_VIDEO=0,
	RTSP_VIDEOSUB=1,
	RTSP_AUDIO=2,
	RTSP_YUV422=3,
	RTSP_RGB=4,
	RTSP_VIDEOPS=5,
	RTSP_VIDEOSUBPS=6
}enRTSP_MonBlockType;

struct _RTP_FIXED_HEADER
{
    /**//* byte 0 */
    unsigned char csrc_len:4;        /**//* expect 0 */
    unsigned char extension:1;        /**//* expect 1, see RTP_OP below */
    unsigned char padding:1;        /**//* expect 0 */
    unsigned char version:2;        /**//* expect 2 */
    /**//* byte 1 */
    unsigned char payload:7;        /**//* RTP_PAYLOAD_RTSP */
    unsigned char marker:1;        /**//* expect 1 */
    /**//* bytes 2, 3 */
    unsigned short seq_no;            
    /**//* bytes 4-7 */
    unsigned  long timestamp;        
    /**//* bytes 8-11 */
    unsigned long ssrc;            /**//* stream number is used here. */
} __PACKED__;

typedef struct _RTP_FIXED_HEADER RTP_FIXED_HEADER;

struct _NALU_HEADER
{
    //byte 0
	unsigned char TYPE:5;
    	unsigned char NRI:2;
	unsigned char F:1;    
	
}__PACKED__; /**//* 1 BYTES */

typedef struct _NALU_HEADER NALU_HEADER;

struct _FU_INDICATOR
{
    	//byte 0
    	unsigned char TYPE:5;
	unsigned char NRI:2; 
	unsigned char F:1;    
	
}__PACKED__; /**//* 1 BYTES */

typedef struct _FU_INDICATOR FU_INDICATOR;

struct _FU_HEADER
{
   	 //byte 0
    	unsigned char TYPE:5;
	unsigned char R:1;
	unsigned char E:1;
	unsigned char S:1;    
} __PACKED__; /**//* 1 BYTES */
typedef struct _FU_HEADER FU_HEADER;

struct _AU_HEADER
{
    //byte 0, 1
    unsigned short au_len;
    //byte 2,3
    unsigned  short frm_len:13;  
    unsigned char au_index:3;
} __PACKED__; /**//* 1 BYTES */
typedef struct _AU_HEADER AU_HEADER;


void InitRtspServer();
int AddFrameToRtspBuf(int nChanNum,enRTSP_MonBlockType eType, char * pData, unsigned int  nSize, unsigned int  nVidFrmNum,int bIFrm);





#endif
