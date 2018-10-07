#if PY_VERSION_HEX < 0x02050000 && !defined(PY_SSIZE_T_MIN)
typedef int Py_ssize_t;
#define PY_SSIZE_T_MAX INT_MAX
#define PY_SSIZE_T_MIN INT_MIN
#endif

/********************************************************
 Audio Tools, a module and set of tools for manipulating audio data
 Copyright (C) 2007-2010  Brian Langenberger

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*******************************************************/

#if PY_MAJOR_VERSION >= 3
#define IS_PY3K
#endif

PyObject*
verifymodule_mpeg(PyObject *dummy, PyObject *args);

PyObject*
verifymodule_ogg(PyObject *dummy, PyObject *args);

PyMethodDef module_methods[] = {
    {"mpeg", (PyCFunction)verifymodule_mpeg,
     METH_VARARGS, ""},
    {"ogg", (PyCFunction)verifymodule_ogg,
     METH_VARARGS, ""},
    {NULL}
};

typedef enum {OK, ERROR} status;

#include "verify/mpeg.h"
#include "verify/ogg.h"

