#=============================================================================
# Phone.mk
#
# Licensed Materials - Property of IBM
# AT7PRNA V2.3	AT2T5ZZ V4.3	
# (C) Copyright IBM Corp. 2000, 2004  All Rights Reserved.
# US Government Users Restricted Rights - Use, duplication or disclosure
# restricted by GSA ADP Schedule Contract with IBM Corp.
#
# The following IBM source code is provided to assist you in your
# development.  You may use this code only in accordance with the
# IBM License Agreement accompanying the Licensed Materials.
#
# This copyright statement may not be removed.
#=============================================================================

# Build type definition
#    PRODUCT = Release level build
#    DEBUG   = Debug build
#    TGTOS   = NT (make -f phone.mk TGTOS=NT CPU=X86)
BLDTYPE = PRODUCT

# Path to the root of the SDK where it is installed on the system.
SDK_ROOT = ../..

# Paths to the required directories within the SDK.
SDK_BIN = $(SDK_ROOT)/bin/nt/x86

SDK_DATA_ESR = $(SDK_ROOT)/data/esr
SDK_DATA_CTTS = $(SDK_ROOT)/data/eCTTS
SDK_LIB_BOARD = $(SDK_ROOT)/lib/$(TGTOS)/$(CPU)/waveio
SDK_LIB_COMMON = $(SDK_ROOT)/lib/$(TGTOS)/$(CPU)/common

# Path to represent the target being built.  If multiple platforms
# are added, then this will also include the target platform information.
TGTDIR = $(TGTOS)/$(BLDTYPE)

# Paths to the output directories for the executables and temporary files.
EXE_DIR = exe/$(TGTDIR)
OBJ_DIR = obj/$(TGTDIR)

# Macros used to control the actual build tools.
ECHO=echo.exe
RM=rm.exe 
COPY =cp.exe
OBJ_EXT=obj
EXE_EXT=.exe
LIB_EXT=.lib
CC=cl

COBJ_NAME_FLAG=/Fo
CCOMP_ONLY_FLAG=-c
ifeq ($(BLDTYPE),DEBUG)
        CFLAGS = /nologo /MTd /W3 /GX /Od /D"WIN32" /D "_WINDOWS" /D"_DEBUG" /Z7
else
        CFLAGS = /nologo /MT /W3 /GX /O2 /D"WIN32" /D "_WINDOWS" /D"NDEBUG"
endif

GEN_EXEC=link
GEN_EXEC_OUTPUT_FLAG= /out:# Note: No blank space after the /out:
ifeq ($(BLDTYPE),DEBUG)
        GEN_EXEC_FLAGS=/DEBUG /MAP /DEBUGTYPE:BOTH /PDB:NONE
else
        GEN_EXEC_FLAGS=/MAP
endif

# Required OS Specific libraries.
OS_LIBS =       user32.lib   \
                winmm.lib    \
                gdi32.lib    \
                comdlg32.lib \
		advapi32.lib

# Required libraries from the EVV SDK.
SDK_LIBS =  $(SDK_LIB_COMMON)/eal$(LIB_EXT) 		\
            $(SDK_LIB_BOARD)/dil$(LIB_EXT)              \
	    $(SDK_LIB_COMMON)/ecommon$(LIB_EXT)         \
	    $(SDK_LIB_COMMON)/esr_mr$(LIB_EXT)          \
	    $(SDK_LIB_COMMON)/edu$(LIB_EXT)             \
	    $(SDK_LIB_COMMON)/aop$(LIB_EXT)	        \
	    $(SDK_LIB_COMMON)/vocu$(LIB_EXT)            \
	    $(SDK_LIB_COMMON)/ebgstub$(LIB_EXT)         \
	    $(SDK_LIB_COMMON)/eci$(LIB_EXT)	        \
	    $(SDK_LIB_COMMON)/exml$(LIB_EXT)

# Include directories.  Both the application local and the SDK are listed here.
INC_DIRS =	-I$(EXE_DIR) -Iinc -I$(SDK_ROOT)/include/common \
			-I$(SDK_ROOT)/include/nt/x86

ACOUSTIC_MODEL = EAK01AGE

###############################################################################
# Locally defined macros to files used during the build.
###############################################################################

# Variable containing paths and IDs for engine initialization data files.
PA0:=$(SDK_DATA_ESR)/LBLR/LANGS/EN_US/LE/$(ACOUSTIC_MODEL).DSP:200
PA1:=$(SDK_DATA_ESR)/LBLR/LANGS/EN_US/LE/$(ACOUSTIC_MODEL).SD:201
PA2:=$(SDK_DATA_ESR)/LBLR/LANGS/EN_US/LE/$(ACOUSTIC_MODEL).LBL:202
PA3:=$(SDK_DATA_ESR)/LBLR/LANGS/EN_US/LE/$(ACOUSTIC_MODEL).DCD:203
PA4:=$(SDK_DATA_ESR)/LBLR/LANGS/EN_US/LE/$(ACOUSTIC_MODEL).ABS:204
PA5:=$(SDK_DATA_ESR)/LBLR/LANGS/EN_US/LE/$(ACOUSTIC_MODEL).MN:205
PA6:=$(SDK_DATA_ESR)/LBLR/LANGS/EN_US/LE/$(ACOUSTIC_MODEL).PQ:206
PA7:=$(SDK_DATA_ESR)/LBLR/LANGS/EN_US/LE/$(ACOUSTIC_MODEL).CQ:207
PA8:=$(SDK_DATA_ESR)/LBLR/LANGS/EN_US/LE/$(ACOUSTIC_MODEL).RQ:208
PA9:=$(SDK_DATA_ESR)/LBLR/LANGS/EN_US/LE/$(ACOUSTIC_MODEL).TR:209
# .PCH FOR CHINESE ONLY
#PA10:=$(SDK_DATA_ESR)/LBLR/LANGS/EN_US/LE/$(ACOUSTIC_MODEL).PCH:210
# .LBE and .ATA FOR ENROLLMENT ONLY
#PA11:=$(SDK_DATA_ESR)/LBLR/LANGS/EN_US/LE/$(ACOUSTIC_MODEL).LBE:211
#PA12:=$(SDK_DATA_ESR)/LBLR/LANGS/EN_US/LE/$(ACOUSTIC_MODEL).ATA:212
PA13:=$(SDK_DATA_ESR)/LBLR/LANGS/EN_US/LE/$(ACOUSTIC_MODEL).PS:213

# Variables containing paths to source .wav files.
DT0SW=wav/dtmf0.wav
DT1SW=wav/dtmf1.wav
DT2SW=wav/dtmf2.wav
DT3SW=wav/dtmf3.wav
DT4SW=wav/dtmf4.wav
DT5SW=wav/dtmf5.wav
DT6SW=wav/dtmf6.wav
DT7SW=wav/dtmf7.wav
DT8SW=wav/dtmf8.wav
DT9SW=wav/dtmf9.wav
DTSTARSW=wav/dtmfstar.wav
DTPOUNDSW=wav/dtmfpnd.wav
READYSW=wav/ready.wav

# Variables containing paths for bldpcm.exe output files.
DT0TP=$(OBJ_DIR)/dtmf0_pcm.le
DT1TP=$(OBJ_DIR)/dtmf1_pcm.le
DT2TP=$(OBJ_DIR)/dtmf2_pcm.le
DT3TP=$(OBJ_DIR)/dtmf3_pcm.le
DT4TP=$(OBJ_DIR)/dtmf4_pcm.le
DT5TP=$(OBJ_DIR)/dtmf5_pcm.le
DT6TP=$(OBJ_DIR)/dtmf6_pcm.le
DT7TP=$(OBJ_DIR)/dtmf7_pcm.le
DT8TP=$(OBJ_DIR)/dtmf8_pcm.le
DT9TP=$(OBJ_DIR)/dtmf9_pcm.le
DTSTARTP=$(OBJ_DIR)/dtmfstar_pcm.le
DTPOUNDTP=$(OBJ_DIR)/dtmfpnd_pcm.le
READYTP=$(OBJ_DIR)/ready_pcm.le

# Variable containing paths and IDs for system supplied pool files.
POOL1:=$(SDK_DATA_ESR)/VIAVOICE/VOCABS/LANGS/$(ACOUSTIC_MODEL)/POOLS/LE/BLAZER.POL:1
POOL2:=$(SDK_DATA_ESR)/VIAVOICE/VOCABS/LANGS/$(ACOUSTIC_MODEL)/POOLS/LE/STARTUS.POL:2
POOL3:=$(SDK_DATA_ESR)/VIAVOICE/VOCABS/LANGS/$(ACOUSTIC_MODEL)/POOLS/LE/W95NAV.POL:3

# Variable containing the CTTS voice files.
CTTS1:=$(SDK_DATA_CTTS)/VOICES/EN_US/LE/E2MLAN00.LE:1

# List of the source files.
C_SRCS =src/addrbook.c	      \
		src/audio.c   \
		src/esrcbs.c  \
		src/oswin32.c \
		src/phone.c   \
		src/uifuncs.c

# List of the object files built from the source file list.
C_OBJS = $(patsubst src%, $(OBJ_DIR)%,$(C_SRCS))
C_OBJS := $(patsubst %.c, %.$(OBJ_EXT),$(C_OBJS))

# List of the header files.
C_HDRS = $(wildcard inc/*.h)

###############################################################################
# RULES
###############################################################################

# all
# Performs all steps.
ifeq ($(TGTOS),NT)
all:  targetall
else
all:
	@$(ECHO) sample app is not build for $(TGTOS)
endif #NT

targetall: clean makedirs binfiles exe

# exe
# Main target for building the executable.
# Note that since the headers generated by bldrom.exe are required,
# the binfiles are a dependency here also.
exe: binfiles $(EXE_DIR)/phone$(EXE_EXT)

# clean
# Remove all output and temporary files.
clean: cleantemp
	-$(RM) $(EXE_DIR)/*.* 

# cleantemp
# Remove only the temporay files, leaving what is needed to run the application.
cleantemp:
	-$(RM) -rf obj 
	-$(RM) -f $(SDK_DATA_ESR)/ViaVoice/Vocabs/langs/$(ACOUSTIC_MODEL)/pools/phone.ppl
	-$(RM) -f vocabs/*.fsg
	-$(RM) -f $(EXE_DIR)/*.h 
	-$(RM) -f $(EXE_DIR)/*.exp 
	-$(RM) -f $(EXE_DIR)/*.lib 

# makedirs
# Make the required output directories.
makedirs:
	-mkdir exe
	-mkdir exe/$(TGTOS)
	-mkdir $(EXE_DIR)
	-mkdir obj
	-mkdir obj/$(TGTOS)
	-mkdir $(OBJ_DIR)
#	-mkdir $(OBJ_DIR)/vocabs

# binfiles
# Target representing the bin files (collection) that the application requires
# at runtime.
binfiles: $(EXE_DIR)/engdata.bin $(EXE_DIR)/vocabs.bin \
	  $(EXE_DIR)/audprmts.bin $(EXE_DIR)/pools.bin $(EXE_DIR)/ctts.bin


# engdata.bin
# Collection containing the engine initialization data.  The data is retrieved
# from the SDK tree and packaged into a bin file that can be located in the
# devices ROM.
$(EXE_DIR)/engdata.bin:
	@@$(ECHO) .
	@@$(ECHO) Generate engine initialization data bin file.
	@@$(ECHO) .
	@@$(ECHO) inputfiles: > $(OBJ_DIR)/engdata.def
	@$(ECHO) $(PA0) >> $(OBJ_DIR)/engdata.def
	@$(ECHO) $(PA1) >> $(OBJ_DIR)/engdata.def
	@$(ECHO) $(PA2) >> $(OBJ_DIR)/engdata.def
	@$(ECHO) $(PA3) >> $(OBJ_DIR)/engdata.def
	@$(ECHO) $(PA4) >> $(OBJ_DIR)/engdata.def
	@$(ECHO) $(PA5) >> $(OBJ_DIR)/engdata.def
	@$(ECHO) $(PA6) >> $(OBJ_DIR)/engdata.def
	@$(ECHO) $(PA7) >> $(OBJ_DIR)/engdata.def
	@$(ECHO) $(PA8) >> $(OBJ_DIR)/engdata.def
	@$(ECHO) $(PA9) >> $(OBJ_DIR)/engdata.def
	@$(ECHO) $(PA13) >> $(OBJ_DIR)/engdata.def
	@$(ECHO). >> $(OBJ_DIR)/engdata.def
	@$(ECHO) outputdir=$(EXE_DIR) >> $(OBJ_DIR)/engdata.def
	@$(ECHO) outputname=engdata.bin >> $(OBJ_DIR)/engdata.def
	$(SDK_BIN)/bldrom   $(OBJ_DIR)/engdata.def

# phone.ppl
# Build the pool file containing words not in the default dictionaries.
$(OBJ_DIR)/phone.ppl: vocabs/phone.pbsp
	@$(ECHO) .
	@$(ECHO) Generate pool file.
	@$(ECHO) .
	$(SDK_BIN)/bldwords /a "r" /l "EN_US" \
	       	/p "$(SDK_DATA_ESR)/ViaVoice/Vocabs/langs/$(ACOUSTIC_MODEL)/en_us.pst" \
        	vocabs/phone.pbsp                                             \
	       	$(SDK_DATA_ESR)/ViaVoice/Vocabs/langs/$(ACOUSTIC_MODEL)/pools/phone.ppl

# phone_voc.le
# Little endian version of the vocabulary set required by the application.
$(OBJ_DIR)/phone_voc.le: $(OBJ_DIR)/phone.ppl vocabs/1_main.bnf                  \
					 vocabs/2_names.bnf vocabs/3_options.bnf \
					 vocabs/4_yesno.bnf \
					 vocabs/500fm.bnf
	@$(ECHO) .
	@$(ECHO) Generate vocabulary set files.
	@$(ECHO) .
	
	@$(ECHO) set:  						 > $(OBJ_DIR)/phone.def
	@$(ECHO) name	=$(OBJ_DIR)/phone_voc			>> $(OBJ_DIR)/phone.def
	@$(ECHO) init_prots_id= $(ACOUSTIC_MODEL)		>> $(OBJ_DIR)/phone.def
	@$(ECHO) params:						>> $(OBJ_DIR)/phone.def
	@$(ECHO) path    =	$(SDK_DATA_ESR)		        >> $(OBJ_DIR)/phone.def
	@$(ECHO) language= $(ACOUSTIC_MODEL)			>> $(OBJ_DIR)/phone.def
	@$(ECHO) taskid  =navus					>> $(OBJ_DIR)/phone.def
	@$(ECHO) pools:						>> $(OBJ_DIR)/phone.def
	@$(ECHO) startus.pol					>> $(OBJ_DIR)/phone.def
	@$(ECHO) blazer.pol					>> $(OBJ_DIR)/phone.def
	@$(ECHO) phone.ppl  					>> $(OBJ_DIR)/phone.def
	@$(ECHO) detailmatch:					>> $(OBJ_DIR)/phone.def
	@$(ECHO) vocabs/1_main.bnf			        >> $(OBJ_DIR)/phone.def
	@$(ECHO) vocabs/2_names.bnf		        	>> $(OBJ_DIR)/phone.def
	@$(ECHO) vocabs/3_options.bnf				>> $(OBJ_DIR)/phone.def
	@$(ECHO) vocabs/4_yesno.bnf		        	>> $(OBJ_DIR)/phone.def
	@$(ECHO) fastmatch:					>> $(OBJ_DIR)/phone.def
	@$(ECHO) vocabs/500fm.bnf				>> $(OBJ_DIR)/phone.def
	$(SDK_BIN)/bldvocab $(OBJ_DIR)/phone.def

# vocabs.bin
# Collection containing the vocabulary sets used by the application.
$(EXE_DIR)/vocabs.bin: $(OBJ_DIR)/phone_voc.le
	@$(ECHO) .
	@$(ECHO) "Generate vocabulary set bin file (collection)."
	@$(ECHO) .
	@$(ECHO) inputfiles= $(OBJ_DIR)/phone_voc.le:1000	 > $(OBJ_DIR)/vocabs.def
	@$(ECHO) outputdir=$(EXE_DIR)				>> $(OBJ_DIR)/vocabs.def
	@$(ECHO) outputname=vocabs.bin				>> $(OBJ_DIR)/vocabs.def
	$(SDK_BIN)/bldrom $(OBJ_DIR)/vocabs.def

# audprmts.bin
# Collection containing PCM for the audio prompts.
$(EXE_DIR)/audprmts.bin: $(DT0SW) $(DT1SW) $(DT2SW) $(DT3SW)  	\
			 $(DT4SW) $(DT5SW) $(DT6SW) $(DT7SW)	\
			 $(DT8SW) $(DT9SW) $(DTSTARTSW)		\
			 $(DTPOUNDSW) $(READYSW)
	@$(ECHO) .
	@$(ECHO) Convert .wav files to .pcm files.
	@$(ECHO) .
	@$(ECHO) Genernate BLDPCM .def file.
	@$(ECHO) .
	@$(ECHO) inputfiles:		> $(OBJ_DIR)/wavtopcm.def
	@$(ECHO) $(DT0SW)		>> $(OBJ_DIR)/wavtopcm.def
	@$(ECHO) $(DT1SW)		>> $(OBJ_DIR)/wavtopcm.def
	@$(ECHO) $(DT2SW)		>> $(OBJ_DIR)/wavtopcm.def
	@$(ECHO) $(DT3SW)		>> $(OBJ_DIR)/wavtopcm.def
	@$(ECHO) $(DT4SW)		>> $(OBJ_DIR)/wavtopcm.def
	@$(ECHO) $(DT5SW)		>> $(OBJ_DIR)/wavtopcm.def
	@$(ECHO) $(DT6SW)		>> $(OBJ_DIR)/wavtopcm.def
	@$(ECHO) $(DT7SW)		>> $(OBJ_DIR)/wavtopcm.def
	@$(ECHO) $(DT8SW)		>> $(OBJ_DIR)/wavtopcm.def
	@$(ECHO) $(DT9SW)		>> $(OBJ_DIR)/wavtopcm.def
	@$(ECHO) $(DTSTARSW)		>> $(OBJ_DIR)/wavtopcm.def
	@$(ECHO) $(DTPOUNDSW)		>> $(OBJ_DIR)/wavtopcm.def
	@$(ECHO) $(READYSW)		>> $(OBJ_DIR)/wavtopcm.def
	@$(ECHO).			>> $(OBJ_DIR)/wavtopcm.def
	@$(ECHO) outputdir=$(OBJ_DIR)    >> $(OBJ_DIR)/wavtopcm.def
	$(SDK_BIN)/bldpcm $(OBJ_DIR)/wavtopcm.def
	@$(ECHO) .
	@$(ECHO) Generate audprmts .bin file.
	@$(ECHO) .
	@$(ECHO) inputfiles:                >$(OBJ_DIR)/audprmts.def
	@$(ECHO) $(DT0TP):100              >>$(OBJ_DIR)/audprmts.def
	@$(ECHO) $(DT1TP):101              >>$(OBJ_DIR)/audprmts.def
	@$(ECHO) $(DT2TP):102              >>$(OBJ_DIR)/audprmts.def
	@$(ECHO) $(DT3TP):103              >>$(OBJ_DIR)/audprmts.def
	@$(ECHO) $(DT4TP):104              >>$(OBJ_DIR)/audprmts.def
	@$(ECHO) $(DT5TP):105              >>$(OBJ_DIR)/audprmts.def
	@$(ECHO) $(DT6TP):106              >>$(OBJ_DIR)/audprmts.def
	@$(ECHO) $(DT7TP):107              >>$(OBJ_DIR)/audprmts.def
	@$(ECHO) $(DT8TP):108              >>$(OBJ_DIR)/audprmts.def
	@$(ECHO) $(DT9TP):109              >>$(OBJ_DIR)/audprmts.def
	@$(ECHO) $(DTSTARTP):110           >>$(OBJ_DIR)/audprmts.def
	@$(ECHO) $(DTPOUNDTP):111          >>$(OBJ_DIR)/audprmts.def
	@$(ECHO) $(READYTP):1000           >>$(OBJ_DIR)/audprmts.def
	@$(ECHO).                  >>$(OBJ_DIR)/audprmts.def
	@$(ECHO) outputdir=$(EXE_DIR)    >> $(OBJ_DIR)/audprmts.def
	@$(ECHO) outputname=audprmts.bin >> $(OBJ_DIR)/audprmts.def
	$(SDK_BIN)/bldrom   $(OBJ_DIR)/audprmts.def

# pools.bin
# Collection containing the system supplied pools.  The data is retrieved
# from the SDK tree and packaged into a bin file that can be located in the
# devices ROM.
$(EXE_DIR)/pools.bin:
	@$(ECHO) .
	@$(ECHO) Generate pools bin file.
	@$(ECHO) .
	@$(ECHO) inputfiles: > $(OBJ_DIR)/pools.def
	@$(ECHO) $(POOL1) >> $(OBJ_DIR)/pools.def
	@$(ECHO) $(POOL2) >> $(OBJ_DIR)/pools.def
	@$(ECHO) $(POOL3) >> $(OBJ_DIR)/pools.def
	@$(ECHO). >> $(OBJ_DIR)/pools.def
	@$(ECHO) outputdir=$(EXE_DIR) >> $(OBJ_DIR)/pools.def
	@$(ECHO) outputname=pools.bin >> $(OBJ_DIR)/pools.def
	$(SDK_BIN)/bldrom   $(OBJ_DIR)/pools.def

# ctts.bin
# Collection containing the system supplied CTTS voices. The data is retrieved
# from the SDK tree and packaged into a bin file that can be located in the
# devices ROM.
$(EXE_DIR)/ctts.bin:
	@$(ECHO) .
	@$(ECHO) Generate CTTS bin file.
	@$(ECHO) .
	@$(ECHO) inputfiles: > $(OBJ_DIR)/ctts.def
	@$(ECHO) $(CTTS1) >> $(OBJ_DIR)/ctts.def
	@$(ECHO). >> $(OBJ_DIR)/ctts.def
	@$(ECHO) outputdir=$(EXE_DIR) >> $(OBJ_DIR)/ctts.def
	@$(ECHO) outputname=ctts.bin >> $(OBJ_DIR)/ctts.def
	$(SDK_BIN)/bldrom   $(OBJ_DIR)/ctts.def

# phone.exe
# This rule will link the objects into the executable image.
$(EXE_DIR)/phone$(EXE_EXT): $(C_OBJS)
	@$(ECHO) .
	@$(ECHO) Linking application.
	$(GEN_EXEC) $(GEN_EXEC_OUTPUT_FLAG)$@ $(GEN_EXEC_FLAGS) \
		    $(OS_LIBS) $(OBJECTS) $(SDK_LIBS) $(C_OBJS)
	@$(ECHO) Link Complete
	@$(ECHO) .
	@$(ECHO) Copy TTS engine to EXE directory.
	$(COPY) $(SDK_LIB_COMMON)/ecienus.syn $(EXE_DIR)
	$(COPY) $(SDK_LIB_COMMON)/ecictts.dll $(EXE_DIR)
	@$(ECHO) .

# This rule will build the object files from the source files.
# The dependencies are setup so that if any of the source files change,
# all files will be rebuilt.
$(C_OBJS): %.$(OBJ_EXT):$(C_SRCS) $(C_HDRS)
	@@$(ECHO) .
	@@$(ECHO) C Compile:
	@@$(ECHO) "Source: $(patsubst %.$(OBJ_EXT),%.c,$(subst $(OBJ_DIR),src,$@))"
	@@$(ECHO) "Object: $@"
	-$(RM) -f $@
	$(CC) $(CFLAGS) $(COBJ_NAME_FLAG)$(@) $(INC_DIRS) $(CCOMP_ONLY_FLAG) \
			 $(patsubst %.$(OBJ_EXT),%.c,$(subst $(OBJ_DIR),src,$@))
	@@$(ECHO) "$@ Compilation complete."
	@@$(ECHO) .

