// OpenSTA, Static Timing Analyzer
// Copyright (c) 2024, Parallax Software, Inc.
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <cstring> // memcpy
#include <vector>

#include "ObjectId.hh"
#include "Error.hh"

namespace sta {

template <class TYPE>
class ArrayBlock;

// Array tables allocate arrays of objects in blocks and use 32 bit IDs to
// reference the array. Paging performance is improved by allocating
// blocks instead of individual arrays, and object sizes are reduced
// by using 32 bit references instead of 64 bit pointers.
// They are similar to ObjectTables but do not support delete/destroy or
// reclaiming deleted arrays.

template <class TYPE>
class ArrayTable
{
public:
  ArrayTable();
  ~ArrayTable();
  void make(uint32_t count,
	    TYPE *&array,
	    ObjectId &id);
  void destroy(ObjectId id,
               uint32_t count);
  // Grow as necessary and return pointer for id.
  TYPE *ensureId(ObjectId id);
  TYPE *pointer(ObjectId id);
  TYPE &ref(ObjectId id);
  size_t size() const { return size_; }
  void clear();

  static constexpr int idx_bits = 7;
  static constexpr int block_size = (1 << idx_bits);
  static constexpr int block_id_max = 1 << (object_id_bits - idx_bits);

private:
  ArrayBlock<TYPE> *makeBlock(uint32_t size);
  void pushBlock(size_t size);
  void deleteBlocks();

  size_t size_;
  // Block index of free block (blocks_[size - 1]).
  BlockIdx free_block_idx_;
  // Index of next free object in free_block_idx_.
  ObjectIdx free_idx_;
  std::vector<ArrayBlock<TYPE>> blocks_;
  std::vector<ArrayBlock<TYPE>> prev_blocks_;
  // Linked list of free arrays indexed by array size.
  std::vector<ObjectId> free_list_;
  static constexpr ObjectId idx_mask_ = block_size - 1;
};

template <class TYPE>
ArrayTable<TYPE>::ArrayTable() :
  size_(0),
  free_block_idx_(block_idx_null),
  free_idx_(object_idx_null)
{
  blocks_.reserve(1024);
}

template <class TYPE>
ArrayTable<TYPE>::~ArrayTable()
{
  deleteBlocks();
}

template <class TYPE>
void
ArrayTable<TYPE>::deleteBlocks()
{
  for (size_t i = 0; i < blocks_.size(); i++)
    blocks_[i].free();
  blocks_.clear();
}

template <class TYPE>
void
ArrayTable<TYPE>::make(uint32_t count,
		       TYPE *&array,
		       ObjectId &id)
{
  // Check the free list for a previously destroyed array with the right size.
  if (count < free_list_.size()
      && free_list_[count] != object_id_null) {
    id = free_list_[count];
    array = pointer(id);

    ObjectId *head = reinterpret_cast<ObjectId*>(array);
    free_list_[count] = *head;
  }
  else {
    ArrayBlock<TYPE> *block = blocks_.size() ? &blocks_[free_block_idx_] : nullptr;
    if ((free_idx_ == object_idx_null
         && free_block_idx_ == block_idx_null)
        || free_idx_ + count >= block->size()) {
      uint32_t size = block_size;
      if (blocks_.size() == 0
          // First block starts at idx 1.
          && count > block_size - 1)
        size = count + 1;
      else if (count > block_size)
        size = count;
      block = makeBlock(size);
    }
    // makeId(free_block_idx_, idx_bits)
    id = (free_block_idx_ << idx_bits) + free_idx_;
    array = block->pointer(free_idx_);
    free_idx_ += count;
  }
  size_ += count;
}

template <class TYPE>
ArrayBlock<TYPE> *
ArrayTable<TYPE>::makeBlock(uint32_t size)
{
  BlockIdx block_idx = blocks_.size();
  pushBlock(size);
  free_block_idx_ = block_idx;
  // ObjectId zero is reserved for object_id_null.
  free_idx_ = (block_idx > 0) ? 0 : 1;
  return &blocks_[block_idx];
}

template <class TYPE>
void
ArrayTable<TYPE>::pushBlock(size_t size)
{
  blocks_.push_back(ArrayBlock<TYPE>(size));

  if (blocks_.size() >= block_id_max)
    criticalError(223, "max array table block count exceeded.");
  if (blocks_.size() == blocks_.capacity()) {
    prev_blocks_.reserve(blocks_.capacity() * 1.5);
    const auto blocks_mid = blocks_.begin() + prev_blocks_.size();
    std::copy(blocks_.begin(), blocks_mid, prev_blocks_.begin());
    prev_blocks_.insert(prev_blocks_.end(), blocks_mid, blocks_.end());
    // Preserve block array for other threads to reference.
    // Swap the vectors while keeping blocks_ valid.
    // vector::swap is usually basically the same,
    // but a naive implementation might temporarily set blocks_ to null.
    const size_t vec_size = sizeof(blocks_);
    std::uint8_t tmp[vec_size];
    std::memcpy(&tmp, static_cast<void*>(&blocks_), vec_size);
    std::memcpy(static_cast<void*>(&blocks_), static_cast<void*>(&prev_blocks_), vec_size);
    std::memcpy(static_cast<void*>(&prev_blocks_), &tmp, vec_size);
  }
}

template <class TYPE>
void
ArrayTable<TYPE>::destroy(ObjectId id,
                          uint32_t count)
{
  if (count >= free_list_.size())
    free_list_.resize(count + 1);
  TYPE *array = pointer(id);
  // Prepend id to the free list.
  ObjectId *head = reinterpret_cast<ObjectId*>(array);
  *head = free_list_[count];
  free_list_[count] = id;
  size_ -= count;
}

template <class TYPE>
TYPE *
ArrayTable<TYPE>::pointer(ObjectId id)
{
  if (id == object_id_null)
    return nullptr;
  else {
    BlockIdx blk_idx = id >> idx_bits;
    ObjectIdx obj_idx = id & idx_mask_;
    return blocks_[blk_idx].pointer(obj_idx);
  }
}

template <class TYPE>
TYPE *
ArrayTable<TYPE>::ensureId(ObjectId id)
{
  BlockIdx blk_idx = id >> idx_bits;
  ObjectIdx obj_idx = id & idx_mask_;
  // Make enough blocks for blk_idx to be valid.
  for (BlockIdx i = blocks_.size(); i <= blk_idx; i++) {
    pushBlock(block_size);
  }
  return blocks_[blk_idx].pointer(obj_idx);
}

template <class TYPE>
TYPE &
ArrayTable<TYPE>::ref(ObjectId id)
{
  if (id == object_id_null)
    criticalError(222, "null ObjectId reference is undefined.");

  BlockIdx blk_idx = id >> idx_bits;
  ObjectIdx obj_idx = id & idx_mask_;
  return blocks_[blk_idx].ref(obj_idx);
}

template <class TYPE>
void
ArrayTable<TYPE>::clear()
{
  deleteBlocks();
  size_ = 0;
  free_block_idx_ = block_idx_null;
  free_idx_ = object_idx_null;
  free_list_.clear();
}

////////////////////////////////////////////////////////////////

template <class TYPE>
class ArrayBlock
{
public:
  ArrayBlock() = default;
  ArrayBlock(uint32_t size);
  uint32_t size() const { return size_; }
  TYPE &ref(ObjectIdx idx) { return objects_[idx]; }
  TYPE *pointer(ObjectIdx idx) { return &objects_[idx]; }
  void free() { delete[] objects_; }

private:
  uint32_t size_;
  TYPE *objects_;
};

template <class TYPE>
ArrayBlock<TYPE>::ArrayBlock(uint32_t size) :
  size_(size),
  objects_(new TYPE[size])
{
}

} // Namespace
