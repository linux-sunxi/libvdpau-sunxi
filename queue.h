/*
 * Copyright (c) 2015 Andreas Baierl <ichgeh@imkreisrum.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef __QUEUE_H__
#define __QUEUE_H__

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum
{
	Q_SUCCESS,
	Q_EMPTY_NODE,
	Q_EMPTY,
	Q_LOCKERROR,
	Q_ERROR
} qStatus;

typedef struct Node
{
	struct Node *prev;
	struct Node *next;
	void *data;
} NODE;

typedef struct Queue
{
	NODE *head;
	NODE *tail;
	int length;
	pthread_mutex_t mutex;
} QUEUE;

QUEUE *q_queue_init(void);
NODE *allocate_node(void *data);

qStatus q_push_tail(QUEUE *queue, void *data);
qStatus q_pop_head(QUEUE *queue, void **data);

qStatus q_isEmpty(QUEUE *queue);
int q_length(QUEUE *queue);

qStatus q_queue_free(QUEUE *queue);
qStatus q_node_free(NODE *node, int data_free);
qStatus q_lock(QUEUE *queue);
qStatus q_unlock(QUEUE *queue);

#endif
