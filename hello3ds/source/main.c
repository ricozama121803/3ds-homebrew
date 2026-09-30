#include <3ds.h>
#include <stdio.h>

int main(void) {
	gfxInitDefault();
	consoleInit(GFX_TOP, NULL);

	int presses = 0;
	touchPosition touch;

	printf("\x1b[2;2HHello from my own 3DS game!");
	printf("\x1b[4;2HA: count   START: quit");

	while (aptMainLoop()) {
		hidScanInput();
		u32 down = hidKeysDown();
		if (down & KEY_START) break;
		if (down & KEY_A) presses++;

		hidTouchRead(&touch);
		printf("\x1b[7;2HA pressed: %d times   ", presses);
		printf("\x1b[9;2HTouch: %3d, %3d   ", touch.px, touch.py);

		gfxFlushBuffers();
		gfxSwapBuffers();
		gspWaitForVBlank();
	}

	gfxExit();
	return 0;
}
