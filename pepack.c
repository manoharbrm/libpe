/*
	pev - the PE file analyzer toolkit

	pepack.c - search packers in PE files

	Copyright (C) 2012 - 2017 pev authors

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.

    In addition, as a special exception, the copyright holders give
    permission to link the code of portions of this program with the
    OpenSSL library under certain conditions as described in each
    individual source file, and distribute linked combinations
    including the two.
    
    You must obey the GNU General Public License in all respects
    for all of the code used other than OpenSSL.  If you modify
    file(s) with this exception, you may extend this exception to your
    version of the file(s), but you are not obligated to do so.  If you
    do not wish to do so, delete this exception statement from your
    version.  If you delete this exception statement from all source
    files in the program, then also delete it here.
*/

#include "common.h"
#include "plugins.h"

#define PROGRAM "pepack"
#define MAX_SIG_SIZE 2048






/* MEW Packer and others basically stores the entrypoint
   in a section marked only as readable (without
   executable and/or writable flags)
   Windows Loader still executes the binary
*/
bool generic_packer(pe_ctx_t *ctx, uint64_t entrypoint)
{
	IMAGE_SECTION_HEADER *section = pe_rva2section(ctx, entrypoint);
	if (section == NULL)
		return false;

	// we count the flags for the section and if there is more than
	// 2 it means we don't have the mew_packer
	const SectionCharacteristics invalid_flags[] = {
		IMAGE_SCN_MEM_EXECUTE,
		IMAGE_SCN_MEM_READ,
		IMAGE_SCN_MEM_WRITE
	};

	// MEW never leave EP in .text section
	if (memcmp(section->Name, ".text", 5) == 0)
		return false;

	unsigned short flags_count = 0;

	for (size_t i=0; i < LIBPE_SIZEOF_ARRAY(invalid_flags); i++) {
		if (section->Characteristics & invalid_flags[i])
			flags_count++;
	}

	return flags_count < 3;
}

static bool loaddb(FILE **fp, const options_t *options)
{
	const char *dbfile = options->dbfile ? options->dbfile : "userdb.txt";

	*fp = fopen(dbfile, "r");
	// FIXME(jweyrich): Granted read permission to the informed dbfile, this will succeed even if it's a directory!
	if (!*fp) {
		// SHAREDIR is defined via CPPFLAGS in the Makefile
		*fp = fopen(SHAREDIR "/userdb.txt", "r");
	}

	return *fp != NULL;
}

static bool match_peid_signature(const unsigned char *data, char *sig)
{
	unsigned char byte_str[3], byte;

	// add null terminator
	byte_str[2] = '\0';

	while (*sig)
	{
		// ignore '=' and blank spaces
		if (*sig == '=' || *sig == ' ')
		{
			sig++;
			continue;
		}

		// match "??"
		if (*sig == '?')
		{
			sig += 2;
			data++;
			continue;
		}

		memcpy(byte_str, sig, 2);
		byte = strtoul((char *) byte_str, NULL, 16);

		if (*data++ != byte)
			return false;

		sig += 2; // next two characters of signature
	}
	return true;
}

static bool compare_signature(const unsigned char *data, uint64_t ep_offset, FILE *dbfile, char *packer_name, size_t packer_name_len)
{
	if (!dbfile || !data)
		return false;

	char *buff = malloc_s(MAX_SIG_SIZE);

	//memset(buff, 0, MAX_SIG_SIZE);
	while (fgets(buff, MAX_SIG_SIZE, dbfile))
	{
		// line length
		size_t len = strlen(buff);

		// ifgore comments and blank lines
		if (*buff == ';' || *buff == '\n' || *buff == '\r')
			continue;

		// remove newline from buffer
		if (*(buff+len-1) == '\n')
			*(buff+len-1) = '\0';

		// removing carriage return, if present
		if (*(buff+len-2) == '\r')
		{
			*(buff+len-2) = '\0';
			//*(buff+len-1) = '\0';
			len--; // update line length
		}

		// line have [packer name]? Fill packer_name pointer
		if (*buff == '[' && *(buff+len-2) == ']')
		{
			*(buff+len-2) = '\0'; // remove square brackets
			strncpy(packer_name, buff+1, packer_name_len);
			packer_name[packer_name_len-1] = '\0'; // Guarantee it's Null-terminated.
		}

		// check if signature match
		if (!strncasecmp(buff, "signature", 9))
		{
			if (match_peid_signature(data + ep_offset, buff+9))
			{
				free(buff);
				return true;
			}
		}
	}
	free(buff);
	return false;
}

pe_err_t pe_getPackerInformation(char* value)
{
	
	const uint64_t ep_offset = pe_rva2ofs(&ctx, ctx.pe.entrypoint);
	if (ep_offset == 0)
		EXIT_ERROR("unable to get entrypoint offset");

	FILE *dbfile = NULL;
	if (!loaddb(&dbfile, options))
		fprintf(stderr, "warning: without valid database file, %s will search in generic mode only\n", PROGRAM);

	// TODO(jweyrich): Create a new API to retrieve map_addr.
	// TODO(jweyrich): Should we use `LIBPE_PTR_ADD(ctx->map_addr, ep_offset)` instead?
	const unsigned char *pe_data = ctx.map_addr;

	// packer by signature
	if (compare_signature(pe_data, ep_offset, dbfile, value, sizeof(value)))
		;
	// generic detection
	else if (generic_packer(&ctx, ep_offset))
		sprintf(value, "generic");
		return EXIT_SUCCESS;
	else
		sprintf(value, "no packerfound");
		return EXIT_SUCCESS;

 // If this is able to find out, the exact packer information, then its going to be in the variable "value"
	if (dbfile != NULL)
		fclose(dbfile);

	return EXIT_SUCCESS;
}