#ifndef __MYREGARGS_H__
#define __MYREGARGS_H__

/* SAS/C, GCC, VBCC, MaxonC and StormC compatible regargs

   Written by Harry "Piru" Sintonen, Feb 2000. Updated Nov 2001.
   Public Domain.
*/

#ifdef MASM
#  undef MASM
#endif
#ifdef MREG
#  undef MREG
#endif

#if defined(__SASC)
#  define MASM __asm
#  define MREG(reg,type) register __##reg type
#elif defined(__GNUC__)
#  define MASM
#  define MREG(reg,type) type __asm(#reg)
#elif defined(__VBCC__)
#  define MASM
#  define MREG(reg,type) __reg(#reg) type
#elif defined(__MAXON__) | defined(__STORM__)
#  define MASM
#  define MREG(reg,type) register __##reg type
#else
#  error Unknown compiler, SAS/C, GCC, VBCC, MaxonC and StormC supported
#endif

#endif /* __MYREGARGS_H__ */
