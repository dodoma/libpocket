#include "mfile.h"

char* mfile_test(char *dirname)
{
    static char line[256] = {0};
    static int count = 101;
    char filename[PATH_MAX];

    if (!dirname) return "unknown path";

    snprintf(filename, sizeof(filename), "%s/avmISAVM.txt", dirname);

#if 0
    FILE *fp = fopen(filename, "w");
    if (fp) {
        fprintf(fp, "xxxyyyzzz %d\n", count++);
        fclose(fp);
    } else TINY_LOG("open file for write failure %s %s", filename, strerror(errno));
#endif

    FILE *fp = fopen(filename, "r");
    if (fp) {
        if (!fgets(line, sizeof(line), fp)) TINY_LOG("read failure %s", strerror(errno));
    } else TINY_LOG("open file for read failure %s %s", filename, strerror(errno));

    return line;
}
