/**
 * The Unicode tables `--grep` needs to answer exactly what CPython answers.
 *
 * (One of them, `PRINTABLE_RANGES`, serves the *refusal* rather than the
 * search: `no mirror line matches …` echoes the pattern through `repr()`, and
 * `repr` escapes exactly the code points `str.isprintable()` calls unprintable.)
 *
 * Generated from this machine's CPython (3.14) and pinned here rather than
 * derived from JavaScript's own tables, because the two disagree in places that
 * change a search's answer:
 *
 * * `str.casefold()` is not `String.prototype.toLowerCase()`.  Full folds
 *   expand (`ß` → `ss`, `ﬀ` → `ff`), a few common folds differ (`ſ` → `s`,
 *   `ς` → `σ`, `µ` → `μ`) and Cherokee folds *up* while JavaScript lowers it
 *   down.  `FOLD_SOURCE` is exactly the set of code points where the two
 *   differ — 297 of them — so `casefold` is `toLowerCase` plus a lookup.
 * * `re`'s `\w`, `\s` and `\d` on a `str` pattern are Unicode-wide and do not
 *   line up with JavaScript's: `\d` is `Nd` where JavaScript's is `[0-9]`,
 *   `\w` follows `str.isalnum()`, and `\s` includes `\x1c`–`\x1f` which
 *   `\p{White_Space}` does not.  The three range tables are CPython's answers,
 *   enumerated over every code point.
 * * `re.IGNORECASE` folds by `unicode_tolower` plus `re._casefix._EXTRA_CASES`,
 *   which agrees with JavaScript's `iu` canonicalisation everywhere except
 *   `ı` (U+0131), which CPython matches against `i` and `I`.  Rather than run
 *   with the `i` flag and carry that hole — and lose the ability to honour a
 *   scoped `(?-i:…)` at all — the compiler expands each literal into its
 *   equivalence class, and `CASE_CLASS_SOURCE` is that partition, measured
 *   pairwise against CPython.
 *
 * The encodings are base-36 so the tables cost ~27 KB of source rather than the
 * ~5 MB a naive dump would, and they decode once at module load.
 */

const decodeInt = (token: string): number => Number.parseInt(token, 36);

/** An inclusive code point range, the form every class in here is stored in. */
export type CodeRange = readonly [number, number];

/** `"gap,width gap,width …"`, each gap measured from the previous range's end. */
const decodeRanges = (source: string): ReadonlyArray<CodeRange> => {
  let previous = -1;
  return source.split(' ').map((token) => {
    const [gap = '0', width = '0'] = token.split(',');
    const low = previous + decodeInt(gap) + 1;
    const high = low + decodeInt(width);
    previous = high;
    return [low, high] as const;
  });
};

const decodeFold = (source: string): ReadonlyMap<number, string> =>
  new Map(
    source.split(' ').map((token) => {
      const [key = '0', value = ''] = token.split(':');
      return [
        decodeInt(key),
        value
          .split('.')
          .map((code) => String.fromCodePoint(decodeInt(code)))
          .join(''),
      ] as const;
    })
  );

const decodeClasses = (source: string): ReadonlyMap<number, ReadonlyArray<number>> => {
  const table = new Map<number, ReadonlyArray<number>>();
  for (const token of source.split(' ')) {
    const members = token.split('.').map(decodeInt);
    for (const member of members) table.set(member, members);
  }
  return table;
};

const decodeNames = (source: string): ReadonlyMap<string, number> =>
  new Map(
    source.split('|').map((token) => {
      const [name = '', code = '0'] = token.split('=');
      return [name, decodeInt(code)] as const;
    })
  );
const FOLD_SOURCE =
  '51:qk 67:37.37 95:jg.32 an:37 ds:2y.lo n9:qh pc:qh.lk.ld q8:qt.lk.ld qq:qr r4:qa r5:qg r9:qu ra:' +
  'qo s0:qi s1:qp s5:qd 13b:12d.136 3vk:3vk 3vl:3vl 3vm:3vm 3vn:3vn 3vo:3vo 3vp:3vp 3vq:3vq 3vr:3vr' +
  ' 3vs:3vs 3vt:3vt 3vu:3vu 3vv:3vv 3vw:3vw 3vx:3vx 3vy:3vy 3vz:3vz 3w0:3w0 3w1:3w1 3w2:3w2 3w3:3w3' +
  ' 3w4:3w4 3w5:3w5 3w6:3w6 3w7:3w7 3w8:3w8 3w9:3w9 3wa:3wa 3wb:3wb 3wc:3wc 3wd:3wd 3we:3we 3wf:3wf' +
  ' 3wg:3wg 3wh:3wh 3wi:3wi 3wj:3wj 3wk:3wk 3wl:3wl 3wm:3wm 3wn:3wn 3wo:3wo 3wp:3wp 3wq:3wq 3wr:3wr' +
  ' 3ws:3ws 3wt:3wt 3wu:3wu 3wv:3wv 3ww:3ww 3wx:3wx 3wy:3wy 3wz:3wz 3x0:3x0 3x1:3x1 3x2:3x2 3x3:3x3' +
  ' 3x4:3x4 3x5:3x5 3x6:3x6 3x7:3x7 3x8:3x8 3x9:3x9 3xa:3xa 3xb:3xb 3xc:3xc 3xd:3xd 3xe:3xe 3xf:3xf' +
  ' 3xg:3xg 3xh:3xh 3xi:3xi 3xj:3xj 3xk:3xk 3xl:3xl 3xm:3xm 3xn:3xn 3xo:3xo 3xp:3xp 3xq:3xq 3xr:3xr' +
  ' 3xs:3xs 3xt:3xt 3xu:3xu 3xv:3xv 3xw:3xw 3xx:3xx 3y0:3xs 3y1:3xt 3y2:3xu 3y3:3xv 3y4:3xw 3y5:3xx' +
  ' 5mo:tu 5mp:tw 5mq:u6 5mr:u9 5ms:ua 5mt:ua 5mu:ui 5mv:v7 5mw:wuj 61i:2w.mp 61j:38.lk 61k:3b.lm 6' +
  '1l:3d.lm 61m:2p.ji 61n:601 61q:37.37 66o:qt.lv 66q:qt.lv.lc 66s:qt.lv.ld 66u:qt.lv.n6 680:64g.qh' +
  ' 681:64h.qh 682:64i.qh 683:64j.qh 684:64k.qh 685:64l.qh 686:64m.qh 687:64n.qh 688:64g.qh 689:64h' +
  '.qh 68a:64i.qh 68b:64j.qh 68c:64k.qh 68d:64l.qh 68e:64m.qh 68f:64n.qh 68g:65c.qh 68h:65d.qh 68i:' +
  '65e.qh 68j:65f.qh 68k:65g.qh 68l:65h.qh 68m:65i.qh 68n:65j.qh 68o:65c.qh 68p:65d.qh 68q:65e.qh 6' +
  '8r:65f.qh 68s:65g.qh 68t:65h.qh 68u:65i.qh 68v:65j.qh 68w:674.qh 68x:675.qh 68y:676.qh 68z:677.q' +
  'h 690:678.qh 691:679.qh 692:67a.qh 693:67b.qh 694:674.qh 695:675.qh 696:676.qh 697:677.qh 698:67' +
  '8.qh 699:679.qh 69a:67a.qh 69b:67b.qh 69e:67k.qh 69f:q9.qh 69g:q4.qh 69i:q9.n6 69j:q9.n6.qh 69o:' +
  'q9.qh 69q:qh 69u:67o.qh 69v:qf.qh 69w:q6.qh 69y:qf.n6 69z:qf.n6.qh 6a4:qf.qh 6aa:qh.lk.lc 6ab:qh' +
  '.lk.ld 6ae:qh.n6 6af:qh.lk.n6 6aq:qt.lk.lc 6ar:qt.lk.ld 6as:qp.lv 6au:qt.n6 6av:qt.lk.n6 6b6:67w' +
  '.qh 6b7:qx.qh 6b8:r2.qh 6ba:qx.n6 6bb:qx.n6.qh 6bg:qx.qh xv4:3vk xv5:3vl xv6:3vm xv7:3vn xv8:3vo' +
  ' xv9:3vp xva:3vq xvb:3vr xvc:3vs xvd:3vt xve:3vu xvf:3vv xvg:3vw xvh:3vx xvi:3vy xvj:3vz xvk:3w0' +
  ' xvl:3w1 xvm:3w2 xvn:3w3 xvo:3w4 xvp:3w5 xvq:3w6 xvr:3w7 xvs:3w8 xvt:3w9 xvu:3wa xvv:3wb xvw:3wc' +
  ' xvx:3wd xvy:3we xvz:3wf xw0:3wg xw1:3wh xw2:3wi xw3:3wj xw4:3wk xw5:3wl xw6:3wm xw7:3wn xw8:3wo' +
  ' xw9:3wp xwa:3wq xwb:3wr xwc:3ws xwd:3wt xwe:3wu xwf:3wv xwg:3ww xwh:3wx xwi:3wy xwj:3wz xwk:3x0' +
  ' xwl:3x1 xwm:3x2 xwn:3x3 xwo:3x4 xwp:3x5 xwq:3x6 xwr:3x7 xws:3x8 xwt:3x9 xwu:3xa xwv:3xb xww:3xc' +
  ' xwx:3xd xwy:3xe xwz:3xf xx0:3xg xx1:3xh xx2:3xi xx3:3xj xx4:3xk xx5:3xl xx6:3xm xx7:3xn xx8:3xo' +
  ' xx9:3xp xxa:3xq xxb:3xr 1dkw:2u.2u 1dkx:2u.2x 1dky:2u.30 1dkz:2u.2u.2x 1dl0:2u.2u.30 1dl1:37.38' +
  ' 1dl2:37.38 1dlf:12s.12u 1dlg:12s.12d 1dlh:12s.12j 1dli:132.12u 1dlj:12s.12l';

const WORD_SOURCE =
  '1c,9 7,p 4,0 1,p 1b,0 7,1 1,0 3,1 1,2 1,m 1,u 1,cp 4,b e,4 7,0 1,0 3l,4 1,1 2,3 1,0 6,0 1,2 1,0 ' +
  '1,j 1,2a 1,3u 8,4l 1,11 2,0 6,14 1z,q 4,3 19,16 l,9 4,1 1,2q 1,0 f,1 7,e 2,0 g,0 1,t t,2g b,0 e,' +
  '16 9,1 4,0 5,l 4,0 9,0 3,0 n,o 7,a 5,n 1,5 h,15 1m,1h 3,0 i,0 7,9 4,9 1,f 4,7 2,1 2,l 1,6 1,0 3,' +
  '3 3,0 g,0 d,1 1,2 4,b 2,5 2,0 8,5 4,1 2,l 1,6 1,1 1,1 1,1 v,3 1,0 7,9 2,2 g,8 1,2 1,l 1,6 1,1 1,' +
  '4 3,0 i,0 f,1 4,9 9,0 b,7 2,1 2,l 1,6 1,1 1,4 3,0 u,1 1,2 4,9 1,6 b,0 1,5 3,2 1,3 3,1 1,0 1,1 3,' +
  '1 3,2 3,b m,0 l,c i,7 1,2 1,m 1,f 3,0 q,2 2,0 2,1 4,9 8,6 1,0 4,7 1,2 1,m 1,9 1,4 3,0 v,1 1,1 4,' +
  '9 1,1 h,8 1,2 1,14 2,0 g,0 5,2 1,9 4,i 1,5 5,h 3,n 1,8 1,0 2,6 v,9 h,1b 1,1 c,6 9,9 13,1 1,0 1,4' +
  ' 1,n 1,0 1,9 1,1 9,0 2,4 1,0 9,9 2,3 w,0 v,j c,7 1,z r,4 37,16 k,a 6,5 4,3 3,0 3,1 7,2 4,c c,0 1' +
  ',9 6,11 1,0 5,0 2,16 1,98 1,3 2,6 1,0 1,3 2,14 1,3 2,w 1,3 2,6 1,0 1,3 2,e 1,1k 1,3 2,1u e,j 3,f' +
  ' g,2d 2,5 3,h7 2,g 1,p 5,22 3,a 7,h d,i e,h e,c 1,2 f,1f z,0 4,0 3,9 6,9 m,9 6,2g 7,4 2,x 1,0 5,' +
  '1x a,u 13,13 2,4 b,17 4,p 6,a 11,m 9,1g 17,9 6,9 d,0 2l,1a h,7 3,9 15,t d,1j q,z s,9 3,1c 2,a 5,' +
  '16 2,2 15,3 1,5 1,1 3,0 5,5b 1s,7p 2,5 2,11 2,5 2,7 1,0 1,0 1,0 1,u 2,1g 1,6 1,0 3,2 1,6 3,3 2,5' +
  ' 4,c 5,2 1,6 37,1 2,5 5,a 6,c 2t,0 4,0 2,9 1,0 3,4 6,0 1,0 1,0 1,3 1,a 2,3 5,4 4,0 1,1l k6,1n 26' +
  ',l hi,t vg,6c 6,3 3,1 9,0 2,11 1,0 5,0 2,1j 7,0 g,m 9,6 1,6 1,6 1,6 1,6 1,6 1,6 1,6 28,0 d1,2 p,' +
  '8 7,4 2,4 4,2d 6,2 1,2h 1,3 5,16 1,2l 3,3 a,v 1c,f w,9 u,7 1,e w,9 13,e 8w,533 1s,h3g 1v,19 2,7g' +
  ' 3,r k,1a g,u 2,27 13,8 2,2u 2,1u 2,1 1,0 1,7 l,f 1,2 1,3 1,m d,5 a,1f e,1d s,9 o,5 3,0 1,1 1,11' +
  ' a,m p,s 7,1a s,a 6,4 1,o 1,14 n,2 1,7 4,9 6,m 3,0 3,1d 1,0 3,1 2,4 2,0 1,0 o,2 2,a 7,2 c,5 2,5 ' +
  '2,5 9,6 1,6 1,16 1,d 6,36 d,9 6,8mb c,m 4,1c 6is,a5 2,2x 12,6 c,4 5,0 1,9 1,c 1,4 1,0 1,1 1,1 1,' +
  '2z x,a2 i,1r 2,1h 14,b 38,4 1,3q j,9 7,p 6,p b,2g 3,5 2,5 2,5 2,2 z,b 1,p 1,i 1,1 1,e 2,d y,3e c' +
  ',18 c,1k h,1 6s,s 3,1c g,q 4,z 9,t 5,11 a,t 2,z 4,7 1,4 16,4d 2,9 6,z 4,z 4,13 8,1f c,a 1,e 1,6 ' +
  '1,1 1,a 1,e 1,6 1,1 3,1f c,8m 9,l a,7 o,5 1,15 1,8 1x,5 2,0 1,17 1,1 3,0 2,m 2,u 2,11 8,8 1c,i 1' +
  ',1 5,w 4,p 1y,1j 4,j 2,1a f,3 1,2 1,s a,8 n,u 1,v w,7 1,r 6,4 g,1h a,l 2,q 5,p n,6 28,20 1j,1e d' +
  ',1e 7,15 c,9 6,11 9,m 62,u 1,15 6,1 g,2 1n,13 8,l b,3 r,h 1a,r k,m c,1g q,t 1,1 2,0 d,18 w,o 7,9' +
  ' 9,z f,9 4,0 2,0 8,y 3,0 c,1b e,3 b,a 1,0 4,j b,h 1,o j,1 1r,6 1,0 1,3 1,e 1,9 7,1a h,9 b,7 2,1 ' +
  '2,l 1,6 1,1 1,4 3,0 i,0 c,4 u,9 1,0 2,0 1,11 1,0 p,0 1,0 18,1g i,3 5,9 5,2 u,1b k,1 1,0 8,9 4m,1' +
  'a 15,3 10,1b k,0 b,9 12,16 d,0 7,9 6,j s,q l,b 4,6 55,17 38,2a c,7 2,0 2,7 1,1 1,n f,0 1,0 e,9 1' +
  'y,7 2,12 g,0 1,0 s,0 a,13 7,0 l,0 b,19 j,0 i,20 5j,w f,9 6,8 1,10 h,0 f,s 5,t 34,6 1,1 1,11 l,0 ' +
  '9,9 6,5 1,1 1,v e,0 7,9 8m,i f,0 1,c 1,x s,9 2e,0 f,k 17,pl 2u,32 h,5f 218,2o f,tr h,5 p,32y 5,g' +
  '6 5a1,t i,9 1c6,fs 7,u 1,9 6,26 1,9 6,t i,1b g,3 c,9 1,6 1,k 5,i c0,18 3,9 5i,2e 2x,22 5,0 1u,c ' +
  '1s,1 1,0 s,4qf 8,yd 15,9 6w7,3 1,6 1,1 1,82 f,0 t,2 2,0 e,3 8,az 1s4,2y 5,c 3,8 7,9 386,9 152,j ' +
  'c,j 30,o 3r,2c 1,1y 1,1 2,0 2,1 2,3 1,b 1,0 1,6 1,1s 1,3 2,7 1,6 1,r 1,3 1,4 1,0 3,6 1,9f 2,o 1,' +
  'o 1,u 1,o 1,u 1,o 1,u 1,o 1,u 1,o 1,7 2,1d 1ds,u 6,5 79,1p 42,18 a,6 2,9 4,0 8x,t i,17 4,9 d2,r ' +
  '4,9 5y,t 2,a dh,6 1,3 1,1 1,e 1,5g 2,8 1c,1v 7,0 4,9 lz,1m 1,2 1,3 24,18 1,e 5e,3 1,q 1,1 1,0 2,' +
  '0 1,9 1,3 1,0 1,0 6,0 4,0 1,0 1,0 1,2 1,1 1,0 2,0 1,0 1,0 1,0 1,0 1,1 1,0 2,3 1,6 1,3 1,3 1,0 1,' +
  '9 1,g 5,2 1,4 1,g g4,c 25f,9 sm,wyn w,37d 6,65 2,4g1 e,5rk f,h9 1wi,f1 15u,3t6 5,38f';

const SPACE_SOURCE =
  '9,4 e,4 2s,0 q,0 4bj,0 1vj,a t,1 5,0 1b,0 334,0';

const DIGIT_SOURCE =
  '1c,9 17q,9 3q,9 5i,9 bg,9 3a,9 3a,9 3a,9 3a,9 3a,9 3a,9 3a,9 3a,9 3a,9 2o,9 3a,9 1y,9 7q,9 1y,9 ' +
  '1fq,9 12,9 8c,9 3k,9 4m,9 6,9 52,9 2e,9 3q,9 6,9 r7q,9 iu,9 12,9 5i,9 m,9 2e,9 ba,9 geu,9 13a,9 ' +
  '1om,9 6,9 m4,9 3k,9 1o,9 40,9 7q,9 9i,9 3a,9 ae,9 2u,9 6,j 24,9 bq,9 2u,9 ie,9 2e,9 6u,9 1y,9 bq' +
  ',9 d06,9 1t2,9 2e,9 3q,9 eu,9 iuu,9 250,1d 1ts,9 bq,9 dy,9 6v,9 np,9 3o6,9';

/**
 * `str.isprintable()` — the code points `repr()` leaves alone.
 *
 * Everything else is escaped, and CPython's answer is a general-category test
 * (`Cc`, `Cf`, `Cs`, `Co`, `Cn`, `Zl`, `Zp` and every `Zs` but the space
 * itself are not printable), so it cannot be spelled with a JavaScript regex
 * property escape: `\p{Cn}` alone disagrees, and the space is the exception.
 * 736 runs, enumerated over every code point.
 */
const PRINTABLE_SOURCE =
  'w,2m y,b 1,jt 2,5 4,6 1,0 1,j 1,b0 1,11 2,1d 2,2 1,1i 8,q 4,5 h,l 1,5b 1,1b 2,1m 2,2s ' +
  'e,1m 2,1c 2,e 1,r 2,0 1,a 5,u 8,22 1,4g 1,7 2,1 2,l 1,6 1,0 3,3 2,8 2,1 2,3 8,0 4,1 1,4 ' +
  '2,o 2,2 1,5 4,1 2,l 1,6 1,1 1,1 1,1 2,0 1,4 4,1 2,2 3,0 7,3 1,0 7,g a,2 1,8 1,2 1,l 1,6 ' +
  '1,1 1,4 2,9 1,2 1,2 2,0 f,3 2,b 7,6 1,2 1,7 2,1 2,l 1,6 1,1 1,4 2,8 2,1 2,2 7,2 4,1 1,4 ' +
  '2,h a,1 1,5 3,2 1,3 3,1 1,0 1,1 3,1 3,2 3,b 4,4 3,2 1,3 2,0 6,0 e,k 5,c 1,2 1,m 1,f 2,8 ' +
  '1,2 1,3 7,1 1,2 2,0 2,3 2,9 7,l 1,2 1,m 1,9 1,4 2,8 1,2 1,3 7,1 6,1 1,3 2,9 1,2 c,c 1,2 ' +
  '1,1e 1,2 1,5 4,f 2,p 1,2 1,h 3,n 1,8 1,0 2,6 3,0 4,5 1,0 1,7 6,9 2,2 c,1l 4,s 11,1 1,0 ' +
  '1,4 1,n 1,0 1,m 2,4 1,0 1,6 1,9 2,3 w,1z 1,z 4,12 1,z 1,e 1,c 11,5h 1,0 5,0 2,ag 1,3 2,6 ' +
  '1,0 1,3 2,14 1,3 2,w 1,3 2,6 1,0 1,3 2,e 1,1k 1,3 2,1u 2,v 3,p 6,2d 2,5 2,hr 1,r 3,2g ' +
  '7,l 9,n 9,j c,c 1,2 1,1 c,2l 2,9 6,9 6,d 1,a 6,2g 7,16 5,1x a,u 1,b 4,b 4,0 3,15 2,4 ' +
  'b,17 4,p 6,a 3,1p 2,1s 1,s 2,a 6,9 6,d 2,u 1d,24 1,4l 8,1n 3,e 3,1p 5,16 2,a 8,16 5,et ' +
  '2,5 2,11 2,5 2,7 1,0 1,0 1,0 1,u 2,1g 1,e 1,d 2,5 1,i 2,2 1,8 h,n 8,1a h,1 2,q 1,c 3,w ' +
  'f,w f,3v 4,ih m,a l,1eb 2,v 1,9o 5,18 1,0 5,0 2,1j 7,1 e,n 9,6 1,6 1,6 1,6 1,6 1,6 1,6 ' +
  '1,6 1,3h y,p 1,2g c,5x q,f 1,1q 1,2d 2,2u 5,16 1,2l 1,2d 9,1b 1,mlo 3,1i 9,9n k,53 8,5p ' +
  '2,1 1,0 1,7 l,1m 3,9 6,1j 8,1x 8,b 6,37 b,t 3,25 1,a 4,w 1,1i 9,d 2,9 2,2u o,r a,5 2,5 ' +
  '2,5 9,6 1,6 1,1n 4,3h 2,9 6,8mb c,m 4,1c 6is,a5 2,2x 12,6 c,4 5,p 1,4 1,0 1,1 1,1 1,3g ' +
  'g,cc 2,1h 7,0 w,15 6,1e 1,i 1,3 4,4 1,3q 4,59 3,5 2,5 2,5 2,2 3,6 1,6 d,1 2,b 1,p 1,i ' +
  '1,1 1,e 2,d y,3e 5,2 4,18 3,2f 1,c 3,0 1b,19 3m,s 3,1c f,r 4,z 9,t 5,16 5,t 1,10 4,d ' +
  '16,4d 2,9 6,z 4,z 4,13 8,1f b,b 1,e 1,6 1,1 1,a 1,e 1,6 1,1 3,1f c,8m 9,l a,7 o,5 1,15 ' +
  '1,8 1x,5 2,0 1,17 1,1 3,0 2,m 1,1z 8,8 1c,i 1,1 5,w 3,q 5,0 1s,1j 4,j 2,1d 1,1 5,7 1,2 ' +
  '1,s 2,2 4,9 7,8 7,1r w,12 4,b 9,1h 3,s 2,q 5,p 7,3 c,6 28,20 1j,1e d,1e 7,19 8,9 6,11 ' +
  '3,s 8,1 5s,u 1,15 1,2 2,1 g,2 1j,17 8,15 m,p 12,r k,m 9,25 4,z 9,1p 1,4 d,o 7,9 6,1g 1,h ' +
  '8,12 9,2n 1,j b,h 1,1a 1q,6 1,0 1,3 1,e 1,a 6,1m 5,9 6,3 1,7 2,1 2,l 1,6 1,1 1,4 1,9 2,1 ' +
  '2,2 2,0 6,0 5,6 2,6 3,4 b,9 1,0 2,0 1,11 1,9 1,0 2,0 1,3 1,9 1,1 8,1 t,2j 1,4 u,1z 8,9 ' +
  '4m,1h 2,11 y,1w b,9 6,c j,1l 6,9 6,j s,q 2,e 4,m 55,1n 2s,2a c,7 2,0 2,7 1,1 1,t 1,1 2,b ' +
  '9,9 1y,7 2,19 2,a r,1z 8,2a d,20 7,9 52,x e,9 6,8 1,18 1,d a,s 3,v 2,l 1,d 21,6 1,1 1,17 ' +
  '3,0 1,1 1,8 8,9 6,5 1,1 1,10 1,1 1,5 7,9 8m,o 7,g 1,14 3,s 2d,0 f,1d d,pm 2u,32 1,4 b,5f ' +
  '218,2q d,tr g,l a,32y 5,g6 5a1,1l 1c6,fs 7,u 1,9 4,28 1,9 6,t 2,5 a,1x a,9 1,6 1,k 5,i ' +
  'c0,1l 5i,2i 2t,22 4,1k 7,g 1s,4 b,1 e,4qf 8,yd 15,9 6w7,3 1,6 1,1 1,82 f,0 t,2 2,0 e,3 ' +
  '8,az 1s4,2y 5,c 3,8 7,9 2,3 31c,6x 6,c3 24,19 2,m 9,37 1o,6t a,12 2,21 8,33 l,1x 3e,j ' +
  'c,j c,2e 9,o 3r,2c 1,1y 1,1 2,0 2,1 2,3 1,b 1,0 1,6 1,1s 1,3 2,7 1,6 1,r 1,3 1,4 1,0 3,6 ' +
  '1,9f 2,83 2,jh f,4 1,e uo,u 6,5 5x,6 1,g 2,6 1,1 1,4 5,1p x,0 34,18 3,d 2,9 4,1 8w,u ' +
  'h,1l 5,0 cw,15 5y,16 4,0 dc,6 1,3 1,1 1,e 1,5g 2,f 15,23 4,9 4,1 lt,1v 24,1o 5e,3 1,q ' +
  '1,1 1,0 2,0 1,9 1,3 1,0 1,0 6,0 4,0 1,0 1,0 1,2 1,1 1,0 2,0 1,0 1,0 1,0 1,0 1,1 1,0 2,3 ' +
  '1,6 1,3 1,3 1,0 1,9 1,g 5,2 1,4 1,g 1g,1 7i,17 4,2r c,e 2,e 1,e 1,10 a,4t 1k,s d,17 4,8 ' +
  '7,1 e,5 4a,rb 4,g 3,c 3,3a 4,2m 6,b 4,0 f,b 4,1j 8,9 6,13 8,t 2,b 4,1 1q,9f c,d 2,c 3,9 ' +
  '5,1j 7,e 2,a 6,8 7,42 1,2t sm,wyn w,37d 6,65 2,4g1 e,5rk f,h9 1wi,f1 15u,3t6 5,38f ' +
  'f9e8,6n';

const CASE_CLASS_SOURCE =
  '1t.2p 1u.2q 1v.2r 1w.2s 1x.2t 1y.2u 1z.2v 20.2w 21.2x.8g.8h 22.2y 23.2z.6ju 24.30 25.31 26.32 27' +
  '.33 28.34 29.35 2a.36 2b.37.an 2c.38 2d.39 2e.3a 2f.3b 2g.3c 2h.3d 2i.3e 51.po.qk 5c.68 5d.69 5e' +
  '.6a 5f.6b 5g.6c 5h.6d.6jv 5i.6e 5j.6f 5k.6g 5l.6h 5m.6i 5n.6j 5o.6k 5p.6l 5q.6m 5r.6n 5s.6o 5t.6' +
  'p 5u.6q 5v.6r 5w.6s 5x.6t 5y.6u 60.6w 61.6x 62.6y 63.6z 64.70 65.71 66.72 67.61q 73.ag 74.75 76.' +
  '77 78.79 7a.7b 7c.7d 7e.7f 7g.7h 7i.7j 7k.7l 7m.7n 7o.7p 7q.7r 7s.7t 7u.7v 7w.7x 7y.7z 80.81 82.' +
  '83 84.85 86.87 88.89 8a.8b 8c.8d 8e.8f 8i.8j 8k.8l 8m.8n 8p.8q 8r.8s 8t.8u 8v.8w 8x.8y 8z.90 91.' +
  '92 93.94 96.97 98.99 9a.9b 9c.9d 9e.9f 9g.9h 9i.9j 9k.9l 9m.9n 9o.9p 9q.9r 9s.9t 9u.9v 9w.9x 9y.' +
  '9z a0.a1 a2.a3 a4.a5 a6.a7 a8.a9 aa.ab ac.ad ae.af ah.ai aj.ak al.am ao.g3 ap.gj aq.ar as.at au.' +
  'gk av.aw ax.gm ay.gn az.b0 b2.d9 b3.gp b4.gr b5.b6 b7.gw b8.gz b9.dy ba.h5 bb.h4 bc.bd be.fx bf.' +
  'x5o bg.hb bh.he bi.f4 bj.hh bk.bl bm.bn bo.bp bq.hs br.bs bt.hv bw.bx by.i0 bz.c0 c1.i2 c2.i3 c3' +
  '.c4 c5.c6 c7.ia c8.c9 cc.cd cf.dz ck.cl.cm cn.co.cp cq.cr.cs ct.cu cv.cw cx.cy cz.d0 d1.d2 d3.d4' +
  ' d5.d6 d7.d8 da.db dc.dd de.df dg.dh di.dj dk.dl dm.dn do.dp dq.dr dt.du.dv dw.dx e0.e1 e2.e3 e4' +
  '.e5 e6.e7 e8.e9 ea.eb ec.ed ee.ef eg.eh ei.ej ek.el em.en eo.ep eq.er es.et eu.ev ew.ex ey.ez f0' +
  '.f1 f2.f3 f6.f7 f8.f9 fa.fb fc.fd fe.ff fg.fh fi.fj fk.fl fm.fn fu.8rp fv.fw fy.8rq fz.8se g0.8s' +
  'f g1.g2 g4.i1 g5.i4 g6.g7 g8.g9 ga.gb gc.gd ge.gf gg.8rz gh.8rx gi.8s0 gs.x4b gx.x4c h0.x57 h1.x' +
  '3h h2.x4a h6.x4e h7.8rm h8.x4d hd.8ry hp.8ro hu.x51 hz.x4h il.x4i im.x4g n9.pl.qh.69q og.oh oi.o' +
  'j om.on or.sd os.se ot.sf ov.s3 p2.q4 p4.q5 p5.q6 p6.q7 p8.r0 pa.r1 pb.r2 pc.6ab pd.q9 pe.qa.r4 ' +
  'pf.qb pg.qc ph.qd.s5 pi.qe pj.qf pk.qg.r5.s4 pm.qi.s0 pn.qj pp.ql pq.qm pr.qn ps.qo.ra pt.qp.s1 ' +
  'pv.qq.qr pw.qs px.qt py.qu.r9 pz.qv q0.qw q1.qx.6jq q2.qy q3.qz q8.6ar r3.rb rc.rd re.rf rg.rh r' +
  'i.rj rk.rl rm.rn ro.rp rq.rr rs.rt ru.rv rw.rx ry.rz s2.s9 s7.s8 sa.sb sg.uo sh.up si.uq sj.ur s' +
  'k.us sl.ut sm.uu sn.uv so.uw sp.ux sq.uy sr.uz ss.v0 st.v1 su.v2 sv.v3 sw.ts sx.tt sy.tu.5mo sz.' +
  'tv t0.tw.5mp t1.tx t2.ty t3.tz t4.u0 t5.u1 t6.u2 t7.u3 t8.u4 t9.u5 ta.u6.5mq tb.u7 tc.u8 td.u9.5' +
  'mr te.ua.5ms.5mt tf.ub tg.uc th.ud ti.ue tj.uf tk.ug tl.uh tm.ui.5mu tn.uj to.uk tp.ul tq.um tr.' +
  'un v4.v5 v6.v7.5mv v8.v9 va.vb vc.vd ve.vf vg.vh vi.vj vk.vl vm.vn vo.vp vq.vr vs.vt vu.vv vw.vx' +
  ' vy.vz w0.w1 wa.wb wc.wd we.wf wg.wh wi.wj wk.wl wm.wn wo.wp wq.wr ws.wt wu.wv ww.wx wy.wz x0.x1' +
  ' x2.x3 x4.x5 x6.x7 x8.x9 xa.xb xc.xd xe.xf xg.xh xi.xj xk.xl xm.xn xo.xp xq.xr xs.y7 xt.xu xv.xw' +
  ' xx.xy xz.y0 y1.y2 y3.y4 y5.y6 y8.y9 ya.yb yc.yd ye.yf yg.yh yi.yj yk.yl ym.yn yo.yp yq.yr ys.yt' +
  ' yu.yv yw.yx yy.yz z0.z1 z2.z3 z4.z5 z6.z7 z8.z9 za.zb zc.zd ze.zf zg.zh zi.zj zk.zl zm.zn zo.zp' +
  ' zq.zr zs.zt zu.zv zw.zx zy.zz 100.101 102.103 104.105 106.107 108.109 10a.10b 10c.10d 10e.10f 1' +
  '0g.10h 10i.10j 10k.10l 10m.10n 10o.10p 10q.10r 10s.10t 10u.10v 10x.129 10y.12a 10z.12b 110.12c 1' +
  '11.12d 112.12e 113.12f 114.12g 115.12h 116.12i 117.12j 118.12k 119.12l 11a.12m 11b.12n 11c.12o 1' +
  '1d.12p 11e.12q 11f.12r 11g.12s 11h.12t 11i.12u 11j.12v 11k.12w 11l.12x 11m.12y 11n.12z 11o.130 1' +
  '1p.131 11q.132 11r.133 11s.134 11t.135 11u.136 11v.137 11w.138 11x.139 11y.13a 3a8.8w0 3a9.8w1 3' +
  'aa.8w2 3ab.8w3 3ac.8w4 3ad.8w5 3ae.8w6 3af.8w7 3ag.8w8 3ah.8w9 3ai.8wa 3aj.8wb 3ak.8wc 3al.8wd 3' +
  'am.8we 3an.8wf 3ao.8wg 3ap.8wh 3aq.8wi 3ar.8wj 3as.8wk 3at.8wl 3au.8wm 3av.8wn 3aw.8wo 3ax.8wp 3' +
  'ay.8wq 3az.8wr 3b0.8ws 3b1.8wt 3b2.8wu 3b3.8wv 3b4.8ww 3b5.8wx 3b6.8wy 3b7.8wz 3b8.8x0 3b9.8x1 3' +
  'bb.8x3 3bh.8x9 3bk.5n4 3bl.5n5 3bm.5n6 3bn.5n7 3bo.5n8 3bp.5n9 3bq.5na 3br.5nb 3bs.5nc 3bt.5nd 3' +
  'bu.5ne 3bv.5nf 3bw.5ng 3bx.5nh 3by.5ni 3bz.5nj 3c0.5nk 3c1.5nl 3c2.5nm 3c3.5nn 3c4.5no 3c5.5np 3' +
  'c6.5nq 3c7.5nr 3c8.5ns 3c9.5nt 3ca.5nu 3cb.5nv 3cc.5nw 3cd.5nx 3ce.5ny 3cf.5nz 3cg.5o0 3ch.5o1 3' +
  'ci.5o2 3cj.5o3 3ck.5o4 3cl.5o5 3cm.5o6 3cn.5o7 3co.5o8 3cp.5o9 3cq.5oa 3ct.5od 3cu.5oe 3cv.5of 3' +
  'vk.xv4 3vl.xv5 3vm.xv6 3vn.xv7 3vo.xv8 3vp.xv9 3vq.xva 3vr.xvb 3vs.xvc 3vt.xvd 3vu.xve 3vv.xvf 3' +
  'vw.xvg 3vx.xvh 3vy.xvi 3vz.xvj 3w0.xvk 3w1.xvl 3w2.xvm 3w3.xvn 3w4.xvo 3w5.xvp 3w6.xvq 3w7.xvr 3' +
  'w8.xvs 3w9.xvt 3wa.xvu 3wb.xvv 3wc.xvw 3wd.xvx 3we.xvy 3wf.xvz 3wg.xw0 3wh.xw1 3wi.xw2 3wj.xw3 3' +
  'wk.xw4 3wl.xw5 3wm.xw6 3wn.xw7 3wo.xw8 3wp.xw9 3wq.xwa 3wr.xwb 3ws.xwc 3wt.xwd 3wu.xwe 3wv.xwf 3' +
  'ww.xwg 3wx.xwh 3wy.xwi 3wz.xwj 3x0.xwk 3x1.xwl 3x2.xwm 3x3.xwn 3x4.xwo 3x5.xwp 3x6.xwq 3x7.xwr 3' +
  'x8.xws 3x9.xwt 3xa.xwu 3xb.xwv 3xc.xww 3xd.xwx 3xe.xwy 3xf.xwz 3xg.xx0 3xh.xx1 3xi.xx2 3xj.xx3 3' +
  'xk.xx4 3xl.xx5 3xm.xx6 3xn.xx7 3xo.xx8 3xp.xx9 3xq.xxa 3xr.xxb 3xs.3y0 3xt.3y1 3xu.3y2 3xv.3y3 3' +
  'xw.3y4 3xx.3y5 5mw.wui.wuj 5mx.5my 5tl.x31 5tp.8rn 5u6.x52 5xc.5xd 5xe.5xf 5xg.5xh 5xi.5xj 5xk.5' +
  'xl 5xm.5xn 5xo.5xp 5xq.5xr 5xs.5xt 5xu.5xv 5xw.5xx 5xy.5xz 5y0.5y1 5y2.5y3 5y4.5y5 5y6.5y7 5y8.5' +
  'y9 5ya.5yb 5yc.5yd 5ye.5yf 5yg.5yh 5yi.5yj 5yk.5yl 5ym.5yn 5yo.5yp 5yq.5yr 5ys.5yt 5yu.5yv 5yw.5' +
  'yx 5yy.5yz 5z0.5z1 5z2.5z3 5z4.5z5 5z6.5z7 5z8.5z9 5za.5zb 5zc.5zd 5ze.5zf 5zg.5zh 5zi.5zj 5zk.5' +
  'zl 5zm.5zn 5zo.5zp 5zq.5zr 5zs.5zt 5zu.5zv 5zw.5zx 5zy.5zz 600.601.61n 602.603 604.605 606.607 6' +
  '08.609 60a.60b 60c.60d 60e.60f 60g.60h 60i.60j 60k.60l 60m.60n 60o.60p 60q.60r 60s.60t 60u.60v 6' +
  '0w.60x 60y.60z 610.611 612.613 614.615 616.617 618.619 61a.61b 61c.61d 61e.61f 61g.61h 61s.61t 6' +
  '1u.61v 61w.61x 61y.61z 620.621 622.623 624.625 626.627 628.629 62a.62b 62c.62d 62e.62f 62g.62h 6' +
  '2i.62j 62k.62l 62m.62n 62o.62p 62q.62r 62s.62t 62u.62v 62w.62x 62y.62z 630.631 632.633 634.635 6' +
  '36.637 638.639 63a.63b 63c.63d 63e.63f 63g.63h 63i.63j 63k.63l 63m.63n 63o.63p 63q.63r 63s.63t 6' +
  '3u.63v 63w.63x 63y.63z 640.641 642.643 644.645 646.647 648.649 64a.64b 64c.64d 64e.64f 64g.64o 6' +
  '4h.64p 64i.64q 64j.64r 64k.64s 64l.64t 64m.64u 64n.64v 64w.654 64x.655 64y.656 64z.657 650.658 6' +
  '51.659 65c.65k 65d.65l 65e.65m 65f.65n 65g.65o 65h.65p 65i.65q 65j.65r 65s.660 65t.661 65u.662 6' +
  '5v.663 65w.664 65x.665 65y.666 65z.667 668.66g 669.66h 66a.66i 66b.66j 66c.66k 66d.66l 66p.66x 6' +
  '6r.66z 66t.671 66v.673 674.67c 675.67d 676.67e 677.67f 678.67g 679.67h 67a.67i 67b.67j 67k.69m 6' +
  '7l.69n 67m.6a0 67n.6a1 67o.6a2 67p.6a3 67q.6ai 67r.6aj 67s.6bc 67t.6bd 67u.6ay 67v.6az 67w.6be 6' +
  '7x.6bf 680.688 681.689 682.68a 683.68b 684.68c 685.68d 686.68e 687.68f 68g.68o 68h.68p 68i.68q 6' +
  '8j.68r 68k.68s 68l.68t 68m.68u 68n.68v 68w.694 68x.695 68y.696 68z.697 690.698 691.699 692.69a 6' +
  '93.69b 69c.69k 69d.69l 69f.69o 69v.6a4 6a8.6ag 6a9.6ah 6ao.6aw 6ap.6ax 6at.6b0 6b7.6bg 6k2.6ku 6' +
  'lc.6ls 6ld.6lt 6le.6lu 6lf.6lv 6lg.6lw 6lh.6lx 6li.6ly 6lj.6lz 6lk.6m0 6ll.6m1 6lm.6m2 6ln.6m3 6' +
  'lo.6m4 6lp.6m5 6lq.6m6 6lr.6m7 6mb.6mc 792.79s 793.79t 794.79u 795.79v 796.79w 797.79x 798.79y 7' +
  '99.79z 79a.7a0 79b.7a1 79c.7a2 79d.7a3 79e.7a4 79f.7a5 79g.7a6 79h.7a7 79i.7a8 79j.7a9 79k.7aa 7' +
  '9l.7ab 79m.7ac 79n.7ad 79o.7ae 79p.7af 79q.7ag 79r.7ah 8ow.8q8 8ox.8q9 8oy.8qa 8oz.8qb 8p0.8qc 8' +
  'p1.8qd 8p2.8qe 8p3.8qf 8p4.8qg 8p5.8qh 8p6.8qi 8p7.8qj 8p8.8qk 8p9.8ql 8pa.8qm 8pb.8qn 8pc.8qo 8' +
  'pd.8qp 8pe.8qq 8pf.8qr 8pg.8qs 8ph.8qt 8pi.8qu 8pj.8qv 8pk.8qw 8pl.8qx 8pm.8qy 8pn.8qz 8po.8r0 8' +
  'pp.8r1 8pq.8r2 8pr.8r3 8ps.8r4 8pt.8r5 8pu.8r6 8pv.8r7 8pw.8r8 8px.8r9 8py.8ra 8pz.8rb 8q0.8rc 8' +
  'q1.8rd 8q2.8re 8q3.8rf 8q4.8rg 8q5.8rh 8q6.8ri 8q7.8rj 8rk.8rl 8rr.8rs 8rt.8ru 8rv.8rw 8s2.8s3 8' +
  's5.8s6 8sg.8sh 8si.8sj 8sk.8sl 8sm.8sn 8so.8sp 8sq.8sr 8ss.8st 8su.8sv 8sw.8sx 8sy.8sz 8t0.8t1 8' +
  't2.8t3 8t4.8t5 8t6.8t7 8t8.8t9 8ta.8tb 8tc.8td 8te.8tf 8tg.8th 8ti.8tj 8tk.8tl 8tm.8tn 8to.8tp 8' +
  'tq.8tr 8ts.8tt 8tu.8tv 8tw.8tx 8ty.8tz 8u0.8u1 8u2.8u3 8u4.8u5 8u6.8u7 8u8.8u9 8ua.8ub 8uc.8ud 8' +
  'ue.8uf 8ug.8uh 8ui.8uj 8uk.8ul 8um.8un 8uo.8up 8uq.8ur 8us.8ut 8uu.8uv 8uw.8ux 8uy.8uz 8v0.8v1 8' +
  'v2.8v3 8v4.8v5 8v6.8v7 8vf.8vg 8vh.8vi 8vm.8vn wu8.wu9 wua.wub wuc.wud wue.wuf wug.wuh wuk.wul w' +
  'um.wun wuo.wup wuq.wur wus.wut wuu.wuv wuw.wux wuy.wuz wv0.wv1 wv2.wv3 wv4.wv5 wv6.wv7 wv8.wv9 w' +
  'va.wvb wvc.wvd wve.wvf wvg.wvh ww0.ww1 ww2.ww3 ww4.ww5 ww6.ww7 ww8.ww9 wwa.wwb wwc.wwd wwe.wwf w' +
  'wg.wwh wwi.wwj wwk.wwl wwm.wwn wwo.wwp wwq.wwr x0i.x0j x0k.x0l x0m.x0n x0o.x0p x0q.x0r x0s.x0t x' +
  '0u.x0v x0y.x0z x10.x11 x12.x13 x14.x15 x16.x17 x18.x19 x1a.x1b x1c.x1d x1e.x1f x1g.x1h x1i.x1j x' +
  '1k.x1l x1m.x1n x1o.x1p x1q.x1r x1s.x1t x1u.x1v x1w.x1x x1y.x1z x20.x21 x22.x23 x24.x25 x26.x27 x' +
  '28.x29 x2a.x2b x2c.x2d x2e.x2f x2g.x2h x2i.x2j x2k.x2l x2m.x2n x2x.x2y x2z.x30 x32.x33 x34.x35 x' +
  '36.x37 x38.x39 x3a.x3b x3f.x3g x3k.x3l x3m.x3n x3o.x50 x3q.x3r x3s.x3t x3u.x3v x3w.x3x x3y.x3z x' +
  '40.x41 x42.x43 x44.x45 x46.x47 x48.x49 x4j.xub x4k.x4l x4m.x4n x4o.x4p x4q.x4r x4s.x4t x4u.x4v x' +
  '4w.x4x x4y.x4z x53.x54 x55.x56 x58.x59 x5c.x5d x5i.x5j x5k.x5l x5m.x5n x6d.x6e 1dl1.1dl2 1ee9.1e' +
  'f5 1eea.1ef6 1eeb.1ef7 1eec.1ef8 1eed.1ef9 1eee.1efa 1eef.1efb 1eeg.1efc 1eeh.1efd 1eei.1efe 1ee' +
  'j.1eff 1eek.1efg 1eel.1efh 1eem.1efi 1een.1efj 1eeo.1efk 1eep.1efl 1eeq.1efm 1eer.1efn 1ees.1efo' +
  ' 1eet.1efp 1eeu.1efq 1eev.1efr 1eew.1efs 1eex.1eft 1eey.1efu 1fcw.1fe0 1fcx.1fe1 1fcy.1fe2 1fcz.' +
  '1fe3 1fd0.1fe4 1fd1.1fe5 1fd2.1fe6 1fd3.1fe7 1fd4.1fe8 1fd5.1fe9 1fd6.1fea 1fd7.1feb 1fd8.1fec 1' +
  'fd9.1fed 1fda.1fee 1fdb.1fef 1fdc.1feg 1fdd.1feh 1fde.1fei 1fdf.1fej 1fdg.1fek 1fdh.1fel 1fdi.1f' +
  'em 1fdj.1fen 1fdk.1feo 1fdl.1fep 1fdm.1feq 1fdn.1fer 1fdo.1fes 1fdp.1fet 1fdq.1feu 1fdr.1fev 1fd' +
  's.1few 1fdt.1fex 1fdu.1fey 1fdv.1fez 1fdw.1ff0 1fdx.1ff1 1fdy.1ff2 1fdz.1ff3 1fhs.1fiw 1fht.1fix' +
  ' 1fhu.1fiy 1fhv.1fiz 1fhw.1fj0 1fhx.1fj1 1fhy.1fj2 1fhz.1fj3 1fi0.1fj4 1fi1.1fj5 1fi2.1fj6 1fi3.' +
  '1fj7 1fi4.1fj8 1fi5.1fj9 1fi6.1fja 1fi7.1fjb 1fi8.1fjc 1fi9.1fjd 1fia.1fje 1fib.1fjf 1fic.1fjg 1' +
  'fid.1fjh 1fie.1fji 1fif.1fjj 1fig.1fjk 1fih.1fjl 1fii.1fjm 1fij.1fjn 1fik.1fjo 1fil.1fjp 1fim.1f' +
  'jq 1fin.1fjr 1fio.1fjs 1fip.1fjt 1fiq.1fju 1fir.1fjv 1fn4.1fo7 1fn5.1fo8 1fn6.1fo9 1fn7.1foa 1fn' +
  '8.1fob 1fn9.1foc 1fna.1fod 1fnb.1foe 1fnc.1fof 1fnd.1fog 1fne.1foh 1fng.1foj 1fnh.1fok 1fni.1fol' +
  ' 1fnj.1fom 1fnk.1fon 1fnl.1foo 1fnm.1fop 1fnn.1foq 1fno.1for 1fnp.1fos 1fnq.1fot 1fnr.1fou 1fns.' +
  '1fov 1fnt.1fow 1fnu.1fox 1fnw.1foz 1fnx.1fp0 1fny.1fp1 1fnz.1fp2 1fo0.1fp3 1fo1.1fp4 1fo2.1fp5 1' +
  'fo4.1fp7 1fo5.1fp8 1h1c.1h34 1h1d.1h35 1h1e.1h36 1h1f.1h37 1h1g.1h38 1h1h.1h39 1h1i.1h3a 1h1j.1h' +
  '3b 1h1k.1h3c 1h1l.1h3d 1h1m.1h3e 1h1n.1h3f 1h1o.1h3g 1h1p.1h3h 1h1q.1h3i 1h1r.1h3j 1h1s.1h3k 1h1' +
  't.1h3l 1h1u.1h3m 1h1v.1h3n 1h1w.1h3o 1h1x.1h3p 1h1y.1h3q 1h1z.1h3r 1h20.1h3s 1h21.1h3t 1h22.1h3u' +
  ' 1h23.1h3v 1h24.1h3w 1h25.1h3x 1h26.1h3y 1h27.1h3z 1h28.1h40 1h29.1h41 1h2a.1h42 1h2b.1h43 1h2c.' +
  '1h44 1h2d.1h45 1h2e.1h46 1h2f.1h47 1h2g.1h48 1h2h.1h49 1h2i.1h4a 1h2j.1h4b 1h2k.1h4c 1h2l.1h4d 1' +
  'h2m.1h4e 1h2n.1h4f 1h2o.1h4g 1h2p.1h4h 1h2q.1h4i 1h74.1h80 1h75.1h81 1h76.1h82 1h77.1h83 1h78.1h' +
  '84 1h79.1h85 1h7a.1h86 1h7b.1h87 1h7c.1h88 1h7d.1h89 1h7e.1h8a 1h7f.1h8b 1h7g.1h8c 1h7h.1h8d 1h7' +
  'i.1h8e 1h7j.1h8f 1h7k.1h8g 1h7l.1h8h 1h7m.1h8i 1h7n.1h8j 1h7o.1h8k 1h7p.1h8l 1jfk.1jgg 1jfl.1jgh' +
  ' 1jfm.1jgi 1jfn.1jgj 1jfo.1jgk 1jfp.1jgl 1jfq.1jgm 1jfr.1jgn 1jfs.1jgo 1jft.1jgp 1jfu.1jgq 1jfv.' +
  '1jgr 1jfw.1jgs 1jfx.1jgt 1jfy.1jgu 1jfz.1jgv 1jg0.1jgw 1jg1.1jgx 1jg2.1jgy 1jg3.1jgz 1jg4.1jh0 1' +
  'jg5.1jh1 1jg6.1jh2 1jg7.1jh3 1jg8.1jh4 1jg9.1jh5 1jga.1jh6 1jgb.1jh7 1jgc.1jh8 1jgd.1jh9 1jge.1j' +
  'ha 1jgf.1jhb 20cg.20dc 20ch.20dd 20ci.20de 20cj.20df 20ck.20dg 20cl.20dh 20cm.20di 20cn.20dj 20c' +
  'o.20dk 20cp.20dl 20cq.20dm 20cr.20dn 20cs.20do 20ct.20dp 20cu.20dq 20cv.20dr 20cw.20ds 20cx.20dt' +
  ' 20cy.20du 20cz.20dv 20d0.20dw 20d1.20dx 20d2.20dy 20d3.20dz 20d4.20e0 20d5.20e1 20d6.20e2 20d7.' +
  '20e3 20d8.20e4 20d9.20e5 20da.20e6 20db.20e7 2olc.2oma 2old.2omb 2ole.2omc 2olf.2omd 2olg.2ome 2' +
  'olh.2omf 2oli.2omg 2olj.2omh 2olk.2omi 2oll.2omj 2olm.2omk 2oln.2oml 2olo.2omm 2olp.2omn 2olq.2o' +
  'mo 2olr.2omp 2ols.2omq 2olt.2omr 2olu.2oms 2olv.2omt 2olw.2omu 2olx.2omv 2oly.2omw 2olz.2omx 2om' +
  '0.2omy 2om1.2omz 2om2.2on0 2om3.2on1 2om4.2on2 2om5.2on3 2om6.2on4 2om7.2on5 2om8.2on6 2om9.2on7';

const NAME_SOURCE =
  'ACTIVATE ARABIC FORM SHAPING=6el|ACTIVATE SYMMETRIC SWAPPING=6ej|ACUTE ACCENT=50|AMPERSAND=12|AP' +
  'OSTROPHE=13|ASTERISK=16|ASTERISM=6de|BLACK LEFTWARDS BULLET=6do|BLACK RIGHTWARDS BULLET=6dp|BROK' +
  'EN BAR=4m|BULLET=6ci|CARET=6d4|CARET INSERTION POINT=6dd|CEDILLA=54|CENT SIGN=4i|CHARACTER TIE=6' +
  'dc|CIRCUMFLEX ACCENT=2m|CLOSE UP=6ds|COLON=1m|COMMA=18|COMMERCIAL AT=1s|COMMERCIAL MINUS SIGN=6d' +
  'u|COPYRIGHT SIGN=4p|CURRENCY SIGN=4k|DAGGER=6cg|DEGREE SIGN=4w|DIAERESIS=4o|DIGIT EIGHT=1k|DIGIT' +
  ' FIVE=1h|DIGIT FOUR=1g|DIGIT NINE=1l|DIGIT ONE=1d|DIGIT SEVEN=1j|DIGIT SIX=1i|DIGIT THREE=1f|DIG' +
  'IT TWO=1e|DIGIT ZERO=1c|DIVISION SIGN=6v|DOLLAR SIGN=10|DOTTED CROSS=6e4|DOUBLE DAGGER=6ch|DOUBL' +
  'E EXCLAMATION MARK=6d8|DOUBLE HIGH-REVERSED-9 QUOTATION MARK=6cf|DOUBLE LOW LINE=6c7|DOUBLE LOW-' +
  '9 QUOTATION MARK=6ce|DOUBLE PRIME=6cz|DOUBLE QUESTION MARK=6dj|DOUBLE VERTICAL LINE=6c6|EM DASH=' +
  '6c4|EM QUAD=6bl|EM SPACE=6bn|EN DASH=6c3|EN QUAD=6bk|EN SPACE=6bm|EQUALS SIGN=1p|EXCLAMATION MAR' +
  'K=x|EXCLAMATION QUESTION MARK=6dl|FEMININE ORDINAL INDICATOR=4q|FIGURE DASH=6c2|FIGURE SPACE=6br' +
  '|FIRST STRONG ISOLATE=6eg|FIVE DOT PUNCTUATION=6e1|FLOWER PUNCTUATION MARK=6dx|FOUR DOT MARK=6e3' +
  '|FOUR DOT PUNCTUATION=6e0|FOUR-PER-EM SPACE=6bp|FRACTION SLASH=6dg|FULL STOP=1a|FUNCTION APPLICA' +
  'TION=6e9|GRAVE ACCENT=2o|GREATER-THAN SIGN=1q|HAIR SPACE=6bu|HORIZONTAL BAR=6c5|HORIZONTAL ELLIP' +
  'SIS=6cm|HYPHEN=6c0|HYPHEN BULLET=6df|HYPHEN-MINUS=19|HYPHENATION POINT=6cn|INHIBIT ARABIC FORM S' +
  'HAPING=6ek|INHIBIT SYMMETRIC SWAPPING=6ei|INTERROBANG=6d9|INVERTED EXCLAMATION MARK=4h|INVERTED ' +
  'QUESTION MARK=5b|INVERTED UNDERTIE=6dw|INVISIBLE PLUS=6ec|INVISIBLE SEPARATOR=6eb|INVISIBLE TIME' +
  'S=6ea|LATIN CAPITAL LETTER A=1t|LATIN CAPITAL LETTER A WITH ACUTE=5d|LATIN CAPITAL LETTER A WITH' +
  ' CIRCUMFLEX=5e|LATIN CAPITAL LETTER A WITH DIAERESIS=5g|LATIN CAPITAL LETTER A WITH GRAVE=5c|LAT' +
  'IN CAPITAL LETTER A WITH RING ABOVE=5h|LATIN CAPITAL LETTER A WITH TILDE=5f|LATIN CAPITAL LETTER' +
  ' AE=5i|LATIN CAPITAL LETTER B=1u|LATIN CAPITAL LETTER C=1v|LATIN CAPITAL LETTER C WITH CEDILLA=5' +
  'j|LATIN CAPITAL LETTER D=1w|LATIN CAPITAL LETTER E=1x|LATIN CAPITAL LETTER E WITH ACUTE=5l|LATIN' +
  ' CAPITAL LETTER E WITH CIRCUMFLEX=5m|LATIN CAPITAL LETTER E WITH DIAERESIS=5n|LATIN CAPITAL LETT' +
  'ER E WITH GRAVE=5k|LATIN CAPITAL LETTER ETH=5s|LATIN CAPITAL LETTER F=1y|LATIN CAPITAL LETTER G=' +
  '1z|LATIN CAPITAL LETTER H=20|LATIN CAPITAL LETTER I=21|LATIN CAPITAL LETTER I WITH ACUTE=5p|LATI' +
  'N CAPITAL LETTER I WITH CIRCUMFLEX=5q|LATIN CAPITAL LETTER I WITH DIAERESIS=5r|LATIN CAPITAL LET' +
  'TER I WITH GRAVE=5o|LATIN CAPITAL LETTER J=22|LATIN CAPITAL LETTER K=23|LATIN CAPITAL LETTER L=2' +
  '4|LATIN CAPITAL LETTER M=25|LATIN CAPITAL LETTER N=26|LATIN CAPITAL LETTER N WITH TILDE=5t|LATIN' +
  ' CAPITAL LETTER O=27|LATIN CAPITAL LETTER O WITH ACUTE=5v|LATIN CAPITAL LETTER O WITH CIRCUMFLEX' +
  '=5w|LATIN CAPITAL LETTER O WITH DIAERESIS=5y|LATIN CAPITAL LETTER O WITH GRAVE=5u|LATIN CAPITAL ' +
  'LETTER O WITH STROKE=60|LATIN CAPITAL LETTER O WITH TILDE=5x|LATIN CAPITAL LETTER P=28|LATIN CAP' +
  'ITAL LETTER Q=29|LATIN CAPITAL LETTER R=2a|LATIN CAPITAL LETTER S=2b|LATIN CAPITAL LETTER T=2c|L' +
  'ATIN CAPITAL LETTER THORN=66|LATIN CAPITAL LETTER U=2d|LATIN CAPITAL LETTER U WITH ACUTE=62|LATI' +
  'N CAPITAL LETTER U WITH CIRCUMFLEX=63|LATIN CAPITAL LETTER U WITH DIAERESIS=64|LATIN CAPITAL LET' +
  'TER U WITH GRAVE=61|LATIN CAPITAL LETTER V=2e|LATIN CAPITAL LETTER W=2f|LATIN CAPITAL LETTER X=2' +
  'g|LATIN CAPITAL LETTER Y=2h|LATIN CAPITAL LETTER Y WITH ACUTE=65|LATIN CAPITAL LETTER Z=2i|LATIN' +
  ' SMALL LETTER A=2p|LATIN SMALL LETTER A WITH ACUTE=69|LATIN SMALL LETTER A WITH CIRCUMFLEX=6a|LA' +
  'TIN SMALL LETTER A WITH DIAERESIS=6c|LATIN SMALL LETTER A WITH GRAVE=68|LATIN SMALL LETTER A WIT' +
  'H RING ABOVE=6d|LATIN SMALL LETTER A WITH TILDE=6b|LATIN SMALL LETTER AE=6e|LATIN SMALL LETTER B' +
  '=2q|LATIN SMALL LETTER C=2r|LATIN SMALL LETTER C WITH CEDILLA=6f|LATIN SMALL LETTER D=2s|LATIN S' +
  'MALL LETTER E=2t|LATIN SMALL LETTER E WITH ACUTE=6h|LATIN SMALL LETTER E WITH CIRCUMFLEX=6i|LATI' +
  'N SMALL LETTER E WITH DIAERESIS=6j|LATIN SMALL LETTER E WITH GRAVE=6g|LATIN SMALL LETTER ETH=6o|' +
  'LATIN SMALL LETTER F=2u|LATIN SMALL LETTER G=2v|LATIN SMALL LETTER H=2w|LATIN SMALL LETTER I=2x|' +
  'LATIN SMALL LETTER I WITH ACUTE=6l|LATIN SMALL LETTER I WITH CIRCUMFLEX=6m|LATIN SMALL LETTER I ' +
  'WITH DIAERESIS=6n|LATIN SMALL LETTER I WITH GRAVE=6k|LATIN SMALL LETTER J=2y|LATIN SMALL LETTER ' +
  'K=2z|LATIN SMALL LETTER L=30|LATIN SMALL LETTER M=31|LATIN SMALL LETTER N=32|LATIN SMALL LETTER ' +
  'N WITH TILDE=6p|LATIN SMALL LETTER O=33|LATIN SMALL LETTER O WITH ACUTE=6r|LATIN SMALL LETTER O ' +
  'WITH CIRCUMFLEX=6s|LATIN SMALL LETTER O WITH DIAERESIS=6u|LATIN SMALL LETTER O WITH GRAVE=6q|LAT' +
  'IN SMALL LETTER O WITH STROKE=6w|LATIN SMALL LETTER O WITH TILDE=6t|LATIN SMALL LETTER P=34|LATI' +
  'N SMALL LETTER Q=35|LATIN SMALL LETTER R=36|LATIN SMALL LETTER S=37|LATIN SMALL LETTER SHARP S=6' +
  '7|LATIN SMALL LETTER T=38|LATIN SMALL LETTER THORN=72|LATIN SMALL LETTER U=39|LATIN SMALL LETTER' +
  ' U WITH ACUTE=6y|LATIN SMALL LETTER U WITH CIRCUMFLEX=6z|LATIN SMALL LETTER U WITH DIAERESIS=70|' +
  'LATIN SMALL LETTER U WITH GRAVE=6x|LATIN SMALL LETTER V=3a|LATIN SMALL LETTER W=3b|LATIN SMALL L' +
  'ETTER X=3c|LATIN SMALL LETTER Y=3d|LATIN SMALL LETTER Y WITH ACUTE=71|LATIN SMALL LETTER Y WITH ' +
  'DIAERESIS=73|LATIN SMALL LETTER Z=3e|LEFT CURLY BRACKET=3f|LEFT DOUBLE QUOTATION MARK=6cc|LEFT P' +
  'ARENTHESIS=14|LEFT SINGLE QUOTATION MARK=6c8|LEFT SQUARE BRACKET=2j|LEFT SQUARE BRACKET WITH QUI' +
  'LL=6dh|LEFT-POINTING DOUBLE ANGLE QUOTATION MARK=4r|LEFT-TO-RIGHT EMBEDDING=6cq|LEFT-TO-RIGHT IS' +
  'OLATE=6ee|LEFT-TO-RIGHT MARK=6by|LEFT-TO-RIGHT OVERRIDE=6ct|LESS-THAN SIGN=1o|LINE SEPARATOR=6co' +
  '|LOW ASTERISK=6dq|LOW LINE=2n|MACRON=4v|MASCULINE ORDINAL INDICATOR=56|MEDIUM MATHEMATICAL SPACE' +
  '=6e7|MICRO SIGN=51|MIDDLE DOT=53|MULTIPLICATION SIGN=5z|NARROW NO-BREAK SPACE=6cv|NATIONAL DIGIT' +
  ' SHAPES=6em|NO-BREAK SPACE=4g|NOMINAL DIGIT SHAPES=6en|NON-BREAKING HYPHEN=6c1|NOT SIGN=4s|NUMBE' +
  'R SIGN=z|ONE DOT LEADER=6ck|OVERLINE=6da|PARAGRAPH SEPARATOR=6cp|PER MILLE SIGN=6cw|PER TEN THOU' +
  'SAND SIGN=6cx|PERCENT SIGN=11|PILCROW SIGN=52|PLUS SIGN=17|PLUS-MINUS SIGN=4x|POP DIRECTIONAL FO' +
  'RMATTING=6cs|POP DIRECTIONAL ISOLATE=6eh|POUND SIGN=4j|PRIME=6cy|PUNCTUATION SPACE=6bs|QUADRUPLE' +
  ' PRIME=6dz|QUESTION EXCLAMATION MARK=6dk|QUESTION MARK=1r|QUOTATION MARK=y|REFERENCE MARK=6d7|RE' +
  'GISTERED SIGN=4u|REVERSE SOLIDUS=2k|REVERSED DOUBLE PRIME=6d2|REVERSED PILCROW SIGN=6dn|REVERSED' +
  ' PRIME=6d1|REVERSED SEMICOLON=6dr|REVERSED TRIPLE PRIME=6d3|RIGHT CURLY BRACKET=3h|RIGHT DOUBLE ' +
  'QUOTATION MARK=6cd|RIGHT PARENTHESIS=15|RIGHT SINGLE QUOTATION MARK=6c9|RIGHT SQUARE BRACKET=2l|' +
  'RIGHT SQUARE BRACKET WITH QUILL=6di|RIGHT-POINTING DOUBLE ANGLE QUOTATION MARK=57|RIGHT-TO-LEFT ' +
  'EMBEDDING=6cr|RIGHT-TO-LEFT ISOLATE=6ef|RIGHT-TO-LEFT MARK=6bz|RIGHT-TO-LEFT OVERRIDE=6cu|SECTIO' +
  'N SIGN=4n|SEMICOLON=1n|SINGLE HIGH-REVERSED-9 QUOTATION MARK=6cb|SINGLE LEFT-POINTING ANGLE QUOT' +
  'ATION MARK=6d5|SINGLE LOW-9 QUOTATION MARK=6ca|SINGLE RIGHT-POINTING ANGLE QUOTATION MARK=6d6|SI' +
  'X-PER-EM SPACE=6bq|SOFT HYPHEN=4t|SOLIDUS=1b|SPACE=w|SUPERSCRIPT ONE=55|SUPERSCRIPT THREE=4z|SUP' +
  'ERSCRIPT TWO=4y|SWUNG DASH=6dv|THIN SPACE=6bt|THREE DOT PUNCTUATION=6dy|THREE-PER-EM SPACE=6bo|T' +
  'ILDE=3i|TIRONIAN SIGN ET=6dm|TRIANGULAR BULLET=6cj|TRICOLON=6e5|TRIPLE PRIME=6d0|TWO ASTERISKS A' +
  'LIGNED VERTICALLY=6dt|TWO DOT LEADER=6cl|TWO DOT PUNCTUATION=6e2|UNDERTIE=6db|VERTICAL FOUR DOTS' +
  '=6e6|VERTICAL LINE=3g|VULGAR FRACTION ONE HALF=59|VULGAR FRACTION ONE QUARTER=58|VULGAR FRACTION' +
  ' THREE QUARTERS=5a|WORD JOINER=6e8|YEN SIGN=4l|ZERO WIDTH JOINER=6bx|ZERO WIDTH NON-JOINER=6bw|Z' +
  'ERO WIDTH SPACE=6bv';


const FOLD = decodeFold(FOLD_SOURCE);
const CASE_CLASSES = decodeClasses(CASE_CLASS_SOURCE);
const CHARACTER_NAMES = decodeNames(NAME_SOURCE);

/**
 * `str.casefold()` — CPython's full case fold, character by character.
 *
 * CPython folds per code point with no context rule (unlike `str.lower()`,
 * which has the final-sigma rule), so this iterates code points and never
 * hands a whole string to `toLowerCase`, whose final-sigma handling would
 * disagree with `casefold` on `ΑΣ`.
 */
export const casefold = (text: string): string => {
  let folded = '';
  for (const character of text) {
    const special = FOLD.get(character.codePointAt(0) ?? 0);
    folded += special ?? character.toLowerCase();
  }
  return folded;
};

/** `\w` on a `str` pattern: `str.isalnum()` plus `_`, as CPython enumerates it. */
export const UNICODE_WORD_RANGES: ReadonlyArray<CodeRange> = decodeRanges(WORD_SOURCE);

/** `\s` on a `str` pattern: `str.isspace()`, which keeps `\x1c`–`\x1f`. */
export const UNICODE_SPACE_RANGES: ReadonlyArray<CodeRange> = decodeRanges(SPACE_SOURCE);

/** `\d` on a `str` pattern: the decimal digits, i.e. general category `Nd`. */
export const UNICODE_DIGIT_RANGES: ReadonlyArray<CodeRange> = decodeRanges(DIGIT_SOURCE);

const PRINTABLE_RANGES: ReadonlyArray<CodeRange> = decodeRanges(PRINTABLE_SOURCE);

/**
 * `str.isprintable()` — what decides whether `repr()` escapes a character.
 *
 * The ranges are sorted and disjoint, so the lookup is a binary search: a
 * `--grep` refusal echoes the pattern through `repr`, and doing that per code
 * point of a 200-character pattern must not walk 736 ranges each time.
 */
export const pyIsPrintable = (code: number): boolean => {
  let low = 0;
  let high = PRINTABLE_RANGES.length - 1;
  while (low <= high) {
    const middle = (low + high) >> 1;
    const range = PRINTABLE_RANGES[middle];
    if (range === undefined) return false;
    if (code < range[0]) high = middle - 1;
    else if (code > range[1]) low = middle + 1;
    else return true;
  }
  return false;
};

/**
 * Every code point `re.IGNORECASE` treats as equal to `code`, itself included.
 *
 * A code point with no case relatives answers `[code]`, so the caller never
 * needs a "is this cased" test of its own.
 */
export const unicodeCaseClass = (code: number): ReadonlyArray<number> =>
  CASE_CLASSES.get(code) ?? [code];

/**
 * `_sre.unicode_iscased` — "this code point has a case".
 *
 * Wider than "has a case relative": 41 code points fold to *more than one*
 * character (`ŉ`, `ǰ`, `ẖ`, `եւ`, the ligatures) and so are cased without
 * having an equivalent, which is why this is the fold and the partition rather
 * than the partition alone.  Verified against `_sre.unicode_iscased` over every
 * code point.
 */
export const unicodeIsCased = (code: number): boolean => {
  const character = String.fromCodePoint(code);
  return CASE_CLASSES.has(code) || casefold(character) !== character;
};

/** `_sre.ascii_iscased`: `A`–`Z` and `a`–`z`, and nothing else. */
export const asciiIsCased = (code: number): boolean =>
  (code >= 0x41 && code <= 0x5a) || (code >= 0x61 && code <= 0x7a);

/** The sorted code points that have any case relative — 2,940 of them. */
export const CASED_CODE_POINTS: ReadonlyArray<number> = [...CASE_CLASSES.keys()].sort(
  (left, right) => left - right
);

const ASCII_UPPER_A = 0x41;
const ASCII_UPPER_Z = 0x5a;
const ASCII_LOWER_A = 0x61;
const ASCII_LOWER_Z = 0x7a;
const ASCII_CASE_SHIFT = ASCII_LOWER_A - ASCII_UPPER_A;

/**
 * The `(?a)` flavour of the same question.  CPython's ASCII `IGNORECASE` folds
 * with `ascii_tolower` and carries no `_EXTRA_CASES`, so `k` is not `K` (U+212A)
 * and the classes are exactly the 26 letter pairs.
 */
export const asciiCaseClass = (code: number): ReadonlyArray<number> => {
  if (code >= ASCII_UPPER_A && code <= ASCII_UPPER_Z) return [code, code + ASCII_CASE_SHIFT];
  if (code >= ASCII_LOWER_A && code <= ASCII_LOWER_Z) return [code - ASCII_CASE_SHIFT, code];
  return [code];
};

/** The ASCII code points that have a case relative: `A`–`Z` and `a`–`z`. */
export const ASCII_CASED_CODE_POINTS: ReadonlyArray<number> = [
  ...Array.from({ length: 26 }, (_unused, index) => ASCII_UPPER_A + index),
  ...Array.from({ length: 26 }, (_unused, index) => ASCII_LOWER_A + index),
];

/**
 * `unicodedata.lookup` for `\N{…}`, over the block this build carries.
 *
 * CPython resolves every name in the database plus the algorithmic CJK and
 * Hangul names; shipping that costs ~5 MB, so the table here is Latin-1 and
 * General Punctuation — the names a pattern typed at a terminal actually uses.
 * A name outside it is reported undefined, which is CPython's answer for a name
 * that does not exist and a refusal (never a silent mismatch) for one that does.
 * `lookup` itself is case-insensitive, and so is this.
 */
export const lookupCharacterName = (name: string): number | null =>
  CHARACTER_NAMES.get(name.toUpperCase()) ?? null;
