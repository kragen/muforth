##  $Id$
##
##  This file is part of muforth.
##
##  Copyright 1997-2004 David Frech. All rights reserved, and all wrongs
##  reversed.
##
##  Licensed under the Apache License, Version 2.0 [the "License"];
##  you may not use this file except in compliance with the License.
##  You may obtain a copy of the License at
##
##     http://www.apache.org/licenses/LICENSE-2.0
##
##  or see the file LICENSE in the top directory of this distribution.
##
##  Unless required by applicable law or agreed to in writing, software
##  distributed under the License is distributed on an "AS IS" BASIS,
##  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
##  See the License for the specific language governing permissions and
##  limitations under the License.)


## Veneer code so we can make safe calls to C code without breaking our
## register convention. edx has address of routine to call.
	.globl	i386_into_cee
i386_into_cee:
	movl	%eax,-4(%ebx)
	addl	$-4,%ebx
	movl	%ebx,sp		# C expects sp to be stack pointer
	call	*%edx
	movl	sp,%ebx
	movl	(%ebx),%eax	# our register setup for Forth
	addl	$4,%ebx
	ret

## Veneer code so we can make safe calls from C code into Forth code without
## breaking our register convention. sp[0] has address of routine to call.
	.globl	i386_into_forth
i386_into_forth:
	pushl	%ebx		# callee saved!
	movl	sp,%ebx
	movl	(%ebx),%edx	# routine address
	movl	4(%ebx),%eax	# get top for Forth
	addl	$8,%ebx
	call	*%edx
	movl	%eax,-4(%ebx)
	addl	$-4,%ebx
	movl	%ebx,sp		# restore for C
	popl	%ebx		# callee saved!
	ret
	
## Mark the limits of these words so we can recognize them as non-C Forth.
	.globl	i386_lib_start
i386_lib_start:

	.globl	mu_dplus
mu_dplus:
	movl	(%ebx),%edx	# lo
	addl	8(%ebx),%edx	# sum_lo
	movl	%edx,8(%ebx)
	adcl	4(%ebx),%eax	# sum_hi
	addl	$8,%ebx
	ret

	.globl	mu_dnegate
mu_dnegate:
	negl	%eax
	negl	4(%ebx)
	sbbl	$0,(%ebx)
	ret

	.globl	mu_um_star
mu_um_star:
	movl	(%ebx),%eax
	mull	4(%ebx)
	movl	%eax,4(%ebx)	# prod low
	movl	%edx,%eax	# prod high
	ret

	.globl	mu_m_star
mu_m_star:
	movl	(%ebx),%eax
	imull	4(%ebx)
	movl	%eax,4(%ebx)	# prod low
	movl	%edx,%eax	# prod high
	ret

	.globl	mu_um_slash_mod
mu_um_slash_mod:
	movl	%eax,%edx	# dividend hi
	movl	4(%ebx),%eax	# dividend lo
	divl	(%ebx)		# divisor on stack
	movl	%edx,4(%ebx)	# remainder ; quotient in eax (top)
	addl	$4,%ebx
	ret

	.globl	mu_sm_slash_rem		# symmetric division, for comparison
mu_sm_slash_rem:
	movl	%eax,%edx	# dividend hi
	movl	4(%ebx),%eax	# dividend lo
	idivl	(%ebx)		# divisor on stack
	movl	%edx,4(%ebx)	# remainder ; quotient in eax (top)
	addl	$4,%ebx
	ret

##  IDIV is symmetric.  To fix it (to make it _FLOOR_) we have to
##  adjust the quotient and remainder when BOTH rem /= 0 AND
##  the divisor and dividend are different signs.  (This is NOT the
##  same as quot < 0, because the quot could be truncated to zero
##  by symmetric division when the actual quotient is < 0!)
##  The adjustment is:
##    q' = q - 1
##    r' = r + divisor
##
##  This preserves the invariant a / b => (r,q) s.t. qb + r = a.
##
##  q'b + r' = (q - 1)b + (r + b) = qb - b + r + b
##           = qb + r
##           = a
##
##  where q',r' are the _floored_ quotient and remainder (really, modulus),
##  and q,r are the symmetric quotient and remainder.
##
##  XXX: discuss how the range of the rem/mod changes as the num changes
##  (in symm. div.) and as the denom changes (in floored div).

	.globl	mu_fm_slash_mod		# floored division!
mu_fm_slash_mod:
	movl	%eax,%edx	# hi word of dividend in eax; move to edx
	movl	4(%ebx),%eax	# dividend = edx:eax, divisor on stack
	idivl	(%ebx)		# now modulus = edx, quotient = eax
	movl	(%ebx),%ecx
	xorl	4(%ebx),%ecx	## <0 if d'dend & d'sor are opposite signs
	jns	1f		# do nothing if same sign
	orl	%edx,%edx
	je	1f		# do nothing if mod == 0
	decl	%eax		# otherwise quot = quot - 1
	addl	(%ebx),%edx	#            mod = mod + divisor
1:	movl	%edx,4(%ebx)	# leave modulus and quotient, quotient on top
	addl	$4,%ebx
	ret

## for jumping thru a following "vector" of compiled calls, each 5 bytes long
	.globl	mu_jump
mu_jump:
	# edx = vector = return address; eax = index
	popl	%edx

	# edx points just _past_ the call/jmp instruction
	# we're interested in
	leal	5(%eax,%eax,4),%eax
	addl	%eax,%edx

	# ... so back up and get the offset, and add it to edx
	addl	-4(%edx),%edx
	movl	(%ebx),%eax	# reload top
	addl	$4,%ebx
	jmp	*%edx		# and go!

## Mark the limits of these words so we can recognize them as non-C Forth.
	.globl	i386_lib_end
i386_lib_end:

