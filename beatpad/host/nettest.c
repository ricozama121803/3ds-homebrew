// End-to-end Get Songs test on the PC: finds a running `tools/songs.py serve` by broadcast, reads the index and
// downloads every song into <dest>. Usage: nettest <dest dir>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../source/net.h"

int main(int argc, char **argv)
{
	if (argc < 2) { printf("usage: nettest <dest>\n"); return 1; }
	net_start(0);
	for (int i = 0; i < 500 && net_state() != NET_CONNECTED; i++) { net_poll(); usleep(10000); }
	if (net_state() != NET_CONNECTED) { printf("FAIL: server not found\n"); return 1; }
	printf("found %s: %d song(s)\n", net_server_name(), net_song_count());
	for (int i = 0; i < net_song_count(); i++) {
		const RemoteSong *s = net_song(i);
		printf("  %s \"%s\" v%s %ld bytes, %d files\n", s->id, s->title, s->version, s->bytes, s->nfiles);
		int before = net_finished();
		if (!net_download(i, argv[1])) { printf("FAIL: download refused\n"); return 1; }
		while (net_busy()) usleep(10000);
		if (net_finished() != before + 1) { printf("FAIL: %s\n", net_status()); return 1; }
		printf("  -> %s\n", net_status());
	}
	net_stop();
	printf("ok\n");
	return 0;
}
