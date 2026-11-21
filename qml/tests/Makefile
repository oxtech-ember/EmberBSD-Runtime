# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.

CC?=clang
OUT?=/tmp/eqml-test
CFLAGS?=-O2
WARNINGS=-std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow
ROOT=..

.PHONY: test clean

test: ${OUT}/test_eqml ${OUT}/test_input ${OUT}/test_animation ${OUT}/test_damage
	${OUT}/test_eqml
	${OUT}/test_input
	${OUT}/test_animation
	${OUT}/test_damage

${OUT}/test_eqml: test_eqml.c ${ROOT}/eqml.c ${ROOT}/eqml.h ${ROOT}/eqml_font8x8.h
	mkdir -p ${OUT}
	${CC} ${CFLAGS} ${WARNINGS} -I${ROOT} ${ROOT}/eqml.c test_eqml.c -o $@

${OUT}/test_input: test_input.c ${ROOT}/eqml_input.c ${ROOT}/eqml_input.h ${ROOT}/eqml.h
	mkdir -p ${OUT}
	${CC} ${CFLAGS} ${WARNINGS} -I${ROOT} ${ROOT}/eqml_input.c test_input.c -o $@

${OUT}/test_animation: test_animation.c ${ROOT}/eqml_input.c ${ROOT}/eqml_input.h ${ROOT}/eqml.h
	mkdir -p ${OUT}
	${CC} ${CFLAGS} ${WARNINGS} -I${ROOT} ${ROOT}/eqml_input.c test_animation.c -o $@

clean:
	rm -f ${OUT}/test_eqml ${OUT}/test_input ${OUT}/test_animation ${OUT}/test_damage

${OUT}/test_damage: test_damage.c ${ROOT}/eqml.c ${ROOT}/eqml_input.c ${ROOT}/eqml.h ${ROOT}/eqml_input.h ${ROOT}/eqml_font8x8.h
	mkdir -p ${OUT}
	${CC} ${CFLAGS} ${WARNINGS} -I${ROOT} ${ROOT}/eqml.c ${ROOT}/eqml_input.c test_damage.c -o $@
