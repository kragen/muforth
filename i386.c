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

/* Compiler essentials for Intel architecture */

#include "muforth.h"

/*
 * Lotsa defines to make the compiler easier to read and write.
 */

#define C1(b)	(*pcd++ = (b))
#define C4(w)	(*((cell_t *)pcd) = (w), pcd += 4)

/* The two meanings of w bit */
#define W32	1
#define W8	0

/* The d-bit */
#define TO_R	2	/* d-bit set */

#define MOV_IR(r,w)	(0xb0 + (8 * (w)) + (r))	/* move imm to reg */
#define MOV_A		0xa0	/* move eax/ax/al to/from offset (opt) */
#define MOV		0x88	/* move register,memory (generic) */

#define LEA		0x8d

/* For mod_rm bytes. */
#define MOD_RM(md,reg,base)	(((md) << 6) | ((reg) << 3) | (base))
#define MD_OFFSET_NONE		0
#define MD_OFFSET_8		1
#define MD_OFFSET_32		2
#define MD_REG			3

/* For sib bytes */
#define SIB(scale,index,base)	(((scale) << 6) | ((index) << 3) | (base))
#define SCALE_1		0
#define INDEX_NONE	R_SP

/* In use these are like registers, so we use R_ prefix; to convert to opcode
 * use OP_R.
*/
#define OP_R(r)		(r << 3)
#define R_ADD		0
#define R_AND		4
#define R_OR		1
#define R_SUB		5
#define R_XOR		6

#define NEG	0xf6
#define R_NEG	3
#define NOT	0xf6
#define R_NOT	2

#define SH_BY_1		0xd0
#define SH_BY_CL	0xd2
#define SH_BY_IMM	0xc0

#define R_SAR	7
#define R_SHL	4
#define R_SHR	5

#define IMM	0x80
#define IMM_SX	0x82

#define R_A	0
#define R_B	3
#define R_C	1
#define R_D	2

#define R_SP	4
#define R_SIB	R_SP	/* when used as base reg in mod_rm, sp means 
			   that an sib byte follows */

#define CALL	0xe8
#define JMP	0xe9
#define RET	0xc3

#define EXT	0x0f
#define JCOND	0x70	/* cond branch with 8 bit offset */
#define EJCOND	0x80	/* preceded by EXT, cond branch with 32 bit offset */
#define COND_EQ	0x04
#define COND_NE	0x05

#define POP_R	0x58
#define POP_M	0x8f
#define R_POP  0

#define PUSH_R	0x50
#define PUSH_M	0xff
#define R_PUSH 6

#define INC_R	0x40
#define INC_M	0xfe
#define R_INC	0

#define DEC_R	0x48
#define DEC_M	0xfe
#define R_DEC	1

static code_t *pcd_last_call;
static code_t *pcd_jump_dest = 0;

static void compile_add_sp(int n)
{
    /*
     * Changed from add to lea so it won't affect flags. Makes writing my
     * conditional test code much easier. ;-)
     */
    C1(LEA);				/* leal n(,%ebx),%ebx */
    C1(MOD_RM(MD_OFFSET_8, R_B, R_B));
#if 0
    C1(IMM_SX | W32);			/* addl $n,%ebx */
    C1(MOD_RM(MD_REG, R_ADD, R_B));
#endif
    C1(n);
}
    
/*
 * Code to manage moving from top register to stack contents pointed to by
 * ebx register.
 */
static void compile_pop_top(cell_t offset)
{
    C1(MOV | TO_R | W32);			/* movl offset(%ebx), %eax */
    if (offset == 0)
	C1(MOD_RM(MD_OFFSET_NONE, R_A, R_B));
    else
    {
	C1(MOD_RM(MD_OFFSET_8, R_A, R_B));
	C1(offset);
    }
    compile_add_sp(offset + 4);
}

static void compile_put_top(cell_t offset)
{
    C1(MOV | W32);				/* movl %eax,-4(%ebx) */
    C1(MOD_RM(MD_OFFSET_8, R_A, R_B));
    C1(-4);
}

static void compile_push_top()
{
    compile_put_top(-4);
    compile_add_sp(-4);
}

/*
 * With the fancy smart compiler literals change.
 *
 * eax is TOP. ebx is sp. A normal inline literal push looks like this:
 *
 *   movl  %eax,-4(%ebx)
 *   addl  -4,%ebx
 *   movl  $literal,%eax
 *
 * The code takes 3 + 3 + 5 = 11 bytes. The "load edx and jump" way took 10.
 * So we're not paying a high price. But the "load and jump" way was also easy
 * to break into two pieces. Load took 5 bytes. Jump/call took 5. Because we
 * have to push TOP before we can do anything, we can't cut this code in half
 * in such a nice way.
 *
 * So, I propose _two_ kinds of literal code: inline, and split. We just saw
 * inline; split looks like this (and will be used mostly in does> words):
 *
 *   movl  $literal,%edx
 *   jmp   somewhere
 *   ...
 * somwhere:
 *   movl  %eax,-4(%ebx)
 *   addl  -4,%ebx
 *   movl  %edx,%eax
 *
 * This last bit of code replaces what used to be call to a routine. That took
 * 5 bytes; this code takes 3 + 3 + 2 = 8 bytes, but doesn't appear very often
 * - it's only in defining words.
 */
static void compile_split_literal_load(cell_t lit)
{
    C1(MOV_IR(R_D, W32));	/* movl $literal, %edx */
    C4(lit);
}

void mu_compile_split_literal_load()
{
    compile_split_literal_load(POP);	/* pop from stack and compile */
}

void mu_compile_split_literal_push()
{
    compile_push_top();
    C1(MOV | W32);
    C1(MOD_RM(MD_REG, R_D, R_A));		/* movl %edx, %eax */
}

static void compile_inline_literal(cell_t lit)
{
    compile_push_top();
    C1(MOV_IR(R_A, W32));	/* movl $literal, %eax */
    C4(lit);
}

void mu_compile_inline_literal()
{
    /* pop literal value from stack and compile */
    compile_inline_literal(POP);
}

void mu_fetch_literal_value()
{
    uint32_t *p;

    p = (uint32_t *)(TOP + 1);	/* TOP points to "movl #x, %edx" */
    TOP = *p;			/* fetch the value that is loaded */
}

#ifdef NOTYET
void mu_was_literal()
{
    if (pcd - 5 == pcd_last_call && *pcd_last_call == 0xe8
	&& (pcd + *(uint32_t *)(pcd - 4) == (char *) &push_literal))
    {
	/* last code compiled was a call to push_literal; back up,
           uncompile the call, and push the literal value onto the stack */
	/* XXX */
    }
}
#endif

void mu_compile_drop()
{
    compile_pop_top(0);
}

void mu_compile_2drop()
{
    compile_pop_top(4);
}


static void compile_offset(code_t *dest)
{
    C4(dest - (pcd + 4));
}

static void compile_call(code_t *dest)
{
    pcd_last_call = pcd;
    C1(CALL);			/* call near, 32 bit offset */
    compile_offset(dest);
}

#if 0
void mu_compile_jump()
{
    pcd_last_call = pcd;
    C1(JMP);			/* jump near, 32 bit offset */
    compile_offset(dest);
}
#endif

static int muforth_word(code_t *p)
{
    return (p >= pcd0 || (p >= i386_lib_start && p < i386_lib_end));
}

void i386_execute()
{
    if (muforth_word((code_t *)TOP))
	i386_into_forth();
    else
	(*(void (*)()) POP)();
}

void mu_compile_call()
{
    code_t *dest = (code_t *) POP;

    if (muforth_word(dest))
	compile_call(dest);
    else
    {
	compile_split_literal_load((cell_t)dest);
	compile_call((code_t *)i386_into_cee);
    }
}

/*
 * mu_compile_entry()
 *
 * Noop for x86.
 */
void mu_compile_entry()
{
}

void mu_compile_exit()
{
    /* convert call in tail position to jump */
    if (pcd - 5 == pcd_last_call && *pcd_last_call == CALL)
    {
	*pcd_last_call = JMP;	/* tail call --> jump */

	/* if this is also a jump destination, compile a return instruction */
	if (pcd == pcd_jump_dest)
	    *pcd++ = RET;		/* ret near, don't pop any args */
    }
    else
	*pcd++ = RET;		/* ret near, don't pop any args */
}

/* This requires that eax be loaded with sp, either with (for if) or
   without (for =if) a stack adjustment. */

/* After running out of "displacement space" in a Forth word, I've changed
   the branch compilers to use the 32-bit displacement versions. */

/* XXX: It might make sense to make _forward_ branches always 32 bits -
   since we don't know ahead of time how far we'll have to jump - but
   make backwards branches only as long as they need to be. */

static void compile_test_top()
{
    C1(OP_R(R_OR) | W32);		/* orl %eax,%eax */
    C1(MOD_RM(MD_REG, R_A, R_A));
}
	
static void compile_zbranch()
{
    C1(EXT);				/* je off8 into je off32 */
    C1(EJCOND | COND_EQ);
    C4(0);				/* offset */
    PUSH(pcd);			/* 32 bit offset to fixup at (top)-1 */
}

void mu_compile_destructive_zbranch()
{
    compile_test_top();
    compile_pop_top(0);
    compile_zbranch();
}

void mu_compile_nondestructive_zbranch()
{
    compile_test_top();
    compile_zbranch();
}


void mu_compile_branch()
{
    C1(JMP);
    C4(0);
    PUSH(pcd);			/* offset to fix up later */
}

/* resolve a forward or backward jump - moved from startup.mu4 because it
 * was i386-specific. In this usage "src" points just _past_ the displacement
 * we need to fix up.
 * : resolve   ( src dest)  over -  swap cell- ! ( 32-bit displacement) ;
 */
void mu_resolve()
{
    uint32_t src = STK(1);
    uint32_t dest = TOP;
    uint32_t *psrc = (uint32_t *)src;
    psrc[-1] = dest - src;
    DROP(2);

    /* also set up last jump destination, for tail-call code */
    pcd_jump_dest = (code_t *)dest;
}


/* We don't need 2shunt because we can call this as many times as
 * necessary; it only compiles 1 byte per call!
 */
void mu_compile_shunt()		/* drop 4 bytes from r stack */
{
    C1(POP_R | R_D);		/* popl %edx */
}

/* Next challenge: for/next. These require popping and pushing the _other_
 * stack: the return (hardware) stack.
 */

void mu_compile_pop_from_r()
{
    compile_push_top();
    C1(POP_R | R_A);
}

/* After 2pop, what was top of return stack is now top of data stack. */
void mu_compile_2pop_from_r()
{
    compile_put_top(-4);
    C1(POP_R | R_A);		/* top R -> top D */
    C1(POP_M);			/* popl -8(%ebx) */
    C1(MOD_RM(MD_OFFSET_8, R_POP, R_B));
    C1(-8);
    compile_add_sp(-8);
}

void mu_compile_push_to_r()
{
    C1(PUSH_R | R_A);
    compile_pop_top(0);
}

/* After 2push, what was top of data stack is now top of return stack. */
void mu_compile_2push_to_r()
{
    C1(PUSH_M);					/* pushl (%ebx) */
    C1(MOD_RM(MD_OFFSET_NONE, R_PUSH, R_B));
    C1(PUSH_R | R_A);				/* pushl %eax */
    compile_pop_top(4);
}

void mu_compile_copy_from_r()
{
    compile_push_top();
    C1(MOV | TO_R | W32);			/* movl (%esp), %eax */
    C1(MOD_RM(MD_OFFSET_NONE, R_A, R_SIB));
    C1(SIB(SCALE_1, INDEX_NONE, R_SP));
}

void mu_compile_qfor()
{
    C1(MOV | W32);				/* movl %eax, %edx */
    C1(MOD_RM(MD_REG, R_A, R_D));
    mu_compile_destructive_zbranch();

    C1(PUSH_R | R_D);		/* our saved copy of top */
    PUSH(pcd);			/* push address to loop back to */
}

void mu_compile_next()
{
    C1(DEC_M | W32);
    C1(MOD_RM(MD_OFFSET_NONE, R_DEC, R_SIB));
    C1(SIB(SCALE_1, INDEX_NONE, R_SP));
    C1(EXT);
    C1(EJCOND | COND_NE);
    C4(0);
    PUSH(pcd);			/* for fixup of jne */
    C1(POP_R | R_D);		/* popl %edx */
}

/*
 * Now code to compile basic kernel operations!
 */
static void compile_2op(uint8_t op)
{
    C1(OP_R(op) | TO_R | W32);			/* <op>  (%ebx),%eax */
    C1(MOD_RM(MD_OFFSET_NONE, R_A, R_B));
    compile_add_sp(4);
}

static void compile_1op(uint8_t op_prefix, uint8_t op)
{
    C1(op_prefix | W32);
    C1(MOD_RM(MD_REG, op, R_A));
}

void mu_add()
{
    compile_2op(R_ADD);
}

void mu_and()
{
    compile_2op(R_AND);
}

void mu_or()
{
    compile_2op(R_OR);
}

void mu_xor()
{
    compile_2op(R_XOR);
}

void mu_invert()
{
    compile_1op(NOT, R_NOT);
}

void mu_negate()
{
    compile_1op(NEG, R_NEG);
}

void mu_two_star()
{
    compile_1op(SH_BY_1, R_SHL);
}

void mu_two_slash()
{
    compile_1op(SH_BY_1, R_SAR);
}

void mu_two_slash_unsigned()
{
    compile_1op(SH_BY_1, R_SHR);
}

void mu_fetch()
{
    C1(MOV | TO_R | W32);
    C1(MOD_RM(MD_OFFSET_NONE, R_A, R_A));
}

void mu_dupe()
{
    compile_push_top();
}

void mu_nip()
{
    compile_add_sp(4);
}

