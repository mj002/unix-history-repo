/* Part of CPP library.  (Macro and #define handling.)
   Copyright (C) 1986, 1987, 1989, 1992, 1993, 1994, 1995, 1996, 1998,
   1999, 2000, 2001, 2002, 2003 Free Software Foundation, Inc.
   Written by Per Bothner, 1994.
   Based on CCCP program by Paul Rubin, June 1986
   Adapted to ANSI C, Richard Stallman, Jan 1987

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

 In other words, you are welcome to use, share and improve this program.
 You are forbidden to forbid anyone else to use, share and improve
 what you give them.   Help stamp out software-hoarding!  */

#include "config.h"
#include "system.h"
#include "cpplib.h"
#include "cpphash.h"

typedef struct macro_arg macro_arg;
struct macro_arg
{
  const cpp_token **first;	/* First token in unexpanded argument.  */
  const cpp_token **expanded;	/* Macro-expanded argument.  */
  const cpp_token *stringified;	/* Stringified argument.  */
  unsigned int count;		/* # of tokens in argument.  */
  unsigned int expanded_count;	/* # of tokens in expanded argument.  */
};

/* Macro expansion.  */

static int enter_macro_context (cpp_reader *, cpp_hashnode *);
static int builtin_macro (cpp_reader *, cpp_hashnode *);
static void push_token_context (cpp_reader *, cpp_hashnode *,
				const cpp_token *, unsigned int);
static void push_ptoken_context (cpp_reader *, cpp_hashnode *, _cpp_buff *,
				 const cpp_token **, unsigned int);
static _cpp_buff *collect_args (cpp_reader *, const cpp_hashnode *);
static cpp_context *next_context (cpp_reader *);
static const cpp_token *padding_token (cpp_reader *, const cpp_token *);
static void expand_arg (cpp_reader *, macro_arg *);
static const cpp_token *new_string_token (cpp_reader *, uchar *, unsigned int);
static const cpp_token *stringify_arg (cpp_reader *, macro_arg *);
static void paste_all_tokens (cpp_reader *, const cpp_token *);
static bool paste_tokens (cpp_reader *, const cpp_token **, const cpp_token *);
static void replace_args (cpp_reader *, cpp_hashnode *, cpp_macro *,
			  macro_arg *);
static _cpp_buff *funlike_invocation_p (cpp_reader *, cpp_hashnode *);
static bool create_iso_definition (cpp_reader *, cpp_macro *);

/* #define directive parsing and handling.  */

static cpp_token *alloc_expansion_token (cpp_reader *, cpp_macro *);
static cpp_token *lex_expansion_token (cpp_reader *, cpp_macro *);
static bool warn_of_redefinition (cpp_reader *, const cpp_hashnode *,
				  const cpp_macro *);
static bool parse_params (cpp_reader *, cpp_macro *);
static void check_trad_stringification (cpp_reader *, const cpp_macro *,
					const cpp_string *);

/* Emits a warning if NODE is a macro defined in the main file that
   has not been used.  */
int
_cpp_warn_if_unused_macro (cpp_reader *pfile, cpp_hashnode *node,
			   void *v ATTRIBUTE_UNUSED)
{
  if (node->type == NT_MACRO && !(node->flags & NODE_BUILTIN))
    {
      cpp_macro *macro = node->value.macro;

      if (!macro->used
	  && MAIN_FILE_P (linemap_lookup (&pfile->line_maps, macro->line)))
	cpp_error_with_line (pfile, CPP_DL_WARNING, macro->line, 0,
			     "macro \"%s\" is not used", NODE_NAME (node));
    }

  return 1;
}

/* Allocates and returns a CPP_STRING token, containing TEXT of length
   LEN, after null-terminating it.  TEXT must be in permanent storage.  */
static const cpp_token *
new_string_token (cpp_reader *pfile, unsigned char *text, unsigned int len)
{
  cpp_token *token = _cpp_temp_token (pfile);

  text[len] = '\0';
  token->type = CPP_STRING;
  token->val.str.len = len;
  token->val.str.text = text;
  token->flags = 0;
  return token;
}

static const char * const monthnames[] =
{
  "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/* Handle builtin macros like __FILE__, and push the resulting token
   on the context stack.  Also handles _Pragma, for which no new token
   is created.  Returns 1 if it generates a new token context, 0 to
   return the token to the caller.  */
const uchar *
_cpp_builtin_macro_text (cpp_reader *pfile, cpp_hashnode *node)
{
  const uchar *result = NULL;
  unsigned int number = 1;

  switch (node->value.builtin)
    {
    default:
      cpp_error (pfile, CPP_DL_ICE, "invalid built-in macro \"%s\"",
		 NODE_NAME (node));
      break;

    case BT_FILE:
    case BT_BASE_FILE:
      {
	unsigned int len;
	const char *name;
	uchar *buf;
	const struct line_map *map = pfile->map;

	if (node->value.builtin == BT_BASE_FILE)
	  while (! MAIN_FILE_P (map))
	    map = INCLUDED_FROM (&pfile->line_maps, map);

	name = map->to_file;
	len = strlen (name);
	buf = _cpp_unaligned_alloc (pfile, len * 4 + 3);
	result = buf;
	*buf = '"';
	buf = cpp_quote_string (buf + 1, (const unsigned char *) name, len);
	*buf++ = '"';
	*buf = '\0';
      }
      break;

    case BT_INCLUDE_LEVEL:
      /* The line map depth counts the primary source as level 1, but
	 historically __INCLUDE_DEPTH__ has called the primary source
	 level 0.  */
      number = pfile->line_maps.depth - 1;
      break;

    case BT_SPECLINE:
      /* If __LINE__ is embedded in a macro, it must expand to the
	 line of the macro's invocation, not its definition.
	 Otherwise things like assert() will not work properly.  */
      if (CPP_OPTION (pfile, traditional))
	number = pfile->line;
      else
	number = pfile->cur_token[-1].line;
      number = SOURCE_LINE (pfile->map, number);
      break;

      /* __STDC__ has the value 1 under normal circumstances.
	 However, if (a) we are in a system header, (b) the option
	 stdc_0_in_system_headers is true (set by target config), and
	 (c) we are not in strictly conforming mode, then it has the
	 value 0.  */
    case BT_STDC:
      {
	if (CPP_IN_SYSTEM_HEADER (pfile)
	    && CPP_OPTION (pfile, stdc_0_in_system_headers)
	    && !CPP_OPTION (pfile,std))
	  number = 0;
	else
	  number = 1;
      }
      break;

    case BT_DATE:
    case BT_TIME:
      if (pfile->date == NULL)
	{
	  /* Allocate __DATE__ and __TIME__ strings from permanent
	     storage.  We only do this once, and don't generate them
	     at init time, because time() and localtime() are very
	     slow on some systems.  */
	  time_t tt;
	  struct tm *tb = NULL;

	  /* (time_t) -1 is a legitimate value for "number of seconds
	     since the Epoch", so we have to do a little dance to
	     distinguish that from a genuine error.  */
	  errno = 0;
	  tt = time(NULL);
	  if (tt != (time_t)-1 || errno == 0)
	    tb = localtime (&tt);

	  if (tb)
	    {
	      pfile->date = _cpp_unaligned_alloc (pfile,
						  sizeof ("\"Oct 11 1347\""));
	      sprintf ((char *) pfile->date, "\"%s %2d %4d\"",
		       monthnames[tb->tm_mon], tb->tm_mday,
		       tb->tm_year + 1900);

	      pfile->time = _cpp_unaligned_alloc (pfile,
						  sizeof ("\"12:34:56\""));
	      sprintf ((char *) pfile->time, "\"%02d:%02d:%02d\"",
		       tb->tm_hour, tb->tm_min, tb->tm_sec);
	    }
	  else
	    {
	      cpp_errno (pfile, CPP_DL_WARNING,
			 "could not determine date and time");
		
	      pfile->date = U"\"??? ?? ????\"";
	      pfile->time = U"\"??:??:??\"";
	    }
	}

      if (node->value.builtin == BT_DATE)
	result = pfile->date;
      else
	result = pfile->time;
      break;
    }

  if (result == NULL)
    {
      /* 21 bytes holds all NUL-terminated unsigned 64-bit numbers.  */
      result = _cpp_unaligned_alloc (pfile, 21);
      sprintf ((char *) result, "%u", number);
    }

  return result;      
}

/* Convert builtin macros like __FILE__ to a token and push it on the
   context stack.  Also handles _Pragma, for which no new token is
   created.  Returns 1 if it generates a new token context, 0 to
   return the token to the caller.  */
static int
builtin_macro (cpp_reader *pfile, cpp_hashnode *node)
{
  const uchar *buf;
  size_t len;
  char *nbuf;

  if (node->value.builtin == BT_PRAGMA)
    {
      /* Don't interpret _Pragma within directives.  The standard is
         not clear on this, but to me this makes most sense.  */
      if (pfile->state.in_directive)
	return 0;

      _cpp_do__Pragma (pfile);
      return 1;
    }

  buf = _cpp_builtin_macro_text (pfile, node);
  len = ustrlen (buf);
  nbuf = alloca (len + 1);
  memcpy (nbuf, buf, len);
  nbuf[len]='\n';

  cpp_push_buffer (pfile, (uchar *) nbuf, len, /* from_stage3 */ true);
  _cpp_clean_line (pfile);

  /* Set pfile->cur_token as required by _cpp_lex_direct.  */
  pfile->cur_token = _cpp_temp_token (pfile);
  push_token_context (pfile, NULL, _cpp_lex_direct (pfile), 1);
  if (pfile->buffer->cur != pfile->buffer->rlimit)
    cpp_error (pfile, CPP_DL_ICE, "invalid built-in macro \"%s\"",
	       NODE_NAME (node));
  _cpp_pop_buffer (pfile);

  return 1;
}

/* Copies SRC, of length LEN, to DEST, adding backslashes before all
   backslashes and double quotes.  Non-printable characters are
   converted to octal.  DEST must be of sufficient size.  Returns
   a pointer to the end of the string.  */
uchar *
cpp_quote_string (uchar *dest, const uchar *src, unsigned int len)
{
  while (len--)
    {
      uchar c = *src++;

      if (c == '\\' || c == '"')
	{
	  *dest++ = '\\';
	  *dest++ = c;
	}
      else
	{
	  if (ISPRINT (c))
	    *dest++ = c;
	  else
	    {
	      sprintf ((char *) dest, "\\%03o", c);
	      dest += 4;
	    }
	}
    }

  return dest;
}

/* Convert a token sequence ARG to a single string token according to
   the rules of the ISO C #-operator.  */
static const cpp_token *
stringify_arg (cpp_reader *pfile, macro_arg *arg)
{
  unsigned char *dest;
  unsigned int i, escape_it, backslash_count = 0;
  const cpp_token *source = NULL;
  size_t len;

  if (BUFF_ROOM (pfile->u_buff) < 3)
    _cpp_extend_buff (pfile, &pfile->u_buff, 3);
  dest = BUFF_FRONT (pfile->u_buff);
  *dest++ = '"';

  /* Loop, reading in the argument's tokens.  */
  for (i = 0; i < arg->count; i++)
    {
      const cpp_token *token = arg->first[i];

      if (token->type == CPP_PADDING)
	{
	  if (source == NULL)
	    source = token->val.source;
	  continue;
	}

      escape_it = (token->type == CPP_STRING || token->type == CPP_WSTRING
		   || token->type == CPP_CHAR || token->type == CPP_WCHAR);

      /* Room for each char being written in octal, initial space and
	 final quote and NUL.  */
      len = cpp_token_len (token);
      if (escape_it)
	len *= 4;
      len += 3;

      if ((size_t) (BUFF_LIMIT (pfile->u_buff) - dest) < len)
	{
	  size_t len_so_far = dest - BUFF_FRONT (pfile->u_buff);
	  _cpp_extend_buff (pfile, &pfile->u_buff, len);
	  dest = BUFF_FRONT (pfile->u_buff) + len_so_far;
	}

      /* Leading white space?  */
      if (dest - 1 != BUFF_FRONT (pfile->u_buff))
	{
	  if (source == NULL)
	    source = token;
	  if (source->flags & PREV_WHITE)
	    *dest++ = ' ';
	}
      source = NULL;

      if (escape_it)
	{
	  _cpp_buff *buff = _cpp_get_buff (pfile, len);
	  unsigned char *buf = BUFF_FRONT (buff);
	  len = cpp_spell_token (pfile, token, buf) - buf;
	  dest = cpp_quote_string (dest, buf, len);
	  _cpp_release_buff (pfile, buff);
	}
      else
	dest = cpp_spell_token (pfile, token, dest);

      if (token->type == CPP_OTHER && token->val.str.text[0] == '\\')
	backslash_count++;
      else
	backslash_count = 0;
    }

  /* Ignore the final \ of invalid string literals.  */
  if (backslash_count & 1)
    {
      cpp_error (pfile, CPP_DL_WARNING,
		 "invalid string literal, ignoring final '\\'");
      dest--;
    }

  /* Commit the memory, including NUL, and return the token.  */
  *dest++ = '"';
  len = dest - BUFF_FRONT (pfile->u_buff);
  BUFF_FRONT (pfile->u_buff) = dest + 1;
  return new_string_token (pfile, dest - len, len);
}

/* Try to paste two tokens.  On success, return nonzero.  In any
   case, PLHS is updated to point to the pasted token, which is
   guaranteed to not have the PASTE_LEFT flag set.  */
static bool
paste_tokens (cpp_reader *pfile, const cpp_token **plhs, const cpp_token *rhs)
{
  unsigned char *buf, *end;
  const cpp_token *lhs;
  unsigned int len;
  bool valid;

  lhs = *plhs;
  len = cpp_token_len (lhs) + cpp_token_len (rhs) + 1;
  buf = alloca (len);
  end = cpp_spell_token (pfile, lhs, buf);

  /* Avoid comment headers, since they are still processed in stage 3.
     It is simpler to insert a space here, rather than modifying the
     lexer to ignore comments in some circumstances.  Simply returning
     false doesn't work, since we want to clear the PASTE_LEFT flag.  */
  if (lhs->type == CPP_DIV && rhs->type != CPP_EQ)
    *end++ = ' ';
  end = cpp_spell_token (pfile, rhs, end);
  *end = '\n';

  cpp_push_buffer (pfile, buf, end - buf, /* from_stage3 */ true);
  _cpp_clean_line (pfile);

  /* Set pfile->cur_token as required by _cpp_lex_direct.  */
  pfile->cur_token = _cpp_temp_token (pfile);
  *plhs = _cpp_lex_direct (pfile);
  valid = pfile->buffer->cur == pfile->buffer->rlimit;
  _cpp_pop_buffer (pfile);

  return valid;
}

/* Handles an arbitrarily long sequence of ## operators, with initial
   operand LHS.  This implementation is left-associative,
   non-recursive, and finishes a paste before handling succeeding
   ones.  If a paste fails, we back up to the RHS of the failing ##
   operator before pushing the context containing the result of prior
   successful pastes, with the effect that the RHS appears in the
   output stream after the pasted LHS normally.  */
static void
paste_all_tokens (cpp_reader *pfile, const cpp_token *lhs)
{
  const cpp_token *rhs;
  cpp_context *context = pfile->context;

  do
    {
      /* Take the token directly from the current context.  We can do
	 this, because we are in the replacement list of either an
	 object-like macro, or a function-like macro with arguments
	 inserted.  In either case, the constraints to #define
	 guarantee we have at least one more token.  */
      if (context->direct_p)
	rhs = FIRST (context).token++;
      else
	rhs = *FIRST (context).ptoken++;

      if (rhs->type == CPP_PADDING)
	abort ();

      if (!paste_tokens (pfile, &lhs, rhs))
	{
	  _cpp_backup_tokens (pfile, 1);

	  /* Mandatory error for all apart from assembler.  */
	  if (CPP_OPTION (pfile, lang) != CLK_ASM)
	    cpp_error (pfile, CPP_DL_ERROR,
	 "pasting \"%s\" and \"%s\" does not give a valid preprocessing token",
		       cpp_token_as_text (pfile, lhs),
		       cpp_token_as_text (pfile, rhs));
	  break;
	}
    }
  while (rhs->flags & PASTE_LEFT);

  /* Put the resulting token in its own context.  */
  push_token_context (pfile, NULL, lhs, 1);
}

/* Returns TRUE if the number of arguments ARGC supplied in an
   invocation of the MACRO referenced by NODE is valid.  An empty
   invocation to a macro with no parameters should pass ARGC as zero.

   Note that MACRO cannot necessarily be deduced from NODE, in case
   NODE was redefined whilst collecting arguments.  */
bool
_cpp_arguments_ok (cpp_reader *pfile, cpp_macro *macro, const cpp_hashnode *node, unsigned int argc)
{
  if (argc == macro->paramc)
    return true;

  if (argc < macro->paramc)
    {
      /* As an extension, a rest argument is allowed to not appear in
	 the invocation at all.
	 e.g. #define debug(format, args...) something
	 debug("string");

	 This is exactly the same as if there had been an empty rest
	 argument - debug("string", ).  */

      if (argc + 1 == macro->paramc && macro->variadic)
	{
	  if (CPP_PEDANTIC (pfile) && ! macro->syshdr)
	    cpp_error (pfile, CPP_DL_PEDWARN,
		       "ISO C99 requires rest arguments to be used");
	  return true;
	}

      cpp_error (pfile, CPP_DL_ERROR,
		 "macro \"%s\" requires %u arguments, but only %u given",
		 NODE_NAME (node), macro->paramc, argc);
    }
  else
    cpp_error (pfile, CPP_DL_ERROR,
	       "macro \"%s\" passed %u arguments, but takes just %u",
	       NODE_NAME (node), argc, macro->paramc);

  return false;
}

/* Reads and returns the arguments to a function-like macro
   invocation.  Assumes the opening parenthesis has been processed.
   If there is an error, emits an appropriate diagnostic and returns
   NULL.  Each argument is terminated by a CPP_EOF token, for the
   future benefit of expand_arg().  */
static _cpp_buff *
collect_args (cpp_reader *pfile, const cpp_hashnode *node)
{
  _cpp_buff *buff, *base_buff;
  cpp_macro *macro;
  macro_arg *args, *arg;
  const cpp_token *token;
  unsigned int argc;

  macro = node->value.macro;
  if (macro->paramc)
    argc = macro->paramc;
  else
    argc = 1;
  buff = _cpp_get_buff (pfile, argc * (50 * sizeof (cpp_token *)
				       + sizeof (macro_arg)));
  base_buff = buff;
  args = (macro_arg *) buff->base;
  memset (args, 0, argc * sizeof (macro_arg));
  buff->cur = (unsigned char *) &args[argc];
  arg = args, argc = 0;

  /* Collect the tokens making up each argument.  We don't yet know
     how many arguments have been supplied, whether too many or too
     few.  Hence the slightly bizarre usage of "argc" and "arg".  */
  do
    {
      unsigned int paren_depth = 0;
      unsigned int ntokens = 0;

      argc++;
      arg->first = (const cpp_token **) buff->cur;

      for (;;)
	{
	  /* Require space for 2 new tokens (including a CPP_EOF).  */
	  if ((unsigned char *) &arg->first[ntokens + 2] > buff->limit)
	    {
	      buff = _cpp_append_extend_buff (pfile, buff,
					      1000 * sizeof (cpp_token *));
	      arg->first = (const cpp_token **) buff->cur;
	    }

	  token = cpp_get_token (pfile);

	  if (token->type == CPP_PADDING)
	    {
	      /* Drop leading padding.  */
	      if (ntokens == 0)
		continue;
	    }
	  else if (token->type == CPP_OPEN_PAREN)
	    paren_depth++;
	  else if (token->type == CPP_CLOSE_PAREN)
	    {
	      if (paren_depth-- == 0)
		break;
	    }
	  else if (token->type == CPP_COMMA)
	    {
	      /* A comma does not terminate an argument within
		 parentheses or as part of a variable argument.  */
	      if (paren_depth == 0
		  && ! (macro->variadic && argc == macro->paramc))
		break;
	    }
	  else if (token->type == CPP_EOF
		   || (token->type == CPP_HASH && token->flags & BOL))
	    break;

	  arg->first[ntokens++] = token;
	}

      /* Drop trailing padding.  */
      while (ntokens > 0 && arg->first[ntokens - 1]->type == CPP_PADDING)
	ntokens--;

      arg->count = ntokens;
      arg->first[ntokens] = &pfile->eof;

      /* Terminate the argument.  Excess arguments loop back and
	 overwrite the final legitimate argument, before failing.  */
      if (argc <= macro->paramc)
	{
	  buff->cur = (unsigned char *) &arg->first[ntokens + 1];
	  if (argc != macro->paramc)
	    arg++;
	}
    }
  while (token->type != CPP_CLOSE_PAREN && token->type != CPP_EOF);

  if (token->type == CPP_EOF)
    {
      /* We still need the CPP_EOF to end directives, and to end
	 pre-expansion of a macro argument.  Step back is not
	 unconditional, since we don't want to return a CPP_EOF to our
	 callers at the end of an -include-d file.  */
      if (pfile->context->prev || pfile->state.in_directive)
	_cpp_backup_tokens (pfile, 1);
      cpp_error (pfile, CPP_DL_ERROR,
		 "unterminated argument list invoking macro \"%s\"",
		 NODE_NAME (node));
    }
  else
    {
      /* A single empty argument is counted as no argument.  */
      if (argc == 1 && macro->paramc == 0 && args[0].count == 0)
	argc = 0;
      if (_cpp_arguments_ok (pfile, macro, node, argc))
	{
	  /* GCC has special semantics for , ## b where b is a varargs
	     parameter: we remove the comma if b was omitted entirely.
	     If b was merely an empty argument, the comma is retained.
	     If the macro takes just one (varargs) parameter, then we
	     retain the comma only if we are standards conforming.

	     If FIRST is NULL replace_args () swallows the comma.  */
	  if (macro->variadic && (argc < macro->paramc
				  || (argc == 1 && args[0].count == 0
				      && !CPP_OPTION (pfile, std))))
	    args[macro->paramc - 1].first = NULL;
	  return base_buff;
	}
    }

  /* An error occurred.  */
  _cpp_release_buff (pfile, base_buff);
  return NULL;
}

/* Search for an opening parenthesis to the macro of NODE, in such a
   way that, if none is found, we don't lose the information in any
   intervening padding tokens.  If we find the parenthesis, collect
   the arguments and return the buffer containing them.  */
static _cpp_buff *
funlike_invocation_p (cpp_reader *pfile, cpp_hashnode *node)
{
  const cpp_token *token, *padding = NULL;

  for (;;)
    {
      token = cpp_get_token (pfile);
      if (token->type != CPP_PADDING)
	break;
      if (padding == NULL
	  || (!(padding->flags & PREV_WHITE) && token->val.source == NULL))
	padding = token;
    }

  if (token->type == CPP_OPEN_PAREN)
    {
      pfile->state.parsing_args = 2;
      return collect_args (pfile, node);
    }

  /* CPP_EOF can be the end of macro arguments, or the end of the
     file.  We mustn't back up over the latter.  Ugh.  */
  if (token->type != CPP_EOF || token == &pfile->eof)
    {
      /* Back up.  We may have skipped padding, in which case backing
	 up more than one token when expanding macros is in general
	 too difficult.  We re-insert it in its own context.  */
      _cpp_backup_tokens (pfile, 1);
      if (padding)
	push_token_context (pfile, NULL, padding, 1);
    }

  return NULL;
}

/* Push the context of a macro with hash entry NODE onto the context
   stack.  If we can successfully expand the macro, we push a context
   containing its yet-to-be-rescanned replacement list and return one.
   Otherwise, we don't push a context and return zero.  */
static int
enter_macro_context (cpp_reader *pfile, cpp_hashnode *node)
{
  /* The presence of a macro invalidates a file's controlling macro.  */
  pfile->mi_valid = false;

  pfile->state.angled_headers = false;

  /* Handle standard macros.  */
  if (! (node->flags & NODE_BUILTIN))
    {
      cpp_macro *macro = node->value.macro;

      if (macro->fun_like)
	{
	  _cpp_buff *buff;

	  pfile->state.prevent_expansion++;
	  pfile->keep_tokens++;
	  pfile->state.parsing_args = 1;
	  buff = funlike_invocation_p (pfile, node);
	  pfile->state.parsing_args = 0;
	  pfile->keep_tokens--;
	  pfile->state.prevent_expansion--;

	  if (buff == NULL)
	    {
	      if (CPP_WTRADITIONAL (pfile) && ! node->value.macro->syshdr)
		cpp_error (pfile, CPP_DL_WARNING,
 "function-like macro \"%s\" must be used with arguments in traditional C",
			   NODE_NAME (node));

	      return 0;
	    }

	  if (macro->paramc > 0)
	    replace_args (pfile, node, macro, (macro_arg *) buff->base);
	  _cpp_release_buff (pfile, buff);
	}

      /* Disable the macro within its expansion.  */
      node->flags |= NODE_DISABLED;

      macro->used = 1;

      if (macro->paramc == 0)
	push_token_context (pfile, node, macro->exp.tokens, macro->count);

      return 1;
    }

  /* Handle built-in macros and the _Pragma operator.  */
  return builtin_macro (pfile, node);
}

/* Replace the parameters in a function-like macro of NODE with the
   actual ARGS, and place the result in a newly pushed token context.
   Expand each argument before replacing, unless it is operated upon
   by the # or ## operators.  */
static void
replace_args (cpp_reader *pfile, cpp_hashnode *node, cpp_macro *macro, macro_arg *args)
{
  unsigned int i, total;
  const cpp_token *src, *limit;
  const cpp_token **dest, **first;
  macro_arg *arg;
  _cpp_buff *buff;

  /* First, fully macro-expand arguments, calculating the number of
     tokens in the final expansion as we go.  The ordering of the if
     statements below is subtle; we must handle stringification before
     pasting.  */
  total = macro->count;
  limit = macro->exp.tokens + macro->count;

  for (src = macro->exp.tokens; src < limit; src++)
    if (src->type == CPP_MACRO_ARG)
      {
	/* Leading and trailing padding tokens.  */
	total += 2;

	/* We have an argument.  If it is not being stringified or
	   pasted it is macro-replaced before insertion.  */
	arg = &args[src->val.arg_no - 1];

	if (src->flags & STRINGIFY_ARG)
	  {
	    if (!arg->stringified)
	      arg->stringified = stringify_arg (pfile, arg);
	  }
	else if ((src->flags & PASTE_LEFT)
		 || (src > macro->exp.tokens && (src[-1].flags & PASTE_LEFT)))
	  total += arg->count - 1;
	else
	  {
	    if (!arg->expanded)
	      expand_arg (pfile, arg);
	    total += arg->expanded_count - 1;
	  }
      }

  /* Now allocate space for the expansion, copy the tokens and replace
     the arguments.  */
  buff = _cpp_get_buff (pfile, total * sizeof (cpp_token *));
  first = (const cpp_token **) buff->base;
  dest = first;

  for (src = macro->exp.tokens; src < limit; src++)
    {
      unsigned int count;
      const cpp_token **from, **paste_flag;

      if (src->type != CPP_MACRO_ARG)
	{
	  *dest++ = src;
	  continue;
	}

      paste_flag = 0;
      arg = &args[src->val.arg_no - 1];
      if (src->flags & STRINGIFY_ARG)
	count = 1, from = &arg->stringified;
      else if (src->flags & PASTE_LEFT)
	count = arg->count, from = arg->first;
      else if (src != macro->exp.tokens && (src[-1].flags & PASTE_LEFT))
	{
	  count = arg->count, from = arg->first;
	  if (dest != first)
	    {
	      if (dest[-1]->type == CPP_COMMA
		  && macro->variadic
		  && src->val.arg_no == macro->paramc)
		{
		  /* Swallow a pasted comma if from == NULL, otherwise
		     drop the paste flag.  */
		  if (from == NULL)
		    dest--;
		  else
		    paste_flag = dest - 1;
		}
	      /* Remove the paste flag if the RHS is a placemarker.  */
	      else if (count == 0)
		paste_flag = dest - 1;
	    }
	}
      else
	count = arg->expanded_count, from = arg->expanded;

      /* Padding on the left of an argument (unless RHS of ##).  */
      if ((!pfile->state.in_directive || pfile->state.directive_wants_padding)
	  && src != macro->exp.tokens && !(src[-1].flags & PASTE_LEFT))
	*dest++ = padding_token (pfile, src);

      if (count)
	{
	  memcpy (dest, from, count * sizeof (cpp_token *));
	  dest += count;

	  /* With a non-empty argument on the LHS of ##, the last
	     token should be flagged PASTE_LEFT.  */
	  if (src->flags & PASTE_LEFT)
	    paste_flag = dest - 1;
	}

      /* Avoid paste on RHS (even case count == 0).  */
      if (!pfile->state.in_directive && !(src->flags & PASTE_LEFT))
	*dest++ = &pfile->avoid_paste;

      /* Add a new paste flag, or remove an unwanted one.  */
      if (paste_flag)
	{
	  cpp_token *token = _cpp_temp_token (pfile);
	  token->type = (*paste_flag)->type;
	  token->val.str = (*paste_flag)->val.str;
	  if (src->flags & PASTE_LEFT)
	    token->flags = (*paste_flag)->flags | PASTE_LEFT;
	  else
	    token->flags = (*paste_flag)->flags & ~PASTE_LEFT;
	  *paste_flag = token;
	}
    }

  /* Free the expanded arguments.  */
  for (i = 0; i < macro->paramc; i++)
    if (args[i].expanded)
      free (args[i].expanded);

  push_ptoken_context (pfile, node, buff, first, dest - first);
}

/* Return a special padding token, with padding inherited from SOURCE.  */
static const cpp_token *
padding_token (cpp_reader *pfile, const cpp_token *source)
{
  cpp_token *result = _cpp_temp_token (pfile);

  result->type = CPP_PADDING;
  result->val.source = source;
  result->flags = 0;
  return result;
}

/* Get a new uninitialized context.  Create a new one if we cannot
   re-use an old one.  */
static cpp_context *
next_context (cpp_reader *pfile)
{
  cpp_context *result = pfile->context->next;

  if (result == 0)
    {
      result = xnew (cpp_context);
      result->prev = pfile->context;
      result->next = 0;
      pfile->context->next = result;
    }

  pfile->context = result;
  return result;
}

/* Push a list of pointers to tokens.  */
static void
push_ptoken_context (cpp_reader *pfile, cpp_hashnode *macro, _cpp_buff *buff,
		     const cpp_token **first, unsigned int count)
{
  cpp_context *context = next_context (pfile);

  context->direct_p = false;
  context->macro = macro;
  context->buff = buff;
  FIRST (context).ptoken = first;
  LAST (context).ptoken = first + count;
}

/* Push a list of tokens.  */
static void
push_token_context (cpp_reader *pfile, cpp_hashnode *macro,
		    const cpp_token *first, unsigned int count)
{
  cpp_context *context = next_context (pfile);

  context->direct_p = true;
  context->macro = macro;
  context->buff = NULL;
  FIRST (context).token = first;
  LAST (context).token = first + count;
}

/* Push a traditional macro's replacement text.  */
void
_cpp_push_text_context (cpp_reader *pfile, cpp_hashnode *macro,
			const uchar *start, size_t len)
{
  cpp_context *context = next_context (pfile);

  context->direct_p = true;
  context->macro = macro;
  context->buff = NULL;
  CUR (context) = start;
  RLIMIT (context) = start + len;
  macro->flags |= NODE_DISABLED;
}

/* Expand an argument ARG before replacing parameters in a
   function-like macro.  This works by pushing a context with the
   argument's tokens, and then expanding that into a temporary buffer
   as if it were a normal part of the token stream.  collect_args()
   has terminated the argument's tokens with a CPP_EOF so that we know
   when we have fully expanded the argument.  */
static void
expand_arg (cpp_reader *pfile, macro_arg *arg)
{
  unsigned int capacity;
  bool saved_warn_trad;

  if (arg->count == 0)
    return;

  /* Don't warn about funlike macros when pre-expanding.  */
  saved_warn_trad = CPP_WTRADITIONAL (pfile);
  CPP_WTRADITIONAL (pfile) = 0;

  /* Loop, reading in the arguments.  */
  capacity = 256;
  arg->expanded = xmalloc (capacity * sizeof (cpp_token *));

  push_ptoken_context (pfile, NULL, NULL, arg->first, arg->count + 1);
  for (;;)
    {
      const cpp_token *token;

      if (arg->expanded_count + 1 >= capacity)
	{
	  capacity *= 2;
	  arg->expanded = xrealloc (arg->expanded,
				    capacity * sizeof (cpp_token *));
	}

      token = cpp_get_token (pfile);

      if (token->type == CPP_EOF)
	break;

      arg->expanded[arg->expanded_count++] = token;
    }

  _cpp_pop_context (pfile);

  CPP_WTRADITIONAL (pfile) = saved_warn_trad;
}

/* Pop the current context off the stack, re-enabling the macro if the
   context represented a macro's replacement list.  The context
   structure is not freed so that we can re-use it later.  */
void
_cpp_pop_context (cpp_reader *pfile)
{
  cpp_context *context = pfile->context;

  if (context->macro)
    context->macro->flags &= ~NODE_DISABLED;

  if (context->buff)
    _cpp_release_buff (pfile, context->buff);

  pfile->context = context->prev;
}

/* External routine to get a token.  Also used nearly everywhere
   internally, except for places where we know we can safely call
   _cpp_lex_token directly, such as lexing a directive name.

   Macro expansions and directives are transparently handled,
   including entering included files.  Thus tokens are post-macro
   expansion, and after any intervening directives.  External callers
   see CPP_EOF only at EOF.  Internal callers also see it when meeting
   a directive inside a macro call, when at the end of a directive and
   state.in_directive is still 1, and at the end of argument
   pre-expansion.  */
const cpp_token *
cpp_get_token (cpp_reader *pfile)
{
  const cpp_token *result;

  for (;;)
    {
      cpp_hashnode *node;
      cpp_context *context = pfile->context;

      /* Context->prev == 0 <=> base context.  */
      if (!context->prev)
	result = _cpp_lex_token (pfile);
      else if (FIRST (context).token != LAST (context).token)
	{
	  if (context->direct_p)
	    result = FIRST (context).token++;
	  else
	    result = *FIRST (context).ptoken++;

	  if (result->flags & PASTE_LEFT)
	    {
	      paste_all_tokens (pfile, result);
	      if (pfile->state.in_directive)
		continue;
	      return padding_token (pfile, result);
	    }
	}
      else
	{
	  _cpp_pop_context (pfile);
	  if (pfile->state.in_directive)
	    continue;
	  return &pfile->avoid_paste;
	}

      if (pfile->state.in_directive && result->type == CPP_COMMENT)
	continue;

      if (result->type != CPP_NAME)
	break;

      node = result->val.node;

      if (node->type != NT_MACRO || (result->flags & NO_EXPAND))
	break;

      if (!(node->flags & NODE_DISABLED))
	{
	  if (!pfile->state.prevent_expansion
	      && enter_macro_context (pfile, node))
	    {
	      if (pfile->state.in_directive)
		continue;
	      return padding_token (pfile, result);
	    }
	}
      else
	{
	  /* Flag this token as always unexpandable.  FIXME: move this
	     to collect_args()?.  */
	  cpp_token *t = _cpp_temp_token (pfile);
	  t->type = result->type;
	  t->flags = result->flags | NO_EXPAND;
	  t->val.str = result->val.str;
	  result = t;
	}

      break;
    }

  return result;
}

/* Returns true if we're expanding an object-like macro that was
   defined in a system header.  Just checks the macro at the top of
   the stack.  Used for diagnostic suppression.  */
int
cpp_sys_macro_p (cpp_reader *pfile)
{
  cpp_hashnode *node = pfile->context->macro;

  return node && node->value.macro && node->value.macro->syshdr;
}

/* Read each token in, until end of the current file.  Directives are
   transparently processed.  */
void
cpp_scan_nooutput (cpp_reader *pfile)
{
  /* Request a CPP_EOF token at the end of this file, rather than
     transparently continuing with the including file.  */
  pfile->buffer->return_at_eof = true;

  if (CPP_OPTION (pfile, traditional))
    while (_cpp_read_logical_line_trad (pfile))
      ;
  else
    while (cpp_get_token (pfile)->type != CPP_EOF)
      ;
}

/* Step back one (or more) tokens.  Can only step mack more than 1 if
   they are from the lexer, and not from macro expansion.  */
void
_cpp_backup_tokens (cpp_reader *pfile, unsigned int count)
{
  if (pfile->context->prev == NULL)
    {
      pfile->lookaheads += count;
      while (count--)
	{
	  pfile->cur_token--;
	  if (pfile->cur_token == pfile->cur_run->base
	      /* Possible with -fpreprocessed and no leading #line.  */
	      && pfile->cur_run->prev != NULL)
	    {
	      pfile->cur_run = pfile->cur_run->prev;
	      pfile->cur_token = pfile->cur_run->limit;
	    }
	}
    }
  else
    {
      if (count != 1)
	abort ();
      if (pfile->context->direct_p)
	FIRST (pfile->context).token--;
      else
	FIRST (pfile->context).ptoken--;
    }
}

/* #define directive parsing and handling.  */

/* Returns nonzero if a macro redefinition warning is required.  */
static bool
warn_of_redefinition (cpp_reader *pfile, const cpp_hashnode *node,
		      const cpp_macro *macro2)
{
  const cpp_macro *macro1;
  unsigned int i;

  /* Some redefinitions need to be warned about regardless.  */
  if (node->flags & NODE_WARN)
    return true;

  /* Redefinition of a macro is allowed if and only if the old and new
     definitions are the same.  (6.10.3 paragraph 2).  */
  macro1 = node->value.macro;

  /* Don't check count here as it can be different in valid
     traditional redefinitions with just whitespace differences.  */
  if (macro1->paramc != macro2->paramc
      || macro1->fun_like != macro2->fun_like
      || macro1->variadic != macro2->variadic)
    return true;

  /* Check parameter spellings.  */
  for (i = 0; i < macro1->paramc; i++)
    if (macro1->params[i] != macro2->params[i])
      return true;

  /* Check the replacement text or tokens.  */
  if (CPP_OPTION (pfile, traditional))
    return _cpp_expansions_different_trad (macro1, macro2);

  if (macro1->count != macro2->count)
    return true;

  for (i = 0; i < macro1->count; i++)
    if (!_cpp_equiv_tokens (&macro1->exp.tokens[i], &macro2->exp.tokens[i]))
      return true;

  return false;
}

/* Free the definition of hashnode H.  */
void
_cpp_free_definition (cpp_hashnode *h)
{
  /* Macros and assertions no longer have anything to free.  */
  h->type = NT_VOID;
  /* Clear builtin flag in case of redefinition.  */
  h->flags &= ~(NODE_BUILTIN | NODE_DISABLED);
}

/* Save parameter NODE to the parameter list of macro MACRO.  Returns
   zero on success, nonzero if the parameter is a duplicate.  */
bool
_cpp_save_parameter (cpp_reader *pfile, cpp_macro *macro, cpp_hashnode *node)
{
  unsigned int len;
  /* Constraint 6.10.3.6 - duplicate parameter names.  */
  if (node->flags & NODE_MACRO_ARG)
    {
      cpp_error (pfile, CPP_DL_ERROR, "duplicate macro parameter \"%s\"",
		 NODE_NAME (node));
      return true;
    }

  if (BUFF_ROOM (pfile->a_buff)
      < (macro->paramc + 1) * sizeof (cpp_hashnode *))
    _cpp_extend_buff (pfile, &pfile->a_buff, sizeof (cpp_hashnode *));

  ((cpp_hashnode **) BUFF_FRONT (pfile->a_buff))[macro->paramc++] = node;
  node->flags |= NODE_MACRO_ARG;
  len = macro->paramc * sizeof (union _cpp_hashnode_value);
  if (len > pfile->macro_buffer_len)
    {
      pfile->macro_buffer = xrealloc (pfile->macro_buffer, len);
      pfile->macro_buffer_len = len;
    }
  ((union _cpp_hashnode_value *) pfile->macro_buffer)[macro->paramc - 1]
    = node->value;
  
  node->value.arg_index  = macro->paramc;
  return false;
}

/* Check the syntax of the parameters in a MACRO definition.  Returns
   false if an error occurs.  */
static bool
parse_params (cpp_reader *pfile, cpp_macro *macro)
{
  unsigned int prev_ident = 0;

  for (;;)
    {
      const cpp_token *token = _cpp_lex_token (pfile);

      switch (token->type)
	{
	default:
	  /* Allow/ignore comments in parameter lists if we are
	     preserving comments in macro expansions.  */
	  if (token->type == CPP_COMMENT
	      && ! CPP_OPTION (pfile, discard_comments_in_macro_exp))
	    continue;

	  cpp_error (pfile, CPP_DL_ERROR,
		     "\"%s\" may not appear in macro parameter list",
		     cpp_token_as_text (pfile, token));
	  return false;

	case CPP_NAME:
	  if (prev_ident)
	    {
	      cpp_error (pfile, CPP_DL_ERROR,
			 "macro parameters must be comma-separated");
	      return false;
	    }
	  prev_ident = 1;

	  if (_cpp_save_parameter (pfile, macro, token->val.node))
	    return false;
	  continue;

	case CPP_CLOSE_PAREN:
	  if (prev_ident || macro->paramc == 0)
	    return true;

	  /* Fall through to pick up the error.  */
	case CPP_COMMA:
	  if (!prev_ident)
	    {
	      cpp_error (pfile, CPP_DL_ERROR, "parameter name missing");
	      return false;
	    }
	  prev_ident = 0;
	  continue;

	case CPP_ELLIPSIS:
	  macro->variadic = 1;
	  if (!prev_ident)
	    {
	      _cpp_save_parameter (pfile, macro,
				   pfile->spec_nodes.n__VA_ARGS__);
	      pfile->state.va_args_ok = 1;
	      if (! CPP_OPTION (pfile, c99) && CPP_OPTION (pfile, pedantic))
		cpp_error (pfile, CPP_DL_PEDWARN,
			   "anonymous variadic macros were introduced in C99");
	    }
	  else if (CPP_OPTION (pfile, pedantic))
	    cpp_error (pfile, CPP_DL_PEDWARN,
		       "ISO C does not permit named variadic macros");

	  /* We're at the end, and just expect a closing parenthesis.  */
	  token = _cpp_lex_token (pfile);
	  if (token->type == CPP_CLOSE_PAREN)
	    return true;
	  /* Fall through.  */

	case CPP_EOF:
	  cpp_error (pfile, CPP_DL_ERROR, "missing ')' in macro parameter list");
	  return false;
	}
    }
}

/* Allocate room for a token from a macro's replacement list.  */
static cpp_token *
alloc_expansion_token (cpp_reader *pfile, cpp_macro *macro)
{
  if (BUFF_ROOM (pfile->a_buff) < (macro->count + 1) * sizeof (cpp_token))
    _cpp_extend_buff (pfile, &pfile->a_buff, sizeof (cpp_token));

  return &((cpp_token *) BUFF_FRONT (pfile->a_buff))[macro->count++];
}

/* Lex a token from the expansion of MACRO, but mark parameters as we
   find them and warn of traditional stringification.  */
static cpp_token *
lex_expansion_token (cpp_reader *pfile, cpp_macro *macro)
{
  cpp_token *token;

  pfile->cur_token = alloc_expansion_token (pfile, macro);
  token = _cpp_lex_direct (pfile);

  /* Is this a parameter?  */
  if (token->type == CPP_NAME
      && (token->val.node->flags & NODE_MACRO_ARG) != 0)
    {
      token->type = CPP_MACRO_ARG;
      token->val.arg_no = token->val.node->value.arg_index;
    }
  else if (CPP_WTRADITIONAL (pfile) && macro->paramc > 0
	   && (token->type == CPP_STRING || token->type == CPP_CHAR))
    check_trad_stringification (pfile, macro, &token->val.str);

  return token;
}

static bool
create_iso_definition (cpp_reader *pfile, cpp_macro *macro)
{
  cpp_token *token;
  const cpp_token *ctoken;

  /* Get the first token of the expansion (or the '(' of a
     function-like macro).  */
  ctoken = _cpp_lex_token (pfile);

  if (ctoken->type == CPP_OPEN_PAREN && !(ctoken->flags & PREV_WHITE))
    {
      bool ok = parse_params (pfile, macro);
      macro->params = (cpp_hashnode **) BUFF_FRONT (pfile->a_buff);
      if (!ok)
	return false;

      /* Success.  Commit the parameter array.  */
      BUFF_FRONT (pfile->a_buff) = (uchar *) &macro->params[macro->paramc];
      macro->fun_like = 1;
    }
  else if (ctoken->type != CPP_EOF && !(ctoken->flags & PREV_WHITE))
    cpp_error (pfile, CPP_DL_PEDWARN,
	       "ISO C requires whitespace after the macro name");

  if (macro->fun_like)
    token = lex_expansion_token (pfile, macro);
  else
    {
      token = alloc_expansion_token (pfile, macro);
      *token = *ctoken;
    }

  for (;;)
    {
      /* Check the stringifying # constraint 6.10.3.2.1 of
	 function-like macros when lexing the subsequent token.  */
      if (macro->count > 1 && token[-1].type == CPP_HASH && macro->fun_like)
	{
	  if (token->type == CPP_MACRO_ARG)
	    {
	      token->flags &= ~PREV_WHITE;
	      token->flags |= STRINGIFY_ARG;
	      token->flags |= token[-1].flags & PREV_WHITE;
	      token[-1] = token[0];
	      macro->count--;
	    }
	  /* Let assembler get away with murder.  */
	  else if (CPP_OPTION (pfile, lang) != CLK_ASM)
	    {
	      cpp_error (pfile, CPP_DL_ERROR,
			 "'#' is not followed by a macro parameter");
	      return false;
	    }
	}

      if (token->type == CPP_EOF)
	break;

      /* Paste operator constraint 6.10.3.3.1.  */
      if (token->type == CPP_PASTE)
	{
	  /* Token-paste ##, can appear in both object-like and
	     function-like macros, but not at the ends.  */
	  if (--macro->count > 0)
	    token = lex_expansion_token (pfile, macro);

	  if (macro->count == 0 || token->type == CPP_EOF)
	    {
	      cpp_error (pfile, CPP_DL_ERROR,
		 "'##' cannot appear at either end of a macro expansion");
	      return false;
	    }

	  token[-1].flags |= PASTE_LEFT;
	}

      token = lex_expansion_token (pfile, macro);
    }

  macro->exp.tokens = (cpp_token *) BUFF_FRONT (pfile->a_buff);

  /* Don't count the CPP_EOF.  */
  macro->count--;

  /* Clear whitespace on first token for warn_of_redefinition().  */
  if (macro->count)
    macro->exp.tokens[0].flags &= ~PREV_WHITE;

  /* Commit the memory.  */
  BUFF_FRONT (pfile->a_buff) = (uchar *) &macro->exp.tokens[macro->count];

  return true;
}

/* Parse a macro and save its expansion.  Returns nonzero on success.  */
bool
_cpp_create_definition (cpp_reader *pfile, cpp_hashnode *node)
{
  cpp_macro *macro;
  unsigned int i;
  bool ok;

  macro = (cpp_macro *) _cpp_aligned_alloc (pfile, sizeof (cpp_macro));
  macro->line = pfile->directive_line;
  macro->params = 0;
  macro->paramc = 0;
  macro->variadic = 0;
  macro->used = !CPP_OPTION (pfile, warn_unused_macros);
  macro->count = 0;
  macro->fun_like = 0;
  /* To suppress some diagnostics.  */
  macro->syshdr = pfile->map->sysp != 0;

  if (CPP_OPTION (pfile, traditional))
    ok = _cpp_create_trad_definition (pfile, macro);
  else
    {
      cpp_token *saved_cur_token = pfile->cur_token;

      ok = create_iso_definition (pfile, macro);

      /* Restore lexer position because of games lex_expansion_token()
	 plays lexing the macro.  We set the type for SEEN_EOL() in
	 cpplib.c.

	 Longer term we should lex the whole line before coming here,
	 and just copy the expansion.  */
      saved_cur_token[-1].type = pfile->cur_token[-1].type;
      pfile->cur_token = saved_cur_token;

      /* Stop the lexer accepting __VA_ARGS__.  */
      pfile->state.va_args_ok = 0;
    }

  /* Clear the fast argument lookup indices.  */
  for (i = macro->paramc; i-- > 0; )
    {
      struct cpp_hashnode *node = macro->params[i];
      node->flags &= ~ NODE_MACRO_ARG;
      node->value = ((union _cpp_hashnode_value *) pfile->macro_buffer)[i];
    }

  if (!ok)
    return ok;

  if (node->type == NT_MACRO)
    {
      if (CPP_OPTION (pfile, warn_unused_macros))
	_cpp_warn_if_unused_macro (pfile, node, NULL);

      if (warn_of_redefinition (pfile, node, macro))
	{
	  cpp_error_with_line (pfile, CPP_DL_PEDWARN, pfile->directive_line, 0,
			       "\"%s\" redefined", NODE_NAME (node));

	  if (node->type == NT_MACRO && !(node->flags & NODE_BUILTIN))
	    cpp_error_with_line (pfile, CPP_DL_PEDWARN,
				 node->value.macro->line, 0,
			 "this is the location of the previous definition");
	}
    }

  if (node->type != NT_VOID)
    _cpp_free_definition (node);

  /* Enter definition in hash table.  */
  node->type = NT_MACRO;
  node->value.macro = macro;
  if (! ustrncmp (NODE_NAME (node), DSC ("__STDC_")))
    node->flags |= NODE_WARN;

  return ok;
}

/* Warn if a token in STRING matches one of a function-like MACRO's
   parameters.  */
static void
check_trad_stringification (cpp_reader *pfile, const cpp_macro *macro,
			    const cpp_string *string)
{
  unsigned int i, len;
  const uchar *p, *q, *limit;

  /* Loop over the string.  */
  limit = string->text + string->len - 1;
  for (p = string->text + 1; p < limit; p = q)
    {
      /* Find the start of an identifier.  */
      while (p < limit && !is_idstart (*p))
	p++;

      /* Find the end of the identifier.  */
      q = p;
      while (q < limit && is_idchar (*q))
	q++;

      len = q - p;

      /* Loop over the function macro arguments to see if the
	 identifier inside the string matches one of them.  */
      for (i = 0; i < macro->paramc; i++)
	{
	  const cpp_hashnode *node = macro->params[i];

	  if (NODE_LEN (node) == len
	      && !memcmp (p, NODE_NAME (node), len))
	    {
	      cpp_error (pfile, CPP_DL_WARNING,
	   "macro argument \"%s\" would be stringified in traditional C",
			 NODE_NAME (node));
	      break;
	    }
	}
    }
}

/* Returns the name, arguments and expansion of a macro, in a format
   suitable to be read back in again, and therefore also for DWARF 2
   debugging info.  e.g. "PASTE(X, Y) X ## Y", or "MACNAME EXPANSION".
   Caller is expected to generate the "#define" bit if needed.  The
   returned text is temporary, and automatically freed later.  */
const unsigned char *
cpp_macro_definition (cpp_reader *pfile, const cpp_hashnode *node)
{
  unsigned int i, len;
  const cpp_macro *macro = node->value.macro;
  unsigned char *buffer;

  if (node->type != NT_MACRO || (node->flags & NODE_BUILTIN))
    {
      cpp_error (pfile, CPP_DL_ICE,
		 "invalid hash type %d in cpp_macro_definition", node->type);
      return 0;
    }

  /* Calculate length.  */
  len = NODE_LEN (node) + 2;			/* ' ' and NUL.  */
  if (macro->fun_like)
    {
      len += 4;		/* "()" plus possible final ".." of named
			   varargs (we have + 1 below).  */
      for (i = 0; i < macro->paramc; i++)
	len += NODE_LEN (macro->params[i]) + 1; /* "," */
    }

  /* This should match below where we fill in the buffer.  */
  if (CPP_OPTION (pfile, traditional))
    len += _cpp_replacement_text_len (macro);
  else
    {
      for (i = 0; i < macro->count; i++)
	{
	  cpp_token *token = &macro->exp.tokens[i];

	  if (token->type == CPP_MACRO_ARG)
	    len += NODE_LEN (macro->params[token->val.arg_no - 1]);
	  else
	    len += cpp_token_len (token);

	  if (token->flags & STRINGIFY_ARG)
	    len++;			/* "#" */
	  if (token->flags & PASTE_LEFT)
	    len += 3;		/* " ##" */
	  if (token->flags & PREV_WHITE)
	    len++;              /* " " */
	}
    }

  if (len > pfile->macro_buffer_len)
    {
      pfile->macro_buffer = xrealloc (pfile->macro_buffer, len);
      pfile->macro_buffer_len = len;
    }

  /* Fill in the buffer.  Start with the macro name.  */
  buffer = pfile->macro_buffer;
  memcpy (buffer, NODE_NAME (node), NODE_LEN (node));
  buffer += NODE_LEN (node);

  /* Parameter names.  */
  if (macro->fun_like)
    {
      *buffer++ = '(';
      for (i = 0; i < macro->paramc; i++)
	{
	  cpp_hashnode *param = macro->params[i];

	  if (param != pfile->spec_nodes.n__VA_ARGS__)
	    {
	      memcpy (buffer, NODE_NAME (param), NODE_LEN (param));
	      buffer += NODE_LEN (param);
	    }

	  if (i + 1 < macro->paramc)
	    /* Don't emit a space after the comma here; we're trying
	       to emit a Dwarf-friendly definition, and the Dwarf spec
	       forbids spaces in the argument list.  */
	    *buffer++ = ',';
	  else if (macro->variadic)
	    *buffer++ = '.', *buffer++ = '.', *buffer++ = '.';
	}
      *buffer++ = ')';
    }

  /* The Dwarf spec requires a space after the macro name, even if the
     definition is the empty string.  */
  *buffer++ = ' ';

  if (CPP_OPTION (pfile, traditional))
    buffer = _cpp_copy_replacement_text (macro, buffer);
  else if (macro->count)
  /* Expansion tokens.  */
    {
      for (i = 0; i < macro->count; i++)
	{
	  cpp_token *token = &macro->exp.tokens[i];

	  if (token->flags & PREV_WHITE)
	    *buffer++ = ' ';
	  if (token->flags & STRINGIFY_ARG)
	    *buffer++ = '#';

	  if (token->type == CPP_MACRO_ARG)
	    {
	      memcpy (buffer,
		      NODE_NAME (macro->params[token->val.arg_no - 1]),
		      NODE_LEN (macro->params[token->val.arg_no - 1]));
	      buffer += NODE_LEN (macro->params[token->val.arg_no - 1]);
	    }
	  else
	    buffer = cpp_spell_token (pfile, token, buffer);

	  if (token->flags & PASTE_LEFT)
	    {
	      *buffer++ = ' ';
	      *buffer++ = '#';
	      *buffer++ = '#';
	      /* Next has PREV_WHITE; see _cpp_create_definition.  */
	    }
	}
    }

  *buffer = '\0';
  return pfile->macro_buffer;
}
