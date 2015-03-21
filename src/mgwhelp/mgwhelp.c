/*
 * Copyright 2002-2013 Jose Fonseca
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */


#include <assert.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <malloc.h>

#include <windows.h>
#include <tchar.h>
#include <psapi.h>

#include "misc.h"
#include "demangle.h"
#include "pehelp.h"
#include "mgwhelp.h"

#include <dwarf.h>
#include <libdwarf.h>
#include "dwarf_pe.h"


struct mgwhelp_module
{
    struct mgwhelp_module *next;

    DWORD64 Base;
    char LoadedImageName[MAX_PATH];

    DWORD64 image_base_vma;

    Dwarf_Debug dbg;
};


struct mgwhelp_process
{
    struct mgwhelp_process *next;

    HANDLE hProcess;

    struct mgwhelp_module *modules;
};


struct mgwhelp_process *processes = NULL;


struct find_handle
{
    struct mgwhelp_module *module;
    DWORD64 pc;
    const char *filename;
    const char *functionname;
    unsigned int line;
    bool found;
};


/*-
 * elftoolchain-0.6.1/addr2line/addr2line.c
 *
 * Copyright (c) 2009 Kai Wang
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

static char unknown[] = { '?', '?', '\0' };
static void
search_func(Dwarf_Debug dbg,
            Dwarf_Die die,
            Dwarf_Addr addr,
            char **rlt_func)
{
    Dwarf_Die spec_die;
    Dwarf_Die child_die;
    Dwarf_Die sibling_die;
    Dwarf_Error de;
    Dwarf_Half tag, return_form;
    Dwarf_Unsigned lopc, hipc;
    Dwarf_Off ref;
    Dwarf_Attribute sub_at, spec_at;
    char *func0;
    int ret;
    enum Dwarf_Form_Class return_class;

    do {

        if (*rlt_func != NULL)
            return;

        if (dwarf_tag(die, &tag, &de) != DW_DLV_OK) {
            OutputDebug("dwarf_tag: %s", dwarf_errmsg(de));
            goto cont_search;
        }

        if (tag == DW_TAG_subprogram) {
            if (dwarf_lowpc(die, &lopc, &de) != DW_DLV_OK ||
                dwarf_highpc_b(die, &hipc, &return_form, &return_class, &de) != DW_DLV_OK)
                goto cont_search;
            if (return_class == DW_FORM_CLASS_CONSTANT)
                hipc += lopc;
            if (addr < lopc || addr >= hipc)
                goto cont_search;

            /* Found it! */

            *rlt_func = unknown;
            ret = dwarf_attr(die, DW_AT_name, &sub_at, &de);
            if (ret == DW_DLV_ERROR)
                return;
            if (ret == DW_DLV_OK) {
                if (dwarf_formstring(sub_at, &func0, &de) != DW_DLV_OK)
                    *rlt_func = unknown;
                else
                    *rlt_func = func0;
                return;
            }

            /*
             * If DW_AT_name is not present, but DW_AT_specification is
             * present, then probably the actual name is in the DIE
             * referenced by DW_AT_specification.
             */
            if (dwarf_attr(die, DW_AT_specification, &spec_at, &de) != DW_DLV_OK)
                return;
            if (dwarf_global_formref(spec_at, &ref, &de) != DW_DLV_OK)
                return;
            if (dwarf_offdie(dbg, ref, &spec_die, &de) != DW_DLV_OK)
                return;
            if (dwarf_diename(spec_die, rlt_func, &de) != DW_DLV_OK)
                *rlt_func = unknown;

            return;
        }

    cont_search:

        /* Recurse into children. */
        ret = dwarf_child(die, &child_die, &de);
        if (ret == DW_DLV_ERROR)
            OutputDebug("dwarf_child: %s", dwarf_errmsg(de));
        else if (ret == DW_DLV_OK)
            search_func(dbg, child_die, addr, rlt_func);

        /* Advance to next sibling. */
        ret = dwarf_siblingof(dbg, die, &sibling_die, &de);
        if (ret != DW_DLV_OK) {
            if (ret == DW_DLV_ERROR)
                OutputDebug("dwarf_siblingof: %s", dwarf_errmsg(de));
            break;
        }
        die = sibling_die;
    } while (true);
}

static void
find_dwarf_symbol(struct mgwhelp_module *module,
                  DWORD64 addr,
                  struct find_handle *info)
{
    Dwarf_Debug dbg = module->dbg;
    Dwarf_Error error = 0;
    char *funcname = NULL;

    Dwarf_Arange *aranges;
    Dwarf_Signed arange_count;
    if (dwarf_get_aranges(dbg, &aranges, &arange_count, &error) != DW_DLV_OK) {
        goto no_aranges;
    }

    Dwarf_Arange arange;
    if (dwarf_get_arange(aranges, arange_count, addr, &arange, &error) != DW_DLV_OK) {
        goto no_arange;
    }

    Dwarf_Off cu_die_offset;
    if (dwarf_get_cu_die_offset(arange, &cu_die_offset, &error) != DW_DLV_OK) {
        goto no_die_offset;
    }

    Dwarf_Die cu_die;
    if (dwarf_offdie_b(dbg, cu_die_offset, 1, &cu_die, &error) != DW_DLV_OK) {
        goto no_cu_die;
    }

    search_func(dbg, cu_die, addr, &funcname);
    if (funcname) {
        info->functionname = funcname;
        info->found = true;
    }

    Dwarf_Line *linebuf;
    Dwarf_Signed linecount;
    if (dwarf_srclines(cu_die, &linebuf, &linecount, &error) == DW_DLV_OK) {
        Dwarf_Unsigned lineno, plineno;
        Dwarf_Addr lineaddr, plineaddr;
        char *file, *file0, *pfile;
        plineaddr = ~0ULL;
        plineno = lineno = 0;
        pfile = file = unknown;
        Dwarf_Signed i;
        for (i = 0; i < linecount; i++) {
            if (dwarf_lineaddr(linebuf[i], &lineaddr, &error) != DW_DLV_OK) {
                OutputDebug("dwarf_lineaddr: %s",
                    dwarf_errmsg(error));
                break;
            }
            if (addr > plineaddr && addr < lineaddr) {
                lineno = plineno;
                file = pfile;
                break;
            }
            if (dwarf_lineno(linebuf[i], &lineno, &error) != DW_DLV_OK) {
                OutputDebug("dwarf_lineno: %s",
                    dwarf_errmsg(error));
                break;
            }
            if (dwarf_linesrc(linebuf[i], &file0, &error) != DW_DLV_OK) {
                OutputDebug("dwarf_linesrc: %s",
                    dwarf_errmsg(error));
            } else {
                file = file0;
            }
            if (addr == lineaddr) {
                break;
            }
            plineaddr = lineaddr;
            plineno = lineno;
            pfile = file;
        }

        info->filename = file;
        info->line = lineno;

        dwarf_srclines_dealloc(dbg, linebuf, linecount);
    }

    dwarf_dealloc(dbg, cu_die, DW_DLA_DIE);
no_cu_die:
    ;
no_die_offset:
    ;
no_arange:
    for (Dwarf_Signed i = 0; i < arange_count; ++i) {
        dwarf_dealloc(dbg, aranges[i], DW_DLA_ARANGE);
    }
    dwarf_dealloc(dbg, aranges, DW_DLA_LIST);
no_aranges:
    if (error) {
        OutputDebug("libdwarf error: %s\n", dwarf_errmsg(error));
    }
}


static struct mgwhelp_module *
mgwhelp_module_create(struct mgwhelp_process * process, DWORD64 Base)
{
    struct mgwhelp_module *module;
    DWORD dwRet;

    module = (struct mgwhelp_module *)calloc(1, sizeof *module);
    if (!module) {
        goto no_module;
    }

    module->Base = Base;

    module->next = process->modules;
    process->modules = module;

    /* SymGetModuleInfo64 is not reliable for this, as explained in
     * https://msdn.microsoft.com/en-us/library/windows/desktop/ms681336.aspx
     */
    dwRet = GetModuleFileNameExA(process->hProcess,
                                 (HMODULE)(UINT_PTR)Base,
                                 module->LoadedImageName,
                                 sizeof module->LoadedImageName);
    if (dwRet == 0) {
        goto no_module_name;
    }

    module->image_base_vma = PEGetImageBase(process->hProcess, Base);

    Dwarf_Error error = 0;
    if (dwarf_pe_init(module->LoadedImageName, 0, 0, &module->dbg, &error) == DW_DLV_OK) {
        return module;
    } else {
        OutputDebug("MGWHELP: %s: %s\n", module->LoadedImageName, "no dwarf symbols");
    }

    return module;

no_module_name:
    OutputDebug("MGWHELP: no module name");
    free(module);
no_module:
    return NULL;
}


static void
mgwhelp_module_destroy(struct mgwhelp_module * module)
{
    if (module->dbg) {
        Dwarf_Error error = 0;
        dwarf_pe_finish(module->dbg, &error);
    }

    free(module);
}


static struct mgwhelp_module *
mgwhelp_module_lookup(struct mgwhelp_process * process, DWORD64 Base)
{
    struct mgwhelp_module *module;

    module = process->modules;
    while (module) {
        if (module->Base == Base)
            return module;

        module = module->next;
    }

    return mgwhelp_module_create(process, Base);
}


static struct mgwhelp_process *
mgwhelp_process_lookup(HANDLE hProcess)
{
    struct mgwhelp_process *process;

    process = processes;
    while (process) {
        if (process->hProcess == hProcess)
            return process;

        process = process->next;
    }

    process = (struct mgwhelp_process *)calloc(1, sizeof *process);
    if (!process)
        return process;

    process->hProcess = hProcess;

    process->next = processes;
    processes = process;

    return process;
}


static BOOL
mgwhelp_find_symbol(HANDLE hProcess, DWORD64 Address, struct find_handle *info)
{
    DWORD64 Base;
    struct mgwhelp_process *process;
    struct mgwhelp_module *module;

    process = mgwhelp_process_lookup(hProcess);
    if (!process) {
        return FALSE;
    }

    Base = GetModuleBase64(hProcess, Address);
    if (!Base) {
        return FALSE;
    }

    module = mgwhelp_module_lookup(process, Base);
    if (!module)
        return FALSE;

    DWORD64 Offset = module->image_base_vma + Address - (DWORD64)module->Base;

    memset(info, 0, sizeof *info);
    info->module = module;
    info->pc = Offset;

    if (module->dbg) {
        find_dwarf_symbol(module, Offset, info);
        if (info->found) {
            return TRUE;
        }
    }

    return FALSE;
}


static void
mgwhelp_initialize(HANDLE hProcess)
{
    struct mgwhelp_process *process;

    process = (struct mgwhelp_process *)calloc(1, sizeof *process);
    if (process) {
        process->hProcess = hProcess;

        process->next = processes;
        processes = process;
    }
}


BOOL WINAPI
MgwSymInitialize(HANDLE hProcess, PCSTR UserSearchPath, BOOL fInvadeProcess)
{
    BOOL ret;

    ret = SymInitialize(hProcess, UserSearchPath, fInvadeProcess);

    if (ret) {
        mgwhelp_initialize(hProcess);
    }

    return ret;
}


BOOL WINAPI
MgwSymInitializeW(HANDLE hProcess, PCWSTR UserSearchPath, BOOL fInvadeProcess)
{
    BOOL ret;

    ret = SymInitializeW(hProcess, UserSearchPath, fInvadeProcess);

    if (ret) {
        mgwhelp_initialize(hProcess);
    }

    return ret;
}


DWORD WINAPI
MgwSymSetOptions(DWORD SymOptions)
{
    return SymSetOptions(SymOptions);
}


BOOL WINAPI
MgwSymFromAddr(HANDLE hProcess, DWORD64 Address, PDWORD64 Displacement, PSYMBOL_INFO Symbol)
{
    struct find_handle info;

    if (mgwhelp_find_symbol(hProcess, Address, &info)) {
        strncpy(Symbol->Name, info.functionname, Symbol->MaxNameLen);

        if (Displacement) {
            /* TODO */
            *Displacement = 0;
        }

        return TRUE;
    }

    return SymFromAddr(hProcess, Address, Displacement, Symbol);
}


BOOL WINAPI
MgwSymGetLineFromAddr64(HANDLE hProcess, DWORD64 dwAddr, PDWORD pdwDisplacement, PIMAGEHLP_LINE64 Line)
{
    struct find_handle info;

    if (mgwhelp_find_symbol(hProcess, dwAddr, &info)) {
        Line->FileName = (char *)info.filename;
        Line->LineNumber = info.line;

        if (pdwDisplacement) {
            /* TODO */
            *pdwDisplacement = 0;
        }

        return TRUE;
    }

    return SymGetLineFromAddr64(hProcess, dwAddr, pdwDisplacement, Line);
}


DWORD WINAPI
MgwUnDecorateSymbolName(PCSTR DecoratedName, PSTR UnDecoratedName, DWORD UndecoratedLength, DWORD Flags)
{
    assert(DecoratedName != NULL);

    if (DecoratedName[0] == '_' && DecoratedName[1] == 'Z') {
        char *res = demangle(DecoratedName);
        if (res) {
            strncpy(UnDecoratedName, res, UndecoratedLength);
            free(res);
            return strlen(UnDecoratedName);
        }
    }

    return UnDecorateSymbolName(DecoratedName, UnDecoratedName, UndecoratedLength, Flags);
}


BOOL WINAPI
MgwSymCleanup(HANDLE hProcess)
{
    struct mgwhelp_process **link;
    struct mgwhelp_process *process;
    struct mgwhelp_module *module;

    link = &processes;
    process = *link;
    while (process) {
        if (process->hProcess == hProcess) {
            module = process->modules;
            while (module) {
                struct mgwhelp_module *next = module->next;

                mgwhelp_module_destroy(module);

                module = next;
            }

            *link = process->next;
            free(process);
            break;
        }

        link = &process->next;
        process = *link;
    }

    return SymCleanup(hProcess);
}


// Unicode stubs


BOOL WINAPI
MgwSymFromAddrW(HANDLE hProcess, DWORD64 Address, PDWORD64 Displacement, PSYMBOL_INFOW SymbolW)
{
    PSYMBOL_INFO SymbolA = (PSYMBOL_INFO)malloc(offsetof(SYMBOL_INFO, Name) + SymbolW->MaxNameLen);
    memcpy(SymbolA, SymbolW, offsetof(SYMBOL_INFO, Name));
    if (MgwSymFromAddr(hProcess, Address, Displacement, SymbolA)) {
        MultiByteToWideChar(CP_ACP, 0, SymbolA->Name, -1, SymbolW->Name, SymbolW->MaxNameLen);
        return TRUE;
    } else {
        return FALSE;
    }
}


BOOL WINAPI
MgwSymGetLineFromAddrW64(HANDLE hProcess, DWORD64 dwAddr, PDWORD pdwDisplacement, PIMAGEHLP_LINEW64 LineW)
{
    IMAGEHLP_LINE64 LineA;
    if (MgwSymGetLineFromAddr64(hProcess, dwAddr, pdwDisplacement, &LineA)) {
        // https://msdn.microsoft.com/en-us/library/windows/desktop/ms681330.aspx
        // states that SymGetLineFromAddrW64 "returns a pointer to a buffer
        // that may be reused by another function" and that callers should be
        // "sure to copy the data returned to another buffer immediately",
        // therefore the static buffer should be safe.
        static WCHAR FileName[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, LineA.FileName, -1, FileName, MAX_PATH);
        memcpy(LineW, &LineA, sizeof LineA);
        LineW->FileName = FileName;
        return TRUE;
    } else {
        return FALSE;
    }
}
