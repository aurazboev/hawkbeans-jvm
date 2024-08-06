/* 
 * This file is part of the Hawkbeans JVM developed by
 * the HExSA Lab at Illinois Institute of Technology.
 *
 * Copyright (c) 2019, Kyle C. Hale <khale@cs.iit.edu>
 *
 * All rights reserved.
 *
 * Author: Kyle C. Hale <khale@cs.iit.edu>
 *
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the 
 * file "LICENSE.txt".
 */
#include <stdlib.h>
#include <string.h>

#include <types.h>
#include <class.h>
#include <stack.h>
#include <mm.h>
#include <thread.h>
#include <exceptions.h>
#include <bc_interp.h>
#include <gc.h>

extern jthread_t * cur_thread;

/* 
 * Maps internal exception identifiers to fully
 * qualified class paths for the exception classes.
 * Note that the ones without fully qualified paths
 * will not be properly raised. 
 *
 * TODO: add the classes for these
 *
 */
static const char * excp_strs[16] __attribute__((used)) =
{
	"java/lang/NullPointerException",
	"java/lang/IndexOutOfBoundsException",
	"java/lang/ArrayIndexOutOfBoundsException",
	"IncompatibleClassChangeError",
	"java/lang/NegativeArraySizeException",
	"java/lang/OutOfMemoryError",
	"java/lang/ClassNotFoundException",
	"java/lang/ArithmeticException",
	"java/lang/NoSuchFieldError",
	"java/lang/NoSuchMethodError",
	"java/lang/RuntimeException",
	"java/io/IOException",
	"FileNotFoundException",
	"java/lang/InterruptedException",
	"java/lang/NumberFormatException",
	"java/lang/StringIndexOutOfBoundsException",
};

int 
hb_excp_str_to_type (char * str)
{
    for (int i = 0; i < sizeof(excp_strs)/sizeof(char*); i++) {
        if (strstr(excp_strs[i], str))
                return i;
    }
    return -1;
}



/*
 * Throws an exception given an internal ID
 * that refers to an exception type. This is to 
 * be used by the runtime (there is no existing
 * exception object, so we have to create a new one
 * and init it).f
 *
 * @return: none. exits on failure.
 *
 */
// WRITTEN
void
hb_throw_and_create_excp (u1 type)
{
    const char* exceptionClassName = excp_strs[type];
    java_class_t* exceptionClass = hb_get_or_load_class(exceptionClassName);

    obj_ref_t* exceptionObject = gc_obj_alloc(exceptionClass);

    if (hb_invoke_ctor(exceptionObject)) 
	{
        HB_ERR("Constructor invocation failed\n");
        exit(EXIT_FAILURE);
    }

    hb_throw_exception(exceptionObject);
}



/* 
 * gets the exception message from the object 
 * ref referring to the exception object.
 *
 * NOTE: caller must free the string
 *
 */
static char *
get_excp_str (obj_ref_t * eref)
{
	char * ret;
	native_obj_t * obj = (native_obj_t*)eref->heap_ptr;
		
	obj_ref_t * str_ref = obj->fields[0].obj;
	native_obj_t * str_obj;
	obj_ref_t * arr_ref;
	native_obj_t * arr_obj;
	int i;
	
	if (!str_ref) {
		return NULL;
	}

	str_obj = (native_obj_t*)str_ref->heap_ptr;
	
	arr_ref = str_obj->fields[0].obj;

	if (!arr_ref) {
		return NULL;
	}

	arr_obj = (native_obj_t*)arr_ref->heap_ptr;

	ret = malloc(arr_obj->flags.array.length+1);

	for (i = 0; i < arr_obj->flags.array.length; i++) {
		ret[i] = arr_obj->fields[i].char_val;
	}

	ret[i] = 0;

	return ret;
}


/*
 * Throws an exception using an
 * object reference to some exception object (which
 * implements Throwable). To be used with athrow.
 * If we're given a bad reference, we throw a 
 * NullPointerException.
 *
 * @return: none. exits on failure.  
 *
 */
void
hb_throw_exception (obj_ref_t * eref)
{
    native_obj_t* native_obj = (native_obj_t*)eref->heap_ptr;
    java_class_t* class_obj = native_obj->class;
    if (!class_obj) {
        exit(EXIT_FAILURE);
    }

    const char* class_name = hb_get_class_name(class_obj);
    HB_ERR("Exception in thread %s %s at %s\n", cur_thread->name, class_name, hb_get_class_name(cur_thread->class) );

    method_info_t* method_info = cur_thread->cur_frame->minfo;
    excp_table_t* excp_table = method_info->code_attr->excp_table;
    u2 excp_table_length = method_info->code_attr->excp_table_len;

    for (u2 i = 0; i < excp_table_length; i++) {
        u2 catch_type_idx = excp_table[i].catch_type;
        CONSTANT_Class_info_t* catch_class_info = (CONSTANT_Class_info_t*)class_obj->const_pool[catch_type_idx];
        const char* catch_class_name = hb_get_const_str(catch_class_info->name_idx, class_obj);
        u2 low = excp_table[i].start_pc;
        u2 high = excp_table[i].end_pc;
        u2 pc = cur_thread->cur_frame->pc;

        if (((pc - high) * (pc - low)) < 0 && !strcmp(catch_class_name, class_name)) {
            var_t exception_var;
            exception_var.obj = eref;
            op_stack_t* stack = cur_thread->cur_frame->op_stack;
            stack->oprs[++stack->sp] = exception_var;
            cur_thread->cur_frame->pc = excp_table[i].handler_pc;
            hb_exec_method(cur_thread);
            return;
        }
    }

    hb_pop_frame(cur_thread);
    if (!cur_thread->cur_frame) {
        return;
    }
    hb_throw_exception(eref);
}
