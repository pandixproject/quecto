#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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
  size_t cap = 128, len = 0;
  int c;
  char *buf = malloc(cap);
  if (!buf)
    return NULL;
  while ((c = fgetc(fp)) != EOF) {
    if (len + 2 >= cap) {
      char *tmp;
      cap *= 2;
      tmp = realloc(buf, cap);
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
  if (len == 0 && c == EOF) {
    free(buf);
    return NULL;
  }
  buf[len] = '\0';
  return buf;
}

static void free_lines(char **lines, int count) {
  for (int i = 0; i < count; i++)
    free(lines[i]);
}

static int ensure_capacity(char ***lines, int *capacity, int required,
                           int max_lines) {
  int new_capacity;
  char **tmp;
  if (max_lines > 0 && required > max_lines)
    return 0;
  if (required <= *capacity)
    return 1;
  new_capacity = *capacity ? *capacity : 32;
  while (new_capacity < required) {
    if (new_capacity > INT_MAX / 2) {
      new_capacity = required;
      break;
    }
    new_capacity *= 2;
  }
  if (max_lines > 0 && new_capacity > max_lines)
    new_capacity = max_lines;
  tmp = realloc(*lines, (size_t)new_capacity * sizeof(*tmp));
  if (!tmp)
    return 0;
  memset(tmp + *capacity, 0,
         (size_t)(new_capacity - *capacity) * sizeof(*tmp));
  *lines = tmp;
  *capacity = new_capacity;
  return 1;
}

static int parse_line_number(const char *text, int *number) {
  char *end;
  long value;
  errno = 0;
  value = strtol(text, &end, 10);
  if (errno || end == text || *end != '\0' || value < 1 || value > INT_MAX)
    return 0;
  *number = (int)value;
  return 1;
}

static int parse_range(const char *text, int *first, int *last) {
  char *end;
  long a, b;
  errno = 0;
  a = strtol(text, &end, 10);
  if (errno || end == text || a < 1 || a > INT_MAX)
    return 0;
  while (*end == ' ')
    end++;
  if (*end == '\0') {
    *first = *last = (int)a;
    return 1;
  }
  errno = 0;
  b = strtol(end, &end, 10);
  if (errno || *end != '\0' || b < a || b > INT_MAX)
    return 0;
  *first = (int)a;
  *last = (int)b;
  return 1;
}

static void print_line_content(char **lines, int line_count, int line) {
  if (line < line_count && lines[line]) {
    size_t length = strlen(lines[line]);
    printf("%d: %s", line + 1, lines[line]);
    if (length == 0 || lines[line][length - 1] != '\n')
      printf("\n");
  } else {
    printf("%d: (empty)\n", line + 1);
  }
}

static void print_help(const char *prog) {
  printf("quecto text editor\n\nUsage: %s [options] <filename>\n\n", prog);
  printf("Options:\n  -h, --help            Show help and exit\n");
  printf("  -v, --version         Show version information and exit\n");
  printf("  -m, --max-lines N     Limit the file to N lines\n\nCommands:\n");
  printf(":w save, :wq save and exit, :q exit, :q! force exit\n");
  printf(":p [N [M]] print, :g N go to line, :f [N] go and print\n");
  printf(":s TEXT find, :n next match, :d [N] delete, :i [N] insert\n");
  printf(":u undo last edit, :help show this help\n");
}

static int save_file(const char *filename, char **lines, int line_count) {
  size_t path_length = strlen(filename) + 8;
  char *temporary = malloc(path_length);
  struct stat status;
  int fd;
  FILE *file;
  int ok = 0, failed = 0;
  if (!temporary)
    return 0;
  snprintf(temporary, path_length, "%s.XXXXXX", filename);
  fd = mkstemp(temporary);
  if (fd < 0)
    goto done;
  if (stat(filename, &status) == 0)
    fchmod(fd, status.st_mode);
  file = fdopen(fd, "w");
  if (!file) {
    close(fd);
    unlink(temporary);
    goto done;
  }
  for (int i = 0; i < line_count; i++) {
    if (lines[i] && fputs(lines[i], file) == EOF) {
      failed = 1;
      break;
    }
  }
  if (!failed && (fflush(file) == EOF || fsync(fd) != 0))
    failed = 1;
  if (fclose(file) == EOF)
    failed = 1;
  if (failed) {
    unlink(temporary);
    goto done;
  }
  if (rename(temporary, filename) != 0) {
    unlink(temporary);
    goto done;
  }
  ok = 1;
done:
  free(temporary);
  return ok;
}

static int copy_lines(char **source, int count, char ***copy) {
  char **result = calloc((size_t)count, sizeof(*result));
  if (!result && count > 0)
    return 0;
  for (int i = 0; i < count; i++) {
    if (source[i] && !(result[i] = dup_string(source[i]))) {
      free_lines(result, count);
      free(result);
      return 0;
    }
  }
  *copy = result;
  return 1;
}

static int save_undo(char **lines, int count, int current, char ***undo,
                     int *undo_count, int *undo_current, int *undo_available) {
  char **copy;
  if (!copy_lines(lines, count, &copy))
    return 0;
  free_lines(*undo, *undo_count);
  free(*undo);
  *undo = copy;
  *undo_count = count;
  *undo_current = current;
  *undo_available = 1;
  return 1;
}

static int find_next(char **lines, int count, int current, const char *query) {
  for (int offset = 1; offset <= count; offset++) {
    int line = (current + offset) % count;
    if (lines[line] && strstr(lines[line], query))
      return line;
  }
  return -1;
}

int main(int argc, char *argv[]) {
  int max_lines = 0, capacity = 0, line_count = 0, current_line = 0;
  int undo_count = 0, undo_current = 0, undo_available = 0, modified = 0;
  char *filename = NULL, *last_search = NULL;
  char **lines = NULL, **undo_lines = NULL;
  FILE *file;

  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-v")) {
      printf("quecto text editor v0.08\nRepo: https://github.com/pandixproject/quecto\n");
      return 0;
    }
    if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
      print_help(argv[0]);
      return 0;
    }
    if (!strcmp(argv[i], "--max-lines") || !strcmp(argv[i], "-m")) {
      const char *option = argv[i];
      if (i + 1 >= argc || !parse_line_number(argv[i + 1], &max_lines)) {
        printf("ERROR: %s requires a positive whole number\n", option);
        return 1;
      }
      i++;
    } else if (filename) {
      printf("ERROR: only one filename may be provided\n");
      return 1;
    } else {
      filename = argv[i];
    }
  }
  if (!filename) {
    printf("Usage: %s [options] <filename>\n", argv[0]);
    return 1;
  }
  if ((file = fopen(filename, "r"))) {
    char *line;
    while ((line = read_dynamic_line(file))) {
      if (!ensure_capacity(&lines, &capacity, line_count + 1, max_lines)) {
        printf("ERROR: could not load more lines\n");
        free(line);
        fclose(file);
        free_lines(lines, line_count);
        free(lines);
        return 1;
      }
      lines[line_count++] = line;
    }
    fclose(file);
    current_line = line_count;
  }
  printf("\033[?1049h\033[2J\033[H\033[30;47m quecto text editor v0.08 \n\033[0m\n");
  printf("Type :help for commands.\n\n");

  while (1) {
    char *raw, *cmd;
    printf("%d%s~ ", current_line + 1, modified ? "*" : "");
    fflush(stdout);
    raw = read_dynamic_line(stdin);
    if (!raw) {
      if (modified)
        printf("\nUnsaved changes. Use :w, :wq, or :q!\n");
      break;
    }
    cmd = dup_string(raw);
    if (!cmd) {
      free(raw);
      printf("ERROR: out of memory\n");
      continue;
    }
    strip_newline(cmd);
    if (!strcmp(cmd, ":help")) {
      print_help(argv[0]);
    } else if (!strcmp(cmd, ":w") || !strcmp(cmd, ":wq")) {
      if (save_file(filename, lines, line_count)) {
        modified = 0;
        printf("Saved %s\n", filename);
        if (!strcmp(cmd, ":wq")) {
          free(cmd); free(raw); break;
        }
      } else printf("ERROR: could not save %s\n", filename);
    } else if (!strcmp(cmd, ":q!")) {
      free(cmd); free(raw); break;
    } else if (!strcmp(cmd, ":q")) {
      if (!modified) { free(cmd); free(raw); break; }
      printf("You have unsaved changes. Quit without saving? (y/N) ");
      fflush(stdout);
      {
        char *answer = read_dynamic_line(stdin);
        int confirmed = answer && (!strcmp(answer, "y\n") || !strcmp(answer, "Y\n") ||
                                   !strcmp(answer, "y") || !strcmp(answer, "Y"));
        free(answer);
        if (confirmed) { free(cmd); free(raw); break; }
      }
    } else if (!strcmp(cmd, ":p")) {
      print_line_content(lines, line_count, current_line);
    } else if (!strncmp(cmd, ":p ", 3)) {
      int first, last;
      if (!parse_range(cmd + 3, &first, &last) || last > line_count) printf("ERROR: invalid line range\n");
      else for (int i = first - 1; i < last; i++) print_line_content(lines, line_count, i);
    } else if (!strncmp(cmd, ":g ", 3) || !strncmp(cmd, ":f ", 3)) {
      int target;
      if (!parse_line_number(cmd + 3, &target) || (max_lines && target > max_lines)) printf("ERROR: invalid line number\n");
      else { current_line = target - 1; print_line_content(lines, line_count, current_line); }
    } else if (!strcmp(cmd, ":f")) {
      if (current_line > 0) current_line--;
      print_line_content(lines, line_count, current_line);
    } else if (!strncmp(cmd, ":s ", 3)) {
      int found;
      free(last_search);
      last_search = dup_string(cmd + 3);
      if (!last_search || !*last_search) { free(last_search); last_search = NULL; printf("ERROR: search text is required\n"); }
      else if ((found = find_next(lines, line_count, current_line, last_search)) < 0) printf("Not found: %s\n", last_search);
      else { current_line = found; print_line_content(lines, line_count, current_line); }
    } else if (!strcmp(cmd, ":n")) {
      int found;
      if (!last_search) printf("ERROR: no previous search\n");
      else if ((found = find_next(lines, line_count, current_line, last_search)) < 0) printf("Not found: %s\n", last_search);
      else { current_line = found; print_line_content(lines, line_count, current_line); }
    } else if (!strcmp(cmd, ":u")) {
      if (!undo_available) printf("ERROR: nothing to undo\n");
      else {
        free_lines(lines, line_count); free(lines);
        lines = undo_lines; line_count = undo_count; capacity = undo_count; current_line = undo_current;
        undo_lines = NULL; undo_count = 0; undo_available = 0; modified = 1;
        printf("Undid last edit\n");
      }
    } else if (!strcmp(cmd, ":d") || !strncmp(cmd, ":d ", 3)) {
      int line = current_line;
      if (cmd[2] && !parse_line_number(cmd + 3, &line)) printf("ERROR: invalid line number\n");
      else {
        if (cmd[2]) line--;
        if (line < 0 || line >= line_count) printf("ERROR: no line %d to delete\n", line + 1);
        else if (!save_undo(lines, line_count, current_line, &undo_lines, &undo_count, &undo_current, &undo_available)) printf("ERROR: could not prepare undo\n");
        else {
          free(lines[line]); memmove(lines + line, lines + line + 1, (size_t)(line_count - line - 1) * sizeof(*lines));
          lines[--line_count] = NULL;
          if (current_line > line) current_line--;
          if (current_line > line_count) current_line = line_count;
          modified = 1; printf("Deleted line %d\n", line + 1);
        }
      }
    } else if (!strcmp(cmd, ":i") || !strncmp(cmd, ":i ", 3)) {
      int line = current_line;
      if (cmd[2] && !parse_line_number(cmd + 3, &line)) printf("ERROR: invalid line number\n");
      else {
        if (cmd[2]) line--;
        if (line < 0 || (max_lines && line >= max_lines) || (max_lines && line_count >= max_lines)) printf("ERROR: line number out of range\n");
        else if (!save_undo(lines, line_count, current_line, &undo_lines, &undo_count, &undo_current, &undo_available) || !ensure_capacity(&lines, &capacity, line_count + 1, max_lines)) printf("ERROR: could not insert line\n");
        else {
          if (line > line_count) line = line_count;
          {
            char *blank = dup_string("\n");
            if (!blank) printf("ERROR: could not insert line\n");
            else {
              memmove(lines + line + 1, lines + line, (size_t)(line_count - line) * sizeof(*lines));
              lines[line] = blank;
              line_count++; current_line = line; modified = 1; printf("Inserted blank line at %d\n", line + 1);
            }
          }
        }
      }
    } else {
      int required = current_line + 1, failed = 0;
      if ((max_lines && required > max_lines) || !save_undo(lines, line_count, current_line, &undo_lines, &undo_count, &undo_current, &undo_available) || !ensure_capacity(&lines, &capacity, required, max_lines)) printf("ERROR: could not add line\n");
      else {
        for (int i = line_count; i < current_line; i++) if (!(lines[i] = dup_string("\n"))) { failed = 1; break; }
        if (failed) {
          for (int i = line_count; i < current_line; i++) { free(lines[i]); lines[i] = NULL; }
          printf("ERROR: could not add line\n");
        } else {
          free(lines[current_line]); lines[current_line] = raw; raw = NULL;
          if (required > line_count) line_count = required;
          current_line++; modified = 1;
        }
      }
    }
    free(cmd);
    free(raw);
  }
  printf("\033[?1049l");
  free(last_search); free_lines(lines, line_count); free(lines);
  free_lines(undo_lines, undo_count); free(undo_lines);
  return 0;
}
