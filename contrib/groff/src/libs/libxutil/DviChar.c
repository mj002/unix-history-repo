/*
 * DviChar.c
 *
 * Map DVI (ditroff output) character names to
 * font indexes and back
 */

#include <stdlib.h>
#include <string.h>
#include "DviChar.h"

extern char *xmalloc(int);

#define allocHash() ((DviCharNameHash *) xmalloc (sizeof (DviCharNameHash)))

struct map_list {
	struct map_list	*next;
	DviCharNameMap	*map;
};

static struct map_list	*world;

static int	standard_maps_loaded = 0;
static void	load_standard_maps (void);
static int	hash_name (const char *);
static void	dispose_hash(DviCharNameMap *);
static void	compute_hash(DviCharNameMap *);

DviCharNameMap *
DviFindMap (char *encoding)
{
	struct map_list	*m;

	if (!standard_maps_loaded)
		load_standard_maps ();
	for (m = world; m; m=m->next)
		if (!strcmp (m->map->encoding, encoding))
			return m->map;
	return 0;
}

void
DviRegisterMap (DviCharNameMap *map)
{
	struct map_list	*m;

	if (!standard_maps_loaded)
		load_standard_maps ();
	for (m = world; m; m = m->next)
		if (!strcmp (m->map->encoding, map->encoding))
			break;
	if (!m) {
		m = (struct map_list *) xmalloc (sizeof *m);
		m->next = world;
		world = m;
	}
	dispose_hash (map);
	m->map = map;
	compute_hash (map);
}

static void
dispose_hash (DviCharNameMap *map)
{
	DviCharNameHash	**buckets;
	DviCharNameHash	*h, *next;
	int		i;

	buckets = map->buckets;
	for (i = 0; i < DVI_HASH_SIZE; i++) {
		for (h = buckets[i]; h; h=next) {
			next = h->next;
			free (h);
		}
	}
}

static int
hash_name (const char *name)
{
	int	i = 0;

	while (*name)
		i = (i << 1) ^ *name++;
	if (i < 0)
		i = -i;
	return i;
}

static void
compute_hash (DviCharNameMap *map)
{
	DviCharNameHash	**buckets;
	int		c, s, i;
	DviCharNameHash	*h;

	buckets = map->buckets;
	for (i = 0; i < DVI_HASH_SIZE; i++)
		buckets[i] = 0;
	for (c = 0; c < DVI_MAP_SIZE; c++)
		for (s = 0; s < DVI_MAX_SYNONYMS; s++) {
			if (!map->dvi_names[c][s])
				break;
			i = hash_name (map->dvi_names[c][s]) % DVI_HASH_SIZE;
			h = allocHash ();
			h->next = buckets[i];
			buckets[i] = h;
			h->name = map->dvi_names[c][s];
			h->position = c;
		}
	
}

int
DviCharIndex (DviCharNameMap *map, const char *name)
{
	int		i;
	DviCharNameHash	*h;

	i = hash_name (name) % DVI_HASH_SIZE;
	for (h = map->buckets[i]; h; h=h->next)
		if (!strcmp (h->name, name))
			return h->position;
	return -1;
}

static DviCharNameMap ISO8859_1_map = {
	"iso8859-1",
	0,
{
{	0,			/* 0 */},
{	0,			/* 1 */},
{	0,			/* 2 */},
{	0,			/* 3 */},
{	0,			/* 4 */},
{	0,			/* 5 */},
{	0,			/* 6 */},
{	0,			/* 7 */},
{	0,			/* 8 */},
{	0,			/* 9 */},
{	0,			/* 10 */},
{	0,			/* 11 */},
{	0,			/* 12 */},
{	0,			/* 13 */},
{	0,			/* 14 */},
{	0,			/* 15 */},
{	0,			/* 16 */},
{	0,			/* 17 */},
{	0,			/* 18 */},
{	0,			/* 19 */},
{	0,			/* 20 */},
{	0,			/* 21 */},
{	0,			/* 22 */},
{	0,			/* 23 */},
{	0,			/* 24 */},
{	0,			/* 25 */},
{	0,			/* 26 */},
{	0,			/* 27 */},
{	0,			/* 28 */},
{	0,			/* 29 */},
{	0,			/* 30 */},
{	0,			/* 31 */},
{	0,			/* 32 */},
{	"!",			/* 33 */},
{	"\"", "dq",		/* 34 */},
{	"#", "sh",		/* 35 */},
{	"$", "Do",		/* 36 */},
{	"%",			/* 37 */},
{	"&",			/* 38 */},
{	"'", "cq",		/* 39 */},
{	"(",			/* 40 */},
{	")",			/* 41 */},
{	"*",			/* 42 */},
{	"+",			/* 43 */},
{	",",			/* 44 */},
{	"\\-",			/* 45 */},
{	".",			/* 46 */},
{	"/", "sl",		/* 47 */},
{	"0",			/* 48 */},
{	"1",			/* 49 */},
{	"2",			/* 50 */},
{	"3",			/* 51 */},
{	"4",			/* 52 */},
{	"5",			/* 53 */},
{	"6",			/* 54 */},
{	"7",			/* 55 */},
{	"8",			/* 56 */},
{	"9",			/* 57 */},
{	":",			/* 58 */},
{	";",			/* 59 */},
{	"<",			/* 60 */},
{	"=",			/* 61 */},
{	">",			/* 62 */},
{	"?",			/* 63 */},
{	"@", "at",		/* 64 */},
{	"A",			/* 65 */},
{	"B",			/* 66 */},
{	"C",			/* 67 */},
{	"D",			/* 68 */},
{	"E",			/* 69 */},
{	"F",			/* 70 */},
{	"G",			/* 71 */},
{	"H",			/* 72 */},
{	"I",			/* 73 */},
{	"J",			/* 74 */},
{	"K",			/* 75 */},
{	"L",			/* 76 */},
{	"M",			/* 77 */},
{	"N",			/* 78 */},
{	"O",			/* 79 */},
{	"P",			/* 80 */},
{	"Q",			/* 81 */},
{	"R",			/* 82 */},
{	"S",			/* 83 */},
{	"T",			/* 84 */},
{	"U",			/* 85 */},
{	"V",			/* 86 */},
{	"W",			/* 87 */},
{	"X",			/* 88 */},
{	"Y",			/* 89 */},
{	"Z",			/* 90 */},
{	"[", "lB",		/* 91 */},
{	"\\", "rs",		/* 92 */},
{	"]", "rB",		/* 93 */},
{	"^", "a^", "ha",	/* 94 */},
{	"_",			/* 95 */},
{	"`", "oq",		/* 96 */},
{	"a",			/* 97 */},
{	"b",			/* 98 */},
{	"c",			/* 99 */},
{	"d",			/* 100 */},
{	"e",			/* 101 */},
{	"f",			/* 102 */},
{	"g",			/* 103 */},
{	"h",			/* 104 */},
{	"i",			/* 105 */},
{	"j",			/* 106 */},
{	"k",			/* 107 */},
{	"l",			/* 108 */},
{	"m",			/* 109 */},
{	"n",			/* 110 */},
{	"o",			/* 111 */},
{	"p",			/* 112 */},
{	"q",			/* 113 */},
{	"r",			/* 114 */},
{	"s",			/* 115 */},
{	"t",			/* 116 */},
{	"u",			/* 117 */},
{	"v",			/* 118 */},
{	"w",			/* 119 */},
{	"x",			/* 120 */},
{	"y",			/* 121 */},
{	"z",			/* 122 */},
{	"{", "lC",		/* 123 */},
{	"|", "ba",		/* 124 */},
{	"}", "rC",		/* 125 */},
{	"~", "a~", "ti",	/* 126 */},
{	0,			/* 127 */},
{	0,			/* 128 */},
{	0,			/* 129 */},
{	0,			/* 130 */},
{	0,			/* 131 */},
{	0,			/* 132 */},
{	0,			/* 133 */},
{	0,			/* 134 */},
{	0,			/* 135 */},
{	0,			/* 136 */},
{	0,			/* 137 */},
{	0,			/* 138 */},
{	0,			/* 139 */},
{	0,			/* 140 */},
{	0,			/* 141 */},
{	0,			/* 142 */},
{	0,			/* 143 */},
{	0,			/* 144 */},
{	0,			/* 145 */},
{	0,			/* 146 */},
{	0,			/* 147 */},
{	0,			/* 148 */},
{	0,			/* 149 */},
{	0,			/* 150 */},
{	0,			/* 151 */},
{	0,			/* 152 */},
{	0,			/* 153 */},
{	0,			/* 154 */},
{	0,			/* 155 */},
{	0,			/* 156 */},
{	0,			/* 157 */},
{	0,			/* 158 */},
{	0,			/* 159 */},
{	0,			/* 160 */},
{	"r!",			/* 161 */},
{	"ct",			/* 162 */},
{	"Po",			/* 163 */},
{	"Cs",			/* 164 */},
{	"Ye",			/* 165 */},
{	"bb",			/* 166 */},
{	"sc",			/* 167 */},
{	"ad",			/* 168 */},
{	"co",			/* 169 */},
{	"Of",			/* 170 */},
{	"Fo",			/* 171 */},
{	"tno",			/* 172 */},
{	"-", "hy",		/* 173 */},
{	"rg",			/* 174 */},
{	"a-",			/* 175 */},
{	"de",			/* 176 */},
{	"t+-",			/* 177 */},
{	"S2",			/* 178 */},
{	"S3",			/* 179 */},
{	"aa",			/* 180 */},
{	"mc",			/* 181 */},
{	"ps",			/* 182 */},
{	"pc",			/* 183 */},
{	"ac",			/* 184 */},
{	"S1",			/* 185 */},
{	"Om",			/* 186 */},
{	"Fc",			/* 187 */},
{	"14",			/* 188 */},
{	"12",			/* 189 */},
{	"34",			/* 190 */},
{	"r?",			/* 191 */},
{	"`A",			/* 192 */},
{	"'A",			/* 193 */},
{	"^A",			/* 194 */},
{	"~A",			/* 195 */},
{	":A",			/* 196 */},
{	"oA",			/* 197 */},
{	"AE",			/* 198 */},
{	",C",			/* 199 */},
{	"`E",			/* 200 */},
{	"'E",			/* 201 */},
{	"^E",			/* 202 */},
{	":E",			/* 203 */},
{	"`I",			/* 204 */},
{	"'I",			/* 205 */},
{	"^I",			/* 206 */},
{	":I",			/* 207 */},
{	"-D",			/* 208 */},
{	"~N",			/* 209 */},
{	"`O",			/* 210 */},
{	"'O",			/* 211 */},
{	"^O",			/* 212 */},
{	"~O",			/* 213 */},
{	":O",			/* 214 */},
{	"tmu",			/* 215 */},
{	"/O",			/* 216 */},
{	"`U",			/* 217 */},
{	"'U",			/* 218 */},
{	"^U",			/* 219 */},
{	":U",			/* 220 */},
{	"'Y",			/* 221 */},
{	"TP",			/* 222 */},
{	"ss",			/* 223 */},
{	"`a",			/* 224 */},
{	"'a",			/* 225 */},
{	"^a",			/* 226 */},
{	"~a",			/* 227 */},
{	":a",			/* 228 */},
{	"oa",			/* 229 */},
{	"ae",			/* 230 */},
{	",c",			/* 231 */},
{	"`e",			/* 232 */},
{	"'e",			/* 233 */},
{	"^e",			/* 234 */},
{	":e",			/* 235 */},
{	"`i",			/* 236 */},
{	"'i",			/* 237 */},
{	"^i",			/* 238 */},
{	":i",			/* 239 */},
{	"Sd",			/* 240 */},
{	"~n",			/* 241 */},
{	"`o",			/* 242 */},
{	"'o",			/* 243 */},
{	"^o",			/* 244 */},
{	"~o",			/* 245 */},
{	":o",			/* 246 */},
{	"tdi",			/* 247 */},
{	"/o",			/* 248 */},
{	"`u",			/* 249 */},
{	"'u",			/* 250 */},
{	"^u",			/* 251 */},
{	":u",			/* 252 */},
{	"'y",			/* 253 */},
{	"Tp",			/* 254 */},
{	":y",			/* 255 */},
}};

static DviCharNameMap Adobe_Symbol_map = {
	"adobe-fontspecific",
	1,
{
{	0,						/* 0 */},
{	0,						/* 1 */},
{	0,						/* 2 */},
{	0,						/* 3 */},
{	0,						/* 4 */},
{	0,						/* 5 */},
{	0,						/* 6 */},
{	0,						/* 7 */},
{	0,						/* 8 */},
{	0,						/* 9 */},
{	0,						/* 10 */},
{	0,						/* 11 */},
{	0,						/* 12 */},
{	0,						/* 13 */},
{	0,						/* 14 */},
{	0,						/* 15 */},
{	0,						/* 16 */},
{	0,						/* 17 */},
{	0,						/* 18 */},
{	0,						/* 19 */},
{	0,						/* 20 */},
{	0,						/* 21 */},
{	0,						/* 22 */},
{	0,						/* 23 */},
{	0,						/* 24 */},
{	0,						/* 25 */},
{	0,						/* 26 */},
{	0,						/* 27 */},
{	0,						/* 28 */},
{	0,						/* 29 */},
{	0,						/* 30 */},
{	0,						/* 31 */},
{	0,						/* 32 */},
{	"!",						/* 33 */},
{	"fa",						/* 34 */},
{	"#", "sh",					/* 35 */},
{	"te",						/* 36 */},
{	"%",						/* 37 */},
{	"&",						/* 38 */},
{	"st",						/* 39 */},
{	"(",						/* 40 */},
{	")",						/* 41 */},
{	"**",						/* 42 */},
{	"+", "pl",					/* 43 */},
{	",",						/* 44 */},
{	"\\-", "mi",					/* 45 */},
{	".",						/* 46 */},
{	"/", "sl",					/* 47 */},
{	"0",						/* 48 */},
{	"1",						/* 49 */},
{	"2",						/* 50 */},
{	"3",						/* 51 */},
{	"4",						/* 52 */},
{	"5",						/* 53 */},
{	"6",						/* 54 */},
{	"7",						/* 55 */},
{	"8",						/* 56 */},
{	"9",						/* 57 */},
{	":",						/* 58 */},
{	";",						/* 59 */},
{	"<",						/* 60 */},
{	"=", "eq",					/* 61 */},
{	">",						/* 62 */},
{	"?",						/* 63 */},
{	"=~",						/* 64 */},
{	"*A",						/* 65 */},
{	"*B",						/* 66 */},
{	"*X",						/* 67 */},
{	"*D",						/* 68 */},
{	"*E",						/* 69 */},
{	"*F",						/* 70 */},
{	"*G",						/* 71 */},
{	"*Y",						/* 72 */},
{	"*I",						/* 73 */},
{	"+h",						/* 74 */},
{	"*K",						/* 75 */},
{	"*L",						/* 76 */},
{	"*M",						/* 77 */},
{	"*N",						/* 78 */},
{	"*O",						/* 79 */},
{	"*P",						/* 80 */},
{	"*H",						/* 81 */},
{	"*R",						/* 82 */},
{	"*S",						/* 83 */},
{	"*T",						/* 84 */},
{	0,						/* 85 */},
{	"ts",						/* 86 */},
{	"*W",						/* 87 */},
{	"*C",						/* 88 */},
{	"*Q",						/* 89 */},
{	"*Z",						/* 90 */},
{	"[", "lB",					/* 91 */},
{	"tf", "3d",					/* 92 */},
{	"]", "rB",					/* 93 */},
{	"pp",						/* 94 */},
{	"_",						/* 95 */},
{	"radicalex",					/* 96 */},
{	"*a",						/* 97 */},
{	"*b",						/* 98 */},
{	"*x",						/* 99 */},
{	"*d",						/* 100 */},
{	"*e",						/* 101 */},
{	"*f",						/* 102 */},
{	"*g",						/* 103 */},
{	"*y",						/* 104 */},
{	"*i",						/* 105 */},
{	"+f",						/* 106 */},
{	"*k",						/* 107 */},
{	"*l",						/* 108 */},
{	"*m",						/* 109 */},
{	"*n",						/* 110 */},
{	"*o",						/* 111 */},
{	"*p",						/* 112 */},
{	"*h",						/* 113 */},
{	"*r",						/* 114 */},
{	"*s",						/* 115 */},
{	"*t",						/* 116 */},
{	"*u",						/* 117 */},
{	"+p",						/* 118 */},
{	"*w",						/* 119 */},
{	"*c",						/* 120 */},
{	"*q",						/* 121 */},
{	"*z",						/* 122 */},
{	"lC", "{",					/* 123 */},
{	"ba", "|",					/* 124 */},
{	"rC", "}",					/* 125 */},
{	"ap",						/* 126 */},
{	0,						/* 127 */},
{	0,						/* 128 */},
{	0,						/* 129 */},
{	0,						/* 130 */},
{	0,						/* 131 */},
{	0,						/* 132 */},
{	0,						/* 133 */},
{	0,						/* 134 */},
{	0,						/* 135 */},
{	0,						/* 136 */},
{	0,						/* 137 */},
{	0,						/* 138 */},
{	0,						/* 139 */},
{	0,						/* 140 */},
{	0,						/* 141 */},
{	0,						/* 142 */},
{	0,						/* 143 */},
{	0,						/* 144 */},
{	0,						/* 145 */},
{	0,						/* 146 */},
{	0,						/* 147 */},
{	0,						/* 148 */},
{	0,						/* 149 */},
{	0,						/* 150 */},
{	0,						/* 151 */},
{	0,						/* 152 */},
{	0,						/* 153 */},
{	0,						/* 154 */},
{	0,						/* 155 */},
{	0,						/* 156 */},
{	0,						/* 157 */},
{	0,						/* 158 */},
{	0,						/* 159 */},
{	0,						/* 160 */},
{	"*U",						/* 161 */},
{	"fm",						/* 162 */},
{	"<=",						/* 163 */},
{	"f/",						/* 164 */},
{	"if",						/* 165 */},
{	"Fn",						/* 166 */},
{	"CL",						/* 167 */},
{	"DI",						/* 168 */},
{	"HE",						/* 169 */},
{	"SP",						/* 170 */},
{	"<>",						/* 171 */},
{	"<-",						/* 172 */},
{	"ua", "arrowverttp",				/* 173 */},
{	"->",						/* 174 */},
{	"da", "arrowvertbt",				/* 175 */},
{	"de",						/* 176 */},
{	"+-",						/* 177 */},
{	"sd",						/* 178 */},
{	">=",						/* 179 */},
{	"mu",						/* 180 */},
{	"pt",						/* 181 */},
{	"pd",						/* 182 */},
{	"bu",						/* 183 */},
{	"di",						/* 184 */},
{	"!=",						/* 185 */},
{	"==",						/* 186 */},
{	"~=", "~~",					/* 187 */},
{	0,						/* 188 */},
{	"arrowvertex",					/* 189 */},
{	"an",						/* 190 */},
{	"CR",						/* 191 */},
{	"Ah",						/* 192 */},
{	"Im",						/* 193 */},
{	"Re",						/* 194 */},
{	"wp",						/* 195 */},
{	"c*",						/* 196 */},
{	"c+",						/* 197 */},
{	"es",						/* 198 */},
{	"ca",						/* 199 */},
{	"cu",						/* 200 */},
{	"sp",						/* 201 */},
{	"ip",						/* 202 */},
{	"nb",						/* 203 */},
{	"sb",						/* 204 */},
{	"ib",						/* 205 */},
{	"mo",						/* 206 */},
{	"nm",						/* 207 */},
{	"/_",						/* 208 */},
{	"gr",						/* 209 */},
{	"rg",						/* 210 */},
{	"co",						/* 211 */},
{	"tm",						/* 212 */},
{	0,						/* 213 */},
{	"sr", "sqrt",					/* 214 */},
{	"md",						/* 215 */},
{	"no",						/* 216 */},
{	"AN",						/* 217 */},
{	"OR",						/* 218 */},
{	"hA",						/* 219 */},
{	"lA",						/* 220 */},
{	"uA",						/* 221 */},
{	"rA",						/* 222 */},
{	"dA",						/* 223 */},
{	"lz",						/* 224 */},
{	"la",						/* 225 */},
{	0,						/* 226 */},
{	0,						/* 227 */},
{	0,						/* 228 */},
{	0,						/* 229 */},
{	"parenlefttp",					/* 230 */},
{	"parenleftex",					/* 231 */},
{	"parenleftbt",					/* 232 */},
{	"bracketlefttp", "lc",				/* 233 */},
{	"bracketleftex",				/* 234 */},
{	"bracketleftbt", "lf",				/* 235 */},
{	"bracelefttp", "lt",				/* 236 */},
{	"braceleftmid", "lk",				/* 237 */},
{	"braceleftbt", "lb",				/* 238 */},
{	"bracerightex", "braceleftex", "braceex", "bv",	/* 239 */},
{	0,						/* 240 */},
{	"ra",						/* 241 */},
{	"is", "integral",				/* 242 */},
{	0,						/* 243 */},
{	0,						/* 244 */},
{	0,						/* 245 */},
{	"parenrighttp",					/* 246 */},
{	"parenrightex",					/* 247 */},
{	"parenrightbt",					/* 248 */},
{	"bracketrighttp", "rc",				/* 249 */},
{	"bracketrightex",				/* 250 */},
{	"bracketrightbt", "rf",				/* 251 */},
{	"bracerighttp", "rt",				/* 252 */},
{	"bracerightmid", "rk",				/* 253 */},
{	"bracerightbt", "rb",				/* 254 */},
{	0,						/* 255 */},
}};


static void
load_standard_maps (void)
{
	standard_maps_loaded = 1;
	DviRegisterMap (&ISO8859_1_map);
	DviRegisterMap (&Adobe_Symbol_map);
}
