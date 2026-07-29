/**************************************************************************/
/*  test_pck_packer.cpp                                                   */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_pck_packer)

#include "core/io/file_access.h"
#include "core/io/file_access_pack.h"
#include "core/io/pck_packer.h"
#include "core/os/os.h"
#include "tests/test_utils.h"

namespace TestPCKPacker {

TEST_CASE("[PCKPacker] Pack an empty PCK file") {
	PCKPacker pck_packer;
	const String output_pck_path = TestUtils::get_temp_path("output_empty.pck");
	CHECK_MESSAGE(
			pck_packer.pck_start(output_pck_path) == OK,
			"Starting a PCK file should return an OK error code.");

	CHECK_MESSAGE(
			pck_packer.flush() == OK,
			"Flushing the PCK should return an OK error code.");

	Error err;
	Ref<FileAccess> f = FileAccess::open(output_pck_path, FileAccess::READ, &err);
	CHECK_MESSAGE(
			err == OK,
			"The generated empty PCK file should be opened successfully.");
	CHECK_MESSAGE(
			f->get_length() >= 100,
			"The generated empty PCK file shouldn't be too small (it should have the PCK header).");
	CHECK_MESSAGE(
			f->get_length() <= 500,
			"The generated empty PCK file shouldn't be too large.");
}

TEST_CASE("[PCKPacker] Pack empty with zero alignment invalid") {
	PCKPacker pck_packer;
	const String output_pck_path = TestUtils::get_temp_path("output_empty.pck");
	ERR_PRINT_OFF;
	CHECK_MESSAGE(pck_packer.pck_start(output_pck_path, 0) != OK, "PCK with zero alignment should fail.");
	ERR_PRINT_ON;
}

TEST_CASE("[PCKPacker] Pack empty with invalid key") {
	PCKPacker pck_packer;
	const String output_pck_path = TestUtils::get_temp_path("output_empty.pck");
	ERR_PRINT_OFF;
	CHECK_MESSAGE(pck_packer.pck_start(output_pck_path, 32, "") != OK, "PCK with invalid key should fail.");
	ERR_PRINT_ON;
}

TEST_CASE("[PCKPacker] Pack a PCK file with some files and directories") {
	PCKPacker pck_packer;
	const String output_pck_path = TestUtils::get_temp_path("output_with_files.pck");
	CHECK_MESSAGE(
			pck_packer.pck_start(output_pck_path) == OK,
			"Starting a PCK file should return an OK error code.");

	const String base_dir = OS::get_singleton()->get_executable_path().get_base_dir();

	CHECK_MESSAGE(
			pck_packer.add_file("version.py", base_dir.path_join("../version.py"), "version.py") == OK,
			"Adding a file to the PCK should return an OK error code.");
	CHECK_MESSAGE(
			pck_packer.add_file("some/directories with spaces/to/create/icon.png", base_dir.path_join("../misc/logo/icon.png")) == OK,
			"Adding a file to a new subdirectory in the PCK should return an OK error code.");
	CHECK_MESSAGE(
			pck_packer.add_file("some/directories with spaces/to/create/icon.svg", base_dir.path_join("../misc/logo/icon.svg")) == OK,
			"Adding a file to an existing subdirectory in the PCK should return an OK error code.");
	CHECK_MESSAGE(
			pck_packer.add_file("some/directories with spaces/to/create/icon.png", base_dir.path_join("../misc/logo/logo.png")) == OK,
			"Overriding a non-flushed file to an existing subdirectory in the PCK should return an OK error code.");
	CHECK_MESSAGE(
			pck_packer.add_file_from_buffer("buffer/new.txt", String("Hello world!").to_utf8_buffer()) == OK,
			"Adding a file from a buffer to the PCK in a new subdirectory should return an OK error code.");
	CHECK_MESSAGE(
			pck_packer.flush() == OK,
			"Flushing the PCK should return an OK error code.");

	Error err;
	Ref<FileAccess> f = FileAccess::open(output_pck_path, FileAccess::READ, &err);
	CHECK_MESSAGE(
			err == OK,
			"The generated non-empty PCK file should be opened successfully.");
	CHECK_MESSAGE(
			f->get_length() >= 18000,
			"The generated non-empty PCK file should be large enough to actually hold the contents specified above.");
	CHECK_MESSAGE(
			f->get_length() <= 27000,
			"The generated non-empty PCK file shouldn't be too large.");
}

TEST_CASE("[PCKPacker] Obfuscated PCK hides metadata and supports random-access reads") {
	PCKPacker pck_packer;
	const String output_pck_path = TestUtils::get_temp_path("output_obfuscated.pck");
	const String packed_path = "private/config/obfuscation_test.txt";
	const Vector<uint8_t> original_data = String("0123456789: this payload must not be stored as plaintext.").to_utf8_buffer();
	const String zero_key = "0000000000000000000000000000000000000000000000000000000000000000";

	REQUIRE(pck_packer.pck_start(output_pck_path, 32, zero_key, false, true) == OK);
	REQUIRE(pck_packer.add_file_from_buffer(packed_path, original_data) == OK);
	REQUIRE(pck_packer.flush() == OK);

	const Vector<uint8_t> raw_pack = FileAccess::get_file_as_bytes(output_pck_path);
	auto contains_bytes = [&raw_pack](const Vector<uint8_t> &p_needle) {
		if (p_needle.is_empty() || p_needle.size() > raw_pack.size()) {
			return false;
		}
		for (int64_t i = 0; i <= raw_pack.size() - p_needle.size(); i++) {
			if (memcmp(raw_pack.ptr() + i, p_needle.ptr(), p_needle.size()) == 0) {
				return true;
			}
		}
		return false;
	};

	CHECK_FALSE(contains_bytes(packed_path.to_utf8_buffer()));
	CHECK_FALSE(contains_bytes(original_data));

	PackedData *packed_data = PackedData::get_singleton();
	REQUIRE(packed_data != nullptr);
	const Error add_pack_error = packed_data->add_pack(output_pck_path, true, 0);
	CHECK(add_pack_error == OK);

	Ref<FileAccess> packed_file;
	if (add_pack_error == OK) {
		packed_file = packed_data->try_open_path("res://" + packed_path);
		CHECK(packed_file.is_valid());
		if (packed_file.is_valid()) {
			packed_file->seek(11);
			Vector<uint8_t> tail = packed_file->get_buffer(original_data.size() - 11);
			CHECK(tail == original_data.slice(11));
		}
	}

	packed_file.unref();
	packed_data->clear();
}

} // namespace TestPCKPacker
