#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdlib.h>

// Device files

#define DEVICE_SERIAL		   	"/dev/ttyS1"
#define DISP_DEVICENAME 		"/dev/kdispdev"

// Ioctl values for panel

#define	IOCTL_PAINT_SCREEN	3
#define IOCTL_PRINT_BP_FRAME	11
#define IOCTL_PRINT_MASH_FRAME 	8
#define IOCTL_PRINT_STRING	6

#define ID_BUZZER			0xC1
#define ID_ON				0x1
#define ID_OFF				0x0

#define STATUS_OK 		0
#define  STATUS_FAIL		-1
#define status 	int

#define BP_BOARD	1
#define MASH_BOARD	0

// Colour codes

#define COLOR_WHITE                     0xffff
#define BLUE                            0xf800
#define FONT_BACKGROUND                 COLOR_WHITE

// LED
//

#define LED_FAX			(0x01 << 0)
#define LED_SCAN		(0x01 << 1)
#define LED_COPY		(0x01 << 2)
#define LED_ENERGYSAVE		(0x01 << 3)
#define LED_FUNCTIONCLEAR	(0x01 << 5)
#define LED_INTERRUPT		(0x01 << 6)
#define LED_MEMORYRX		(0x01 << 8)
#define LED_EFILING		(0x01 << 9)
#define LED_USERFUNCTION	(0x01 << 10)
#define LED_PRINTDATA		(0x01 << 12)
#define LED_ERROR		(0x01 << 13)
#define LED_COMM		(0x01 << 16)
#define LED_PRINT		(0x01 << 18)
#define LED_POWER		(0x01 << 19)


#define KEY_0			0
#define KEY_1			1
#define KEY_2			2
#define KEY_3			3
#define KEY_4			4
#define KEY_5			5
#define KEY_6			6
#define KEY_7			7
#define KEY_8			8
#define KEY_9			9
#define KEY_USERFUNCTION	10
#define KEY_INTERRUPT		11
#define KEY_COUNTER		12
#define KEY_FUNCTIONCLEAR	13
#define KEY_STOP		14
#define KEY_START		15
#define KEY_ENERGYSAVE		16
#define KEY_MENU		17
#define KEY_COPY		18
#define KEY_EFILING		19
#define KEY_SCAN		20
#define KEY_PRINT		21
#define KEY_FAX			22
#define KEY_ACCESS		23
#define KEY_SYM_ASTERISK	24
#define KEY_SYM_HASH		25
#define KEY_CLEAR		26

typedef struct{
        unsigned int nStartAddress;
        unsigned int nLenght;
        unsigned char *pData;
}DISPDRV_OFFSET_INFO;

typedef struct{
        unsigned int nX;
        unsigned int nY;
        unsigned int nWidth;
        char *str;
        unsigned int nColor;
}PANEL_DISPLAY_POSITION;


void plt_initialize(void);
int plt_conpanel_init(void);
int plt_paintScreen(void);
int plt_printFrames();
int plt_printString(int,int,int,char*,int);
void plt_beep(int);
void plt_displayProgress(int , int);
int plt_led_on(int);
status plt_messageWindow (char *, char *, char *, char *);
status plt_ServiceCallUI_1(char *, char *, char *, char *);
status plt_ServiceCallUI_2(char *, char *, char *, char *);
unsigned int boardDetection(void);
#ifdef __cplusplus
}
#endif

