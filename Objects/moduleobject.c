
/* Module object implementation */

#include "Python.h"
#include "pycore_pystate.h"
#include "structmember.h"

static Py_ssize_t max_module_number;

static PyMemberDef module_members[] = {
    {"__dict__", T_OBJECT, offsetof(PyModuleObject, md_dict), READONLY},
    {0}
};


/* Helper for sanity check for traverse not handling m_state == NULL
 * Issue #32374 */
#ifdef Py_DEBUG
static int
bad_traverse_test(PyObject *self, void *arg) {
    assert(self != NULL);
    return 0;
}
#endif

PyTypeObject PyModuleDef_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "moduledef",                                /* tp_name */
    sizeof(struct PyModuleDef),                 /* tp_basicsize */
    0,                                          /* tp_itemsize */
};


PyObject*
PyModuleDef_Init(struct PyModuleDef* def)
{
    if (PyType_Ready(&PyModuleDef_Type) < 0)
         return NULL;
    if (def->m_base.m_index == 0) {
        max_module_number++;
        Py_SET_REFCNT(def, 1);
        Py_TYPE(def) = &PyModuleDef_Type;
        def->m_base.m_index = max_module_number;
    }
    return (PyObject*)def;
}

static int
module_init_dict(PyModuleObject *mod, PyObject *md_dict,
                 PyObject *name, PyObject *doc)
{
    _Py_IDENTIFIER(__name__);
    _Py_IDENTIFIER(__doc__);
    _Py_IDENTIFIER(__package__);
    _Py_IDENTIFIER(__loader__);
    _Py_IDENTIFIER(__spec__);

    if (md_dict == NULL)
        return -1;
    if (doc == NULL)
        doc = Py_None;

    if (_PyDict_SetItemId(md_dict, &PyId___name__, name) != 0)
        return -1;
    if (_PyDict_SetItemId(md_dict, &PyId___doc__, doc) != 0)
        return -1;
    if (_PyDict_SetItemId(md_dict, &PyId___package__, Py_None) != 0)
        return -1;
    if (_PyDict_SetItemId(md_dict, &PyId___loader__, Py_None) != 0)
        return -1;
    if (_PyDict_SetItemId(md_dict, &PyId___spec__, Py_None) != 0)
        return -1;
    if (PyUnicode_CheckExact(name)) {
        Py_INCREF(name);
        Py_XSETREF(mod->md_name, name);
    }

    return 0;
}


PyObject *
PyModule_NewObject(PyObject *name)
{
    PyModuleObject *m;
    m = PyObject_GC_New(PyModuleObject, &PyModule_Type);
    if (m == NULL)
        return NULL;
    m->md_def = NULL;
    m->md_state = NULL;
    m->md_weaklist = NULL;
    m->md_name = NULL;
    m->md_dict = PyDict_New();
    if (module_init_dict(m, m->md_dict, name, NULL) != 0)
        goto fail;
    PyObject_GC_Track(m);
    return (PyObject *)m;

 fail:
    Py_DECREF(m);
    return NULL;
}

PyObject *
PyModule_New(const char *name)
{
    PyObject *nameobj, *module;
    nameobj = PyUnicode_FromString(name);
    if (nameobj == NULL)
        return NULL;
    module = PyModule_NewObject(nameobj);
    Py_DECREF(nameobj);
    return module;
}

/* Check API/ABI version
 * Issues a warning on mismatch, which is usually not fatal.
 * Returns 0 if an exception is raised.
 */
static int
check_api_version(const char *name, int module_api_version)
{
    if (module_api_version != PYTHON_API_VERSION && module_api_version != PYTHON_ABI_VERSION) {
        int err;
        err = PyErr_WarnFormat(PyExc_RuntimeWarning, 1,
            "Python C API version mismatch for module %.100s: "
            "This Python has API version %d, module %.100s has version %d.",
             name,
             PYTHON_API_VERSION, name, module_api_version);
        if (err)
            return 0;
    }
    return 1;
}

static int
_add_methods_to_object(PyObject *module, PyObject *name, PyMethodDef *functions)
{
    PyObject *func;
    PyMethodDef *fdef;

    for (fdef = functions; fdef->ml_name != NULL; fdef++) {
        if ((fdef->ml_flags & METH_CLASS) ||
            (fdef->ml_flags & METH_STATIC)) {
            PyErr_SetString(PyExc_ValueError,
                            "module functions cannot set"
                            " METH_CLASS or METH_STATIC");
            return -1;
        }
        func = PyCFunction_NewEx(fdef, (PyObject*)module, name);
        if (func == NULL) {
            return -1;
        }
        if (PyStrictModule_Check(module)) {
            PyObject *globals = ((PyStrictModuleObject *)module)->globals;
            if (PyDict_SetItemString(globals, fdef->ml_name, func) != 0) {
                Py_DECREF(func);
                return -1;
            }
        } else if (PyObject_SetAttrString(module, fdef->ml_name, func) != 0) {
            Py_DECREF(func);
            return -1;
        }
        Py_DECREF(func);
    }

    return 0;
}

PyObject *
PyModule_Create2(struct PyModuleDef* module, int module_api_version)
{
    if (!_PyImport_IsInitialized(_PyInterpreterState_Get()))
        Py_FatalError("Python import machinery not initialized");
    return _PyModule_CreateInitialized(module, module_api_version);
}

PyObject *
_PyModule_CreateInitialized(struct PyModuleDef* module, int module_api_version)
{
    const char* name;
    PyModuleObject *m;

    if (!PyModuleDef_Init(module))
        return NULL;
    name = module->m_name;
    if (!check_api_version(name, module_api_version)) {
        return NULL;
    }
    if (module->m_slots) {
        PyErr_Format(
            PyExc_SystemError,
            "module %s: PyModule_Create is incompatible with m_slots", name);
        return NULL;
    }
    /* Make sure name is fully qualified.

       This is a bit of a hack: when the shared library is loaded,
       the module name is "package.module", but the module calls
       PyModule_Create*() with just "module" for the name.  The shared
       library loader squirrels away the true name of the module in
       _Py_PackageContext, and PyModule_Create*() will substitute this
       (if the name actually matches).
    */
    if (_Py_PackageContext != NULL) {
        const char *p = strrchr(_Py_PackageContext, '.');
        if (p != NULL && strcmp(module->m_name, p+1) == 0) {
            name = _Py_PackageContext;
            _Py_PackageContext = NULL;
        }
    }
    if ((m = (PyModuleObject*)PyModule_New(name)) == NULL)
        return NULL;

    if (module->m_size > 0) {
        m->md_state = PyMem_MALLOC(module->m_size);
        if (!m->md_state) {
            PyErr_NoMemory();
            Py_DECREF(m);
            return NULL;
        }
        memset(m->md_state, 0, module->m_size);
    }

    if (module->m_methods != NULL) {
        if (PyModule_AddFunctions((PyObject *) m, module->m_methods) != 0) {
            Py_DECREF(m);
            return NULL;
        }
    }
    if (module->m_doc != NULL) {
        if (PyModule_SetDocString((PyObject *) m, module->m_doc) != 0) {
            Py_DECREF(m);
            return NULL;
        }
    }
    m->md_def = module;
    return (PyObject*)m;
}

PyObject *
PyModule_FromDefAndSpec2(struct PyModuleDef* def, PyObject *spec, int module_api_version)
{
    PyModuleDef_Slot* cur_slot;
    PyObject *(*create)(PyObject *, PyModuleDef*) = NULL;
    PyObject *nameobj;
    PyObject *m = NULL;
    int has_execution_slots = 0;
    const char *name;
    int ret;

    PyModuleDef_Init(def);

    nameobj = PyObject_GetAttrString(spec, "name");
    if (nameobj == NULL) {
        return NULL;
    }
    name = PyUnicode_AsUTF8(nameobj);
    if (name == NULL) {
        goto error;
    }

    if (!check_api_version(name, module_api_version)) {
        goto error;
    }

    if (def->m_size < 0) {
        PyErr_Format(
            PyExc_SystemError,
            "module %s: m_size may not be negative for multi-phase initialization",
            name);
        goto error;
    }

    for (cur_slot = def->m_slots; cur_slot && cur_slot->slot; cur_slot++) {
        if (cur_slot->slot == Py_mod_create) {
            if (create) {
                PyErr_Format(
                    PyExc_SystemError,
                    "module %s has multiple create slots",
                    name);
                goto error;
            }
            create = cur_slot->value;
        } else if (cur_slot->slot < 0 || cur_slot->slot > _Py_mod_LAST_SLOT) {
            PyErr_Format(
                PyExc_SystemError,
                "module %s uses unknown slot ID %i",
                name, cur_slot->slot);
            goto error;
        } else {
            has_execution_slots = 1;
        }
    }

    if (create) {
        m = create(spec, def);
        if (m == NULL) {
            if (!PyErr_Occurred()) {
                PyErr_Format(
                    PyExc_SystemError,
                    "creation of module %s failed without setting an exception",
                    name);
            }
            goto error;
        } else {
            if (PyErr_Occurred()) {
                PyErr_Format(PyExc_SystemError,
                            "creation of module %s raised unreported exception",
                            name);
                goto error;
            }
        }
    } else {
        m = PyModule_NewObject(nameobj);
        if (m == NULL) {
            goto error;
        }
    }

    if (PyModule_Check(m)) {
        ((PyModuleObject*)m)->md_state = NULL;
        ((PyModuleObject*)m)->md_def = def;
    } else {
        if (def->m_size > 0 || def->m_traverse || def->m_clear || def->m_free) {
            PyErr_Format(
                PyExc_SystemError,
                "module %s is not a module object, but requests module state",
                name);
            goto error;
        }
        if (has_execution_slots) {
            PyErr_Format(
                PyExc_SystemError,
                "module %s specifies execution slots, but did not create "
                    "a ModuleType instance",
                name);
            goto error;
        }
    }

    if (def->m_methods != NULL) {
        ret = _add_methods_to_object(m, nameobj, def->m_methods);
        if (ret != 0) {
            goto error;
        }
    }

    if (def->m_doc != NULL) {
        ret = PyModule_SetDocString(m, def->m_doc);
        if (ret != 0) {
            goto error;
        }
    }

    /* Sanity check for traverse not handling m_state == NULL
     * This doesn't catch all possible cases, but in many cases it should
     * make many cases of invalid code crash or raise Valgrind issues
     * sooner than they would otherwise.
     * Issue #32374 */
#ifdef Py_DEBUG
    if (def->m_traverse != NULL) {
        def->m_traverse(m, bad_traverse_test, NULL);
    }
#endif
    Py_DECREF(nameobj);
    return m;

error:
    Py_DECREF(nameobj);
    Py_XDECREF(m);
    return NULL;
}

int
PyModule_ExecDef(PyObject *module, PyModuleDef *def)
{
    PyModuleDef_Slot *cur_slot;
    const char *name;
    int ret;

    name = PyModule_GetName(module);
    if (name == NULL) {
        return -1;
    }

    if (def->m_size >= 0) {
        PyModuleObject *md = (PyModuleObject*)module;
        if (md->md_state == NULL) {
            /* Always set a state pointer; this serves as a marker to skip
             * multiple initialization (importlib.reload() is no-op) */
            md->md_state = PyMem_MALLOC(def->m_size);
            if (!md->md_state) {
                PyErr_NoMemory();
                return -1;
            }
            memset(md->md_state, 0, def->m_size);
        }
    }

    if (def->m_slots == NULL) {
        return 0;
    }

    for (cur_slot = def->m_slots; cur_slot && cur_slot->slot; cur_slot++) {
        switch (cur_slot->slot) {
            case Py_mod_create:
                /* handled in PyModule_FromDefAndSpec2 */
                break;
            case Py_mod_exec:
                ret = ((int (*)(PyObject *))cur_slot->value)(module);
                if (ret != 0) {
                    if (!PyErr_Occurred()) {
                        PyErr_Format(
                            PyExc_SystemError,
                            "execution of module %s failed without setting an exception",
                            name);
                    }
                    return -1;
                }
                if (PyErr_Occurred()) {
                    PyErr_Format(
                        PyExc_SystemError,
                        "execution of module %s raised unreported exception",
                        name);
                    return -1;
                }
                break;
            default:
                PyErr_Format(
                    PyExc_SystemError,
                    "module %s initialized with unknown slot %i",
                    name, cur_slot->slot);
                return -1;
        }
    }
    return 0;
}

int
PyModule_AddFunctions(PyObject *m, PyMethodDef *functions)
{
    int res;
    PyObject *name = PyModule_GetNameObject(m);
    if (name == NULL) {
        return -1;
    }

    res = _add_methods_to_object(m, name, functions);
    Py_DECREF(name);
    return res;
}

int
PyModule_SetDocString(PyObject *m, const char *doc)
{
    PyObject *v;
    _Py_IDENTIFIER(__doc__);

    v = PyUnicode_FromString(doc);
    if (v == NULL) {
        Py_DECREF(v);
        return -1;
    } else if (PyStrictModule_Check(m)) {
        PyObject *globals = ((PyStrictModuleObject*)m)->globals;
        if (_PyDict_SetItemId(globals, &PyId___doc__, v) != 0) {
            Py_DECREF(v);
            return -1;
        }
    } else if(_PyObject_SetAttrId(m, &PyId___doc__, v) != 0) {
        Py_XDECREF(v);
        return -1;
    }
    Py_DECREF(v);
    return 0;
}

PyObject *
PyModule_GetDict(PyObject *m)
{
    PyObject *d;
    if (!PyModule_Check(m)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    d = ((PyModuleObject *)m) -> md_dict;
    assert(d != NULL);
    return d;
}

PyObject*
PyModule_GetNameObject(PyObject *m)
{
    _Py_IDENTIFIER(__name__);
    PyObject *d;
    PyObject *name;
    if (!PyModule_Check(m)) {
        PyErr_BadArgument();
        return NULL;
    }
    d = ((PyModuleObject *)m)->md_dict;
    if (d == NULL ||
        (name = _PyDict_GetItemId(d, &PyId___name__)) == NULL ||
        !PyUnicode_Check(name))
    {
        PyErr_SetString(PyExc_SystemError, "nameless module");
        return NULL;
    }
    Py_INCREF(name);
    return name;
}

const char *
PyModule_GetName(PyObject *m)
{
    PyObject *name = PyModule_GetNameObject(m);
    if (name == NULL)
        return NULL;
    Py_DECREF(name);   /* module dict has still a reference */
    return PyUnicode_AsUTF8(name);
}

PyObject*
PyModule_GetFilenameObject(PyObject *m)
{
    _Py_IDENTIFIER(__file__);
    PyObject *d;
    PyObject *fileobj;
    if (!PyModule_Check(m)) {
        PyErr_BadArgument();
        return NULL;
    }
    d = ((PyModuleObject *)m)->md_dict;
    if (d == NULL ||
        (fileobj = _PyDict_GetItemId(d, &PyId___file__)) == NULL ||
        !PyUnicode_Check(fileobj))
    {
        PyErr_SetString(PyExc_SystemError, "module filename missing");
        return NULL;
    }
    Py_INCREF(fileobj);
    return fileobj;
}

const char *
PyModule_GetFilename(PyObject *m)
{
    PyObject *fileobj;
    const char *utf8;
    fileobj = PyModule_GetFilenameObject(m);
    if (fileobj == NULL)
        return NULL;
    utf8 = PyUnicode_AsUTF8(fileobj);
    Py_DECREF(fileobj);   /* module dict has still a reference */
    return utf8;
}

PyModuleDef*
PyModule_GetDef(PyObject* m)
{
    if (!PyModule_Check(m)) {
        PyErr_BadArgument();
        return NULL;
    }
    return ((PyModuleObject *)m)->md_def;
}

void*
PyModule_GetState(PyObject* m)
{
    if (!PyModule_Check(m)) {
        PyErr_BadArgument();
        return NULL;
    }
    return ((PyModuleObject *)m)->md_state;
}

void
_PyModule_Clear(PyObject *m)
{
    PyObject *d = ((PyModuleObject *)m)->md_dict;
    if (d != NULL)
        _PyModule_ClearDict(d);
}

void
_PyModule_ClearDict(PyObject *d)
{
    /* To make the execution order of destructors for global
       objects a bit more predictable, we first zap all objects
       whose name starts with a single underscore, before we clear
       the entire dictionary.  We zap them by replacing them with
       None, rather than deleting them from the dictionary, to
       avoid rehashing the dictionary (to some extent). */

    Py_ssize_t pos;
    PyObject *key, *value;

    int verbose = _PyInterpreterState_GET_UNSAFE()->config.verbose;

    /* First, clear only names starting with a single underscore */
    pos = 0;
    while (PyDict_Next(d, &pos, &key, &value)) {
        if (value != Py_None && PyUnicode_Check(key)) {
            if (PyUnicode_READ_CHAR(key, 0) == '_' &&
                PyUnicode_READ_CHAR(key, 1) != '_') {
                if (verbose > 1) {
                    const char *s = PyUnicode_AsUTF8(key);
                    if (s != NULL)
                        PySys_WriteStderr("#   clear[1] %s\n", s);
                    else
                        PyErr_Clear();
                }
                if (PyDict_SetItem(d, key, Py_None) != 0) {
                    PyErr_WriteUnraisable(NULL);
                }
            }
        }
    }

    /* Next, clear all names except for __builtins__ */
    pos = 0;
    while (PyDict_Next(d, &pos, &key, &value)) {
        if (value != Py_None && PyUnicode_Check(key)) {
            if (PyUnicode_READ_CHAR(key, 0) != '_' ||
                !_PyUnicode_EqualToASCIIString(key, "__builtins__"))
            {
                if (verbose > 1) {
                    const char *s = PyUnicode_AsUTF8(key);
                    if (s != NULL)
                        PySys_WriteStderr("#   clear[2] %s\n", s);
                    else
                        PyErr_Clear();
                }
                if (PyDict_SetItem(d, key, Py_None) != 0) {
                    PyErr_WriteUnraisable(NULL);
                }
            }
        }
    }

    /* Note: we leave __builtins__ in place, so that destructors
       of non-global objects defined in this module can still use
       builtins, in particularly 'None'. */

}

/*[clinic input]
class module "PyModuleObject *" "&PyModule_Type"
[clinic start generated code]*/
/*[clinic end generated code: output=da39a3ee5e6b4b0d input=3e35d4f708ecb6af]*/

#include "clinic/moduleobject.c.h"

/* Methods */

/*[clinic input]
module.__init__
    name: unicode
    doc: object = None

Create a module object.

The name must be a string; the optional doc argument can have any type.
[clinic start generated code]*/

static int
module___init___impl(PyModuleObject *self, PyObject *name, PyObject *doc)
/*[clinic end generated code: output=e7e721c26ce7aad7 input=57f9e177401e5e1e]*/
{
    PyObject *dict = self->md_dict;
    if (dict == NULL) {
        dict = PyDict_New();
        if (dict == NULL)
            return -1;
        self->md_dict = dict;
    }
    if (module_init_dict(self, dict, name, doc) < 0)
        return -1;
    return 0;
}

static void
module_dealloc(PyModuleObject *m)
{
    int verbose = _PyInterpreterState_GET_UNSAFE()->config.verbose;

    PyObject_GC_UnTrack(m);
    if (verbose && m->md_name) {
        PySys_FormatStderr("# destroy %S\n", m->md_name);
    }
    if (m->md_weaklist != NULL)
        PyObject_ClearWeakRefs((PyObject *) m);
    if (m->md_def && m->md_def->m_free)
        m->md_def->m_free(m);
    Py_XDECREF(m->md_dict);
    Py_XDECREF(m->md_name);
    if (m->md_state != NULL)
        PyMem_FREE(m->md_state);
    Py_TYPE(m)->tp_free((PyObject *)m);
}

static PyObject *
module_repr(PyModuleObject *m)
{
    PyInterpreterState *interp = _PyInterpreterState_Get();

    return PyObject_CallMethod(interp->importlib, "_module_repr", "O", m);
}

/* Check if the "_initializing" attribute of the module spec is set to true.
   Clear the exception and return 0 if spec is NULL.
 */
int
_PyModuleSpec_IsInitializing(PyObject *spec)
{
    if (spec != NULL) {
        _Py_IDENTIFIER(_initializing);
        PyObject *value = _PyObject_GetAttrId(spec, &PyId__initializing);
        if (value != NULL) {
            int initializing = PyObject_IsTrue(value);
            Py_DECREF(value);
            if (initializing >= 0) {
                return initializing;
            }
        }
    }
    PyErr_Clear();
    return 0;
}

static PyObject *
module_lookupattro(PyModuleObject *m, PyObject *name, int suppress)
{
    PyObject *attr, *mod_name, *getattr;
    if (Py_TYPE(m) != &PyModule_Type || m->md_dict == NULL ||
        !PyUnicode_Check(name)) {
        /* Fall back to default behavior in odd ball cases */
        attr = _PyObject_GenericGetAttrWithDict((PyObject *)m, name, NULL, 1);
    } else if (PyUnicode_GET_LENGTH(name) == 8 &&
               PyUnicode_READ_CHAR(name, 0) == '_' &&
               _PyUnicode_EqualToASCIIString(name, "__dict__")) {
        /* This is a data descriptor, it always takes precedence over
         * an entry in __dict__ */
        Py_INCREF(m->md_dict);
        return m->md_dict;
    } else if (PyUnicode_GET_LENGTH(name) == 9 &&
               PyUnicode_READ_CHAR(name, 0) == '_' &&
               _PyUnicode_EqualToASCIIString(name, "__class__")) {
        Py_INCREF(&PyModule_Type);
        return (PyObject *) &PyModule_Type;
    } else {
        /* Otherwise we have no other data descriptors, just look in the
         * dictionary  and elide the _PyType_Lookup */
        attr = _PyDict_GetItem_Unicode(m->md_dict, name);
        if (attr != NULL) {
            Py_INCREF(attr);
            return attr;
        }

        /* see if we're accessing a descriptor defined on the module type */
        attr = _PyType_Lookup(&PyModule_Type, name);
        if (attr != NULL) {
            assert(!PyDescr_IsData(
                attr)); /* it better not be a data descriptor */

            descrgetfunc f = attr->ob_type->tp_descr_get;
            if (f != NULL) {
                attr = f(attr, (PyObject *)m, (PyObject *)&PyModule_Type);
                if (attr == NULL &&
                    PyErr_ExceptionMatches(PyExc_AttributeError)) {
                    PyErr_Clear();
                }
            } else {
                Py_INCREF(attr); /* got a borrowed ref */
            }
        }
    }

    if (attr) {
        return attr;
    }
    if (PyErr_Occurred()) {
        if (suppress && PyErr_ExceptionMatches(PyExc_AttributeError)) {
            PyErr_Clear();
        }
        return NULL;
    }

    if (m->md_dict) {
        _Py_IDENTIFIER(__getattr__);
        getattr = _PyDict_GetItemId(m->md_dict, &PyId___getattr__);
        if (getattr) {
            PyObject* stack[1] = {name};
            PyObject *res = _PyObject_FastCall(getattr, stack, 1);
            if (res == NULL && suppress &&
                PyErr_ExceptionMatches(PyExc_AttributeError)) {
                PyErr_Clear();
            }
            return res;
        }
        _Py_IDENTIFIER(__name__);
        mod_name = _PyDict_GetItemId(m->md_dict, &PyId___name__);
        if (mod_name && PyUnicode_Check(mod_name)) {
            _Py_IDENTIFIER(__spec__);
            Py_INCREF(mod_name);
            PyObject *spec = _PyDict_GetItemId(m->md_dict, &PyId___spec__);
            Py_XINCREF(spec);
            if (!suppress) {
                if (_PyModuleSpec_IsInitializing(spec)) {
                    PyErr_Format(PyExc_AttributeError,
                                "partially initialized "
                                "module '%U' has no attribute '%U' "
                                "(most likely due to a circular import)",
                                mod_name, name);
                }
                else {
                    PyErr_Format(PyExc_AttributeError,
                                "module '%U' has no attribute '%U'",
                                mod_name, name);
                }
            }
            Py_XDECREF(spec);
            Py_DECREF(mod_name);
            return NULL;
        }
    }
    if (!suppress) {
        PyErr_Format(
            PyExc_AttributeError, "module has no attribute '%U'", name);
    }
    return NULL;
}

static PyObject *
module_getattro(PyModuleObject *m, PyObject *name)
{
    return module_lookupattro(m, name, 0);
}

static int
module_traverse(PyModuleObject *m, visitproc visit, void *arg)
{
    if (m->md_def && m->md_def->m_traverse) {
        int res = m->md_def->m_traverse((PyObject*)m, visit, arg);
        if (res)
            return res;
    }
    Py_VISIT(m->md_dict);
    return 0;
}

static int
module_clear(PyModuleObject *m)
{
    if (m->md_def && m->md_def->m_clear) {
        int res = m->md_def->m_clear((PyObject*)m);
        if (res)
            return res;
    }
    Py_CLEAR(m->md_dict);
    return 0;
}

static PyObject *
module_dir(PyObject *self, PyObject *args)
{
    _Py_IDENTIFIER(__dict__);
    _Py_IDENTIFIER(__dir__);
    PyObject *result = NULL;
    PyObject *dict = _PyObject_GetAttrId(self, &PyId___dict__);

    if (dict != NULL) {
        if (PyDict_Check(dict)) {
            PyObject *dirfunc = _PyDict_GetItemIdWithError(dict, &PyId___dir__);
            if (dirfunc) {
                result = _PyObject_CallNoArg(dirfunc);
            }
            else if (!PyErr_Occurred()) {
                result = PyDict_Keys(dict);
            }
        }
        else {
            const char *name = PyModule_GetName(self);
            if (name)
                PyErr_Format(PyExc_TypeError,
                             "%.200s.__dict__ is not a dictionary",
                             name);
        }
    }

    Py_XDECREF(dict);
    return result;
}

static PyMethodDef module_methods[] = {
    {"__dir__", module_dir, METH_NOARGS,
     PyDoc_STR("__dir__() -> list\nspecialized dir() implementation")},
    {0}
};

PyTypeObject PyModule_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "module",                                   /* tp_name */
    sizeof(PyModuleObject),                     /* tp_basicsize */
    0,                                          /* tp_itemsize */
    (destructor)module_dealloc,                 /* tp_dealloc */
    0,                                          /* tp_vectorcall_offset */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
    0,                                          /* tp_as_async */
    (reprfunc)module_repr,                      /* tp_repr */
    0,                                          /* tp_as_number */
    0,                                          /* tp_as_sequence */
    0,                                          /* tp_as_mapping */
    0,                                          /* tp_hash */
    0,                                          /* tp_call */
    0,                                          /* tp_str */
    (getattrofunc)module_getattro,              /* tp_getattro */
    PyObject_GenericSetAttr,                    /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,                    /* tp_flags */
    module___init____doc__,                     /* tp_doc */
    (traverseproc)module_traverse,              /* tp_traverse */
    (inquiry)module_clear,                      /* tp_clear */
    0,                                          /* tp_richcompare */
    offsetof(PyModuleObject, md_weaklist),      /* tp_weaklistoffset */
    0,                                          /* tp_iter */
    0,                                          /* tp_iternext */
    module_methods,                             /* tp_methods */
    module_members,                             /* tp_members */
    0,                                          /* tp_getset */
    0,                                          /* tp_base */
    0,                                          /* tp_dict */
    0,                                          /* tp_descr_get */
    0,                                          /* tp_descr_set */
    offsetof(PyModuleObject, md_dict),          /* tp_dictoffset */
    module___init__,                            /* tp_init */
    PyType_GenericAlloc,                        /* tp_alloc */
    PyType_GenericNew,                          /* tp_new */
    PyObject_GC_Del,                            /* tp_free */
};

PyObject *
_PyModule_LookupAttr(PyObject *mod, PyObject *name)
{
    return module_lookupattro((PyModuleObject *)mod, name, 1);
}


static int
strictmodule_init(PyStrictModuleObject *self, PyObject *args, PyObject *kwds)
{
    PyObject* d;
    PyObject* enable_patching;
    static char *kwlist[] = {"d", "enable_patching", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "OO", kwlist, &d, &enable_patching)){
        return -1;
    }

    if (d == NULL || !PyDict_CheckExact(d)) {
        return -1;
    }
    if (enable_patching == NULL) {
        return -1;
    }

    return 0;
}

PyObject *
PyStrictModule_New(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyStrictModuleObject *self;
    PyObject* d = NULL;
    PyObject* enable_patching = NULL;
    static char *kwlist[] = {"d", "enable_patching", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OO", kwlist, &d, &enable_patching)){
        return NULL;
    }

    if (d != NULL && !PyDict_CheckExact(d)) {
        PyErr_SetString(PyExc_TypeError, "StrictModule.__new__ expected dict for 1st argument");
        return NULL;
    }
    if (enable_patching != NULL &&
       (enable_patching != Py_True && enable_patching != Py_False)) {
        PyErr_SetString(PyExc_TypeError, "StrictModule.__new__ expected bool for 2nd argument");
        return NULL;
    }

    self = (PyStrictModuleObject *)type->tp_alloc(type, 0);
    if (self == NULL) {
        return NULL;
    }

    self->globals = d;
    Py_XINCREF(d);
    if (enable_patching == Py_True) {
        self->global_setter = d;
        Py_XINCREF(d);
    }
    return (PyObject *)self;
}

static void
strictmodule_dealloc(PyStrictModuleObject *m)
{
    Py_XDECREF(m->globals);
    Py_XDECREF(m->global_setter);
    module_dealloc((PyModuleObject *)m);
}

static int
strictmodule_traverse(PyStrictModuleObject *m, visitproc visit, void *arg)
{
    Py_VISIT(m->globals);
    Py_VISIT(m->global_setter);
    return 0;
}

static int
strictmodule_clear(PyStrictModuleObject *m)
{
    Py_CLEAR(m->globals);
    Py_CLEAR(m->global_setter);
    return 0;
}

int strictmodule_is_unassigned(PyObject *dict, PyObject *name) {
    if (!PyUnicode_Check(name)) {
        // somehow name is not unicode
        return 0;
    }
    else {
        PyObject *assigned_name = PyUnicode_FromFormat("<assigned:%U>", name);
        if (assigned_name == NULL) {
            return -1;
        }
        PyObject *assigned_status = _PyDict_GetItem_Unicode(dict, assigned_name);
        Py_DECREF(assigned_name);
        if (assigned_status == Py_False) {
            // name has a corresponding <assigned:name> that's False
            return 1;
        }
        return 0;
    }
}

static PyObject * strict_module_dict_get(PyObject *self, void *closure)
{
    PyStrictModuleObject *m = (PyStrictModuleObject *)self;
    if (m->globals == NULL) {
        // module is uninitialized, return None
        Py_RETURN_NONE;
    }
    assert(PyDict_Check(m->globals));

    PyObject *dict = PyDict_New();
    if (dict == NULL) {
        goto error;
    }
    Py_ssize_t i = 0;
    PyObject *key, *value;


    while (PyDict_Next(m->globals, &i, &key, &value)){
        if (key == NULL || value == NULL) {
            goto error;
        }
        if (PyUnicode_Check(key)) {
            PyObject * angle = PyUnicode_FromString("<");
            if (angle == NULL) {
                goto error;
            }
            Py_ssize_t angle_pos = PyUnicode_Find(key, angle, 0, PyUnicode_GET_LENGTH(key), 1);
            Py_DECREF(angle);
            if (angle_pos == -2) {
                goto error;
            }
            if (angle_pos != 0) {
                // name does not start with <, report in __dict__
                int unassigned = strictmodule_is_unassigned(m->globals, key);
                if (unassigned < 0) {
                    goto error;
                } else if (!unassigned) {
                    const char* key_string = PyUnicode_AsUTF8(key);
                    if (key_string == NULL || PyDict_SetItemString(dict, key_string, value) < 0) {
                        goto error;
                    }
                }
            }

        } else {
            if (PyDict_SetItem(dict, key, value) < 0) {
                goto error;
            }
        }
    }
    return dict;
error:
    Py_XDECREF(dict);
    return NULL;
}

static PyObject * PyStrictModule_GetNameObject(PyStrictModuleObject *self)
{
    PyStrictModuleObject *m = (PyStrictModuleObject *)self;
    _Py_IDENTIFIER(__name__);
    PyObject * name;
    if (m->globals == NULL ||
        (name = _PyDict_GetItemId(m->globals, &PyId___name__)) == NULL ||
        !PyUnicode_Check(name))
    {
        PyErr_SetString(PyExc_SystemError, "nameless module");
        return NULL;
    }
    Py_INCREF(name);
    return name;
}

static PyObject * strict_module_name_get(PyObject *self, void *closure)
{
    PyObject *name = PyStrictModule_GetNameObject((PyStrictModuleObject*) self);
    if (name == NULL) {
        PyErr_Clear();
        PyErr_SetString(PyExc_AttributeError, "strict module has no attribute __name__");
        return NULL;
    }
    // already incref
    return name;
}

static PyObject * strict_module_patch_enabled(PyObject *self, void *closure)
{
    if (((PyStrictModuleObject *) self) -> global_setter != NULL) {
        return Py_True;
    }
    return Py_False;
}

static PyObject *
strictmodule_repr(PyStrictModuleObject *m)
{
    PyObject *repr_obj;
    if (!PyStrictModule_Check(m)) {
        PyErr_BadArgument();
        return NULL;
    }

    PyObject * name = PyStrictModule_GetNameObject(m);
    if (name == NULL) {
        PyErr_Clear();
        name = PyUnicode_FromString("?");
        if (name == NULL) {
            return NULL;
        }
    }
    _Py_IDENTIFIER(__file__);
    PyObject *d;
    PyObject *filename;

    d = m->globals;
    if (d == NULL ||
        (filename = _PyDict_GetItemId(d, &PyId___file__)) == NULL ||
        !PyUnicode_Check(filename))
    {
        repr_obj = PyUnicode_FromFormat("<module '%U'>", name);
    } else {
        repr_obj = PyUnicode_FromFormat("<module '%U' from '%U'>", name, filename);
    }
    Py_DECREF(name);
    return repr_obj;
}

static PyObject *
strictmodule_dir(PyObject *self, PyObject *args)
{
    _Py_IDENTIFIER(__dict__);
    PyObject *result = NULL;
    PyObject *dict = _PyObject_GetAttrId(self, &PyId___dict__);

    if (dict != NULL) {
        if (PyDict_Check(dict)) {
            PyObject *dirfunc = PyDict_GetItemString(dict, "__dir__");
            if (dirfunc) {
                result = _PyObject_CallNoArg(dirfunc);
            }
            else {
                result = PyDict_Keys(dict);
            }
        }
        else {
            PyObject *name = PyStrictModule_GetNameObject((PyStrictModuleObject *)self);
            if (name) {
                PyErr_Format(PyExc_TypeError, "%U.__dict__ is not a dictionary", name);
                Py_DECREF(name);
            }
        }
    }
    Py_XDECREF(dict);
    return result;
}

int _Py_do_strictmodule_patch(PyObject *self, PyObject *name, PyObject *value) {
    PyStrictModuleObject *mod = (PyStrictModuleObject *) self;
    PyObject * global_setter = mod->global_setter;
    if (global_setter == NULL){
        PyObject* repr = strictmodule_repr(mod);
        if (repr == NULL) {
            return -1;
        }
        PyErr_Format(PyExc_AttributeError,
                        "cannot modify attribute '%U' of strict module %U", name, repr);
        Py_DECREF(repr);
        return -1;
    }


    if (_PyObject_GenericSetAttrWithDict(self, name, value, global_setter) < 0) {
        return -1;
    }
    return 0;
}

static PyObject * strictmodule_patch(PyObject *self, PyObject *args)
{
    PyObject* name;
    PyObject* value;
    if (!PyArg_ParseTuple(args, "UO", &name, &value)) {
        return NULL;
    }
    if (_Py_do_strictmodule_patch(self, name, value) < 0) {
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject * strictmodule_patch_delete(PyObject *self, PyObject *args)
{
    PyObject* name;
    if (!PyArg_ParseTuple(args, "U", &name)) {
        return NULL;
    }
    if (_Py_do_strictmodule_patch(self, name, NULL) < 0) {
        return NULL;
    }
    Py_RETURN_NONE;
}


static PyObject *
strictmodule_lookupattro(PyStrictModuleObject *m, PyObject *name, int suppress)
{
    PyObject *attr;
    if (Py_TYPE(m) != &PyStrictModule_Type || !PyUnicode_Check(name)) {
        attr = NULL;
    } else if (PyUnicode_GET_LENGTH(name) == 9 &&
               PyUnicode_READ_CHAR(name, 0) == '_' &&
               _PyUnicode_EqualToASCIIString(name, "__class__")) {
        Py_INCREF(&PyStrictModule_Type);
        return (PyObject *)&PyStrictModule_Type;
    } else if (PyUnicode_GET_LENGTH(name) == 8 &&
               PyUnicode_READ_CHAR(name, 0) == '_' &&
               _PyUnicode_EqualToASCIIString(name, "__dict__")) {
        return strict_module_dict_get((PyObject *) m, NULL);
    } else if (PyUnicode_GET_LENGTH(name) == 8 &&
               PyUnicode_READ_CHAR(name, 0) == '_' &&
               _PyUnicode_EqualToASCIIString(name, "__name__")) {
        /* This is a data descriptor, it always takes precedence over
         * an entry in __dict__ */
        return strict_module_name_get((PyObject *) m, NULL);
    } else {
        /* Otherwise we have no other data descriptors, just look in the
         * dictionary  and elide the _PyType_Lookup */
        if (m->globals) {
            int name_unassigned = strictmodule_is_unassigned(m->globals, name);
            if (name_unassigned < 0) {
                return NULL;
            } else if (!name_unassigned) {
                attr = _PyDict_GetItem_Unicode(m->globals, name);
                if (attr != NULL) {
                    Py_INCREF(attr);
                    return attr;
                }
            }
        }

        /* see if we're accessing a descriptor defined on the module type */
        attr = _PyType_Lookup(&PyStrictModule_Type, name);
        if (attr != NULL) {
            assert(!PyDescr_IsData(
                attr)); /* it better not be a data descriptor */

            descrgetfunc f = attr->ob_type->tp_descr_get;
            if (f != NULL) {
                attr = f(attr, (PyObject *)m, (PyObject *)&PyStrictModule_Type);
                if (attr == NULL &&
                    PyErr_ExceptionMatches(PyExc_AttributeError)) {
                    PyErr_Clear();
                }
            } else {
                Py_INCREF(attr); /* got a borrowed ref */
            }
        }
    }

    if (attr) {
        return attr;
    }
    if (PyErr_Occurred()) {
        if (suppress && PyErr_ExceptionMatches(PyExc_AttributeError)) {
            PyErr_Clear();
        }
        return NULL;
    }
    if (m->globals) {
        _Py_IDENTIFIER(__getattr__);
        PyObject *getattr = _PyDict_GetItemId(m->globals, &PyId___getattr__);
        if (getattr) {
            PyObject* stack[1] = {name};
            PyObject *res = _PyObject_FastCall(getattr, stack, 1);
            if (res == NULL && suppress &&
                PyErr_ExceptionMatches(PyExc_AttributeError)) {
                PyErr_Clear();
            }
            return res;
        }

        _Py_IDENTIFIER(__name__);
        PyObject *mod_name = _PyDict_GetItemId(m->globals, &PyId___name__);
        if (mod_name && PyUnicode_Check(mod_name)) {
            if (!suppress) {
                PyErr_Format(PyExc_AttributeError,
                                "strict module '%U' has no attribute '%U'",
                                mod_name,
                                name);
            }
            return NULL;
        }
    }
    if (!suppress) {
        PyErr_Format(
            PyExc_AttributeError, "strict module has no attribute '%U'", name);
    }
    return NULL;
}

static PyObject *
strictmodule_getattro(PyStrictModuleObject *m, PyObject *name)
{
    return strictmodule_lookupattro(m, name, 0);
}

static int
strictmodule_setattro(PyStrictModuleObject *m, PyObject *name, PyObject *value)
{
    PyObject *modname = PyStrictModule_GetNameObject(m);
    if (modname == NULL) {
        return -1;
    }
    if (value == NULL) {
        PyErr_Format(PyExc_AttributeError,
                        "cannot delete attribute '%U' of strict module %U", name, modname);
    } else {
        PyErr_Format(PyExc_AttributeError,
                        "cannot modify attribute '%U' of strict module %U", name, modname);
    }

    Py_DECREF(modname);
    return -1;
}

static PyMemberDef strictmodule_members[] = {
    {NULL}
};

static PyMethodDef strictmodule_methods[] = {
    {"__dir__", strictmodule_dir, METH_NOARGS,
     PyDoc_STR("__dir__() -> list\nspecialized dir() implementation")},
     {
         "patch", strictmodule_patch, METH_VARARGS, PyDoc_STR("Patch a strict module. Only enabled for testing")
     },
     {
         "patch_delete",
         strictmodule_patch_delete,
         METH_VARARGS,
         PyDoc_STR("Patch by deleting a field from strict module. Only enabled for testing")
     },
    {0}
};

static PyGetSetDef strict_module_getset[] = {
    {"__dict__", strict_module_dict_get, NULL, NULL, NULL},
    {"__name__", strict_module_name_get, NULL, NULL, NULL},
    {"__patch_enabled__", strict_module_patch_enabled, NULL, NULL, NULL},
    {NULL}
};


PyTypeObject PyStrictModule_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "StrictModule",                             /* tp_name */
    sizeof(PyStrictModuleObject),               /* tp_basicsize */
    0,                                          /* tp_itemsize */
    (destructor)strictmodule_dealloc,           /* tp_dealloc */
    0,                                          /* tp_print */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
    0,                                          /* tp_reserved */
    (reprfunc)strictmodule_repr,                /* tp_repr */
    0,                                          /* tp_as_number */
    0,                                          /* tp_as_sequence */
    0,                                          /* tp_as_mapping */
    0,                                          /* tp_hash */
    0,                                          /* tp_call */
    0,                                          /* tp_str */
    (getattrofunc)strictmodule_getattro,        /* tp_getattro */
    (setattrofunc)strictmodule_setattro,        /* tp_setattro */
    0,                                          /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,    /* tp_flags */
    0,                                          /* tp_doc */
    (traverseproc)strictmodule_traverse,        /* tp_traverse */
    (inquiry)strictmodule_clear,                /* tp_clear */
    0,                                          /* tp_richcompare */
    0,                                          /* tp_weaklistoffset */
    0,                                          /* tp_iter */
    0,                                          /* tp_iternext */
    strictmodule_methods,                       /* tp_methods */
    strictmodule_members,                       /* tp_members */
    strict_module_getset,                       /* tp_getset */
    &PyModule_Type,                             /* tp_base */
    0,                                          /* tp_dict */
    0,                                          /* tp_descr_get */
    0,                                          /* tp_descr_set */
    0,                                          /* tp_dictoffset */
    (initproc)strictmodule_init,                /* tp_init */
    PyType_GenericAlloc,                        /* tp_alloc */
    PyStrictModule_New,                         /* tp_new */
    PyObject_GC_Del,                            /* tp_free */
};

 Py_ssize_t strictmodule_dictoffset = offsetof(PyStrictModuleObject, globals);
