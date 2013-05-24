/*-
 * Copyright (c) 2009 Ryan Kwolek
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are
 * permitted provided that the following conditions are met:
 *  1. Redistributions of source code must retain the above copyright notice, this list of
 *     conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice, this list
 *     of conditions and the following disclaimer in the documentation and/or other materials
 *     provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* 
 * IX86Archimonde.c - 
 *    Variation of Blizzard's GamePatcher that patches two specific regions
 *    of executable code in StarCraft 1.15b.
 *    Probably compiled with MS Visual C++ 2008, /O1, /Os, /Oy, /Gd
 */

#include <windows.h>

typedef struct {
	unsigned __int16 gameid;
	unsigned __int16 len;
	char output[1024];
} EXTRAWORK, *LPEXTRAWORK;

typedef struct {
	int todo;
	char *modname;
	unsigned __int32 build;
	unsigned __int32 destrva;
	void *src;
	unsigned __int32 len;
} MODIFICATION, *LPMODIFICATION;

char *codeblob1 = "\x91\xBA\x02\x00\x00\x00\x39\x51\x18\x75\x23\x8B\x41\x20\x39\x50"
                  "\x18\x75\x1B\xBA\x03\x00\x00\x00\x39\x51\x10\x75\x11\x39\x50\x10"
                  "\x74\x0C\x8B\x50\x10\x89\x51\x10\x8B\x40\x14\x89\x41\x14\x91\x5E"
                  "\xC2\x0C\x00";
/* contains...
 xchg    eax, ecx
 mov     edx, 2
 cmp     [ecx+18h], edx
 jnz     short done
 mov     eax, [ecx+20h]
 cmp     [eax+18h], edx
 jnz     short done
 mov     edx, 3
 cmp     [ecx+10h], edx
 jnz     short done
 cmp     [eax+10h], edx
 jz      short done
 mov     edx, [eax+10h]
 mov     [ecx+10h], edx
 mov     eax, [eax+14h]
 mov     [ecx+14h], eax
done:
 xchg    eax, ecx
 pop     esi
 retn    0Ch
*/

char *codeblob2 = "\xE9\x34\xBE\x41\x00";
/* contains...
 jmp     near ptr [esi + 41be34h] (psuedocode)
*/

MODIFICATION mods = {
	{1, "game.dll", 6352, 0x86F800, codeblob1, 0x33},
	{1, "game.dll", 6352, 0x4539C7, codeblob2, 0x05}	
};


///////////////////////////////////////////////////////////////////////////////


bool __stdcall DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
	if (fdwReason == DLL_PROCESS_ATTACH)
		DisableThreadLibraryCalls(hinstDLL);
	return true;
}


/*
	NOTE: this is run like so:

	EXTRAWORKFN lpfnExtraWork = (EXTRAWORKFN)GetProcAddress(hLib, "ExtraWork");

	EXTRAWORK ew;
	ew.length = 4;
	ew.gameid = 2;
	*ew.output = 0;

	(*lpfnExtraWork)(&ew);

	FreeLibrary(hLib);

	InsertDWORD(ew.gameid);
	InsertDWORD(ew.length);
	InsertNTString(ew.output);
	SendPacket(0x4B);
*/

int __fastcall ExtraWork(LPEXTRAWORK lpew) {
	if (lpew) {
		if (!(lpew->gameid - 2))
			MakeModifications();
		lpew->length  = 0;
		lpew->gameid  = 2;
		*lpew->output = 0;
	}
	return 0;
}


void GetModuleVerBuildNumber(HMODULE hMod, int *output) {
	TCHAR tstrFilename[536];
	VS_FIXEDFILEINFO *ffi;
	DWORD dwHandle;
	DWORD puLen;
	register int len;
	register LPVOID buf;	

	HANDLE hHeap = GetProcessHeap();
	if (hMod == 0)
		return 0;

	if (GetModuleFileNameW(hMod, tstrFilename, MAX_PATH) - 1 <= 258) {
		len = GetFileVersionInfoSizeW(tstrFilename, &dwHandle);
		if (len == 0)
			return 0;

		buf = HeapAlloc(hHeap, 0, len);
		if (buf == 0)
			return 0;

		if (!GetFileVersionInfoW(tstrFilename, dwHandle, len, buf)) {
			HeapFree(hHeap, 0, buf);
			return 0;
		}

		if (!VerQueryValueW(buf, L"\\", &ffi, &puLen)) {
			HeapFree(hHeap, 0, buf);
			return 0;
		}

common_ground:
		if (output != 0)
			*output = ffi->dwFileVersionLS & 0xFFFF;

		HeapFree(hHeap, 0, buf);
		return 1;
	} else {
		len = GetModuleFileName(hMod, tstrFilename, MAX_PATH);
		if (len == 0 || len == MAX_PATH)
			return 0;

		len = GetFileVersionInfoSizeA(tstrFilename, &dwHandle);
		if (len == 0)
			return 0;

		buf = HeapAlloc(hHeap, 0, len);
		if (buf == 0)
			return 0;

		if (!GetFileVersionInfoA(tstrFilename, dwHandle, len, buf)) {
			HeapFree(hHeap, 0, buf);
			return 0;
		}

		VerQueryValueA(buf, "\\", &ffi, &puLen) {
			HeapFree(hHeap, 0, buf);
			return 0;
		}

		goto common_ground;
	}
}


void MakeModifications() {
	LPMODIFICATION lpmod = mods;

	//probably optimized from "while (lpMod->todo == 1) {"
	do {
		if (lpmod->todo - 1 == 0) {
			modbase = GetModuleHandleA(lpmod->modname);
			if (modbase) {
				if (GetModuleVerBuildNumber(modbase, &buildnum)) {
					if (buildnum == lpmod->build) {
						WriteProcessMemory(GetCurrentProcess(),
 							modbase + lpmod->destrva, 
							lpmod->src, lpmod->len, 0);
					}
				}				

			}
		}
		lpmod++;
	} while (lpmod->todo != 0);
}

