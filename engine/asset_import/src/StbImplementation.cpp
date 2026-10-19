// SPDX-License-Identifier: MIT
// The stb_image implementation, compiled once, with only the formats the importer accepts. Kept in its own translation
// unit so the third-party code is isolated (its header is a system include, so the engine's warnings don't apply).
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#define STBI_ONLY_HDR
#define STBI_MAX_DIMENSIONS 16384
#include <stb_image.h>
