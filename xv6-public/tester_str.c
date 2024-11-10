#include <stdio.h>
char *str = "You can't change a character!";
int main() {
    str[1] = 'O';
    printf("%s\n", str);
    return 0;
}

