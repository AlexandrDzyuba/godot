/**************************************************************************/
/*  pck_packer.cpp                                                        */
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

#include "pck_packer.h"

#include "core/crypto/crypto_core.h"
#include "core/io/file_access.h"
#include "core/io/file_access_encrypted.h"
#include "core/io/file_access_pack.h" // PACK_HEADER_MAGIC, PACK_FORMAT_VERSION
#include "core/object/class_db.h"
#include "core/version.h"

static int _get_pad(int p_alignment, int p_n) {
	int rest = p_n % p_alignment;
	int pad = 0;
	if (rest > 0) {
		pad = p_alignment - rest;
	}

	return pad;
}

void PCKPacker::_bind_methods() {
	ClassDB::bind_method(D_METHOD("pck_start", "pck_path", "alignment", "key", "encrypt_directory", "obfuscate"), &PCKPacker::pck_start, DEFVAL(32), DEFVAL("0000000000000000000000000000000000000000000000000000000000000000"), DEFVAL(false), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("add_file", "target_path", "source_path", "encrypt"), &PCKPacker::add_file, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("add_file_from_buffer", "target_path", "data", "encrypt"), &PCKPacker::add_file_from_buffer, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("add_file_removal", "target_path"), &PCKPacker::add_file_removal);
	ClassDB::bind_method(D_METHOD("flush", "verbose"), &PCKPacker::flush, DEFVAL(false));
}

Error PCKPacker::pck_start(const String &p_pck_path, int p_alignment, const String &p_key, bool p_encrypt_directory, bool p_obfuscate) {
	ERR_FAIL_COND_V_MSG((p_key.is_empty() || !p_key.is_valid_hex_number(false) || p_key.length() != 64), ERR_CANT_CREATE, "Invalid Encryption Key (must be 64 characters long).");
	ERR_FAIL_COND_V_MSG(p_alignment <= 0, ERR_CANT_CREATE, "Invalid alignment, must be greater then 0.");

	String _key = p_key.to_lower();
	key.resize(32);
	for (int i = 0; i < 32; i++) {
		int v = 0;
		if (i * 2 < _key.length()) {
			char32_t ct = _key[i * 2];
			if (is_digit(ct)) {
				ct = ct - '0';
			} else if (ct >= 'a' && ct <= 'f') {
				ct = 10 + ct - 'a';
			}
			v |= ct << 4;
		}

		if (i * 2 + 1 < _key.length()) {
			char32_t ct = _key[i * 2 + 1];
			if (is_digit(ct)) {
				ct = ct - '0';
			} else if (ct >= 'a' && ct <= 'f') {
				ct = 10 + ct - 'a';
			}
			v |= ct;
		}
		key.write[i] = v;
	}
	enc_dir = p_encrypt_directory;
	obfuscated = p_obfuscate;

	file = FileAccess::open(p_pck_path, FileAccess::WRITE);
	ERR_FAIL_COND_V_MSG(file.is_null(), ERR_CANT_CREATE, vformat("Can't open file to write: '%s'.", String(p_pck_path)));

	alignment = p_alignment;

	file->store_32(PACK_HEADER_MAGIC);
	file->store_32(PACK_FORMAT_VERSION);
	file->store_32(GODOT_VERSION_MAJOR);
	file->store_32(GODOT_VERSION_MINOR);
	file->store_32(GODOT_VERSION_PATCH);

	uint32_t pack_flags = PACK_REL_FILEBASE;
	if (enc_dir) {
		pack_flags |= PACK_DIR_ENCRYPTED;
	}
	if (obfuscated) {
		pack_flags |= PACK_OBFUSCATED;
	}
	file->store_32(pack_flags); // flags

	file_base_ofs = file->get_position();
	file->store_64(0); // Files base.

	dir_base_ofs = file->get_position();
	file->store_64(0); // Directory offset.

	for (int i = 0; i < 16; i++) {
		file->store_32(0); // Reserved.
	}

	// Align for first file.
	int pad = _get_pad(alignment, file->get_position());
	for (int i = 0; i < pad; i++) {
		file->store_8(0);
	}

	file_base = file->get_position();
	file->seek(file_base_ofs);
	file->store_64(file_base); // Update files base.
	file->seek(file_base);

	files.clear();

	return OK;
}

Error PCKPacker::add_file_removal(const String &p_target_path) {
	ERR_FAIL_COND_V_MSG(file.is_null(), ERR_INVALID_PARAMETER, "File must be opened before use.");

	File pf;
	// Simplify path here and on every 'files' access so that paths that have extra '/'
	// symbols or 'res://' in them still match the MD5 hash for the saved path.
	pf.path = p_target_path.simplify_path().trim_prefix("res://");
	pf.ofs = file->get_position();
	pf.size = 0;
	pf.removal = true;

	pf.md5.resize_initialized(16);

	files.push_back(pf);

	return OK;
}

Error PCKPacker::add_file(const String &p_target_path, const String &p_source_path, bool p_encrypt) {
	ERR_FAIL_COND_V_MSG(file.is_null(), ERR_INVALID_PARAMETER, "File must be opened before use.");

	Ref<FileAccess> f = FileAccess::open(p_source_path, FileAccess::READ);
	if (f.is_null()) {
		return ERR_FILE_CANT_OPEN;
	}
	Vector<uint8_t> data = FileAccess::get_file_as_bytes(p_source_path);

	return _add_file(p_target_path, p_source_path, data, p_encrypt);
}

Error PCKPacker::add_file_from_buffer(const String &p_target_path, const Vector<uint8_t> &p_data, bool p_encrypt) {
	return _add_file(p_target_path, "<PackedByteArray>", p_data, p_encrypt);
}

Error PCKPacker::_add_file(const String &p_target_path, const String &p_source_path, const Vector<uint8_t> &p_data, bool p_encrypt) {
	ERR_FAIL_COND_V_MSG(file.is_null(), ERR_INVALID_PARAMETER, "File must be opened before use.");

	File pf;
	// Simplify path here and on every 'files' access so that paths that have extra '/'
	// symbols or 'res://' in them still match the MD5 hash for the saved path.
	pf.path = p_target_path.simplify_path().trim_prefix("res://");
	pf.src_path = p_source_path;
	pf.ofs = file->get_position();
	pf.size = p_data.size();

	{
		unsigned char hash[16];
		CryptoCore::md5(p_data.ptr(), p_data.size(), hash);
		pf.md5.resize(16);
		for (int i = 0; i < 16; i++) {
			pf.md5.write[i] = hash[i];
		}
	}
	pf.encrypted = p_encrypt;

	Ref<FileAccess> ftmp = file;

	Ref<FileAccessEncrypted> fae;
	if (p_encrypt) {
		fae.instantiate();
		ERR_FAIL_COND_V(fae.is_null(), ERR_CANT_CREATE);

		Error err = fae->open_and_parse(file, key, FileAccessEncrypted::MODE_WRITE_AES256, false);
		ERR_FAIL_COND_V(err != OK, ERR_CANT_CREATE);
		ftmp = fae;
	}

	if (obfuscated) {
		Vector<uint8_t> transformed_data = p_data;
		pck_obfuscation_transform(transformed_data.ptrw(), transformed_data.size(), pck_obfuscation_path_key(pf.path));
		ftmp->store_buffer(transformed_data);
	} else {
		ftmp->store_buffer(p_data);
	}

	if (fae.is_valid()) {
		ftmp.unref();
		fae.unref();
	}

	int pad = _get_pad(alignment, file->get_position());
	for (int j = 0; j < pad; j++) {
		file->store_8(0);
	}

	files.push_back(pf);

	return OK;
}

Error PCKPacker::flush(bool p_verbose) {
	ERR_FAIL_COND_V_MSG(file.is_null(), ERR_INVALID_PARAMETER, "File must be opened before use.");

	int dir_padding = _get_pad(alignment, file->get_position());
	for (int i = 0; i < dir_padding; i++) {
		file->store_8(0);
	}

	// Write directory.
	uint64_t dir_offset = file->get_position();
	file->seek(dir_base_ofs);
	file->store_64(dir_offset);
	file->seek(dir_offset);

	const uint32_t stored_file_count = obfuscated ? uint32_t(files.size()) ^ uint32_t(pck_obfuscation_mask(PCK_OBFUSCATION_DIRECTORY_DOMAIN)) : uint32_t(files.size());
	file->store_32(stored_file_count);

	Ref<FileAccessEncrypted> fae;
	Ref<FileAccess> fhead = file;

	if (enc_dir) {
		fae.instantiate();
		ERR_FAIL_COND_V(fae.is_null(), ERR_CANT_CREATE);

		Error err = fae->open_and_parse(file, key, FileAccessEncrypted::MODE_WRITE_AES256, false);
		ERR_FAIL_COND_V(err != OK, ERR_CANT_CREATE);

		fhead = fae;
	}

	const int file_num = files.size();
	for (int i = 0; i < file_num; i++) {
		CharString utf8_string = files[i].path.utf8();
		int string_len = utf8_string.length();
		int pad = _get_pad(4, string_len);
		const uint64_t entry_key = PCK_OBFUSCATION_DIRECTORY_DOMAIN + uint64_t(i) * PCK_OBFUSCATION_STREAM_STEP;

		const uint32_t stored_string_len = obfuscated ? uint32_t(string_len + pad) ^ uint32_t(pck_obfuscation_mask(entry_key)) : uint32_t(string_len + pad);
		fhead->store_32(stored_string_len);
		if (obfuscated) {
			Vector<uint8_t> path_data;
			path_data.resize(string_len + pad);
			memcpy(path_data.ptrw(), utf8_string.get_data(), string_len);
			memset(path_data.ptrw() + string_len, 0, pad);
			pck_obfuscation_transform(path_data.ptrw(), path_data.size(), entry_key ^ PCK_OBFUSCATION_PATH_DOMAIN);
			fhead->store_buffer(path_data);
		} else {
			fhead->store_buffer((const uint8_t *)utf8_string.get_data(), string_len);
			for (int j = 0; j < pad; j++) {
				fhead->store_8(0);
			}
		}

		const uint64_t stored_offset = obfuscated ? (files[i].ofs - file_base) ^ pck_obfuscation_mask(entry_key ^ PCK_OBFUSCATION_OFFSET_DOMAIN) : files[i].ofs - file_base;
		const uint64_t stored_size = obfuscated ? files[i].size ^ pck_obfuscation_mask(entry_key ^ PCK_OBFUSCATION_SIZE_DOMAIN) : files[i].size;
		fhead->store_64(stored_offset);
		fhead->store_64(stored_size);
		if (obfuscated) {
			Vector<uint8_t> stored_md5 = files[i].md5;
			pck_obfuscation_transform(stored_md5.ptrw(), stored_md5.size(), entry_key ^ PCK_OBFUSCATION_MD5_DOMAIN);
			fhead->store_buffer(stored_md5);
		} else {
			fhead->store_buffer(files[i].md5.ptr(), 16);
		}

		uint32_t flags = 0;
		if (files[i].encrypted) {
			flags |= PACK_FILE_ENCRYPTED;
		}
		if (files[i].removal) {
			flags |= PACK_FILE_REMOVAL;
		}
		const uint32_t stored_flags = obfuscated ? flags ^ uint32_t(pck_obfuscation_mask(entry_key ^ PCK_OBFUSCATION_FLAGS_DOMAIN)) : flags;
		fhead->store_32(stored_flags);

		if (p_verbose) {
			print_line(vformat("[%d/%d - %d%%] PCKPacker flush: %s -> %s", i + 1, file_num, float(i + 1) / file_num * 100, files[i].src_path, files[i].path));
		}
	}

	if (fae.is_valid()) {
		fhead.unref();
		fae.unref();
	}

	file.unref();
	return OK;
}

PCKPacker::~PCKPacker() {
	if (file.is_valid()) {
		flush();
	}
}
