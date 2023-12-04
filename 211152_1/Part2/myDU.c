#include <dirent.h>
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ucontext.h>
#include <sys/wait.h>
#include <unistd.h>

void error() {
  printf("Unable to execute\n");
  exit(1);
}

unsigned long symlink_main(char *basedir) {
  struct stat check_sym;
  stat(basedir, &check_sym);
  if (S_ISLNK(check_sym.st_mode)) {
    char buf[10000];
    for (int i = 0; i < 10000; i++) {
      buf[i] = '\0';
    }
    if (readlink(basedir, buf, 10000) == 10000)
      error();
    for (int i = 9999; i >= 0; i--) {
      if (basedir[i] == '/')
        break;
      basedir[i] = '\0';
    }
    strcat(basedir, buf);
    return symlink_main(basedir);
  }
  DIR *maindir = opendir(basedir);
  if (maindir == NULL)
    error();

  struct dirent *md_iter;
  unsigned long dir_size = 0;

  struct stat filest;
  stat(basedir, &filest);
  dir_size += filest.st_size;
  while ((md_iter = readdir(maindir)) != NULL) {
    char fpath[5000];
    strcpy(fpath, basedir);
    strcat(fpath, "/");
    strcat(fpath, md_iter->d_name);

    if (strcmp(md_iter->d_name, ".") == 0 || strcmp(md_iter->d_name, "..") == 0)
      continue;
    if (md_iter->d_type == DT_DIR) {
      dir_size += symlink_main(fpath);
    } else if (md_iter->d_type == DT_REG) {
      struct stat filest;
      stat(fpath, &filest);
      dir_size += filest.st_size;
    } else if (md_iter->d_type == DT_LNK) {

      char sym_path[10000];
      strcat(sym_path, basedir);
      strcat(sym_path, "/");

      char buf[10000];
      for (int i = 0; i < 10000; i++) {
        buf[i] = '\0';
      }
      if (readlink(fpath, buf, 10000) == 10000)
        error();
      strcat(sym_path, buf);

      dir_size += symlink_main(sym_path);
    }
  }
  return dir_size;
}

unsigned long directory_main(char *basedir, int root) {

  DIR *maindir = opendir(basedir);
  if (maindir == NULL)
    error();

  struct dirent *md_iter;
  unsigned long dir_size = 0;

  struct stat filest;
  stat(basedir, &filest);
  dir_size += filest.st_size;
  while ((md_iter = readdir(maindir)) != NULL) {
    char fpath[5000];
    strcpy(fpath, basedir);
    strcat(fpath, "/");
    strcat(fpath, md_iter->d_name);

    if (strcmp(md_iter->d_name, ".") == 0 || strcmp(md_iter->d_name, "..") == 0)
      continue;
    if (md_iter->d_type == DT_DIR) {
      dir_size += directory_main(fpath, 0);
    } else if (md_iter->d_type == DT_REG) {
      struct stat filest;
      stat(fpath, &filest);
      dir_size += filest.st_size;
    } else if (md_iter->d_type == DT_LNK) {
      char sym_path[10000];
      strcat(sym_path, basedir);
      strcat(sym_path, "/");

      char buf[10000];
      for (int i = 0; i < 10000; i++) {
        buf[i] = '\0';
      }
      if (readlink(fpath, buf, 10000) == 10000)
        error();
      strcat(sym_path, buf);

      dir_size += symlink_main(sym_path);
    }
  }

  if (root)
    write(1, &dir_size, sizeof(dir_size));
  return dir_size;
}

int main(int argc, char *argv[]) {
  if (argc != 2)
    error();

  char *basedir = argv[1];
  DIR *maindir = opendir(basedir);

  if (maindir == NULL)
    error();
  struct dirent *md_iter;
  unsigned long dir_size = 0;

  int pipefd[2];
  if (pipe(pipefd) == -1)
    error();
  int dircount = 0;

  struct stat filest;
  stat(basedir, &filest);
  dir_size += filest.st_size;

  while ((md_iter = readdir(maindir)) != NULL) {
    char fpath[5000];
    strcpy(fpath, argv[1]);
    strcat(fpath, "/");
    strcat(fpath, md_iter->d_name);

    if (strcmp(md_iter->d_name, ".") == 0 || strcmp(md_iter->d_name, "..") == 0)
      continue;
    if (md_iter->d_type == DT_DIR) {
      dircount++;

      pid_t pid = fork();
      char *argv2[3];
      if (!pid) {
        char *argv2[3];
        argv2[0] = argv[0];
        argv2[1] = fpath;
        argv2[2] = NULL;

        close(1);
        dup(pipefd[1]);

        directory_main(argv2[1], 1);
        exit(0);
      }

      close(0);
      dup(pipefd[0]);

    } else if (md_iter->d_type == DT_REG) {
      struct stat filest;
      stat(fpath, &filest);
      dir_size += filest.st_size;
    } else if (md_iter->d_type == DT_LNK) {
      char sym_path[10000];
      strcat(sym_path, basedir);
      strcat(sym_path, "/");

      char buf[10000];
      for (int i = 0; i < 10000; i++) {
        buf[i] = '\0';
      }
      if (readlink(fpath, buf, 10000) == 10000)
        error();
      strcat(sym_path, buf);

      dir_size += symlink_main(sym_path);
    }
  }

  long unsigned rec_dir_size = 0;
  while (dircount--) {
    read(0, &rec_dir_size, sizeof(rec_dir_size));
    dir_size += rec_dir_size;
  }
  printf("%lu\n", dir_size);
  exit(0);
}
