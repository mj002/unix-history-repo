/*-
 * Copyright (c) 1992 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rodney Ruddock of the University of Guelph.
 *
 * %sccs.include.redist.c%
 */

#ifndef lint
static char sccsid[] = "@(#)main.c	5.1 (Berkeley) %G%";
#endif /* not lint */

#include "ed.h"

/*
 * This is where all of the "global" variables are declared. They are
 * set for extern in the ed.h header file (so everyone can get them).
 */

int nn_max, nn_max_flag, start_default, End_default, address_flag;
int zsnum, filename_flag, add_flag=0;
int help_flag=0;

#ifdef STDIO
FILE *fhtmp;
int file_seek;
#endif

#ifdef DBI
DB *dbhtmp;
#endif

LINE *nn_max_start, *nn_max_end;

struct MARK mark_matrix[26]; /* in init set all to null */ 

char *text;
char *filename_current, *prompt_string=NULL, help_msg[130];
char *template=NULL;
int prompt_str_flg=0, start_up_flag=0, name_set=0;

LINE *top, *current, *bottom, *start, *End; 
struct u_layer *u_stk;
struct d_layer *d_stk;
LINE *u_current, *u_top, *u_bottom;
int u_set;
regex_t RE_comp;
regmatch_t RE_match[RE_SEC];
int RE_sol=0, RE_flag=0;
char *RE_patt=NULL;

int ss; /* for the getc() */
int explain_flag=1, g_flag=0, GV_flag=0, printsfx=0;
long change_flag=0L;
int line_length;
jmp_buf ctrl_position; /* for SIGnal handling */
int sigint_flag, sighup_flag, sigspecial;

/* SIGINT is never turned off. We flag it happened and then pay attention
 * to it at certain logical locations in the code
 * we don't do more here cause some of our buffer pointer's may be in
 * an inbetween state at the time of the SIGINT. So we flag it happened,
 * let the local fn handle it and do a jump back to the cmd_loop
 */
sigint_handler()

{
  sigint_flag = 1;
  if (sigspecial)  /* unstick when SIGINT on read(stdin) */
    SIGINT_ACTION;
} /* end-sigint_handler */

sighup_handler()

{
  fprintf(stderr,"\n  SIGHUP \n");
  sighup_flag = 1;
  undo();
  do_hup(); /* we shouldn't return from here */
  SIGHUP_ACTION; /* it shouldn't get here */
} /* end-sighup_handler */

/*
 * the command loop. What the command is that the user has specified
 * is determined here. This is not just for commands coming from
 * the terminal but any standard i/o stream; see the global commands.
 * Some of the commands are handled within here (i.e. 'H') while most
 * are handled in their own functions (as called).
 */

void
cmd_loop(inputt, errnum)

FILE *inputt;
int *errnum;

{
  int l_last, l_jmp_flag;
  LINE *l_tempp;

  l_last = 0;

  if (g_flag ==0) /* big, BIG trouble if we don't check! think. */
    {
      /* set the jump point for the signals */
      l_jmp_flag = setjmp(ctrl_position);
      signal(SIGINT, sigint_handler);
      signal(SIGHUP, sighup_handler);
      switch (l_jmp_flag)
            {
              case JMP_SET: break;
              case INTERUPT: /* some general cleanup not specific to the jmp pt */
                             sigint_flag = 0;
                             GV_flag = 0; /* safest place to do these flags */
                             g_flag = 0;
                             printf("?\n");
                             break;
              case HANGUP: /* shouldn't get here. */
                           break;
              default: fprintf(stderr, "Signal jump problem\n");
            }
      /* only do this once! */
      if (start_up_flag)
        {
          start_up_flag = 0;
          /* simulate the 'e' at startup */
          e2(inputt, errnum);
        }
    } /* end-if */

  while (1)
       {

         if (prompt_str_flg == 1)
           printf("%s", prompt_string);
         sigspecial = 1;  /* see the SIGINT function above */
         ss = getc(inputt);
         sigspecial = 0;
         *errnum = 0;
         l_tempp = start = End = NULL;
         start_default = End_default = 1;

         while (1)
         {
         switch (ss)
               {
                 /* this isn't nice and alphabetical mainly because of
                  * retrictions with 'G' and 'V' (see ed(1)).
                  */
                 case 'd': d(inputt, errnum);
                           break;
                 case 'e':
                 case 'E': e(inputt, errnum);
                           break;
                 case 'f': f(inputt, errnum);
                           break;
                 case 'a': 
                 case 'c':
                 case 'i':
                 case 'g':
                 case 'G':
                 case 'v':
                 case 'V': if (GV_flag == 1)
                             {
                               strcpy(help_msg, "command `");
                               strncat(help_msg, &ss, 1);
                               strcat(help_msg, "' illegal in G/V");
                               *errnum = -1;
                               break;
                             }
                           switch (ss)
                                 {
                                   case 'a': a(inputt, errnum);
                                             break;
                                   case 'c': c(inputt, errnum);
                                             break;
                                   case 'i': i(inputt, errnum);
                                             break;
                                    default: g(inputt, errnum);
                                 } /* end-switch */
                           break;
                 case 'h': if (rol(inputt, errnum))
                             break;
                           printf("%s\n", help_msg);
                           *errnum = 1;
                           break;
                 case 'H': if (rol(inputt, errnum))
                             break;
                           if (help_flag == 0)
                             {
                               help_flag = 1;
                               printf("%?: %s\n", help_msg);
                             }
                           else
                             help_flag = 0;
                           *errnum = 1;
                           break;
                 case 'j': j(inputt, errnum);
                           break;
                 case 'k': set_mark(inputt, errnum);
                           break;
                 case 'l': l(inputt, errnum);
                           break;
                 case 'm': m(inputt, errnum);
                           break;
#ifdef POSIX
                           /* in POSIXland 'P' toggles the prompt */
                 case 'P': if (rol(inputt, errnum))
                             break;
                           prompt_str_flg = prompt_str_flg?0:1;
                           *errnum = 1;
                           break;
#endif
                 case '\n': if (GV_flag == 1)
                              return;
                            ungetc(ss, inputt); /* for 'p' to consume */
                            if ((current == bottom) && (End == NULL))
                              {
                                strcpy(help_msg, "at end of buffer");
                                *errnum = -1;
                                break;
                              }
                            current = current->below;
#ifdef BSD
                           /* in BSD 'P'=='p' */
                 case 'P':
#endif
                 case 'p': p(inputt, errnum, 0);
                           break;
                 case 'n': p(inputt, errnum, 1);
                           break;
                           /* an EOF means 'q' unless we're still in the middle
                            * of a global command, in whcih case it was just
                            * the end of the command list found
                            */
                 case EOF: clearerr(inputt);
                           if (g_flag > 0)
                             return;
                           ss = 'q';
                 case 'q':
                 case 'Q': q(inputt, errnum);
                           break;
                 case 'r': r(inputt, errnum);
                           break;
                 case 's': s(inputt, errnum);
                           break;
                 case 't': t(inputt, errnum);
                           break;
                 case 'u': u(inputt, errnum);
                           break;
                 case 'w':
                 case 'W': w(inputt, errnum);
                           break;
                 case 'z': z(inputt, errnum);
                           break;
                 case '!': bang (inputt, errnum);
                           break;
                 case '=': equal (inputt, errnum);
                           break;
                         /* control of address forms from here down */
                         /* It's a head-game to understand why ";" and ","
                          * look as they do below, but a lot of it has to
                          * do with ";" and "," being special address pair
                          * forms themselves and the compatibility for
                          * address "chains".
                          */
                 case ';': if ((End_default == 1) && (start_default == 1))
                             {
                               start = current;
                               End = bottom;
                               start_default = End_default = 0;
                             }
                           else
                             {
                               start = current = End;
                               start_default = 0;
                               End_default = 1;
                             }
                           l_tempp = NULL;
                           break;
                           /* note address ".,x" where x is a cmd is legal;
                            * not a bug - for backward compatability */
                 case ',': if ((End_default == 1) && (start_default == 1))
                             {
                               start = top;
                               End = bottom;
                               start_default = End_default = 0;
                             }
                           else
                             {
                               start = End;
                               start_default = 0;
                               End_default = 1;
                             }
                           l_tempp = NULL;
                           break;
                 case '%': if (End_default == 0)
                             {
                               strcpy(help_msg, "'%' is an address pair");
                               *errnum = -1;
                               break;
                             }
                           start = top;
                           End = bottom;
                           start_default = End_default = 0;
                           l_tempp = NULL;
                           break;
                 case ' ': /* within address_conv=>l_last='+', foobar, but
                              historical and now POSIX... */
                           break;
                 case '0':
                 case '1':
                 case '2':
                 case '3':
                 case '4':
                 case '5':
                 case '6':
                 case '7':
                 case '8':
                 case '9':
                 case '-':
                 case '^':
                 case '+':
                 case '\'':
                 case '$':
                 case '?':
                 case '/':
                 case '.': ungetc(ss, inputt);
                           if ((start_default == 0) && (End_default == 0))
                             {
                               strcpy(help_msg, "badly formed address");
                               *errnum = -1;
                               break;
                             }
                           ss = l_last;
                           l_tempp = address_conv(l_tempp, inputt, errnum);
                           if (*errnum < 0)
                             break;
                           End = l_tempp;
                           End_default = 0;
                           if (start_default == 0)
                             *errnum = address_check(start, End);
                           break;
                 default: *errnum = -1;
                          strcpy(help_msg, "unknown command");
                          break;
               } /* end-switch(ss) */

          if (*errnum > 0)
            {
              /* things came out okay with the last command */
              if (GV_flag == 1)
                return;
              /* do the suffixes if there were any */
              if (printsfx > 0)
                {
                  start = End = current;
                  ungetc(ss, inputt);
                  if (printsfx == 1)
                    p(inputt, errnum, 0);
                  else if (printsfx == 2)
                    p(inputt, errnum, 1);
                  else if (printsfx == 4)
                    l(inputt, errnum);
                  if (*errnum < 0)
                    goto errmsg; /* unlikely it's needed, but... */
                }
              break;
            }
          else if (*errnum < 0)
            {
errmsg:
              /* there was a problem with the last command */
              while (((ss=getc(inputt)) != '\n') && (ss != EOF))
                   ;
              if (help_flag == 1)
                printf("%?: %s\n", help_msg);
              else
                printf("?\n");
              if (g_flag > 0)
                  return;
              break;
            }
          l_last = ss;
          ss = getc(inputt);
          } /* second while */
          
       } /* end-while(1) */
} /* end-cmd_loop */


/* exits ed and prints an appropriate message about the command line
 * being malformed (see below).
 */

void
ed_exit(err)

int err;

{
  switch (err)
        {
          case 1: fprintf(stderr, "ed: illegal option\n");
                  break;
          case 2: fprintf(stderr, "ed: missing promptstring\n");
                  break;
          case 3: fprintf(stderr, "ed: too many filenames\n");
                  break;
          case 4: fprintf(stderr, "ed: out of memory error\n");
                  break;
          default: fprintf(stderr, "ed: command line error\n");
                   break;
        } /* end-switch */
  fprintf(stderr, "ed: ed [ -s ] [ -p promtstring ] [ filename ]\n");
  exit(1);
} /* end-ed_exit */


/*
 * Starts the whole show going. Set things up as the arguments spec
 * in the shell and set a couple of the global variables.
 */

void
main(argc, argv)

int argc;
char *argv[];

{
  int l_num, errnum=0, l_err=0; /* note naming viol'n with errnum for consistancy */
  char *l_fnametmp;
#ifdef DBI
  RECNOINFO l_dbaccess;
#endif
  /* termcap isn't really need unless you move back and forth between 80 and
   * 123 column terminals. And if your system is wacked and you have to use
   * ed because it's the only reliable editor (sysadmin-types know what I'm
   * talking about), you likely don't want the termcap routines.
   * See the Makefile about compile options.
   */
#ifndef NOTERMCAP
  int l_ret;
  char l_bp[1024], *l_term;
#endif

  setuid(getuid()); /* in case some fool does suid on ed */

#ifndef NOTERMCAP
  l_term = getenv("TERM");
  l_ret = tgetent(l_bp, l_term);
  if (l_ret < 1)
    line_length = 78; /* reasonable number for all term's */
  else if ((line_length = tgetnum("co") - 1) < 2)
#endif
    line_length = 78;
  
  start = End = NULL;
  top = bottom = NULL;
  current = NULL;
  nn_max_flag = 0;
  nn_max_start = nn_max_end = NULL;
  l_fnametmp = (char *)calloc(FILENAME_LEN, sizeof(char));
  if (l_fnametmp == NULL)
    ed_exit(4);
  text = (char *)calloc(NN_MAX_START+2, sizeof(char));
  if (text == NULL)
    ed_exit(4);
  start_default = End_default = 0;
  zsnum = 22; /* for the 'z' command */
  u_stk = NULL;
  d_stk = NULL;
  u_current = u_top = u_bottom = NULL;
  u_set = 0; /* for in d after a j */
  filename_flag = 0;
  filename_current = NULL;

  l_num = 1;
  while (1)
       {
         /* process the command line options */
         if (l_num >= argc)
           break;
         switch (argv[l_num][0])
               {
                 case '-':
                           switch (argv[l_num][1])
                                 {
                                   case '\0': /* this is why 'getopt' not used */
                                   case 's': explain_flag = 0;
                                             break;
                                   case 'p': if (++l_num < argc)
                                               {
                                                 prompt_string = (char *)calloc(strlen(argv[l_num]), sizeof(char));
                                                 if (prompt_string == NULL)
                                                   ed_exit(4);
                                                 strcpy(prompt_string, argv[l_num]);
                                                 prompt_str_flg = 1;
                                                 break;
                                               }
                                             l_err = 1;
                                   default: l_err++;
                                            ed_exit(l_err);
                                 } /* end-switch */
                           break;
                 default:  if (name_set)
                             ed_exit(3);
                           strcpy(l_fnametmp, argv[l_num]);
                           filename_current = l_fnametmp;
                           name_set = 1;
                           if (prompt_str_flg)
                             break;
                           /* default ed prompt */
                           prompt_string = (char *)calloc(3, sizeof(char));
                           strcpy(prompt_string, "*");
                           break;
               } /* end-switch */
         l_num++;
       } /* end-while(1) */

  start_up_flag = 1;
  cmd_loop(stdin, &errnum);
} /* end-main */
