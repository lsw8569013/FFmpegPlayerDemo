#include <malloc.h>
#include <android/log.h>
#include <pthread.h>

typedef struct _Queue Queue;
//分配队列元素内存的函数
typedef void *(*queue_fill_func)();
//释放队列中元素所占用的内存
typedef void *(*queue_free_func)(void *elem);

/**
 * 初始化队列
 */
Queue *queue_init(int size, queue_fill_func fill_func);

/**
 * 销毁队列
 */
void queue_free(Queue *queue, queue_free_func free_func);

/**
 * 获取下一个索引位置
 */
int queue_get_next(Queue *queue, int current);

/**
 * 队列压人元素
 */
void* queue_push(Queue *queue,pthread_mutex_t *mutex, pthread_cond_t *cond);

/**
 * 弹出元素
 */
void* queue_pop(Queue *queue,pthread_mutex_t *mutex, pthread_cond_t *cond);
