/**************************************************************************/
/*  gdscript_tokenizer_buffer.cpp                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "gdscript_tokenizer_buffer.h"


#include "core/crypto/crypto_core.h"
#include "core/io/compression.h"
#include "core/io/marshalls.h"

namespace {

static constexpr uint32_t PROTECTED_HEADER_SIZE = 40;
static constexpr uint32_t PROTECTED_FORMAT_VERSION = 1;

// Replace these constants once before compiling your editor and export templates.
static constexpr uint64_t GDSCRIPT_SECRET_0 = 0xB23228B5636973DFULL;
static constexpr uint64_t GDSCRIPT_SECRET_1 = 0xD8E13E7C3EE7AEF8ULL;
static constexpr uint64_t GDSCRIPT_SECRET_2 = 0xFF118AFDD7DA8965ULL;
static constexpr uint64_t GDSCRIPT_SECRET_3 = 0x8BE8BCABA56E1A80ULL;
static constexpr uint64_t GDSCRIPT_SECRET_4 = 0x629C2D291C9376DDULL;
static constexpr uint64_t GDSCRIPT_SECRET_5 = 0x8C24C8EC5034C7A3ULL;
static constexpr uint64_t GDSCRIPT_SECRET_6 = 0x70BA6132CA9CE270ULL;
static constexpr uint64_t GDSCRIPT_SECRET_7 = 0x75780FCA36794ACEULL;

static _FORCE_INLINE_ uint64_t _protected_rotl64(uint64_t p_value, uint32_t p_shift) {
	return (p_value << p_shift) | (p_value >> (64 - p_shift));
}

static _FORCE_INLINE_ uint64_t _protected_mix64(uint64_t p_value) {
	p_value ^= p_value >> 30;
	p_value *= 0xBF58476D1CE4E5B9ULL;
	p_value ^= p_value >> 27;
	p_value *= 0x94D049BB133111EBULL;
	p_value ^= p_value >> 31;
	return p_value;
}

static uint32_t _protected_gcd(uint32_t p_a, uint32_t p_b) {
	while (p_b != 0) {
		const uint32_t remainder = p_a % p_b;
		p_a = p_b;
		p_b = remainder;
	}
	return p_a;
}

static uint32_t _protected_mod_inverse(uint32_t p_value, uint32_t p_modulo) {
	int64_t old_t = 0;
	int64_t new_t = 1;
	int64_t old_r = p_modulo;
	int64_t new_r = p_value;

	while (new_r != 0) {
		const int64_t quotient = old_r / new_r;
		const int64_t next_t = old_t - quotient * new_t;
		const int64_t next_r = old_r - quotient * new_r;
		old_t = new_t;
		new_t = next_t;
		old_r = new_r;
		new_r = next_r;
	}

	if (old_t < 0) {
		old_t += p_modulo;
	}
	return uint32_t(old_t);
}

static uint32_t _protected_token_multiplier(uint64_t p_seed) {
	const uint32_t modulo = uint32_t(GDScriptTokenizer::Token::TK_MAX) - 1;
	uint32_t multiplier = uint32_t(_protected_mix64(p_seed ^ GDSCRIPT_SECRET_6) % modulo);
	if (multiplier == 0) {
		multiplier = 1;
	}
	while (_protected_gcd(multiplier, modulo) != 1) {
		multiplier++;
		if (multiplier >= modulo) {
			multiplier = 1;
		}
	}
	return multiplier;
}

static uint32_t _protected_token_offset(uint64_t p_seed) {
	return uint32_t(_protected_mix64(p_seed ^ GDSCRIPT_SECRET_7) % (uint32_t(GDScriptTokenizer::Token::TK_MAX) - 1));
}

static uint32_t _protected_encode_token(uint32_t p_type, uint32_t p_multiplier, uint32_t p_offset) {
	if (p_type == 0) {
		return 0;
	}
	const uint32_t modulo = uint32_t(GDScriptTokenizer::Token::TK_MAX) - 1;
	return (((p_type - 1) * p_multiplier + p_offset) % modulo) + 1;
}

static uint32_t _protected_decode_token(uint32_t p_type, uint32_t p_inverse_multiplier, uint32_t p_offset) {
	if (p_type == 0) {
		return 0;
	}
	const uint32_t modulo = uint32_t(GDScriptTokenizer::Token::TK_MAX) - 1;
	return ((((p_type - 1 + modulo - p_offset) % modulo) * p_inverse_multiplier) % modulo) + 1;
}

static uint64_t _protected_effective_seed(uint64_t p_build_seed, uint64_t p_file_nonce) {
	return _protected_mix64(p_build_seed ^ _protected_rotl64(p_file_nonce, 23) ^ GDSCRIPT_SECRET_2);
}

static void _protected_derive_crypto(uint64_t p_build_seed, uint64_t p_file_nonce, uint8_t r_key[32], uint8_t r_iv[16], uint64_t &r_token_seed) {
	const uint64_t effective_seed = _protected_effective_seed(p_build_seed, p_file_nonce);
	encode_uint64(_protected_mix64(effective_seed ^ GDSCRIPT_SECRET_3), &r_key[0]);
	encode_uint64(_protected_mix64(p_build_seed + GDSCRIPT_SECRET_4), &r_key[8]);
	encode_uint64(_protected_mix64(p_file_nonce ^ GDSCRIPT_SECRET_5), &r_key[16]);
	encode_uint64(_protected_mix64(effective_seed + _protected_rotl64(p_build_seed, 11) + _protected_rotl64(p_file_nonce, 37)), &r_key[24]);
	encode_uint64(_protected_mix64(effective_seed ^ GDSCRIPT_SECRET_0), &r_iv[0]);
	encode_uint64(_protected_mix64(_protected_rotl64(effective_seed, 29) ^ GDSCRIPT_SECRET_1), &r_iv[8]);
	r_token_seed = _protected_mix64(effective_seed ^ _protected_rotl64(p_build_seed, 17) ^ _protected_rotl64(p_file_nonce, 41));
}

static Error _protected_hash_payload(const Vector<uint8_t> &p_payload, uint64_t &r_hash) {
	uint8_t digest[32];
	const Error err = CryptoCore::sha256(p_payload.ptr(), p_payload.size(), digest);
	ERR_FAIL_COND_V(err != OK, err);
	r_hash = decode_uint64(&digest[0]) ^ decode_uint64(&digest[8]) ^ decode_uint64(&digest[16]) ^ decode_uint64(&digest[24]);
	return OK;
}

static Error _protected_crypt_payload(const Vector<uint8_t> &p_source, Vector<uint8_t> &r_destination, const uint8_t p_key[32], const uint8_t p_iv[16], bool p_decrypt) {
	r_destination.resize(p_source.size());
	if (p_source.is_empty()) {
		return OK;
	}

	uint8_t iv[16];
	for (uint32_t i = 0; i < 16; i++) {
		iv[i] = p_iv[i];
	}

	CryptoCore::AESContext aes;
	Error err = aes.set_encode_key(p_key, 256);
	ERR_FAIL_COND_V(err != OK, err);
	if (p_decrypt) {
		return aes.decrypt_cfb(p_source.size(), iv, p_source.ptr(), r_destination.ptrw());
	}
	return aes.encrypt_cfb(p_source.size(), iv, p_source.ptr(), r_destination.ptrw());
}

} // namespace

int GDScriptTokenizerBuffer::_token_to_binary(const Token &p_token, Vector<uint8_t> &r_buffer, int p_start, HashMap<StringName, uint32_t> &r_identifiers_map, HashMap<Variant, uint32_t> &r_constants_map, uint32_t p_token_multiplier, uint32_t p_token_offset) {
	int pos = p_start;

	int token_type = _protected_encode_token(uint32_t(p_token.type), p_token_multiplier, p_token_offset);

	switch (p_token.type) {
		case GDScriptTokenizer::Token::ANNOTATION:
		case GDScriptTokenizer::Token::IDENTIFIER: {
			// Add identifier to map.
			int identifier_pos;
			StringName id = p_token.get_identifier();
			if (r_identifiers_map.has(id)) {
				identifier_pos = r_identifiers_map[id];
			} else {
				identifier_pos = r_identifiers_map.size();
				r_identifiers_map[id] = identifier_pos;
			}
			token_type |= identifier_pos << TOKEN_BITS;
		} break;
		case GDScriptTokenizer::Token::ERROR:
		case GDScriptTokenizer::Token::LITERAL: {
			// Add literal to map.
			int constant_pos;
			if (r_constants_map.has(p_token.literal)) {
				constant_pos = r_constants_map[p_token.literal];
			} else {
				constant_pos = r_constants_map.size();
				r_constants_map[p_token.literal] = constant_pos;
			}
			token_type |= constant_pos << TOKEN_BITS;
		} break;
		default:
			break;
	}

	// Encode token.
	int token_len;
	if (token_type & TOKEN_MASK) {
		token_len = 8;
		r_buffer.resize(pos + token_len);
		encode_uint32(token_type | TOKEN_BYTE_MASK, &r_buffer.write[pos]);
		pos += 4;
	} else {
		token_len = 5;
		r_buffer.resize(pos + token_len);
		r_buffer.write[pos] = token_type;
		pos++;
	}
	encode_uint32(p_token.start_line, &r_buffer.write[pos]);
	return token_len;
}

GDScriptTokenizer::Token GDScriptTokenizerBuffer::_binary_to_token(const uint8_t *p_buffer) {
	Token token;
	const uint8_t *b = p_buffer;

	uint32_t token_type = decode_uint32(b);
	token.type = (Token::Type)_protected_decode_token(token_type & TOKEN_MASK, token_decode_multiplier, token_decode_offset);
	if (token_type & TOKEN_BYTE_MASK) {
		b += 4;
	} else {
		b++;
	}
	token.start_line = decode_uint32(b);
	token.end_line = token.start_line;

	token.literal = token.get_name();
	if (token.type == Token::CONST_NAN) {
		token.literal = String("NAN"); // Special case since name and notation are different.
	}

	switch (token.type) {
		case GDScriptTokenizer::Token::ANNOTATION:
		case GDScriptTokenizer::Token::IDENTIFIER: {
			// Get name from map.
			int identifier_pos = token_type >> TOKEN_BITS;
			if (unlikely(identifier_pos >= identifiers.size())) {
				Token error;
				error.type = Token::ERROR;
				error.literal = "Identifier index out of bounds.";
				return error;
			}
			token.literal = identifiers[identifier_pos];
		} break;
		case GDScriptTokenizer::Token::ERROR:
		case GDScriptTokenizer::Token::LITERAL: {
			// Get literal from map.
			int constant_pos = token_type >> TOKEN_BITS;
			if (unlikely(constant_pos >= constants.size())) {
				Token error;
				error.type = Token::ERROR;
				error.literal = "Constant index out of bounds.";
				return error;
			}
			token.literal = constants[constant_pos];
		} break;
		default:
			break;
	}

	return token;
}

Error GDScriptTokenizerBuffer::set_code_buffer(const Vector<uint8_t> &p_buffer) {
	const bool standard_format = p_buffer.size() >= 12 && p_buffer[0] == 'G' && p_buffer[1] == 'D' && p_buffer[2] == 'S' && p_buffer[3] == 'C';
	const bool protected_format = p_buffer.size() >= PROTECTED_HEADER_SIZE && p_buffer[0] == 'G' && p_buffer[1] == 'D' && p_buffer[2] == 'X' && p_buffer[3] == 'C';
	ERR_FAIL_COND_V(!standard_format && !protected_format, ERR_INVALID_DATA);

	token_decode_multiplier = 1;
	token_decode_offset = 0;
	identifiers.clear();
	constants.clear();
	token_lines.clear();
	token_columns.clear();
	tokens.clear();
	current = 0;
	current_line = 1;

	uint32_t decompressed_size = 0;
	Vector<uint8_t> payload;

	if (standard_format) {
		const int version = decode_uint32(&p_buffer[4]);
		ERR_FAIL_COND_V_MSG(version != TOKENIZER_VERSION, ERR_INVALID_DATA, "Binary GDScript is not compatible with this engine version.");
		decompressed_size = decode_uint32(&p_buffer[8]);
		payload = p_buffer.slice(12);
	} else {
		const uint64_t build_seed = decode_uint64(&p_buffer[16]) ^ GDSCRIPT_SECRET_0;
		const uint64_t file_nonce = decode_uint64(&p_buffer[24]) ^ _protected_rotl64(build_seed, 17) ^ GDSCRIPT_SECRET_1;
		const uint64_t effective_seed = _protected_effective_seed(build_seed, file_nonce);
		const uint32_t version = decode_uint32(&p_buffer[4]) ^ uint32_t(_protected_mix64(effective_seed ^ GDSCRIPT_SECRET_4));
		decompressed_size = decode_uint32(&p_buffer[8]) ^ uint32_t(_protected_mix64(effective_seed ^ GDSCRIPT_SECRET_5) >> 32);
		const uint32_t format_version = decode_uint32(&p_buffer[12]) ^ uint32_t(_protected_mix64(effective_seed ^ GDSCRIPT_SECRET_6));

		ERR_FAIL_COND_V_MSG(format_version != PROTECTED_FORMAT_VERSION, ERR_INVALID_DATA, "Unsupported protected GDScript format.");
		ERR_FAIL_COND_V_MSG(version != TOKENIZER_VERSION, ERR_INVALID_DATA, "Protected binary GDScript is not compatible with this engine version.");

		uint8_t key[32];
		uint8_t iv[16];
		uint64_t token_seed = 0;
		_protected_derive_crypto(build_seed, file_nonce, key, iv, token_seed);

		const uint32_t token_multiplier = _protected_token_multiplier(token_seed);
		token_decode_multiplier = _protected_mod_inverse(token_multiplier, uint32_t(Token::TK_MAX) - 1);
		token_decode_offset = _protected_token_offset(token_seed);

		const Vector<uint8_t> encrypted_payload = p_buffer.slice(PROTECTED_HEADER_SIZE);
		const Error decrypt_error = _protected_crypt_payload(encrypted_payload, payload, key, iv, true);
		ERR_FAIL_COND_V_MSG(decrypt_error != OK, ERR_INVALID_DATA, "Error decrypting protected GDScript tokenizer buffer.");

		uint64_t payload_hash = 0;
		const Error hash_error = _protected_hash_payload(payload, payload_hash);
		ERR_FAIL_COND_V(hash_error != OK, hash_error);
		const uint64_t expected_hash = decode_uint64(&p_buffer[32]) ^ _protected_mix64(effective_seed ^ GDSCRIPT_SECRET_7);
		ERR_FAIL_COND_V_MSG(payload_hash != expected_hash, ERR_INVALID_DATA, "Protected GDScript payload checksum mismatch.");
	}

	Vector<uint8_t> contents;
	if (decompressed_size == 0) {
		contents = payload;
	} else {
		ERR_FAIL_COND_V(payload.is_empty(), ERR_INVALID_DATA);
		contents.resize(decompressed_size);
		const int64_t result = Compression::decompress(contents.ptrw(), contents.size(), payload.ptr(), payload.size(), Compression::MODE_ZSTD);
		ERR_FAIL_COND_V_MSG(result != decompressed_size, ERR_INVALID_DATA, "Error decompressing GDScript tokenizer buffer.");
	}

	ERR_FAIL_COND_V(contents.size() < 16, ERR_INVALID_DATA);
	int total_len = contents.size();
	const uint8_t *buf = contents.ptr();
	uint32_t identifier_count = decode_uint32(&buf[0]);
	uint32_t constant_count = decode_uint32(&buf[4]);
	uint32_t token_line_count = decode_uint32(&buf[8]);
	uint32_t token_count = decode_uint32(&buf[12]);

	const uint8_t *b = &buf[16];
	total_len -= 16;

	identifiers.resize(identifier_count);
	for (uint32_t i = 0; i < identifier_count; i++) {
		ERR_FAIL_COND_V(total_len < 4, ERR_INVALID_DATA);
		uint32_t len = decode_uint32(b);
		total_len -= 4;
		ERR_FAIL_COND_V((len * 4u) > (uint32_t)total_len, ERR_INVALID_DATA);
		b += 4;
		Vector<uint32_t> cs;
		cs.resize(len);
		for (uint32_t j = 0; j < len; j++) {
			uint8_t tmp[4];
			for (uint32_t k = 0; k < 4; k++) {
				tmp[k] = b[j * 4 + k] ^ 0xb6;
			}
			cs.write[j] = decode_uint32(tmp);
		}

		String s = String::utf32(Span(reinterpret_cast<const char32_t *>(cs.ptr()), len));
		b += len * 4;
		total_len -= len * 4;
		identifiers.write[i] = s;
	}

	constants.resize(constant_count);
	for (uint32_t i = 0; i < constant_count; i++) {
		Variant v;
		int len;
		Error err = decode_variant(v, b, total_len, &len, false);
		if (err) {
			return err;
		}
		b += len;
		total_len -= len;
		constants.write[i] = v;
	}

	for (uint32_t i = 0; i < token_line_count; i++) {
		ERR_FAIL_COND_V(total_len < 8, ERR_INVALID_DATA);
		uint32_t token_index = decode_uint32(b);
		b += 4;
		uint32_t line = decode_uint32(b);
		b += 4;
		total_len -= 8;
		token_lines[token_index] = line;
	}
	for (uint32_t i = 0; i < token_line_count; i++) {
		ERR_FAIL_COND_V(total_len < 8, ERR_INVALID_DATA);
		uint32_t token_index = decode_uint32(b);
		b += 4;
		uint32_t column = decode_uint32(b);
		b += 4;
		total_len -= 8;
		token_columns[token_index] = column;
	}

	tokens.resize(token_count);
	for (uint32_t i = 0; i < token_count; i++) {
		ERR_FAIL_COND_V(total_len < 1, ERR_INVALID_DATA);
		int token_len = 5;
		if ((*b) & TOKEN_BYTE_MASK) {
			token_len = 8;
		}
		ERR_FAIL_COND_V(total_len < token_len, ERR_INVALID_DATA);
		Token token = _binary_to_token(b);
		b += token_len;
		ERR_FAIL_INDEX_V(token.type, Token::TK_MAX, ERR_INVALID_DATA);
		tokens.write[i] = token;
		total_len -= token_len;
	}

	ERR_FAIL_COND_V(total_len > 0, ERR_INVALID_DATA);
	return OK;
}

Vector<uint8_t> GDScriptTokenizerBuffer::parse_code_string(const String &p_code, CompressMode p_compress_mode, uint64_t p_build_seed, uint64_t p_file_nonce) {
	const bool protected_format = p_build_seed != 0 || p_file_nonce != 0;
	ERR_FAIL_COND_V_MSG(protected_format && (p_build_seed == 0 || p_file_nonce == 0), Vector<uint8_t>(), "Protected GDScript requires both a build seed and file nonce.");

	uint8_t key[32] = {};
	uint8_t iv[16] = {};
	uint64_t token_seed = 0;
	uint32_t token_multiplier = 1;
	uint32_t token_offset = 0;
	if (protected_format) {
		_protected_derive_crypto(p_build_seed, p_file_nonce, key, iv, token_seed);
		token_multiplier = _protected_token_multiplier(token_seed);
		token_offset = _protected_token_offset(token_seed);
	}

	HashMap<StringName, uint32_t> identifier_map;
	HashMap<Variant, uint32_t> constant_map;
	Vector<uint8_t> token_buffer;
	HashMap<uint32_t, uint32_t> token_lines;
	HashMap<uint32_t, uint32_t> token_columns;

	GDScriptTokenizerText tokenizer;
	tokenizer.set_source_code(p_code);
	tokenizer.set_multiline_mode(true); // Ignore whitespace tokens.
	Token current = tokenizer.scan();
	int token_pos = 0;
	int last_token_line = 0;
	int token_counter = 0;

	while (current.type != Token::TK_EOF) {
		int token_len = _token_to_binary(current, token_buffer, token_pos, identifier_map, constant_map, token_multiplier, token_offset);
		token_pos += token_len;
		if (token_counter > 0 && current.start_line > last_token_line) {
			token_lines[token_counter] = current.start_line;
			token_columns[token_counter] = current.start_column;
		}
		last_token_line = current.end_line;

		current = tokenizer.scan();
		token_counter++;
	}

	Vector<StringName> rev_identifier_map;
	rev_identifier_map.resize(identifier_map.size());
	for (const KeyValue<StringName, uint32_t> &E : identifier_map) {
		rev_identifier_map.write[E.value] = E.key;
	}
	Vector<Variant> rev_constant_map;
	rev_constant_map.resize(constant_map.size());
	for (const KeyValue<Variant, uint32_t> &E : constant_map) {
		rev_constant_map.write[E.value] = E.key;
	}
	HashMap<uint32_t, uint32_t> rev_token_lines;
	for (const KeyValue<uint32_t, uint32_t> &E : token_lines) {
		rev_token_lines[E.value] = E.key;
	}

	for (int line : tokenizer.get_continuation_lines()) {
		if (rev_token_lines.has(line)) {
			token_lines.erase(rev_token_lines[line]);
			token_columns.erase(rev_token_lines[line]);
		}
	}

	Vector<uint8_t> contents;
	contents.resize(16);
	encode_uint32(identifier_map.size(), &contents.write[0]);
	encode_uint32(constant_map.size(), &contents.write[4]);
	encode_uint32(token_lines.size(), &contents.write[8]);
	encode_uint32(token_counter, &contents.write[12]);

	int buf_pos = 16;

	for (const StringName &id : rev_identifier_map) {
		String s = id.operator String();
		int len = s.length();

		contents.resize(buf_pos + (len + 1) * 4);
		encode_uint32(len, &contents.write[buf_pos]);
		buf_pos += 4;

		for (int i = 0; i < len; i++) {
			uint8_t tmp[4];
			encode_uint32(s[i], tmp);
			for (int b = 0; b < 4; b++) {
				contents.write[buf_pos + b] = tmp[b] ^ 0xb6;
			}
			buf_pos += 4;
		}
	}

	for (const Variant &v : rev_constant_map) {
		int len;
		Error err = encode_variant(v, nullptr, len, false);
		ERR_FAIL_COND_V_MSG(err != OK, Vector<uint8_t>(), "Error when trying to encode Variant.");
		contents.resize(buf_pos + len);
		encode_variant(v, &contents.write[buf_pos], len, false);
		buf_pos += len;
	}

	contents.resize(buf_pos + token_lines.size() * 16);
	for (const KeyValue<uint32_t, uint32_t> &e : token_lines) {
		encode_uint32(e.key, &contents.write[buf_pos]);
		buf_pos += 4;
		encode_uint32(e.value, &contents.write[buf_pos]);
		buf_pos += 4;
	}
	for (const KeyValue<uint32_t, uint32_t> &e : token_columns) {
		encode_uint32(e.key, &contents.write[buf_pos]);
		buf_pos += 4;
		encode_uint32(e.value, &contents.write[buf_pos]);
		buf_pos += 4;
	}

	contents.append_array(token_buffer);

	Vector<uint8_t> payload;
	uint32_t decompressed_size = 0;
	switch (p_compress_mode) {
		case COMPRESS_NONE:
			payload = contents;
			break;
		case COMPRESS_ZSTD: {
			decompressed_size = contents.size();
			Vector<uint8_t> compressed;
			const int64_t max_size = Compression::get_max_compressed_buffer_size(contents.size(), Compression::MODE_ZSTD);
			compressed.resize(max_size);
			const int64_t compressed_size = Compression::compress(compressed.ptrw(), contents.ptr(), contents.size(), Compression::MODE_ZSTD);
			ERR_FAIL_COND_V_MSG(compressed_size < 0, Vector<uint8_t>(), "Error compressing GDScript tokenizer buffer.");
			compressed.resize(compressed_size);
			payload = compressed;
		} break;
	}

	Vector<uint8_t> buf;
	if (!protected_format) {
		buf.resize(12);
		buf.write[0] = 'G';
		buf.write[1] = 'D';
		buf.write[2] = 'S';
		buf.write[3] = 'C';
		encode_uint32(TOKENIZER_VERSION, &buf.write[4]);
		encode_uint32(decompressed_size, &buf.write[8]);
		buf.append_array(payload);
		return buf;
	}

	uint64_t payload_hash = 0;
	const Error hash_error = _protected_hash_payload(payload, payload_hash);
	ERR_FAIL_COND_V(hash_error != OK, Vector<uint8_t>());

	Vector<uint8_t> encrypted_payload;
	const Error encryption_error = _protected_crypt_payload(payload, encrypted_payload, key, iv, false);
	ERR_FAIL_COND_V_MSG(encryption_error != OK, Vector<uint8_t>(), "Error encrypting protected GDScript tokenizer buffer.");

	const uint64_t effective_seed = _protected_effective_seed(p_build_seed, p_file_nonce);
	buf.resize(PROTECTED_HEADER_SIZE);
	buf.write[0] = 'G';
	buf.write[1] = 'D';
	buf.write[2] = 'X';
	buf.write[3] = 'C';
	encode_uint32(TOKENIZER_VERSION ^ uint32_t(_protected_mix64(effective_seed ^ GDSCRIPT_SECRET_4)), &buf.write[4]);
	encode_uint32(decompressed_size ^ uint32_t(_protected_mix64(effective_seed ^ GDSCRIPT_SECRET_5) >> 32), &buf.write[8]);
	encode_uint32(PROTECTED_FORMAT_VERSION ^ uint32_t(_protected_mix64(effective_seed ^ GDSCRIPT_SECRET_6)), &buf.write[12]);
	encode_uint64(p_build_seed ^ GDSCRIPT_SECRET_0, &buf.write[16]);
	encode_uint64(p_file_nonce ^ _protected_rotl64(p_build_seed, 17) ^ GDSCRIPT_SECRET_1, &buf.write[24]);
	encode_uint64(payload_hash ^ _protected_mix64(effective_seed ^ GDSCRIPT_SECRET_7), &buf.write[32]);
	buf.append_array(encrypted_payload);
	return buf;
}

int GDScriptTokenizerBuffer::get_cursor_line() const {
	return 0;
}

int GDScriptTokenizerBuffer::get_cursor_column() const {
	return 0;
}

void GDScriptTokenizerBuffer::set_cursor_position(int p_line, int p_column) {
}

void GDScriptTokenizerBuffer::set_multiline_mode(bool p_state) {
	multiline_mode = p_state;
}

bool GDScriptTokenizerBuffer::is_past_cursor() const {
	return false;
}

void GDScriptTokenizerBuffer::push_expression_indented_block() {
	indent_stack_stack.push_back(indent_stack);
}

void GDScriptTokenizerBuffer::pop_expression_indented_block() {
	ERR_FAIL_COND(indent_stack_stack.is_empty());
	indent_stack = indent_stack_stack.back()->get();
	indent_stack_stack.pop_back();
}

GDScriptTokenizer::Token GDScriptTokenizerBuffer::scan() {
	// Add final newline.
	if (current >= tokens.size() && !last_token_was_newline) {
		Token newline;
		newline.type = Token::NEWLINE;
		newline.start_line = current_line;
		newline.end_line = current_line;
		last_token_was_newline = true;
		return newline;
	}

	// Resolve pending indentation change.
	if (pending_indents > 0) {
		pending_indents--;
		Token indent;
		indent.type = Token::INDENT;
		indent.start_line = current_line;
		indent.end_line = current_line;
		return indent;
	} else if (pending_indents < 0) {
		pending_indents++;
		Token dedent;
		dedent.type = Token::DEDENT;
		dedent.start_line = current_line;
		dedent.end_line = current_line;
		return dedent;
	}

	if (current >= tokens.size()) {
		if (!indent_stack.is_empty()) {
			pending_indents -= indent_stack.size();
			indent_stack.clear();
			return scan();
		}
		Token eof;
		eof.type = Token::TK_EOF;
		eof.start_line = current_line;
		eof.end_line = current_line;
		return eof;
	};

	if (!last_token_was_newline && token_lines.has(current)) {
		current_line = token_lines[current];
		uint32_t current_column = token_columns[current];

		// Check if there's a need to indent/dedent.
		if (!multiline_mode) {
			uint32_t previous_indent = 0;
			if (!indent_stack.is_empty()) {
				previous_indent = indent_stack.back()->get();
			}
			if (current_column - 1 > previous_indent) {
				pending_indents++;
				indent_stack.push_back(current_column - 1);
			} else {
				while (current_column - 1 < previous_indent) {
					pending_indents--;
					indent_stack.pop_back();
					if (indent_stack.is_empty()) {
						break;
					}
					previous_indent = indent_stack.back()->get();
				}
			}

			Token newline;
			newline.type = Token::NEWLINE;
			newline.start_line = current_line;
			newline.end_line = current_line;
			last_token_was_newline = true;

			return newline;
		}
	}

	last_token_was_newline = false;

	Token token = tokens[current++];
	return token;
}
