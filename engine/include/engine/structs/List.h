//
// Created by droc101 on 4/21/2024.
//

#ifndef GAME_LIST_H
#define GAME_LIST_H

#include <assert.h>
#include <SDL3/SDL_mutex.h>
#include <stddef.h>
#include <stdint.h>

typedef struct List List;
typedef struct LockingList LockingList;
typedef struct SortedList SortedList;

struct List
{
	/// The data that the list is storing
	void *data;
	/// The number of slots that are actually in use
	size_t length;
	/// The stride of the data in the list
	size_t stride;
};

struct LockingList
{
	struct List;
	/// The mutex used to ensure synchronization across threads
	SDL_Mutex *mutex;
};

struct SortedList
{
	struct List;
	/// The function for comparing of values
	int (*CompareFunction)(const void *, const void *);
};

#define List()


// NOLINTBEGIN(*-reserved-identifier)
void _ListInit(List *list, size_t stride);
void _LockingListInit(LockingList *list, size_t stride);
void _SortedListInit(SortedList *list, size_t stride, int (*CompareFunction)(const void *, const void *));

void _ListCopy(const List *restrict oldList, List *restrict newList);
void _LockingListCopy(const LockingList *restrict oldList, LockingList *restrict newList);

void _ListAdd(List *list, const void *data);
void _LockingListAdd(LockingList *list, const void *data);
void _SortedListAdd(SortedList *list, const void *data);

void _ListSet(const List *list, size_t index, const void *data);
void _LockingListSet(const LockingList *list, size_t index, const void *data);

void _ListRemoveAt(List *list, size_t index);
void _LockingListRemoveAt(LockingList *list, size_t index);

void _ListInsertAfter(List *list, size_t index, const void *data);
void _LockingListInsertAfter(LockingList *list, size_t index, const void *data);

size_t _ListFind(const List *list, const void *data);
size_t _LockingListFind(LockingList *list, const void *data);
size_t _SortedListFind(const SortedList *list, const void *data);

void _ListLock(const LockingList *list);

void _ListUnlock(const LockingList *list);

void _ListClear(List *list);
void _LockingListClear(LockingList *list);

void _ListZero(const List *list);
void _LockingListZero(const LockingList *list);

void _ListFree(List *list);
void _LockingListFree(LockingList *list);

void _ListFreeOnlyContents(const List *list);
void _LockingListFreeOnlyContents(const LockingList *list);

void _ListAndContentsFree(List *list);
void _LockingListAndContentsFree(LockingList *list);
// NOLINTEND(*-reserved-identifier)


/**
 * Create a new list of a given size, with zeroed data
 * @param list A pointer to the list object to initialize
 * @param Type A value indicating what type of data the list is storing
 */
#define ListInit(list, Type, ...) \
	_Generic((list), \
			List: _ListInit, \
			LockingList: _LockingListInit, \
			SortedList: _SortedListInit)(&(list), sizeof(Type) __VA_OPT__(, __VA_ARGS__))

/**
 * Create a copy of a list
 * @param oldList The list to make a copy of
 * @param newList The list to copy into
 */
#define ListCopy(oldList, newList) \
	({ \
		static_assert(__builtin_types_compatible_p(typeof(oldList), typeof(newList))); \
		_Generic((oldList), \
				List: _ListCopy((List *)&(oldList), (List *)&(newList)), \
				LockingList: _LockingListCopy((LockingList *)&(oldList), (LockingList *)&(newList)), \
				SortedList: _ListCopy((List *)&(oldList), (List *)&(newList))); \
	})

/**
 * Append an item to the list
 * @param list List to append to
 * @param data Data to append
 */
#define ListAdd(list, data) \
	_Generic((list), List: _ListAdd, LockingList: _LockingListAdd, SortedList: _SortedListAdd)(&(list), (void *)&(data))

/**
 * Set an item in the list by index
 * @param list List to set the item in
 * @param index Index to set
 * @param value Value to set at the index
 */
#define ListSet(list, index, value) \
	_Generic((list), List: _ListSet, LockingList: _LockingListSet)(&(list), (index), (void *)&(data))

/**
 * Remove an item from the list by index
 * @param list List to remove from
 * @param index Index to remove
 */
#define ListRemoveAt(list, index) \
	(assert((size_t)(index) < (list).length), \
	 _Generic((list), \
			 List: _ListRemoveAt((List *)&(list), (index)), \
			 LockingList: _LockingListRemoveAt((LockingList *)&(list), (index)), \
			 SortedList: _ListRemoveAt((List *)&(list), (index))))

/**
 * Insert an item after a node
 * @param list List to insert into
 * @param index Index to insert after
 * @param data Data to insert
 */
#define ListInsertAfter(list, index, data) \
	_Generic((list), List: _ListInsertAfter, LockingList: _LockingListInsertAfter)(&(list), (index), (void *)&(data))

/**
 * Get an item from the list by index
 * @param list The list to get from
 * @param index The index to get
 * @param Type The type of data stored in the list
 */
#define ListGet(list, index, Type) \
	(((Type *)_Generic((list), List: (list), LockingList: (list), SortedList: (list)) \
			  .data)[(assert((size_t)(index) < (list).length), (index))])

/**
 * Find an item in the list
 * @param list List to search
 * @param data Data to search for
 * @return Index of the item in the list, -1 if not found
 */
#define ListFind(list, data) \
	_Generic((list), List: _ListFind, LockingList: _LockingListFind, SortedList: _SortedListFind)(&(list), \
																								  (void *)&(data))

/**
 * Lock the mutex on a list
 * @param list The list to lock
 */
#define ListLock(list) _Generic((list), LockingList: _ListLock)(&(list))

/**
 * Unlock the mutex on a list
 * @param list The list to unlock
 */
#define ListUnlock(list) _Generic((list), LockingList: _ListUnlock)(&(list))

/**
 * Clear all items from the list
 * @param list List to clear
 * @warning This does not free the data in the list
 */
#define ListClear(list) \
	_Generic((list), \
			List: _ListClear((List *)&(list)), \
			LockingList: _LockingListClear((LockingList *)&(list)), \
			SortedList: _ListClear((List *)&(list)))

/**
 * Sets all items in the list to zero
 * @param list List to zero
 * @warning This does not free the data in the list
 */
#define ListZero(list) _Generic((list), List: _ListZero, LockingList: _LockingListZero)(&(list))

/**
 * Free the list structure
 * @param list List to free
 * @warning This does not free the data in the list, but does free the data pointer
 */
#define ListFree(list) \
	_Generic((list), \
			List: _ListFree((List *)&(list)), \
			LockingList: _LockingListFree((LockingList *)&(list)), \
			SortedList: _ListFree((List *)&(list)))

/**
 * Free the data stored in the list
 * @param list List to free
 */
#define ListFreeOnlyContents(list) \
	_Generic((list), \
			List: _ListFreeOnlyContents((List *)&(list)), \
			LockingList: _LockingListFreeOnlyContents((LockingList *)&(list)), \
			SortedList: _ListFreeOnlyContents((List *)&(list)))

/**
 * Free the list structure and the data in the list
 * @param list List to free
 * @warning If the data is a struct, any pointers in the struct will not be freed, just the struct itself
 */
#define ListAndContentsFree(list) \
	_Generic((list), \
			List: _ListAndContentsFree((List *)&(list)), \
			LockingList: _LockingListAndContentsFree((LockingList *)&(list)), \
			SortedList: _ListAndContentsFree((List *)&(list)))

#endif //GAME_LIST_H
