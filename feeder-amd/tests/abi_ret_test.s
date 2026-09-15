	.file	"abi_ret_test.cpp"
	.intel_syntax noprefix
	.text
	.section	.text$_ZN4Impl5plainEPKcS1_,"x"
	.linkonce discard
	.align 2
	.p2align 4
	.globl	"_ZN4Impl5plainEPKcS1_"
	.def	"_ZN4Impl5plainEPKcS1_";	.scl	2;	.type	32;	.endef
	.seh_proc	"_ZN4Impl5plainEPKcS1_"
"_ZN4Impl5plainEPKcS1_":
.LFB7:
	.seh_endprologue
	lea	rax, [rdx+r8]
	ret
	.seh_endproc
	.section	.text$_ZN4Impl7wrappedEPKcS1_,"x"
	.linkonce discard
	.align 2
	.p2align 4
	.globl	"_ZN4Impl7wrappedEPKcS1_"
	.def	"_ZN4Impl7wrappedEPKcS1_";	.scl	2;	.type	32;	.endef
	.seh_proc	"_ZN4Impl7wrappedEPKcS1_"
"_ZN4Impl7wrappedEPKcS1_":
.LFB8:
	.seh_endprologue
	mov	rax, rcx
	add	r8, r9
	mov	QWORD PTR [rcx], r8
	ret
	.seh_endproc
	.text
	.p2align 4
	.globl	"call_plain"
	.def	"call_plain";	.scl	2;	.type	32;	.endef
	.seh_proc	"call_plain"
"call_plain":
.LFB15:
	.seh_endprologue
	lea	rax, [rcx+rdx]
	ret
	.seh_endproc
	.p2align 4
	.globl	"call_wrapped"
	.def	"call_wrapped";	.scl	2;	.type	32;	.endef
	.seh_proc	"call_wrapped"
"call_wrapped":
.LFB21:
	.seh_endprologue
	lea	rax, [rcx+rdx]
	ret
	.seh_endproc
	.globl	"_ZTS1C"
	.section	.rdata$_ZTS1C,"dr"
	.linkonce same_size
"_ZTS1C":
	.ascii "1C\0"
	.globl	"_ZTI1C"
	.section	.rdata$_ZTI1C,"dr"
	.linkonce same_size
	.align 8
"_ZTI1C":
	.quad	"_ZTVN10__cxxabiv117__class_type_infoE"+16
	.quad	"_ZTS1C"
	.globl	"_ZTS4Impl"
	.section	.rdata$_ZTS4Impl,"dr"
	.linkonce same_size
"_ZTS4Impl":
	.ascii "4Impl\0"
	.globl	"_ZTI4Impl"
	.section	.rdata$_ZTI4Impl,"dr"
	.linkonce same_size
	.align 8
"_ZTI4Impl":
	.quad	"_ZTVN10__cxxabiv120__si_class_type_infoE"+16
	.quad	"_ZTS4Impl"
	.quad	"_ZTI1C"
	.globl	"_ZTV4Impl"
	.section	.rdata$_ZTV4Impl,"dr"
	.linkonce same_size
	.align 8
"_ZTV4Impl":
	.quad	0
	.quad	"_ZTI4Impl"
	.quad	"_ZN4Impl5plainEPKcS1_"
	.quad	"_ZN4Impl7wrappedEPKcS1_"
	.globl	"g"
	.data
	.align 8
"g":
	.quad	"_ZTV4Impl"+16
	.ident	"GCC: (Rev3, Built by MSYS2 project) 16.2.0"
