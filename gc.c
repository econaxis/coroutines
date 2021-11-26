#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <glib.h>
#include <math.h>
#include <assert.h>


typedef double *GCDouble;
typedef char *GCString;
typedef void *GCPointer;
typedef int *GCInt;
typedef struct {
    void **arr;
    size_t len;
} ArrayType;


struct KnownObjects {
    void *buf[10000];
    int index;
} known;

struct StackMap {
    int index;

    void (*fn_ptr)(void *, GHashTable *);
};

extern struct StackMap STACK_MAP[];

void *gc_malloc(size_t size) {
    void *ptr;
    ptr = malloc(size);
    memset(ptr, 0, size);

    printf("Allocating %p\n", ptr);
    known.buf[known.index++] = ptr;
}


void *gc_free(void *ptr, int index_hint) {
    if (index_hint != -1) {
        assert(known.buf[index_hint] == ptr);
        free(ptr);
        known.buf[index_hint] = NULL;
    } else {
        for (int i = 0; i < known.index; i++) {
            if (known.buf[i] == ptr) {
                //todo: actually free here
                known.buf[i] = NULL;
                return NULL;
            }
        }
        assert(false && "Trying to free a non-gc owned pointer");
    }
}


void gc_mark_plain(const void *obj, GHashTable *table) {
    g_hash_table_add(table, (gpointer) obj);
}


void gc_mark_ArrayType1(ArrayType *arr, void (*fnptr)(void *, GHashTable *), GHashTable *table) {
    if (arr != NULL) {
        if (arr->arr != NULL) {
            gc_mark_plain((const void *) arr->arr, table);
            for (int i = 0; i < arr->len; i++) {
                gc_mark_plain(arr->arr[i], table);
                fnptr(arr->arr[i], table);
            }
        }
    }
}


void gc_mark_ArrayType(ArrayType *arr, GHashTable *table) {
    gc_mark_ArrayType1(arr, (void (*)(void *, GHashTable *)) gc_mark_plain, table);
}

void gc_mark_GCString(GCString *str, GHashTable *table) {
    gc_mark_plain((void *) *str, table);
}


void gc_find_unused(GHashTable *marked) {
    int deadcount = 0;
    for (int i = 0; i < known.index; i++) {
        gconstpointer to_check = (gconstpointer) known.buf[i];
        if (to_check == NULL) {
            // NULL means we've deallocated that chunk
            continue;
        }
        if (g_hash_table_contains(marked, to_check)) {
        } else {
            gc_free(to_check, i);
            deadcount++;
        }
    }

    int livecount = 0;
    for (int i = 0; i < known.index; i++) {
        if (known.buf[i] != NULL) {
            livecount++;
        }
    }
    printf("Dead count: %d; Livecount: %d\n", deadcount, livecount);

}

void *g_stack_frames[1000];
size_t g_sf_index = 0;

void gc_push_stack_frame(void *ptr) {
    g_stack_frames[g_sf_index++] = ptr;
}

void gc_pop_stack_frame() {
    g_stack_frames[g_sf_index] = NULL;
    g_sf_index--;
}

void *gc_peek_stack_frame(size_t index) {
    return g_stack_frames[index];
}

void gc_run() {
    GHashTable *table = g_hash_table_new(g_direct_hash, g_direct_equal);

    for (ssize_t i = g_sf_index - 1; i >= 0; i--) {
        void *cur_stack = gc_peek_stack_frame(i);
        int stack_id = *(int *) cur_stack;
        void (*ptr)(void *, GHashTable *) = STACK_MAP[stack_id].fn_ptr;
        ptr(cur_stack, table);
    }
    gc_find_unused(table);
    g_hash_table_destroy(table);

}

struct Nested1 {
    ArrayType arr;
    GCString identifier;
};

struct ArrOfNested {
    ArrayType arr;

    void (*fnptr)(void *, GHashTable *);
};

void gc_mark_Nested1(struct Nested1 *stc, GHashTable *table);

void gc_mark_ArrOfNested(struct ArrOfNested *arr, GHashTable *table) {
    if (arr != NULL && arr->fnptr != NULL) {
        gc_mark_ArrayType1(&arr->arr, arr->fnptr, table);
    }
}

const int END_CODE_HERE;


GCString gc_string(size_t length) {
    return (GCString) gc_malloc(length);
}

GCString gc_string1(const char *source) {
    GCString alloc = gc_string(strlen(source) + 1);
    strcpy(alloc, source);
    return alloc;
}

ArrayType alloc_string_arr(size_t length) {
    ArrayType arr;
    arr.len = length;
    arr.arr = gc_malloc(length * sizeof(void *));
    for (int i = 0; i < length; i++) {
        long rand_num = random();
        long strlen = floor(log10((double) rand_num)) + 1;
        arr.arr[i] = gc_string(strlen);
        snprintf(arr.arr[i], strlen, "%ld", rand_num);
    }

    gc_run();
    printf("%s\n", arr.arr[0]);
    return arr;
}


struct Nested1 *alloc_nested(int arr_len, const char *identifier) {
    struct Nested1 *ret = gc_malloc(sizeof(struct Nested1));
    ret->arr = alloc_string_arr(arr_len);
    ret->identifier = gc_string1(identifier);
    gc_run();
    return ret;
}

struct ArrOfNested alloc_nested_array() {
    struct ArrOfNested arr;
    arr.fnptr = gc_mark_Nested1;
    arr.arr.len = 100;
    arr.arr.arr = gc_malloc(arr.arr.len * sizeof(void *));
    for (int i = 0; i < arr.arr.len; i++) {
        arr.arr.arr[i] = alloc_nested(19, "hello world");
    }
    return arr;
}

void moving_delete(ArrayType *arr, size_t index) {
    assert(index < arr->len);
    for (size_t i = index; i < arr->len - 1; i++) {
        arr->arr[i] = arr->arr[i + 1];
    }
    arr->len--;
}

void array_push(ArrayType *arr, void *elem) {
    arr->arr[arr->len++] = elem;
}

struct Nested1 *alt_fun2() {
    struct Nested1 *nested;
    nested = alloc_nested(10, "hello");
    return nested;
}

ArrayType alt_fun1() {
    ArrayType arr = alloc_string_arr(100);

    while (arr.len > 9) {
        for (int i = 0; i < arr.len; i += 10) {
            moving_delete(&arr, i);
        }
        printf("Running gc\n");
        gc_run();
    }

    struct Nested1 *nested = alt_fun2();
    gc_run();
    gc_run();

    return arr;
}

int main() {
    srand(time(NULL));
    ArrayType main_array = alt_fun1();
    GCString third = main_array.arr[3];
    GCString fourth = main_array.arr[4];
    printf("third val: %s\n", third);

    for (int i = 0; i < main_array.len; i++) {
        printf("Got %s\n", (char *) main_array.arr[i]);
    }
    gc_run();
    main_array.arr = NULL;
    main_array.len = 0;
    fourth = NULL;
    gc_run();

    struct ArrOfNested nested = alloc_nested_array();
    gc_run();
    struct Nested1 *ptr;
    ptr = nested.arr.arr[0];
    printf("Nested %s\n", (char *) ptr->identifier);


    printf("third val: %s\n", third);
}

//
//#include <assert.h>
//#include <stdbool.h>
//#include <glib.h>
//#include <glib/gtypes.h>
//#include <stdio.h>
//


//
//char *abc = "abcdef";
//
//struct StackFrame_main {
//    int stack_id;
//    struct TestStruct s;
//    ArrayType arr;
//};
//
//void gc_mark_StackFrame_main(struct StackFrame_main *stck, GHashTable *table) {
//    assert(stck->stack_id == 0);
//    gc_mark_TestStruct(&stck->s, table);
//    if (*(char *) &stck->arr != 0) {
//        gc_mark_array(&stck->arr, table);
//    }
//}
//
//struct StackFrame_alt_fun {
//    int stack_id;
//    ArrayType arr;
//};
//
//void gc_mark_StackFrame_alt_fun(struct StackFrame_alt_fun *stck, GHashTable *table) {
//    assert(stck->stack_id == 1);
//    gc_mark_array(&stck->arr, table);
//}
//
//ArrayType alt_fun() {
//    struct StackFrame_alt_fun stck;
//    stck.stack_id = 1;
//    stck.arr.len = 100;
//    stck.arr.arr = gc_malloc(stck.arr.len * sizeof(void *));
//
//    for (int i = 0; i < stck.arr.len; i++) {
//        stck.arr.arr[i] = gc_malloc(3);
//        snprintf(stck.arr.arr[i], 3, "%d", i);
//    }
//    return stck.arr;
//}
//
//
//

//
//void anything_left() {
//    for (int i = 0; i < known.index; i++) {
//        if (known.buf[i] != NULL) {
//            printf("%p still allocated\n", known.buf[i]);
//            assert(false);
//        }
//    }
//}
//
//int main() {
//    struct StackFrame_main stack_frame;
//    memset(&stack_frame, 0, sizeof stack_frame);
//    stack_frame.stack_id = 0;
//
//
//    init_test_struct(&stack_frame.s);
//
//
//    printf("First delete\n");
//    stack_frame.s.string = abc;
//    run_main_gc(&stack_frame);
//    printf("Second delete\n");
//    stack_frame.s.dbl = NULL;
//    run_main_gc(&stack_frame);
//    printf("Third delete\n");
//    stack_frame.arr = alt_fun();
//    run_main_gc(&stack_frame);
//    printf("Fourth delete\n");
//    memset(&stack_frame.arr, 0, sizeof stack_frame.arr);
//    run_main_gc(&stack_frame);
//
//    printf("Final\n");
//    memset(&stack_frame.s, 0, sizeof stack_frame.s);
//    run_main_gc(&stack_frame);
//
//    anything_left();
//}