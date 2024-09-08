#include "mfile.h"

char* mfile_test()
{
    static char line[256] = {0};
    static int count = 101;

    FILE *fp = fopen("avmISAVM.txt", "w");
    if (fp) {
        fprintf(fp, "xxxyyyzzz %d\n", count++);
        fclose(fp);
    } else TINY_LOG("open file for write failure %s", strerror(errno));

    fp = fopen("avmISAVM.txt", "r");
    if (fp) {
        if (!fgets(line, sizeof(line), fp)) TINY_LOG("read failure %s", strerror(errno));
    } else TINY_LOG("open file for read failure %s", strerror(errno));

    return line;
}
