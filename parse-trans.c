#include <stdio.h>
#include <string.h>
#include <err.h>

int main()
{
	FILE *f = fopen("", "rt");
	if (f == NULL)
		err(1, "cannot open transactions csv file");
	
	
}
