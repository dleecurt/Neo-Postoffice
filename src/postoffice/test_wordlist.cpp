#include "chop.h"
#include <stdio.h>

main()
{
	char buf[1024];
	while (!feof (stdin))
	{
		buf[0] = 0;
		fgets (buf, 1023, stdin);
		if (!feof (stdin))
		{
			char *tmp = chopwincrap (buf);
			printf ("%s\n", tmp);
			delete[] tmp;
		}
	}
}