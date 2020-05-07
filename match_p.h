/*
 * Set match_start to the longest match starting at the given string and
 * return its length. Matches shorter or equal to prev_length are discarded,
 * in which case the result is equal to prev_length and match_start is garbage.
 *
 * IN assertions: cur_match is the head of the hash chain for the current
 * string (strstart) and its distance is <= MAX_DIST, and prev_length >=1
 * OUT assertion: the match length is not greater than s->lookahead
 */

#include "zbuild.h"
#include "deflate.h"

#if (defined(UNALIGNED_OK) && MAX_MATCH == 258)

   /* ARM 32-bit clang/gcc builds perform better, on average, with std2. Both gcc and clang and define __GNUC__. */
#  if defined(__GNUC__) && defined(__arm__) && !defined(__aarch64__)
#    define std2_longest_match
   /* Only use std3_longest_match for little_endian systems, also avoid using it with
      non-gcc compilers since the __builtin_ctzl() function might not be optimized. */
#  elif(defined(__GNUC__) && defined(HAVE_BUILTIN_CTZL) && ((__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) \
        || defined(__LITTLE_ENDIAN__)))
#    define std3_longest_match
#  elif(defined(_MSC_VER) && defined(_WIN32))
#    define std3_longest_match
#  else
#    define std2_longest_match
#  endif

#else
#  define std1_longest_match
#endif


#if defined(_MSC_VER) && !defined(__clang__)
#  if defined(_M_IX86) || defined(_M_AMD64) || defined(_M_IA64) ||  defined(_M_ARM) || defined(_M_ARM64)
#    include "fallback_builtins.h"
#  endif
#endif

extern int32_t compare258_c(const unsigned char *src0, const unsigned char *src1);
#ifdef UNALIGNED_OK
extern int32_t compare258_unaligned_16(const unsigned char *src0, const unsigned char *src1);
extern int32_t compare258_unaligned_32(const unsigned char *src0, const unsigned char *src1);
extern int32_t compare258_unaligned_64(const unsigned char *src0, const unsigned char *src1);
#ifdef X86_SSE42_CMP_STR
extern int32_t compare258_unaligned_sse4(const unsigned char *src0, const unsigned char *src1);
#endif
#endif

#ifdef std1_longest_match

/*
 * Standard longest_match
 *
 */
static inline unsigned longest_match(deflate_state *const s, IPos cur_match) {
    const unsigned wmask = s->w_mask;
    const Pos *prev = s->prev;

    unsigned chain_length;
    IPos limit;
    unsigned int len, best_len, nice_match;
    unsigned char *scan, *match, *strend, scan_end, scan_end1;

    /*
     * The code is optimized for HASH_BITS >= 8 and MAX_MATCH-2 multiple
     * of 16. It is easy to get rid of this optimization if necessary.
     */
    Assert(s->hash_bits >= 8 && MAX_MATCH == 258, "Code too clever");

    /*
     * Do not waste too much time if we already have a good match
     */
    best_len = s->prev_length ? s->prev_length : 1;
    chain_length = s->max_chain_length;
    if (best_len >= s->good_match)
        chain_length >>= 2;

    /*
     * Do not looks for matches beyond the end of the input. This is
     * necessary to make deflate deterministic
     */
    nice_match = (unsigned int)s->nice_match > s->lookahead ? s->lookahead : (unsigned int)s->nice_match;

    /*
     * Stop when cur_match becomes <= limit. To simplify the code,
     * we prevent matches with the string of window index 0
     */
    limit = s->strstart > MAX_DIST(s) ? s->strstart - MAX_DIST(s) : 0;

    scan = s->window + s->strstart;
    strend = s->window + s->strstart + MAX_MATCH;
    scan_end1 = scan[best_len-1];
    scan_end = scan[best_len];

    Assert((unsigned long)s->strstart <= s->window_size - MIN_LOOKAHEAD, "need lookahead");
    do {
        if (cur_match >= s->strstart) {
            break;
        }
        match = s->window + cur_match;

        /*
         * Skip to next match if the match length cannot increase
         * or if the match length is less than 2. Note that the checks
         * below for insufficient lookahead only occur occasionally
         * for performance reasons. Therefore uninitialized memory
         * will be accessed and conditional jumps will be made that
         * depend on those values. However the length of the match
         * is limited to the lookahead, so the output of deflate is not
         * affected by the uninitialized values.
         */
        if (match[best_len] != scan_end ||
            match[best_len-1] != scan_end1 ||
            *match != *scan ||
            match[1] != scan[1])
            continue;

        len = compare258_c(scan, match);
        Assert(scan+len <= window+(unsigned)(s->window_size-1), "wild scan");

        if (len > best_len) {
            s->match_start = cur_match;
            best_len = len;
            if (len >= nice_match)
                break;
            scan_end1 = scan[best_len-1];
            scan_end = scan[best_len];
        } else {
            /*
             * The probability of finding a match later if we here
             * is pretty low, so for performance it's best to
             * outright stop here for the lower compression levels
             */
            if (s->level < TRIGGER_LEVEL)
                break;
        }
    } while ((cur_match = prev[cur_match & wmask]) > limit && --chain_length);

    if ((unsigned int)best_len <= s->lookahead)
        return best_len;
    return s->lookahead;
}
#endif

#ifdef std2_longest_match
/*
 * UNALIGNED_OK longest_match
 *
 */
static inline unsigned longest_match(deflate_state *const s, IPos cur_match) {
    const unsigned wmask = s->w_mask;
    const Pos *prev = s->prev;

    uint16_t scan_start, scan_end;
    unsigned chain_length;
    IPos limit;
    unsigned int len, best_len, nice_match;
    unsigned char *scan, *strend;

    /*
     * The code is optimized for HASH_BITS >= 8 and MAX_MATCH-2 multiple
     * of 16. It is easy to get rid of this optimization if necessary.
     */
    Assert(s->hash_bits >= 8 && MAX_MATCH == 258, "Code too clever");

    /*
     * Do not waste too much time if we already have a good match
     */
    best_len = s->prev_length ? s->prev_length : 1;
    chain_length = s->max_chain_length;
    if (best_len >= s->good_match)
        chain_length >>= 2;

    /*
     * Do not look for matches beyond the end of the input. This is
     * necessary to make deflate deterministic
     */
    nice_match = (unsigned int)s->nice_match > s->lookahead ? s->lookahead : (unsigned int)s->nice_match;

    /*
     * Stop when cur_match becomes <= limit. To simplify the code,
     * we prevent matches with the string of window index 0
     */
    limit = s->strstart > MAX_DIST(s) ? s->strstart - MAX_DIST(s) : 0;

    scan = s->window + s->strstart;
    strend = s->window + s->strstart + MAX_MATCH - 1;
    memcpy(&scan_start, scan, sizeof(scan_start));
    memcpy(&scan_end, scan + best_len - 1, sizeof(scan_end));

    Assert((unsigned long)s->strstart <= s->window_size - MIN_LOOKAHEAD, "need lookahead");
    do {
        unsigned char *match;
        if (cur_match >= s->strstart) {
            break;
        }
        match = s->window + cur_match;

        /*
         * Skip to next match if the match length cannot increase
         * or if the match length is less than 2. Note that the checks
         * below for insufficient lookahead only occur occasionally
         * for performance reasons. Therefore uninitialized memory
         * will be accessed and conditional jumps will be made that
         * depend on those values. However the length of the match
         * is limited to the lookahead, so the output of deflate is not
         * affected by the uninitialized values.
         */
        uint16_t val;
        memcpy(&val, match + best_len - 1, sizeof(val));
        if (LIKELY(val != scan_end))
            continue;

        memcpy(&val, match, sizeof(val));
        if (val != scan_start)
            continue;

        len = compare258_unaligned_16(scan, match);
        Assert(scan+len <= window+(unsigned)(s->window_size-1), "wild scan");

        if (len > best_len) {
            s->match_start = cur_match;
            best_len = len;
            if (len >= nice_match)
                break;
            memcpy(&scan_end, scan + best_len - 1, sizeof(scan_end));
        } else {
            /*
             * The probability of finding a match later if we here
             * is pretty low, so for performance it's best to
             * outright stop here for the lower compression levels
             */
            if (s->level < TRIGGER_LEVEL)
                break;
        }
    } while (--chain_length && (cur_match = prev[cur_match & wmask]) > limit);

    if ((unsigned)best_len <= s->lookahead)
        return best_len;
    return s->lookahead;
}
#endif

#ifdef std3_longest_match
/* longest_match() with minor change to improve performance (in terms of
 * execution time).
 *
 * The pristine longest_match() function is sketched below (strip the
 * then-clause of the "#ifdef UNALIGNED_OK"-directive)
 *
 * ------------------------------------------------------------
 * unsigned int longest_match(...) {
 *    ...
 *    do {
 *        match = s->window + cur_match;                //s0
 *        if (*(ushf*)(match+best_len-1) != scan_end || //s1
 *            *(ushf*)match != scan_start) continue;    //s2
 *        ...
 *
 *        do {
 *        } while (*(ushf*)(scan+=2) == *(ushf*)(match+=2) &&
 *                 *(ushf*)(scan+=2) == *(ushf*)(match+=2) &&
 *                 *(ushf*)(scan+=2) == *(ushf*)(match+=2) &&
 *                 *(ushf*)(scan+=2) == *(ushf*)(match+=2) &&
 *                 scan < strend); //s3
 *
 *        ...
 *    } while(cond); //s4
 *
 * -------------------------------------------------------------
 *
 * The change include:
 *
 *  1) The hottest statements of the function is: s0, s1 and s4. Pull them
 *     together to form a new loop. The benefit is two-fold:
 *
 *    o. Ease the compiler to yield good code layout: the conditional-branch
 *       corresponding to s1 and its biased target s4 become very close (likely,
 *       fit in the same cache-line), hence improving instruction-fetching
 *       efficiency.
 *
 *    o. Ease the compiler to promote "s->window" into register. "s->window"
 *       is loop-invariant; it is supposed to be promoted into register and keep
 *       the value throughout the entire loop. However, there are many such
 *       loop-invariant, and x86-family has small register file; "s->window" is
 *       likely to be chosen as register-allocation victim such that its value
 *       is reloaded from memory in every single iteration. By forming a new loop,
 *       "s->window" is loop-invariant of that newly created tight loop. It is
 *       lot easier for compiler to promote this quantity to register and keep
 *       its value throughout the entire small loop.
 *
 * 2) Transfrom s3 such that it examines sizeof(long)-byte-match at a time.
 *    This is done by:
 *        ------------------------------------------------
 *        v1 = load from "scan" by sizeof(long) bytes
 *        v2 = load from "match" by sizeof(lnog) bytes
 *        v3 = v1 xor v2
 *        match-bit = little-endian-machine(yes-for-x86) ?
 *                     count-trailing-zero(v3) :
 *                     count-leading-zero(v3);
 *
 *        match-byte = match-bit/8
 *
 *        "scan" and "match" advance if necessary
 *       -------------------------------------------------
 */

static inline unsigned longest_match(deflate_state *const s, IPos cur_match) {
    unsigned int strstart = s->strstart;
    unsigned chain_length = s->max_chain_length;/* max hash chain length */
    unsigned char *window = s->window;
    register unsigned char *scan = window + strstart; /* current string */
    register unsigned char *match;                       /* matched string */
    register unsigned int len;                  /* length of current match */
    unsigned int best_len = s->prev_length ? s->prev_length : 1;     /* best match length so far */
    unsigned int nice_match = s->nice_match;    /* stop if match long enough */
    IPos limit = strstart > (IPos)MAX_DIST(s) ?
        strstart - (IPos)MAX_DIST(s) : NIL;
    /* Stop when cur_match becomes <= limit. To simplify the code,
     * we prevent matches with the string of window index 0.
     */
    Pos *prev = s->prev;
    unsigned int wmask = s->w_mask;

    register unsigned char *strend = window + strstart + MAX_MATCH;

    uint16_t scan_start, scan_end;

    memcpy(&scan_start, scan, sizeof(scan_start));
    memcpy(&scan_end, scan+best_len-1, sizeof(scan_end));

    /* The code is optimized for HASH_BITS >= 8 and MAX_MATCH-2 multiple of 16.
     * It is easy to get rid of this optimization if necessary.
     */
    Assert(s->hash_bits >= 8 && MAX_MATCH == 258, "Code too clever");

    /* Do not waste too much time if we already have a good match: */
    if (s->prev_length >= s->good_match) {
        chain_length >>= 2;
    }
    /* Do not look for matches beyond the end of the input. This is necessary
     * to make deflate deterministic.
     */
    if ((unsigned int)nice_match > s->lookahead) nice_match = s->lookahead;

    Assert((unsigned long)strstart <= s->window_size-MIN_LOOKAHEAD, "need lookahead");

    do {
        if (cur_match >= strstart) {
            break;
        }

        /* Skip to next match if the match length cannot increase
         * or if the match length is less than 2.  Note that the checks below
         * for insufficient lookahead only occur occasionally for performance
         * reasons.  Therefore uninitialized memory will be accessed, and
         * conditional jumps will be made that depend on those values.
         * However the length of the match is limited to the lookahead, so
         * the output of deflate is not affected by the uninitialized values.
         */
        int cont = 1;
        do {
            match = window + cur_match;
            if (LIKELY(memcmp(match+best_len-1, &scan_end, sizeof(scan_end)) != 0
                || memcmp(match, &scan_start, sizeof(scan_start)) != 0)) {
                if ((cur_match = prev[cur_match & wmask]) > limit
                    && --chain_length != 0) {
                    continue;
                } else {
                    cont = 0;
                }
            }
            break;
        } while (1);

        if (!cont)
            break;

#ifdef HAVE_BUILTIN_CTZLL
        len = compare258_unaligned_64(scan, match);
#elif defined(HAVE_BUILTIN_CTZ)
        len = compare258_unaligned_32(scan, match);
#endif

        Assert(scan+len <= window+(unsigned)(s->window_size-1), "wild scan");

        if (len > best_len) {
            s->match_start = cur_match;
            best_len = len;
            if (len >= nice_match)
                break;
            memcpy(&scan_end, scan+best_len-1, sizeof(scan_end));
        } else {
            /*
             * The probability of finding a match later if we here
             * is pretty low, so for performance it's best to
             * outright stop here for the lower compression levels
             */
            if (s->level < TRIGGER_LEVEL)
                break;
        }
    } while ((cur_match = prev[cur_match & wmask]) > limit && --chain_length != 0);

    if ((unsigned int)best_len <= s->lookahead)
        return (unsigned int)best_len;
    return s->lookahead;
}
#endif
