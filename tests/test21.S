/* SPDX-License-Identifier: GPL-2.0-or-later */

#define ASM_NL ;
#define DEFINE_FUNCTION(name, marker) \
	.globl name ASM_NL             \
	name: ASM_NL                   \
	.type name, marker

	.type direct_before, @function
direct_before:

direct_after:
	.type direct_after, %function

	.type hash_function, #function
hash_function:

stt_function:
	.type stt_function, STT_FUNC

DEFINE_FUNCTION(macro_function, @function)

not_a_function:
	.type not_a_function, @object

ordinary_label:

x86_caller:
	call x86_target
	call *%rax
	.type x86_caller, @function

arm64_caller:
	bl arm64_target
	blr x0
	.type arm64_caller, %function

riscv_caller:
	call riscv_target
	jal riscv_jal_target
	jal t0, .Llocal_jump
	.type riscv_caller, @function

s390_caller:
	brasl %r14, s390_target
	.type s390_caller, @function

m68k_caller:
	jbsr m68k_target
	jbsr %a0@
	.type m68k_caller, @function

	.ent ent_caller
ent_caller:
	call ent_target
	.end ent_caller
