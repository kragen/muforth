/*
 * $Id$
 *
 * This file is part of muforth.
 *
 * Copyright (c) 1997-2004 David Frech. All rights reserved, and all wrongs
 * reversed.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * or see the file LICENSE in the top directory of this distribution.
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <sys/types.h>
#include <string.h>

#include "env.h"

typedef int32_t		cell_t;
typedef int64_t		dcell_t;
typedef u_int8_t	code_t;
#define CELL_BYTES	4
#define CELL_BITS	32

/* data stack */
#define STACK_SIZE 4096
#define STACK_SAFETY 256
#define S0 &stack[STACK_SIZE - STACK_SAFETY]

/* gcc generates stupid code using this def'n */
/* #define PUSH(n) 	(*--sp = (cell_t)(n)) */
#define PUSH(n)		(sp[-1] = (cell_t)(n), --sp)
#define POP		(*sp++)
#define STK(n)  	(sp[n])
#define TOP		STK(0)
#define DROP(n)		(sp += n)
#define EXECUTE		(*(void (*)()) POP)()

#ifdef DEBUG
#include <stdio.h>
#define BUG printf
#else
#define BUG
#endif

#define ALIGN_SIZE	sizeof(cell_t)
#define ALIGNED(x)	(((cell_t)(x) + ALIGN_SIZE - 1) & -ALIGN_SIZE)

/*
 * struct string is a "normal" string: pointer to the first character, and
 * length. However, since these are often sitting on the data stack with
 * the length on top (and therefore at a _lower_ address) let's define it
 * that way.
 */
struct string
{
    size_t length;
    char *data;
};

/*
 * struct text is an odd beast. It's intended for parsing, and other
 * applications that scan a piece of text. To make this more efficient
 * we store a pointer to the _end_ of the text, and a _negative_
 * offset to its start, rather than the way struct string works.
 */

struct text
{
    char *end;
    ssize_t start;	/* ssize_t is a _signed_ type */
};

struct counted_string
{
    size_t length;	/* cell-sized length, unlike older Forths */
    char data[0];
};

#define COUNTED_STRING(x)	{ strlen(x), x }

extern struct string parsed;	/* for errors */

extern cell_t stack[];
extern cell_t *sp;

extern int  cmd_line_argc;
extern char **cmd_line_argv;

extern uint8_t *pnm0, *pdt0;	/* ptrs to name & data spaces */
extern code_t  *pcd0;		/* ptr to code space */

extern uint8_t *pnm, *pdt;    /* ptrs to next free byte in each space */
extern code_t  *pcd;

extern void (*mu_number)();
extern void (*mu_number_comma)();
extern void (*mu_name_hook)();		/* called when a name is created */

/* XXX: Gross hack alert! */
extern char *ate_the_stack;
extern char *isnt_defined;
extern char *version;

/* declare common functions */

/* public.h is automagically generated, and can match every public function
 * taking no arguments. Other functions need to be put here explicitly.
 */
#include "public.h"

/* compile.c */
char *to_counted_string(char *);

/* error.c */
void die(const char *msg);

/* kernel.c */
int string_compare(const char *string1, size_t length1,
                   const char *string2, size_t length2);
