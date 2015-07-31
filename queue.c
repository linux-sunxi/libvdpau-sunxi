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

#include "queue.h"

/* initialize queue
 *
 */
QUEUE *q_queue_init(void)
{
	QUEUE *queue = (QUEUE *) calloc(1, sizeof(QUEUE));
	queue->head = queue->tail = NULL;
	queue->length = 0;
	pthread_mutex_init(&queue->mutex, NULL);
	return queue;
}

/* allocate node data
 *
 */
NODE *allocate_node(void *data)
{
	NODE *node = (NODE *)calloc(1, sizeof(NODE));
	node->next = node->prev = NULL;
	node->data = data;
	return (node);
}

/* push node to the tail position
 *
 */
qStatus q_push_tail(QUEUE *queue, void *data)
{
	if (!queue || !data)
		return Q_ERROR;

	NODE *node = allocate_node(data);

	if (!node)
		return Q_ERROR;

	q_lock(queue);

	if (queue->head == NULL)
		queue->head = node;
	else
		queue->tail->next = node;

	node->prev = queue->tail;
	queue->tail = node;
	queue->length++;

	q_unlock(queue);

	return Q_SUCCESS;
}

/* drop node at head position
 *
 */
qStatus q_pop_head(QUEUE *queue, void **data)
{
	if (!queue)
		return Q_ERROR;

	q_lock(queue);

	if (!queue->head)
	{
		q_unlock(queue);
		return Q_ERROR;
	}

	*data = queue->head->data;

	NODE *tmp = queue->head;
	queue->head = queue->head->next;

	if (queue->head == NULL)
		queue->tail = NULL;
	else
		queue->head->prev = NULL;

	queue->length--;
	q_node_free(tmp, 0);

	q_unlock(queue);
	return Q_SUCCESS;
}

/* check, if queue is empty
 *
 */
qStatus q_isEmpty(QUEUE *queue)
{
	if (queue->length == 0)
		return Q_EMPTY;

	return Q_SUCCESS;
}

/* return the length of the queue
 *
 */
int q_length(QUEUE *queue)
{
	return queue->length;
}

/* free queue and all nodes
 *
 */
qStatus q_queue_free(QUEUE *queue)
{
	if (!queue)
		return Q_ERROR;

	q_lock(queue);

	NODE *tmp = queue->head;
	while(tmp)
	{
		NODE *next = tmp->next;
		q_node_free(tmp, 1);
		tmp = next;
	}
	q_unlock(queue);
	pthread_mutex_destroy(&queue->mutex);
	free(queue);

	return Q_SUCCESS;
}

/* free node and allocated data
 *
 */
qStatus q_node_free(NODE *node, int data_free)
{
	if (!node)
		return Q_ERROR;

	if (data_free && node->data)
		free(node->data);

	free(node);

	return Q_SUCCESS;
}

/* lock the queue
 *
 */
qStatus q_lock(QUEUE *queue)
{
	if(pthread_mutex_lock(&queue->mutex))
		return Q_LOCKERROR;

	return  Q_SUCCESS;
}

/* unlock the queue
 *
 */
qStatus q_unlock(QUEUE *queue)
{
	if(pthread_mutex_unlock(&queue->mutex))
		return Q_LOCKERROR;

	return  Q_SUCCESS;
}
