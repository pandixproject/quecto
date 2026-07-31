#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFFER_SIZE 1024
#define MAX_LINES 1000

static char lines[MAX_LINES][BUFFER_SIZE];

int main(int argc, char *argv[]) {
  FILE *file;
  char line[BUFFER_SIZE];
  int line_count = 0;
  int current_line = 0;
  int existing_file = 0;
  int truncated = 0;
  if (argc > 1 &&
      (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)) {
    printf("quecto text editor v0.06\n");
    printf("Repo: https://github.com/pandixproject/quecto\n");
    return 0;
  }
  if (argc != 2) {
    printf("Usage: %s <filename>\n", argv[0]);
    return 1;
  }
  file = fopen(argv[1], "r");
  if (file != NULL) {
    existing_file = 1;
    while (fgets(line, sizeof(line), file) != NULL) {
      if (line_count >= MAX_LINES) {
        truncated = 1;
        break;
      }
      strcpy(lines[line_count], line);
      line_count++;
    }
    fclose(file);
    current_line = line_count;
  }
  printf("\033[?1049h");
  printf("\033[2J");
  printf("\033[H");
  printf("\033[30;47m");
  printf(" quecto text editor v0.05 \n");
  printf("\033[0m\n");
  printf("Commands:\n");
  printf(":w      Save and exit\n");
  printf(":q      Exit without saving\n");
  printf(":f      Previous line\n");
  printf(":f N    Go to line N\n\n");
  if (existing_file) {
    printf("Loaded %d lines from %s\n", line_count, argv[1]);
    if (truncated) {
      printf(
          "WARNING: file has more than %d lines, extra lines were not loaded\n",
          MAX_LINES);
    }
    printf("\n");
  }
  while (1) {
    printf("%d~ ", current_line + 1);
    fflush(stdout);
    if (fgets(line, sizeof(line), stdin) == NULL) {
      break;
    }
    if (strcmp(line, ":w\n") == 0) {
      file = fopen(argv[1], "w");
      if (file == NULL) {
        printf("ERROR: Could not save file\n");
        continue;
      }
      for (int i = 0; i < line_count; i++) {
        fputs(lines[i], file);
      }
      fclose(file);
      break;
    }
    if (strcmp(line, ":q\n") == 0) {
      printf("\033[?1049l");
      return 0;
    }
    if (strcmp(line, ":f\n") == 0) {
      if (current_line > 0) {
        current_line--;
      }
      continue;
    }
    if (strncmp(line, ":f ", 3) == 0) {
      int target = atoi(line + 3);
      if (target > 0 && target <= MAX_LINES) {
        current_line = target - 1;
        if (current_line > line_count) {
          line_count = current_line;
        }
      } else {
        printf("ERROR: line number out of range (1-%d)\n", MAX_LINES);
      }
      continue;
    }
    if (current_line < MAX_LINES) {
      strcpy(lines[current_line], line);
      if (current_line >= line_count) {
        line_count = current_line + 1;
      }
      current_line++;
    } else {
      printf("ERROR: max line limit reached (%d)\n", MAX_LINES);
    }
  }
  printf("\033[?1049l");
  return 0;
}
