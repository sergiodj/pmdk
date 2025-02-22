/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2021, Intel Corporation */

/*
 * future.h - public definitions for the future type, its associated state and
 * related functionality.
 *
 * A future is an abstract type representing a task, or a collection of tasks,
 * that can be executed incrementally by polling until the operation
 * is complete. Futures are typically meant to be implemented by library
 * developers and then used by applications to concurrently run multiple,
 * possibly unrelated, tasks.
 *
 * A future contains the following context:
 * - current state of execution for the future
 * - a function pointer for the task
 * - structure for data which is the required state needed to perform the task
 * - structure for output to store the result of the task
 * - the size of the data and output structures (both can be 0)
 *
 * A future definition must begin an instance of the `struct future` type, which
 * contains all common metadata for all futures, followed by the structures for
 * data and output. The library provides convenience macros to simplify
 * the definition of user-defined future types.
 *
 * Applications must call the `future_poll` method to make progress on the task
 * associated with the future. This function will perform
 * an implementation-defined operation towards completing the task and return
 * the future's current state. Futures are generally safe to poll until they
 * are complete. Unless the documentation for a specific future implementation
 * indicates otherwise, futures can be moved in memory and don't always have
 * to be polled by the same thread.
 *
 * TODO: Any future with an inline value that is updated externally,
 * like in DSA or io_uring, is not safe to move in memory.
 *
 * Optionally, future implementations can accept wakers for use in polling.
 * A future can use a waker to signal the caller that some progress can be made
 * and the future should be polled again. This is useful to avoid busy polling
 * when the future is waiting for some asynchronous operation to finish
 * or for some resource to become available.
 *
 * A waker is a tuple composed of a function pointer and a data context pointer.
 * If a waker is supplied and consumed by a future, it will call the provided
 * function with its data pointer. The caller needs to make sure that the waker
 * is safe to call until the future is complete or until it supplies a different
 * waker to the poll method.
 * The waker implementation needs to be thread-safe.
 *
 * TODO: wakers are not optional right now. And are kind of limited. Futures
 * should have a way of indicating to the caller that it was consumed.
 * Maybe a pointer to a tagged union?
 *
 * To facilitate the composition of futures, the library provides a generic
 * "chain" future implementation that applications can use to compose
 * two or more futures into a chain on sequentially executed tasks. The futures
 * that make up a chain are executed in the order in which they are stored
 * in memory. To share state between the futures, each chain entry contains
 * a `map` function to map the state of the just-completed future onto the next
 * future state. Applications can use the map function of the last future in
 * a chain to map its output to the output of the entire chain.
 */

#ifndef FUTURE_H
#define FUTURE_H 1

#include <unistd.h>
#include <stdint.h>

enum future_state {
	FUTURE_STATE_IDLE,
	FUTURE_STATE_COMPLETE,
	FUTURE_STATE_RUNNING,
};

struct future_context {
	enum future_state state;
	size_t data_size;
	size_t output_size;
};

typedef void (*future_waker_wake_fn)(void *data);

struct future_waker {
	void *data;
	future_waker_wake_fn wake;
};

void *future_context_get_data(struct future_context *context);

void *future_context_get_output(struct future_context *context);

size_t future_context_get_size(struct future_context *context);

#define FUTURE_WAKER_WAKE(_wakerp)\
((_wakerp)->wake((_wakerp)->data))

typedef enum future_state (*future_task_fn)(struct future_context *context,
			struct future_waker waker);

struct future {
	future_task_fn task;
	struct future_context context;
};

#define FUTURE(_name, _data_type, _output_type)\
	struct _name {\
		struct future base;\
		_data_type data;\
		_output_type output;\
	}

#define FUTURE_INIT(_futurep, _taskfn)\
do {\
	(_futurep)->base.task = (_taskfn);\
	(_futurep)->base.context.state = (FUTURE_STATE_IDLE);\
	(_futurep)->base.context.data_size = sizeof((_futurep)->data);\
	(_futurep)->base.context.output_size =\
		sizeof((_futurep)->output);\
} while (0)

#define FUTURE_AS_RUNNABLE(futurep) (&(futurep)->base)
#define FUTURE_OUTPUT(futurep) (&(futurep)->output)

typedef void (*future_map_fn)(struct future_context *lhs,
			struct future_context *rhs, void *arg);

struct future_chain_entry {
	future_map_fn map;
	void *arg;
	struct future future;
};

#define FUTURE_CHAIN_ENTRY(_future_type, _name)\
struct {\
	future_map_fn map;\
	void *arg;\
	_future_type fut;\
} _name;

#define FUTURE_CHAIN_ENTRY_INIT(_entry, _fut, _map, _map_arg)\
do {\
	(_entry)->fut = (_fut);\
	(_entry)->map = _map;\
	(_entry)->arg = _map_arg;\
} while (0)

struct future_waker future_noop_waker(void);

enum future_state future_poll(struct future *fut, struct future_waker waker);

#define FUTURE_BUSY_POLL(_futurep)\
while (future_poll(FUTURE_AS_RUNNABLE((_futurep)), future_noop_waker()) !=\
	FUTURE_STATE_COMPLETE) {}

enum future_state async_chain_impl(struct future_context *ctx,
	struct future_waker waker);

#define FUTURE_CHAIN_INIT(_futurep)\
FUTURE_INIT((_futurep), async_chain_impl)

#endif
