/* The underlying bcrypt (hashing) code is derived from OpenBSD and is
*  subject to the following license: */

/*	$OpenBSD: bcrypt.c,v 1.57 2016/08/26 08:25:02 guenther Exp $	*/

/*
 * Copyright (c) 2014 Ted Unangst <tedu@openbsd.org>
 * Copyright (c) 1997 Niels Provos <provos@umich.edu>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* This password hashing algorithm was designed by David Mazieres
 * <dm@lcs.mit.edu> and works as follows:
 *
 * 1. state := InitState ()
 * 2. state := ExpandKey (state, salt, password)
 * 3. REPEAT rounds:
 *      state := ExpandKey (state, 0, password)
 *	state := ExpandKey (state, 0, salt)
 * 4. ctext := "OrpheanBeholderScryDoubt"
 * 5. REPEAT 64:
 * 	ctext := Encrypt_ECB (state, ctext);
 * 6. RETURN Concatenate (salt, ctext);
 *
 */

/* Notes about this implementation */

/*
 * The NIF functions, which are needed to communicate with Erlang / Elixir,
 * have been implemented by David Whitlock.
 *
 * The `secure_bzero` function was implemented by Jason M Barnes.
 *
 * The `secure_compare` function is taken from the reference C implementation
 * of Argon2, which is copyright (c) 2015 Daniel Dinu, Dmitry Khovratovich (main authors),
 * Jean-Philippe Aumasson and Samuel Neves, and dual licensed under the CC0 License
 * and the Apache 2.0 License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "erl_nif.h"
#include "blf.h"

#define BCRYPT_PREFIX "$2*$"
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#define STR_LEN(str)	  (ARRAY_SIZE(str) - 1)
#define STR_SIZE(str)	 (ARRAY_SIZE(str))

#define ATOM(/*ErlNifEnv **/env, /*const char **/ string) \
	enif_make_atom_len(env, string, STR_LEN(string))

/* This implementation is adaptable to current computing power.
 * You can have up to 2^31 rounds which should be enough for some
 * time to come.
 */

#define BCRYPT_VERSION '2'
#define BCRYPT_MAXSALT 16	/* Precomputation is just so nice */
#define BCRYPT_MAXPASS 256	/* 256, not 73, to replicate behavior for the old 2a prefix */
#define BCRYPT_WORDS 6		/* Ciphertext words */
#define BCRYPT_MINLOGROUNDS 4	/* we have log2(rounds) in salt */
#define BCRYPT_MAXLOGROUNDS 31

#define	BCRYPT_SALTSPACE	(7 + (BCRYPT_MAXSALT * 4 + 2) / 3)
#define	BCRYPT_HASHSPACE	60

static int bcrypt_initsalt(int, uint8_t *, char *, uint8_t);
static int encode_base64(char *, const uint8_t *, size_t);
static int decode_base64(uint8_t *, size_t, const char *);
static void secure_bzero(void *, size_t);
static int secure_compare(const uint8_t *, const uint8_t *, size_t);

static ERL_NIF_TERM bcrypt_gensalt_nif(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
	ErlNifBinary csalt;
	unsigned int log_rounds, minor;

	if (argc != 3 || !enif_inspect_binary(env, argv[0], &csalt) ||
			csalt.size != BCRYPT_MAXSALT ||
			!enif_get_uint(env, argv[1], &log_rounds) ||
			!enif_get_uint(env, argv[2], &minor))
		return enif_make_badarg(env);

	ERL_NIF_TERM output;
	unsigned char *output_data = enif_make_new_binary(env, BCRYPT_SALTSPACE, &output);

	bcrypt_initsalt(log_rounds, (uint8_t *)csalt.data, (char *)output_data, (uint8_t)minor);

	return output;
}

/*
 * Generate a salt.
 */
static int bcrypt_initsalt(int log_rounds, uint8_t *csalt, char *salt, uint8_t minor)
{
	if (log_rounds < BCRYPT_MINLOGROUNDS)
		log_rounds = BCRYPT_MINLOGROUNDS;
	else if (log_rounds > BCRYPT_MAXLOGROUNDS)
		log_rounds = BCRYPT_MAXLOGROUNDS;

	snprintf(salt, BCRYPT_SALTSPACE, "$2%c$%2.2u$", minor, log_rounds);
	encode_base64(salt + 7, csalt, BCRYPT_MAXSALT);

	return 0;
}

/*
 * The core bcrypt function.
 */
static int bcrypt_hashpass(const char *key, size_t key_len, const char *salt, size_t salt_len, char *encrypted, size_t encryptedlen)
{
	blf_ctx state;
	uint32_t rounds, i, k;
	uint16_t j;
	uint8_t logr, minor;
	uint8_t ciphertext[4 * BCRYPT_WORDS] = "OrpheanBeholderScryDoubt";
	uint8_t csalt[BCRYPT_MAXSALT];
	uint32_t cdata[BCRYPT_WORDS];

	if (encryptedlen < BCRYPT_HASHSPACE)
		goto inval;

	/* Check and discard "$" identifier */
	if (salt[0] != '$')
		goto inval;
	salt += 1;

	if (salt[0] != BCRYPT_VERSION)
		goto inval;

	/* Check for minor versions */
	switch ((minor = salt[1])) {
		case 'a':
			key_len = (uint8_t) (key_len + 1);
			break;
		case 'b':
			/* strlen() returns a size_t, but the function calls
			 * below result in implicit casts to a narrower integer
			 * type, so cap key_len at the actual maximum supported
			 * length here to avoid integer wraparound */
			if (key_len > 72)
				key_len = 72;
			key_len++; /* include the NUL */
			break;
		default:
			goto inval;
	}
	if (salt[2] != '$')
		goto inval;
	/* Discard version + "$" identifier */
	salt += 3;

	/* Check and parse num rounds */
	if (!isdigit((unsigned char)salt[0]) ||
			!isdigit((unsigned char)salt[1]) || salt[2] != '$')
		goto inval;
	logr = (salt[1] - '0') + ((salt[0] - '0') * 10);
	if (logr < BCRYPT_MINLOGROUNDS || logr > BCRYPT_MAXLOGROUNDS)
		goto inval;
	/* Computer power doesn't increase linearly, 2^x should be fine */
	rounds = 1U << logr;

	/* Discard num rounds + "$" identifier */
	salt += 3;

	if (strlen(salt) * 3 / 4 < BCRYPT_MAXSALT)
		goto inval;

	/* We dont want the base64 salt but the raw data */
	if (decode_base64(csalt, BCRYPT_MAXSALT, salt))
		goto inval;
	salt_len = BCRYPT_MAXSALT;

	/* Setting up S-Boxes and Subkeys */
	Blowfish_initstate(&state);
	Blowfish_expandstate(&state, csalt, salt_len,
			(uint8_t *)key, key_len);
	for (k = 0; k < rounds; k++) {
		Blowfish_expand0state(&state, (uint8_t *)key, key_len);
		Blowfish_expand0state(&state, csalt, salt_len);
	}

	/* This can be precomputed later */
	j = 0;
	for (i = 0; i < BCRYPT_WORDS; i++)
		cdata[i] = Blowfish_stream2word(ciphertext, 4 * BCRYPT_WORDS, &j);

	/* Now do the encryption */
	for (k = 0; k < 64; k++)
		blf_enc(&state, cdata, BCRYPT_WORDS / 2);

	for (i = 0; i < BCRYPT_WORDS; i++) {
		ciphertext[4 * i + 3] = cdata[i] & 0xff;
		cdata[i] = cdata[i] >> 8;
		ciphertext[4 * i + 2] = cdata[i] & 0xff;
		cdata[i] = cdata[i] >> 8;
		ciphertext[4 * i + 1] = cdata[i] & 0xff;
		cdata[i] = cdata[i] >> 8;
		ciphertext[4 * i + 0] = cdata[i] & 0xff;
	}

	snprintf(encrypted, 8, "$2%c$%2.2u$", minor, logr);
	encode_base64(encrypted + 7, csalt, BCRYPT_MAXSALT);
	encode_base64(encrypted + 7 + 22, ciphertext, 4 * BCRYPT_WORDS - 1);
	secure_bzero(&state, sizeof(state));
	secure_bzero(ciphertext, sizeof(ciphertext));
	secure_bzero(csalt, sizeof(csalt));
	secure_bzero(cdata, sizeof(cdata));
	return 0;

inval:
	return -1;
}

static ERL_NIF_TERM bcrypt_hash_nif(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
	int pass_size;
	ErlNifBinary salt;
	ERL_NIF_TERM output;
	char pass[BCRYPT_MAXPASS];
	char hash[BCRYPT_HASHSPACE];

	if (
		2 == argc
		&& 0 != (pass_size = enif_get_string(env, argv[0], pass, STR_SIZE(pass), ERL_NIF_LATIN1))
		&& enif_inspect_binary(env, argv[1], &salt)
		&& 0 == bcrypt_hashpass((const char *) pass, (size_t) (pass_size < 0 ? STR_SIZE(pass) : (pass_size - 1)), (const char *) salt.data, salt.size, hash, BCRYPT_HASHSPACE)
	) {
		unsigned char *buffer;

		buffer = enif_make_new_binary(env, BCRYPT_HASHSPACE, &output);
		memcpy(buffer, hash, BCRYPT_HASHSPACE);
	} else {
		output = enif_make_badarg(env);
	}

	return output;
}

static ERL_NIF_TERM bcrypt_checkpass_nif(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
	int pass_size;
	ERL_NIF_TERM output;
	ErlNifBinary goodhash;
	char pass[BCRYPT_MAXPASS];

	if (
		2 != argc
		|| 0 == (pass_size = enif_get_string(env, argv[0], pass, STR_SIZE(pass), ERL_NIF_LATIN1))
		|| !enif_inspect_binary(env, argv[1], &goodhash)
	) {
		output = enif_make_badarg(env);
	} else {
		int/*bool*/ match;
		char hash[BCRYPT_HASHSPACE + 1];

		hash[BCRYPT_HASHSPACE] = '\0';
		match = 0 == bcrypt_hashpass((const char *) pass, (size_t) (pass_size < 0 ? STR_SIZE(pass) : (pass_size - 1)), (const char *) goodhash.data, goodhash.size, hash, STR_SIZE(hash))
			&& strlen(hash) == goodhash.size
			&& 0 == secure_compare((const uint8_t *) hash, (const uint8_t *) goodhash.data, goodhash.size)
		;
		secure_bzero(hash, STR_SIZE(hash));
		output = match ? ATOM(env, "true") : ATOM(env, "false");
  }

	return output;
}

static int/*bool*/ bcrypt_valid(const ErlNifBinary *hash)
{
	return hash->size == BCRYPT_HASHSPACE && '$' == hash->data[0] && '2' == hash->data[1] && ('a' == hash->data[2] || 'b' == hash->data[2]);
}

static int/*bool*/ extract_cost_from_hash(const ErlNifBinary *hash, int *cost)
{
	// NOTE: length is checked before by a call to bcrypt_valid
	unsigned const char * const r = hash->data + STR_LEN(BCRYPT_PREFIX);

	if (isdigit(r[0]) && isdigit(r[1])) {
		*cost = (r[1] - '0') + ((r[0] - '0') * 10);
	} else {
		*cost = 0;
	}

	return *cost >= BCRYPT_MINLOGROUNDS && *cost <= BCRYPT_MAXLOGROUNDS;
}

static ERL_NIF_TERM bcrypt_valid_nif(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
	ErlNifBinary hash;
	ERL_NIF_TERM output;

	if (1 != argc || !enif_inspect_binary(env, argv[0], &hash)) {
		output = enif_make_badarg(env);
	} else {
		output = bcrypt_valid(&hash) ? ATOM(env, "true") : ATOM(env, "false");
	}

	return output;
}

enum {
	BCRYPT_OPTIONS_COST,
	_BCRYPT_OPTIONS_COUNT,
};

static ERL_NIF_TERM bcrypt_get_options_nif(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
	int cost;
	ErlNifBinary hash;
	ERL_NIF_TERM output;

	if (1 != argc || !enif_inspect_binary(env, argv[0], &hash)) {
		output = enif_make_badarg(env);
	} else if (bcrypt_valid(&hash) && extract_cost_from_hash(&hash, &cost)) {
		ERL_NIF_TERM options;
		ERL_NIF_TERM pairs[2][_BCRYPT_OPTIONS_COUNT];

		pairs[0][BCRYPT_OPTIONS_COST] = ATOM(env, "cost");
		pairs[1][BCRYPT_OPTIONS_COST] = enif_make_int(env, cost);
		enif_make_map_from_arrays(env, pairs[0], pairs[1], _BCRYPT_OPTIONS_COUNT, &options);

		output = enif_make_tuple2(env, ATOM(env, "ok"), options);
	} else {
		output = enif_make_tuple2(env, ATOM(env, "error"), ATOM(env, "invalid"));
	}

	return output;
}

static ERL_NIF_TERM bcrypt_needs_rehash_nif(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
	int new_cost;
	ErlNifBinary hash;
	ERL_NIF_TERM value, output;

	if (
		2 != argc
		|| !enif_inspect_binary(env, argv[0], &hash)
		|| !enif_is_map(env, argv[1])
		|| !enif_get_map_value(env, argv[1], ATOM(env, "cost"), &value)
		|| !enif_get_int(env, value, &new_cost)
	) {
		output = enif_make_badarg(env);
	} else {
		int old_cost;

		if (bcrypt_valid(&hash) && extract_cost_from_hash(&hash, &old_cost)) {
			// OK, NOP
		} else {
			old_cost = 0;
		}
		output = old_cost != new_cost ? ATOM(env, "true") : ATOM(env, "false");
	}

	return output;
}

/*
 * internal utilities
 */
static const uint8_t Base64Code[] =
"./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

static const uint8_t index_64[128] = {
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 0, 1, 54, 55,
	56, 57, 58, 59, 60, 61, 62, 63, 255, 255,
	255, 255, 255, 255, 255, 2, 3, 4, 5, 6,
	7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
	17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
	255, 255, 255, 255, 255, 255, 28, 29, 30,
	31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
	41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
	51, 52, 53, 255, 255, 255, 255, 255
};
#define CHAR64(c)  ((c) > 127 ? 255 : index_64[(c)])

/*
 * read buflen (after decoding) bytes of data from b64data
 */
static int decode_base64(uint8_t *buffer, size_t len, const char *b64data)
{
	uint8_t *bp = buffer;
	const uint8_t *p = (uint8_t *)b64data;
	uint8_t c1, c2, c3, c4;

	while (bp < buffer + len) {
		c1 = CHAR64(*p);
		/* Invalid data */
		if (c1 == 255)
			return -1;

		c2 = CHAR64(*(p + 1));
		if (c2 == 255)
			return -1;

		*bp++ = (c1 << 2) | ((c2 & 0x30) >> 4);
		if (bp >= buffer + len)
			break;

		c3 = CHAR64(*(p + 2));
		if (c3 == 255)
			return -1;

		*bp++ = ((c2 & 0x0f) << 4) | ((c3 & 0x3c) >> 2);
		if (bp >= buffer + len)
			break;

		c4 = CHAR64(*(p + 3));
		if (c4 == 255)
			return -1;
		*bp++ = ((c3 & 0x03) << 6) | c4;

		p += 4;
	}
	return 0;
}

/*
 * Turn len bytes of data into base64 encoded data.
 * This works without = padding.
 */
static int encode_base64(char *b64buffer, const uint8_t *data, size_t len)
{
	uint8_t *bp = (uint8_t *)b64buffer;
	const uint8_t *p = data;
	uint8_t c1, c2;

	while (p < data + len) {
		c1 = *p++;
		*bp++ = Base64Code[(c1 >> 2)];
		c1 = (c1 & 0x03) << 4;
		if (p >= data + len) {
			*bp++ = Base64Code[c1];
			break;
		}
		c2 = *p++;
		c1 |= (c2 >> 4) & 0x0f;
		*bp++ = Base64Code[c1];
		c1 = (c2 & 0x0f) << 2;
		if (p >= data + len) {
			*bp++ = Base64Code[c1];
			break;
		}
		c2 = *p++;
		c1 |= (c2 >> 6) & 0x03;
		*bp++ = Base64Code[c1];
		*bp++ = Base64Code[c2 & 0x3f];
	}
	return 0;
}

/*
 * Safe memset that will not be optimized away by the compiler.
 */
static void secure_bzero(void *buf, size_t len)
{
	if (buf == NULL || len == 0) {
		return;
	}

	volatile unsigned char *ptr = buf;
	while (len--) {
		*ptr++ = 0;
	}
}

/*
 * Compare the two inputs in constant time in order to prevent timing attacks.
 */
static int secure_compare(const uint8_t *b1, const uint8_t *b2, size_t len)
{
	size_t i;
	uint8_t d = 0U;

	for (i = 0U; i < len; i++) {
		d |= b1[i] ^ b2[i];
	}
	return (int)((1 & ((d - 1) >> 8)) - 1);
}

static ErlNifFunc bcrypt_nif_funcs[] =
{
	{"gensalt_nif", 3, bcrypt_gensalt_nif, 0},
	{"hash_nif", 2, bcrypt_hash_nif, ERL_NIF_DIRTY_JOB_CPU_BOUND},
	{"checkpass_nif", 2, bcrypt_checkpass_nif, ERL_NIF_DIRTY_JOB_CPU_BOUND},
	{"get_options_nif", 1, bcrypt_get_options_nif, 0},
	{"needs_rehash_nif", 2, bcrypt_needs_rehash_nif, 0},
	{"valid_nif", 1, bcrypt_valid_nif, 0},
};

ERL_NIF_INIT(Elixir.Bcrypt.Base, bcrypt_nif_funcs, NULL, NULL, NULL, NULL)
