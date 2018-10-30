#ifdef __cplusplus
#if __cplusplus
extern "C"{
#endif
#endif /* End of #ifdef __cplusplus */

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <netdb.h>
#include <sys/socket.h>

#include <sys/ioctl.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <netinet/if_ether.h>
#include <net/if.h>

#include <linux/if_ether.h>
#include <linux/sockios.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "rtsp_server.h"
#include "sample_comm.h"

#define nalu_sent_len        (14*1024)
//#define nalu_sent_len        1400
#define RTP_H264                    96
#define MAX_CHAN                 2
#define RTP_AUDIO              97
#define MAX_RTSP_CLIENT       4
#define RTSP_SERVER_PORT      554
#define RTSP_RECV_SIZE        1024
#define RTSP_MAX_VID          (640*1024)
#define RTSP_MAX_AUD          (15*1024)

#define AU_HEADER_SIZE    4
#define PARAM_STRING_MAX        100

static SAMPLE_VENC_GETSTREAM_PARA_S gs_stPara;
static pthread_t gs_VencPid;

typedef unsigned short u_int16_t;
typedef unsigned char u_int8_t;
typedef u_int16_t portNumBits;
typedef u_int32_t netAddressBits;
typedef long long _int64;
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE  1
#endif
#define AUDIO_RATE    8000
#define PACKET_BUFFER_END            (unsigned int)0x00000000

#ifdef hi3518ev201
HI_U32 g_u32BlkCnt = 4;
#endif
#ifdef hi3518ev200
HI_U32 g_u32BlkCnt = 2;
#endif
#ifdef hi3516cv200
HI_U32 g_u32BlkCnt = 10;
#endif

typedef enum
{
	RTSP_IDLE = 0,
	RTSP_CONNECTED = 1,
	RTSP_SENDING = 2,
}RTSP_STATUS;

typedef enum
{
	RTP_UDP,
	RTP_TCP,
	RAW_UDP
}StreamingMode;

typedef struct
{
	int index;
	int socket;
	int reqchn;
	int seqnum;
	int seqnum2;
	unsigned int tsvid;
	unsigned int tsaud;
	int status;
	int sessionid;
	int rtpport[2];
	int rtcpport;
	char IP[20];
	char urlPre[PARAM_STRING_MAX];
}RTSP_CLIENT;

//static bool flag = true;
RTP_FIXED_HEADER *rtp_hdr;
NALU_HEADER      *nalu_hdr;
FU_INDICATOR     *fu_ind;
FU_HEADER		 *fu_hdr;
AU_HEADER        *au_hdr;

RTSP_CLIENT g_rtspClients[MAX_RTSP_CLIENT];

int g_nSendDataChn = -1;
pthread_mutex_t g_mutex;
pthread_cond_t  g_cond;
pthread_mutex_t g_sendmutex;

pthread_t g_SendDataThreadId = 0;

VIDEO_NORM_E gs_enNorm = VIDEO_ENCODING_MODE_PAL;//30fps
char g_rtp_playload[20];

int g_nframerate;
int exitok = 0;
int udpfd;

static char const* dateHeader()
{
	static char buf[200];
#if !defined(_WIN32_WCE)
	time_t tt = time(NULL);
	strftime(buf, sizeof buf, "Date: %a, %b %d %Y %H:%M:%S GMT\r\n", gmtime(&tt));
#endif

	return buf;
}

static char* GetLocalIP(int sock)
{
	struct ifreq ifreq;
	struct sockaddr_in *sin;
	char * LocalIP = malloc(20);
	strcpy(ifreq.ifr_name,"wlan0");
	if (!(ioctl (sock, SIOCGIFADDR,&ifreq)))
    	{
		sin = (struct sockaddr_in *)&ifreq.ifr_addr;
		sin->sin_family = AF_INET;
       	strcpy(LocalIP,inet_ntoa(sin->sin_addr));
		//inet_ntop(AF_INET, &sin->sin_addr,LocalIP, 16);
    	}
	printf("--------------------------------------------%s\n",LocalIP);
	return LocalIP;
}

char* strDupSize(char const* str)
{
  if (str == NULL) return NULL;
  size_t len = strlen(str) + 1;
  char* copy = malloc(len);

  return copy;
}

int ParseRequestString(char const* reqStr,
		       unsigned reqStrSize,
		       char* resultCmdName,
		       unsigned resultCmdNameMaxSize,
		       char* resultURLPreSuffix,
		       unsigned resultURLPreSuffixMaxSize,
		       char* resultURLSuffix,
		       unsigned resultURLSuffixMaxSize,
		       char* resultCSeq,
		       unsigned resultCSeqMaxSize)
{
  // This parser is currently rather dumb; it should be made smarter #####

  // Read everything up to the first space as the command name:
  int parseSucceeded = FALSE;
  unsigned i;
  for (i = 0; i < resultCmdNameMaxSize-1 && i < reqStrSize; ++i) {
    char c = reqStr[i];
    if (c == ' ' || c == '\t') {
      parseSucceeded = TRUE;
      break;
    }

    resultCmdName[i] = c;
  }
  resultCmdName[i] = '\0';
  if (!parseSucceeded) return FALSE;

  // Skip over the prefix of any "rtsp://" or "rtsp:/" URL that follows:
  unsigned j = i+1;
  while (j < reqStrSize && (reqStr[j] == ' ' || reqStr[j] == '\t')) ++j; // skip over any additional white space
  for (j = i+1; j < reqStrSize-8; ++j) {
    if ((reqStr[j] == 'r' || reqStr[j] == 'R')
	&& (reqStr[j+1] == 't' || reqStr[j+1] == 'T')
	&& (reqStr[j+2] == 's' || reqStr[j+2] == 'S')
	&& (reqStr[j+3] == 'p' || reqStr[j+3] == 'P')
	&& reqStr[j+4] == ':' && reqStr[j+5] == '/') {
      j += 6;
      if (reqStr[j] == '/') {
	// This is a "rtsp://" URL; skip over the host:port part that follows:
	++j;
	while (j < reqStrSize && reqStr[j] != '/' && reqStr[j] != ' ') ++j;
      } else {
	// This is a "rtsp:/" URL; back up to the "/":
	--j;
      }
      i = j;
      break;
    }
  }

  // Look for the URL suffix (before the following "RTSP/"):
  parseSucceeded = FALSE;
  unsigned k;
  for (k = i+1; k < reqStrSize-5; ++k) {
    if (reqStr[k] == 'R' && reqStr[k+1] == 'T' &&
	reqStr[k+2] == 'S' && reqStr[k+3] == 'P' && reqStr[k+4] == '/') {
      while (--k >= i && reqStr[k] == ' ') {} // go back over all spaces before "RTSP/"
      unsigned k1 = k;
      while (k1 > i && reqStr[k1] != '/' && reqStr[k1] != ' ') --k1;
      // the URL suffix comes from [k1+1,k]

      // Copy "resultURLSuffix":
      if (k - k1 + 1 > resultURLSuffixMaxSize) return FALSE; // there's no room
      unsigned n = 0, k2 = k1+1;
      while (k2 <= k) resultURLSuffix[n++] = reqStr[k2++];
      resultURLSuffix[n] = '\0';

      // Also look for the URL 'pre-suffix' before this:
      unsigned k3 = --k1;
      while (k3 > i && reqStr[k3] != '/' && reqStr[k3] != ' ') --k3;
      // the URL pre-suffix comes from [k3+1,k1]

      // Copy "resultURLPreSuffix":
      if (k1 - k3 + 1 > resultURLPreSuffixMaxSize) return FALSE; // there's no room
      n = 0; k2 = k3+1;
      while (k2 <= k1) resultURLPreSuffix[n++] = reqStr[k2++];
      resultURLPreSuffix[n] = '\0';

      i = k + 7; // to go past " RTSP/"
      parseSucceeded = TRUE;
      break;
    }
  }
  if (!parseSucceeded) return FALSE;

  // Look for "CSeq:", skip whitespace,
  // then read everything up to the next \r or \n as 'CSeq':
  parseSucceeded = FALSE;
  for (j = i; j < reqStrSize-5; ++j) {
    if (reqStr[j] == 'C' && reqStr[j+1] == 'S' && reqStr[j+2] == 'e' &&
	reqStr[j+3] == 'q' && reqStr[j+4] == ':') {
      j += 5;
      unsigned n;
      while (j < reqStrSize && (reqStr[j] ==  ' ' || reqStr[j] == '\t')) ++j;
      for (n = 0; n < resultCSeqMaxSize-1 && j < reqStrSize; ++n,++j) {
	char c = reqStr[j];
	if (c == '\r' || c == '\n') {
	  parseSucceeded = TRUE;
	  break;
	}

	resultCSeq[n] = c;
      }
      resultCSeq[n] = '\0';
      break;
    }
  }
  if (!parseSucceeded) return FALSE;

  return TRUE;
}

int OptionAnswer(char *cseq, int sock)
{
	if (sock != 0)
	{
		char buf[1024];
		memset(buf,0,1024);
		char *pTemp = buf;
		pTemp += sprintf(pTemp,"RTSP/1.0 200 OK\r\nCSeq: %s\r\n%sPublic: %s\r\n\r\n",
			cseq,dateHeader(),"OPTIONS,DESCRIBE,SETUP,PLAY,PAUSE,TEARDOWN");

		int reg = send(sock, buf,strlen(buf),0);
		if(reg <= 0)
		{
			return FALSE;
		}
		else
		{
			printf(">>>>>%s\n",buf);
		}
		return TRUE;
	}
	return FALSE;
}

int DescribeAnswer(char *cseq,int sock,char * urlSuffix,char* recvbuf)
{
	if (sock != 0)
	{
		char sdpMsg[1024];
		char buf[2048];
		memset(buf,0,2048);
		memset(sdpMsg,0,1024);
		char*localip;
		localip = GetLocalIP(sock);

		char *pTemp = buf;
		pTemp += sprintf(pTemp,"RTSP/1.0 200 OK\r\nCSeq: %s\r\n",cseq);
		pTemp += sprintf(pTemp,"%s",dateHeader());
		pTemp += sprintf(pTemp,"Content-Type: application/sdp\r\n");

		//TODO °ÑÒ»Ð©¹Ì¶šÖµžÄÎª¶¯Ì¬Öµ
		char *pTemp2 = sdpMsg;
		pTemp2 += sprintf(pTemp2,"v=0\r\n");
		pTemp2 += sprintf(pTemp2,"o=StreamingServer 3331435948 1116907222000 IN IP4 %s\r\n",localip);
		pTemp2 += sprintf(pTemp2,"s=H.264\r\n");
		pTemp2 += sprintf(pTemp2,"c=IN IP4 0.0.0.0\r\n");
		pTemp2 += sprintf(pTemp2,"t=0 0\r\n");
		pTemp2 += sprintf(pTemp2,"a=control:*\r\n");

		/*H264 TrackID=0 RTP_PT 96*/
		pTemp2 += sprintf(pTemp2,"m=video 0 RTP/AVP 96\r\n");
		pTemp2 += sprintf(pTemp2,"a=control:trackID=0\r\n");
		pTemp2 += sprintf(pTemp2,"a=rtpmap:96 H264/90000\r\n");
		pTemp2 += sprintf(pTemp2,"a=fmtp:96 packetization-mode=1; sprop-parameter-sets=%s\r\n", "AAABBCCC");
#if 1
		/*G726*/
		pTemp2 += sprintf(pTemp2,"m=audio 0 RTP/AVP 97\r\n");
		pTemp2 += sprintf(pTemp2,"a=control:trackID=1\r\n");
		if(strcmp(g_rtp_playload,"AAC")==0)
		{
			pTemp2 += sprintf(pTemp2,"a=rtpmap:97 MPEG4-GENERIC/%d/2\r\n",16000);
			pTemp2 += sprintf(pTemp2,"a=fmtp:97 streamtype=5;profile-level-id=1;mode=AAC-hbr;sizelength=13;indexlength=3;indexdeltalength=3;config=1410\r\n");
		}
		else
		{
			pTemp2 += sprintf(pTemp2,"a=rtpmap:97 G726-32/%d/1\r\n",8000);
			pTemp2 += sprintf(pTemp2,"a=fmtp:97 packetization-mode=1\r\n");
		}
#endif
		pTemp += sprintf(pTemp,"Content-length: %d\r\n", strlen(sdpMsg));
		pTemp += sprintf(pTemp,"Content-Base: rtsp://%s/%s/\r\n\r\n",localip,urlSuffix);

		//printf("mem ready\n");
		strcat(pTemp, sdpMsg);
		free(localip);
		//printf("Describe ready sent\n");
		int re = send(sock, buf, strlen(buf),0);
		if(re <= 0)
		{
			return FALSE;
		}
		else
		{
			printf(">>>>>%s\n",buf);
		}
	}

	return TRUE;
}
void ParseTransportHeader(char const* buf,
						  StreamingMode* streamingMode,
						 char**streamingModeString,
						 char**destinationAddressStr,
						 u_int8_t* destinationTTL,
						 portNumBits* clientRTPPortNum, // if UDP
						 portNumBits* clientRTCPPortNum, // if UDP
						 unsigned char* rtpChannelId, // if TCP
						 unsigned char* rtcpChannelId // if TCP
						 )
 {
	// Initialize the result parameters to default values:
	*streamingMode = RTP_UDP;
	*streamingModeString = NULL;
	*destinationAddressStr = NULL;
	*destinationTTL = 255;
	*clientRTPPortNum = 0;
	*clientRTCPPortNum = 1;
	*rtpChannelId = *rtcpChannelId = 0xFF;

	portNumBits p1, p2;
	unsigned ttl, rtpCid, rtcpCid;

	// First, find "Transport:"
	while (1) {
		if (*buf == '\0') return; // not found
		if (strncasecmp(buf, "Transport: ", 11) == 0) break;
		++buf;
	}

	// Then, run through each of the fields, looking for ones we handle:
	char const* fields = buf + 11;
	char* field = strDupSize(fields);
	while (sscanf(fields, "%[^;]", field) == 1) {
		if (strcmp(field, "RTP/AVP/TCP") == 0) {
			*streamingMode = RTP_TCP;
		} else if (strcmp(field, "RAW/RAW/UDP") == 0 ||
			strcmp(field, "MP2T/H2221/UDP") == 0) {
			*streamingMode = RAW_UDP;
			//*streamingModeString = strDup(field);
		} else if (strncasecmp(field, "destination=", 12) == 0)
		{
			//delete[] destinationAddressStr;
			free(destinationAddressStr);
			//destinationAddressStr = strDup(field+12);
		} else if (sscanf(field, "ttl%u", &ttl) == 1) {
			*destinationTTL = (u_int8_t)ttl;
		} else if (sscanf(field, "client_port=%hu-%hu", &p1, &p2) == 2) {
			*clientRTPPortNum = p1;
			*clientRTCPPortNum = p2;
		} else if (sscanf(field, "client_port=%hu", &p1) == 1) {
			*clientRTPPortNum = p1;
			*clientRTCPPortNum = (*streamingMode == RAW_UDP) ? 0 : p1 + 1;
		} else if (sscanf(field, "interleaved=%u-%u", &rtpCid, &rtcpCid) == 2) {
			*rtpChannelId = (unsigned char)rtpCid;
			*rtcpChannelId = (unsigned char)rtcpCid;
		}

		fields += strlen(field);
		while (*fields == ';') ++fields; // skip over separating ';' chars
		if (*fields == '\0' || *fields == '\r' || *fields == '\n') break;
	}
	free(field);
}


int SetupAnswer(char *cseq,int sock,int SessionId,char * urlSuffix,char* recvbuf,int* rtpport, int* rtcpport)
{
	if (sock != 0)
	{
		char buf[1024];
		memset(buf,0,1024);

		StreamingMode streamingMode;
		char* streamingModeString; // set when RAW_UDP streaming is specified
		char* clientsDestinationAddressStr;
		u_int8_t clientsDestinationTTL;
		portNumBits clientRTPPortNum, clientRTCPPortNum;
		unsigned char rtpChannelId, rtcpChannelId;
		ParseTransportHeader(recvbuf,&streamingMode, &streamingModeString,
			&clientsDestinationAddressStr, &clientsDestinationTTL,
			&clientRTPPortNum, &clientRTCPPortNum,
			&rtpChannelId, &rtcpChannelId);

		//Port clientRTPPort(clientRTPPortNum);
		//Port clientRTCPPort(clientRTCPPortNum);
		*rtpport = clientRTPPortNum;
		*rtcpport = clientRTCPPortNum;

		char *pTemp = buf;
		char*localip;
		localip = GetLocalIP(sock);
		pTemp += sprintf(pTemp,"RTSP/1.0 200 OK\r\nCSeq: %s\r\n%sTransport: RTP/AVP;unicast;destination=%s;client_port=%d-%d;server_port=%d-%d\r\nSession: %d\r\n\r\n",
			cseq,dateHeader(),localip,
			ntohs(htons(clientRTPPortNum)),
			ntohs(htons(clientRTCPPortNum)),
			ntohs(2000),
			ntohs(2001),
			SessionId);

		free(localip);
		int reg = send(sock, buf,strlen(buf),0);
		if(reg <= 0)
		{
			return FALSE;
		}
		else
		{
			printf(">>>>>%s",buf);
		}
		return TRUE;
	}
	return FALSE;
}

int PlayAnswer(char *cseq, int sock,int SessionId,char* urlPre,char* recvbuf)
{
	if (sock != 0)
	{
		char buf[1024];
		memset(buf,0,1024);
		char *pTemp = buf;
		char*localip;
		localip = GetLocalIP(sock);
		pTemp += sprintf(pTemp,"RTSP/1.0 200 OK\r\nCSeq: %s\r\n%sRange: npt=0.000-\r\nSession: %d\r\nRTP-Info: url=rtsp://%s/%s;seq=0\r\n\r\n",
			cseq,dateHeader(),SessionId,localip,urlPre);

		free(localip);

		int reg = send(sock, buf,strlen(buf),0);
		if(reg <= 0)
		{
			return FALSE;
		}
		else
		{
			printf(">>>>>%s",buf);
		}
		return TRUE;
	}
	return FALSE;
}

int PauseAnswer(char *cseq,int sock,char *recvbuf)
{
	if (sock != 0)
	{
		char buf[1024];
		memset(buf,0,1024);
		char *pTemp = buf;
		pTemp += sprintf(pTemp,"RTSP/1.0 200 OK\r\nCSeq: %s\r\n%s\r\n\r\n",
			cseq,dateHeader());

		int reg = send(sock, buf,strlen(buf),0);
		if(reg <= 0)
		{
			return FALSE;
		}
		else
		{
			printf(">>>>>%s",buf);
		}
		return TRUE;
	}
	return FALSE;
}

int TeardownAnswer(char *cseq,int sock,int SessionId,char *recvbuf)
{
	if (sock != 0)
	{
		char buf[1024];
		memset(buf,0,1024);
		char *pTemp = buf;
		pTemp += sprintf(pTemp,"RTSP/1.0 200 OK\r\nCSeq: %s\r\n%sSession: %d\r\n\r\n",
			cseq,dateHeader(),SessionId);

		int reg = send(sock, buf,strlen(buf),0);
		if(reg <= 0)
		{
			return FALSE;
		}
		else
		{
			printf(">>>>>%s",buf);
		}
		return TRUE;
	}
	return FALSE;
}
void * RtspClientMsg(void*pParam)
{
	pthread_detach(pthread_self());
	int nRes;
	char pRecvBuf[RTSP_RECV_SIZE];
	RTSP_CLIENT * pClient = (RTSP_CLIENT*)pParam;
	memset(pRecvBuf,0,sizeof(pRecvBuf));
	printf("RTSP:-----Create Client %s\n",pClient->IP);
	while(pClient->status != RTSP_IDLE)
	{
		nRes = recv(pClient->socket, pRecvBuf, RTSP_RECV_SIZE,0);
		//printf("-------------------%d\n",nRes);
		if(nRes < 1)
		{
			//usleep(1000);
			printf("RTSP:Recv Error--- %d\n",nRes);
            continue;
			g_rtspClients[pClient->index].status = RTSP_IDLE;
			g_rtspClients[pClient->index].seqnum = 0;
			g_rtspClients[pClient->index].tsvid = 0;
			g_rtspClients[pClient->index].tsaud = 0;
			close(pClient->socket);
			break;
		}
		char cmdName[PARAM_STRING_MAX];
		char urlPreSuffix[PARAM_STRING_MAX];
		char urlSuffix[PARAM_STRING_MAX];
		char cseq[PARAM_STRING_MAX];

		ParseRequestString(pRecvBuf,nRes,cmdName,sizeof(cmdName),urlPreSuffix,sizeof(urlPreSuffix),
			urlSuffix,sizeof(urlSuffix),cseq,sizeof(cseq));

		char *p = pRecvBuf;

		printf("<<<<<%s\n",p);

		//printf("\--------------------------\n");
		//printf("%s %s\n",urlPreSuffix,urlSuffix);

		if(strstr(cmdName, "OPTIONS"))
		{
			OptionAnswer(cseq,pClient->socket);
		}
		else if(strstr(cmdName, "DESCRIBE"))
		{
			DescribeAnswer(cseq,pClient->socket,urlSuffix,p);
			//printf("-----------------------------DescribeAnswer %s %s\n",
			//	urlPreSuffix,urlSuffix);
		}
		else if(strstr(cmdName, "SETUP"))
		{
			int rtpport,rtcpport;
			int trackID=0;
			SetupAnswer(cseq,pClient->socket,pClient->sessionid,urlPreSuffix,p,&rtpport,&rtcpport);

			sscanf(urlSuffix, "trackID=%u", &trackID);
			//printf("----------------------------------------------TrackId %d\n",trackID);
			if(trackID<0 || trackID>=2)trackID=0;
			g_rtspClients[pClient->index].rtpport[trackID] = rtpport;
			g_rtspClients[pClient->index].rtcpport= rtcpport;
			g_rtspClients[pClient->index].reqchn = atoi(urlPreSuffix);
			if(strlen(urlPreSuffix)<100)
				strcpy(g_rtspClients[pClient->index].urlPre,urlPreSuffix);
			//printf("-----------------------------SetupAnswer %s-%d-%d\n",
			//	urlPreSuffix,g_rtspClients[pClient->index].reqchn,rtpport);
		}
		else if(strstr(cmdName, "PLAY"))
		{
			PlayAnswer(cseq,pClient->socket,pClient->sessionid,g_rtspClients[pClient->index].urlPre,p);
			g_rtspClients[pClient->index].status = RTSP_SENDING;
			printf("Start Play\n");
			//printf("-----------------------------PlayAnswer %d %d\n",pClient->index);
			//usleep(100);
		}
		else if(strstr(cmdName, "PAUSE"))
		{
			PauseAnswer(cseq,pClient->socket,p);
		}
		else if(strstr(cmdName, "TEARDOWN"))
		{
			TeardownAnswer(cseq,pClient->socket,pClient->sessionid,p);
			g_rtspClients[pClient->index].status = RTSP_IDLE;
			g_rtspClients[pClient->index].seqnum = 0;
			g_rtspClients[pClient->index].tsvid = 0;
			g_rtspClients[pClient->index].tsaud = 0;
			close(pClient->socket);
		}
		if(exitok){ exitok++;return NULL; }
	}
	printf("RTSP:-----Exit Client %s\n",pClient->IP);
	return NULL;
}

void * RtspServerListen(void*pParam)
{
	int s32Socket;
	struct sockaddr_in servaddr;
	int s32CSocket;
    int s32Rtn;
    int s32Socket_opt_value = 1;
	int nAddrLen;
	struct sockaddr_in addrAccept;

	memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(RTSP_SERVER_PORT);

	s32Socket = socket(AF_INET, SOCK_STREAM, 0);

	if (setsockopt(s32Socket ,SOL_SOCKET,SO_REUSEADDR,&s32Socket_opt_value,sizeof(int)) == -1)
    {
        return (void *)(-1);
    }
    s32Rtn = bind(s32Socket, (struct sockaddr *)&servaddr, sizeof(struct sockaddr_in));
    if(s32Rtn < 0)
    {
        return (void *)(-2);
    }

    s32Rtn = listen(s32Socket, 50);   /*50,×îŽóµÄÁ¬œÓÊý*/
    if(s32Rtn < 0)
    {
         return (void *)(-2);
    }


	nAddrLen = sizeof(struct sockaddr_in);
	int nSessionId = 1000;
    while ((s32CSocket = accept(s32Socket, (struct sockaddr*)&addrAccept, &nAddrLen)) >= 0)
    {
		printf("<<<<RTSP Client %s Connected...\n", inet_ntoa(addrAccept.sin_addr));

		int nMaxBuf = 10 * 1024; // ÏµÍ³œ«»á·ÖÅä 2 x nMaxBuf µÄ»º³åŽóÐ¡
		if(setsockopt(s32CSocket, SOL_SOCKET, SO_SNDBUF, (char*)&nMaxBuf, sizeof(nMaxBuf)) == -1)
			printf("RTSP:!!!!!! Enalarge socket sending buffer error !!!!!!\n");
		int i;
		int bAdd=FALSE;
		for(i=0;i<MAX_RTSP_CLIENT;i++)
		{
			if(g_rtspClients[i].status == RTSP_IDLE)
			{
				memset(&g_rtspClients[i],0,sizeof(RTSP_CLIENT));
				g_rtspClients[i].index = i;
				g_rtspClients[i].socket = s32CSocket;
				g_rtspClients[i].status = RTSP_CONNECTED ;//RTSP_SENDING;
				g_rtspClients[i].sessionid = nSessionId++;
				strcpy(g_rtspClients[i].IP,inet_ntoa(addrAccept.sin_addr));
				pthread_t threadIdlsn = 0;

				struct sched_param sched;
				sched.sched_priority = 1;
				//to return ACKecho
				pthread_create(&threadIdlsn, NULL, RtspClientMsg, &g_rtspClients[i]);
				pthread_setschedparam(threadIdlsn,SCHED_RR,&sched);

				bAdd = TRUE;
				break;
			}
		}
		if(bAdd==FALSE)
		{
			memset(&g_rtspClients[0],0,sizeof(RTSP_CLIENT));
			g_rtspClients[0].index = 0;
			g_rtspClients[0].socket = s32CSocket;
			g_rtspClients[0].status = RTSP_CONNECTED ;//RTSP_SENDING;
			g_rtspClients[0].sessionid = nSessionId++;
			strcpy(g_rtspClients[0].IP,inet_ntoa(addrAccept.sin_addr));
			pthread_t threadIdlsn = 0;
			struct sched_param sched;
			sched.sched_priority = 1;
			//to return ACKecho
			pthread_create(&threadIdlsn, NULL, RtspClientMsg, &g_rtspClients[0]);
			pthread_setschedparam(threadIdlsn,SCHED_RR,&sched);
			bAdd = TRUE;
		}
		if(exitok){ exitok++;return NULL; }
    }
    if(s32CSocket < 0)
    {
       // HI_OUT_Printf(0, "RTSP listening on port %d,accept err, %d\n", RTSP_SERVER_PORT, s32CSocket);
    }

	printf("----- INIT_RTSP_Listen() Exit !! \n");

	return NULL;
}

HI_S32 VENC_Sent(char *buffer,int buflen)
{
	int is=0;
	int nChanNum=0;
	for(is=0;is<MAX_RTSP_CLIENT;is++)
	{
		if(g_rtspClients[is].status!=RTSP_SENDING)
		{
		    continue;
		}
		int heart = g_rtspClients[is].seqnum % 1000;

		if(heart==0)
		{
            printf("Heart[%d] %d\n", is, g_rtspClients[is].seqnum);
		}

		char* nalu_payload;
		int nAvFrmLen = 0;
		int nIsIFrm = 0;
		int nNaluType = 0;
		char sendbuf[nalu_sent_len+14];

		nChanNum = g_rtspClients[is].reqchn;
		if(nChanNum<0 || nChanNum>=MAX_CHAN )
		{
			continue;
		}
		nAvFrmLen = buflen;
		//printf("%d\n",nAvFrmLen);
		//nAvFrmLen = vStreamInfo.dwSize ;//Streamlen
		struct sockaddr_in server;
		server.sin_family = AF_INET;
	   	server.sin_port = htons(g_rtspClients[is].rtpport[0]);
	   	server.sin_addr.s_addr = inet_addr(g_rtspClients[is].IP);
		int	bytes = 0;
		unsigned int ts_increase = 0;

        g_nframerate = (VIDEO_ENCODING_MODE_PAL== gs_enNorm)?15:20;
		ts_increase = (unsigned int)(90000.0 / g_nframerate);
        if (heart == 0)
            printf("FRC= %d, ts = %d\n",g_nframerate, ts_increase);
		rtp_hdr = (RTP_FIXED_HEADER*)&sendbuf[0];
		rtp_hdr->payload = RTP_H264;
		rtp_hdr->version = 2;
		rtp_hdr->marker  = 0;
		rtp_hdr->ssrc    = htonl(10);

		if(nAvFrmLen<=nalu_sent_len)
		{
			//printf("a");
			rtp_hdr->marker = 1;
			rtp_hdr->seq_no = htons(g_rtspClients[is].seqnum++); //ÐòÁÐºÅ£¬Ã¿·¢ËÍÒ»¸öRTP°üÔö1
			nalu_hdr = (NALU_HEADER*)&sendbuf[12];
			nalu_hdr->F = 0;
			nalu_hdr->NRI = nIsIFrm;
			nalu_hdr->TYPE = nNaluType;

			nalu_payload = &sendbuf[13];//Í¬Àí½«sendbuf[13]¸³¸ønalu_payload
			memcpy(nalu_payload,buffer,nAvFrmLen);
            g_rtspClients[is].tsvid += ts_increase;

			rtp_hdr->timestamp = htonl(g_rtspClients[is].tsvid);
			bytes = nAvFrmLen + 13 ;
			sendto(udpfd, sendbuf, bytes, 0, (struct sockaddr *)&server,sizeof(server));
		}
		else
		{
			//printf("b");
			int k = 0;
            int l = 0;
			int t = 0;

            k=nAvFrmLen/nalu_sent_len;
			l=nAvFrmLen%nalu_sent_len;

            g_rtspClients[is].tsvid += ts_increase;

			while(t<=k)
			{
				rtp_hdr->seq_no = htons(g_rtspClients[is].seqnum++);
				if(t==0)
				{
					rtp_hdr->marker = 0;
					fu_ind = (FU_INDICATOR*)&sendbuf[12];
					fu_ind->F = 0;
					fu_ind->NRI = nIsIFrm;
					fu_ind->TYPE = 28;

					fu_hdr = (FU_HEADER*)&sendbuf[13];
					fu_hdr->E = 0;
					fu_hdr->R = 0;
					fu_hdr->S = 1;
					fu_hdr->TYPE = nNaluType;

					nalu_payload = &sendbuf[14];
					memcpy(nalu_payload,buffer,nalu_sent_len);

					bytes = nalu_sent_len + 14;
					sendto( udpfd, sendbuf, bytes, 0, (struct sockaddr *)&server,sizeof(server));
					t++;
				}
				else if(k==t)
				{
					rtp_hdr->marker = 1;
					fu_ind = (FU_INDICATOR*)&sendbuf[12];
					fu_ind->F = 0 ;
					fu_ind->NRI = nIsIFrm ;
					fu_ind->TYPE = 28;

                    fu_hdr = (FU_HEADER*)&sendbuf[13];
					fu_hdr->R = 0;
					fu_hdr->S = 0;
					fu_hdr->E = 1;
					fu_hdr->TYPE = nNaluType;

                    nalu_payload = &sendbuf[14];
					memcpy(nalu_payload,buffer+t*nalu_sent_len,l);

                    bytes = l+14;
					sendto(udpfd, sendbuf, bytes, 0, (struct sockaddr *)&server,sizeof(server));
					t++;
				}
				else if(t<k && t!=0)
				{
					rtp_hdr->marker = 0;
					fu_ind = (FU_INDICATOR*)&sendbuf[12];
					fu_ind->F = 0;
					fu_ind->NRI = nIsIFrm;
					fu_ind->TYPE = 28;

                    fu_hdr = (FU_HEADER*)&sendbuf[13];
					fu_hdr->R = 0;
					fu_hdr->S = 0;
					fu_hdr->E = 0;
					fu_hdr->TYPE = nNaluType;

                    nalu_payload = &sendbuf[14];
					memcpy(nalu_payload,buffer+t*nalu_sent_len,nalu_sent_len);

                    bytes = nalu_sent_len+14;
					sendto(udpfd, sendbuf, bytes, 0, (struct sockaddr *)&server,sizeof(server));
					t++;
				}
			}
		}
	}
    return 0;
}

/******************************************************************************
* funciton : sent H264 stream
******************************************************************************/
HI_S32 SAMPLE_COMM_VENC_Sentjin(VENC_STREAM_S *pstStream)
{
    HI_S32 i,flag=0;

    for(i=0;i<MAX_RTSP_CLIENT;i++)//have atleast a connect
    {
        if(g_rtspClients[i].status == RTSP_SENDING)
        {
            flag = 1;
            break;
        }
    }
    if(flag)
    {
        for (i = 0; i < pstStream->u32PackCount; i++)
        {
            HI_S32 lens=0;
            char sendbuf[320*1024];

            lens = pstStream->pstPack[i].u32Len-pstStream->pstPack[i].u32Offset;
            if (lens > 320*1024)
            {
                printf("Lens = %d\n", lens);
            } else {
                memcpy(&sendbuf[0],
                pstStream->pstPack[i].pu8Addr+pstStream->pstPack[i].u32Offset,
                lens);

                VENC_Sent(sendbuf,lens);
            }
        }
    }

    return HI_SUCCESS;
}

/******************************************************************************
 * funciton : get stream from each channels and save them
 ******************************************************************************/
HI_VOID* SAMPLE_COMM_VENC_GetVencStreamProcsent(HI_VOID *p)
{
    pthread_detach(pthread_self());
    printf("RTSP:-----create send thread\n");
    //i=0,720p; i=1,VGA 640*480; i=2, 320*240;
    HI_S32 i=0;//exe ch NO.
    HI_S32 s32ChnTotal;
    SAMPLE_VENC_GETSTREAM_PARA_S *pstPara;
    HI_S32 maxfd = 0;
    struct timeval TimeoutVal;
    fd_set read_fds;
    HI_S32 VencFd[VENC_MAX_CHN_NUM];
    VENC_CHN_STAT_S stStat;
    HI_S32 s32Ret;
    VENC_STREAM_S stStream;

    pstPara = (SAMPLE_VENC_GETSTREAM_PARA_S*)p;
    s32ChnTotal = pstPara->s32Cnt;

    /******************************************
      step 1:  check & prepare save-file & venc-fd
     ******************************************/
    if (s32ChnTotal >= VENC_MAX_CHN_NUM)
    {
        SAMPLE_PRT("input count invaild\n");
        return NULL;
    }

    //for (i = 0; i < s32ChnTotal; i++)
    {
        /* Set Venc Fd. */
        VencFd[i] = HI_MPI_VENC_GetFd(i);
        if (VencFd[i] < 0)
        {
            SAMPLE_PRT("HI_MPI_VENC_GetFd failed with %#x!\n",
                    VencFd[i]);
            return NULL;
        }
        if (maxfd <= VencFd[i])
        {
            maxfd = VencFd[i];
        }
    }



    udpfd = socket(AF_INET,SOCK_DGRAM,0);//UDP
    printf("udp up\n");

    /******************************************
      step 2:  Start to get streams of each channel.
     ******************************************/
    while (HI_TRUE == pstPara->bThreadStart)
    {
        HI_S32 is,flag=0;

        for(is=0;is<MAX_RTSP_CLIENT;is++)//have atleast a connect
        {
            if(g_rtspClients[is].status == RTSP_SENDING)
            {
                flag = 1;
                break;
            }
        }
        if(flag)
        {
            FD_ZERO(&read_fds);
            FD_SET(VencFd[i], &read_fds);

            TimeoutVal.tv_sec  = 2;
            TimeoutVal.tv_usec = 0;
            s32Ret = select(maxfd + 1, &read_fds, NULL, NULL, &TimeoutVal);
            if (s32Ret < 0)
            {
                SAMPLE_PRT("select failed!\n");
                break;
            }
            else if (s32Ret == 0)
            {
                SAMPLE_PRT("get venc stream time out, exit thread\n");
                continue;
            }
            else
            {
                //only ch1 used
                //for (i = 0; i < s32ChnTotal; i++)
                //for (i = 0; i < 1; i++)
                //i = 1;
                {
                    if (FD_ISSET(VencFd[i], &read_fds))
                    {
                        /*******************************************************
                          step 2.1 : query how many packs in one-frame stream.
                         *******************************************************/
                        //printf("query how many packs \n");
                        memset(&stStream, 0, sizeof(stStream));
                        s32Ret = HI_MPI_VENC_Query(i, &stStat);
                        if (HI_SUCCESS != s32Ret)
                        {
                            SAMPLE_PRT("HI_MPI_VENC_Query chn[%d] failed with %#x!\n", i, s32Ret);
                            break;
                        }

                        /*******************************************************
                          step 2.2 :suggest to check both u32CurPacks and u32LeftStreamFrames at the same time,for example:
                          if(0 == stStat.u32CurPacks || 0 == stStat.u32LeftStreamFrames)
                          {
                          SAMPLE_PRT("NOTE: Current  frame is NULL!\n");
                          continue;
                          }
                         *******************************************************/
                        if(0 == stStat.u32CurPacks)
                        {
                            SAMPLE_PRT("NOTE: Current  frame is NULL!\n");
                            continue;
                        }
                        /*******************************************************
                          step 2.3 : malloc corresponding number of pack nodes.
                         *******************************************************/
                        stStream.pstPack = (VENC_PACK_S*)malloc(sizeof(VENC_PACK_S) * stStat.u32CurPacks);
                        if (NULL == stStream.pstPack)
                        {
                            SAMPLE_PRT("malloc stream pack failed!\n");
                            break;
                        }
                        /*******************************************************
                          step 2.3 : malloc corresponding number of pack nodes.
                         *******************************************************/
                        stStream.pstPack = (VENC_PACK_S*)malloc(sizeof(VENC_PACK_S) * stStat.u32CurPacks);
                        if (NULL == stStream.pstPack)
                        {
                            SAMPLE_PRT("malloc stream pack failed!\n");
                            break;
                        }

                        if(exitok)
                        {
                            free(stStream.pstPack);
                            stStream.pstPack = NULL;
                            exitok++;
                            return NULL;
                        }

                        /*******************************************************
                          step 2.4 : call mpi to get one-frame stream
                         *******************************************************/
                        stStream.u32PackCount = stStat.u32CurPacks;
                        s32Ret = HI_MPI_VENC_GetStream(i, &stStream, HI_TRUE);
                        if (HI_SUCCESS != s32Ret)
                        {
                            free(stStream.pstPack);
                            stStream.pstPack = NULL;
                            SAMPLE_PRT("HI_MPI_VENC_GetStream failed with %#x!\n", \
                                    s32Ret);
                            break;
                        }

                        /*******************************************************
                          step 2.5 : save frame to file
                         *******************************************************/
                        #if 0
                        FILE * pFile = fopen("Ludo.h264", "wb");

                        s32Ret = SAMPLE_COMM_VENC_SaveStream(PT_H264, pFile, &stStream);
                        if (HI_SUCCESS != s32Ret)
                        {
                            free(stStream.pstPack);
                            stStream.pstPack = NULL;
                            SAMPLE_PRT("save stream failed!\n");
                            break;
                        }
                        #endif
                        SAMPLE_COMM_VENC_Sentjin(&stStream);

                        /*   pthread_t threadsentId = 0;
                           struct sched_param sentsched;
                           sentsched.sched_priority = 20;
                        //to listen visiting
                        pthread_create(&threadsentId, NULL, SAMPLE_COMM_VENC_Sentjin,(HI_VOID*)&stStream);
                        pthread_setschedparam(threadsentId,SCHED_RR,&sentsched);
                        */

                        /*******************************************************
                          stVep 2.6 : release stream
                         *******************************************************/
                        s32Ret = HI_MPI_VENC_ReleaseStream(i, &stStream);
                        if (HI_SUCCESS != s32Ret)
                        {
                            free(stStream.pstPack);
                            stStream.pstPack = NULL;
                            break;
                        }
                        /*******************************************************
                          step 2.7 : free pack nodes
                         *******************************************************/
                        free(stStream.pstPack);
                        stStream.pstPack = NULL;

                        if(exitok){ exitok++;return NULL; }

                    }
                }
            }
        }
        usleep(100);
    }

    return NULL;
}

/******************************************************************************
* function :  H.264@1080p@30fps+H.264@VGA@30fps


******************************************************************************/
HI_S32 SAMPLE_VENC_1080P_CLASSIC(HI_VOID)
{
    PAYLOAD_TYPE_E enPayLoad= PT_H264;
    PIC_SIZE_E enSize = PIC_HD1080;
	HI_U32 u32Profile = 0;

    VB_CONF_S stVbConf;
    SAMPLE_VI_CONFIG_S stViConfig = {0};

    VPSS_GRP VpssGrp;
    VPSS_CHN VpssChn;
    VPSS_GRP_ATTR_S stVpssGrpAttr;
    VPSS_CHN_ATTR_S stVpssChnAttr;
    VPSS_CHN_MODE_S stVpssChnMode;

    VENC_CHN VencChn;
    SAMPLE_RC_E enRcMode= SAMPLE_RC_CBR;
    HI_S32 s32ChnNum=1;

    HI_S32 s32Ret = HI_SUCCESS;
    HI_U32 u32BlkSize;
    SIZE_S stSize;


    /******************************************
     step  1: init sys variable
    ******************************************/
    memset(&stVbConf,0,sizeof(VB_CONF_S));

    stVbConf.u32MaxPoolCnt = 128;
	SAMPLE_COMM_VI_GetSizeBySensor(&enSize);

    /*video buffer*/
	u32BlkSize = SAMPLE_COMM_SYS_CalcPicVbBlkSize(gs_enNorm,\
	            enSize, SAMPLE_PIXEL_FORMAT, SAMPLE_SYS_ALIGN_WIDTH);

    stVbConf.astCommPool[0].u32BlkSize = u32BlkSize;
	stVbConf.astCommPool[0].u32BlkCnt = g_u32BlkCnt;


    /******************************************
     step 2: mpp system init.
    ******************************************/
    s32Ret = SAMPLE_COMM_SYS_Init(&stVbConf);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("system init failed with %d!\n", s32Ret);
        goto END_VENC_1080P_CLASSIC_0;
    }

    /******************************************
     step 3: start vi dev & chn to capture
    ******************************************/
    stViConfig.enViMode   = SENSOR_TYPE;
    stViConfig.enRotate   = ROTATE_NONE;
    stViConfig.enNorm     = VIDEO_ENCODING_MODE_AUTO;
    stViConfig.enViChnSet = VI_CHN_SET_NORMAL;
    stViConfig.enWDRMode  = WDR_MODE_NONE;
    s32Ret = SAMPLE_COMM_VI_StartVi(&stViConfig);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("start vi failed!\n");
        goto END_VENC_1080P_CLASSIC_1;
    }

    /******************************************
     step 4: start vpss and vi bind vpss
    ******************************************/
    s32Ret = SAMPLE_COMM_SYS_GetPicSize(gs_enNorm, enSize, &stSize);
    if (HI_SUCCESS != s32Ret)
    {
        SAMPLE_PRT("SAMPLE_COMM_SYS_GetPicSize failed!\n");
        goto END_VENC_1080P_CLASSIC_1;
    }

    VpssGrp = 0;
	stVpssGrpAttr.u32MaxW = stSize.u32Width;
	stVpssGrpAttr.u32MaxH = stSize.u32Height;
	stVpssGrpAttr.bIeEn = HI_FALSE;
	stVpssGrpAttr.bNrEn = HI_TRUE;
	stVpssGrpAttr.bHistEn = HI_FALSE;
	stVpssGrpAttr.bDciEn = HI_FALSE;
	stVpssGrpAttr.enDieMode = VPSS_DIE_MODE_NODIE;
	stVpssGrpAttr.enPixFmt = SAMPLE_PIXEL_FORMAT;

	s32Ret = SAMPLE_COMM_VPSS_StartGroup(VpssGrp, &stVpssGrpAttr);
	if (HI_SUCCESS != s32Ret)
	{
	    SAMPLE_PRT("Start Vpss failed!\n");
	    goto END_VENC_1080P_CLASSIC_2;
	}

	s32Ret = SAMPLE_COMM_VI_BindVpss(stViConfig.enViMode);
	if (HI_SUCCESS != s32Ret)
	{
	    SAMPLE_PRT("Vi bind Vpss failed!\n");
	    goto END_VENC_1080P_CLASSIC_3;
	}

	VpssChn = 0;
	stVpssChnMode.enChnMode      = VPSS_CHN_MODE_USER;
	stVpssChnMode.bDouble        = HI_FALSE;
	stVpssChnMode.enPixelFormat  = SAMPLE_PIXEL_FORMAT;
	stVpssChnMode.u32Width       = stSize.u32Width;
	stVpssChnMode.u32Height      = stSize.u32Height;
	stVpssChnMode.enCompressMode = COMPRESS_MODE_SEG;
	memset(&stVpssChnAttr, 0, sizeof(stVpssChnAttr));
	stVpssChnAttr.s32SrcFrameRate = -1;
	stVpssChnAttr.s32DstFrameRate = -1;
	s32Ret = SAMPLE_COMM_VPSS_EnableChn(VpssGrp, VpssChn, &stVpssChnAttr, &stVpssChnMode, HI_NULL);
	if (HI_SUCCESS != s32Ret)
	{
	    SAMPLE_PRT("Enable vpss chn failed!\n");
	    goto END_VENC_1080P_CLASSIC_4;
	}


    /******************************************
     step 5: start stream venc
    ******************************************/
	VpssGrp = 0;
	VpssChn = 0;
	VencChn = 0;
	s32Ret = SAMPLE_COMM_VENC_Start(VencChn, enPayLoad,\
	                               gs_enNorm, enSize, enRcMode,u32Profile);
	if (HI_SUCCESS != s32Ret)
	{
	    SAMPLE_PRT("Start Venc failed!\n");
	    goto END_VENC_1080P_CLASSIC_5;
	}

	s32Ret = SAMPLE_COMM_VENC_BindVpss(VencChn, VpssGrp, VpssChn);
	if (HI_SUCCESS != s32Ret)
	{
	    SAMPLE_PRT("Start Venc failed!\n");
	    goto END_VENC_1080P_CLASSIC_5;
	}

    /******************************************
     step 6: stream venc process -- get stream, then save it to file.
    ******************************************/
    //s32Ret = SAMPLE_COMM_VENC_StartGetStream(s32ChnNum);
    //getchar();
    //getchar();

    gs_stPara.bThreadStart = HI_TRUE;
    gs_stPara.s32Cnt = s32ChnNum;

    struct sched_param schedvenc;
    schedvenc.sched_priority = 10;
    //to get stream
    pthread_create(&gs_VencPid, 0, SAMPLE_COMM_VENC_GetVencStreamProcsent, (HI_VOID*)&gs_stPara);
    pthread_setschedparam(gs_VencPid,SCHED_RR,&schedvenc);

    while(1) {}

    /******************************************
     step 7: exit process
    ******************************************/
    SAMPLE_COMM_VENC_StopGetStream();

END_VENC_1080P_CLASSIC_5:

    VpssGrp = 0;
	VpssChn = 0;
	VencChn = 0;
	SAMPLE_COMM_VENC_UnBindVpss(VencChn, VpssGrp, VpssChn);
	SAMPLE_COMM_VENC_Stop(VencChn);
    SAMPLE_COMM_VI_UnBindVpss(stViConfig.enViMode);

END_VENC_1080P_CLASSIC_4:	//vpss stop

    VpssGrp = 0;
	VpssChn = 0;
	SAMPLE_COMM_VPSS_DisableChn(VpssGrp, VpssChn);

END_VENC_1080P_CLASSIC_3:    //vpss stop
    SAMPLE_COMM_VI_UnBindVpss(stViConfig.enViMode);
END_VENC_1080P_CLASSIC_2:    //vpss stop
    SAMPLE_COMM_VPSS_StopGroup(VpssGrp);
END_VENC_1080P_CLASSIC_1:	//vi stop
    SAMPLE_COMM_VI_StopVi(&stViConfig);
END_VENC_1080P_CLASSIC_0:	//system exit
    SAMPLE_COMM_SYS_Exit();

    return s32Ret;
}

void InitRtspServer()
{
	int i;
	pthread_t threadId = 0;
	memset(g_rtp_playload,0,sizeof(g_rtp_playload));
	strcpy(g_rtp_playload,"G726-32");
	pthread_mutex_init(&g_sendmutex,NULL);
	pthread_mutex_init(&g_mutex,NULL);
	pthread_cond_init(&g_cond,NULL);
	memset(g_rtspClients,0,sizeof(RTSP_CLIENT)*MAX_RTSP_CLIENT);

	//pthread_create(&g_SendDataThreadId, NULL, SendDataThread, NULL);

	struct sched_param thdsched;
	thdsched.sched_priority = 2;
	//to listen visiting
	pthread_create(&threadId, NULL, RtspServerListen, NULL);
	pthread_setschedparam(threadId,SCHED_RR,&thdsched);
	printf("RTSP:-----Init Rtsp server\n");

	HI_S32 s32Ret;
    s32Ret = SAMPLE_VENC_1080P_CLASSIC();
    //exit
    if (HI_SUCCESS == s32Ret)
        printf("program exit normally!\n");
    else
        printf("program exit abnormally!\n");
	exitok++;

}
int loop()
{
	while(1)
	{
		usleep(1000);
		if(exitok>0)exitok++;
		if(exitok>10)exit(0);
	}
	return 1;


}
/*****************************************************************
  Function:       Èë¿Úµãº¯Êý
  Description:   ÓŠÓÃ³ÌÐòÈë¿Úµã
  Input:		argc,	²ÎÊýžöÊý
  			argv,	²ÎÊý×Ö·ûŽ®ÖžÕëÊý×é
  Return:        ²Ù×÷³É¹Š£¬Ôò·µ»Ø0£¬ÎÞÔò·µ»Ø<0

******************************************************************/
int main(int argc, char* argv[])
{
	InitRtspServer();
	loop();
    return 0;
}
#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */
