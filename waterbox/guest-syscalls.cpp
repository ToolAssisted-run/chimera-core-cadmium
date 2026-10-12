// Sandbox-only replacements for C library calls whose system calls miniBox
// does not have. One static link means defining these here takes the place
// of the C library's versions. Not compiled into the native reference.
//
// Cadmium's loader asks whether the folder a program came from exists, to
// list it in its file browser. In a sandbox there are files mounted by name
// and no folders. So a path is "there" when it can be opened, and what it is
// is what the open file says; anything else is not there.
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {

int fstatat(int, const char* path, struct stat* st, int)
{
  if (!path || !*path)
  {
    errno = ENOENT;
    return -1;
  }
  const int fd = open(path, O_RDONLY);
  if (fd < 0)
  {
    errno = ENOENT;
    return -1;
  }
  const int result = fstat(fd, st);
  close(fd);
  return result;
}

int stat(const char* path, struct stat* st)
{
  return fstatat(AT_FDCWD, path, st, 0);
}

int lstat(const char* path, struct stat* st)
{
  return fstatat(AT_FDCWD, path, st, 0);
}

}  // extern "C"
