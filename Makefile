#!/bin/sh

OUT = fpv
SRC = cam.c fpv.c input.c osd.c stb_image.c telem.c

DEP = $(SRC:.c=.d)
OBJ = $(SRC:.c=.o)

CC = gcc
CFLAGS = -MMD -MP -Ofast
LDFLAGS = -lbcm2835 -lbcm_host -lEGL -lGLESv2 -lm -lmmal_core -lmmal_util -lmmal_vc_client

all: fpv

clean:
	rm -f $(DEP) $(OBJ) $(OUT)

fpv: $(OBJ)
	gcc -o $(OUT) $^ $(LDFLAGS)

-include $(DEP)
