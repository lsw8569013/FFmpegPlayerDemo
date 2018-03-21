#include "queue.h"

#include <android/log.h>
#include <pthread.h>

#define LOGE(FORMAT, ...) __android_log_print(ANDROID_LOG_ERROR,"ffmpeg",FORMAT,##__VA_ARGS__);

struct _Queue {
    //队列长度
    int size;
    //存放size个的AVPacket **packets;
    void **tab;
    //压入栈元素的下一个元素位置
    int next_to_write;
    //弹出栈元素的下一个元素位置
    int next_to_read;
};

/**
 * 初始化队列
 */
Queue *queue_init(int size, queue_fill_func fill_func){
    Queue* queue = (Queue*)malloc(sizeof(Queue));
    queue->size = size;
    queue->next_to_write = 0;
    queue->next_to_read = 0;
    //数组开辟空间
    queue->tab = (void **) malloc(sizeof(*queue->tab) * size);
    int i;
    for(i=0; i<size; i++){
        queue->tab[i] = fill_func();
    }
    return queue;
}

/**
 * 销毁队列
 */
void queue_free(Queue *queue, queue_free_func free_func){
    int i;
    for(i=0; i<queue->size; i++){
        //销毁队列的元素，通过使用回调函数
        free_func((void*)queue->tab[i]);
    }
    free(queue->tab);
    free(queue);
}

/**
 * 获取下一个索引位置
 */
int queue_get_next(Queue *queue, int current){
    return (current + 1) % queue->size;
}

/**
 * 队列压人元素（生产）
 */
void* queue_push(Queue *queue,pthread_mutex_t *mutex, pthread_cond_t *cond){
    int current = queue->next_to_write;
    int next_to_write;
    for(;;){
        //下一个要读的位置等于下一个要写的，等我写完，在读
        //不等于，就继续
        next_to_write = queue_get_next(queue,current);
        if(next_to_write != queue->next_to_read){
            break;
        }
        //阻塞
        pthread_cond_wait(cond,mutex);
    }

    queue->next_to_write = next_to_write;
//    LOGE("queue_push queue:%#x, %d",queue,current);
    //通知
    pthread_cond_broadcast(cond);

    return queue->tab[current];
}

/**
 * 弹出元素（消费）
 */
void* queue_pop(Queue *queue,pthread_mutex_t *mutex, pthread_cond_t *cond){
    int current = queue->next_to_read;
    for(;;){
        if(queue->next_to_read != queue->next_to_write){
            break;
        }
        pthread_cond_wait(cond,mutex);
    }

    queue->next_to_read = queue_get_next(queue,current);
//    LOGE("queue_pop queue:%#x, %d",queue,current);

    pthread_cond_broadcast(cond);
    return queue->tab[current];
}