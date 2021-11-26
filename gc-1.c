
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <glib.h>
#include <assert.h>

#pragma GCC diagnostic pop
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
    if (index_hint != (-1)) {
        assert(known.buf[index_hint] == ptr);
        printf("Freeing %p\n", ptr);
        free(ptr);
        known.buf[index_hint] = 0;
    } else {
        for (int i = 0; i < known.index; i++) {
            if (known.buf[i] == ptr) {
                known.buf[i] = 0;
                return 0;
            }
        }

        assert(0 && "Trying to free a non-gc owned pointer");
    }
}

void gc_mark_plain(const void *obj, GHashTable *table) {
    g_hash_table_add(table, (gpointer) obj);
}

void gc_mark_ArrayType1(ArrayType *arr, void (*fnptr)(void *, GHashTable *), GHashTable *table) {
    if (arr != 0) {
        if (arr->arr != 0) {
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
    gc_mark_plain((void *) (*str), table);
}

void gc_find_unused(GHashTable *marked) {
    int deadcount = 0;
    for (int i = 0; i < known.index; i++) {
        gconstpointer to_check = (gconstpointer) known.buf[i];
        if (to_check == 0) {
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
        if (known.buf[i] != 0) {
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
    g_stack_frames[g_sf_index] = 0;
    g_sf_index--;
}

void *gc_peek_stack_frame(size_t index) {
    return g_stack_frames[index];
}

void gc_run() {
    GHashTable *table = g_hash_table_new(g_direct_hash, g_direct_equal);
    for (ssize_t i = g_sf_index - 1; i >= 0; i--) {
        void *cur_stack = gc_peek_stack_frame(i);
        int stack_id = *((int *) cur_stack);
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
    if ((arr != 0) && (arr->fnptr != 0)) {
        gc_mark_ArrayType1(&arr->arr, arr->fnptr, table);
    }
}

void gc_mark_Nested1(struct Nested1 *stc, GHashTable *table);

struct StackFrame_alloc_string_arr;

void gc_mark_StackFrame_alloc_string_arr(struct StackFrame_alloc_string_arr *stc, GHashTable *table);

struct StackFrame_alloc_nested;

void gc_mark_StackFrame_alloc_nested(struct StackFrame_alloc_nested *stc, GHashTable *table);

struct StackFrame_alloc_nested_array;

void gc_mark_StackFrame_alloc_nested_array(struct StackFrame_alloc_nested_array *stc, GHashTable *table);

struct StackFrame_moving_delete;

void gc_mark_StackFrame_moving_delete(struct StackFrame_moving_delete *stc, GHashTable *table);

struct StackFrame_array_push;

void gc_mark_StackFrame_array_push(struct StackFrame_array_push *stc, GHashTable *table);

struct StackFrame_alt_fun2;

void gc_mark_StackFrame_alt_fun2(struct StackFrame_alt_fun2 *stc, GHashTable *table);

struct StackFrame_alt_fun1;

void gc_mark_StackFrame_alt_fun1(struct StackFrame_alt_fun1 *stc, GHashTable *table);

struct StackFrame_main;

void gc_mark_StackFrame_main(struct StackFrame_main *stc, GHashTable *table);

void gc_mark_Nested1(struct Nested1 *stc, GHashTable *table) {
    if (stc != NULL) {
        gc_mark_ArrayType(&stc->arr, table);
        gc_mark_GCString(&stc->identifier, table);
    }
}

struct StackFrame_alloc_string_arr {
    int id;
    ArrayType arr;
};

void gc_mark_StackFrame_alloc_string_arr(struct StackFrame_alloc_string_arr *stc, GHashTable *table) {
    if (stc != NULL) {
        gc_mark_ArrayType(&stc->arr, table);
    }
}

struct StackFrame_alloc_nested {
    int id;
    struct Nested1 *ret;
};

void gc_mark_StackFrame_alloc_nested(struct StackFrame_alloc_nested *stc, GHashTable *table) {
    if (stc != NULL) {
        {
            gc_mark_Nested1(stc->ret, table);
            gc_mark_plain(stc->ret, table);
        }
    }
}

struct StackFrame_alloc_nested_array {
    int id;
    struct ArrOfNested arr;
};

void gc_mark_StackFrame_alloc_nested_array(struct StackFrame_alloc_nested_array *stc, GHashTable *table) {
    if (stc != NULL) {
        gc_mark_ArrOfNested(&stc->arr, table);
    }
}

struct StackFrame_moving_delete {
    int id;
};

void gc_mark_StackFrame_moving_delete(struct StackFrame_moving_delete *stc, GHashTable *table) {
    if (stc != NULL) {
    }
}

struct StackFrame_array_push {
    int id;
};

void gc_mark_StackFrame_array_push(struct StackFrame_array_push *stc, GHashTable *table) {
    if (stc != NULL) {
    }
}

struct StackFrame_alt_fun2 {
    int id;
    struct Nested1 *nested;
};

void gc_mark_StackFrame_alt_fun2(struct StackFrame_alt_fun2 *stc, GHashTable *table) {
    if (stc != NULL) {
        {
            gc_mark_Nested1(stc->nested, table);
            gc_mark_plain(stc->nested, table);
        }
    }
}

struct StackFrame_alt_fun1 {
    int id;
    ArrayType arr;
    struct Nested1 *nested;
};

void gc_mark_StackFrame_alt_fun1(struct StackFrame_alt_fun1 *stc, GHashTable *table) {
    if (stc != NULL) {
        gc_mark_ArrayType(&stc->arr, table);
        {
            gc_mark_Nested1(stc->nested, table);
            gc_mark_plain(stc->nested, table);
        }
    }
}

struct StackFrame_main {
    int id;
    ArrayType main_array;
    GCString third;
    GCString fourth;
    struct ArrOfNested nested;
    struct Nested1 *ptr;
};

void gc_mark_StackFrame_main(struct StackFrame_main *stc, GHashTable *table) {
    if (stc != NULL) {
        gc_mark_ArrayType(&stc->main_array, table);
        gc_mark_GCString(&stc->third, table);
        gc_mark_GCString(&stc->fourth, table);
        gc_mark_ArrOfNested(&stc->nested, table);
        {
            gc_mark_Nested1(stc->ptr, table);
            gc_mark_plain(stc->ptr, table);
        }
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
    struct StackFrame_alloc_string_arr stack_frame;
    memset(&stack_frame, 0, sizeof(stack_frame));
    stack_frame.id = 0;
    gc_push_stack_frame((void *) (&stack_frame));
    stack_frame.arr.len = length;
    stack_frame.arr.arr = gc_malloc(length * (sizeof(void *)));
    for (int i = 0; i < length; i++) {
        long rand_num = random();
        long strlen = floor(log10((double) rand_num)) + 1;
        stack_frame.arr.arr[i] = gc_string(strlen);
        snprintf(stack_frame.arr.arr[i], strlen, "%ld", rand_num);
    }

    gc_run();
    printf("%s\n", stack_frame.arr.arr[0]);
    {
        gc_pop_stack_frame();
        return stack_frame.arr;
    }
    gc_pop_stack_frame();
}

struct Nested1 *alloc_nested(int arr_len, const char *identifier) {
    struct StackFrame_alloc_nested stack_frame;
    memset(&stack_frame, 0, sizeof(stack_frame));
    stack_frame.id = 1;
    gc_push_stack_frame((void *) (&stack_frame));
    stack_frame.ret = gc_malloc(sizeof(struct Nested1));
    stack_frame.ret->arr = alloc_string_arr(arr_len);
    stack_frame.ret->identifier = gc_string1(identifier);
    gc_run();
    {
        gc_pop_stack_frame();
        return stack_frame.ret;
    }
    gc_pop_stack_frame();
}

struct ArrOfNested alloc_nested_array() {
    struct StackFrame_alloc_nested_array stack_frame;
    memset(&stack_frame, 0, sizeof(stack_frame));
    stack_frame.id = 2;
    gc_push_stack_frame((void *) (&stack_frame));
    stack_frame.arr.fnptr = gc_mark_Nested1;
    stack_frame.arr.arr.len = 100;
    stack_frame.arr.arr.arr = gc_malloc(stack_frame.arr.arr.len * (sizeof(void *)));
    for (int i = 0; i < stack_frame.arr.arr.len; i++) {
        stack_frame.arr.arr.arr[i] = alloc_nested(19, "hello world");
    }

    {
        gc_pop_stack_frame();
        return stack_frame.arr;
    }
    gc_pop_stack_frame();
}

void moving_delete(ArrayType *arr, size_t index) {
    struct StackFrame_moving_delete stack_frame;
    memset(&stack_frame, 0, sizeof(stack_frame));
    stack_frame.id = 3;
    gc_push_stack_frame((void *) (&stack_frame));
    assert(index < arr->len);
    for (size_t i = index; i < (arr->len - 1); i++) {
        arr->arr[i] = arr->arr[i + 1];
    }

    arr->len--;
    gc_pop_stack_frame();
}

void array_push(ArrayType *arr, void *elem) {
    struct StackFrame_array_push stack_frame;
    memset(&stack_frame, 0, sizeof(stack_frame));
    stack_frame.id = 4;
    gc_push_stack_frame((void *) (&stack_frame));
    arr->arr[arr->len++] = elem;
    gc_pop_stack_frame();
}

struct Nested1 *alt_fun2() {
    struct StackFrame_alt_fun2 stack_frame;
    memset(&stack_frame, 0, sizeof(stack_frame));
    stack_frame.id = 5;
    gc_push_stack_frame((void *) (&stack_frame));
    stack_frame.nested = alloc_nested(10, "hello");
    {
        gc_pop_stack_frame();
        return stack_frame.nested;
    }
    gc_pop_stack_frame();
}

ArrayType alt_fun1() {
    struct StackFrame_alt_fun1 stack_frame;
    memset(&stack_frame, 0, sizeof(stack_frame));
    stack_frame.id = 6;
    gc_push_stack_frame((void *) (&stack_frame));
    stack_frame.arr = alloc_string_arr(100);
    stack_frame.nested = alt_fun2();
    while (stack_frame.arr.len > 9) {
        for (int i = 0; i < stack_frame.arr.len; i += 10) {
            moving_delete(&stack_frame.arr, i);
        }

        printf("Running gc\n");
        gc_run();
    }

    gc_run();
    gc_run();
    {
        gc_pop_stack_frame();
        return stack_frame.arr;
    }
    gc_pop_stack_frame();
}

int main() {
    struct StackFrame_main stack_frame;
    memset(&stack_frame, 0, sizeof(stack_frame));
    stack_frame.id = 7;
    gc_push_stack_frame((void *) (&stack_frame));
    stack_frame.main_array = alt_fun1();
    stack_frame.third = stack_frame.main_array.arr[3];
    stack_frame.fourth = stack_frame.main_array.arr[4];
    stack_frame.nested = alloc_nested_array();
    srand(time(0));
    printf("third val: %s\n", stack_frame.third);
    for (int i = 0; i < stack_frame.main_array.len; i++) {
        printf("Got %s\n", (char *) stack_frame.main_array.arr[i]);
    }

    gc_run();
    stack_frame.main_array.arr = 0;
    stack_frame.main_array.len = 0;
    stack_frame.fourth = 0;
    gc_run();
    gc_run();
    stack_frame.ptr = stack_frame.nested.arr.arr[0];
    printf("Nested %s\n", (char *) stack_frame.ptr->identifier);
    printf("third val: %s\n", stack_frame.third);
    gc_pop_stack_frame();

    gc_run();
}

struct StackMap STACK_MAP[] = {{0, gc_mark_StackFrame_alloc_string_arr},
                               {1, gc_mark_StackFrame_alloc_nested},
                               {2, gc_mark_StackFrame_alloc_nested_array},
                               {3, gc_mark_StackFrame_moving_delete},
                               {4, gc_mark_StackFrame_array_push},
                               {5, gc_mark_StackFrame_alt_fun2},
                               {6, gc_mark_StackFrame_alt_fun1},
                               {7, gc_mark_StackFrame_main}};
