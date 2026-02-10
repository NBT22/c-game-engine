//
// Created by droc101 on 4/21/2024.
//

#include <assert.h>
#include <engine/helpers/Realloc.h>
#include <engine/structs/List.h>
#include <engine/subsystem/Error.h>
#include <engine/subsystem/Logging.h>
#include <limits.h>
#include <SDL_error.h>
#include <SDL_mutex.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void _ListInit(List *list, const enum _ListType listType)
{
	assert(list);
	assert(listType == LIST_POINTER || listType == LIST_UINT64 || listType == LIST_UINT32 || listType == LIST_INT32);

	list->length = 0;
	list->type = listType;
	list->pointerData = NULL;
}

void _LockingListInit(LockingList *list, const enum _ListType listType)
{
	assert(list);

	_ListInit((List *)list, listType);
	list->mutex = SDL_CreateMutex();
}

void _SortedListInit(SortedList *list,
					 const enum _ListType listType,
					 int (*const CompareFunction)(const void *, const void *))
{
	assert(list);

	_ListInit((List *)list, listType);
	list->CompareFunction = CompareFunction;
}


static inline void ListCopyHelper(const List *oldList, List *newList)
{
	size_t listSize = 0;
	switch (oldList->type)
	{
		case LIST_POINTER:
		case LIST_UINT64:
			listSize = oldList->length * 8;
			break;
		case LIST_UINT32:
		case LIST_INT32:
			listSize = oldList->length * 4;
			break;
	}
	assert(!newList->pointerData);
	newList->pointerData = malloc(listSize);
	CheckAlloc(newList->pointerData);
	memcpy(newList->pointerData, oldList->pointerData, listSize);
	newList->length = oldList->length;
}

void _ListCopy(const List *restrict oldList, List *restrict newList)
{
	assert(oldList);
	assert(newList);

	_ListFree(newList);
	_ListInit(newList, oldList->type);
	ListCopyHelper(oldList, newList);
}

void _LockingListCopy(const LockingList *restrict oldList, LockingList *restrict newList)
{
	assert(oldList);
	assert(newList);

	ListLock(*oldList);
	_LockingListFree(newList);
	_LockingListInit(newList, oldList->type);
	ListCopyHelper((const List *)oldList, (List *)newList);
	ListUnlock(*oldList);
}


void _ListAdd(List *list, void *data)
{
	assert(list);

	switch (list->type)
	{
		case LIST_POINTER:
			list->pointerData = GameReallocArray(list->pointerData, list->length + 1, sizeof(void *));
			CheckAlloc(list->pointerData);
			list->pointerData[list->length] = data;
			break;
		case LIST_UINT64:
			list->uint64Data = GameReallocArray(list->uint64Data, list->length + 1, sizeof(uint64_t));
			CheckAlloc(list->uint64Data);
			list->uint64Data[list->length] = (uint64_t)(uintptr_t)data;
			break;
		case LIST_UINT32:
			list->uint32Data = GameReallocArray(list->uint32Data, list->length + 1, sizeof(uint32_t));
			CheckAlloc(list->uint32Data);
			list->uint32Data[list->length] = (uint32_t)(uintptr_t)data;
			break;
		case LIST_INT32:
			list->int32Data = GameReallocArray(list->int32Data, list->length + 1, sizeof(int32_t));
			CheckAlloc(list->int32Data);
			list->int32Data[list->length] = (int32_t)(uintptr_t)data;
			break;
	}
	list->length++;
}

void _LockingListAdd(LockingList *list, void *data)
{
	assert(list);

	ListLock(*list);
	_ListAdd((List *)list, data);
	ListUnlock(*list);
}

void _SortedListAdd(SortedList *list, void *data)
{
	// TODO: Fast insert logic
	assert(list);

	_ListAdd((List *)list, data);

	switch (list->type)
	{
		case LIST_POINTER:
		case LIST_UINT64:
			qsort(list->pointerData, list->length, sizeof(uint64_t), list->CompareFunction);
			break;
		case LIST_UINT32:
		case LIST_INT32:
			qsort(list->pointerData, list->length, sizeof(uint32_t), list->CompareFunction);
			break;
	}
}


void _ListSet(const List *list, const size_t index, void *data)
{
	assert(list);
	assert(index <= list->length);

	switch (list->type)
	{
		case LIST_POINTER:
			list->pointerData[index] = data;
			break;
		case LIST_UINT64:
			list->uint64Data[index] = (uint64_t)(uintptr_t)data;
			break;
		case LIST_UINT32:
			list->uint32Data[index] = (uint32_t)(uintptr_t)data;
			break;
		case LIST_INT32:
			list->int32Data[index] = (int32_t)(uintptr_t)data;
			break;
	}
}

void _LockingListSet(const LockingList *list, const size_t index, void *data)
{
	assert(list);
	assert(index <= list->length);

	ListLock(*list);
	_ListSet((const List *)list, index, data);
	ListUnlock(*list);
}


void ListRemoveAtHelper(List *list, const size_t index)
{
	switch (list->type)
	{
		case LIST_POINTER:
			memmove(&list->pointerData[index], &list->pointerData[index + 1], sizeof(void *) * (list->length - index));
			list->pointerData = GameReallocArray(list->pointerData, list->length, sizeof(void *));
			CheckAlloc(list->pointerData);
			break;
		case LIST_UINT64:
			memmove(&list->uint64Data[index], &list->uint64Data[index + 1], sizeof(uint64_t) * (list->length - index));
			list->uint64Data = GameReallocArray(list->uint64Data, list->length, sizeof(uint64_t));
			CheckAlloc(list->uint64Data);
			break;
		case LIST_UINT32:
			memmove(&list->uint32Data[index], &list->uint32Data[index + 1], sizeof(uint32_t) * (list->length - index));
			list->uint32Data = GameReallocArray(list->uint32Data, list->length, sizeof(uint32_t));
			CheckAlloc(list->uint32Data);
			break;
		case LIST_INT32:
			memmove(&list->int32Data[index], &list->int32Data[index + 1], sizeof(int32_t) * (list->length - index));
			list->int32Data = GameReallocArray(list->int32Data, list->length, sizeof(int32_t));
			CheckAlloc(list->int32Data);
			break;
	}
}

void _ListRemoveAt(List *list, const size_t index)
{
	assert(list);
	assert(index <= list->length);
	assert(list->length);

	list->length--;
	if (list->length == 0)
	{
		free(list->pointerData);
		list->pointerData = NULL;
		return;
	}
	ListRemoveAtHelper(list, index);
}

void _LockingListRemoveAt(LockingList *list, const size_t index)
{
	assert(list);
	assert(index <= list->length);
	assert(list->length);

	ListLock(*list);
	list->length--;
	if (list->length == 0)
	{
		free(list->pointerData);
		list->pointerData = NULL;

		ListUnlock(*list);
		return;
	}
	ListRemoveAtHelper((List *)list, index);
	ListUnlock(*list);
}


void _ListInsertAfter(List *list, size_t index, void *data)
{
	assert(list);

	if (list->length == 0 || index == SIZE_MAX)
	{
		_ListAdd(list, data);
		return;
	}

	assert(index <= list->length);

	index++;
	list->length++;
	switch (list->type)
	{
		case LIST_POINTER:
			list->pointerData = GameReallocArray(list->pointerData, list->length, sizeof(void *));
			CheckAlloc(list->pointerData);
			memmove(&list->pointerData[index + 1],
					&list->pointerData[index],
					sizeof(void *) * (list->length - index - 1));
			list->pointerData[index] = data;
			break;
		case LIST_UINT64:
			list->uint64Data = GameReallocArray(list->uint64Data, list->length, sizeof(uint64_t));
			CheckAlloc(list->uint64Data);
			memmove(&list->uint64Data[index + 1],
					&list->uint64Data[index],
					sizeof(uint64_t) * (list->length - index - 1));
			list->uint64Data[index] = (uint64_t)(uintptr_t)data;
			break;
		case LIST_UINT32:
			list->uint32Data = GameReallocArray(list->uint32Data, list->length, sizeof(uint32_t));
			CheckAlloc(list->uint32Data);
			memmove(&list->uint32Data[index + 1],
					&list->uint32Data[index],
					sizeof(uint32_t) * (list->length - index - 1));
			list->uint32Data[index] = (uint32_t)(uintptr_t)data;
			break;
		case LIST_INT32:
			list->int32Data = GameReallocArray(list->int32Data, list->length, sizeof(int32_t));
			CheckAlloc(list->int32Data);
			memmove(&list->int32Data[index + 1], &list->int32Data[index], sizeof(int32_t) * (list->length - index - 1));
			list->int32Data[index] = (int32_t)(uintptr_t)data;
			break;
	}
}

void _LockingListInsertAfter(LockingList *list, const size_t index, void *data)
{
	assert(list);

	ListLock(*list);
	_ListInsertAfter((List *)list, index, data);
	ListUnlock(*list);
}


size_t _ListFind(const List *list, const void *data)
{
	if (!list->length)
	{
		return -1;
	}

	for (size_t i = 0; i < list->length; i++)
	{
		switch (list->type)
		{
			case LIST_POINTER:
				if (list->pointerData[i] == data)
				{
					return i;
				}
				break;
			case LIST_UINT64:
				if (list->uint64Data[i] == (uint64_t)(uintptr_t)data)
				{
					return i;
				}
				break;
			case LIST_UINT32:
				if (list->uint32Data[i] == (uint32_t)(uintptr_t)data)
				{
					return i;
				}
				break;
			case LIST_INT32:
				if (list->int32Data[i] == (int32_t)(uintptr_t)data)
				{
					return i;
				}
				break;
		}
	}
	return -1;
}

size_t _LockingListFind(LockingList *list, const void *data)
{
	if (!list->length)
	{
		return -1;
	}

	ListLock(*list);
	for (size_t i = 0; i < list->length; i++)
	{
		switch (list->type)
		{
			case LIST_POINTER:
				if (list->pointerData[i] == data)
				{
					ListUnlock(*list);
					return i;
				}
				break;
			case LIST_UINT64:
				if (list->uint64Data[i] == (uint64_t)(uintptr_t)data)
				{
					ListUnlock(*list);
					return i;
				}
				break;
			case LIST_UINT32:
				if (list->uint32Data[i] == (uint32_t)(uintptr_t)data)
				{
					ListUnlock(*list);
					return i;
				}
				break;
			case LIST_INT32:
				if (list->int32Data[i] == (int32_t)(uintptr_t)data)
				{
					ListUnlock(*list);
					return i;
				}
				break;
		}
	}
	ListUnlock(*list);
	return -1;
}

size_t _SortedListFind(const SortedList *list, const void *data)
{
	assert(list);

	const void *foundElement = NULL;
	switch (list->type)
	{
		case LIST_POINTER:
		case LIST_UINT64:
			foundElement = bsearch(data, list->pointerData, list->length, sizeof(uint64_t), list->CompareFunction);
			break;
		case LIST_UINT32:
		case LIST_INT32:
			foundElement = bsearch(data, list->pointerData, list->length, sizeof(uint32_t), list->CompareFunction);
			break;
	}
	if (foundElement == NULL)
	{
		return -1;
	}
	return (size_t)((uintptr_t)list->pointerData - (uintptr_t)foundElement);
}


void _ListLock(const LockingList *list)
{
	assert(list);
	// TODO: Explodes on arm64 when the quit button is pressed
	if (SDL_LockMutex(list->mutex) < 0)
	{
		LogError("Failed to lock list mutex with error: %s\n", SDL_GetError());
		Error("Failed to lock list mutex!");
	}
}


void _ListUnlock(const LockingList *list)
{
	assert(list);
	if (SDL_UnlockMutex(list->mutex) < 0)
	{
		LogError("Failed to unlock list mutex with error: %s\n", SDL_GetError());
		Error("Failed to unlock list mutex!");
	}
}


void _ListClear(List *list)
{
	assert(list);

	list->length = 0;
	free(list->pointerData);
	list->pointerData = NULL;
}

void _LockingListClear(LockingList *list)
{
	assert(list);

	ListLock(*list);
	_ListClear((List *)list);
	ListUnlock(*list);
}


void _ListZero(const List *list)
{
	assert(list);

	switch (list->type)
	{
		case LIST_POINTER:
		case LIST_UINT64:
			memset(list->pointerData, 0, list->length * 8);
			break;
		case LIST_UINT32:
		case LIST_INT32:
			memset(list->pointerData, 0, list->length * 4);
			break;
	}
}

void _LockingListZero(const LockingList *list)
{
	assert(list);

	ListLock(*list);
	_ListZero((List *)list);
	ListUnlock(*list);
}


void _ListFree(List *list)
{
	assert(list);

	free(list->pointerData);
	list->pointerData = NULL;
	list->length = 0;
}

void _LockingListFree(LockingList *list)
{
	assert(list);

	ListLock(*list);
	_ListFree((List *)list);
	ListUnlock(*list);
	SDL_DestroyMutex(list->mutex);
	list->mutex = NULL;
}


void _ListFreeOnlyContents(const List *list)
{
	assert(list);
	assert(list->type == LIST_POINTER);

	if (list->length)
	{
		for (size_t i = 0; i < list->length; i++)
		{
			free(list->pointerData[i]);
		}
	}
}

void _LockingListFreeOnlyContents(const LockingList *list)
{
	assert(list);
	assert(list->type == LIST_POINTER);

	ListLock(*list);
	if (list->length)
	{
		for (size_t i = 0; i < list->length; i++)
		{
			free(list->pointerData[i]);
		}
	}
	ListUnlock(*list);
}


void _ListAndContentsFree(List *list)
{
	assert(list);

	_ListFreeOnlyContents(list);
	_ListFree(list);
}

void _LockingListAndContentsFree(LockingList *list)
{
	assert(list);

	_LockingListFreeOnlyContents(list);
	_LockingListFree(list);
}
