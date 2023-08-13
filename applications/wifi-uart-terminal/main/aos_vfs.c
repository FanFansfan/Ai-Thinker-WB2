#include "mongoose.h"
#include "vfs.h"
#include "vfs_dir.h"

struct mg_fs aos_vfs;

static void vfs_aos_list(const char *dir, void (*fn)(const char *, void *),
                   void *userdata) {
  aos_dirent_t *dp;
  aos_dir_t *dirp;
  if ((dirp = (aos_opendir(dir))) == NULL) return;
  while ((dp = aos_readdir(dirp)) != NULL) {
    if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..")) continue;
    fn(dp->d_name, userdata);
  }
  aos_closedir(dirp);
}

static void *vfs_aos_open(const char *path, int flags) {
//   flags = flags == MG_FS_READ ? O_RDONLY : O_RDWR; // RDWR append
  (void)flags;
  return (void *) aos_open(path, 0);
}

static void vfs_aos_close(void *fp) {
  aos_close((int)fp);
}

static size_t vfs_aos_read(void *fp, void *buf, size_t len) {
  return aos_read((int) fp, buf, len);
}

static size_t vfs_aos_write(void *fp, const void *buf, size_t len) {
  return aos_write((int) fp, buf, len);
}

static size_t vfs_aos_seek(void *fp, size_t offset) {
  return (size_t) aos_lseek((int) fp, offset, SEEK_SET);
}

static bool vfs_aos_rename(const char *from, const char *to) {
  return aos_rename(from, to) == 0;
}

static bool vfs_aos_remove(const char *path) {
  return aos_unlink(path) == 0;
}

static bool vfs_aos_mkdir(const char *path) {
  return aos_mkdir(path) == 0;
}

void init_aos_vfs() {
    aos_vfs.st = mg_fs_posix.st;
    aos_vfs.ls = vfs_aos_list;
    aos_vfs.op = vfs_aos_open;
    aos_vfs.cl = vfs_aos_close;
    aos_vfs.rd = vfs_aos_read;
    aos_vfs.wr = vfs_aos_write;
    aos_vfs.sk = vfs_aos_seek;
    aos_vfs.mv = vfs_aos_rename;
    aos_vfs.rm = vfs_aos_remove;
    aos_vfs.mkd = vfs_aos_mkdir;
}
// struct mg_fs mg_fs_posix = {p_stat,  p_list, p_open,   p_close,  p_read,
//                             p_write, p_seek, p_rename, p_remove, p_mkdir};