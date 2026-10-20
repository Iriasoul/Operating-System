PROJ	:= lab2
EMPTY	:=
SPACE	:= $(EMPTY) $(EMPTY)
SLASH	:= /

V       := @

# physical page manager configuration: PMM=best_fit (default) or PMM=buddy.
# PMM=buddy BUDDY_TEST=1 additionally builds kern/mm/buddy_test.c with
# -D UCORE_BUDDY_TEST and runs the dedicated buddy behavior tests at boot;
# enabling BUDDY_TEST without PMM=buddy is rejected. PMM=buddy SLUB=1
# additionally builds kern/mm/slub.c with -D UCORE_SLUB and initializes the
# SLUB object allocator at boot; SLUB without PMM=buddy is rejected, and
# BUDDY_TEST together with SLUB is rejected. PMM=buddy SLUB=1 SLUB_TEST=1
# additionally builds kern/mm/slub_test.c with -D UCORE_SLUB_TEST and runs
# the dedicated SLUB behavior tests at boot right after slub_init();
# SLUB_TEST without PMM=buddy or without SLUB=1 is rejected, and BUDDY_TEST
# together with SLUB_TEST is rejected. The effective configuration is
# named PMMCFG: compile-time defines come from DEFS, object files live in
# obj/$(PMMCFG), and bin/.pmm_config records $(PMMCFG), so switching between
# best_fit, buddy, buddy_test, buddy_slub and buddy_slub_test never reuses
# the other configuration's object files or images and no manual make clean
# is needed.
PMM ?= best_fit
BUDDY_TEST ?= 0
SLUB ?= 0
SLUB_TEST ?= 0
ifneq ($(BUDDY_TEST),0)
ifneq ($(BUDDY_TEST),1)
$(error unsupported BUDDY_TEST "$(BUDDY_TEST)": use 0 or 1)
endif
endif
ifneq ($(SLUB),0)
ifneq ($(SLUB),1)
$(error unsupported SLUB "$(SLUB)": use 0 or 1)
endif
endif
ifneq ($(SLUB_TEST),0)
ifneq ($(SLUB_TEST),1)
$(error unsupported SLUB_TEST "$(SLUB_TEST)": use 0 or 1)
endif
endif

ifeq ($(PMM),best_fit)
PMMCFG := best_fit
ifneq ($(BUDDY_TEST),0)
$(error BUDDY_TEST=$(BUDDY_TEST) requires PMM=buddy)
endif
ifneq ($(SLUB),0)
$(error SLUB=$(SLUB) requires PMM=buddy)
endif
ifneq ($(SLUB_TEST),0)
$(error SLUB_TEST=$(SLUB_TEST) requires PMM=buddy)
endif
else
ifeq ($(PMM),buddy)
PMMCFG := buddy
DEFS += -D UCORE_PMM_BUDDY
ifneq ($(BUDDY_TEST),0)
ifneq ($(SLUB),0)
$(error BUDDY_TEST=$(BUDDY_TEST) and SLUB=$(SLUB) are mutually exclusive)
endif
ifneq ($(SLUB_TEST),0)
$(error BUDDY_TEST=$(BUDDY_TEST) and SLUB_TEST=$(SLUB_TEST) are mutually exclusive)
endif
PMMCFG := buddy_test
DEFS += -D UCORE_BUDDY_TEST
else
ifeq ($(SLUB),1)
PMMCFG := buddy_slub
DEFS += -D UCORE_SLUB
ifneq ($(SLUB_TEST),0)
PMMCFG := buddy_slub_test
DEFS += -D UCORE_SLUB_TEST
endif
else
ifneq ($(SLUB_TEST),0)
$(error SLUB_TEST=$(SLUB_TEST) requires SLUB=1)
endif
PMMCFG := buddy
endif
endif
else
$(error unsupported PMM "$(PMM)": use best_fit or buddy)
endif
endif

#ifndef GCCPREFIX
GCCPREFIX := riscv64-unknown-elf-
#endif

ifndef QEMU
QEMU := qemu-system-riscv64
endif

ifndef SPIKE
SPIKE := spike
endif

# eliminate default suffix rules
.SUFFIXES: .c .S .h

# delete target files if there is an error (or make is interrupted)
.DELETE_ON_ERROR:

# define compiler and flags
HOSTCC		:= gcc
HOSTCFLAGS	:= -Wall -O2

GDB		:= $(GCCPREFIX)gdb

CC		:= $(GCCPREFIX)gcc
CFLAGS  := -mcmodel=medany -std=gnu99 -Wno-unused -Werror 
CFLAGS	+= -fno-builtin -Wall -O2 -nostdinc $(DEFS)
CFLAGS	+= -fno-stack-protector -ffunction-sections -fdata-sections
CFLAGS	+= -g
CFLAGS2 = $(CFLAGS) -D ucore_test
CTYPE	:= c S
LD      := $(GCCPREFIX)ld
LDFLAGS	:= -m elf64lriscv
LDFLAGS	+= -nostdlib --gc-sections

OBJCOPY := $(GCCPREFIX)objcopy
OBJDUMP := $(GCCPREFIX)objdump

COPY	:= cp
MKDIR   := mkdir -p
MV		:= mv
RM		:= rm -f
AWK		:= awk
SED		:= sed
SH		:= sh
TR		:= tr
TOUCH	:= touch -c

OBJDIR	:= obj/$(PMMCFG)
BINDIR	:= bin

# bin/.pmm_config remembers which configuration the linked images came from.
# Objects live in per-config objdirs, so after a config switch without make
# clean the shared bin/kernel would look newer than the other config's
# objects and never relink; force it instead. The identity includes the
# test switches (best_fit, buddy, buddy_test, buddy_slub, buddy_slub_test).
RELINK_PMM := $(shell cat $(BINDIR)/.pmm_config 2>/dev/null)
ifneq ($(RELINK_PMM),$(PMMCFG))
KERNEL_FORCE := FORCE
endif

ALLOBJS	:=
ALLDEPS	:=
TARGETS	:=

include tools/function.mk

listf_cc = $(call listf,$(1),$(CTYPE))

# for cc
add_files_cc = $(call add_files,$(1),$(CC),$(CFLAGS) $(3),$(2),$(4))
add_files_cc2 = $(call add_files,$(1),$(CC),$(CFLAGS2) $(3),$(2),$(4))
create_target_cc = $(call create_target,$(1),$(2),$(3),$(CC),$(CFLAGS))

# for hostcc
add_files_host = $(call add_files,$(1),$(HOSTCC),$(HOSTCFLAGS),$(2),$(3))
create_target_host = $(call create_target,$(1),$(2),$(3),$(HOSTCC),$(HOSTCFLAGS))

cgtype = $(patsubst %.$(2),%.$(3),$(1))
objfile = $(call toobj,$(1))
asmfile = $(call cgtype,$(call toobj,$(1)),o,asm)
outfile = $(call cgtype,$(call toobj,$(1)),o,out)
symfile = $(call cgtype,$(call toobj,$(1)),o,sym)

# for match pattern
match = $(shell echo $(2) | $(AWK) '{for(i=1;i<=NF;i++){if(match("$(1)","^"$$(i)"$$")){exit 1;}}}'; echo $$?)

# >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
# include kernel/user

INCLUDE	+= libs/

CFLAGS	+= $(addprefix -I,$(INCLUDE))

LIBDIR	+= libs

$(call add_files_cc,$(call listf_cc,$(LIBDIR)),libs,)

# -------------------------------------------------------------------
# kernel

KINCLUDE	+= kern/debug/ \
			   kern/driver/ \
			   kern/mm/ \
			   kern/arch/

KSRCDIR		+= kern/init \
			   kern/libs \
			   kern/debug \
			   kern/driver \
			   kern/mm

KCFLAGS		+= $(addprefix -I,$(KINCLUDE))

ifeq ($(MAKECMDGOALS),test)
$(call add_files_cc2,$(call listf_cc,$(KSRCDIR)),kernel,$(KCFLAGS))
else
$(call add_files_cc,$(call listf_cc,$(KSRCDIR)),kernel,$(KCFLAGS))
endif
KOBJS	= $(call read_packet,kernel libs)

# create kernel target
kernel = $(call totarget,kernel)

$(kernel): tools/kernel.ld

$(kernel): $(KOBJS) $(KERNEL_FORCE)
	@echo + ld $@
	$(V)$(LD) $(LDFLAGS) -T tools/kernel.ld -o $@ $(KOBJS)
	@echo $(PMMCFG) > $(BINDIR)/.pmm_config
	@$(OBJDUMP) -S $@ > $(call asmfile,kernel)
	@$(OBJDUMP) -t $@ | $(SED) '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(call symfile,kernel)

$(call create_target,kernel)

# -------------------------------------------------------------------
# create ucore.img
UCOREIMG	:= $(call totarget,ucore.img)


$(UCOREIMG): $(kernel)
	$(OBJCOPY) $(kernel) --strip-all -O binary $@

$(call create_target,ucore.img)

# >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

$(call finish_all)

IGNORE_ALLDEPS	= clean \
				  dist-clean \
				  grade \
				  touch \
				  print-.+ \
				  handin

ifeq ($(call match,$(MAKECMDGOALS),$(IGNORE_ALLDEPS)),0)
-include $(ALLDEPS)
endif

# files for grade script

TARGETS: $(TARGETS)

.DEFAULT_GOAL := TARGETS
# -kernel instead of -device loader: with this QEMU's default OpenSBI
# (fw_dynamic) the loader device leaves Domain0 Next Address = 0, so the
# firmware jumps to 0 and no kernel output ever appears
.PHONY: qemu spike test
qemu: $(UCOREIMG) $(SWAPIMG) $(SFSIMG)
	$(V)$(QEMU) \
		-machine virt \
		-nographic \
		-bios default \
		-kernel $(UCOREIMG)

debug: $(UCOREIMG) $(SWAPIMG) $(SFSIMG)
	$(V)$(QEMU) \
		-machine virt \
		-nographic \
		-bios default \
		-kernel $(UCOREIMG) \
		-s -S

gdb:
	riscv64-unknown-elf-gdb \
    -ex 'file bin/kernel' \
    -ex 'set arch riscv:rv64' \
    -ex 'target remote localhost:1234'

test: $(UCOREIMG) $(SWAPIMG) $(SFSIMG)
	$(V)$(QEMU) \
		-machine virt \
		-nographic \
		-bios default \
		-kernel $(UCOREIMG)
spike: $(UCOREIMG) $(SWAPIMG) $(SFSIMG)
	$(V)$(SPIKE) $(UCOREIMG)

.PHONY: grade touch

GRADE_GDB_IN	:= .gdb.in
GRADE_QEMU_OUT	:= .qemu.out
HANDIN			:= proj$(PROJ)-handin.tar.gz

TOUCH_FILES		:=

MAKEOPTS		:= --quiet --no-print-directory

GRADE_TMP	:= /tmp/lab2_grade

grade:
	$(V)$(MAKE) $(MAKEOPTS) clean
	$(V)$(RM) -rf $(GRADE_TMP)
	$(V)$(MKDIR) $(GRADE_TMP)
	$(V)tar -C . --exclude=.git --exclude=.vscode --exclude=vcs -cf - . | tar -C $(GRADE_TMP) -xf -
	$(V)python3 tools/inject.py $(GRADE_TMP)
	$(V)cd $(GRADE_TMP) && $(SH) tools/grade.sh
	$(V)$(RM) -rf $(GRADE_TMP)

touch:
	$(V)$(foreach f,$(TOUCH_FILES),$(TOUCH) $(f))

print-%:
	@echo $($(shell echo $(patsubst print-%,%,$@) | $(TR) [a-z] [A-Z]))

FORCE:
.PHONY: FORCE

.PHONY: clean dist-clean handin packall tags
clean:
	$(V)$(RM) $(GRADE_GDB_IN) $(GRADE_QEMU_OUT) cscope* tags
	-$(RM) -r $(OBJDIR) $(BINDIR)

dist-clean: clean
	-$(RM) $(HANDIN)

handin: packall
	@echo Please visit http://learn.tsinghua.edu.cn and upload $(HANDIN). Thanks!

packall: clean
	@$(RM) -f $(HANDIN)
	@tar -czf $(HANDIN) `find . -type f -o -type d | grep -v '^\.*$$' | grep -vF '$(HANDIN)'`

tags:
	@echo TAGS ALL
	$(V)rm -f cscope.files cscope.in.out cscope.out cscope.po.out tags
	$(V)find . -type f -name "*.[chS]" >cscope.files
	$(V)cscope -bq 
	$(V)ctags -L cscope.files
