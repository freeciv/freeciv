#ifndef __DECLGATE_H__
#define __DECLGATE_H__

/* Transparent MorphOS gate function macros

	Written by Harry "Piru" Sintonen, 2000-2001.
	Public Domain.
*/

#ifdef __MORPHOS__
#  include <exec/types.h>
#  include <emul/emulregs.h>
#  include <emul/emulinterface.h>

#  define REG_a0 REG_A0
#  define REG_a1 REG_A1
#  define REG_a2 REG_A2
#  define REG_a3 REG_A3
#  define REG_a4 REG_A4
#  define REG_a5 REG_A5
#  define REG_a6 REG_A6
#  define REG_a7 REG_A7
#  define REG_d0 REG_D0
#  define REG_d1 REG_D1
#  define REG_d2 REG_D2
#  define REG_d3 REG_D3
#  define REG_d4 REG_D4
#  define REG_d5 REG_D5
#  define REG_d6 REG_D6
#  define REG_d7 REG_D7

#define DECLGATE(t, fn, rt) \
t struct EmulLibEntry fn = {TRAP_ ## rt, 0, (void (*)(void)) _ ## fn};
#define DECLFUNC_1(fn, r1, t1, n1) _ ## fn(void)
#define DECLFUNC_2(fn, r1, t1, n1, r2, t2, n2) _ ## fn(void)
#define DECLFUNC_3(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3) _ ## fn(void)
#define DECLFUNC_4(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4) _ ## fn(void)
#define DECLFUNC_5(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5) _ ## fn(void)
#define DECLFUNC_6(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6) _ ## fn(void)
#define DECLFUNC_7(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6, r7, t7, n7) _ ## fn(void)
#define DECLFUNC_8(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6, r7, t7, n7, r8, t8, n8) _ ## fn(void)
#define DECLFUNC_9(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6, r7, t7, n7, r8, t8, n8, r9, t9, n9) _ ## fn(void)
#define DECLFUNC_10(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6, r7, t7, n7, r8, t8, n8, r9, t9, n9, r10, t10, n10) _ ## fn(void)

#define NATDECLGATE(t, fn, rt)
#define NATDECLFUNC_1(fn, r1, t1, n1) fn(void)
#define NATDECLFUNC_2(fn, r1, t1, n1, r2, t2, n2) fn(void)
#define NATDECLFUNC_3(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3) fn(void)
#define NATDECLFUNC_4(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4) fn(void)
#define NATDECLFUNC_5(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5) fn(void)
#define NATDECLFUNC_6(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6) fn(void)
#define NATDECLFUNC_7(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6, r7, t7, n7) fn(void)
#define NATDECLFUNC_8(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6, r7, t7, n7, r8, t8, n8) fn(void)
#define NATDECLFUNC_9(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6, r7, t7, n7, r8, t8, n8, r9, t9, n9) fn(void)
#define NATDECLFUNC_10(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6, r7, t7, n7, r8, t8, n8, r9, t9, n9, r10, t10, n10) fn(void)

#define DECLARG_1(r1, t1, n1) \
  t1 n1 = (t1) REG_ ## r1;

#define DECLARG_2(r1, t1, n1, r2, t2, n2) \
  t1 n1 = (t1) REG_ ## r1; \
  t2 n2 = (t2) REG_ ## r2;

#define DECLARG_3(r1, t1, n1, r2, t2, n2, r3, t3, n3) \
  t1 n1 = (t1) REG_ ## r1; \
  t2 n2 = (t2) REG_ ## r2; \
  t3 n3 = (t3) REG_ ## r3;

#define DECLARG_4(r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4) \
  t1 n1 = (t1) REG_ ## r1; \
  t2 n2 = (t2) REG_ ## r2; \
  t3 n3 = (t3) REG_ ## r3; \
  t4 n4 = (t4) REG_ ## r4;

#define DECLARG_5(r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5) \
  t1 n1 = (t1) REG_ ## r1; \
  t2 n2 = (t2) REG_ ## r2; \
  t3 n3 = (t3) REG_ ## r3; \
  t4 n4 = (t4) REG_ ## r4; \
  t5 n5 = (t5) REG_ ## r5;

#define DECLARG_6(r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6) \
  t1 n1 = (t1) REG_ ## r1; \
  t2 n2 = (t2) REG_ ## r2; \
  t3 n3 = (t3) REG_ ## r3; \
  t4 n4 = (t4) REG_ ## r4; \
  t5 n5 = (t5) REG_ ## r5; \
  t6 n6 = (t6) REG_ ## r6;

#define DECLARG_7(r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6, r7, t7, n7) \
  t1 n1 = (t1) REG_ ## r1; \
  t2 n2 = (t2) REG_ ## r2; \
  t3 n3 = (t3) REG_ ## r3; \
  t4 n4 = (t4) REG_ ## r4; \
  t5 n5 = (t5) REG_ ## r5; \
  t6 n6 = (t6) REG_ ## r6; \
  t7 n7 = (t7) REG_ ## r7;

#define DECLARG_8(r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6, r7, t7, n7, r8, t8, n8) \
  t1 n1 = (t1) REG_ ## r1; \
  t2 n2 = (t2) REG_ ## r2; \
  t3 n3 = (t3) REG_ ## r3; \
  t4 n4 = (t4) REG_ ## r4; \
  t5 n5 = (t5) REG_ ## r5; \
  t6 n6 = (t6) REG_ ## r6; \
  t7 n7 = (t7) REG_ ## r7; \
  t8 n8 = (t8) REG_ ## r8;

#define DECLARG_9(r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6, r7, t7, n7, r8, t8, n8, r9, t9, n9) \
  t1 n1 = (t1) REG_ ## r1; \
  t2 n2 = (t2) REG_ ## r2; \
  t3 n3 = (t3) REG_ ## r3; \
  t4 n4 = (t4) REG_ ## r4; \
  t5 n5 = (t5) REG_ ## r5; \
  t6 n6 = (t6) REG_ ## r6; \
  t7 n7 = (t7) REG_ ## r7; \
  t8 n8 = (t8) REG_ ## r8; \
  t9 n9 = (t9) REG_ ## r9;

#define DECLARG_10(r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6, r7, t7, n7, r8, t8, n8, r9, t9, n9, r10, t10, n10) \
  t1  n1  = (t1)  REG_ ## r1; \
  t2  n2  = (t2)  REG_ ## r2; \
  t3  n3  = (t3)  REG_ ## r3; \
  t4  n4  = (t4)  REG_ ## r4; \
  t5  n5  = (t5)  REG_ ## r5; \
  t6  n6  = (t6)  REG_ ## r6; \
  t7  n7  = (t7)  REG_ ## r7; \
  t8  n8  = (t8)  REG_ ## r8; \
  t9  n9  = (t9)  REG_ ## r9; \
  t10 n10 = (t10) REG_ ## r10;

#else /* __MORPHOS__ */

#include "myregargs.h"

#define DECLARG_1(r1, t1, n1)
#define DECLARG_2(r1, t1, n1, r2, t2, n2)
#define DECLARG_3(r1, t1, n1, r2, t2, n2, r3, t3, n3)
#define DECLARG_4(r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4)
#define DECLARG_5(r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5)
#define DECLARG_6(r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6)
#define DECLARG_7(r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6, r7, t7, n7)
#define DECLARG_8(r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6, r7, t7, n7, r8, t8, n8)
#define DECLARG_9(r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6, r7, t7, n7, r8, t8, n8, r9, t9, n9)
#define DECLARG_10(r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6, r7, t7, n7, r8, t8, n8, r9, t9, n9, r10, t10, n10)

#define DECLGATE(t, fn, rt)

#define DECLFUNC_1(fn, r1, t1, n1) \
  MASM fn(MREG(r1, t1 n1))
#define DECLFUNC_2(fn, r1, t1, n1, r2, t2, n2) \
  MASM fn(MREG(r1, t1 n1), MREG(r2, t2 n2))
#define DECLFUNC_3(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3) \
  MASM fn(MREG(r1, t1 n1), MREG(r2, t2 n2), MREG(r3, t3 n3))
#define DECLFUNC_4(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4) \
  MASM fn(MREG(r1, t1 n1), MREG(r2, t2 n2), MREG(r3, t3 n3), MREG(r4, t4 n4))
#define DECLFUNC_5(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5) \
  MASM fn(MREG(r1, t1 n1), MREG(r2, t2 n2), MREG(r3, t3 n3), MREG(r4, t4 n4), MREG(r5, t5 n5))
#define DECLFUNC_6(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6) \
  MASM fn(MREG(r1, t1 n1), MREG(r2, t2 n2), MREG(r3, t3 n3), MREG(r4, t4 n4), MREG(r5, t5 n5), MREG(r6, t6 n6))
#define DECLFUNC_7(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6, r7, t7, n7) \
  MASM fn(MREG(r1, t1 n1), MREG(r2, t2 n2), MREG(r3, t3 n3), MREG(r4, t4 n4), MREG(r5, t5 n5), MREG(r6, t6 n6), MREG(r7, t7 n7))
#define DECLFUNC_8(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6, r7, t7, n7, r8, t8, n8) \
  MASM fn(MREG(r1, t1 n1), MREG(r2, t2 n2), MREG(r3, t3 n3), MREG(r4, t4 n4), MREG(r5, t5 n5), MREG(r6, t6 n6), MREG(r7, t7 n7), MREG(r8, t8 n8))
#define DECLFUNC_9(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6, r7, t7, n7, r8, t8, n8, r9, t9, n9) \
  MASM fn(MREG(r1, t1 n1), MREG(r2, t2 n2), MREG(r3, t3 n3), MREG(r4, t4 n4), MREG(r5, t5 n5), MREG(r6, t6 n6), MREG(r7, t7 n7), MREG(r8, t8 n8), MREG(r9, t9 n9))
#define DECLFUNC_10(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6, r7, t7, n7, r8, t8, n8, r9, t9, n9, r10, t10, n10) \
  MASM fn(MREG(r1, t1 n1), MREG(r2, t2 n2), MREG(r3, t3 n3), MREG(r4, t4 n4), MREG(r5, t5 n5), MREG(r6, t6 n6), MREG(r7, t7 n7), MREG(r8, t8 n8), MREG(r9, t9 n9), MREG(r10, t10 n10))

#define NATDECLGATE(t, fn, rt)

#define NATDECLFUNC_1(fn, r1, t1, n1) \
  DECLFUNC_1(fn, r1, t1, n1)
#define NATDECLFUNC_2(fn, r1, t1, n1, r2, t2, n2) \
  DECLFUNC_2(fn, r1, t1, n1, r2, t2, n2)
#define NATDECLFUNC_3(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3) \
  DECLFUNC_3(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3)
#define NATDECLFUNC_4(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4) \
  DECLFUNC_4(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4)
#define NATDECLFUNC_5(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5) \
  DECLFUNC_5(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5)
#define NATDECLFUNC_6(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6) \
  DECLFUNC_6(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6)
#define NATDECLFUNC_7(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6, r7, t7, n7) \
  DECLFUNC_7(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6, r7, t7, n7)
#define NATDECLFUNC_8(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6, r7, t7, n7, r8, t8, n8) \
  DECLFUNC_8(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6, r7, t7, n7, r8, t8, n8)
#define NATDECLFUNC_9(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6, r7, t7, n7, r8, t8, n8, r9, t9, n9) \
  DECLFUNC_9(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6, r7, t7, n7, r8, t8, n8, r9, t9, n9)
#define NATDECLFUNC_10(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6, r7, t7, n7, r8, t8, n8, r9, t9, n9, r10, t10, n10) \
  DECLFUNC_10(fn, r1, t1, n1, r2, t2, n2, r3, t3, n3, r4, t4, n4, r5, t5, n5, r6, t6, n6, r7, t7, n7, r8, t8, n8, r9, t9, n9, r10, t10, n10)

#endif /* __MORPHOS__ */

#endif /* __DECLGATE_H__ */
