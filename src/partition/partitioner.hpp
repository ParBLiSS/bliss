/**
 * @file		Partitioner.hpp
 * @ingroup bliss::partition
 * @author	tpan
 * @brief   contains several class that provide different logic for partitioning a range
 * @details contains block, cyclic, and demand driven (THREAD SAFE) partitioners
						logic implementation uses comparison to avoid overflows and implicit casting where needed.
 *
 * Copyright (c) 2014 Georgia Institute of Technology.  All Rights Reserved.
 *
 * TODO add License
 */
#ifndef PARTITIONER_HPP_
#define PARTITIONER_HPP_

#include <stdexcept>
#include <atomic>
#include <config.hpp>
#include <partition/range.hpp>

namespace bliss
{
  /**
   * @namespace partition
   */
  namespace partition
  {

    /**
     * @class			Partitioner
     * @brief     base class to partition a range.
     * @details   operates without knowledge of the data that the range refers to,  so no "search" type of partitioning
     *
     *            PARTIONER essentially divide the RANGE into CHUNKS, then assign PARTITION id to the chunks.
     *            each partition consists of 0 or more chunks.  chunks in a partition share the same (implicit) partition id.
     *
     *            Simple types (essentially functinoids), use default copy constructor and assignment operator.  no need to move versions.
     *            uses the Curiously Recursive Template Pattern to enforce consistent API from base to derived classes and to provide a path
     *            for deriving new classes

     *            uses default constructor, copy constructor, and move constructor.
     *
     *            Works for continuous value ranges.
     *
     * @note      The purpose of partitioning is to divide the data for computation.  Partitioner will therefore divide the source range,
     *            including the ghost region (see range class documentation), into equal parts.
     *            A new ghost region length can be specified during partitioning.  When ghost region length is specified, all subranges
     *            from the partitioner, except the last subrange, will have its "ghost" variable set to the ghost region size.  The last
     *            subrange has ghost region of size 0.
     *
     * @tparam  Range     Range object to be partitioned.
     * @tparam  Derived   Subclass, used to specialize the Base class for deciding the private impl of public api.
     */
    template<typename Range, typename Derived>
    class Partitioner {
      protected:

        /**
         * @typedef RangeValueType
         * @brief   value type used by the range's start/end/overlap
         */
        using RangeValueType = typename Range::ValueType;

        /**
         * @typedef chunkSizeType
         * @brief   value type used for chunkSize.  if integral ype for valueType, then size_t.  else (floating point) same as RangeValueType
         */
        using ChunkSizeType = typename std::conditional<std::is_integral<RangeValueType>::value,
                                                        size_t, RangeValueType>::type;

        /**
         * @var src
         * @brief   range to be partitioned
         */
        Range src;

        /**
         * @var end
         * @brief   end of the range to be partitioned.
         */
        Range end;

        /**
         * @var nPartitions
         * @brief   number of partitions to divide the range into.  chunks in a partition have the same partition id.  1 or more partition is assigned to a caller.
         */
        size_t nPartitions;

        /**
         * @var chunkSize
         * @brief   size of each partition.  computed for block partitioner, and user specified for cyclic and demand driven.
         * @note    this value excludes ghost region size.
         */
        ChunkSizeType chunkSize;

        /**
         * @var     ghostSize
         * @brief   the ghostSize that each subrange should contain.  Parameter of this partitioning.
         */
        RangeValueType ghostSize;

        /**
         * @brief   computes the number of chunks in a src range.  for Integral type only.
         * @details computed using the nonoverlapping part of each subrange, length in chunkSize
         * @tparam  R  range type.  used for enable_if.
         * @return  number of chunks in a range.
         */
        template <typename R = Range>
        typename std::enable_if<std::is_integral<typename R::ValueType>::value, size_t>::type computeNumberOfChunks() {
          return std::floor((this->chunkSize - 1 + this->src.size()) / this->chunkSize);
        }
        /**
         * @brief   computes the number of chunks in a src range, excluding the ghost region.  for floating type only.
         * @tparam  R  range type.  used for enable_if.
         * @return  number of chunks in a range, based on chunkSize.
         */
        template <typename R = Range>
        typename std::enable_if<std::is_floating_point<typename R::ValueType>::value, size_t>::type computeNumberOfChunks() {
          return this->src.size() / this->chunkSize;
        }

        /**
         * @brief   internal function to compute the range for a chunk of the parent range.
         * @param[in/out] r     the range to modify.  should be initialized with pr.start or pr.start+rem (for block partitioner)
         * @param[in]     pr    the parent range
         * @param chunkId       the chunk to get the range for
         * @param chunk_size    the size of the chunk        (precomputed or user specified)
         * @param ghost_size    the size of the ghost region (user specified)
         */
        void computeRangeForChunkId(Range & r, const Range & pr, const size_t& chunkId, const ChunkSizeType& chunk_size, const RangeValueType& ghost_size) {
          // compute start
          r.start += static_cast<RangeValueType>(chunkId) *  chunk_size;

          if (pr.end - r.start > static_cast<RangeValueType>(chunk_size) + ghost_size) {
            // far away from parent range's end
            r.end = r.start + static_cast<RangeValueType>(chunk_size) + ghost_size;
            r.ghost = ghost_size;
          } else if (pr.end - r.start <= static_cast<RangeValueType>(chunk_size)) {
            // parent range's end is within the target subrange's chunk region
            r.end = pr.end;
            r.ghost = 0;
          } else {
            // parent range's end is within the target subrange's ghost region
            r.end = pr.end;
            r.ghost = pr.end - r.start - static_cast<RangeValueType>(chunk_size);
          }
        }

      public:
        /**
         * @brief default destructor
         */
        virtual ~Partitioner() {};

        /**
         * @brief configures the partitioner with the source range, number of partitions.
         * @param _src          range object to be partitioned.
         * @param _nPartitions the number of partitions to divide this range into
         * @param _chunkSize   the size of each partition.  default is 0 (in subclasses), computed later.
         * @param _ghostSize   the size of the overlap ghost region.
         */
        void configure(const Range &_src, const size_t &_nPartitions, const ChunkSizeType &_chunkSize, const RangeValueType &_ghostSize) {

          if (_nPartitions == 0)
            throw std::invalid_argument("ERROR: partitioner c'tor: nPartitions is 0");
          nPartitions = _nPartitions;

          if (_ghostSize < 0)
            throw std::invalid_argument("ERROR: partitioner c'tor: ghostSize is < 0");
          ghostSize = _ghostSize;

          if (_chunkSize < 0)
            throw std::invalid_argument("ERROR: partitioner c'tor: chunkSize is < 0");
          chunkSize = _chunkSize;

          src = _src;
          src.ghost = 0;  // partitioning, so there is no ghost region (overlap)
          end = src;
          end.start = end.end;


        }

        /**
         * @brief get the next sub range (chunk) within a partition.
         * @details the function calls the specific subclass implementation.
         * @param partId    the id of the partition to retrieve
         * @return          range object containing the start and end of the next chunk in the partition.
         */
        inline Range& getNext(const size_t &partId) {
          // both non-negative.
          if (partId >= nPartitions)
            throw std::invalid_argument("ERROR: partitioner.getNext called with partition id larger than number of partitions.");

          return static_cast<Derived*>(this)->getNextImpl(partId);
        }

        /**
         * @brief   resets the partitioner to the initial condition.
         * @details useful for cyclic and demand-driven partitioner, where there is an internal counter pointing to the next chunk range.
         */
        void reset() {
          static_cast<Derived*>(this)->resetImpl();
        }


    };


    /**
     * @class BlockPartitioner
     * @brief A partition that creates equal sized partitions during division of the range.
     * @details   Each partition has a size that's guaranteed to be within 1 of each other in size.
     *            inherits from base Partitioner using CRTP, and implements the detail impl for getNext and reset
     * @tparam Range  the range type used.
     */
    template<typename Range>
    class BlockPartitioner : public Partitioner<Range, BlockPartitioner<Range> >
    {
      protected:
        /**
         * @typedef BaseClassType
         * @brief   the superclass type.
         */
        using BaseClassType = Partitioner<Range, BlockPartitioner<Range> >;

        /**
         * @var curr
         * @brief the result range.  cached here for speed
         */
        Range curr;

        /**
         * @var done
         * @brief boolean indicating that partition has already been done.
         */
        bool done;

        /**
         * @typedef ValueType
         */
        using RangeValueType = typename BaseClassType::RangeValueType;

        /**
         * @typedef ChunkSizeType
         */
        using ChunkSizeType = decltype(std::declval<Range>().size());

        /**
         * @var rem
         * @brief the left-over that is to be spread out to the first rem partitions.
         */
        RangeValueType rem;


      public:

        /**
         * @brief default destructor
         */
        virtual ~BlockPartitioner() {};

        /**
         * @brief configures the partitioner with the source range, number of partitions
         * @param _src          range object to be partitioned.
         * @param _nPartitions the number of partitions to divide this range into
         * @param _chunkSize  size of each chunk.  here it should be 0 so they will be auto computed.
         * @param _ghostSize   the size of the overlap ghost region.
         *
         */
        void configure(const Range &_src, const size_t &_nPartitions,
                       const ChunkSizeType &_chunkSize = 0, const RangeValueType &_ghostSize = 0) {
          this->BaseClassType::configure(_src, _nPartitions, _chunkSize, _ghostSize);

          // compute the size of partitions and number of partitions that have size larger than others by 1.
          // if chunkSize to be 0 - rem would be more than 0, so the first rem partitions will have size of 1.

          // first compute the chunkSize excluding the ghost region
          this->chunkSize = this->src.size() / static_cast<ChunkSizeType>(this->nPartitions);

          // next figure out the remainder using chunkSize that excludes ghost region size.
          // not using modulus since we RangeValueType may be float.
          rem = this->src.size() - this->chunkSize * static_cast<ChunkSizeType>(this->nPartitions);

          resetImpl();
        }

        /**
         * @brief       get the next chunk in the partition.  for BlockPartition, there is only 1 chunk.  INTEGRAL Type Only
         * @details     first call to this function returns the partitioned range.  subsequent calls get the end object (start == end)
         *              to call again and get meaningful result, reset() must be called.
         *              no need to really store much - everything can be recomputed relatively fast.
         * @param partId   partition id for the sub range.
         * @return      range of the partition
         */
        template<typename R = Range>
        typename std::enable_if<std::is_integral<typename R::ValueType>::value, Range>::type& getNextImpl(const size_t& partId) {
          // param validation already done.

          // if previously computed, then return end object, signalling no more chunks.
          if (done) return this->end;  // can only call this once.

          // if just 1 partition, return.
          if (this->nPartitions == 1) return this->src;

          // compute the subrange's start and end, spreading out the remainder to the first rem chunks/partitions.
          ChunkSizeType cs = this->chunkSize;
          if (partId < rem)
          {
            // each chunk with partition id < rem gets 1 extra.
            cs += 1;
            curr.start = this->src.start;
          }
          else
          {
            // first rem chunks have 1 extra element than chunkSize.  the rest have chunk size number of elements.
            curr.start = this->src.start + rem;
          }

          // compute the new range
          computeRangeForChunkId(curr, this->src, partId, cs, this->ghostSize);

          // block partitioning only allows calling this once.
          done = true;
          return curr;
        }


        /**
         * @brief       get the next chunk in the partition.  for BlockPartition, there is only 1 chunk.  Floating Point only
         * @details     first call to this function returns the partitioned range.  subsequent calls get the end object (start == end)
         *              to call again and get meaningful result, reset() must be called.
         *              no need to really store much - everything can be recomputed relatively fast.
         * @param partId   partition id for the sub range.
         * @return      range of the partition
         */
        template<typename R = Range>
        typename std::enable_if<std::is_floating_point<typename R::ValueType>::value, Range>::type& getNextImpl(const size_t& partId) {
          // param validation already done.

          // if previously computed, then return end object, signalling no more chunks.
          if (done) return this->end;  // can only call this once.

          // if just 1 partition, return.
          if (this->nPartitions == 1) return this->src;

          // compute the subrange's start and end, spreading out the remainder to the first rem chunks/partitions.
          computeRangeForChunkId(curr, this->src, partId, this->chunkSize, this->ghostSize);

          done = true;
          return curr;
        }


        /**
         * @brief reset size to full range, mark the done to false.
         */
        void resetImpl() {
          curr = this->src;
          done = false;
        }

    };

    /**
     * @class CyclicPartitioner
     * @brief cyclically partition the range into chunkSize blocks.
     *        uses CRTP pattern to enforce consistent API.
     * @tparam Range  type of range to be partitioned.
     */
    template<typename Range>
    class CyclicPartitioner : public Partitioner<Range, CyclicPartitioner<Range> >
    {

      protected:
        /**
         * @typedef BaseClassType
         * @brief   the superclass type.
         */
        using BaseClassType = Partitioner<Range, CyclicPartitioner<Range> >;

        /**
         * @typedef ChunkSizeType
         */
        using ChunkSizeType = typename BaseClassType::ChunkSizeType;


        /**
         * @var done
         * @brief An array of "state", one for each partition.
         */
        uint8_t *state;

        /**
         * @var curr
         * @brief An array of "curr" subranges, one for each partition.  updated as getNext is called.
         */
        Range *curr;

        /**
         * @var nChunks
         * @brief   number of chunks in the src range.
         * @details by comparing nChunks to nPartitions (user specified) we can tell if some partitions will not be receiving a chunk.
         *          this is a mechanism to deal with nPartition that are too large.
         */
        size_t nChunks;

        /**
         * @var stride
         * @brief size of a stride, which is chunkSize *  number of partitions (stride for successive call to getNext)
         */
        ChunkSizeType stride;

				/**
				 * @var BEFORE
         * @brief	static variable indicating we have not yet called "getNext"
			   */
        static const uint8_t BEFORE = 0;
				
				/**
				 * @var DURING
         * @brief	static variable indicating we have called getNext at least once, but are not at the end yet
			   */
        static const uint8_t DURING = 1;
        
        /**
				 * @var BEFORE
         * @brief	static variable indicating we have reached the end of the range during calls to getNext
			   */
        static const uint8_t AFTER = 2;

      public:

        /**
         * @brief default destructor.  cleans up arrays for callers.
         */
        virtual ~CyclicPartitioner() {
          if (state) delete [] state;
          if (curr) delete [] curr;
        }

        /**
         * @brief configures the partitioner with the source range, number of partitions
         * @param _src          range object to be partitioned.
         * @param _nPartitions the number of partitions to divide this range into
         * @param _chunkSize    Size of the chunks
         *
         */
        void configure(const Range &_src, const size_t &_nPartitions, const ChunkSizeType &_chunkSize) {
          this->BaseClassType::configure(_src, _nPartitions, _chunkSize);

          // get the maximum number of chunks in the source range.
          // TODO: make this compatible with floating point.
          nChunks = BaseClassType::computeNumberOfChunks();

          // stride:  if there are less chunks than partition, we can only walk through once, so stride is src size.
          stride = (nChunks > this->nPartitions) ? 
          	this->chunkSize * static_cast<ChunkSizeType>(this->nPartitions) : 
          	this->src.size();

          state = new uint8_t[std::min(nChunks, this->nPartitions)];
          curr = new Range[std::min(nChunks, this->nPartitions)];

          resetImpl();
        }

        /**
         * @brief       get the next chunk in the partition.  for CyclickPartition, keep getting until done.
         * @details     each call to this function gets the next chunk belonging to this partition.
         *              for cyclicPartitioner, the chunks are separated by chunkSize * nPartitions
         * @param partId   partition id for the sub range.
         * @return      range of the partition
         */
        inline Range& getNextImpl(const size_t& partId) {

          /// if nChunks < nPartitions, then each partId will only get a chunk once.
          // in that case, if partId is >= nChunks, then end is returned.
          if (partId >= nChunks) return this->end;

          // if this partition is done, return the last entry (end)
          if (state[partId] == AFTER)  return this->end;

          /// first iteration, use initialized value
          if (state[partId] == BEFORE) {
            state[partId] = DURING;
            return curr[partId];
          }
          // else not the first and not last, so increment.

          /// comparing to amount of range available - trying to avoid data type overflow.

          // shift starting position by stride length
          if (this->src.end - curr[partId].start > stride) {
            // has room.  so shift
            curr[partId].start += stride;
          } else {
            state[partId] = AFTER;         // if outside range, done
            return this->end;
          }

          // shift end position by stride length - start is NOT outside range.
          // overlap is already part of curr[partId].end set from resetImpl.
          // use comparison to avoid overflow and implicit conversion.
          if (this->src.end - curr[partId].end > stride) {
            // end is not outside parent range.
            curr[partId].end += stride;
          } else {
            // end is outside parent range
            curr[partId].ghost = std::min(this->)
            curr[partId].end = this->src.end;
            state[partId] = AFTER;
          }

          return curr[partId];
        }

        /**
         * @brief resets the partitioner by resetting the internal done and subrange arrays.
         * @details this function also serves to initialize the arrays.
         */
        void resetImpl()
        {
          size_t s = std::min(nChunks, this->nPartitions);
          for (size_t i = 0; i < s; ++i) {
            state[i] = BEFORE;
            curr[i].start = this->src.start + static_cast<ChunkSizeType>(i) * this->chunkSize;
            // if it's the last chunk (s-1), and number of chunks is less or equal to number of partitions
            // so if i = nChunks - 1
            // use comparison to avoid overflow and implicit conversion.
            if (i == nChunks -1) {
              curr[i].end = this->src.end;
              curr[i].ghost = 0;
            } else {
              curr[i].end = (curr[i].start > this->src.end - this->chunkSize) ? this->src.end :

            }
            curr[i].end = (i == nChunks - 1) ?
                this->src.end :
                curr[i].start + this->chunkSize + this->src.ghost;
            curr[i].ghost = this->src.ghost;

//            printf ("values start-end: %lu %lu\n", curr[i].start, curr[i].end);

          }
        }


    };


    /**
     * @class DemandDrivenPartitioner
     * @brief a partitioner that assigns chunks to partition in the order that the getNext function is called.
     * @tparam Range  type of the range object to be partitioned.
     */
    template<typename Range>
    class DemandDrivenPartitioner : public Partitioner<Range, DemandDrivenPartitioner<Range> >
    {

      protected:
        /**
         * @typedef BaseClassType
         * @brief   the superclass type.
         */
        using BaseClassType = Partitioner<Range, DemandDrivenPartitioner<Range> >;

        /**
         * @typedef ChunkSizeType
         */
        using ChunkSizeType = typename BaseClassType::ChunkSizeType;

        /**
         * @var chunkOffset
         * @brief the offset for the next chunk to be returned.  TREHAD SAFE
         */
        std::atomic<ChunkSizeType> chunkOffset;

        /**
         * @var id of current chunk
         * @brief  used for tracking the chunk begin returned.  useful if nChunk < nPartition
         */
        std::atomic<size_t> chunkId;

        /**
         * @var nChunks
         * @brief   number of chunks in the src range.
         * @details by comparing nChunks to nPartitions (user specified) we can tell if some partitions will not be receiving a chunk.
         *          this is a mechanism to deal with nPartition that are too large.
         */
        size_t nChunks;

        /**
         * @var curr
         * @brief internal cache of subrange objects that each partition last retrieved.
         */
        Range *curr;

        /**
         * @done
         * @brief boolean indicating that the range has been exhausted by the traversal.  THREAD SAFE
         */
        std::atomic<bool> done;

        /**
         * @typedef RangeValueType
         * @brief   type for the start/end/overlap
         */
        using RangeValueType = typename BaseClassType::RangeValueType;


        /**
         * @brief     internal method to atomically increment the chunkOffset.  this is the version for integral type
         * @return    old value.  chunkOffset incremented.
         */
        template<typename T = ChunkSizeType>
        typename std::enable_if<std::is_integral<T>::value, RangeValueType>::type getNextOffset() {
          return chunkOffset.fetch_add(this->chunkSize, std::memory_order_acq_rel);
        }
        /**
         * @brief     internal method to atomically increment the chunkOffset for floating point types
         * @details   std::atomics does not support fetch_add on non-integral types.
         * @return    old value.  chunkOffset incremented.
         */
        template<typename T = ChunkSizeType>
        typename std::enable_if<std::is_floating_point<T>::value, RangeValueType>::type getNextOffset() {
          RangeValueType origval = chunkOffset.load(std::memory_order_consume);
          RangeValueType newval;
          do {
            newval = origval + this->chunkSize;
          } while (!chunkOffset.compare_exchange_weak(origval, newval, std::memory_order_acq_rel, std::memory_order_acquire));
          return origval;
        }

      public:
        /**
         * @brief default destructor
         */
        virtual ~DemandDrivenPartitioner() {
          if (curr != nullptr) delete [] curr;
        }


        /**
         * @brief configures the partitioner with the source range, number of partitions
         * @param _src          range object to be partitioned.
         * @param _nPartitions  the number of partitions to divide this range into
         * @param _chunkSize  size of each chunk for the partitioning.
         *
         */
        void configure(const Range &_src, const size_t &_nPartitions, const ChunkSizeType &_chunkSize) {
          this->BaseClassType::configure(_src, _nPartitions, _chunkSize);

          // get the maximum number of chunks in the source range.
          // TODO: make this compatible with floating point.
          nChunks = BaseClassType::computeNumberOfChunks();

          // if there are more partitions than chunks, then the array represents mapping from chunkId to subrange
          // else if there are more chunks than partitions, then the array represents the most recent chunk assigned to a partition.
          curr = new Range[std::min(nChunks, this->nPartitions)];

          resetImpl();
        };




        /**
         * @brief       get the next chunk in the partition.  for DemandDrivenPartition, keep getting until done.
         * @details     each call to this function gets the next chunk in the range and assign it to a partition.
         *              the sequence of partition ids depends on call order.
         *
         *              ASSUMPTION:  no 2 concurrent callers will be requesting the same partition Id.
         *
         *              THREAD SAFE
         * @param partId   partition id for the sub range.
         * @return      range of the partition
         */
        inline Range& getNextImpl(const size_t& partId) {
          // all done, so return end
          if (done.load(std::memory_order_consume)) return this->end;

          // call internal function (so integral and floating point types are handled properly)
          RangeValueType s = getNextOffset<ChunkSizeType>();

          /// identify the location in array to store the result
          // first get the id of the chunk we are returning.
          size_t id = chunkId.fetch_add(1, std::memory_order_acq_rel);
          // if there are more partitions than chunks, then the array represents mapping from chunkId to subrange
          // else if there are more chunks than partitions, then the array represents the most recent chunk assigned to a partition.
          id = (nChunks < this->nPartitions ? id : partId);

          if (s >= this->src.end) {
            done.store(true, std::memory_order_release);
            return this->end;
          } else {

            curr[id].start = s;
                // use comparison to avoid overflow.
            curr[id].end = (this->src.end - s > this->chunkSize + this->src.ghost) ?
                s + this->chunkSize + this->src.ghost : this->src.end;

            return curr[id];
          }
        }

        /**
         * @brief resets the partitioner by resetting the internal subrange arrays.  also reset the offset to the start of the src range, chunk Id, and "done".
         * @details this function also serves to initialize the subrange array.
         */
        void resetImpl() {
          // these 3 calls probably should be synchronized together.
          chunkOffset.store(this->src.start, std::memory_order_release);
          chunkId.store(0, std::memory_order_release);
          done.store(false, std::memory_order_release);

          size_t s = std::min(this->nPartitions, nChunks);
          for (size_t i = 0; i < s; ++i) {
            curr[i] = Range(this->src.start, this->src.start, this->src.ghost);
          }
        }


    };


  } /* namespace partition */
} /* namespace bliss */

#endif /* PARTITIONER_HPP_ */
