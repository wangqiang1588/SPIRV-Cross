/*
 * Copyright 2019 Hans-Kristian Arntzen
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SPIRV_CROSS_C_API_H
#define SPIRV_CROSS_C_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPVC_PUBLIC_API

typedef struct spvc_context_s *spvc_context;
typedef struct spvc_parsed_ir_s *spvc_parsed_ir;
typedef struct spvc_compiler_s *spvc_compiler;
typedef struct spvc_type_s *spvc_type;
typedef struct spvc_string_s *spvc_string;
typedef struct spvc_compiler_options_s *spvc_compiler_options;

typedef uint32_t spvc_type_id;
typedef uint32_t spvc_variable_id;
typedef uint32_t spvc_constant_id;
typedef uint32_t spvc_id;

typedef uint32_t spvc_bool;
#define SPVC_TRUE ((spvc_bool)1)
#define SPVC_FALSE ((spvc_bool)0)

typedef enum spvc_error
{
	// Success.
	SPVC_SUCCESS = 0,

	// The SPIR-V is invalid.
	SPVC_INVALID_SPIRV = -1,

	// The SPIR-V might be valid, but SPIRV-Cross currently cannot correctly translate this to your target language.
	SPVC_UNSUPPORTED_SPIRV = -2,

	// If for some reason we hit this.
	SPVC_OUT_OF_MEMORY = -3,

	// Invalid API argument.
	SPVC_INVALID_ARGUMENT = -4,

	SPVC_ERROR_INT_MAX = 0x7fffffff
} spvc_error;

typedef enum spvc_capture_mode
{
	// The Parsed IR will be copied, and the handle can be reused.
	SPVC_CAPTURE_MODE_COPY = 0,

	// The handle will now be owned by the compiler.
	// parsed_ir can no longer be used by the caller.
	// This is optimal for performance.
	SPVC_CAPTURE_MODE_TAKE_OWNERSHIP = 1,

	SPVC_CAPTURE_MODE_INT_MAX = 0x7fffffff
} spvc_capture_mode;

typedef enum spvc_backend
{
	// This backend can only perform reflection, no compiler options are supported.
	SPVC_BACKEND_NONE = 0,
	SPVC_BACKEND_GLSL = 1,
	SPVC_BACKEND_HLSL = 2,
	SPVC_BACKEND_MSL = 3,
	SPVC_BACKEND_CPP = 4,
	SPVC_BACKEND_JSON = 5,
	SPVC_BACKEND_INT_MAX = 0x7fffffff
} spvc_backend;

typedef enum spvc_compiler_option
{
	SPVC_COMPILER_OPTION_INT_MAX = 0x7fffffff
} spvc_compiler_option;

// Context is the highest-level API construct.
SPVC_PUBLIC_API spvc_error spvc_create_context(spvc_context *context);
SPVC_PUBLIC_API spvc_error spvc_destroy_context(spvc_context context);
SPVC_PUBLIC_API const char *spvc_get_last_error_string(spvc_context context);

// SPIR-V parsing interface.
SPVC_PUBLIC_API spvc_error spvc_parse_spirv(spvc_context context, const uint32_t *spirv, size_t word_count,
                                            spvc_parsed_ir *parsed_ir);
SPVC_PUBLIC_API void spvc_destroy_parsed_ir(spvc_parsed_ir parsed_ir);

// Create a compiler backend
SPVC_PUBLIC_API spvc_error spvc_create_compiler(spvc_context context, spvc_backend backend,
                                                spvc_parsed_ir parsed_ir, spvc_capture_mode mode,
                                                spvc_compiler *compiler);
SPVC_PUBLIC_API void spvc_destroy_compiler(spvc_compiler compiler);

// Set options.
SPVC_PUBLIC_API spvc_error spvc_create_compiler_options(spvc_compiler compiler, spvc_compiler_options *options);
SPVC_PUBLIC_API spvc_error spvc_set_compiler_option_bool(spvc_compiler_options options,
                                                         spvc_compiler_option, spvc_bool value);
SPVC_PUBLIC_API spvc_error spvc_set_compiler_option_uint(spvc_compiler_options options,
                                                         spvc_compiler_option, uint32_t value);
SPVC_PUBLIC_API spvc_error spvc_install_compiler_options(spvc_compiler compiler, spvc_compiler_options options);
SPVC_PUBLIC_API void spvc_destroy_compiler_options(spvc_compiler_options options);

// Compile IR into a string.
SPVC_PUBLIC_API spvc_error spvc_compile(spvc_compiler compiler, spvc_string *string);
SPVC_PUBLIC_API void spvc_destroy_string(spvc_string string);

#ifdef __cplusplus
}
#endif
#endif
