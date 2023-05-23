#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>


int main() {
    FILE* fp = fopen("H:/cprogramming/csse2310-sem1-s4759487/trunk/a4/jobfile.txt","w");
    fprintf(fp,"#ignore this line\r\n");
    fprintf(fp,"crack azkOmxs9hZUpg 1\r\n");
    fclose(fp);
    return 0;
}