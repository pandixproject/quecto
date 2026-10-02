#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

typedef struct {
  char *data;
  size_t length, capacity, cursor;
} Buffer;

typedef struct {
  struct termios original;
  int active;
} Terminal;

static int terminal_enter(Terminal *terminal) {
  struct termios raw;
  raw = terminal->original;
  raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
  raw.c_iflag &= (tcflag_t) ~(IXON | ICRNL);
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0)
    return 0;
  terminal->active = 1;
  printf("\033[?1049h");
  return 1;
}

static void terminal_leave(Terminal *terminal) {
  if (!terminal->active)
    return;
  printf("\033[?1049l");
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &terminal->original);
  terminal->active = 0;
}

static int reserve(Buffer *b, size_t n) {
  if (n <= b->capacity)
    return 1;
  size_t cap = b->capacity ? b->capacity : 1024;
  while (cap < n) {
    if (cap > (size_t)-1 / 2)
      return 0;
    cap *= 2;
  }
  char *p = realloc(b->data, cap);
  if (!p)
    return 0;
  b->data = p;
  b->capacity = cap;
  return 1;
}

static int buffer_insert(Buffer *b, char c) {
  if (!reserve(b, b->length + 2))
    return 0;
  memmove(b->data + b->cursor + 1, b->data + b->cursor, b->length - b->cursor);
  b->data[b->cursor++] = c;
  b->length++;
  return 1;
}

static int buffer_delete_before(Buffer *b) {
  if (!b->cursor)
    return 0;
  memmove(b->data + b->cursor - 1, b->data + b->cursor, b->length - b->cursor);
  b->cursor--;
  b->length--;
  return 1;
}

static int buffer_delete_at(Buffer *b) {
  if (b->cursor >= b->length)
    return 0;
  memmove(b->data + b->cursor, b->data + b->cursor + 1,
          b->length - b->cursor - 1);
  b->length--;
  return 1;
}

static int load_file(Buffer *b, const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return errno == ENOENT;
  char chunk[4096];
  size_t n;
  while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) {
    if (!reserve(b, b->length + n)) {
      fclose(f);
      return 0;
    }
    memcpy(b->data + b->length, chunk, n);
    b->length += n;
  }
  int ok = !ferror(f);
  fclose(f);
  return ok;
}

static int save_file(Buffer *b, const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f)
    return 0;
  int ok = fwrite(b->data, 1, b->length, f) == b->length;
  if (fclose(f) != 0)
    ok = 0;
  return ok;
}

static size_t line_start(const Buffer *b, size_t at) {
  while (at && b->data[at - 1] != '\n')
    at--;
  return at;
}
static size_t line_end(const Buffer *b, size_t at) {
  while (at < b->length && b->data[at] != '\n')
    at++;
  return at;
}
static size_t line_number(const Buffer *b, size_t at) {
  size_t number = 1;
  for (size_t i = 0; i < at; i++)
    if (b->data[i] == '\n')
      number++;
  return number;
}

static void move_to_line(Buffer *b, size_t number) {
  size_t at = 0;
  if (!number)
    number = 1;
  while (number > 1 && at < b->length) {
    if (b->data[at++] == '\n')
      number--;
  }
  b->cursor = at;
}

static char *copy_string(const char *s) {
  size_t n = strlen(s) + 1;
  char *copy = malloc(n);
  if (copy)
    memcpy(copy, s, n);
  return copy;
}

static void move_vertical(Buffer *b, int direction, size_t *goal) {
  size_t start = line_start(b, b->cursor), col = b->cursor - start;
  if (*goal == (size_t)-1)
    *goal = col;
  if (direction < 0) {
    if (!start)
      return;
    size_t prev_end = start - 1, prev_start = line_start(b, prev_end);
    size_t end = prev_end;
    if (end - prev_start > *goal)
      end = prev_start + *goal;
    b->cursor = end;
  } else {
    size_t end = line_end(b, b->cursor);
    if (end == b->length)
      return;
    size_t next = end + 1, next_end = line_end(b, next);
    b->cursor = next + ((*goal < next_end - next) ? *goal : next_end - next);
  }
}

static void draw(const Buffer *b, const char *path, int modified, int numbers,
                 const char *message, int command, const char *cmd) {
  struct winsize size = {0};
  size_t cursor_start, top, cursor_row, cursor_col, pos;
  size_t rows, cols, available_rows;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) != 0 || !size.ws_row ||
      !size.ws_col) {
    size.ws_row = 24;
    size.ws_col = 80;
  }
  rows = size.ws_row;
  cols = size.ws_col;
  available_rows = rows > 2 ? rows - 2 : 1;

  cursor_start = line_start(b, b->cursor);
  cursor_col = b->cursor - cursor_start + (numbers ? 6 : 0);
  top = cursor_start;
  cursor_row = 0;
  while (cursor_row + 1 < available_rows && top > 0) {
    top = line_start(b, top - 1);
    cursor_row++;
  }

  printf("\033[?25l\033[2J");
  pos = top;
  for (size_t row = 0; row < available_rows && pos <= b->length; row++) {
    size_t end = line_end(b, pos);
    size_t prefix = numbers ? 6 : 0;
    size_t text_cols = cols > prefix ? cols - prefix : 1;
    size_t text_col = cursor_col - prefix;
    size_t left = row == cursor_row && text_col >= text_cols
                      ? text_col - text_cols + 1
                      : 0;
    size_t count = end > pos + left ? end - pos - left : 0;
    if (count > text_cols)
      count = text_cols;
    printf("\033[%zu;1H", row + 1);
    if (numbers)
      printf("%5zu ", line_number(b, pos));
    if (count)
      fwrite(b->data + pos + left, 1, count, stdout);
    printf("\033[K");
    if (end == b->length)
      break;
    pos = end + 1;
  }

  printf("\033[%zu;1H\033[7m %s%s | %zu chars | %s ", rows - 1, path,
         modified ? " [+]" : "", b->length, command ? "COMMAND" : "INSERT");
  if (message && *message)
    printf("| %s", message);
  printf("\033[K\033[0m");
  if (command) {
    const char *command_text = cmd[0] == ':' ? cmd + 1 : cmd;
    printf("\033[%zu;1H:%s%s\033[K", rows, ":", command_text);
  } else {
    size_t screen_col = cursor_col >= cols ? cols : cursor_col + 1;
    printf("\033[%zu;%zuH", cursor_row + 1, screen_col);
  }
  printf("\033[?25h");
  fflush(stdout);
}

static void handle_command_key(Buffer *b, char **path, unsigned char c,
                               int *modified, int *numbers, size_t *goal,
                               int *command, int *quit, char *cmd,
                               size_t cmd_size, char *message,
                               size_t message_size) {
  size_t n = strlen(cmd);
  if (c == 27) {
    *command = 0;
    cmd[0] = '\0';
  } else if (c == '\r' || c == '\n') {
    char *name = cmd[0] == ':' ? cmd + 1 : cmd;
    while (*name == ' ' || *name == '\t')
      name++;
    char *argument = name;
    while (*argument && *argument != ' ' && *argument != '\t')
      argument++;
    if (*argument) {
      *argument++ = '\0';
      while (*argument == ' ' || *argument == '\t')
        argument++;
    }
    int save_and_quit = !strcmp(name, "wq");
    if (!strcmp(name, "w") || save_and_quit) {
      const char *destination = *argument ? argument : *path;
      if (save_file(b, destination)) {
        if (destination != *path) {
          char *new_path = copy_string(destination);
          if (!new_path) {
            strcpy(message, "Saved, but filename update failed");
            *command = 0;
            cmd[0] = '\0';
            return;
          }
          free(*path);
          *path = new_path;
        }
        *modified = 0;
        strcpy(message, "Saved");
        if (save_and_quit)
          *quit = 1;
      } else
        snprintf(message, message_size, "Save failed: %s", strerror(errno));
    } else if (!strcmp(name, "q!"))
      *quit = 1;
    else if (!strcmp(name, "q")) {
      if (*modified)
        strcpy(message, "Unsaved changes; use :q! or :wq");
      else
        *quit = 1;
    } else if (!strcmp(name, "e")) {
      if (!*argument)
        strcpy(message, "Usage: :e filename");
      else if (*modified)
        strcpy(message, "Unsaved changes; save or use :q! first");
      else {
        Buffer opened = {0};
        if (!load_file(&opened, argument) ||
            !reserve(&opened, opened.length + 1)) {
          free(opened.data);
          snprintf(message, message_size, "Open failed: %s", strerror(errno));
        } else {
          char *new_path = copy_string(argument);
          if (!new_path) {
            free(opened.data);
            strcpy(message, "Open failed: out of memory");
          } else {
            free(b->data);
            *b = opened;
            free(*path);
            *path = new_path;
            *modified = 0;
            *goal = (size_t)-1;
            strcpy(message, "Opened");
          }
        }
      }
    } else if (!strcmp(name, "set")) {
      if (!strcmp(argument, "number")) {
        *numbers = 1;
        strcpy(message, "Line numbers on");
      } else if (!strcmp(argument, "nonumber")) {
        *numbers = 0;
        strcpy(message, "Line numbers off");
      } else
        strcpy(message, "Usage: :set number | :set nonumber");
    } else if (!strcmp(name, "goto")) {
      char *end;
      errno = 0;
      unsigned long long line = strtoull(argument, &end, 10);
      if (!*argument || *argument == '-' || errno || *end || !line ||
          line > (size_t)-1)
        strcpy(message, "Usage: :goto line-number");
      else {
        move_to_line(b, (size_t)line);
        *goal = (size_t)-1;
        strcpy(message, "Moved to line");
      }
    } else if (!strcmp(name, "help")) {
      if (!*argument)
        strcpy(message, ":w [file] :wq [file] :e file :set number|nonumber :goto N :q :q! :help [cmd]");
      else if (!strcmp(argument, "w") || !strcmp(argument, "wq"))
        strcpy(message, "Save current file or to optional filename");
      else if (!strcmp(argument, "e"))
        strcpy(message, "Open file; requires no unsaved changes");
      else if (!strcmp(argument, "set"))
        strcpy(message, "Toggle display with number or nonumber");
      else if (!strcmp(argument, "goto"))
        strcpy(message, "Move cursor to a 1-based line number");
      else if (!strcmp(argument, "q") || !strcmp(argument, "q!"))
        strcpy(message, ":q quits if saved; :q! discards changes and quits");
      else
        strcpy(message, "No help for that command");
    }
    else if (*name)
      strcpy(message, "Unknown command; try :help");
    *command = 0;
    cmd[0] = '\0';
  } else if (c == 127 || c == 8) {
    if (n)
      cmd[n - 1] = '\0';
  } else if (c >= 32 && c < 127 && n < cmd_size - 1) {
    cmd[n] = (char)c;
    cmd[n + 1] = '\0';
  }
}

static void handle_insert_key(Buffer *b, unsigned char c, int *modified,
                              int *command, int *quit, size_t *goal, char *cmd,
                              char *message) {
  if (c == 27) {
    unsigned char seq[2];
    *command = 1;
    cmd[0] = '\0';
    fd_set input;
    struct timeval timeout = {0, 50000};
    FD_ZERO(&input);
    FD_SET(STDIN_FILENO, &input);
    if (select(STDIN_FILENO + 1, &input, NULL, NULL, &timeout) > 0 &&
        read(STDIN_FILENO, &seq[0], 1) == 1) {
      if (seq[0] == '[' && read(STDIN_FILENO, &seq[1], 1) == 1) {
        if (seq[1] == 'A') {
          move_vertical(b, -1, goal);
          *command = 0;
        } else if (seq[1] == 'B') {
          move_vertical(b, 1, goal);
          *command = 0;
        } else if (seq[1] == 'C' && b->cursor < b->length) {
          b->cursor++;
          *goal = (size_t)-1;
          *command = 0;
        } else if (seq[1] == 'D' && b->cursor) {
          b->cursor--;
          *goal = (size_t)-1;
          *command = 0;
        }
      } else if (seq[0] == ':')
        strcpy(cmd, ":");
      else if (seq[0] >= 32 && seq[0] < 127) {
        cmd[0] = (char)seq[0];
        cmd[1] = '\0';
      }
    }
  } else if (c == 3) {
    if (!*modified)
      *quit = 1;
    else
      strcpy(message, "Unsaved changes; use Escape then :q! or :wq");
  } else if (c == 127 || c == 8) {
    if (buffer_delete_before(b))
      *modified = 1;
    *goal = (size_t)-1;
  } else if (c == 4) {
    if (buffer_delete_at(b))
      *modified = 1;
  } else if (c == '\r' || c == '\n') {
    if (buffer_insert(b, '\n'))
      *modified = 1;
    *goal = (size_t)-1;
  } else if (c >= 32 && c != 127) {
    if (buffer_insert(b, (char)c))
      *modified = 1;
    *goal = (size_t)-1;
  }
}

int main(int argc, char **argv) {
  const char *path = NULL;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
      printf("Usage: %s <filename>\nEscape opens command mode; :w saves, :q "
             "quits, :wq saves and quits.\n",
             argv[0]);
      return 0;
    }
    if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version")) {
      puts("quecto text editor v1.1");
      return 0;
    }
    if (argv[i][0] == '-') {
      fprintf(stderr, "Unknown option: %s\n", argv[i]);
      return 1;
    }
    if (path) {
      fputs("Only one filename may be provided\n", stderr);
      return 1;
    }
    path = argv[i];
  }
  if (!path) {
    fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
    return 1;
  }
  char *current_path = copy_string(path);
  if (!current_path) {
    fputs("Out of memory\n", stderr);
    return 1;
  }
  Buffer b = {0};
  if (!load_file(&b, current_path)) {
    perror("Could not read file");
    free(current_path);
    free(b.data);
    return 1;
  }
  if (!reserve(&b, b.length + 1)) {
    fputs("Out of memory\n", stderr);
    free(current_path);
    free(b.data);
    return 1;
  }
  Terminal terminal = {0};
  if (!isatty(STDIN_FILENO) ||
      tcgetattr(STDIN_FILENO, &terminal.original) != 0) {
    fputs("quecto needs an interactive terminal\n", stderr);
    free(current_path);
    free(b.data);
    return 1;
  }
  if (!terminal_enter(&terminal)) {
    perror("terminal setup");
    free(current_path);
    free(b.data);
    return 1;
  }
  int modified = 0, command = 0, quit = 0;
  char cmd[256] = "",
       message[256] = "Escape: command mode  |  arrows move  |  Ctrl-C exits";
  int numbers = 0;
  size_t goal = (size_t)-1;
  draw(&b, current_path, modified, numbers, message, command, cmd);
  while (!quit) {
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1)
      continue;
    message[0] = '\0';
    if (command)
      handle_command_key(&b, &current_path, c, &modified, &numbers, &goal,
                         &command, &quit, cmd, sizeof cmd, message,
                         sizeof message);
    else
      handle_insert_key(&b, c, &modified, &command, &quit, &goal, cmd, message);
    draw(&b, current_path, modified, numbers, message, command, cmd);
  }
  terminal_leave(&terminal);
  free(current_path);
  free(b.data);
  return 0;
}
// Fuck yeah the code works!!! \(^o^)/
// These 2 lines were written with quecto v1.0 btw
