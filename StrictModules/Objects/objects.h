// Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
#ifndef __STRICTM_OBJECTS_H__
#define __STRICTM_OBJECTS_H__
#include "StrictModules/Objects/base_object.h"
#include "StrictModules/Objects/callable.h"
#include "StrictModules/Objects/constants.h"
#include "StrictModules/Objects/dict_object.h"
#include "StrictModules/Objects/exception_object.h"
#include "StrictModules/Objects/function.h"
#include "StrictModules/Objects/helper.h"
#include "StrictModules/Objects/instance.h"
#include "StrictModules/Objects/iterable_objects.h"
#include "StrictModules/Objects/iterator_objects.h"
#include "StrictModules/Objects/module.h"
#include "StrictModules/Objects/module_type.h"
#include "StrictModules/Objects/numerics.h"
#include "StrictModules/Objects/object_type.h"
#include "StrictModules/Objects/string_object.h"
#include "StrictModules/Objects/type.h"
#include "StrictModules/Objects/type_type.h"
#include "StrictModules/Objects/unknown.h"

namespace strictmod::objects {
//--------------------Builtin Type Declarations-----------------------
std::shared_ptr<StrictModuleObject> BuiltinsModule();

std::shared_ptr<StrictType> ObjectType();
std::shared_ptr<StrictType> TypeType();
std::shared_ptr<StrictType> ModuleType();
std::shared_ptr<StrictType> UnknownType();
std::shared_ptr<StrictType> IntType();
std::shared_ptr<StrictType> BoolType();
std::shared_ptr<StrictType> StrType();

std::shared_ptr<StrictType> ListType();
std::shared_ptr<StrictType> TupleType();
std::shared_ptr<StrictType> SetType();
std::shared_ptr<StrictType> FrozensetType();
std::shared_ptr<StrictType> DictObjectType();
std::shared_ptr<StrictType> DictViewType();
std::shared_ptr<StrictType> SequenceIteratorType();
std::shared_ptr<StrictType> SetIteratorType();
std::shared_ptr<StrictType> CallableIteratorType();
std::shared_ptr<StrictType> GenericObjectIteratorType();
std::shared_ptr<StrictType> GeneratorFuncIteratorType();
std::shared_ptr<StrictType> NoneType();
std::shared_ptr<StrictType> NotImplementedType();

std::shared_ptr<StrictType> FunctionType();
std::shared_ptr<StrictType> BuiltinFunctionOrMethodType();
std::shared_ptr<StrictType> MethodDescrType();
std::shared_ptr<StrictType> MethodType();

std::shared_ptr<StrictType> ExceptionType();
std::shared_ptr<StrictType> TypeErrorType();
std::shared_ptr<StrictType> AttributeErrorType();
std::shared_ptr<StrictType> ValueErrorType();
std::shared_ptr<StrictType> NameErrorType();
std::shared_ptr<StrictType> StopIterationType();
std::shared_ptr<StrictType> KeyErrorType();

//--------------------Builtin Constant Declarations-----------------------
std::shared_ptr<BaseStrictObject> NoneObject();
std::shared_ptr<BaseStrictObject> NotImplemented();
std::shared_ptr<BaseStrictObject> StrictTrue();
std::shared_ptr<BaseStrictObject> StrictFalse();

/* Create a dictionary containing all values in the builtins module
 */
static inline std::unique_ptr<DictType> getBuiltinsDict() {
  return std::make_unique<DictType>(BuiltinsModule()->getDict());
}

//--------------------Constant names-------------------------
static const std::string kDunderGet = "__get__";
static const std::string kDunderSet = "__set__";
static const std::string kDunderDel = "__del__";
static const std::string kDunderContains = "__contains__";
static const std::string kDunderGetItem = "__getitem__";
static const std::string kDunderClassGetItem = "__class_getitem__";
static const std::string kDunderSetItem = "__setitem__";
static const std::string kDunderDelItem = "__delitem__";
static const std::string kDunderCall = "__call__";
static const std::string kDunderBool = "__bool__";
static const std::string kDunderLen = "__len__";
static const std::string kDunderIter = "__iter__";
static const std::string kDunderNext = "__next__";

/* indices corresponds to enum values in Python-ast.h
 * Do not change the order of names.
 */
static const std::string kBinOpNames[] = {"",
                                          "__add__",
                                          "__sub__",
                                          "__mul__",
                                          "__matmul__",
                                          "__truediv__",
                                          "__mod__",
                                          "__pow__",
                                          "__lshift__",
                                          "__rshift__",
                                          "__or__",
                                          "__xor__",
                                          "__and__",
                                          "__floordiv__"};

static const std::string kBinOpDisplays[] =
    {"", "+", "-", "*", "@", "/", "%", "**", "<<", ">>", "|", "^", "&", "//"};

static const std::string kRBinOpNames[] = {"",
                                           "__radd__",
                                           "__rsub__",
                                           "__rmul__",
                                           "__rmatmul__",
                                           "__rtruediv__",
                                           "__rmod__",
                                           "__rpow__",
                                           "__rlshift__",
                                           "__rrshift__",
                                           "__ror__",
                                           "__rxor__",
                                           "__rand__",
                                           "__rfloordiv__"};

static const std::string kUnaryOpNames[] = {"",
                                            "__invert__",
                                            "",
                                            "__pos__",
                                            "__neg__"};

static const std::string kUnaryOpDisplays[] = {"", "~", "not", "+", "-"};

static const std::string kCmpOpNames[] = {"",
                                          "__eq__",
                                          "__ne__",
                                          "__lt__",
                                          "__le__",
                                          "__gt__",
                                          "__ge__",
                                          "",
                                          "",
                                          "__contains__",
                                          ""};

static const std::string kCmpOpDisplays[] =
    {"", "==", "!=", "<", "<=", ">", ">=", "is", "is not", "in", "not in"};

static const std::string kRCmpOpNames[] = {"",
                                           "__eq__",
                                           "__ne__",
                                           "__gt__",
                                           "__ge__",
                                           "__lt__",
                                           "__le__",
                                           "",
                                           "",
                                           "",
                                           ""};

static const int kIterationLimit = 10000;
} // namespace strictmod::objects
#endif // __STRICTM_OBJECTS_H__
