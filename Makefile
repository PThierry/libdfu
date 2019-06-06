###################################################################
# About the library name and path
###################################################################

# library name, without extension
LIB_NAME ?= libdfu

# project root directory, relative to app dir
PROJ_FILES = ../../

# library name, with extension
LIB_FULL_NAME = $(LIB_NAME).a

# SDK helper Makefiles inclusion
-include $(PROJ_FILES)/m_config.mk
-include $(PROJ_FILES)/m_generic.mk

# use an app-specific build dir
APP_BUILD_DIR = $(BUILD_DIR)/libs/$(LIB_NAME)

###################################################################
# About the compilation flags
###################################################################

CFLAGS := $(LIBS_CFLAGS)
CFLAGS += -MMD -MP -O3

#############################################################
# About library sources
#############################################################


SRC_DIR = .
SRC = $(wildcard $(SRC_DIR)/*.c)
OBJ = $(patsubst %.c,$(APP_BUILD_DIR)/%.o,$(SRC))
DEP = $(OBJ:.o=.d)

OUT_DIRS = $(dir $(OBJ))

# file to (dist)clean
# objects and compilation related
TODEL_CLEAN += $(OBJ)
# targets
TODEL_DISTCLEAN += $(APP_BUILD_DIR)

##########################################################
# About Frama-C checks
##########################################################

SESSION   :=frama-c-rte-val-wp.session
JOBS      :=$(shell nproc)
TIMEOUT   :=15

framac:
		frama-c-gui dfu.c \
			-cpp-command "gcc -std=c11 -E -C -I. -D__FRAMAC -I$(PROJ_FILES) -I$(PROJ_FILES)/include/generated -I$(PROJ_FILES)/libs/std/api -I$(PROJ_FILES)/drivers/socs/$(SOC)/usb/api" \
			-warn-left-shift-negative \
	        -warn-right-shift-negative \
			-warn-signed-downcast \
			-warn-signed-overflow \
			-warn-unsigned-downcast \
			-warn-unsigned-overflow \
			-rte \
		    -eva \
		    -wp-dynamic \
		    -eva-slevel 1 \
			-eva-warn-undefined-pointer-comparison none \
		    -then \
		    -wp \
		    -wp-dynamic \
		    -wp-par $(JOBS) \
		    -wp-steps 100000 -wp-depth 100000 -pp-annot \
		    -wp-split -wp-literals \
		    -wp-timeout $(TIMEOUT) -save $(SESSION)

framac-gui:
	frama-c-gui -load $(SESSION)


##########################################################
# generic targets of all libraries makefiles
##########################################################

.PHONY: app doc

default: all

all: $(APP_BUILD_DIR) lib

doc:
	$(Q)$(MAKE) BUILDDIR=../$(APP_BUILD_DIR)/doc  -C doc html latexpdf

show:
	@echo
	@echo "\tAPP_BUILD_DIR\t=> " $(APP_BUILD_DIR)
	@echo
	@echo "C sources files:"
	@echo "\tSRC_DIR\t\t=> " $(SRC_DIR)
	@echo "\tSRC\t\t=> " $(SRC)
	@echo "\tOBJ\t\t=> " $(OBJ)
	@echo

lib: $(APP_BUILD_DIR)/$(LIB_FULL_NAME)

$(APP_BUILD_DIR)/%.o: %.c
	$(call if_changed,cc_o_c)

# lib
$(APP_BUILD_DIR)/$(LIB_FULL_NAME): $(OBJ)
	$(call if_changed,mklib)
	$(call if_changed,ranlib)

$(APP_BUILD_DIR):
	$(call cmd,mkdir)

-include $(DEP)
-include $(TESTSDEP)
