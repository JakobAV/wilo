/*** includes ***/
#include <stdio.h>
#include <windows.h>
#include <ctype.h>
#include <conio.h>
#include <Synchapi.h>
#include <string.h>

#include "utils.h"

/*** defines ***/

#define WILO_VERSION "0.0.1"
#define WILO_TAB_STOP 8

#define CTRL_KEY(k) ((k)&0x1f)

enum editorKey
{
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN,
};

/*** data ***/

typedef struct erow
{
  int size;
  int rsize;
  char *chars;
  char *render;
} erow;

struct editorConfig
{
  int cx, cy;
  int rx;
  int rowoff;
  int coloff;
  int screenrows;
  int screencols;
  int numrows;
  erow *row;
  DWORD origInMode;
  DWORD origOutMode;
  HANDLE hStdin;
  HANDLE hStdout;
};

struct editorConfig E;

/*** terminal ***/

int GetLastErrorAsString(char *messageBuffer, int bufferSize)
{
  // Get the error message ID, if any.
  DWORD errorMessageID = GetLastError();
  if (errorMessageID == 0)
  {
    return 0;
  }

  // Ask Win32 to give us the string version of that message ID.
  // The parameters we pass in, tell Win32 to create the buffer that holds the message for us (because we don't yet know how long the message string will be).
  size_t size = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                               NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), messageBuffer, bufferSize, NULL);
  return errorMessageID;
}

void die(const char *s)
{
  char message[1094];
  printf("%s\n", s);
  int errorId = GetLastErrorAsString(message, 1094);
  printf("%s(%d): %s\n", s, errorId, message);
  exit(1);
}

void disableRawMode()
{
  if (!(SetConsoleMode(E.hStdin, E.origInMode) && SetConsoleMode(E.hStdout, E.origOutMode) && SetConsoleCtrlHandler(NULL, FALSE)))
    die("disableRawMode");
}

void enableRawMode()
{
  atexit(disableRawMode);
  GetConsoleMode(E.hStdin, &E.origInMode);
  GetConsoleMode(E.hStdout, &E.origOutMode);
  DWORD rawIn = E.origInMode;
  rawIn &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
  rawIn |= (DISABLE_NEWLINE_AUTO_RETURN);
  DWORD rawOut = E.origOutMode;
  rawOut &= ~(ENABLE_WRAP_AT_EOL_OUTPUT | ENABLE_LVB_GRID_WORLDWIDE);
  rawOut |= (ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN | ENABLE_PROCESSED_OUTPUT);
  if (!SetConsoleMode(E.hStdin, rawIn))
    die("enableRawMode In");
  if (!SetConsoleMode(E.hStdout, rawOut))
    die("enableRawMode Out");
  if (!SetConsoleCtrlHandler(NULL, TRUE))
    die("enableRawMode Ctrl");
}

int read(char *c, int numToRead)
{
  static char backBuffer[64];
  static int bufferLength = 0;
  static int nextByte = 0;
  if (bufferLength > nextByte)
  {
    *c = backBuffer[nextByte++];
    return 1;
  }
  else
  {
    bufferLength = 0;
    nextByte = 0;
  }
  if (WaitForSingleObject(E.hStdin, 100) == WAIT_OBJECT_0)
  {
    INPUT_RECORD r[64];
    DWORD read;

    if (!ReadConsoleInput(E.hStdin, r, 64, &read))
      return -1;
    // TODO: Add repeater support
    for (int i = 0; i < read; i++)
    {
      switch (r[i].EventType)
      {
      case KEY_EVENT:
        if (!r[i].Event.KeyEvent.bKeyDown)
        {
          continue;
        }
        switch (r[i].Event.KeyEvent.wVirtualKeyCode)
        {
        case VK_UP:
          backBuffer[bufferLength++] = '\x1b';
          backBuffer[bufferLength++] = '[';
          backBuffer[bufferLength++] = 'A';
          break;
        case VK_DOWN:
          backBuffer[bufferLength++] = '\x1b';
          backBuffer[bufferLength++] = '[';
          backBuffer[bufferLength++] = 'B';
          break;
        case VK_RIGHT:
          backBuffer[bufferLength++] = '\x1b';
          backBuffer[bufferLength++] = '[';
          backBuffer[bufferLength++] = 'C';
          break;
        case VK_LEFT:
          backBuffer[bufferLength++] = '\x1b';
          backBuffer[bufferLength++] = '[';
          backBuffer[bufferLength++] = 'D';
          break;
        case VK_PRIOR:
          backBuffer[bufferLength++] = '\x1b';
          backBuffer[bufferLength++] = '[';
          backBuffer[bufferLength++] = '5';
          backBuffer[bufferLength++] = '~';
          break;
        case VK_NEXT:
          backBuffer[bufferLength++] = '\x1b';
          backBuffer[bufferLength++] = '[';
          backBuffer[bufferLength++] = '6';
          backBuffer[bufferLength++] = '~';
          break;
        case VK_HOME:
          backBuffer[bufferLength++] = '\x1b';
          backBuffer[bufferLength++] = '[';
          backBuffer[bufferLength++] = '1';
          backBuffer[bufferLength++] = '~';
          break;
        case VK_END:
          backBuffer[bufferLength++] = '\x1b';
          backBuffer[bufferLength++] = '[';
          backBuffer[bufferLength++] = '4';
          backBuffer[bufferLength++] = '~';
          break;
        case VK_DELETE:
          backBuffer[bufferLength++] = '\x1b';
          backBuffer[bufferLength++] = '[';
          backBuffer[bufferLength++] = '3';
          backBuffer[bufferLength++] = '~';
          break;
        default:
          backBuffer[bufferLength++] = r[i].Event.KeyEvent.uChar.AsciiChar;
        }
      }
    }
    if (bufferLength > nextByte)
    {
      *c = backBuffer[nextByte++];
      return 1;
    }
    else
    {
      bufferLength = 0;
      nextByte = 0;
    }
  }
  return 0;
}

int write(char *buff, int bytesToWrite)
{
  DWORD bytesWritten;
  if ((WriteConsole(E.hStdout, buff, bytesToWrite, &bytesWritten, NULL) == 0))
    return -1;
  return bytesWritten;
}

int editorReadKey()
{
  int nread;
  char c;
  while ((nread = read(&c, 1)) != 1)
  {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }
  if (c == '\x1b')
  {
    char seq[3];
    seq[2] = '\0';
    if (read(&seq[0], 1) != 1)
      return '\x1b';
    if (read(&seq[1], 1) != 1)
      return '\x1b';

    if (seq[0] == '[')
    {
      if (seq[1] >= '0' && seq[1] <= '9')
      {
        if (read(&seq[2], 1) != 1)
          return '\x1b';
        if (seq[2] == '~')
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
      else
      {
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
    }
    else if (seq[0] == 'O')
    {
      switch (seq[1])
      {
      case 'H':
        return HOME_KEY;
      case 'F':
        return END_KEY;
      }
    }
    return '\x1b';
  }
  else
  {
    return c;
  }
}

int getCursorPosition(int *rows, int *cols)
{
  char buf[32];
  unsigned int i = 0;

  if (write("\x1b[6n", 4) != 4)
    return -1;

  while (i < sizeof(buf) - 1)
  {
    if (read(&buf[i], 1) != 1)
      break;
    if (buf[i] == 'R')
      break;
    i++;
  }
  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[')
    return -1;
  if (sscanf_s(&buf[2], "%d;%d", rows, cols) != 2)
    return -1;

  return 0;
}

int getWindowSize(int *rows, int *cols)
{
  CONSOLE_SCREEN_BUFFER_INFO consoleScreenBufferInfo;
  if (GetConsoleScreenBufferInfo(E.hStdout, &consoleScreenBufferInfo) == 0 || consoleScreenBufferInfo.srWindow.Right <= consoleScreenBufferInfo.srWindow.Left)
  {
    if (write("\x1b[999C\x1b[999B", 12) != 12)
      return -1;
    return getCursorPosition(rows, cols);
  }
  else
  {
    *cols = consoleScreenBufferInfo.srWindow.Right - consoleScreenBufferInfo.srWindow.Left + 1;
    *rows = consoleScreenBufferInfo.srWindow.Bottom - consoleScreenBufferInfo.srWindow.Top + 1;
    return 0;
  }
}

/*** row operations ***/

int editorRowCxtoRx(erow *row, int cx)
{
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++)
  {
    if (row->chars[j] == '\t')
      rx += (WILO_TAB_STOP - 1) - (rx % WILO_TAB_STOP);
    rx++;
  }
  return rx;
}

void editorUpdateRow(erow *row)
{
  int tabs = 0;
  int j;
  for (j = 0; j < row->size; j++)
  {
    if (row->chars[j] == '\t')
      tabs++;
  }
  free(row->render);
  row->render = malloc(row->size + tabs * (WILO_TAB_STOP - 1) + 1);

  int idx = 0;
  for (j = 0; j < row->size; j++)
  {
    if (row->chars[j] == '\t')
    {
      row->render[idx++] = ' ';
      while (idx % WILO_TAB_STOP != 0)
        row->render[idx++] = ' ';
    }
    else
    {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;
}

void editorAppendRow(char *s, size_t len)
{
  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

  int at = E.numrows;
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  editorUpdateRow(&E.row[at]);

  E.numrows++;
}

/*** file i/o ***/

void editorOpen(char *filename)
{
  FILE *fp = NULL;
  fopen_s(&fp, filename, "r");
  if (!fp)
    die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  size_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1)
  {
    while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
      linelen--;

    editorAppendRow(line, linelen);
  }
  free(line);
  fclose(fp);
}

/*** append buffer ***/

typedef struct abuf
{
  char *b;
  int len;
} abuf;

#define ABUF_INIT \
  {               \
    NULL, 0       \
  }

void abAppend(abuf *ab, const char *s, int len)
{
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL)
    return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(abuf *ab)
{
  free(ab->b);
}

/*** output ***/

void editorScroll()
{
  E.rx = 0;
  if (E.cy < E.numrows)
  {
    E.rx = editorRowCxtoRx(&E.row[E.cy], E.cx);
  }

  if (E.cy < E.rowoff)
  {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrows)
  {
    E.rowoff = E.cy - E.screenrows + 1;
  }
  if (E.rx < E.coloff)
  {
    E.coloff = E.rx;
  }
  if (E.rx >= E.coloff + E.screencols)
  {
    E.coloff = E.rx - E.screencols + 1;
  }
}

void editorDrawRows(abuf *ab)
{
  int y;
  for (y = 0; y < E.screenrows; y++)
  {
    int filerow = y + E.rowoff;
    if (filerow >= E.numrows)
    {
      if (E.numrows == 0 && y == E.screenrows / 3)
      {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
                                  "Wilo editor -- version %s", WILO_VERSION);
        if (welcomelen > E.screencols)
          welcomelen = E.screencols;
        int padding = (E.screencols - welcomelen) / 2;
        if (padding)
        {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--)
        {
          abAppend(ab, " ", 1);
        }
        abAppend(ab, welcome, welcomelen);
      }
      else
      {
        abAppend(ab, "~", 1);
      }
    }
    else
    {
      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0)
        len = 0;
      if (len > E.screencols)
        len = E.screencols;
      abAppend(ab, &E.row[filerow].render[E.coloff], len);
    }

    abAppend(ab, "\x1b[K", 3);
    abAppend(ab, "\r\n", 2);
  }
}

void editorDrawStatusBar(abuf *ab)
{
  abAppend(ab, "\x1b[7m", 4);
  int len = 0;
  while (len < E.screencols)
  {
    abAppend(ab, " ", 1);
    len++;
  }
  abAppend(ab, "\x1b[m", 3);
}

void editorRefreshScreen()
{
  editorScroll();

  abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, "\x1b[?25h", 6);

  write(ab.b, ab.len);
  abFree(&ab);
}

void editorClearScreen()
{
  abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);

  for (int y = 0; y < E.screenrows; y++)
  {
    abAppend(&ab, "\x1b[K", 3);
    if (y < E.screenrows - 1)
      abAppend(&ab, "\r\n", 2);
  }
  abAppend(&ab, "\x1b[?25h", 6);
  abAppend(&ab, "\x1b[H", 3);

  write(ab.b, ab.len);
  abFree(&ab);
}

/*** input ***/

void editorMoveCursor(int key)
{
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

  switch (key)
  {
  case ARROW_LEFT:
    if (E.cx != 0)
    {
      E.cx--;
    }
    else if (E.cy > 0)
    {
      E.cy--;
      E.cx = E.row[E.cy].size;
    }
    break;
  case ARROW_RIGHT:
    if (row && E.cx < row->size)
    {
      E.cx++;
    }
    else if (row && E.cx == row->size)
    {
      E.cy++;
      E.cx = 0;
    }
    break;
  case ARROW_UP:
    if (E.cy != 0)
    {
      E.cy--;
    }
    break;
  case ARROW_DOWN:
    if (E.cy < E.numrows)
    {
      E.cy++;
    }
    break;
  }

  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen)
  {
    E.cx = rowlen;
  }
}

void editorProcessKeyPress()
{
  int c = editorReadKey();

  switch (c)
  {
  case CTRL_KEY('q'):
    editorClearScreen();
    exit(0);
    break;
  case HOME_KEY:
    E.cx = 0;
    break;
  case END_KEY:
    if (E.cy < E.numrows)
      E.cx = E.row[E.cy].size;
    break;
  case PAGE_UP:
  case PAGE_DOWN:
  {
    if (c == PAGE_UP)
    {
      E.cy = E.rowoff;
    }
    else if (c == PAGE_DOWN)
    {
      E.cy = E.rowoff + E.screenrows - 1;
      if (E.cy > E.numrows)
        E.cy = E.numrows;
    }
    int times = E.screenrows;
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
  }
}

/*** init ***/
void initEditor()
{
  E.cx = 0;
  E.cy = 0;
  E.rx = 0;
  E.rowoff = 0;
  E.coloff = 0;
  E.numrows = 0;
  E.row = NULL;

  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");

  E.screenrows -= 1;
  FlushConsoleInputBuffer(E.hStdin);
}

int main(int argc, char *argv[])
{
  E.hStdin = GetStdHandle(STD_INPUT_HANDLE);
  E.hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
  enableRawMode();
  initEditor();
  if (argc >= 2)
  {
    editorOpen(argv[1]);
  }

  while (1)
  {
    editorRefreshScreen();
    editorProcessKeyPress();
  }

  return 0;
}