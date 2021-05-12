#include <dos.h>
#include <stdio.h>
#include <i86.h>
#include <conio.h>
#include <memory.h>
#include <stdint.h>
#include "CGA.h"
#include "CGAData.inc"

#define CGA_BASE_VRAM_ADDRESS (uint8_t*) MK_FP(0xB800, 0)

#define WINDOW_TOP 24
#define WINDOW_HEIGHT 168
#define WINDOW_BOTTOM (WINDOW_TOP + WINDOW_HEIGHT)

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 200

#define ADDRESS_BAR_X 80
#define ADDRESS_BAR_Y 10
#define ADDRESS_BAR_WIDTH 480
#define ADDRESS_BAR_HEIGHT 12
#define TITLE_BAR_HEIGHT 8
#define STATUS_BAR_HEIGHT 8
#define STATUS_BAR_Y (SCREEN_HEIGHT - STATUS_BAR_HEIGHT)

#define SCROLL_BAR_WIDTH 16

#define WINDOW_VRAM_TOP_EVEN (BYTES_PER_LINE * (WINDOW_TOP / 2))
#define WINDOW_VRAM_TOP_ODD (0x2000 + BYTES_PER_LINE * (WINDOW_TOP / 2))
#define WINDOW_VRAM_BOTTOM_EVEN (BYTES_PER_LINE * (WINDOW_BOTTOM / 2 - 1))
#define WINDOW_VRAM_BOTTOM_ODD (0x2000 + BYTES_PER_LINE * (WINDOW_BOTTOM / 2 - 1))
#define BYTES_PER_LINE 80

int scissorX1 = 0, scissorY1 = 0;
int scissorX2 = SCREEN_WIDTH, scissorY2 = SCREEN_HEIGHT;

CGADriver::CGADriver()
{
	screenWidth = 640;
	screenHeight = 200;
	windowWidth = screenWidth - 16;
	windowHeight = WINDOW_HEIGHT;
	windowX = 0;
	windowY = WINDOW_TOP;

	addressBar.x = ADDRESS_BAR_X;
	addressBar.y = ADDRESS_BAR_Y;
	addressBar.width = ADDRESS_BAR_WIDTH;
	addressBar.height = ADDRESS_BAR_HEIGHT;

	scrollBar.x = SCREEN_WIDTH - SCROLL_BAR_WIDTH;
	scrollBar.y = WINDOW_TOP;
	scrollBar.width = SCROLL_BAR_WIDTH;
	scrollBar.height = WINDOW_HEIGHT;
}

void CGADriver::Init()
{
	startingScreenMode = GetScreenMode();
	SetScreenMode(6);
}

void CGADriver::Shutdown()
{
	SetScreenMode(startingScreenMode);
}

int CGADriver::GetScreenMode()
{
	union REGS inreg, outreg;
	inreg.h.ah = 0xf;

	int86(0x10, &inreg, &outreg);

	return (int)outreg.h.al;
}

void CGADriver::SetScreenMode(int screenMode)
{
	union REGS inreg, outreg;
	inreg.h.ah = 0;
	inreg.h.al = (unsigned char)screenMode;

	int86(0x10, &inreg, &outreg);
}

static void FastMemSet(void far* mem, uint8_t value, unsigned int count);
#pragma aux FastMemSet = \
	"rep stosb" \
	modify [di cx] \	
	parm[es di][al][cx];


void CGADriver::ClearScreen()
{
	// White out main page
	FastMemSet(CGA_BASE_VRAM_ADDRESS + BYTES_PER_LINE * TITLE_BAR_HEIGHT / 2, 0xff, BYTES_PER_LINE * (SCREEN_HEIGHT - TITLE_BAR_HEIGHT - STATUS_BAR_HEIGHT) / 2);
	FastMemSet(CGA_BASE_VRAM_ADDRESS + 0x2000 + BYTES_PER_LINE * TITLE_BAR_HEIGHT / 2, 0xff, BYTES_PER_LINE * (SCREEN_HEIGHT - TITLE_BAR_HEIGHT - STATUS_BAR_HEIGHT) / 2);

	// Page top line divider
	HLine(0, WINDOW_TOP - 1, 640);

	// Scroll bar 
	DrawScrollBar(0, WINDOW_HEIGHT);

	DrawButtonRect(addressBar.x, addressBar.y, addressBar.width, addressBar.height);
	DrawString("file://path/to/file", addressBar.x + 2, addressBar.y + 2, 1);

	DrawTitle("MicroWeb");
	DrawStatus("Status");

	scissorY1 = WINDOW_TOP;
	scissorY2 = WINDOW_BOTTOM;
}

void CGADriver::DrawString(const char* text, int x, int y, int size, FontStyle::Type style)
{
	Font* font = GetFont(size);

	int startX = x;
	uint8_t glyphHeight = font->glyphHeight;
	if (x >= scissorX2)
	{
		return;
	}
	if (y >= scissorY2)
	{
		return;
	}
	if (y + glyphHeight > scissorY2)
	{
		glyphHeight = (uint8_t)(scissorY2 - y);
	}
	if (y + glyphHeight < scissorY1)
	{
		return;
	}

	uint8_t firstLine = 0;
	if (y < scissorY1)
	{
		firstLine += scissorY1 - y;
		y += firstLine;
	}

	uint8_t far* VRAM = (uint8_t far*) CGA_BASE_VRAM_ADDRESS;

	VRAM += (y >> 1) * BYTES_PER_LINE;

	if (y & 1)
	{
		VRAM += 0x2000;
	}

	while (*text)
	{
		char c = *text++;
		if (c < 32 || c >= 128)
		{
			continue;
		}

		char index = c - 32;
		uint8_t glyphWidth = font->glyphWidth[index];

		if (glyphWidth == 0)
		{
			continue;
		}

		uint8_t* glyphData = font->glyphData + (font->glyphDataStride * index);
		
		glyphData += (firstLine * font->glyphWidthBytes);

		bool oddLine = (y & 1);
		uint8_t far* VRAMptr = VRAM + (x >> 3);

		for (uint8_t j = firstLine; j < glyphHeight; j++)
		{
			uint8_t writeOffset = (uint8_t)(x) & 0x7;

			if ((style & FontStyle::Italic) && j < (glyphHeight >> 1))
			{
				writeOffset++;
			}

			for (uint8_t i = 0; i < font->glyphWidthBytes; i++)
			{
				uint8_t glyphPixels = *glyphData++;

				if (style & FontStyle::Bold)
				{
					glyphPixels |= (glyphPixels >> 1);
				}

				VRAMptr[i] ^= (glyphPixels >> writeOffset);
				VRAMptr[i + 1] ^= (glyphPixels << (8 - writeOffset));
			}

			if (oddLine)
			{
				VRAMptr -= (0x2000 - BYTES_PER_LINE);
			}
			else
			{
				VRAMptr += 0x2000;
			}
			oddLine = !oddLine;
		}

		x += glyphWidth;
		if (style & FontStyle::Bold)
		{
			x++;
		}

		if (x >= scissorX2)
		{
			break;
		}
	}

	if ((style & FontStyle::Underline) && y + font->glyphHeight - 1 < scissorY2)
	{
		HLine(startX, y + font->glyphHeight - 1, x - startX);
	}
}

Font* CGADriver::GetFont(int fontSize)
{
	switch (fontSize)
	{
	case 0:
		return &CGA_SmallFont;
	case 2:
		return &CGA_LargeFont;
	default:
		return &CGA_RegularFont;
	}
}

void CGADriver::HLine(int x, int y, int count)
{
	uint8_t far* VRAMptr = (uint8_t far*) CGA_BASE_VRAM_ADDRESS;

	VRAMptr += (y >> 1) * BYTES_PER_LINE;
	VRAMptr += (x >> 3);

	if (y & 1)
	{
		VRAMptr += 0x2000;
	}

	uint8_t data = *VRAMptr;
	uint8_t mask = ~(0x80 >> (x & 7));

	while (count--)
	{
		data &= mask;
		x++;
		mask = (mask >> 1) | 0x80;
		if ((x & 7) == 0)
		{
			*VRAMptr++ = data;
			while (count > 8)
			{
				*VRAMptr++ = 0;
				count -= 8;
			}
			mask = ~0x80;
			data = *VRAMptr;
		}
	}

	*VRAMptr = data;
}

void CGADriver::VLine(int x, int y, int count)
{
	uint8_t far* VRAMptr = (uint8_t far*) CGA_BASE_VRAM_ADDRESS;
	uint8_t mask = ~(0x80 >> (x & 7));

	VRAMptr += (y >> 1) * BYTES_PER_LINE;
	VRAMptr += (x >> 3);

	bool oddLine = (y & 1);
	if (oddLine)
	{
		VRAMptr += 0x2000;
	}

	while (count--)
	{
		*VRAMptr &= mask;
		if (oddLine)
		{
			VRAMptr -= (0x2000 - BYTES_PER_LINE);
		}
		else
		{
			VRAMptr += 0x2000;
		}
		oddLine = !oddLine;
	}
}

MouseCursorData* CGADriver::GetCursorGraphic(MouseCursor::Type type)
{
	switch (type)
	{
	default:
	case MouseCursor::Pointer:
		return &CGA_MouseCursor;
	case MouseCursor::Hand:
		return &CGA_MouseCursorHand;
	case MouseCursor::TextSelect:
		return &CGA_MouseCursorTextSelect;
	}
}

int CGADriver::GetGlyphWidth(char c, int fontSize, FontStyle::Type style)
{
	Font* font = GetFont(fontSize);
	if (c >= 32 && c < 128)
	{
		int width = font->glyphWidth[c - 32];
		if (style & FontStyle::Bold)
		{
			width++;
		}
		return width;
	}
	return 0;
}

int CGADriver::GetLineHeight(int fontSize)
{
	return GetFont(fontSize)->glyphHeight + 1;
}

void DrawScrollBarBlock(uint8_t far* ptr, int top, int middle, int bottom);
#pragma aux DrawScrollBarBlock = \
	"cmp bx, 0" \
	"je _startMiddle" \	
	"mov ax, 0xff7f" \
	"_loopTop:" \
	"stosw" \
	"add di, 78" \
	"dec bx"\
	"jnz _loopTop" \
	"_startMiddle:" \
	"mov ax, 0x0360" \
	"_loopMiddle:" \
	"stosw" \
	"add di, 78" \
	"dec cx"\
	"jnz _loopMiddle" \
	"mov ax, 0xff7f" \
	"cmp dx, 0" \
	"je _end" \
	"_loopBottom:" \
	"stosw" \
	"add di, 78" \
	"dec dx"\
	"jnz _loopBottom" \
	"_end:" \
	modify [di ax bx cx dx] \	
parm[es di][bx][cx][dx];


void CGADriver::DrawScrollBar(int position, int size)
{
	position >>= 1;
	size >>= 1;

	uint8_t* VRAM = CGA_BASE_VRAM_ADDRESS + WINDOW_TOP / 2 * BYTES_PER_LINE + (BYTES_PER_LINE - 2);
	DrawScrollBarBlock(VRAM, position, size, (WINDOW_HEIGHT / 2) - position - size);
	VRAM += 0x2000;
	DrawScrollBarBlock(VRAM, position, size, (WINDOW_HEIGHT / 2) - position - size);
}

void CGADriver::DrawTitle(const char* text)
{
	// Black out title bar
	FastMemSet(CGA_BASE_VRAM_ADDRESS, 0x00, BYTES_PER_LINE * TITLE_BAR_HEIGHT / 2);
	FastMemSet(CGA_BASE_VRAM_ADDRESS + 0x2000, 0x00, BYTES_PER_LINE * TITLE_BAR_HEIGHT / 2);

	int textWidth = CGA_RegularFont.CalculateWidth(text);

	scissorY1 = 0;
	scissorY2 = TITLE_BAR_HEIGHT;
	DrawString(text, SCREEN_WIDTH / 2 - textWidth / 2, 0, 1);

	scissorY1 = WINDOW_TOP;
	scissorY2 = WINDOW_BOTTOM;
}

void CGADriver::DrawStatus(const char* text)
{
	// Black out status bar
	FastMemSet(CGA_BASE_VRAM_ADDRESS + BYTES_PER_LINE * STATUS_BAR_Y / 2, 0x00, BYTES_PER_LINE * STATUS_BAR_HEIGHT / 2);
	FastMemSet(CGA_BASE_VRAM_ADDRESS + 0x2000 + BYTES_PER_LINE * STATUS_BAR_Y / 2, 0x00, BYTES_PER_LINE * STATUS_BAR_HEIGHT / 2);

	scissorY1 = SCREEN_HEIGHT - STATUS_BAR_HEIGHT;
	scissorY2 = SCREEN_HEIGHT;
	DrawString(text, 8, SCREEN_HEIGHT - STATUS_BAR_HEIGHT, 1);

	scissorY1 = WINDOW_TOP;
	scissorY2 = WINDOW_BOTTOM;
}

void CGADriver::DrawRect(int x, int y, int width, int height)
{
	HLine(x, y, width);
	HLine(x, y + height - 1, width);
	VLine(x, y + 1, height - 2);
	VLine(x + width - 1, y + 1, height - 2);
}

void CGADriver::DrawButtonRect(int x, int y, int width, int height)
{
	HLine(x + 1, y, width - 2);
	HLine(x + 1, y + height - 1, width - 2);
	VLine(x, y + 1, height - 2);
	VLine(x + width - 1, y + 1, height - 2);
}