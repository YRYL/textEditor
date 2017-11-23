/**************************************************************************
*                               Includes                                  *
**************************************************************************/

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <iostream>
#include <time.h>
#include <stdarg.h>

using namespace std;

/**************************************************************************
*                                Defines                                  *
**************************************************************************/

#define KILO_VERSION 		"0.0.1"

#define KILO_TAB 			"    "

#define KILO_STATUS_SIZE 	80

enum EditorKey
{
	BACKSPACE = 127,
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	DEL_KEY,
	HOME_KEY,
	END_KEY,
	PAGE_UP,
	PAGE_DOWN
};

#define CURSOR_TOP_LEFT_CMD     "\x1b[H"
#define CURSOR_BOTTOM_RIGHT_CMD "\x1b[999C\x1b[999B"
#define CURSOR_POSITION_CMD     "\x1b[6n"
#define CLEAR_SCREEN_CMD        "\x1b[2J"
#define HIDE_CURSOR_CMD         "\x1b[?25l"
#define SHOW_CURSOR_CMD         "\x1b[?25h"
#define CLEAR_LINE_CMD          "\x1b[K"
#define INVERT_COLORS_CMD		"\x1b[7m"
#define RESET_COLORS_CMD		"\x1b[m"

/**************************************************************************
*                                Macros                                   *
**************************************************************************/

// Output macros

#define EXEC_CMD(cmdStr) (write(STDOUT_FILENO, cmdStr, sizeof(cmdStr)) == sizeof(cmdStr))

#define CURSOR_TO_TOP_LEFT()        EXEC_CMD(CURSOR_TOP_LEFT_CMD)
#define CURSOR_TO_BOTTOM_RIGHT()    EXEC_CMD(CURSOR_BOTTOM_RIGHT_CMD)
#define CURSOR_POSITION()           EXEC_CMD(CURSOR_POSITION_CMD)

#define CLEAR_SCREEN()                  \
do                                      \
{                                       \
	EXEC_CMD(CLEAR_SCREEN_CMD);         \
	CURSOR_TO_TOP_LEFT();               \
}                                       \
while(0)

// Error macros
#define KILO_SANITY(cond, errFmt, errArgs...) if(!(cond)) KILO_DIE(errFmt, ##errArgs)
#define KILO_DIE(errFmt, errArgs...)                                    \
do                                                                      \
{                                                                       \
	CLEAR_SCREEN();                                                     \
	fprintf(stderr, errFmt ": %s(%d) - (%s, %s, %d)\n", ##errArgs,      \
			strerror(errno), errno, __FILE__, __FUNCTION__, __LINE__);  \
	exit(1);                                                            \
}                                                                       \
while(0)

// Char macros
#define CTRL_KEY(k) ((k) & 0x1f)


/**************************************************************************
*                                 Data                                    *
**************************************************************************/

struct EditorConfig
{
	int cx, cy ,rx;
	int rowOff;
	int colOff;
	int screenRows;
	int screenCols;
	int numRows;
	string* rows;
	string* renderRows;
	string filename;
	char statusMsg[KILO_STATUS_SIZE];
	time_t statusMsgTime;
	struct termios orig_termois;
};

struct EditorConfig config;

/**************************************************************************
*                               Terminal                                  *
**************************************************************************/

void disableRawMode()
{
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &config.orig_termois);
}

void enableRawMode()
{
	KILO_SANITY(tcgetattr(STDIN_FILENO, &config.orig_termois) == 0, "tcgetattr Failed!!!");
	atexit(disableRawMode);

	struct termios raw = config.orig_termois;

	raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
	raw.c_iflag &= ~(BRKINT | IXON | ICRNL | INPCK | ISTRIP);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	KILO_SANITY(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0, "tcsetattr Failed!!!");
}


int editorReadSequenceKey()
{
	char seq[3];
	memset(seq, 0, sizeof(seq));

	if (read(STDIN_FILENO, &seq[0], 1) != 1 ||
		read(STDIN_FILENO, &seq[1], 1) != 1)
	{
		return '\x1b';
	}
	if(seq[0] == '[')
	{
		// Number escape sequence
		if (seq[1] >= '0' && seq[1] <= '9')
		{
			if (read(STDIN_FILENO, &seq[2], 1) != 1)
			{
				return '\x1b';
			}
			if(seq[2] == '~')
			{
				switch (seq[1])
				{
					case '1':
						return HOME_KEY;
					case '3':
						return DEL_KEY;
					case '4':
						return END_KEY;
					case '5':
						return PAGE_UP;
					case '6':
						return PAGE_DOWN;
					case '7':
						return HOME_KEY;
					case '8':
						return END_KEY;
				}
			}
		}

		// Letter escape sequence
		switch (seq[1])
		{
			case 'A':
				return ARROW_UP;
			case 'B':
				return ARROW_DOWN;
			case 'C':
				return ARROW_RIGHT;
			case 'D':
				return ARROW_LEFT;
			case 'H':
				return HOME_KEY;
			case 'F':
				return END_KEY;
		}
	}
	else if (seq[0] == 'O')
	{
		switch(seq[1])
		{
			case 'H':
				return HOME_KEY;
			case 'F':
				return END_KEY;
		}
	}
	return '\x1b';
}


int editorReadKey()
{
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
	{
		KILO_SANITY(nread != -1 || errno == EAGAIN, "read from STDIN Failed!!!");
	}

	if (c == '\x1b')
	{
		return editorReadSequenceKey();
	}
	else
	{
		return c;
	}
}


int getCursorPosition(int* rows, int* cols)
{
	char buf[32];
	memset(buf, 0, sizeof(buf));
	uint i = 0;

	if(!CURSOR_POSITION())
		return -1;
	while(i < sizeof(buf) - 2)
	{
		if(read(STDIN_FILENO, &buf[i], 1) != 1 || buf[i] == 'R')
		{
			break;
		}
		++i;
	}
	buf[i] = '\0';

	if(buf[0] != '\x1b' || buf[1] != '[')
		return -1;
	if(sscanf(&buf[2], "%d;%d", rows, cols) != 2)
		return -1;

	return 0;
}

int getWindowSize(int* rows, int* cols)
{
	struct winsize ws;
	ws.ws_col = 0;
	ws.ws_row = 0;

	if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
	{
		if(!CURSOR_TO_BOTTOM_RIGHT())
			return -1;
		return getCursorPosition(rows, cols);
	}
	else
	{
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}

/**************************************************************************
*                                File i/o                                 *
**************************************************************************/

void reallocStringArray(string*& array, int currSize, int newSize)
{
	string* tmpRows = new string[newSize];
	KILO_SANITY(tmpRows != nullptr, "Couldn't allocate array");
	if (array != nullptr)
	{
		if (currSize <= newSize)
		{
			for (int i = 0; i < currSize; ++i)
			{
				tmpRows[i] = array[i];
			}
		}
		else
		{
			for (int i = 0; i < newSize; ++i)
			{
				tmpRows[i] = array[i];
			}
		}
		delete[] array;
	}
	array = tmpRows;
}

int editorRowCxToRx(const string& row, int cx)
{
	int rx = 0;
	for (int j = 0; j < cx; ++j)
	{
		if(row[j] == '\t')
		{
			rx += (sizeof(KILO_TAB) - 1) - 1;
		}
		++rx;
	}

	return rx;
}

void editorUpdateRow(uint rowIdx)
{
	string rowStr(config.rows[rowIdx]);
	size_t index = 0;
	while (index != string::npos)
	{
		index = rowStr.find("\t", index);
		if (index != string::npos)
		{    
			rowStr.replace(index, 1, KILO_TAB);
			index += sizeof(KILO_TAB) - 1;
		}
	}

	config.renderRows[rowIdx] = rowStr;
}

void editorAppendRow(const char* row, size_t len)
{
	reallocStringArray(config.rows, config.numRows, config.numRows + 1);
	reallocStringArray(config.renderRows, config.numRows, config.numRows + 1);
	config.rows[config.numRows] = string(row, len);
	editorUpdateRow(config.numRows);

	++config.numRows;
}

void editorRowInsertChar(uint rowIdx, int at, const char c)
{
	if (at < 0 || at > static_cast<int>(config.rows[rowIdx].length()))
		at = config.rows[rowIdx].length();
	config.rows[rowIdx].insert(at, 1, c);
	editorUpdateRow(rowIdx);
}

void editorOpen(char* filename)
{
	config.filename = filename;
	KILO_SANITY(filename != nullptr, "Filename is NULL!!!");
	FILE* fp = fopen(filename, "r");
	KILO_SANITY(fp, "Open of %s Failed!!!", filename);
	char* line = nullptr;
	size_t linecap = 0;
	ssize_t linelen = 0;
	while((linelen = getline(&line, &linecap, fp)) != -1)
	{
			while (linelen > 0 && (line[linelen - 1] == '\n' ||
				   line[linelen - 1] == '\r'))
			{
				--linelen;
			}
			line[linelen] = '\0';
			editorAppendRow(line, linelen);
	}
	
	free(line);
	fclose(fp);
}

/**************************************************************************
*                            Editor Operations                            *
**************************************************************************/

void editorInsertChar(const char c)
{
	if (config.cy == config.numRows)
	{
		editorAppendRow("", 0);
	}
	editorRowInsertChar(config.cy, config.cx, c);
	config.cx++;
}


/**************************************************************************
*                                 Input                                   *
**************************************************************************/

void editorMoveCursor(int key)
{
	string* row = (config.cy >= config.numRows) ? nullptr : &config.rows[config.cy];
	switch(key)
	{
		case ARROW_LEFT:
			if (config.cx > 0)
			{
				config.cx--;
			}
			else if (config.cy > 0)
			{
				config.cy--;
				config.cx = config.rows[config.cy].length();
			}
			break;
		case ARROW_RIGHT:
			if (row && config.cx < static_cast<int>(row->length()))
			{
				config.cx++;
			}
			else if (row && config.cx == static_cast<int>(row->length()))
			{
				config.cy++;
				config.cx = 0;
			}
			break;
		case ARROW_UP:
			if (config.cy > 0)
			{
				config.cy--;
			}
			break;
		case ARROW_DOWN:
			if (config.cy < config.numRows)
			{
				config.cy++;
			}
			break;
	}

	row = (config.cy >= config.numRows) ? nullptr : &config.rows[config.cy];
	int rowlen = row ? row->length() : 0;
	if (config.cx > rowlen)
	{
		config.cx = rowlen;
	}
}


void editorProcessKeypress()
{
	int c = editorReadKey();

	switch(c)
	{
		case '\r':
			/* TODO */
			break;

		case CTRL_KEY('q'):
			CLEAR_SCREEN();
			exit(0);
			break;

		case HOME_KEY:
			config.cx = 0;
			break;
		case END_KEY:
			config.cx = config.rows[config.cy].length();
			break;

		case BACKSPACE:
		case CTRL_KEY('h'):
		case DEL_KEY:
			/* TODO */
			break;

		case PAGE_UP:
		case PAGE_DOWN:
			{
				if (c == PAGE_UP)
				{
					config.cy = config.rowOff;
				}
				else
				{
					config.cy = config.rowOff + config.screenRows - 1;
					if (config.cy > config.numRows)
					{
						config.cy = config.numRows;
					}
				}
				int times = config.screenRows;
				while (times--)
					editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
			}
			break;

		case ARROW_LEFT:
		case ARROW_RIGHT:
		case ARROW_UP:
		case ARROW_DOWN:
			editorMoveCursor(c);
			break;

		case CTRL_KEY('l'):
		case '\x1b':
			break;

		default:
			editorInsertChar(c);
			break;
	}
}

/**************************************************************************
*                                Output                                   *
**************************************************************************/

void editorScroll()
{
	config.rx = 0;
	if (config.cy < config.numRows)
	{
		config.rx = editorRowCxToRx(config.rows[config.cy], config.cx);
	}

	if (config.cy < config.rowOff)
	{
		config.rowOff = config.cy;
	}
	if (config.cy >= config.rowOff + config.screenRows)
	{
		config.rowOff = config.cy - config.screenRows + 1;
	}

	if (config.rx < config.colOff)
	{
		config.colOff = config.rx;
	}
	if (config.rx >= config.colOff + config.screenCols)
	{
		config.colOff = config.rx - config.screenCols + 1;
	}
}

void editorDrawRows(string& ab)
{
	for (int i = 0; i < config.screenRows; ++i)
	{
		int fileRow = i + config.rowOff;
		if (fileRow >= config.numRows)
		{
			if (config.numRows == 0 && i == config.screenRows / 3)
			{
				char welcome[80];
				int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);
				if (welcomelen > config.screenCols)
					welcomelen = config.screenCols;
				int padding = (config.screenCols - welcomelen) / 2;
				if(padding)
				{
					ab.append("~");
					--padding;
				}
				while(padding--)
				{
					ab.append(" ");
				}
				ab.append(welcome, welcomelen);
			}
			else
			{
				ab.append("~");
			}
		}
		else
		{
			int len = config.renderRows[fileRow].length() - config.colOff;
			if (len < 0)
				len = 0;
			if (len > config.screenCols)
				len = config.screenCols;
			ab.append(&(config.renderRows[fileRow].c_str()[config.colOff]), len);
		}

		ab.append(CLEAR_LINE_CMD);
		ab.append("\r\n");
	}
}

void editorDrawStatusBar(string& ab)
{
	ab.append(INVERT_COLORS_CMD);
	char status[KILO_STATUS_SIZE], rstatus[KILO_STATUS_SIZE];
	memset(status, 0, KILO_STATUS_SIZE);
	memset(rstatus, 0, KILO_STATUS_SIZE);
	int len = snprintf(status, KILO_STATUS_SIZE, "%.20s - %d lines",
					   config.filename.c_str(), config.numRows);
	int rlen = snprintf(rstatus, KILO_STATUS_SIZE, "%d/%d", config.cy + 1, config.numRows);
	if (len > config.screenCols)
		len = config.screenCols;
	ab.append(status);
	while (len < config.screenCols)
	{
		if (config.screenCols - len == rlen)
		{
			ab.append(rstatus);
			break;
		}
		else
		{
			ab.append(" ");
			++len;
		}
	}
	ab.append(RESET_COLORS_CMD);
	ab.append("\r\n");
}

void editorDrawMessageBar(string& ab)
{
	ab.append(CLEAR_LINE_CMD);
	int msglen = strnlen(config.statusMsg, KILO_STATUS_SIZE - 1);
	if (msglen > config.screenCols)
		msglen = config.screenCols;
	if (msglen && time(NULL) - config.statusMsgTime < 5)
		ab.append(config.statusMsg, msglen);
}

void editorRefreshScreen()
{
	editorScroll();

	string ab = "";
	ab.append(HIDE_CURSOR_CMD);
	ab.append(CURSOR_TOP_LEFT_CMD);

	editorDrawRows(ab);
	editorDrawStatusBar(ab);
	editorDrawMessageBar(ab);

	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (config.cy - config.rowOff) + 1, (config.rx - config.colOff) + 1);
	ab.append(buf, strlen(buf));

	ab.append(SHOW_CURSOR_CMD);

	write(STDOUT_FILENO, ab.c_str(), ab.length());
}

void editorSetStatusMsg(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(config.statusMsg, KILO_STATUS_SIZE, fmt, ap);
	va_end(ap);
	config.statusMsgTime = time(NULL);
}

/**************************************************************************
*                                 Init                                    *
**************************************************************************/

void initEditor()
{
	config.cx       = 0;
	config.rx       = 0;
	config.cy       = 0;
	config.numRows  = 0;
	config.rowOff   = 0;
	config.colOff   = 0;
	config.filename = "[No Name]";
	memset(config.statusMsg, 0, KILO_STATUS_SIZE);
	config.statusMsgTime = 0;

	KILO_SANITY(getWindowSize(&config.screenRows, &config.screenCols) != -1, "getWindowSize Failed!!!");
	config.screenRows -= 2;
}

int main(int argc, char* argv[])
{
	enableRawMode();
	initEditor();
	if (argc >= 2)
	{
		editorOpen(argv[1]);
	}

	editorSetStatusMsg("HELP: Ctrl-Q = quit");

	while (true)
	{
		editorRefreshScreen();
		editorProcessKeypress();
	}
	return 0;
}