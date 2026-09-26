/**
 * @file    task_net.h
 * @brief   网络任务 —— 单任务持有 W5500 socket 层
 */

#ifndef __TASK_NET_H__
#define __TASK_NET_H__

#ifdef __cplusplus
extern "C" {
#endif

void vNetTask(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif /* __TASK_NET_H__ */
