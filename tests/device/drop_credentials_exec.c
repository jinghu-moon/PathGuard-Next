#include <errno.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

enum { kMaxGroups = 128 };

static int read_credentials(pid_t pid, uid_t* uid, gid_t* gid,
                            gid_t groups[kMaxGroups], size_t* group_count) {
  char path[64];
  if (snprintf(path, sizeof(path), "/proc/%ld/status", (long)pid) >=
      (int)sizeof(path)) {
    errno = ENAMETOOLONG;
    return -1;
  }

  FILE* status = fopen(path, "re");
  if (status == NULL) {
    return -1;
  }

  char* line = NULL;
  size_t capacity = 0;
  int have_uid = 0;
  int have_gid = 0;
  int have_groups = 0;
  while (getline(&line, &capacity, status) >= 0) {
    unsigned long value = 0;
    if (sscanf(line, "Uid:\t%lu", &value) == 1) {
      *uid = (uid_t)value;
      have_uid = 1;
      continue;
    }
    if (sscanf(line, "Gid:\t%lu", &value) == 1) {
      *gid = (gid_t)value;
      have_gid = 1;
      continue;
    }
    if (strncmp(line, "Groups:\t", 8) != 0) {
      continue;
    }

    char* cursor = line + 8;
    while (*cursor != '\0') {
      char* end = NULL;
      value = strtoul(cursor, &end, 10);
      if (end == cursor) {
        break;
      }
      if (*group_count >= kMaxGroups) {
        free(line);
        fclose(status);
        errno = E2BIG;
        return -1;
      }
      groups[(*group_count)++] = (gid_t)value;
      cursor = end;
    }
    have_groups = 1;
  }

  free(line);
  fclose(status);
  if (!have_uid || !have_gid || !have_groups) {
    errno = EINVAL;
    return -1;
  }
  return 0;
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s PID COMMAND [ARG...]\n", argv[0]);
    return 2;
  }

  char* end = NULL;
  const long parsed_pid = strtol(argv[1], &end, 10);
  if (*argv[1] == '\0' || *end != '\0' || parsed_pid <= 0) {
    fprintf(stderr, "invalid PID: %s\n", argv[1]);
    return 2;
  }

  uid_t uid = 0;
  gid_t gid = 0;
  gid_t groups[kMaxGroups];
  size_t group_count = 0;
  if (read_credentials((pid_t)parsed_pid, &uid, &gid, groups,
                       &group_count) != 0) {
    perror("read credentials");
    return 1;
  }
  if (setgroups(group_count, groups) != 0 || setresgid(gid, gid, gid) != 0 ||
      setresuid(uid, uid, uid) != 0) {
    perror("drop credentials");
    return 1;
  }

  execvp(argv[2], &argv[2]);
  perror("execvp");
  return 1;
}
