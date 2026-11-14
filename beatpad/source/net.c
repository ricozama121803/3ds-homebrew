// BeatPad - Get Songs client. Plain BSD sockets (the 3DS soc service speaks the same API), so this file also builds
// on the PC for host/nettest.c. Protocol (tools/songs.py):
//   UDP  3DS -> broadcast:47321 "BEATPAD?"          server -> 3DS:47322 "BEATPAD! <http port> <name>"
//        (the server also broadcasts that reply every second, so the 3DS finds it even if its query is dropped)
//   HTTP GET /index.txt   lines: "song\t<id>\t<version>\t<title>\t<artist>\t<bytes>" then "file\t<id>\t<name>\t<size>"
//   HTTP GET /songs/<id>/<file>
#include "net.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <poll.h>

#ifdef __3DS__
#include <3ds.h>
#include <malloc.h>
static u32 *socBuf;
static Thread worker;
static void sleep_ms(int ms) { svcSleepThread((s64)ms * 1000000); }
static uint64_t now_ms(void) { return osGetTime(); }
#else
#include <pthread.h>
#include <time.h>
static pthread_t worker;
static void sleep_ms(int ms) { usleep(ms * 1000); }
static uint64_t now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000; }
#endif

enum { JOB_NONE, JOB_INDEX, JOB_DOWNLOAD };

static volatile int state = NET_OFF, job = JOB_NONE, finished;
static volatile bool running, cancel;
static volatile float progress;
static char status[96], serverName[96], dlDest[200];
static volatile int dlIndex;
static uint32_t serverIp;
static uint16_t serverPort = NET_HTTP_PORT;
static int udp = -1;
static uint64_t startMs, lastQuery;
static RemoteSong songs[NET_MAX_SONGS], staging[NET_MAX_SONGS];
static volatile int nsongs;

int net_state(void) { return state; }
const char *net_server_name(void) { return serverName; }
uint32_t net_server_ip(void) { return serverIp; }
int net_song_count(void) { return nsongs; }
const RemoteSong *net_song(int i) { return &songs[i]; }
bool net_busy(void) { return job != JOB_NONE; }
float net_progress(void) { return progress; }
const char *net_status(void) { return status; }
int net_finished(void) { return finished; }
void net_cancel(void) { cancel = true; }
float net_searching_for(void) { return (now_ms() - startMs) / 1000.0f; }

// ---- HTTP -----------------------------------------------------------------------------
// recv that gives up after `ms` of silence (so a vanished PC can't hang the worker)
static int recv_t(int s, void *buf, int n, int ms)
{
	struct pollfd p = { .fd = s, .events = POLLIN };
	if (poll(&p, 1, ms) <= 0) return -1;
	return recv(s, buf, n, 0);
}

static int connect_t(int s, const struct sockaddr_in *a, int ms)
{
	int fl = fcntl(s, F_GETFL, 0);
	fcntl(s, F_SETFL, fl | O_NONBLOCK);
	int r = connect(s, (const struct sockaddr *)a, sizeof *a);
	if (r < 0 && (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EALREADY)) {
		struct pollfd p = { .fd = s, .events = POLLOUT };
		r = poll(&p, 1, ms) == 1 && !(p.revents & (POLLERR | POLLHUP)) ? 0 : -1;
	}
	fcntl(s, F_SETFL, fl);
	return r;
}

static int http_get(const char *path, long *length)
{
	int s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0) return -1;
	struct sockaddr_in a = { 0 };
	a.sin_family = AF_INET; a.sin_port = htons(serverPort); a.sin_addr.s_addr = serverIp;
	if (connect_t(s, &a, 2500) < 0) { close(s); return -1; }
	char req[256];
	int n = snprintf(req, sizeof req, "GET %s HTTP/1.0\r\nHost: beatpad\r\n\r\n", path);
	if (send(s, req, n, 0) != n) { close(s); return -1; }
	// headers, one byte at a time (they are short)
	char hdr[1024]; int h = 0;
	while (h < (int)sizeof hdr - 1) {
		if (recv_t(s, hdr + h, 1, 5000) != 1) { close(s); return -1; }
		h++;
		if (h >= 4 && !memcmp(hdr + h - 4, "\r\n\r\n", 4)) break;
	}
	hdr[h] = 0;
	if (strncmp(hdr, "HTTP/1.", 7) || strncmp(hdr + 9, "200", 3)) { close(s); return -1; }
	*length = -1;
	for (char *p = hdr; (p = strchr(p, '\n')); p++)
		if (!strncasecmp(p + 1, "Content-Length:", 15)) *length = atol(p + 16);
	return s;
}

static bool fetch_index(void)
{
	long len;
	int s = http_get("/index.txt", &len);
	if (s < 0) return false;
	static char buf[32768];
	int got = 0, r;
	while (got < (int)sizeof buf - 1 && (r = recv_t(s, buf + got, sizeof buf - 1 - got, 5000)) > 0) got += r;
	close(s);
	buf[got] = 0;
	int n = 0;
	char *save = NULL;
	for (char *line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		char *f[7]; int nf = 0;
		for (char *t = line; nf < 7; ) { f[nf++] = t; char *tab = strchr(t, '\t'); if (!tab) break; *tab = 0; t = tab + 1; }
		if (nf >= 6 && !strcmp(f[0], "song") && n < NET_MAX_SONGS) {
			RemoteSong *rs = &staging[n++];
			memset(rs, 0, sizeof *rs);
			snprintf(rs->id, sizeof rs->id, "%s", f[1]); snprintf(rs->version, sizeof rs->version, "%s", f[2]);
			snprintf(rs->title, sizeof rs->title, "%s", f[3]); snprintf(rs->artist, sizeof rs->artist, "%s", f[4]);
			rs->bytes = atol(f[5]);
		} else if (nf >= 4 && !strcmp(f[0], "file") && n > 0 && !strcmp(staging[n - 1].id, f[1])) {
			RemoteSong *rs = &staging[n - 1];
			if (rs->nfiles < NET_MAX_FILES) {
				snprintf(rs->file[rs->nfiles], sizeof rs->file[0], "%s", f[2]);
				rs->size[rs->nfiles++] = atol(f[3]);
			}
		}
	}
	memcpy(songs, staging, sizeof songs);
	nsongs = n;
	return true;
}

static void remove_dir(const char *path)
{
	DIR *d = opendir(path);
	if (d) {
		struct dirent *e;
		char p[512];
		while ((e = readdir(d))) {
			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
			snprintf(p, sizeof p, "%s/%s", path, e->d_name);
			remove(p);
		}
		closedir(d);
	}
	rmdir(path);
}

static bool download(const RemoteSong *rs, const char *dest)
{
	char dir[300], part[300], path[400], url[200];
	snprintf(path, sizeof path, "%s", dest); mkdir(path, 0777);
	snprintf(path, sizeof path, "%s/songs", dest); mkdir(path, 0777);
	snprintf(dir, sizeof dir, "%s/songs/%s", dest, rs->id);
	snprintf(part, sizeof part, "%s/songs/%s.part", dest, rs->id);
	remove_dir(part);
	mkdir(part, 0777);
	long total = rs->bytes > 0 ? rs->bytes : 1, done = 0;
	static char buf[16384];
	for (int i = 0; i < rs->nfiles; i++) {
		snprintf(url, sizeof url, "/songs/%s/%s", rs->id, rs->file[i]);
		long len;
		int s = http_get(url, &len);
		if (s < 0) { snprintf(status, sizeof status, "Couldn't get %s", rs->file[i]); remove_dir(part); return false; }
		snprintf(path, sizeof path, "%s/%s", part, rs->file[i]);
		FILE *f = fopen(path, "wb");
		if (!f) { close(s); snprintf(status, sizeof status, "SD card write failed"); remove_dir(part); return false; }
		long got = 0; int r;
		while ((r = recv_t(s, buf, sizeof buf, 8000)) > 0) {
			if (fwrite(buf, 1, r, f) != (size_t)r) { r = -1; break; }
			got += r; done += r;
			progress = (float)done / total;
			snprintf(status, sizeof status, "%s  %.1f / %.1f MB", rs->title, done / 1048576.0, total / 1048576.0);
			if (cancel) { r = -1; break; }
		}
		fclose(f); close(s);
		if (r < 0 || (len >= 0 && got != len)) {
			snprintf(status, sizeof status, cancel ? "Cancelled" : "Download of %s was cut off", rs->file[i]);
			remove_dir(part);
			return false;
		}
	}
	snprintf(path, sizeof path, "%s/version.txt", part);
	FILE *v = fopen(path, "w");
	if (v) { fprintf(v, "%s\n", rs->version); fclose(v); }
	remove_dir(dir);                       // replace an older copy
	if (rename(part, dir)) { snprintf(status, sizeof status, "Couldn't finish %s", rs->title); return false; }
	snprintf(status, sizeof status, "Got %s", rs->title);
	return true;
}

#ifdef __3DS__
static void worker_main(void *arg)
#else
static void *worker_main(void *arg)
#endif
{
	(void)arg;
	while (running) {
		int j = job;
		if (j == JOB_INDEX) {
			if (fetch_index()) state = NET_CONNECTED;
			else if (state == NET_CONNECTED) { state = NET_SEARCHING; snprintf(status, sizeof status, "Lost the connection"); }
			job = JOB_NONE;
		} else if (j == JOB_DOWNLOAD) {
			cancel = false; progress = 0;
			if (download(&songs[dlIndex], dlDest)) finished++;
			job = JOB_NONE;
		} else sleep_ms(20);
	}
#ifndef __3DS__
	return NULL;
#endif
}

// ---- discovery (main thread, non-blocking) ---------------------------------------------
static void found(uint32_t ip, int port, const char *name)
{
	serverIp = ip; serverPort = (uint16_t)port;
	struct in_addr ia; ia.s_addr = ip;
	snprintf(serverName, sizeof serverName, "%s (%s)", name, inet_ntoa(ia));
	if (job == JOB_NONE) job = JOB_INDEX;
}

void net_poll(void)
{
	if (state == NET_OFF || udp < 0) return;
	uint64_t t = now_ms();
	if (state == NET_SEARCHING && t - lastQuery > 1000 && job == JOB_NONE) {
		lastQuery = t;
		struct sockaddr_in b = { 0 };
		b.sin_family = AF_INET; b.sin_port = htons(NET_QUERY_PORT); b.sin_addr.s_addr = htonl(INADDR_BROADCAST);
		sendto(udp, "BEATPAD?", 8, 0, (struct sockaddr *)&b, sizeof b);
	}
	char msg[128];
	struct sockaddr_in from; socklen_t fl = sizeof from;
	int r;
	while ((r = recvfrom(udp, msg, sizeof msg - 1, 0, (struct sockaddr *)&from, &fl)) > 0) {
		msg[r] = 0;
		int port; char name[64] = "PC";
		if (state == NET_SEARCHING && sscanf(msg, "BEATPAD! %d %63s", &port, name) >= 1) found(from.sin_addr.s_addr, port, name);
		fl = sizeof from;
	}
}

void net_connect_ip(uint32_t ip)
{
	if (state == NET_OFF) return;
	state = NET_SEARCHING;
	found(ip, NET_HTTP_PORT, "PC");
}

void net_refresh(void) { if (state == NET_CONNECTED && job == JOB_NONE) job = JOB_INDEX; }

bool net_download(int i, const char *dest)
{
	if (state != NET_CONNECTED || job != JOB_NONE || i < 0 || i >= nsongs || strlen(dest) >= sizeof dlDest) return false;
	snprintf(dlDest, sizeof dlDest, "%s", dest);
	dlIndex = i;
	snprintf(status, sizeof status, "Starting %s", songs[i].title);
	job = JOB_DOWNLOAD;
	return true;
}

bool net_start(uint32_t lastIp)
{
	if (state != NET_OFF) return true;
#ifdef __3DS__
	socBuf = memalign(0x1000, 0x100000);
	if (!socBuf || R_FAILED(socInit(socBuf, 0x100000))) { free(socBuf); socBuf = NULL; state = NET_FAILED; return false; }
#endif
	udp = socket(AF_INET, SOCK_DGRAM, 0);
	if (udp >= 0) {
		int one = 1;
		setsockopt(udp, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
		setsockopt(udp, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
		struct sockaddr_in a = { 0 };
		a.sin_family = AF_INET; a.sin_port = htons(NET_REPLY_PORT); a.sin_addr.s_addr = htonl(INADDR_ANY);
		bind(udp, (struct sockaddr *)&a, sizeof a);
		fcntl(udp, F_SETFL, fcntl(udp, F_GETFL, 0) | O_NONBLOCK);
	}
	state = NET_SEARCHING; nsongs = 0; job = JOB_NONE; status[0] = 0; serverName[0] = 0;
	startMs = now_ms(); lastQuery = 0;
	running = true;
#ifdef __3DS__
	s32 prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	worker = threadCreate(worker_main, NULL, 32 * 1024, prio + 1, -1, false);
#else
	pthread_create(&worker, NULL, worker_main, NULL);
#endif
	if (lastIp) found(lastIp, NET_HTTP_PORT, "PC");
	return true;
}

void net_stop(void)
{
	if (state == NET_OFF) return;
	cancel = true;
	running = false;
#ifdef __3DS__
	threadJoin(worker, U64_MAX);
	threadFree(worker);
#else
	pthread_join(worker, NULL);
#endif
	if (udp >= 0) close(udp);
	udp = -1;
#ifdef __3DS__
	socExit();
	free(socBuf); socBuf = NULL;
#endif
	state = NET_OFF; job = JOB_NONE;
}
