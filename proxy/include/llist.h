/**
 * @file llist.h
 * @brief Generic macro-based doubly linked list generator.
 *
 * Copied and adapted from the collector Git history.  The list provides
 * O(1) tail insertion and O(1) head removal, making it suitable as a FIFO.
 */

#ifndef __LLIST_H__
#define __LLIST_H__

#include "common.h"

#define llistnode_data(X)   ((X)->data)
#define llistnode_next(X)   ((X) ? (X)->next : NULL)
#define llistnode_prev(X)   ((X) ? (X)->prev : NULL)
#define llist_head(X)       ((X) ? (X)->head : NULL)
#define llist_tail(X)       ((X) ? (X)->tail : NULL)
#define llist_count(X)      ((X) ? (X)->count : 0)

#define for_all_list_elems(list, node) \
    for ((node) = llist_head(list); (node); (node) = llistnode_next(node))

#define DECLARE_LLIST_STRUCTURES(name, dataType)                                    \
    typedef struct name ## _llistnode_s                                             \
    {                                                                               \
        struct name ## _llistnode_s *prev;                                          \
        struct name ## _llistnode_s *next;                                          \
        dataType data;                                                              \
    } name ## _llistnode_t;                                                         \
                                                                                    \
    typedef struct name ## _llist_s                                                 \
    {                                                                               \
        name ## _llistnode_t *head;                                                 \
        name ## _llistnode_t *tail;                                                 \
        void (*free_data)(dataType);                                                \
        dataType (*copy_data)(dataType);                                            \
        uint32_t count;                                                             \
    } name ## _llist_t;



#define DECLARE_LLIST_PROTOTYPES(name, dataType)                                    \
    void name ## _llist_free(name ## _llist_t *list);                              \
    name ## _llist_t *name ## _llist_new(void (*free_data)(dataType),              \
                                           dataType (*copy_data)(dataType));         \
    name ## _llistnode_t *name ## _llistnode_new(                                  \
        name ## _llist_t *list, dataType data, bool copy_data);                    \
    name ## _llistnode_t *name ## _llist_add_tail(                                 \
        name ## _llist_t *list, dataType data, bool copy_data);                    \
    dataType name ## _llist_pop_head(name ## _llist_t *list);                      \
    void name ## _llist_delete_elem(name ## _llist_t *list,                        \
                                     name ## _llistnode_t *node);



#define DECLARE_LLIST_CORE_FUNC(name, dataType)                                     \
    void name ## _llist_free(name ## _llist_t *list)                               \
    {                                                                               \
        if (!list)                                                                  \
            return;                                                                 \
        name ## _llistnode_t *node = list->head;                                    \
        while (node) {                                                              \
            name ## _llistnode_t *next = node->next;                               \
            if (list->free_data)                                                    \
                list->free_data(node->data);                                        \
            free(node);                                                             \
            node = next;                                                            \
        }                                                                           \
        free(list);                                                                 \
    }                                                                               \
                                                                                    \
    name ## _llist_t *name ## _llist_new(void (*free_data)(dataType),              \
                                           dataType (*copy_data)(dataType))          \
    {                                                                               \
        name ## _llist_t *list = calloc(1, sizeof(*list));                          \
        if (!list)                                                                  \
            return NULL;                                                            \
        list->free_data = free_data;                                                \
        list->copy_data = copy_data;                                                \
        return list;                                                                \
    }                                                                               \
                                                                                    \
    name ## _llistnode_t *name ## _llistnode_new(                                  \
        name ## _llist_t *list, dataType data, bool copy_data)                     \
    {                                                                               \
        if (!list)                                                                  \
            return NULL;                                                            \
        name ## _llistnode_t *node = calloc(1, sizeof(*node));                     \
        if (!node)                                                                  \
            return NULL;                                                            \
        node->data = copy_data && list->copy_data ? list->copy_data(data) : data;   \
        return node;                                                                \
    }                                                                               \
                                                                                    \
    name ## _llistnode_t *name ## _llist_add_tail(                                 \
        name ## _llist_t *list, dataType data, bool copy_data)                     \
    {                                                                               \
        name ## _llistnode_t *node =                                                \
            name ## _llistnode_new(list, data, copy_data);                         \
        if (!node)                                                                  \
            return NULL;                                                            \
        node->prev = list->tail;                                                    \
        if (list->tail)                                                             \
            list->tail->next = node;                                                \
        else                                                                        \
            list->head = node;                                                      \
        list->tail = node;                                                          \
        list->count++;                                                              \
        return node;                                                                \
    }                                                                               \
                                                                                    \
    dataType name ## _llist_pop_head(name ## _llist_t *list)                       \
    {                                                                               \
        dataType data = (dataType){0};                                              \
        if (!list || !list->head)                                                   \
            return data;                                                            \
        name ## _llistnode_t *node = list->head;                                    \
        data = node->data;                                                          \
        list->head = node->next;                                                    \
        if (list->head)                                                             \
            list->head->prev = NULL;                                                \
        else                                                                        \
            list->tail = NULL;                                                      \
        list->count--;                                                              \
        free(node);                                                                 \
        return data;                                                                \
    }                                                                               \
                                                                                    \
    void name ## _llist_delete_elem(name ## _llist_t *list,                        \
                                     name ## _llistnode_t *node)                    \
    {                                                                               \
        if (!list || !node)                                                         \
            return;                                                                 \
        if (node->prev)                                                             \
            node->prev->next = node->next;                                          \
        else                                                                        \
            list->head = node->next;                                                \
        if (node->next)                                                             \
            node->next->prev = node->prev;                                          \
        else                                                                        \
            list->tail = node->prev;                                                \
        if (list->free_data)                                                        \
            list->free_data(node->data);                                            \
        list->count--;                                                              \
        free(node);                                                                 \
    }



#endif
