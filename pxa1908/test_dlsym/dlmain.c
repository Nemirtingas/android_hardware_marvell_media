#include <stdio.h>

__attribute__ ((visibility ("hidden"))) void the_printer(char* message)
{
    printf("%s\n", message);
}

extern void func1()
{
    printf("In func1(%p), calling the_printer at %p\n", func1, the_printer);
    the_printer("Test func1");
}

extern void func2()
{
    printf("In func2(%p), calling the_printer at %p\n", func2, the_printer);
    the_printer("Test func2");
}
