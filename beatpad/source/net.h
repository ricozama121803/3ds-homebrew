// BeatPad - Get Songs over Wi-Fi: finds the song server on your PC (tools/songs.py serve), lists its songs and
// downloads them to the SD card. Discovery and downloads run on a worker thread; the UI just polls.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define NET_HTTP_PORT   47320
#define NET_QUERY_PORT  47321    // the server listens here for "BEATPAD?"
#define NET_REPLY_PORT  47322    // the 3DS listens here for "BEATPAD! <port> <name>" (replies and beacons)
#define NET_MAX_SONGS   64
#define NET_MAX_FILES   8

typedef struct {
	char id[48], title[64], artist[64], version[24];
	long bytes;
	int nfiles;
	char file[NET_MAX_FILES][24];
	long size[NET_MAX_FILES];
} RemoteSong;

enum { NET_OFF, NET_SEARCHING, NET_CONNECTED, NET_FAILED };

bool  net_start(uint32_t lastIp);          // lastIp (network order) is tried first; 0 = none
void  net_stop(void);
void  net_poll(void);                      // once per frame: discovery
int   net_state(void);
const char *net_server_name(void);         // "hostname (192.168.1.101)"
uint32_t net_server_ip(void);
void  net_connect_ip(uint32_t ip);         // manual address
float net_searching_for(void);             // seconds spent searching

// song list from the server (valid when net_state() == NET_CONNECTED)
int   net_song_count(void);
const RemoteSong *net_song(int i);
void  net_refresh(void);

// downloads into <dest>/songs/<id>/ (via <id>.part/); one at a time
bool  net_download(int i, const char *dest);
bool  net_busy(void);
float net_progress(void);                  // 0..1 of the current download
const char *net_status(void);              // last message (progress / error)
int   net_finished(void);                  // downloads completed so far (monotonic)
void  net_cancel(void);
