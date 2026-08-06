#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_MAX_LINES 1000

static char *dup_string(const char *s) {
  size_t l = strlen(s) + 1;
  char *d = malloc(l);
  if (d)
    memcpy(d, s, l);
  return d;
}

static void strip_newline(char *s) {
  size_t l = strlen(s);
  if (l > 0 && s[l - 1] == '\n')
    s[l - 1] = '\0';
}

static char *read_dynamic_line(FILE *fp) {
  size_t cap = 128;
  size_t len = 0;
  int c;
  int got_any = 0;
  char *buf = malloc(cap);
  if (!buf)
    return NULL;
  while ((c = fgetc(fp)) != EOF) {
    got_any = 1;
    if (len + 2 >= cap) {
      cap *= 2;
      char *tmp = realloc(buf, cap);
      if (!tmp) {
        free(buf);
        return NULL;
      }
      buf = tmp;
    }
    buf[len++] = (char)c;
    if (c == '\n')
      break;
  }
  if (!got_any) {
    free(buf);
    return NULL;
  }
  buf[len] = '\0';
  return buf;
}

static void print_help(const char *prog) {
  printf("quecto text editor\n\n");
  printf("Usage: %s [options] <filename>\n\n", prog);
  printf("Options:\n");
  printf("  -h, --help            Show this help message and exit\n");
  printf("  -v, --version         Show version information and exit\n");
  printf("  -m, --max-lines N     Set the maximum number of lines (default "
         "%d)\n\n",
         DEFAULT_MAX_LINES);
  printf("Commands (inside the editor):\n");
  printf(":w         Save and exit\n");
  printf(":q         Exit without saving\n");
  printf(":f         Go to previous line and show its content\n");
  printf(":f N       Go to line N and show its content\n");
  printf(":d         Delete current line\n");
  printf(":d N       Delete line N\n");
  printf(":i         Insert a blank line at the current position\n");
  printf(":i N       Insert a blank line at line N\n");
}

static void free_lines(char **lines, int count) {
  for (int i = 0; i < count; i++) {
    if (lines[i])
      free(lines[i]);
  }
}

static void print_line_content(char **lines, int line_count, int current_line) {
  if (current_line < line_count && lines[current_line]) {
    printf("%d: %s", current_line + 1, lines[current_line]);
    if (lines[current_line][strlen(lines[current_line]) - 1] != '\n')
      printf("\n");
  } else {
    printf("%d: (empty)\n", current_line + 1);
  }
}

int main(int argc, char *argv[]) {
  int max_lines = DEFAULT_MAX_LINES;
  char *filename = NULL;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
      printf("quecto text editor v0.07\n");
      printf("Repo: https://github.com/pandixproject/quecto\n");
      return 0;
    }
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      print_help(argv[0]);
      return 0;
    }
    if (strcmp(argv[i], "--max-lines") == 0 || strcmp(argv[i], "-m") == 0) {
      if (i + 1 >= argc) {
        printf("ERROR: %s requires a number argument\n", argv[i]);
        return 1;
      }
      max_lines = atoi(argv[i + 1]);
      if (max_lines <= 0) {
        printf("ERROR: invalid max line count\n");
        return 1;
      }
      i++;
      continue;
    }
    filename = argv[i];
  }
  if (filename == NULL) {
    printf("Usage: %s [options] <filename>\n", argv[0]);
    printf("Try '%s --help' for more information\n", argv[0]);
    return 1;
  }
  char **lines = calloc(max_lines, sizeof(char *));
  if (!lines) {
    printf("ERROR: could not allocate memory for %d lines\n", max_lines);
    return 1;
  }
  FILE *file;
  int line_count = 0;
  int current_line = 0;
  int existing_file = 0;
  int truncated = 0;
  int modified = 0;
  file = fopen(filename, "r");
  if (file != NULL) {
    existing_file = 1;
    char *l;
    while ((l = read_dynamic_line(file)) != NULL) {
      if (line_count >= max_lines) {
        truncated = 1;
        free(l);
        break;
      }
      lines[line_count] = l;
      line_count++;
    }
    fclose(file);
    current_line = line_count;
  }
  printf("\033[?1049h");
  printf("\033[2J");
  printf("\033[H");
  printf("\033[30;47m");
  printf(" quecto text editor v0.07 \n");
  printf("\033[0m\n");
  printf("Commands:\n");
  printf(":w         Save and exit\n");
  printf(":q         Exit without saving\n");
  printf(":f         Previous line\n");
  printf(":f N       Go to line N\n");
  printf(":d [N]     Delete current or line N\n");
  printf(":i [N]     Insert blank line at current or line N\n\n");
  if (existing_file) {
    printf("Loaded %d lines from %s\n", line_count, filename);
    if (truncated) {
      printf(
          "WARNING: file has more than %d lines, extra lines were not loaded\n",
          max_lines);
    }
    printf("\n");
  }
  while (1) {
    printf("%d~ ", current_line + 1);
    fflush(stdout);
    char *raw = read_dynamic_line(stdin);
    if (raw == NULL) {
      break;
    }
    char *cmd = dup_string(raw);
    strip_newline(cmd);
    if (strcmp(cmd, ":w") == 0) {
      free(cmd);
      free(raw);
      file = fopen(filename, "w");
      if (file == NULL) {
        printf("ERROR: Could not save file\n");
        continue;
      }
      for (int i = 0; i < line_count; i++) {
        if (lines[i])
          fputs(lines[i], file);
      }
      fclose(file);
      modified = 0;
      break;
    }
    if (strcmp(cmd, ":q") == 0) {
      free(cmd);
      free(raw);
      if (modified) {
        printf("You have unsaved changes. Quit without saving? (y/N) ");
        fflush(stdout);
        char *ans = read_dynamic_line(stdin);
        int confirmed = 0;
        if (ans != NULL) {
          strip_newline(ans);
          if (strcmp(ans, "y") == 0 || strcmp(ans, "Y") == 0)
            confirmed = 1;
          free(ans);
        }
        if (!confirmed) {
          continue;
        }
      }
      printf("\033[?1049l");
      free_lines(lines, max_lines);
      free(lines);
      return 0;
    }
    if (strcmp(cmd, ":f") == 0) {
      free(cmd);
      free(raw);
      if (current_line > 0) {
        current_line--;
      }
      print_line_content(lines, line_count, current_line);
      continue;
    }
    if (strncmp(cmd, ":f ", 3) == 0) {
      int target = atoi(cmd + 3);
      free(cmd);
      free(raw);
      if (target > 0 && target <= max_lines) {
        current_line = target - 1;
        if (current_line > line_count) {
          line_count = current_line;
        }
        print_line_content(lines, line_count, current_line);
      } else {
        printf("ERROR: line number out of range (1-%d)\n", max_lines);
      }
      continue;
    }
    if (strcmp(cmd, ":d") == 0 || strncmp(cmd, ":d ", 3) == 0) {
      int idx;
      if (strcmp(cmd, ":d") == 0) {
        idx = current_line;
      } else {
        idx = atoi(cmd + 3) - 1;
      }
      free(cmd);
      free(raw);
      if (idx < 0 || idx >= line_count) {
        printf("ERROR: no line %d to delete\n", idx + 1);
        continue;
      }
      free(lines[idx]);
      for (int i = idx; i < line_count - 1; i++) {
        lines[i] = lines[i + 1];
      }
      lines[line_count - 1] = NULL;
      line_count--;
      modified = 1;
      if (current_line > idx)
        current_line--;
      if (current_line > line_count)
        current_line = line_count;
      printf("Deleted line %d\n", idx + 1);
      continue;
    }
    if (strcmp(cmd, ":i") == 0 || strncmp(cmd, ":i ", 3) == 0) {
      int idx;
      if (strcmp(cmd, ":i") == 0) {
        idx = current_line;
      } else {
        idx = atoi(cmd + 3) - 1;
      }
      free(cmd);
      free(raw);
      if (idx < 0 || idx > max_lines - 1) {
        printf("ERROR: line number out of range (1-%d)\n", max_lines);
        continue;
      }
      if (line_count >= max_lines) {
        printf("ERROR: max line limit reached (%d)\n", max_lines);
        continue;
      }
      if (idx > line_count)
        idx = line_count;
      for (int i = line_count; i > idx; i--) {
        lines[i] = lines[i - 1];
      }
      lines[idx] = dup_string("\n");
      line_count++;
      modified = 1;
      current_line = idx;
      printf("Inserted blank line at %d\n", idx + 1);
      continue;
    }
    free(cmd);
    if (current_line < max_lines) {
      if (lines[current_line])
        free(lines[current_line]);
      lines[current_line] = raw;
      if (current_line >= line_count) {
        line_count = current_line + 1;
      }
      modified = 1;
      current_line++;
    } else {
      printf("ERROR: max line limit reached (%d)\n", max_lines);
      free(raw);
    }
  }
  printf("\033[?1049l");
  free_lines(lines, max_lines);
  free(lines);
  return 0;
}
