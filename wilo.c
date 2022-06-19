/*
  A simple cmd editor.
*/

/*** includes ***/
#include <stdio.h>
#include <stdarg.h>
#include <windows.h>
#include <ctype.h>
#include <conio.h>
#include <Synchapi.h>
#include <string.h>
#include <time.h>

#include "utils.h"

/*** defines ***/

#define WILO_VERSION "0.0.1"
#define WILO_TAB_STOP 4
#define WILO_QUIT_TIMES 3

#define CTRL_KEY(k) ((k)&0x1f)

enum editorKey
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
  PAGE_DOWN,
};

enum editorHighlight
{
  HL_NORMAL = 0,
  HL_COMMENT,
  HL_MLCOMMENT,
  HL_FUNCTION,
  HL_KEYWORD1,
  HL_KEYWORD2,
  HL_STRING,
  HL_NUMBER,
  HL_MATCH,
};

#define HL_HIGHLIGHT_NUMBERS (1 << 0)
#define HL_HIGHLIGHT_STRINGS (1 << 1)
#define HL_HIGHLIGHT_FUNCTIONS (1 << 2)

/*** data ***/

typedef struct editorSyntax
{
  char *filetype;
  char **filematch;
  char **keywords;
  char *singleline_comment_start;
  char *multiline_comment_start;
  char *multiline_comment_end;
  int flags;
} editorSyntax;

typedef struct erow
{
  int idx;
  int size;
  int rsize;
  char *chars;
  char *render;
  unsigned char *hl;
  int hl_open_comment;
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
  int dirty;
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;
  editorSyntax *syntax;
  DWORD origInMode;
  DWORD origOutMode;
  HANDLE hStdin;
  HANDLE hStdout;
};

struct editorConfig E;

/*** filetypes ***/

char *C_HL_extensions[] = {".c", ".h", ".cpp", NULL};
char *C_HL_keywords[] = {
    "switch", "if", "while", "for", "break", "continue", "return", "else",
    "struct", "union", "typedef", "static", "enum", "class", "case", "default",
    "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
    "void|", NULL};

editorSyntax HLDB[] = {
    {
        "c",
        C_HL_extensions,
        C_HL_keywords,
        "//",
        "/*",
        "*/",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS | HL_HIGHLIGHT_FUNCTIONS,
    },
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/*** prototypes ***/

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));

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

int writeFile(char *filename, char *buf, int len)
{
  HANDLE hFile = CreateFileA(filename,
                             GENERIC_WRITE, FILE_SHARE_READ,
                             NULL,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                             NULL);

  if (hFile == INVALID_HANDLE_VALUE)
    return -1;

  int numberOfBytesWritten;
  if (!WriteFile(hFile, buf, len, &numberOfBytesWritten, NULL))
  {
    CloseHandle(hFile);
    return -1;
  }

  // NOTE: If this fails there could be junk data at the end of the file
  if (!SetEndOfFile(hFile))
  {
    CloseHandle(hFile);
    return -1;
  }

  CloseHandle(hFile);
  return numberOfBytesWritten;
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

/*** syntax highlighting ***/

int is_seperator(int c)
{
  return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];:", c) != NULL;
}

void editorUpdateSyntax(erow *row)
{
  row->hl = realloc(row->hl, row->rsize);
  memset(row->hl, HL_NORMAL, row->rsize);

  if (E.syntax == NULL)
    return;

  char **keywords = E.syntax->keywords;

  char *scs = E.syntax->singleline_comment_start;
  char *mcs = E.syntax->multiline_comment_start;
  char *mce = E.syntax->multiline_comment_end;

  int scs_len = scs ? strlen(scs) : 0;
  int mcs_len = mcs ? strlen(mcs) : 0;
  int mce_len = mce ? strlen(mce) : 0;

  int prev_sep = 1;
  int in_string = 0;
  int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);

  int i = 0;
  while (i < row->rsize)
  {
    char c = row->render[i];
    unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

    if (scs_len && !in_string && !in_comment)
    {
      if (!strncmp(&row->render[i], scs, scs_len))
      {
        memset(&row->hl[i], HL_COMMENT, row->rsize - i);
        break;
      }
    }

    if (mcs_len && mce_len && !in_string)
    {
      if (in_comment)
      {
        row->hl[i] = HL_MLCOMMENT;
        if (!strncmp(&row->render[i], mce, mce_len))
        {
          memset(&row->hl[i], HL_MLCOMMENT, mce_len);
          i += mce_len;
          in_comment = 0;
          prev_sep = 1;
        }
        else
        {
          i++;
          continue;
        }
      }
      else if (!strncmp(&row->render[i], mcs, mcs_len))
      {
        memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
        i += mcs_len;
        in_comment = 1;
        continue;
      }
    }

    if (E.syntax->flags & HL_HIGHLIGHT_STRINGS)
    {
      if (in_string)
      {
        row->hl[i] = HL_STRING;
        if (c == '\\' && i + 1 < row->rsize)
        {
          row->hl[i + 1] = HL_STRING;
          i += 2;
          continue;
        }
        if (c == in_string)
          in_string = 0;
        i++;
        prev_sep = 1;
        continue;
      }
      else
      {
        if (c == '"' || c == '\'')
        {
          in_string = c;
          row->hl[i] = HL_STRING;
          i++;
          continue;
        }
      }
    }

    if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS)
    {

      if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) || (c == '.' && prev_hl == HL_NUMBER))
      {
        row->hl[i] = HL_NUMBER;
        i++;
        prev_sep = 0;
        continue;
      }
    }
    if (prev_sep)
    {
      int j;
      for (j = 0; keywords[j]; j++)
      {
        int klen = strlen(keywords[j]);
        int kw2 = keywords[j][klen - 1] == '|';
        if (kw2)
          klen--;

        if (!strncmp(&row->render[i], keywords[j], klen) && is_seperator(row->render[i + klen]))
        {
          memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
          i += klen;
          break;
        }
      }
      if (keywords[j] != NULL)
      {
        prev_sep = 0;
        continue;
      }
    }

    if (E.syntax->flags & HL_HIGHLIGHT_FUNCTIONS)
    {
      if (row->render[i] == '(')
      {
        for (int j = i - 1; j >= 0; j--)
        {
          int c = row->render[j];
          if (isspace(c) || c == '\0' || strchr("!(", c) != NULL)
            break;
          row->hl[j] = HL_FUNCTION;
        }
      }
    }

    prev_sep = is_seperator(c);
    i++;
  }

  int changed = (row->hl_open_comment != in_comment);
  row->hl_open_comment = in_comment;
  if (changed && row->idx + 1 < E.numrows)
    editorUpdateSyntax(&E.row[row->idx + 1]);
}

int editorSyntaxToColor(int hl)
{
  switch (hl)
  {
  case HL_COMMENT:
  case HL_MLCOMMENT:
    return 36;
  case HL_STRING:
    return 35;
  case HL_FUNCTION:
  case HL_KEYWORD1:
    return 33;
  case HL_KEYWORD2:
    return 32;
  case HL_NUMBER:
    return 31;
  case HL_MATCH:
    return 34;
  default:
    return 37;
  }
}

void editorSelectSyntaxHighlight()
{
  E.syntax = NULL;
  if (E.filename == NULL)
    return;

  char *ext = strrchr(E.filename, '.');

  for (unsigned int j = 0; j < HLDB_ENTRIES; j++)
  {
    editorSyntax *s = &HLDB[j];
    unsigned int i = 0;
    while (s->filematch[i])
    {
      int is_ext = (s->filematch[i][0] == '.');
      if ((is_ext && ext && !strcmp(ext, s->filematch[i])) || (!is_ext && strstr(E.filename, s->filematch[i])))
      {
        E.syntax = s;

        int filerow;
        for (filerow = 0; filerow < E.numrows; filerow++)
        {
          editorUpdateSyntax(&E.row[filerow]);
        }
        return;
      }
      i++;
    }
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

int editorRowRxToCx(erow *row, int rx)
{
  int cur_rx = 0;
  int cx;
  for (cx = 0; cx < row->size; cx++)
  {
    if (row->chars[cx] == '\t')
      cur_rx += (WILO_TAB_STOP - 1) - (cur_rx % WILO_TAB_STOP);
    cur_rx++;

    if (cur_rx > rx)
      return cx;
  }
  return cx;
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

  editorUpdateSyntax(row);
}

void editorInsertRow(int at, char *s, size_t len)
{
  if (at < 0 || at > E.numrows)
    return;

  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));
  for (int j = at + 1; j <= E.numrows; j++)
    E.row[j].idx++;

  E.row[at].idx = at;

  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  E.row[at].hl = NULL;
  E.row[at].hl_open_comment = 0;
  editorUpdateRow(&E.row[at]);

  E.numrows++;
  E.dirty++;
}

void editorFreeRow(erow *row)
{
  free(row->render);
  free(row->chars);
  free(row->hl);
}

void editorDelRow(int at)
{
  if (at < 0 || at >= E.numrows)
    return;
  editorFreeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
  for (int j = at; j < E.numrows - 1; j++)
    E.row[j].idx--;
  E.numrows--;
  E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c)
{
  if (at < 0 || at > row->size)
    at = row->size;
  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  editorUpdateRow(row);
  E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len)
{
  printf("editorRowAppendString\r\n");
  row->chars = realloc(row->chars, row->size + len + 1);
  printf("editorRowAppendString\r\n");
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  printf("editorRowAppendString\r\n");
  E.dirty++;
}

void editorRowDelChar(erow *row, int at)
{
  if (at < 0 || at >= row->size)
    return;

  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  editorUpdateRow(row);
  E.dirty++;
}

/*** editor operations ***/

void editorInsertChar(int c)
{
  if (E.cy == E.numrows)
  {
    editorInsertRow(E.numrows, "", 0);
  }
  editorRowInsertChar(&E.row[E.cy], E.cx, c);
  E.cx++;
}

void editorInsertNewLine()
{
  if (E.cx == 0)
  {
    editorInsertRow(E.cy, "", 0);
  }
  else
  {
    erow *row = &E.row[E.cy];
    editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
    row = &E.row[E.cy];
    row->size = E.cx;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
  }
  E.cy++;
  E.cx = 0;
}

void editorDelChar()
{
  if (E.cy == E.numrows)
    return;
  if (E.cx == 0 && E.cy == 0)
    return;

  erow *row = &E.row[E.cy];
  if (E.cx > 0)
  {
    editorRowDelChar(row, E.cx - 1);
    E.cx--;
  }
  else
  {
    E.cx = E.row[E.cy - 1].size;
    editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
    editorDelRow(E.cy);
    E.cy--;
  }
}

/*** file i/o ***/

char *editorRowsToString(int *buflen)
{
  int totlen = 0;
  int j;
  for (j = 0; j < E.numrows; j++)
  {
    totlen += E.row[j].size + 1;
  }
  *buflen = totlen;

  char *buf = malloc(totlen);
  char *p = buf;
  for (j = 0; j < E.numrows; j++)
  {
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;
  }

  return buf;
}

void editorOpen(char *filename)
{
  free(E.filename);
  E.filename = _strdup(filename);

  editorSelectSyntaxHighlight();

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

    editorInsertRow(E.numrows, line, linelen);
  }
  free(line);
  fclose(fp);
  E.dirty = 0;
}

void editorSave()
{
  if (E.filename == NULL)
  {
    E.filename = editorPrompt("Save as: %s", NULL);
    if (E.filename == NULL)
    {
      editorSetStatusMessage("Save aborted");
      return;
    }
    editorSelectSyntaxHighlight();
  }

  int len;
  char *buf = editorRowsToString(&len);

#if 0
  FILE *fp = NULL;
  fopen_s(&fp, E.filename, "w");
  fprintf_s(fp, buf, len);
  fclose(fp);
  free(buf);
#else
  if (writeFile(E.filename, buf, len) == len)
  {
    editorSetStatusMessage("%d bytes written to disk", len);
    E.dirty = 0;
    return;
  }
  char msg[1024];
  int code = GetLastErrorAsString(msg, sizeof(msg));
  if (code == 0)
    strcpy_s(msg, 1024, "Unknown error");
  editorSetStatusMessage("Can't save! I/O error: %s", msg);
#endif
}

/*** find ***/

void editorFindCallback(char *query, int key)
{
  static int last_match = -1;
  static int direction = 1;

  static int saved_hl_line;
  static char *saved_hl = NULL;

  if (saved_hl)
  {
    memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);
    free(saved_hl);
    saved_hl = NULL;
  }

  if (key == '\r' || key == '\x1b')
  {
    last_match = -1;
    return;
  }
  else if (key == ARROW_RIGHT || key == ARROW_DOWN)
  {
    direction = 1;
  }
  else if (key == ARROW_LEFT || key == ARROW_UP)
  {
    direction = -1;
  }
  else
  {
    last_match = -1;
    direction = 1;
  }

  if (last_match == -1)
    direction = 1;
  int current = last_match;
  int i;
  for (i = 0; i < E.numrows; i++)
  {
    current += direction;
    if (current == -1)
      current = E.numrows - 1;
    else if (current == E.numrows)
      current = 0;

    erow *row = &E.row[current];
    // TODO: Make case insensitive
    char *match = strstr(row->render, query);
    if (match)
    {
      last_match = current;
      E.cy = current;
      E.cx = editorRowRxToCx(row, match - row->render);
      E.rowoff = E.numrows;

      saved_hl_line = current;
      saved_hl = malloc(row->rsize);
      memcpy(saved_hl, row->hl, row->rsize);
      memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
      break;
    }
  }
}

void editorFind()
{
  int saved_cx = E.cx;
  int saved_cy = E.cy;
  int saved_coloff = E.coloff;
  int saved_rowoff = E.rowoff;

  char *query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)", editorFindCallback);
  if (query)
  {
    free(query);
  }
  else
  {
    E.cx = saved_cx;
    E.cy = saved_cy;
    E.coloff = saved_coloff;
    E.rowoff = saved_rowoff;
  }
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

      char *c = &E.row[filerow].render[E.coloff];
      unsigned char *hl = &E.row[filerow].hl[E.coloff];
      int current_color = -1;
      int current_color_inverted = 0;
      int j;
      for (j = 0; j < len; j++)
      {
        if (iscntrl(c[j]))
        {
          char sym = (c[j] <= 26) ? '@' + c[j] : '?';
          abAppend(ab, "\x1b[7m", 4);
          abAppend(ab, &sym, 1);
          abAppend(ab, "\x1b[m", 3);
          if (current_color != -1)
          {
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color);
            abAppend(ab, buf, clen);
          }
        }
        else if (hl[j] == HL_NORMAL)
        {
          if (current_color_inverted)
          {
            current_color_inverted = 0;
            abAppend(ab, "\x1b[m", 3);
          }
          if (current_color != -1)
          {
            abAppend(ab, "\x1b[39m", 5);
            current_color = -1;
          }
          abAppend(ab, &c[j], 1);
        }
        else
        {
          if (hl[j] == HL_MATCH && !current_color_inverted)
          {
            current_color_inverted = 1;
            abAppend(ab, "\x1b[7m", 4);
          }
          else if (hl[j] != HL_MATCH && current_color_inverted)
          {
            current_color_inverted = 0;
            abAppend(ab, "\x1b[m", 3);
          }
          int color = editorSyntaxToColor(hl[j]);
          if (current_color != color)
          {
            current_color = color;
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
            abAppend(ab, buf, clen);
          }
          abAppend(ab, &c[j], 1);
        }
      }
      abAppend(ab, "\x1b[39m", 5);
    }

    abAppend(ab, "\x1b[K", 3);
    abAppend(ab, "\r\n", 2);
  }
}

void editorDrawStatusBar(abuf *ab)
{
  abAppend(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                     E.filename ? E.filename : "[No Name]", E.numrows,
                     E.dirty ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
                      E.syntax ? E.syntax->filetype : "no ft",
                      E.cy + 1, E.numrows);
  if (len > E.screencols)
    len = E.screencols;
  abAppend(ab, status, len);
  while (len < E.screencols)
  {
    if (E.screencols - len == rlen)
    {
      abAppend(ab, rstatus, rlen);
      break;
    }
    else
    {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(abuf *ab)
{
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols)
    msglen = E.screencols;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
    abAppend(ab, E.statusmsg, msglen);
}

void editorSetStatusMessage(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

void editorRefreshScreen()
{
  editorScroll();

  abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);

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
  int rowCount = E.screenrows + 2;
  for (int y = 0; y < rowCount; y++)
  {
    abAppend(&ab, "\x1b[K", 3);
    if (y < rowCount - 1)
      abAppend(&ab, "\r\n", 2);
  }
  abAppend(&ab, "\x1b[?25h", 6);
  abAppend(&ab, "\x1b[H", 3);

  write(ab.b, ab.len);
  abFree(&ab);
}

/*** input ***/

char *editorPrompt(char *prompt, void (*callback)(char *, int))
{
  size_t bufsize = 128;
  char *buf = malloc(bufsize);

  size_t buflen = 0;
  buf[0] = '\0';

  while (1)
  {
    editorSetStatusMessage(prompt, buf);
    editorRefreshScreen();

    int c = editorReadKey();
    if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE)
    {
      if (buflen != 0)
        buf[--buflen] = '\0';
    }
    else if (c == '\x1b')
    {
      editorSetStatusMessage("");
      if (callback)
        callback(buf, c);
      free(buf);
      return NULL;
    }
    else if (c == '\r')
    {
      if (buflen != 0)
      {
        editorSetStatusMessage("");
        if (callback)
          callback(buf, c);
        return buf;
      }
    }
    else if (!iscntrl(c) && c < 128)
    {
      if (buflen == bufsize - 1)
      {
        bufsize *= 2;
        buf = realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }

    if (callback)
      callback(buf, c);
  }
}

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
  static int quit_times = WILO_QUIT_TIMES;

  int c = editorReadKey();
  switch (c)
  {
  case '\r':
    editorInsertNewLine();
    break;
  case CTRL_KEY('q'):
    if (E.dirty && quit_times > 0)
    {
      editorSetStatusMessage("WARNING!!! File has unsaved changes."
                             "Press CTRL-Q %d more times to quit",
                             quit_times);
      quit_times--;
      return;
    }
    editorClearScreen();
    exit(0);
    break;
  case CTRL_KEY('s'):
    editorSave();
    break;
  case HOME_KEY:
    E.cx = 0;
    break;
  case END_KEY:
    if (E.cy < E.numrows)
      E.cx = E.row[E.cy].size;
    break;
  case CTRL_KEY('f'):
    editorFind();
    break;
  case BACKSPACE:
  case CTRL_KEY('h'):
  case DEL_KEY:
    if (c == DEL_KEY)
      editorMoveCursor(ARROW_RIGHT);
    editorDelChar();
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
  case CTRL_KEY('l'):
  case '\x1b':
  case 0:
    break;
  default:
    editorInsertChar(c);
    break;
  }

  quit_times = WILO_QUIT_TIMES;
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
  E.dirty = 0;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;
  E.syntax = NULL;

  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");

  E.screenrows -= 2;
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

  editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

  while (1)
  {
    editorRefreshScreen();
    editorProcessKeyPress();
  }

  return 0;
}
