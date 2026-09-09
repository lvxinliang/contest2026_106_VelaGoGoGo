/****************************************************************************
 * app/home_scense/doubao/voice_transport.c
 * Curl TLS + manual WebSocket for Doubao RealtimeAPI.
 ****************************************************************************/

#include "voice_transport.h"
#include <curl/curl.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#define DOUBAO_ERR(fmt, ...) \
  do { FILE *_f=fopen("/tmp/doubao.log","a"); \
    if(_f){fprintf(_f,fmt"\n",##__VA_ARGS__);fclose(_f);} \
    fprintf(stderr,fmt"\n",##__VA_ARGS__); }while(0)

#define WS_FIN 0x80u
#define WS_OPCODE_BINARY 0x02u
#define WS_OPCODE_CLOSE 0x08u
#define WS_OPCODE_PING 0x09u
#define WS_OPCODE_PONG 0x0Au
#define WS_MASK 0x80u

struct voice_transport_s { CURL *curl; curl_socket_t fd; char logid[96]; uint8_t pbf[4096]; int pbf_len; };

static ssize_t curl_send(CURL *c, const void *d, size_t n) {
  size_t s=0; CURLcode r=curl_easy_send(c,d,n,&s);
  return (r==CURLE_OK)?(ssize_t)s:-EIO; }

static int curl_recv_to(CURL *c, curl_socket_t fd, void *b, size_t n, int ms) {
  struct timeval st,now,df; int el; size_t nr; CURLcode r;
  gettimeofday(&st,NULL);
  do { r=curl_easy_recv(c,b,n,&nr); if(r==CURLE_OK&&nr>0)return(int)nr;
    { fd_set f;struct timeval tv={0,20000};
      FD_ZERO(&f);FD_SET(fd,&f);select(FD_SETSIZE,&f,NULL,NULL,&tv); }
    gettimeofday(&now,NULL);timersub(&now,&st,&df);
    el=(int)(df.tv_sec*1000+df.tv_usec/1000);
  } while(el<ms);
  return 0; }

/* ms 窗口是否已过(用于 got==0 的干净超时判定;ms<=0 视为立即过期) */
static int el_expired(struct timeval *st, int ms) {
  struct timeval now,df; int el;
  if(ms<=0) return 1;
  gettimeofday(&now,NULL); timersub(&now,st,&df);
  el=(int)(df.tv_sec*1000+df.tv_usec/1000);
  return el>=ms;
}

/* 在 ms 超时内精确读满 n 字节(累加分段到达的字节)。
 * 返回 n=读满;0=整段超时且一个字节都没来(干净超时);-EIO=读到部分后中断。
 *
 * 关键修复(TTS 无声根因):一旦读到部分字节(got>0),就必须把整个 n 字节
 * 读完,不能受 ms 限制退出。此前 ms=0(session_loop 非阻塞排空)时循环条件
 * el<0 恒假,只执行一次;若帧头(2B)或大音频帧(16KB+)被 TCP 分片,首次
 * curl_easy_recv 只读到部分字节,got>0 但 <n,立即退出返回 -EIO,被上层
 * 当成断连 → session 重连 → 丢失 TTSResponse(352)音频帧 → 只有文字无声音。
 * 现在:开始读到数据后,即使 ms=0 也持续 select 等待剩余字节读满 n。 */
static int recv_exact(CURL *c, curl_socket_t fd, uint8_t *b, size_t n, int ms) {
  struct timeval st,now,df; int el; size_t got=0; CURLcode r; size_t nr;
  gettimeofday(&st,NULL);
  for(;;) {
    nr=0; r=curl_easy_recv(c,b+got,n-got,&nr);
    if(r==CURLE_OK&&nr>0){ got+=nr; if(got>=n)return(int)n; continue; }
    /* 连接关闭/出错:CURLE_OK 且 nr==0 是对端 EOF;非 AGAIN 是错误。 */
    if(r==CURLE_OK&&nr==0) return -ECONNRESET;
    if(r!=CURLE_AGAIN)     return -ECONNRESET;
    /* AGAIN(暂无数据):已读到半帧则必须继续等剩余字节(TCP 分片),
     * 一个字节都没读到才受 ms 超时约束(ms=0 立即返回干净超时)。 */
    if(got==0 && el_expired(&st,ms)) return 0;
    { fd_set f;struct timeval tv={0,20000};
      FD_ZERO(&f);FD_SET(fd,&f);select(FD_SETSIZE,&f,NULL,NULL,&tv); }
    /* 已读到部分数据时,给剩余字节一个兜底上限(2s),防止对端只发半帧
     * 后卡死导致本函数永久阻塞。 */
    if(got>0){
      gettimeofday(&now,NULL);timersub(&now,&st,&df);
      el=(int)(df.tv_sec*1000+df.tv_usec/1000);
      if(el>2000) return -EIO;
    }
  } }

static ssize_t curl_send_to(CURL *c, curl_socket_t fd, const void *d, size_t n, int ms) {
  struct timeval st,now,df; int el; ssize_t s;
  gettimeofday(&st,NULL);
  do { s=curl_send(c,d,n); if(s>0)return s;
    /* send failed: drain mbedTLS pending server data, poll socket, retry */
    { char sink[256]; size_t nr=0;
      curl_easy_recv(c, sink, sizeof(sink), &nr);
}
    { fd_set f;struct timeval tv={0,20000};
      FD_ZERO(&f);FD_SET(fd,&f);select(FD_SETSIZE,&f,NULL,NULL,&tv); }
    gettimeofday(&now,NULL);timersub(&now,&st,&df);
    el=(int)(df.tv_sec*1000+df.tv_usec/1000);
  } while(el<ms);
  return -EIO; }

static int ws_upgrade(voice_transport_t *t, const voice_transport_config_t *cfg) {
  char req[1024],rsp[1024]; ssize_t tt=0,ln,ps;
  ln=snprintf(req,sizeof(req),
    "GET /api/v3/realtime/dialogue HTTP/1.1\r\n"
    "Host: openspeech.bytedance.com\r\n"
    "User-Agent: curl/8.9.1\r\n"
    "Accept: */*\r\n"
    "Upgrade: websocket\r\nConnection: Upgrade\r\n"
    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
    "Sec-WebSocket-Version: 13\r\n"
    "X-Api-App-ID: %s\r\nX-Api-Access-Key: %s\r\n"
    "X-Api-Resource-Id: %s\r\nX-Api-App-Key: %s\r\n"
    "%s%s%s\r\n",
    cfg->app_id,cfg->access_token,cfg->resource_id,cfg->app_key,
    cfg->connect_id?"X-Api-Connect-Id: ":"",
    cfg->connect_id?cfg->connect_id:"",
    cfg->connect_id?"\r\n":"");
  if(ln<0||(size_t)ln>=sizeof(req))return-EMSGSIZE;
  if(curl_send_to(t->curl,t->fd,req,(size_t)ln,5000)!=ln)return-EIO;
  while(tt<(ssize_t)(sizeof(rsp)-1)){
    ln=curl_recv_to(t->curl,t->fd,rsp+tt,sizeof(rsp)-1-(size_t)tt,8000);
    if(ln<=0){DOUBAO_ERR("doubao:WS timeout(%d)",(int)tt);return-EIO;}
    tt+=ln;rsp[tt]='\0';if(strstr(rsp,"\r\n\r\n"))break; }
  if(tt>=(ssize_t)(sizeof(rsp)-1))return-EMSGSIZE;
  { char*l=strcasestr(rsp,"X-Tt-Logid:");
    if(l){l+=10;while(*l==' '||*l=='\t')l++;
      for(ps=0;l[ps]&&l[ps]!='\r'&&l[ps]!='\n'&&ps<(ssize_t)(sizeof(t->logid)-1);ps++)
        t->logid[ps]=l[ps];t->logid[ps]='\0'; } }
  if(!strstr(rsp,"101")){ssize_t i;char s[256];
    size_t n=tt<(ssize_t)(sizeof(s)-1)?(size_t)tt:sizeof(s)-1;
    for(i=0;i<(ssize_t)n;i++)s[i]=rsp[i]>=0x20&&rsp[i]<=0x7e?rsp[i]:'.';
    s[n]='\0';DOUBAO_ERR("doubao:WS refused: %s",s);return-EIO;}
  return 0; }

static int ws_send(CURL *c, curl_socket_t fd, const uint8_t *p, size_t sz) {
  uint8_t hd[14],mk[4],*m;size_t ps=2,tt,i; int ret=-EIO;
  hd[0]=WS_FIN|WS_OPCODE_BINARY;hd[1]=WS_MASK;
  if(sz<=125)hd[1]|=(uint8_t)sz;
  else if(sz<=65535){hd[1]|=126;hd[2]=(uint8_t)(sz>>8);hd[3]=(uint8_t)sz;ps=4;}
  else return-EMSGSIZE;
  tt=ps+4+sz;m=malloc(tt);if(!m)return-ENOMEM;
  memcpy(m,hd,ps);
  mk[0]=(uint8_t)(rand()&0xff);mk[1]=(uint8_t)(rand()&0xff);
  mk[2]=(uint8_t)(rand()&0xff);mk[3]=(uint8_t)(rand()&0xff);
  memcpy(m+ps,mk,4);
  for(i=0;i<sz;i++)m[ps+4+i]=p[i]^mk[i&3];
  if(curl_send_to(c,fd,m,tt,10000)==(ssize_t)tt)ret=0;
  else DOUBAO_ERR("doubao:ws send fail %zu",sz);
  free(m);return ret; }

static int ws_recv(CURL *c, curl_socket_t fd, uint8_t *p, size_t cap, int ms) {
  uint8_t hd[2],op;uint64_t ln;size_t ps;int r;
  /* 帧头:读满 2 字节。干净超时(整个 ms 内无任何字节)返回 -ETIMEDOUT,
   * 供上层区分"暂时没数据"(继续等)与"真错误"。 */
  r=recv_exact(c,fd,hd,2,ms);
  if(r==0)return-ETIMEDOUT;
  if(r==-ECONNRESET)return-ECONNRESET;   /* 连接已死 → 上层重连 */
  if(r!=2)return-EIO;
  op=hd[0]&0x0f;ln=hd[1]&0x7f;if(hd[1]&WS_MASK)return-EIO;
  if(ln==126){uint8_t e[2];if(recv_exact(c,fd,e,2,5000)!=2)return-EIO;
    ln=((uint64_t)e[0]<<8)|e[1];}
  else if(ln==127){uint8_t e[8];uint32_t hi,lo;
    if(recv_exact(c,fd,e,8,5000)!=8)return-EIO;
    hi=((uint32_t)e[0]<<24)|((uint32_t)e[1]<<16)|((uint32_t)e[2]<<8)|e[3];
    lo=((uint32_t)e[4]<<24)|((uint32_t)e[5]<<16)|((uint32_t)e[6]<<8)|e[7];
    ln=(uint64_t)hi*0x100000000u+lo;}
  if(ln>cap)return-EMSGSIZE;
  if(ln>0){r=recv_exact(c,fd,p,(size_t)ln,5000);if(r!=(int)ln)return-EIO;}
  ps=(size_t)ln;
  switch(op){case WS_OPCODE_CLOSE:{
    /* 记录服务端关连接的状态码(payload 前 2 字节大端),定位为何频繁断开 */
    int code = (ln>=2)? (((int)p[0]<<8)|p[1]) : -1;
    DOUBAO_ERR("doubao:WS CLOSE code=%d len=%llu",code,(unsigned long long)ln);
    return-ECONNRESET;}
  case WS_OPCODE_PING:{
    /* WebSocket 要求客户端→服务端所有帧必须加掩码,并回显 PING 的
     * payload。此前回的 PONG 未加掩码(且丢了 payload),服务器判定协议
     * 违规 → 每隔几秒关连接,表现为豆包反复重连。这里用带掩码的 ws_send
     * 发 PONG(ws_send 内部已加掩码位+4字节掩码 key)。 */
    uint8_t mp[2]={WS_FIN|WS_OPCODE_PONG,WS_MASK}; uint8_t mk[4]; size_t i;
    uint8_t *fr; size_t frn;
    if(ln>125)return 0;                    /* 控制帧 payload ≤125,超出忽略 */
    mp[1]|=(uint8_t)ln;
    frn=2+4+(size_t)ln; fr=malloc(frn);
    if(!fr)return 0;
    fr[0]=mp[0]; fr[1]=mp[1];
    mk[0]=(uint8_t)(rand()&0xff);mk[1]=(uint8_t)(rand()&0xff);
    mk[2]=(uint8_t)(rand()&0xff);mk[3]=(uint8_t)(rand()&0xff);
    memcpy(fr+2,mk,4);
    for(i=0;i<(size_t)ln;i++)fr[6+i]=p[i]^mk[i&3];  /* 回显 PING payload */
    curl_send_to(c,fd,fr,frn,2000);
    free(fr);
    return 0;}
  case WS_OPCODE_BINARY:return(int)ps;default:return(int)ps;}
}

int voice_transport_connect(voice_transport_t **o,const voice_transport_config_t *cfg,int ms){
  voice_transport_t*t;CURLcode r;
  if(!o||!cfg->url)return-EINVAL;
  t=calloc(1,sizeof(*t));if(!t)return-ENOMEM;t->fd=CURL_SOCKET_BAD;
  t->curl=curl_easy_init();if(!t->curl){free(t);return-ENOMEM;}
  curl_easy_setopt(t->curl,CURLOPT_URL,cfg->url);
  curl_easy_setopt(t->curl,CURLOPT_CONNECT_ONLY,1L);
  curl_easy_setopt(t->curl,CURLOPT_CONNECTTIMEOUT_MS,(long)ms);
  curl_easy_setopt(t->curl,CURLOPT_SSL_VERIFYPEER,0L);
  curl_easy_setopt(t->curl,CURLOPT_SSL_VERIFYHOST,0L);
  r=curl_easy_perform(t->curl);
  if(r!=CURLE_OK){DOUBAO_ERR("doubao:TLS fail:%s(%d)",curl_easy_strerror(r),(int)r);
    voice_transport_close(t);return-EIO;}
  r=curl_easy_getinfo(t->curl,CURLINFO_ACTIVESOCKET,&t->fd);
  if(r!=CURLE_OK||t->fd==CURL_SOCKET_BAD){
    DOUBAO_ERR("doubao:no sock");voice_transport_close(t);return-EIO;}
  /* Drain TLS post-handshake data (NewSessionTicket etc.) BEFORE ws_upgrade
   * so we don't accidentally consume WebSocket-level frames. */
  { int drained=0;
    while(drained<32768){char sink[256];size_t nr=0;
      if(curl_easy_recv(t->curl,sink,sizeof(sink),&nr)!=CURLE_OK||nr==0)break;
      drained+=(int)nr;}
}

  r=ws_upgrade(t,cfg);if(r<0){voice_transport_close(t);return r;}

  /* After ws_upgrade, drain leftover bytes (HTTP trailer garbage + first
   * real WS frame that were decrypted in the same TLS record). Save the
   * raw bytes — voice_transport_receive will skip the HTTP junk at front. */
  { size_t nr=0; t->pbf_len=0;
    while(t->pbf_len<4096&&curl_easy_recv(t->curl,t->pbf+t->pbf_len,
            sizeof(t->pbf)-t->pbf_len,&nr)==CURLE_OK&&nr>0)
      t->pbf_len+=(int)nr; }

  DOUBAO_ERR("doubao:WS OK logid=%s",t->logid);*o=t;return 0; }

int voice_transport_send(voice_transport_t*t,voice_ws_opcode_t op,
  const uint8_t*d,size_t n){
  (void)op;if(!t||!t->curl)return-EINVAL;
  return ws_send(t->curl,t->fd,d,n); }

int voice_transport_receive(voice_transport_t*t,voice_ws_opcode_t*op,
  uint8_t*d,size_t c,int ms){
  int r,start;if(!t||!t->curl||!op||!d||c==0)return-EINVAL;
  /* pbf drain: 略过 HTTP 101 尾部残渣, 找到第一个 WS 帧头 0x82 */
  if(t->pbf_len>0){
    for(start=0;start<t->pbf_len-1;start++)
      if(t->pbf[start]==0x82) break;
    if(start<t->pbf_len-1){
      int n=t->pbf_len-start<(int)c?t->pbf_len-start:(int)c;
      memcpy(d,t->pbf+start,n);t->pbf_len=0;*op=VOICE_WS_BINARY;
      return n;}
    t->pbf_len=0;}
  /* 收到 PING/PONG/CLOSE 等控制帧时 ws_recv 内部处理后返回 0,
   * 需继续轮询而非向上层返回 0(上层 0=break 接收循环 → 漏帧)。
   * 此处用 do-while 吞掉连续控制帧, 直到收到数据帧或超时/断连。 */
  do {
    r=ws_recv(t->curl,t->fd,d,c,ms);
    if(r==-ETIMEDOUT)return 0;                 /* 干净超时:无数据,让上层继续等 */
    if(r==-ECONNRESET){*op=VOICE_WS_CLOSE;return r;}  /* 连接已死 → 重连 */
    if(r==-EMSGSIZE)return 0;                  /* 超大帧丢弃,不断连 */
    if(r<0){
      DOUBAO_ERR("doubao:ws recv err r=%d ms=%d",r,ms);
      *op=VOICE_WS_CLOSE;return-ECONNRESET;}  /* 读到半帧中断:视为断连重连,不空转 */
    /* r==0: ws_recv 处理了 PING/PONG, 继续轮询下一帧;
     * r>0: 收到 BINARY 数据帧, 返回 payload 长度 */
  } while(r==0);
  *op=VOICE_WS_BINARY;return r; }

const char* voice_transport_logid(const voice_transport_t*t){return t?t->logid:"";}

void voice_transport_close(voice_transport_t*t){
  uint8_t cf[6]={WS_FIN|WS_OPCODE_CLOSE,WS_MASK|2,0,0,0,0};
  if(!t)return;if(t->curl){
    if(t->fd!=CURL_SOCKET_BAD)curl_send(t->curl,cf,sizeof(cf));
    curl_easy_cleanup(t->curl);}free(t); }
