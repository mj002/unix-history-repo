#ifndef lint
static char sccsid[] = "@(#)1.tables.c	4.1	(Berkeley)	2/11/83";
#endif not lint

#include <stdio.h>

int match[146]
			= {
			   0,   1,   2,   3,   4,   5,  19,  21,
			  23,  25,  29,  32,  36,  38,  42,  44,
			  46,  50,  52,  56,  59,  61,  65,  74,
			  77,  81,  83,  85,  87,  89,  91,  93,
			  95,  97,  99, 102, 105, 108, 114, 116,
			 118, 120, 122, 124, 126, 129, 131, 134,
			 136, 139, 142, 144, 147, 149, 151, 153,
			 155, 157, 159, 161, 163, 165, 167, 169,
			 171, 174, 176, 178, 180, 182, 184, 186,
			 188, 190, 192, 194, 196, 198, 200, 202,
			 204, 206, 208, 210, 212, 214, 216, 218,
			 221, 223, 225, 227, 229, 231, 233, 235,
			 237, 239, 241, 243, 245, 247, 249, 251,
			 254, 256, 258, 260, 262, 264, 266, 268,
			 270, 272, 274, 276, 278, 280, 283, 287,
			 292, 298, 303, 307, 311, 316, 320, 324,
			 327, 329, 331, 333, 335, 337, 339, 341,
			 343, 345, 347, 349, 351, 353, 355, 356,
			 357, 359
			};

int symclass[358]
			= {
			   1,   1,   1,   1,   1,   0,   0,   0,
			   0,   0,   0,   0,   0,   0,   0,   0,
			   0,   0,   1,   0,   1,   0,   1,   0,
			   1,   2,   3,   0,   1,   2,   0,   1,
			   4,   0,   0,   1,   0,   1,   2,   0,
			   0,   1,   0,   1,   2,   1,   2,   0,
			   0,   1,   0,   1,   4,   5,   0,   1,
			   0,   0,   1,   0,   1,   0,   0,   0,
			   1,   0,   0,   0,   0,   0,   0,   0,
			   2,   1,   2,   0,   1,   2,   0,   0,
			   1,   0,   1,   0,   1,   0,   1,   0,
			   1,   0,   1,   0,   1,   0,   1,   0,
			   1,   0,   1,   2,   0,   1,   2,   3,
			   1,   4,   0,   1,   4,   0,   0,   0,
			   5,   1,   0,   1,   0,   1,   0,   1,
			   0,   1,   0,   1,   2,   1,   2,   0,
			   1,   0,   1,   4,   0,   1,   0,   1,
			   0,   0,   1,   0,   0,   1,   0,   1,
			   0,   0,   1,   0,   1,   0,   1,   0,
			   1,   0,   1,   0,   1,   0,   1,   0,
			   1,   0,   1,   0,   1,   0,   1,   0,
			   1,   0,   1,   0,   0,   1,   0,   1,
			   0,   1,   0,   1,   0,   1,   0,   1,
			   0,   1,   0,   1,   0,   1,   0,   1,
			   0,   1,   0,   1,   0,   1,   0,   1,
			   0,   1,   0,   1,   0,   1,   0,   1,
			   0,   1,   0,   1,   0,   1,   0,   1,
			   0,   1,   0,   0,   1,   0,   1,   0,
			   1,   0,   1,   0,   1,   0,   1,   0,
			   1,   0,   1,   0,   1,   0,   1,   0,
			   1,   0,   1,   0,   1,   0,   1,   0,
			   1,   0,   1,   0,   0,   1,   0,   1,
			   0,   1,   0,   1,   0,   1,   0,   1,
			   0,   1,   0,   1,   0,   1,   0,   1,
			   0,   1,   0,   1,   0,   1,   0,   1,
			   0,   2,   1,   2,   0,   0,   1,   0,
			   0,   0,   0,   1,   2,   0,   0,   0,
			   0,   1,   0,   0,   0,   0,   1,   0,
			   0,   0,   1,   0,   0,   0,   1,   2,
			   0,   0,   0,   1,   0,   0,   0,   1,
			   0,   0,   0,   1,   0,   0,   1,   0,
			   1,   0,   1,   0,   1,   0,   1,   0,
			   1,   0,   1,   0,   1,   0,   1,   0,
			   1,   0,   1,   0,   1,   0,   1,   0,
			   1,   0,   1,   0,   0,   0
			};

char symbol[358]
			= {
			    '_',    '_',    '_',    '_',    '_',    'i',    'd',    'g',
			    'a',    'r',    'w',    'c',    'l',    's',    'e',    'p',
			    'f',    'b',    '_',    'o',    '_',    't',    '_',    'o',
			    '_',    '_',    '_',    '(',    '_',    '_',    '\0',    '_',
			    '_',    ',',    '\0',    '_',    '(',    '_',    '_',    ',',
			    ')',    '_',    '\0',    '_',    '_',    '_',    '_',    ',',
			    ')',    '_',    ',',    '_',    '_',    '_',    '\0',    '_',
			    'f',    'n',    '_',    '(',    '_',    '(',    ')',    '\0',
			    '_',    '=',    'g',    'a',    'r',    'p',    'w',    's',
			    '_',    '_',    '_',    ',',    '_',    '_',    ',',    '\0',
			    '_',    'o',    '_',    'n',    '_',    't',    '_',    'i',
			    '_',    'n',    '_',    'u',    '_',    'e',    '_',    '\0',
			    '_',    'o',    '_',    '_',    'u',    '_',    '_',    '_',
			    '_',    '_',    '=',    '_',    '_',    '(',    ')',    ',',
			    '_',    '_',    's',    '_',    's',    '_',    'i',    '_',
			    'g',    '_',    'n',    '_',    '_',    '_',    '_',    't',
			    '_',    'o',    '_',    '_',    '\0',    '_',    'e',    '_',
			    'a',    't',    '_',    'l',    'd',    '_',    'f',    '_',
			    'u',    'o',    '_',    'n',    '_',    'c',    '_',    't',
			    '_',    'i',    '_',    'o',    '_',    'n',    '_',    't',
			    '_',    'e',    '_',    'g',    '_',    'e',    '_',    'r',
			    '_',    'o',    '_',    'm',    'n',    '_',    'p',    '_',
			    'l',    '_',    'e',    '_',    'x',    '_',    'b',    '_',
			    'l',    '_',    'e',    '_',    'p',    '_',    'r',    '_',
			    'e',    '_',    'c',    '_',    'i',    '_',    's',    '_',
			    'i',    '_',    'o',    '_',    'n',    '_',    'o',    '_',
			    'g',    '_',    'i',    '_',    'c',    '_',    'a',    '_',
			    'l',    '_',    't',    'u',    '_',    'o',    '_',    'p',
			    '_',    'b',    '_',    'r',    '_',    'o',    '_',    't',
			    '_',    'i',    '_',    'n',    '_',    'e',    '_',    'e',
			    '_',    't',    '_',    'u',    '_',    'r',    '_',    'n',
			    '_',    'n',    '_',    'd',    't',    '_',    '\0',    '_',
			    'r',    '_',    'y',    '_',    'r',    '_',    'm',    '_',
			    'a',    '_',    't',    '_',    '(',    '_',    'r',    '_',
			    'i',    '_',    't',    '_',    'e',    '_',    '(',    '_',
			    '(',    '_',    '_',    '_',    ',',    '\0',    '_',    '(',
			    ')',    ',',    '\0',    '_',    '_',    'e',    ',',    '\0',
			    ')',    '_',    'n',    'r',    ')',    '\0',    '_',    'd',
			    ')',    '\0',    '_',    '=',    ')',    '\0',    '_',    '_',
			    ',',    ')',    '\0',    '_',    'r',    ')',    '\0',    '_',
			    '=',    ')',    '\0',    '_',    'r',    'u',    '_',    'i',
			    '_',    'n',    '_',    't',    '_',    'n',    '_',    'c',
			    '_',    'h',    '_',    'l',    '_',    'o',    '_',    'c',
			    '_',    'k',    '_',    'd',    '_',    'a',    '_',    't',
			    '_',    'a',    '_',    '_',    '_',    '_'
			};

int action[358]
			= {
			      1,      1,      1,      1,      1,      3,      3,      3,
			      3,      3,      3,      3,      3,      3,      3,      3,
			      3,      3,    111,      0,    111,      0,    111,     76,
			    111,      1,      0,      0,    111,      1,    122,    111,
			      0,     72,    123,    111,      0,    111,      1,      3,
			      3,    111,    124,    111,      1,    111,      1,      3,
			      3,    111,     76,    111,      0,      0,    125,    111,
			      0,      0,    111,     45,    111,     30,     31,    111,
			      0,    133,      0,      0,      0,      0,      0,      0,
			      1,    111,      1,      3,    111,      1,      3,    141,
			    111,      0,    111,      0,    111,      0,    111,      0,
			    111,      0,    111,      0,    111,      0,    111,    180,
			    111,      0,    111,      1,      0,    111,      1,     61,
			    111,      0,      0,    111,      0,     62,     63,     64,
			      0,    111,      0,    111,      0,    111,      0,    111,
			      0,    111,      0,    111,      1,    111,      1,      3,
			    111,     76,    111,      0,    150,    111,      0,    111,
			      0,      0,    111,      0,     76,    111,      0,    111,
			      0,      0,    111,      0,    111,      0,    111,      0,
			    111,      0,    111,      0,    111,    200,    111,      0,
			    111,      0,    111,      0,    111,      0,    111,      0,
			    111,      0,    111,      0,      0,    111,      0,    111,
			      0,    111,      0,    111,      0,    111,      0,    111,
			      0,    111,      0,    111,      0,    111,      0,    111,
			      0,    111,      0,    111,      0,    111,      0,    111,
			      0,    111,      0,    111,      0,    111,      0,    111,
			      0,    111,      0,    111,      0,    111,      0,    111,
			      0,    111,      0,      0,    111,      0,    111,    350,
			    111,      0,    111,      0,    111,      0,    111,      0,
			    111,      0,    111,      0,    111,    200,    111,      0,
			    111,      0,    111,      0,    111,      0,    111,    300,
			    111,      0,    111,      0,      0,    111,    400,    111,
			      0,    111,    700,    111,      0,    111,      0,    111,
			      0,    111,      0,    111,    600,    111,      0,    111,
			      0,    111,      0,    111,      0,    111,      0,    111,
			      0,      1,    111,      1,    520,    520,    111,     62,
			     77,     70,    111,      0,      1,      0,     75,    111,
			    520,      0,      0,      0,    510,    111,      0,      0,
			    510,    111,      0,     73,    510,    111,      0,      1,
			      3,    510,    111,      0,      0,    510,    111,      0,
			     74,    510,    111,      0,      0,      0,    111,      0,
			    111,      0,    111,     76,    111,      0,    111,      0,
			    111,     76,    111,      0,    111,      0,    111,      0,
			    111,      0,    111,      0,    111,      0,    111,      0,
			    111,    210,    111,      0,      0,      0
			};

int newstate[358]
			= {
			   1,   2,   3,   4,   5,  19,  33,   6,
			  38,  47, 112,  63,  81,  87, 102, 127,
			  51, 134, 142,   7, 142,   8,  -5,   9,
			  -5,  10,  11,  15,  -5,  10, 142,  -5,
			  11,  12, 142,  -5,  13,  -5,  13,  13,
			  14,  -5, 142,  -5,  16, 142,  16,  15,
			  17, 142,  18, 142,  18,  18, 142,  -5,
			  20,  58, 142,  21,  -5,  21,  -5,  -5,
			  21, 142,   6,  38,  47, 127, 112,  87,
			  23, 142,  23,  24, 142,  24,  24, 142,
			  -5,  26,  -5,  27,  -5,  28,  -5,  29,
			  -5,  30,  -5,  31,  -5,  32,  -5, 142,
			 142,  34,  -5,  35,  69,  -5,  35,  36,
			  -5,  36,  37,  -5,  37,  37,  37,  37,
			  37,  -5,  39,  -5,  40,  -5,  41,  -5,
			  42,  -5,  43,  -5,  44, 142,  44,  45,
			 142,  46,  -5,  46, 142,  -5,  48, 142,
			  49,  99, 142,  50, 117, 142,  51, 142,
			  52, 107, 142,  53, 142,  54, 142,  55,
			 142,  56, 142,  57, 142, 142, 142,  59,
			 142,  60, 142,  61, 142,  62, 142,  50,
			 142,  64, 142,  65,  27, 142,  66, 142,
			  67, 142,  68, 142,  50, 142,  70, 142,
			  71, 142,  72, 142,  73, 142,  74, 142,
			  75, 142,  76, 142,  77, 142,  78, 142,
			  79, 142,  80, 142,  50, 142,  82, 142,
			  83, 142,  84, 142,  85, 142,  86, 142,
			  50, 142,  88,  90, 142,  89, 142, 142,
			 142,  91, 142,  92, 142,  93, 142,  94,
			 142,  95, 142,  96, 142, 142, 142,  98,
			 142,  99, 142, 100, 142, 101, 142, 142,
			 142, 103, 142, 104, 105, 142, 142, 142,
			 106, 142, 142, 142, 108,  -5, 109,  -5,
			 110,  -5, 111,  -5,  -5,  -5, 113,  -5,
			 114,  -5, 115,  -5, 116,  -5, 119,  -5,
			 119, 118,  -5, 118,  -5,  -5,  -5, 119,
			 119,  -5,  -5, 119, 120, 121, 120,  -5,
			  -5, 119, 122, 125,  -5,  -5, 119, 123,
			  -5,  -5, 119, 124,  -5,  -5, 119, 124,
			 120,  -5,  -5, 119, 126,  -5,  -5, 119,
			 124,  -5,  -5, 119, 128, 131, 142, 129,
			 142, 130, 142, 117, 142, 132, 142, 133,
			 142, 117, 142, 135,  -5, 136,  -5, 137,
			  -5, 138,  -5, 139,  -5, 140,  -5, 141,
			  -5,  -5,  -5,  -5,  -5,  -5
			};
