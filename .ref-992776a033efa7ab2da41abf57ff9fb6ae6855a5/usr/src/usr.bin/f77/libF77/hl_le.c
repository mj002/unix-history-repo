/*
 *	"@(#)hl_le.c	1.1"
 */

short hl_le(a,b,la,lb)
char *a, *b;
long int la, lb;
{
return(s_cmp(a,b,la,lb) <= 0);
}
