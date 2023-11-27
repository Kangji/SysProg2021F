//--------------------------------------------------------------------------------------------------
// System Programming                         I/O Lab                                    Fall 2021
//
/// @file
/// @brief resursively traverse directory tree and list all entries
/// @author <Ji Ho Kang>
/// @studid <20XX-XXXXX>
//--------------------------------------------------------------------------------------------------

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <assert.h>
#include <grp.h>
#include <pwd.h>

#define MAX_DIR 64            ///< maximum number of directories supported

#define INIT_SZ 16

#define NO_META ""

/// @brief output control flags
#define F_TREE      0x1       ///< enable tree view
#define F_SUMMARY   0x2       ///< enable summary
#define F_VERBOSE   0x4       ///< turn on verbose mode


/// @brief struct holding the summary
struct summary {
  unsigned int dirs;          ///< number of directories encountered
  unsigned int files;         ///< number of files
  unsigned int links;         ///< number of links
  unsigned int fifos;         ///< number of pipes
  unsigned int socks;         ///< number of sockets

  unsigned long long size;    ///< total size (in bytes)
  unsigned long long blocks;  ///< total number of blocks (512 byte blocks)
};


/// @brief variable size array
struct __varray {
  struct dirent *head;        ///< points the head of the array
  unsigned int numElem;       ///< number of current entry
  unsigned int maxElem;       ///< number of maximum entry for now
};


/// @brief abort the program with EXIT_FAILURE and an optional error message
///
/// @param msg optional error message or NULL
void panic(const char *msg)
{
  if (msg) fprintf(stderr, "%s\n", msg);
  exit(EXIT_FAILURE);
}


/// @brief create varray.
/// Panic if failed.
///
/// @retval pointer to new varray if memory allocation succeeded
/// @retval NULL if memory allocation failed
static struct __varray *createVArray()
{
  struct __varray *arr;
  struct dirent *head = malloc(INIT_SZ * sizeof(struct dirent));

  if (!head)  panic(strerror(errno));

  arr = malloc(sizeof(struct __varray));

  if (!arr) panic(strerror(errno));

  arr->head = head;
  arr->numElem = 0;
  arr->maxElem = INIT_SZ;

  return arr;
}


/// @brief push @a entry into @a arr. Resize the array if needed.
/// Panic if failed.
///
/// @param arr pointer to varray
/// @param entry pointer to entry
static void pushEntry(struct __varray *arr, struct dirent *entry)
{
  if (arr->numElem == arr->maxElem) {
    arr->maxElem *= 2;
    if ( !(arr->head = realloc(arr->head, arr->maxElem * sizeof(struct dirent))) )  panic(strerror(errno));
  }

  arr->head[arr->numElem++] = *entry;
}


/// @brief frees the memory allocated to @a arr
///
/// @param arr pointer to varray
static void freeVArray(struct __varray *arr)
{
  free(arr->head);
  free(arr);
}


/// @brief read next directory entry from open directory 'dir'. Ignores '.' and '..' entries
///
/// @param dir open DIR* stream
/// @retval entry on success
/// @retval NULL on error or if there are no more entries
struct dirent *getNext(DIR *dir)
{
  struct dirent *next;
  int ignore;

  do {
    errno = 0;
    next = readdir(dir);
    if (errno != 0) perror(NULL);
    ignore = next && ((strcmp(next->d_name, ".") == 0) || (strcmp(next->d_name, "..") == 0));
  } while (next && ignore);

  return next;
}


/// @brief qsort comparator to sort directory entries. Sorted by name, directories first.
///
/// @param a pointer to first entry
/// @param b pointer to second entry
/// @retval -1 if a<b
/// @retval 0  if a==b
/// @retval 1  if a>b
static int dirent_compare(const void *a, const void *b)
{
  struct dirent *e1 = (struct dirent*)a;
  struct dirent *e2 = (struct dirent*)b;

  // if one of the entries is a directory, it comes first
  if (e1->d_type != e2->d_type) {
    if (e1->d_type == DT_DIR) return -1;
    if (e2->d_type == DT_DIR) return 1;
  }

  // otherwise sorty by name
  return strcmp(e1->d_name, e2->d_name);
}


/// @brief clear @a stats
///
/// @param stats pointer to statistics
static void clearStats(struct summary *stats)
{
  stats->blocks = 0;
  stats->dirs = 0;
  stats->fifos = 0;
  stats->files = 0;
  stats->links = 0;
  stats->size = 0;
  stats->socks = 0;
}


/// @brief update metadata in summary
///
/// @param stats pointer to the summary
/// @param size size of the file
/// @param blocks blocks of the file
static void updateMetaSummary(struct summary *stats, long long size, long long blocks)
{
  stats->size += size;
  stats->blocks += blocks;
}


/// @brief update file count
///
/// @param stats pointer to the summary
/// @param type type of the file
/// @param flags output control flags (F_*)
static void updateFileCountIfNeeded(struct summary *stats, unsigned char d_type, unsigned int flags)
{
  if (flags & F_SUMMARY) {
    if      (d_type == DT_REG)  stats->files++;
    else if (d_type == DT_DIR)  stats->dirs++;
    else if (d_type == DT_LNK)  stats->links++;
    else if (d_type == DT_FIFO) stats->fifos++;
    else if (d_type == DT_SOCK) stats->socks++;
  }
}


/// @brief integrate summary into total summary
///
/// @param dstat summary of a directory
/// @param tstat summary of total
static void integrateSummary(struct summary *dstat, struct summary *tstat)
{
  tstat->size += dstat->size;
  tstat->blocks += dstat->blocks;
  tstat->files += dstat->files;
  tstat->dirs += dstat->dirs;
  tstat->fifos += dstat->fifos;
  tstat->socks += dstat->socks;
  tstat->links += dstat->links;
}


/// @brief print one liner. Assumes that fprintf never fails.
static void printOneLiner()
{
  char liner[101];
  for (int i = 0; i < 100; i++) liner[i] = '-';
  liner[100] = 0;
  fprintf(stdout, "%s\n", liner);
}


/// @brief remove / if command line argument (directory path) ends with /.
/// Panic if failed.
///
/// @param dn pointer to the directory path
/// @retval pointer to the generated path string
static char *generatePathWithoutSlash(const char *dn)
{
  char *newPath;
  size_t len = strlen(dn);

  if (dn[len-1] == '/') {
    newPath = malloc(len * sizeof(char));
    
    if (!newPath) panic(strerror(errno));

    strncpy(newPath, dn, len-1);
    newPath[len-1] = 0;
  } else {
    newPath = strdup(dn);

    if (!newPath) panic(strerror(errno));
  }
  return newPath;
}


/// @brief generate file path with directory path and file name.
/// Panic if failed.
///
/// @param dn pointer to the directory path
/// @param name pointer to the file name
/// @retval pointer to the file path
static char *generateFilePath(const char *dn, const char *name)
{
  char *filepath;

  if (asprintf(&filepath, "%s/%s", dn, name) < 0) panic(strerror(errno));

  return filepath;
}


/// @brief based on current directory name, generate directory name for next depth.
/// Panic if failed.
///
/// @param dn pointer to the current directory path
/// @param name pointer to the next directory name
/// @retval pointer to the next directory path
static char *generateDirPathForNextDepth(const char *dn, const char *name)
{
  char *nextDn;

  if (asprintf(&nextDn, "%s/%s", dn, name) < 0) panic(strerror(errno));

  return nextDn;
}


/// @brief based on current prefix string, mode, and other entries, generate the prefix string for next depth.
/// Panic if failed.
///
/// @param pstr pointer to current prefix string
/// @param rIndex number of remaining entries to traverse in current level
/// @param flags output control flags (F_*)
/// @retval pointer to the generated prefix string
static char *generatePrefixForNextDepth(const char *pstr, int rIndex, unsigned int flags)
{
  char *prefix;
  int ret;

  if ((flags & F_TREE) && rIndex)  ret = asprintf(&prefix, "%s| ", pstr);
  else  ret = asprintf(&prefix, "%s  ", pstr);

  if (ret < 0)  panic(strerror(errno));

  return prefix;
}


/// @brief using prefix string of current level, generate name string with prefix.
/// Panic if failed.
///
/// @param pstr pointer to prefix string
/// @param rIndex number of remaining entries to traverse in current level
/// @param name pointer to the name of entry
/// @param flags output control flags (F_*)
/// @retval pointer to the generated prefix string
static char *generateNameStrWithPrefix(const char *pstr, int rIndex, const char *name, unsigned int flags)
{
  char *fullname;
  int ret;

  if (flags & F_TREE){
    if (rIndex) ret = asprintf(&fullname, "%s|-%s", pstr, name);
    else  ret = asprintf(&fullname, "%s`-%s", pstr, name);
  }
  else  ret = asprintf(&fullname, "%s  %s", pstr, name);

  if (ret < 0)  panic(strerror(errno));
  
  return fullname;
}


/// @brief generates a name string that is shortened into @a length if needed.
/// Panic if failed.
///
/// @param name pointer to the name
/// @param length the maximum length
/// @param flags output control flags (F_*)
/// @retval pointer to the generated name string which is either shortened or not
static char *generateShortNameIfNeeded(const char *name, int length, unsigned int flags)
{
  char *shortForm;

  if (name && (flags & F_VERBOSE) && strlen(name) > length) {
    shortForm = malloc((length + 1) * sizeof(char));

    if (!shortForm) panic(strerror(errno));

    strncpy(shortForm, name, length - 3);
    strncpy(shortForm + length - 3, "...", 3);
    shortForm[length] = 0;
  } else {
    shortForm = strdup(name);

    if (!shortForm) panic(strerror(errno));
  }
  return shortForm;
}


/// @brief analyze stats. After successful operation, each parameters point to appropriate addresses
/// Panic if error occured.
///
/// @param buf pointer to stat to analyse
/// @param user where to save user string
/// @param group where to save group string
/// @param size where to save size
/// @param blocks where to save blocks
/// @param type where to save type
static void analyzeMetadata(struct stat *buf, char **user, char **group, long long *size, long long *blocks, char *type)
{
  struct passwd *pwd;
  struct group *grp;

  errno = 0;

  // user
  pwd = getpwuid(buf->st_uid);
  
  if (!pwd) {
    if (errno)  panic(strerror(errno));
    else  if (asprintf(user, "%u", buf->st_uid) < 0)  panic(strerror(errno));
  } else {
    if (asprintf(user, "%s", pwd->pw_name) < 0) panic(strerror(errno));
  }

  // group
  grp = getgrgid(buf->st_gid);

  if (!grp) {
    if (errno)  panic(strerror(errno));
    else  if (asprintf(user, "%u", buf->st_gid) < 0)  panic(strerror(errno));
  } else {
    if (asprintf(group, "%s", grp->gr_name) < 0)  panic(strerror(errno));
  }

  // size, blocks
  *size = buf->st_size;
  *blocks = buf->st_blocks;

  // type
  if      (S_ISREG(buf->st_mode))   *type = ' ';
  else if (S_ISBLK(buf->st_mode))   *type = 'b';
  else if (S_ISCHR(buf->st_mode))   *type = 'c';
  else if (S_ISDIR(buf->st_mode))   *type = 'd';
  else if (S_ISFIFO(buf->st_mode))  *type = 'f';
  else if (S_ISSOCK(buf->st_mode))  *type = 's';
  else if (S_ISLNK(buf->st_mode))   *type = 'l';
}


/// @brief generate metadata string if needed.
/// Panic if failed.
///
/// @param name pointer to the name of the file
/// @param stats pointer to the summary
/// @param flags output control flags (F_*)
/// @retval pointer to the metadata string if succeeded
/// @retval pointer to the error string if failed with such handlable errors
static char *generateMetadataStrIfNeeded(const char *name, struct summary *stats, unsigned int flags)
{
  char *metadata;

  if (flags & F_VERBOSE) {
    struct stat buf;

    if (lstat(name, &buf) < 0) {
      metadata = strdup(strerror(errno));

      if (!metadata)  panic(strerror(errno)); // out of memory
    } else {
      char *user, *group;
      long long size, blocks;
      char type = 0;

      analyzeMetadata(&buf, &user, &group, &size, &blocks, &type);

      if (asprintf(&metadata, "%8s:%-8s  %10lld  %8lld  %c", user, group, size, blocks, type) < 0)  panic(strerror(errno));

      if (flags & F_SUMMARY)  updateMetaSummary(stats, size, blocks);

      free(user);
      free(group);
    }
  } else {
    metadata = strdup(NO_META);

    if (!metadata)  panic(strerror(errno));
  }
  return metadata;
}


/// @brief print a line that presents a single entry. Assumes that vfprintf never fails.
///
/// @param name pointer to the name string of the file
/// @param metadata pointer to the metadata string of the file
/// @param flags output control flags (F_*)
static void printSingleEntry(const char *name, const char *metadata, unsigned int flags)
{
  char *shortname = generateShortNameIfNeeded(name, 54, flags);

  fprintf(stdout, "%-54s  %s\n", shortname, metadata);

  free(shortname);
}


/// @brief print a header if needed. Assumes that fprintf never fails.
///
/// @param flags output control flags (F_*)
static void printHeaderIfNeeded(unsigned int flags)
{
  if (flags & F_SUMMARY) {
    if (flags & F_VERBOSE) fprintf(stdout, "%-54s  %8s:%-8s  %10s  %8s %4s\n", "Name", "User", "Group", "Size", "Blocks", "Type");
    else  fprintf(stdout, "%s\n", "Name");
    printOneLiner();
  }
}


/// @brief returns a pluralized unit depending on @a count
///
/// @param count count of the unit
/// @param unit pointer to the unit string
/// @retval pointer to the unit string(do nothing) if count is 1 or unit is not valid
/// @retval plural form of the unit if count is 0, 2 or bigger
static const char *pluralize(unsigned int count, const char *unit)
{
  if (count == 1) {
    return unit;
  } else {
    if (!strcmp(unit, "file"))            return "files";
    else if (!strcmp(unit, "directory"))  return "directories";
    else if (!strcmp(unit, "link"))       return "links";
    else if (!strcmp(unit, "pipe"))       return "pipes";
    else if (!strcmp(unit, "socket"))     return "sockets";
    else  return unit;
  }
}


/// @brief generates a string containing the @a count and @a unit in appropriate form.
/// Must be called only in @a generateSummaryStr function..
/// Panic if failed.
///
/// @param count count of the unit
/// @param unit pointer to the unit string
/// @retval pointer to the generated string
static char *generateCountStr(unsigned int count, const char *unit)
{
  char *str;
  
  if (asprintf(&str, "%d %s", count, pluralize(count, unit)) < 0) panic(strerror(errno));

  return str;
}


/// @brief generates default summary string based on @a stats.
/// Panic if failed.
///
/// @param stats pointer to the summary
/// @retval pointer to the generated string
static char *generateSummaryStr(struct summary *stats)
{
  char *fCnt, *dCnt, *lCnt, *pCnt, *sCnt;
  char *summaryCnt;

  fCnt = generateCountStr(stats->files, "file");
  dCnt = generateCountStr(stats->dirs, "directory");
  lCnt = generateCountStr(stats->links, "link");
  pCnt = generateCountStr(stats->fifos, "pipe");
  sCnt = generateCountStr(stats->socks, "socket");

  if (asprintf(&summaryCnt, "%s, %s, %s, %s, and %s", fCnt, dCnt, lCnt, pCnt, sCnt) < 0)  panic(strerror(errno));

  // frees memory since these will no longer be used
  free(fCnt);
  free(dCnt);
  free(lCnt);
  free(pCnt);
  free(sCnt);

  return summaryCnt;
}


/// @brief prints a footer if needed. Assumes that fprintf never fails.
///
/// @param stats statistics of a directory
/// @param flags output control flags (F_*)
static void printFooterIfNeeded(struct summary *stats, unsigned int flags)
{
  if (flags & F_SUMMARY) {
    char *summaryCnt = generateSummaryStr(stats);

    printOneLiner();
    if (flags & F_VERBOSE)  fprintf(stdout, "%-68s   %14lld %9lld\n", summaryCnt, stats->size, stats->blocks);
    else  fprintf(stdout, "%s\n", summaryCnt);

    free(summaryCnt);
  }
}


/// @brief recursively process directory @a dn and print its tree.
/// Panic if failed.
///
/// @param dn absolute or relative path string
/// @param pstr prefix string printed in front of each entry
/// @param stats pointer to statistics
/// @param flags output control flags (F_*)
void processDir(const char *dn, const char *pstr, struct summary *stats, unsigned int flags)
{
  DIR *dir;
  struct dirent *entry;
  struct __varray *arr;

  if ((dir = opendir(dn)) == NULL) {
    char *error;

    if (asprintf(&error, "ERROR: %s", strerror(errno)) < 0) panic(strerror(errno));

    printSingleEntry(error, NO_META, flags);

    return;
  }

  arr = createVArray();

  while ((entry = getNext(dir)) != NULL)  pushEntry(arr, entry);

  qsort(arr->head, arr->numElem, sizeof(struct dirent), dirent_compare);

  for(int i = 0; i < arr->numElem; i++) {
    char *nameWithPrefix, *metadata, *filepath;

    updateFileCountIfNeeded(stats, arr->head[i].d_type, flags);

    nameWithPrefix = generateNameStrWithPrefix(pstr, arr->numElem - 1 - i, arr->head[i].d_name, flags);
    filepath = generateFilePath(dn, arr->head[i].d_name);
    metadata = generateMetadataStrIfNeeded(filepath, stats, flags);

    printSingleEntry(nameWithPrefix, metadata, flags);

    free(nameWithPrefix);
    free(filepath);
    free(metadata);

    if (arr->head[i].d_type == DT_DIR) {
      char *nextDn, *nextPstr;

      nextDn = generateDirPathForNextDepth(dn, arr->head[i].d_name);
      nextPstr = generatePrefixForNextDepth(pstr, arr->numElem - 1 - i, flags);

      processDir(nextDn, nextPstr, stats, flags);

      free(nextDn);
      free(nextPstr);
    }
  }
  
  freeVArray(arr);
  closedir(dir);
}


/// @brief print program syntax and an optional error message. Aborts the program with EXIT_FAILURE
///
/// @param argv0 command line argument 0 (executable)
/// @param error optional error (format) string (printf format) or NULL
/// @param ... parameter to the error format string
void syntax(const char *argv0, const char *error, ...)
{
  if (error) {
    va_list ap;

    va_start(ap, error);
    vfprintf(stderr, error, ap);
    va_end(ap);

    printf("\n\n");
  }

  assert(argv0 != NULL);

  fprintf(stderr, "Usage %s [-t] [-s] [-v] [-h] [path...]\n"
                  "Gather information about directory trees. If no path is given, the current directory\n"
                  "is analyzed.\n"
                  "\n"
                  "Options:\n"
                  " -t        print the directory tree (default if no other option specified)\n"
                  " -s        print summary of directories (total number of files, total file size, etc)\n"
                  " -v        print detailed information for each file. Turns on tree view.\n"
                  " -h        print this help\n"
                  " path...   list of space-separated paths (max %d). Default is the current directory.\n",
                  basename(argv0), MAX_DIR);

  exit(EXIT_FAILURE);
}


/// @brief program entry point
int main(int argc, char *argv[])
{
  //
  // default directory is the current directory (".")
  //
  const char CURDIR[] = ".";
  const char *directories[MAX_DIR];
  int   ndir = 0;

  struct summary dstat, tstat;
  unsigned int flags = 0;

  //
  // parse arguments
  //
  for (int i = 1; i < argc; i++) {
    if (argv[i][0] == '-') {
      // format: "-<flag>"
      if      (!strcmp(argv[i], "-t")) flags |= F_TREE;
      else if (!strcmp(argv[i], "-s")) flags |= F_SUMMARY;
      else if (!strcmp(argv[i], "-v")) flags |= F_VERBOSE;
      else if (!strcmp(argv[i], "-h")) syntax(argv[0], NULL);
      else syntax(argv[0], "Unrecognized option '%s'.", argv[i]);
    } else {
      // anything else is recognized as a directory
      if (ndir < MAX_DIR) {
        directories[ndir++] = argv[i];
      } else {
        printf("Warning: maximum number of directories exceeded, ignoring '%s'.\n", argv[i]);
      }
    }
  }

  // if no directory was specified, use the current directory
  if (ndir == 0) directories[ndir++] = CURDIR;


  //
  // process each directory
  //
  // TODO
  clearStats(&tstat);

  for (int i = 0; i < ndir; i++) {
    char *dirNoSlash = generatePathWithoutSlash(directories[i]), *pstr = "";

    clearStats(&dstat);

    printHeaderIfNeeded(flags);
    
    fprintf(stdout, "%s\n", directories[i]);
    processDir(dirNoSlash, pstr, &dstat, flags);

    printFooterIfNeeded(&dstat, flags);

    if (i < ndir - 1 || (flags & F_SUMMARY)) fprintf(stdout, "\n");

    free(dirNoSlash);
    integrateSummary(&dstat, &tstat);
  }

  //
  // print grand total
  //
  if ((flags & F_SUMMARY) && (ndir > 1)) {
    printf("Analyzed %d directories:\n"
           "  total # of files:        %16d\n"
           "  total # of directories:  %16d\n"
           "  total # of links:        %16d\n"
           "  total # of pipes:        %16d\n"
           "  total # of socksets:     %16d\n",
           ndir, tstat.files, tstat.dirs, tstat.links, tstat.fifos, tstat.socks);

    if (flags & F_VERBOSE) {
      printf("  total file size:         %16llu\n"
             "  total # of blocks:       %16llu\n",
             tstat.size, tstat.blocks);
    }

  }

  //
  // that's all, folks
  //
  return EXIT_SUCCESS;
}
